#include "thread.h"

#include <algorithm> // For std::count
#include <cassert>

#include "MoveGenerator.h"
#include "Searcher.h"
#include "UCI.h"

using namespace std;
using namespace Searcher;
using namespace MoveGenerator;

ThreadPool Threads; // Global object

extern void check_time ();

namespace {

    // start_routine() is the C function which is called when a new thread
    // is launched. It is a wrapper to the virtual function idle_loop().
    extern "C" { long start_routine (ThreadBase *th) { th->idle_loop (); return 0; } }

    // Helpers to launch a thread after creation and joining before delete. Must be
    // outside Thread c'tor and d'tor because object shall be fully initialized
    // when start_routine (and hence virtual idle_loop) is called and when joining.
    template<class T>
    T* new_thread()
    {
        T *th = new T ();
        thread_create (th->handle, start_routine, th); // Will go to sleep
        return th;
    }

    void delete_thread (ThreadBase *th)
    {
        th->exit = true; // Search must be already finished
        th->notify_one ();
        thread_join (th->handle); // Wait for thread termination
        delete th;
    }

}

// ThreadBase::notify_one () wakes up the thread when there is some work to do
void ThreadBase::notify_one ()
{
    mutex.lock ();
    sleep_condition.notify_one ();
    mutex.unlock ();
}

// ThreadBase::wait_for() set the thread to sleep until condition 'b' turns true
void ThreadBase::wait_for (volatile const bool &b)
{
    mutex.lock ();
    while (!b) sleep_condition.wait (mutex);
    mutex.unlock ();
}

// Thread c'tor just inits data but does not launch any thread of execution that
// instead will be started only upon c'tor returns.
Thread::Thread () /* : split_points() */  // Value-initialization bug in MSVC
    : split_points ()
{
    searching = false;
    max_ply = split_points_size = 0;
    active_split_point = NULL;
    active_pos = NULL;
    idx = Threads.size ();
}

// Thread::cutoff_occurred() checks whether a beta cutoff has occurred in the
// current active split point, or in some ancestor of the split point.
bool Thread::cutoff_occurred() const
{
    for (SplitPoint *sp = active_split_point; sp != NULL; sp = sp->parent_split_point)
    {
        if (sp->cut_off) return true;
    }
    return false;
}

// Thread::available_to() checks whether the thread is available to help the
// thread 'master' at a split point. An obvious requirement is that thread must
// be idle. With more than two threads, this is not sufficient: If the thread is
// the master of some split point, it is only available as a slave to the slaves
// which are busy searching the split point at the top of slaves split point
// stack (the "helpful master concept" in YBWC terminology).
bool Thread::available_to (const Thread *master) const
{
    if (searching) return false;

    // Make a local copy to be sure doesn't become zero under our feet while
    // testing next condition and so leading to an out of bound access.
    int32_t size = split_points_size;

    // No split points means that the thread is available as a slave for any
    // other thread otherwise apply the "helpful master" concept if possible.
    return !size || (split_points[size - 1].slaves_mask & (1ULL << master->idx));
}

// TimerThread::idle_loop() is where the timer thread waits msec milliseconds
// and then calls check_time(). If msec is 0 thread sleeps until is woken up.
void TimerThread::idle_loop ()
{
    while (!exit)
    {
        mutex.lock ();

        if (!exit) sleep_condition.wait_for (mutex, run ? Resolution : INT_MAX);

        mutex.unlock ();

        if (run) check_time ();
    }
}

// MainThread::idle_loop() is where the main thread is parked waiting to be started
// when there is a new search. Main thread will launch all the slave threads.
void MainThread::idle_loop ()
{
    while (true)
    {
        mutex.lock ();

        thinking = false;

        while (!thinking && !exit)
        {
            Threads.sleep_condition.notify_one (); // Wake up UI thread if needed
            sleep_condition.wait (mutex);
        }

        mutex.unlock ();

        if (exit) return;

        searching = true;

        Searcher::think ();

        ASSERT (searching);

        searching = false;
    }
}

