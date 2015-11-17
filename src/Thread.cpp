#include "Thread.h"

#include "UCI.h"

Threading::ThreadPool Threadpool; // Global ThreadPool

namespace Threading {

    using namespace std;

    // Thread constructor makes some init and launches the thread that will go to
    // sleep in idle_loop().
    Thread::Thread ()
        : alive (true)
        , searching (true)          // Avoid a race with start_thinking()
        , reset_check (false)
        , max_ply (0)
        , chk_count (0)
    {
        history_values.clear ();
        counter_moves.clear ();
        index       = u16(Threadpool.size ()); // Starts from 0
        std::thread::operator= (std::thread (&Thread::idle_loop, this));
    }

    // Thread destructor waits for thread termination before deleting
    Thread::~Thread ()
    {
        mutex.lock ();
        alive = false;          // Search must be already finished
        mutex.unlock ();

        notify_one ();
        std::thread::join ();   // Wait for thread termination
    }

    // Thread::idle_loop() is where the thread is parked when it has no work to do
    void Thread::idle_loop ()
    {
        while (alive)
        {
            unique_lock<Mutex> lk (mutex);

            searching = false;

            while (alive && !searching)
            {
                sleep_condition.notify_one (); // Wake up main thread if needed
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

    // ThreadPool::initialize() is called at startup to create and launch
    // requested threads, that will go immediately to sleep.
    // Cannot use a constructor becuase threadpool is a static object
    // and require a fully initialized engine.
    void ThreadPool::initialize ()
    {
        push_back (new MainThread);

        configure ();
    }

    // ThreadPool::deinitialize() cleanly terminates the threads before the program exits
    // Cannot be done in destructor because threads must be terminated before freeing threadpool.
    void ThreadPool::deinitialize ()
    {
        for (auto *th : *this)
        {
            delete th;
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
        assert(threads > 0);

        while (size () < threads)
        {
            push_back (new Thread);
        }
        while (size () > threads)
        {
            delete back ();
            pop_back ();
        }

        sync_cout << "info string Thread(s) " << threads << "." << sync_endl;
    }

    u64 ThreadPool::game_nodes ()
    {
        u64 nodes = U64(0);
        for (auto *th : *this)
        {
            nodes += th->root_pos.game_nodes ();
        }
        return nodes;
    }

    // ThreadPool::start_main() wakes up the main thread sleeping in
    // Thread::idle_loop() and starts a new search, then returns immediately.
    void ThreadPool::start_thinking (const Position &pos, const LimitsT &limits, StateStackPtr &states)
    {
        for (auto *th : Threadpool)
        {
            th->join ();
        }

        Signals.force_stop     = false;
        Signals.ponderhit_stop = false;
        Signals.firstmove_root = false;
        Signals.failedlow_root = false;

        Limits  = limits;
        main ()->root_pos = pos;
        main ()->root_moves.initialize (pos, limits.root_moves);
        if (states.get () != nullptr) // If don't set a new position, preserve current state
        {
            SetupStates = std::move (states); // Ownership transfer here
            assert(states.get () == nullptr);
        }

        main ()->searching = true;
        main ()->notify_one (); // Wake up main thread: 'thinking' must be already set
    }

}
