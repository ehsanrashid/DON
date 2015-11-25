#include "Thread.h"

#include "UCI.h"

Threading::ThreadPool Threadpool; // Global ThreadPool

namespace Threading {

    using namespace std;

    // Thread constructor launchs the thread and then wait until it goes to sleep in idle_loop().
    Thread::Thread ()
        : _alive (true)
        , _searching (true)
        , max_ply (0)
        , chk_count (0)
        , reset_check (false)
    {
        index = u16(Threadpool.size ()); // Starts from 0
        history_values.clear ();
        counter_moves.clear ();

        std::unique_lock<Mutex> lk (_mutex);
        _native_thread = std::thread (&Thread::idle_loop, this);
        _sleep_condition.wait (lk, [&] { return !_searching; });
        lk.unlock ();
    }

    // Thread destructor waits for thread termination before returning
    Thread::~Thread ()
    {
        _alive = false;
        std::unique_lock<Mutex> lk (_mutex);
        _sleep_condition.notify_one ();
        lk.unlock ();
        _native_thread.join ();
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

    // ThreadPool::start_searching() wakes up the main thread sleeping in
    // Thread::idle_loop() and starts a new search, then returns immediately.
    void ThreadPool::start_thinking (const Position &pos, const LimitsT &limits, StateStackPtr &states)
    {
        main ()->wait_while_searching ();

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

        main ()->start_searching (false);
    }

    void ThreadPool::wait_while_thinking ()
    {
        Threadpool.main ()->wait_while_searching ();
    }

}
