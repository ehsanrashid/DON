#include "Thread.h"

#include "Position.h"
#include "MoveGenerator.h"
#include "Searcher.h"
#include "Endgame.h"
#include "UCI.h"

Threading::ThreadPool Threadpool; // Global ThreadPool

namespace Threading {

    using namespace std;
    using namespace MoveGen;
    using namespace Searcher;

    // Helpers to launch a thread after creation and joining before delete. Must be
    // outside Thread c'tor and d'tor because object must be fully initialized
    // when start_routine (and hence virtual idle_loop) is called and when joining.
    template<class T>
    T* new_thread ()
    {
        thread *th = new T;
        *th = thread (&T::idle_loop, (T*)th); // Will go to sleep
        return (T*)th;
    }

    void delete_thread (ThreadBase *th)
    {
        th->mutex.lock ();
        th->alive = false;   // Search must be already finished
        th->mutex.unlock ();

        th->notify_one ();
        th->join ();         // Wait for thread termination
        delete th;
        th = nullptr;
    }
    
    // explicit template instantiations
    // --------------------------------
    template TimerThread* new_thread<TimerThread> ();
    // ------------------------------------

    // ThreadBase::notify_one () wakes up the thread when there is some work to do
    void ThreadBase::notify_one ()
    {
        unique_lock<Mutex> lk (mutex);
        sleep_condition.notify_one ();
    }

    // ThreadBase::wait_for() set the thread to sleep until condition turns true
    void ThreadBase::wait_for (volatile const bool &condition)
    {
        unique_lock<Mutex> lk (mutex);
        sleep_condition.wait (lk, [&]{ return condition; });
    }

    // ------------------------------------

    // Thread::Thread() makes some init but does not launch any execution thread that
    // will be started only when c'tor returns.
    Thread::Thread ()
        : ThreadBase ()
    {
        active_pos          = nullptr;
        max_ply             = 0;
        active_splitpoint   = nullptr;
        splitpoint_count    = 0;
        searching           = false;
        index               = Threadpool.size (); // Starts from 0
    }

    // Thread::cutoff_occurred() checks whether a beta cutoff has occurred in the
    // current active splitpoint, or in some ancestor of the splitpoint.
    bool Thread::cutoff_occurred () const
    {
        for (SplitPoint *sp = active_splitpoint; sp != nullptr; sp = sp->parent_splitpoint)
        {
            if (sp->cut_off) return true;
        }
        return false;
    }

    // Thread::can_join() checks whether the thread is available to join the split
    // point 'sp'. An obvious requirement is that thread must be idle. With more than
    // two threads, this is not sufficient: If the thread is the master of some split
    // point, it is only available as a slave for the split points below his active
    // one (the "helpful master" concept in YBWC terminology).
    bool Thread::can_join (const SplitPoint *sp) const
    {
        if (searching) return false;

        // Make a local copy to be sure it doesn't become zero under our feet while
        // testing next condition and so leading to an out of bounds access.
        const u08 count = splitpoint_count;

        // No split points means that the thread is available as a slave for any
        // other thread otherwise apply the "helpful master" concept if possible.
        return 0 == count || splitpoints[count - 1].slaves_mask.test (sp->master->index);
    }


    // Thread::split<>() does the actual work of distributing the work at a node between several available threads.
    // Almost always allocate a slave, only in the rare case of a race (< 2%) this is not true.
    // SplitPoint object is initialized with all the data that must be copied to the helper threads
    // and then helper threads are told that they have been assigned work. This causes them to instantly
    // leave their idle loops and call search<>().
    // When all threads have returned from search() then split() returns.
    void Thread::split (Position &pos, Stack *ss, Value alpha, Value beta, Value &best_value, Move &best_move,
                        Depth depth, u08 legal_count, MovePicker &movepicker, NodeT node_type, bool cut_node)
    {
        assert (pos.ok ());
        assert (searching);
        assert (-VALUE_INFINITE <= alpha && alpha >= best_value && alpha < beta && best_value <= beta && beta <= +VALUE_INFINITE);
        assert (Threadpool.split_depth <= depth);
        assert (splitpoint_count < MAX_SPLITPOINTS_PER_THREAD);

        // Pick the next available splitpoint from the splitpoint stack
        SplitPoint &sp = splitpoints[splitpoint_count];

        sp.spinlock.acquire (); // No contention here until we don't increment splitPointsSize

        sp.master       = this;
        sp.parent_splitpoint = active_splitpoint;
        sp.slaves_mask  = 0, sp.slaves_mask.set (index);
        sp.pos          = &pos;
        sp.ss           = ss;
        sp.alpha        = alpha;
        sp.beta         = beta;
        sp.best_value   = best_value;
        sp.best_move    = best_move;
        sp.depth        = depth;
        sp.legal_count  = legal_count;
        sp.movepicker   = &movepicker;
        sp.node_type    = node_type;
        sp.cut_node     = cut_node;
        sp.nodes        = 0;
        sp.cut_off      = false;
        sp.slave_searching = true; // Must be set under lock protection

        ++splitpoint_count;
        active_splitpoint = &sp;
        active_pos = nullptr;

        // Try to allocate available threads
        Thread *slave;

        while (    sp.slaves_mask.count () < MAX_SLAVES_PER_SPLITPOINT
               && (slave = Threadpool.available_slave (&sp)) != nullptr
              )
        {
            slave->spinlock.acquire ();

            if (slave->can_join (active_splitpoint))
            {
                active_splitpoint->slaves_mask.set (slave->index);
                slave->active_splitpoint = active_splitpoint;
                slave->searching = true;
            }

            slave->spinlock.release ();
        }

        // Everything is set up. The master thread enters the idle loop, from which
        // it will instantly launch a search, because its 'searching' flag is set.
        // The thread will return from the idle loop when all slaves have finished
        // their work at this split point.
        sp.spinlock.release ();

        Thread::idle_loop (); // Force a call to base class Thread::idle_loop()

        // In helpful master concept a master can help only a sub-tree of its splitpoint,
        // and because here is all finished is not possible master is booked.
        assert (!searching);
        assert (active_pos == nullptr);

        searching = true;

        // Have returned from the idle loop, which means that all threads are finished.
        // Note that setting 'searching' and decreasing splitpoint_count is
        // done under lock protection to avoid a race with available_slave().
        sp.spinlock.acquire ();

        --splitpoint_count;
        active_pos = &pos;
        active_splitpoint = sp.parent_splitpoint;

        pos.game_nodes (pos.game_nodes () + sp.nodes);

        best_move  = sp.best_move;
        best_value = sp.best_value;

        sp.spinlock.release ();
    }