// init() is called at startup to create and launch requested threads, that will
// go immediately to sleep due to 'sleep_while_idle' set to true. We cannot use
// a c'tor becuase Threads is a static object and we need a fully initialized
// engine at this point due to allocation of Endgames in Thread c'tor.
void ThreadPool::initialize ()
{
    sleep_while_idle = true;
    timer = new_thread<TimerThread> ();
    push_back (new_thread<MainThread> ());
    read_uci_options ();
}

// exit() cleanly terminates the threads before the program exits
void ThreadPool::deinitialize ()
{
    delete_thread (timer); // As first because check_time() accesses threads data

    for (iterator itr = begin (); itr != end (); ++itr)
    {
        delete_thread (*itr);
    }
}

// read_uci_options() updates internal threads parameters from the corresponding
// UCI options and creates/destroys threads to match the requested number. Thread
// objects are dynamically allocated to avoid creating in advance all possible
// threads, with included pawns and material tables, if only few are used.
void ThreadPool::read_uci_options ()
{
    max_threads_per_split_point = int32_t (*(Options["Threads per Split Point"]));
    min_split_depth             = int32_t (*(Options["Split Depth"])) * ONE_MOVE;
    int32_t num_threads         = int32_t (*(Options["Threads"]));

    ASSERT (num_threads > 0);

    // Value 0 has a special meaning: We determine the optimal minimum split depth
    // automatically. Anyhow the min_split_depth should never be under 4 plies.
    min_split_depth = !min_split_depth ?
        (num_threads < 8 ? 4 : 7) * ONE_MOVE : max (4 * ONE_MOVE, min_split_depth);

    while (size () < num_threads)
    {
        push_back (new_thread<Thread> ());
    }

    while (size () > num_threads)
    {
        delete_thread (back ());
        pop_back ();
    }
}


// slave_available() tries to find an idle thread which is available as a slave
// for the thread 'master'.
Thread* ThreadPool::available_slave (const Thread *master) const
{
    for (const_iterator itr = cbegin (); itr != cend (); ++itr)
    {
        if ((*itr)->available_to (master))
        {
            return *itr;
        }
    }
    return NULL;
}


// split() does the actual work of distributing the work at a node between
// several available threads. If it does not succeed in splitting the node
// (because no idle threads are available), the function immediately returns.
// If splitting is possible, a SplitPoint object is initialized with all the
// data that must be copied to the helper threads and then helper threads are
// told that they have been assigned work. This will cause them to instantly
// leave their idle loops and call search(). When all threads have returned from
// search() then split() returns.
template <bool FAKE>
void Thread::split (Position &pos, const Stack *ss, Value alpha, Value beta, Value *best_value, Move *best_move,
                    Depth depth, int32_t moves_count, MovePicker *move_picker, int32_t node_type, bool cut_node)
{
    ASSERT (pos.ok ());
    ASSERT (-VALUE_INFINITE <*best_value && *best_value <= alpha && alpha < beta && beta <= VALUE_INFINITE);
    ASSERT (depth >= Threads.min_split_depth);
    ASSERT (searching);
    ASSERT (split_points_size < MAX_SPLITPOINTS_PER_THREAD);

    // Pick the next available split point from the split point stack
    SplitPoint &sp = split_points[split_points_size];

    sp.master_thread = this;
    sp.parent_split_point = active_split_point;
    sp.slaves_mask  = 1ULL << idx;
    sp.depth        = depth;
    sp.best_value   = *best_value;
    sp.best_move    = *best_move;
    sp.alpha        = alpha;
    sp.beta         = beta;
    sp.node_type    = node_type;
    sp.cut_node     = cut_node;
    sp.move_picker  = move_picker;
    sp.moves_count   = moves_count;
    sp.pos          = &pos;
    sp.nodes        = 0;
    sp.cut_off      = false;
    sp.ss           = ss;

    // Try to allocate available threads and ask them to start searching setting
    // 'searching' flag. This must be done under lock protection to avoid concurrent
    // allocation of the same slave by another master.
    Threads.mutex.lock ();
    sp.mutex.lock ();

    ++split_points_size;
    active_split_point = &sp;
    active_pos = NULL;

    size_t slaves_count = 1; // This thread is always included
    Thread *slave;

    while ((slave = Threads.available_slave (this)) != NULL
        && ++slaves_count <= Threads.max_threads_per_split_point && !FAKE)
    {
        sp.slaves_mask |= 1ULL << slave->idx;
        slave->active_split_point = &sp;
        slave->searching = true; // Slave leaves idle_loop()
        slave->notify_one (); // Could be sleeping
    }

    // Everything is set up. The master thread enters the idle loop, from which
    // it will instantly launch a search, because its 'searching' flag is set.
    // The thread will return from the idle loop when all slaves have finished
    // their work at this split point.
    if (slaves_count > 1 || FAKE)
    {
        sp.mutex.unlock ();
        Threads.mutex.unlock ();

        Thread::idle_loop(); // Force a call to base class idle_loop()

        // In helpful master concept a master can help only a sub-tree of its split
        // point, and because here is all finished is not possible master is booked.
        ASSERT (!searching);
        ASSERT (!active_pos);

        // We have returned from the idle loop, which means that all threads are
        // finished. Note that setting 'searching' and decreasing split_points_size is
        // done under lock protection to avoid a race with Thread::available_to().
        Threads.mutex.lock ();
        sp.mutex.lock ();
    }

    searching = true;
    --split_points_size;
    active_split_point = sp.parent_split_point;
    active_pos  = &pos;
    pos.game_nodes (pos.game_nodes () + sp.nodes);
    *best_move  = sp.best_move;
    *best_value = sp.best_value;

    sp.mutex.unlock ();
    Threads.mutex.unlock ();
}

