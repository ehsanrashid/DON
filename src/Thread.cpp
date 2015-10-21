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
        if (th != nullptr)
        {
            th->mutex.lock ();
            th->alive = false;   // Search must be already finished
            th->mutex.unlock ();
            th->notify_one ();
            th->join ();         // Wait for thread termination
            delete th;
            th = nullptr;
        }
    }

    // Explicit template instantiations
    // --------------------------------
    template TimerThread* new_thread<TimerThread> ();
    // ------------------------------------

    // Thread::Thread() makes some init but does not launch any execution thread that
    // will be started only when c'tor returns.
    Thread::Thread ()
        : ThreadBase ()
    {
        //max_ply     = 0;
        //searching   = false;
        index       = Threadpool.size (); // Starts from 0
    }

    // Thread::idle_loop() is where the thread is parked when it has no work to do
    void Thread::idle_loop ()
    {
        while (alive)
        {
            std::unique_lock<Mutex> lk (mutex);

            while (alive && !searching)
            {
                sleep_condition.wait (lk);
            }

            lk.unlock ();

            if (alive && searching)
            {
                search ();
            }
        }
    }

    // ------------------------------------

    // MainThread::join() waits for main thread to finish thinking
    void MainThread::join ()
    {
        unique_lock<Mutex> lk (mutex);
        sleep_condition.wait (lk, [&]{ return !thinking; });
    }

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
                think ();   // Start thinking
            }
        }
    }

    // ------------------------------------

    // TimerThread::idle_loop() is where the timer thread waits msec milliseconds
    // and then calls task(). If msec is 0 thread sleeps until is woken up.
    void TimerThread::idle_loop ()
    {
        while (alive)
        {
            unique_lock<Mutex> lk (mutex);

            if (alive)
            {
                sleep_condition.wait_for (lk, chrono::milliseconds (_running ? resolution : INT_MAX));
            }

            lk.unlock ();

            if (_running) task ();
        }
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
        delete_thread (check_limits_th);
        delete_thread (save_hash_th);

        for (auto *th : *this)
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
        size_t threads = i32(Options["Threads"]);
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

        sync_cout << "info string Thread(s) "   << threads << "." << sync_endl;
    }

    u64 ThreadPool::game_nodes ()
    {
        u64 nodes = 0;
        for (auto *th : *this)
        {
            nodes += th->root_pos.game_nodes ();
        }
        return nodes;
    }

    // ThreadPool::start_main() wakes up the main thread sleeping in
    // MainThread::idle_loop() and starts a new search, then returns immediately.
    void ThreadPool::start_main (const Position &pos, const LimitsT &limits, StateStackPtr &states)
    {
        main ()->join ();

        Signals.force_stop     = false;
        Signals.ponderhit_stop = false;
        Signals.firstmove_root = false;
        Signals.failedlow_root = false;

        main ()->root_pos = pos;
        main ()->root_moves.initialize (pos);

        Limits  = limits;
        if (states.get () != nullptr) // If don't set a new position, preserve current state
        {
            SetupStates = std::move (states); // Ownership transfer here
            assert (states.get () == nullptr);
        }

        main ()->thinking = true;
        main ()->notify_one (); // Wake up main thread: 'thinking' must be already set
    }

}
