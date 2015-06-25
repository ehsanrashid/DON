#ifndef _THREAD_H_INC_
#define _THREAD_H_INC_

#include <atomic>
#include <bitset>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "thread_win32.h"
#include "Position.h"
#include "Pawns.h"
#include "Material.h"
#include "MovePicker.h"
#include "Searcher.h"

namespace Threading {

    using namespace Searcher;
    using namespace MovePick;

    const u16 MAX_THREADS               = 128; // Maximum Threads
    const u08 MAX_SPLITPOINTS_PER_THREAD=   8; // Maximum Splitpoints/Thread
    const u08 MAX_SLAVES_PER_SPLITPOINT =   4; // Maximum Slaves/Splitpoint
    const u08 MAX_SPLIT_DEPTH           =  12; // Maximum SplitDepth

    class Thread;

    class Spinlock
    {
    private:
        std::atomic_int _state;

    public:
        Spinlock () { _state = 1; }
        Spinlock (const Spinlock&) = delete; 
        Spinlock& operator= (const Spinlock&) = delete;

        void acquire ()
        {
            while (_state.fetch_sub (1, std::memory_order_acquire) != 1)
            {
                while (_state.load (std::memory_order_relaxed) < 1)
                {
                    std::this_thread::yield (); // Be nice to hyperthreading
                }
            }
        }

        void release ()
        {
            _state.store (1, std::memory_order_release);
        }
    };

    // SplitPoint struct stores information shared by the threads searching in
    // parallel below the same split point. It is populated at splitting time.
    struct SplitPoint
    {

    public:
        // Const data after splitpoint has been setup
        const Position *pos;

        Stack  *ss;
        Thread *master;
        Value   beta;
        Depth   depth;
        NodeT   node_type;
        bool    cut_node;

        // Const pointers to shared data
        MovePicker  *movepicker;
        SplitPoint  *parent_splitpoint;

        // Shared data
        Spinlock    spinlock;
        std::bitset<MAX_THREADS> slaves_mask;

        volatile bool  slave_searching;
        volatile u08   legal_count;
        volatile Value alpha;
        volatile Value best_value;
        volatile Move  best_move;
        volatile u64   nodes;
        volatile bool  cut_off;
    };


    // ThreadBase class is the base of the hierarchy from where
    // derive all the specialized thread classes.
    class ThreadBase
        : public std::thread
    {
    public:
        Mutex               mutex;
        Spinlock            spinlock;
        ConditionVariable   sleep_condition;

        volatile bool       alive = true;

        ThreadBase () : std::thread() {}
        virtual ~ThreadBase() = default;

        void notify_one ();

        void wait_for (volatile const bool &condition);

        virtual void idle_loop () = 0;
    };

    // Thread struct keeps together all the thread related stuff like locks, state
    // and especially split points. We also use per-thread pawn and material hash
    // tables so that once we get a pointer to an entry its life time is unlimited
    // and we don't have to care about someone changing the entry under our feet.
    class Thread
        : public ThreadBase
    {
    public:

        SplitPoint      splitpoints[MAX_SPLITPOINTS_PER_THREAD];
        Pawns   ::Table pawn_table;
        Material::Table matl_table;

        Position   *active_pos  = nullptr;
        i32         max_ply     = 0;
        size_t      index;

        SplitPoint* volatile active_splitpoint  = nullptr;
        volatile u08         splitpoint_count   = 0;
        volatile bool        searching          = false;

        Thread ();
        
        void idle_loop () override;
        
        bool cutoff_occurred () const;
        
        bool can_join (const SplitPoint *sp) const;

        void split (Position &pos, Stack *ss, Value alpha, Value beta, Value &best_value, Move &best_move,
            Depth depth, u08 legal_count, MovePicker &movepicker, NodeT node_type, bool cut_node);

    };

    // MainThread struct is derived struct used for the main one
    class MainThread
        : public Thread
    {
    public:

        volatile bool thinking = true; // Avoid a race with start_thinking()
        
        void idle_loop () override;
        
        void join ();
    };

    const i32 TIMER_RESOLUTION = 5; // Millisec between two check_time() calls

    // TimerThread struct is derived struct used for the recurring timer.
    class TimerThread
        : public ThreadBase
    {
    private:
        bool _running = false;

    public:
        
        i32 resolution; // Millisec between two task() calls
        void (*task) () = nullptr;
        
        void start () { _running = true ; }
        void stop  () { _running = false; }

        void idle_loop () override;

    };

    // ThreadPool struct handles all the threads related stuff like
    // - initializing
    // - starting
    // - parking
    // - launching a slave thread at a split point (most important).
    // All the access to shared thread data is done through this.
    class ThreadPool
        : public std::vector<Thread*>
    {
    public:

        TimerThread *check_limits_th = nullptr;
        TimerThread *save_hash_th    = nullptr;
        Depth        split_depth;

        MainThread* main () { return static_cast<MainThread*> (at (0)); }

        // No c'tor and d'tor, threadpool rely on globals that should
        // be initialized and valid during the whole thread lifetime.
        void initialize ();
        void exit ();

        Thread* available_slave (const SplitPoint *sp) const;

        void start_main (const Position &pos, const LimitsT &limit, StateStackPtr &states);

        void configure ();

    };

    template<class T>
    extern T* new_thread ();

    extern void delete_thread (ThreadBase *th);

    extern void check_limits ();
    extern void save_hash ();

}

enum SyncT { IO_LOCK, IO_UNLOCK };

// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
inline std::ostream& operator<< (std::ostream &os, SyncT sync)
{
    static Mutex io_mutex;

    (sync == IO_LOCK) ?
        io_mutex.lock () :
    (sync == IO_UNLOCK) ?
        io_mutex.unlock () : (void) 0;

    return os;
}

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK


extern Threading::ThreadPool  Threadpool;

#endif // _THREAD_H_INC_
