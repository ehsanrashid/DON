#include "Thread.h"

#include "Position.h"
#include "MoveGenerator.h"
#include "Searcher.h"
#include "Endgame.h"
#include "UCI.h"

Threads::ThreadPool Threadpool; // Global ThreadPool

namespace Threads {

    using namespace std;
    using namespace MoveGen;
    using namespace Searcher;
    using namespace UCI;

    namespace {

        // start_routine() is the C function which is called when a new thread
        // is launched. It is a wrapper to the virtual function idle_loop().
        extern "C" { inline long start_routine (ThreadBase *th) { th->idle_loop (); return 0; } }
    }

    // Helpers to launch a thread after creation and joining before delete. Must be
    // outside Thread c'tor and d'tor because object must be fully initialized
    // when start_routine (and hence virtual idle_loop) is called and when joining.
    template<class T>
    T* new_thread ()
    {
        T *th = new T ();
        thread_create (th->native_handle, start_routine, th); // Will go to sleep
        return th;
    }

    template<class T>
    void delete_thread (T *th)
    {
        if (th != NULL)
        {
            th->mutex.lock ();
            th->kill ();                    // Search must be already finished
            th->mutex.unlock ();
            th->notify_one ();
            thread_join (th->native_handle);// Wait for thread termination
            delete th;
        }
    }

    // explicit template instantiations
    // --------------------------------
    template TimerThread* new_thread<TimerThread> ();
    template void delete_thread<TimerThread> (TimerThread *th);

    // ------------------------------------

    // notify_one () wakes up the thread when there is some work to do
    void ThreadBase::notify_one ()
    {
        mutex.lock ();
        sleep_condition.notify_one ();
        mutex.unlock ();
    }

    // wait_for() set the thread to sleep until condition turns true
    void ThreadBase::wait_for (const volatile bool &condition)
    {
        mutex.lock ();
        while (!condition)
        {
            sleep_condition.wait (mutex);
        }
        mutex.unlock ();
    }

    // ------------------------------------

    // Thread c'tor makes some init but does not launch any execution thread that
    // will be started only when c'tor returns.
    Thread::Thread () //: splitpoints ()  // Initialization of non POD broken in MSVC
        : active_pos (NULL)
        , index (Threadpool.size ())  // Starts from 0
        , max_ply (0)
        , active_splitpoint (NULL)
        , splitpoint_count (0)
        , searching (false)
    {}

    // cutoff_occurred() checks whether a beta cutoff has occurred in the
    // current active splitpoint, or in some ancestor of the splitpoint.
    bool Thread::cutoff_occurred () const
    {
        for (SplitPoint *sp = active_splitpoint; sp != NULL; sp = sp->parent_splitpoint)
        {
            if (sp->cut_off) return true;
        }
        return false;
    }

    // available_to() checks whether the thread is available to help the thread 'master'
    // at a splitpoint. An obvious requirement is that thread must be idle.
    // With more than two threads, this is not sufficient: If the thread is the
    // master of some splitpoint, it is only available as a slave to the slaves
    // which are busy searching the splitpoint at the top of slaves splitpoint
    // stack (the "helpful master concept" in YBWC terminology).
    bool Thread::available_to (const Thread *master) const
    {
        if (searching) return false;

        // Make a local copy to be sure doesn't become zero under our feet while
        // testing next condition and so leading to an out of bound access.
        size_t count = splitpoint_count;

        // No splitpoints means that the thread is available as a slave for any
        // other thread otherwise apply the "helpful master" concept if possible.
        return 0 == count || splitpoints[count - 1].slaves_mask.test (master->index);
    }

    // split<>() does the actual work of distributing the work at a node between several available threads.
    // Almost always allocate a slave, only in the rare case of a race (< 2%) this is not true.
    // SplitPoint object is initialized with all the data that must be copied to the helper threads
    // and then helper threads are told that they have been assigned work. This causes them to instantly
    // leave their idle loops and call search<>().
    // When all threads have returned from search() then split() returns.
    void Thread::split (Position &pos, Stack *ss, Value alpha, Value beta, Value &best_value, Move &best_move,
        Depth depth, u08 legals, MovePicker &movepicker, NodeT node_type, bool cut_node)
    {
        assert (pos.ok ());
        assert (searching);
        assert (-VALUE_INFINITE <= alpha && alpha >= best_value && alpha < beta && best_value <= beta && beta <= +VALUE_INFINITE);
        assert (Threadpool.split_depth <= depth);
        assert (splitpoint_count < MAX_SPLITPOINTS_PER_THREAD);

        // Pick the next available splitpoint from the splitpoint stack
        SplitPoint &sp = splitpoints[splitpoint_count];

        sp.master       = this;
        sp.parent_splitpoint = active_splitpoint;
        sp.slaves_mask  = 0, sp.slaves_mask.set (index);
        sp.ss           = ss;
        sp.pos          = &pos;
        sp.alpha        = alpha;
        sp.beta         = beta;
        sp.best_value   = best_value;
        sp.best_move    = best_move;
        sp.depth        = depth;
        sp.legals       = legals;
        sp.movepicker   = &movepicker;
        sp.node_type    = node_type;
        sp.cut_node     = cut_node;
        sp.nodes        = 0;
        sp.cut_off      = false;

        // Try to allocate available threads and ask them to start
        // searching by setting 'searching' flag.
        // This must be done under lock protection to avoid concurrent
        // allocation of the same slave by another master.
        Threadpool.spinlock.acquire ();
        sp.spinlock.acquire ();

        sp.slave_searching  = true;
        ++splitpoint_count;
        active_splitpoint   = &sp;
        active_pos          = NULL;

        Thread *slave;
        while (   sp.slaves_mask.count () < MAX_SLAVES_PER_SPLITPOINT 
              && (slave = Threadpool.available_slave (this)) != NULL
              )
        {
            sp.slaves_mask.set (slave->index);
            slave->active_splitpoint = &sp;
            slave->searching = true;        // Leaves idle_loop()
            slave->notify_one ();           // Notifies could be sleeping
        }

        // Everything is set up. The master thread enters the idle-loop, from which
        // it will instantly launch a search, because its 'searching' flag is set.
        // The thread will return from the idle loop when all slaves have finished
        // their work at this splitpoint.
        sp.spinlock.release ();
        Threadpool.spinlock.release ();

        Thread::idle_loop (); // Force a call to base class Thread::idle_loop()

        // In helpful master concept a master can help only a sub-tree of its splitpoint,
        // and because here is all finished is not possible master is booked.
        assert (!searching);
        assert (!active_pos);

        // Have returned from the idle loop, which means that all threads are finished.
        // Note that setting 'searching' and decreasing splitpoint_count is
        // done under lock protection to avoid a race with available_to().
        Threadpool.spinlock.acquire ();
        sp.spinlock.acquire ();

        searching = true;

        active_pos = &pos;
        active_splitpoint = sp.parent_splitpoint;
        --splitpoint_count;

        pos.game_nodes (pos.game_nodes () + sp.nodes);

        best_move  = sp.best_move;
        best_value = sp.best_value;

        sp.spinlock.release ();
        Threadpool.spinlock.release ();
    }

