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

    // Thread struct keeps together all the thread related stuff like locks, state
    // and especially split points. We also use per-thread pawn and material hash
    // tables so that once we get a pointer to an entry its life time is unlimited
    // and we don't have to care about someone changing the entry under our feet.
    class Thread
        : public std::thread
    {
    public:
        std::atomic_bool
              alive { true }
            , searching { false }
            , reset_check { false };

        Mutex mutex;

        ConditionVariable sleep_condition;

        Pawns   ::Table pawn_table;
        Material::Table matl_table;

        u16  index      = 0
           , pv_index   = 0
           , max_ply    = 0
           , chk_count  = 0;

        Position        root_pos;
        RootMoveVector  root_moves;
        Depth           root_depth = DEPTH_ZERO
            ,           leaf_depth = DEPTH_ZERO;
        HValueStats     history_values;
        MoveStats       counter_moves;

        Thread ();
        virtual ~Thread ();

        // notify_one () wakes up the thread when there is some work to do
        void notify_one ()
        {
            std::unique_lock<Mutex> lk (mutex);
            sleep_condition.notify_one ();
        }
        // wait_until() set the thread to sleep until 'condition' turns true
        void wait_until (const std::atomic_bool &condition)
        {
            std::unique_lock<Mutex> lk (mutex);
            sleep_condition.wait (lk, [&] { return bool(condition); });
        }
        // wait_while() set the thread to sleep until 'condition' turns false
        void wait_while (const std::atomic_bool &condition)
        {
            std::unique_lock<Mutex> lk (mutex);
            sleep_condition.wait (lk, [&] { return !bool(condition); });
        }
        // join() waits for thread to finish searching
        void join ()
        {
            std::unique_lock<Mutex> lk (mutex);
            sleep_condition.wait (lk, [&] { return !bool(searching); });
        }

        void idle_loop ();

        virtual void search ();
        
    };

    // MainThread class is derived class used to characterize the the main one
    class MainThread : public Thread
    {
        virtual void search () override;
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
        ThreadPool () = default;

        MainThread* main () const { return static_cast<MainThread*> (at (0)); }

        // No constructor and destructor, threadpool rely on globals
        // that should be initialized and valid during the whole thread lifetime.
        void initialize ();
        void deinitialize ();

        void start_thinking (const Position &pos, const LimitsT &limit, StateStackPtr &states);
        u64  game_nodes ();

        void configure ();

    };

}

enum SyncT { IO_LOCK, IO_UNLOCK };

// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
inline std::ostream& operator<< (std::ostream &os, SyncT sync)
{
    static Mutex io_mutex;

    if (sync == IO_LOCK)
    {
        io_mutex.lock ();
    }
    else
    if (sync == IO_UNLOCK)
    {
        io_mutex.unlock ();
    }
    return os;
}

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK


extern Threading::ThreadPool  Threadpool;

#endif // _THREAD_H_INC_
