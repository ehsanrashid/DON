#ifndef _THREAD_H_INC_
#define _THREAD_H_INC_

#include <bitset>
#include <thread>

#include "thread_win32.h"

#include "Position.h"
#include "Pawns.h"
#include "Material.h"
#include "Searcher.h"
#include "MovePicker.h"

namespace Threading {

    using namespace Searcher;
    using namespace MovePick;

    const u16 MAX_THREADS = 128; // Maximum Threads

    // Thread struct keeps together all the thread related stuff like.
    // It also use pawn and material hash tables so that once get a pointer
    // to an entry its life time is unlimited and don't have to care about
    // someone changing the entry under its feet.
    class Thread
    {
    private:
        std::thread       _native_thread;
        Mutex             _mutex;
        ConditionVariable _sleep_condition;

        bool  _alive     = true
            , _searching = false;

    public:
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

        std::atomic_bool reset_check { false };

        Thread ();
        virtual ~Thread ();

        // Thread::start_searching() wake up the thread that will start the search
        void start_searching (bool resume = false)
        {
            std::unique_lock<Mutex> lk (_mutex);
            if (!resume)
            {
                _searching = true;
            }
            _sleep_condition.notify_one ();
            lk.unlock ();
        }
        // Thread::wait_while_searching() wait on sleep condition until not searching
        void wait_while_searching ()
        {
            std::unique_lock<Mutex> lk (_mutex);
            _sleep_condition.wait (lk, [&] { return !_searching; });
            lk.unlock ();
        }

        // Thread::wait_until() set the thread to sleep until 'condition' turns true
        void wait_until (const std::atomic_bool &condition)
        {
            std::unique_lock<Mutex> lk (_mutex);
            _sleep_condition.wait (lk, [&] { return bool(condition); });
            lk.unlock ();
        }
        // Thread::wait_while() set the thread to sleep until 'condition' turns false
        void wait_while (const std::atomic_bool &condition)
        {
            std::unique_lock<Mutex> lk (_mutex);
            _sleep_condition.wait (lk, [&] { return !bool(condition); });
            lk.unlock ();
        }

        // Thread::idle_loop() is where the thread is parked when it has no work to do
        void idle_loop ()
        {
            while (_alive)
            {
                std::unique_lock<Mutex> lk (_mutex);

                _searching = false;

                while (_alive && !_searching)
                {
                    _sleep_condition.notify_one (); // Wake up main thread if needed
                    _sleep_condition.wait (lk);
                }

                lk.unlock ();

                if (_alive)
                {
                    search ();
                }
            }
        }

        virtual void search ();
    };

    // MainThread class is derived class used to characterize the the main one
    class MainThread
        : public Thread
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
        void wait_while_thinking ();
        u64  game_nodes ();

        void configure ();
    };

}

enum SyncT { IO_LOCK, IO_UNLOCK };

// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
inline std::ostream& operator<< (std::ostream &os, SyncT sync)
{
    static Mutex mutex;

    if (sync == IO_LOCK)
    {
        mutex.lock ();
    }
    else
    if (sync == IO_UNLOCK)
    {
        mutex.unlock ();
    }
    return os;
}

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK


extern Threading::ThreadPool  Threadpool;

#endif // _THREAD_H_INC_