    // ------------------------------------

    // TimerThread::idle_loop() is where the timer thread waits msec milliseconds
    // and then calls task(). If msec is 0 thread sleeps until is woken up.
    void TimerThread::idle_loop ()
    {
        do
        {
            mutex.lock ();
            if (alive)
            {
                sleep_condition.wait_for (mutex, run ? resolution : INT_MAX);
            }
            mutex.unlock ();

            if (run) task ();
        } while (alive);
    }

    // ------------------------------------

    // MainThread::idle_loop() is where the main thread is parked waiting to be started
    // when there is a new search. Main thread will launch all the slave threads.
    void MainThread::idle_loop ()
    {
        do
        {
            mutex.lock ();
            while (alive && !thinking)
            {
                Threadpool.sleep_condition.notify_one (); // Wake up UI thread if needed
                sleep_condition.wait (mutex);
            }
            mutex.unlock ();

            if (alive)
            {
                searching = true;
                think ();   // Start thinking
                assert (searching);
                searching = false;
                thinking  = false;
            }
        } while (alive);
    }

    // ------------------------------------

    // initialize() is called at startup to create and launch requested threads,
    // that will go immediately to sleep.
    // Cannot use a c'tor becuase Threadpool is a static object
    // and need a fully initialized engine.
    void ThreadPool::initialize ()
    {
        push_back (new_thread<MainThread> ());

        check_limits_th             = new_thread<TimerThread> ();
        check_limits_th->task       = check_limits;
        check_limits_th->resolution = TIMER_RESOLUTION;

        auto_save_th                = NULL;

        configure ();
    }

    // deinitialize() cleanly terminates the threads before the program exits
    // Cannot be done in d'tor because have to terminate
    // the threads before to free ThreadPool object.
    void ThreadPool::deinitialize ()
    {
        delete_thread (auto_save_th);
        delete_thread (check_limits_th); // As first because check_limits() accesses threads data
        for (iterator itr = begin (); itr != end (); ++itr)
        {
            delete_thread (*itr);
        }
    }

    // configure() updates internal threads parameters from the corresponding
    // UCI options and creates/destroys threads to match the requested number.
    // Thread objects are dynamically allocated to avoid creating in advance all possible
    // threads, with included pawns and material tables, if only few are used.
    void ThreadPool::configure ()
    {
        u32 threads = i32(Options["Threads"]);
        split_depth = i32(Options["Split Depth"])*DEPTH_ONE;

        assert (threads > 0);

        // Split depth '0' has a special meaning:
        // Determines the best optimal minimum split depth automatically
        if (DEPTH_ZERO == split_depth)
        {
            split_depth = min (4 + max (i32(threads)-1, 0)/3, i32(MAX_SPLIT_DEPTH))*DEPTH_ONE;
        }

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

    // available_slave() tries to find an idle thread
    // which is available as a slave for the thread 'master'.
    Thread* ThreadPool::available_slave (const Thread *master) const
    {
        for (const_iterator itr = begin (); itr != end (); ++itr)
        {
            Thread *slave = *itr;
            if (slave->available_to (master))
            {
                return slave;
            }
        }
        return NULL;
    }

    // start_main() wakes up the main thread sleeping in MainThread::idle_loop()
    // so to start a new search, then returns immediately.
    void ThreadPool::start_main (const Position &pos, const LimitsT &limits, StateInfoStackPtr &states)
    {
        wait_for_main ();

        SearchTime = now (); // As early as possible

        RootPos = pos;
        RootMoves.initialize (pos, limits.root_moves);
        Limits  = limits;
        if (states.get () != NULL) // If don't set a new position, preserve current state
        {
            SetupStates = states;  // Ownership transfer here
            assert (states.get () == NULL);
        }

        Signals.force_stop     = false;
        Signals.ponderhit_stop = false;
        Signals.root_1stmove   = false;
        Signals.root_failedlow = false;

        MainThread *main_th = main ();
        main_th->thinking = true;
        main_th->notify_one ();     // Starts main thread
    }

    // wait_for_main() waits for main thread to go to sleep then returns
    void ThreadPool::wait_for_main ()
    {
        MainThread *main_th = main ();
        main_th->mutex.lock ();
        while (main_th->thinking)
        {
            sleep_condition.wait (main_th->mutex);
        }
        main_th->mutex.unlock ();
    }

}