    // ------------------------------------

    // TimerThread::idle_loop() is where the timer thread waits msec milliseconds
    // and then calls task(). If msec is 0 thread sleeps until is woken up.
    void TimerThread::idle_loop ()
    {
        while (alive)
        {
            unique_lock<Mutex> lk (mutex);

            if (alive) sleep_condition.wait_for (lk, chrono::milliseconds(run ? resolution : INT_MAX));

            lk.unlock ();

            if (run) task ();

        }
    }

    // ------------------------------------

    // MainThread::idle_loop() is where the main thread is parked waiting to be started
    // when there is a new search. The main thread will launch all the slave threads.
    void MainThread::idle_loop ()
    {
        while (alive)
        {
            unique_lock<Mutex> lk (mutex);

            thinking = false;

            while (alive && !thinking)
            {
                sleep_condition.notify_one (); // Wake up the UI thread if needed
                sleep_condition.wait (lk);
            }

            lk.unlock ();

            if (alive)
            {
                searching = true;

                think ();   // Start thinking

                assert (searching);

                searching = false;
            }
        }
    }

    // MainThread::join() waits for main thread to finish the search
    void MainThread::join ()
    {
        unique_lock<Mutex> lk (mutex);
        sleep_condition.wait (lk, [&]{ return !thinking; });
    }


    // ------------------------------------

    // ThreadPool::initialize() is called at startup to create and launch requested threads,
    // that will go immediately to sleep.
    // Cannot use a c'tor becuase Threadpool is a static object
    // and need a fully initialized engine.
    void ThreadPool::initialize ()
    {
        push_back (new_thread<MainThread> ());

        check_limits_th             = new_thread<TimerThread> ();
        check_limits_th->task       = check_limits;
        check_limits_th->resolution = TIMER_RESOLUTION;

        configure ();
    }

    // ThreadPool::exit() cleanly terminates the threads before the program exits
    // Cannot be done in d'tor because have to terminate
    // the threads before to free ThreadPool object.
    void ThreadPool::exit ()
    {
        // As first because they accesses threads data
        delete_thread (auto_save_th);
        delete_thread (check_limits_th);

        for (Thread *th : *this)
        {
            delete_thread (th);
        }

        clear (); // Get rid of stale pointers
    }

    // ThreadPool::configure() updates internal threads parameters from the corresponding
    // UCI options and creates/destroys threads to match the requested number.
    // Thread objects are dynamically allocated to avoid creating in advance all possible
    // threads, with included pawns and material tables, if only few are used.
    void ThreadPool::configure ()
    {
        u32 threads = i32(Options["Threads"]);
        split_depth = i32(Options["Split Depth"])*DEPTH_ONE;
        //if (split_depth == DEPTH_ZERO) split_depth = 5 * DEPTH_ONE;

        assert (threads > 0);

        while (size () < threads)
        {
            push_back (new_thread<Thread> ());
        }

        while (size () > threads)
        {
            delete_thread (back ());
            pop_back ();
        }

        sync_cout
            << "info string Thread(s) "   << u16(threads) << ".\n"
            << "info string Split Depth " << u16(split_depth) << sync_endl;
    }

    // ThreadPool::available_slave() tries to find an idle thread which is available
    // to join SplitPoint 'sp'.
    Thread* ThreadPool::available_slave (const SplitPoint *sp) const
    {
        for (Thread *th : *this)
        {
            if (th->can_join (sp))
            {
                return th;
            }
        }
        return nullptr;
    }

    // ThreadPool::start_main() wakes up the main thread sleeping in MainThread::idle_loop()
    // so to start a new search, then returns immediately.
    void ThreadPool::start_main (const Position &pos, const LimitsT &limits, StateStackPtr &states)
    {
        main ()->join ();

        RootPos = pos;
        RootMoves.initialize (pos, limits.root_moves);
        Limits  = limits;
        if (states.get () != nullptr) // If don't set a new position, preserve current state
        {
            SetupStates = move (states); // Ownership transfer here
            assert (states.get () == nullptr);
        }

        Signals.force_stop     = false;
        Signals.ponderhit_stop = false;
        Signals.firstmove_root = false;
        Signals.failedlow_root = false;

        main ()->thinking = true;
        main ()->notify_one ();     // Starts main thread
    }

}
