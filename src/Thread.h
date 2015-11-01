#ifndef _THREAD_H_INC_
#define _THREAD_H_INC_

#include <bitset>
#include <thread>

#include "thread_win32.h"

#include "Position.h"
#include "Pawns.h"
#include "Material.h"
#include "MovePicker.h"
#include "Searcher.h"

namespace Threading {

    using namespace Searcher;
    using namespace MovePick;

    const u16 MAX_THREADS = 128; // Maximum Threads

    // ThreadBase class is the base of the hierarchy from where
    // derive all the specialized thread classes.
    class ThreadBase
        : public std::thread
    {
    public:
        Mutex               mutex;
        ConditionVariable   sleep_condition;
        std::atomic<bool>   alive { true };

        virtual ~ThreadBase() = default;

        // ThreadBase::notify_one () wakes up the thread when there is some work to do
        void notify_one ()
        {
            std::unique_lock<Mutex> lk (mutex);
            sleep_condition.notify_one ();
        }
        // ThreadBase::wait_until() set the thread to sleep until 'condition' turns true
        void wait_until (const std::atomic<bool> &condition)
        {
            std::unique_lock<Mutex> lk (mutex);
            sleep_condition.wait (lk, [&]{ return bool(condition); });
        }
        // ThreadBase::wait_while() set the thread to sleep until 'condition' turns false
        void wait_while (const std::atomic<bool> &condition)
        {
            std::unique_lock<Mutex> lk (mutex);
            sleep_condition.wait (lk, [&]{ return !bool(condition); });
        }

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
        Pawns   ::Table pawn_table;
        Material::Table matl_table;

        u16  index      = 0
           , pv_index   = 0
           , max_ply    = 0;

        Position        root_pos;
        RootMoveVector  root_moves;
        HValueStats     history_values;
        MoveStats       counter_moves;
        Depth           root_depth;

        std::atomic<bool> searching { false };

        Thread ();

        void search (bool thread_main = false);

        virtual void idle_loop () override;
    };

    // MainThread struct is derived struct used for the main one
    class MainThread
        : public Thread
    {
    public:
        std::atomic<bool> thinking { true }; // Avoid a race with start_thinking()

        // MainThread::join() waits for main thread to finish thinking
        void join ()
        {
            std::unique_lock<Mutex> lk (mutex);
            sleep_condition.wait (lk, [&]{ return !bool(thinking); });
        }

        void think ();

        virtual void idle_loop () override;
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

        virtual void idle_loop () override;
    };

    // ThreadPool struct handles all the threads related stuff like
    // - initializing
    // - starting
    // - parking
    // - launching a thread.
    // All the access to shared thread data is done through this.
    class ThreadPool
        : public std::vector<Thread*>
    {
    public:

        TimerThread *check_limits_th = nullptr;
        TimerThread *save_hash_th    = nullptr;
        
        MainThread* main () const { return static_cast<MainThread*> (at (0)); }

        // No constructor and destructor, threadpool rely on globals
        // that should be initialized and valid during the whole thread lifetime.
        void initialize ();
        void exit ();

        void start_main (const Position &pos, const LimitsT &limit, StateStackPtr &states);
        u64  game_nodes ();

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

    if (sync == IO_LOCK)
        io_mutex.lock ();
    else
    if (sync == IO_UNLOCK)
        io_mutex.unlock ();

    return os;
}

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK


extern Threading::ThreadPool  Threadpool;

#endif // _THREAD_H_INC_