// Explicit template instantiations
template void Thread::split<false>(Position&, const Stack*, Value, Value, Value*, Move*, Depth, int32_t, MovePicker*, int32_t, bool);
template void Thread::split< true>(Position&, const Stack*, Value, Value, Value*, Move*, Depth, int32_t, MovePicker*, int32_t, bool);

// start_thinking() wakes up the main thread sleeping in MainThread::idle_loop()
// so to start a new search, then returns immediately.
void ThreadPool::start_thinking (const Position &pos, const Limits &search_limits, StateInfoStackPtr &states)
{
    wait_for_think_finished ();

    searchTime = Time::now (); // As early as possible

    signals.stop_on_ponderhit = false;
    signals.first_root_move = false;
    signals.stop = false;
    signals.failed_low_at_root = false;

    rootMoves.clear();
    rootPos = pos;
    limits  = search_limits;
    if (states.get ()) // If we don't set a new position, preserve current state
    {
        //setupStates = move (states); // Ownership transfer here
        setupStates = states; // Ownership transfer here
        ASSERT (!states.get ());
    }

    MoveList mov_lst = generate<LEGAL>(pos);
    for_each (mov_lst.cbegin (), mov_lst.cend (), [&] (Move m)
    {
        if (search_limits.search_moves.empty ()
            || count (search_limits.search_moves.begin (), search_limits.search_moves.end (), m))
        {
            rootMoves.push_back (RootMove (m));
        }
    });

    main ()->thinking = true;
    main ()->notify_one (); // Starts main thread
}

// wait_for_think_finished() waits for main thread to go to sleep then returns
void ThreadPool::wait_for_think_finished ()
{
    MainThread *main_th = main ();
    main_th->mutex.lock ();
    while (main_th->thinking) sleep_condition.wait (main_th->mutex);
    main_th->mutex.unlock ();
}


/// prefetch() preloads the given address in L1/L2 cache. This is a non-blocking
/// function that doesn't stall the CPU waiting for data to be loaded from memory,
/// which can be quite slow.
#ifdef NO_PREFETCH

void prefetch (char *addr) {}

#else

void prefetch (char *addr)
{

#   if defined(__INTEL_COMPILER)
    {
        // This hack prevents prefetches from being optimized away by
        // Intel compiler. Both MSVC and gcc seem not be affected by this.
        __asm__ ("");
    }
#   endif

#   if defined(__INTEL_COMPILER) || defined(_MSC_VER)
    {
        _mm_prefetch (addr, _MM_HINT_T0);
    }
#   else
    {
        __builtin_prefetch (addr);
    }
#   endif
}

#endif