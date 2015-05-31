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

    const size_t MAX_THREADS               = 128; // Maximum Threads
    const size_t MAX_SPLITPOINTS_PER_THREAD=   8; // Maximum Splitpoints/Thread
    const size_t MAX_SLAVES_PER_SPLITPOINT =   4;
    const size_t MAX_SPLIT_DEPTH           =  12; // Maximum SplitDepth

    struct Thread;

    class Spinlock
    {
    private:
        std::atomic_int _lock;

    public:
        Spinlock () { _lock = 1; } // Init here to workaround a bug with MSVC 2013
    
        void acquire ()
        {
            while (_lock.fetch_sub(1, std::memory_order_acquire) != 1)
            {
                while (_lock.load(std::memory_order_relaxed) <= 0)
                {
                    std::this_thread::yield(); // Be nice to hyperthreading
                }
            }
        }
        void release()
        {
            _lock.store(1, std::memory_order_release);
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
        volatile u08   legals;
        volatile Value alpha;
        volatile Value best_value;
        volatile Move  best_move;
        volatile u64   nodes;
        volatile bool  cut_off;
    };


    // ThreadBase class is the base of the hierarchy from where
    // derive all the specialized thread classes.
    class ThreadBase
        : public ::std::thread
    {
    public:
        Mutex               mutex;
        Spinlock            spinlock;
        ConditionVariable   sleep_condition;

        volatile bool       alive;

        ThreadBase ()
            : alive (true)
        {}

        virtual ~ThreadBase() = default;

        void notify_one ();

        void wait_for (const volatile bool &condition);

        virtual void idle_loop () = 0;
    };


    // Thread struct keeps together all the thread related stuff like locks, state
    // and especially split points. We also use per-thread pawn and material hash
    // tables so that once we get a pointer to an entry its life time is unlimited
    // and we don't have to care about someone changing the entry under our feet.
    struct Thread
        : public ThreadBase
    {
        SplitPoint      splitpoints[MAX_SPLITPOINTS_PER_THREAD];
        Pawns   ::Table pawn_table;
        Material::Table matl_table;
        //Endgames endgames;
        Position   *active_pos;
        size_t      index;
        i32         max_ply;

        SplitPoint* volatile active_splitpoint;
        volatile size_t      splitpoint_count;
        volatile bool        searching;

        Thread ();
        
        virtual void idle_loop ();
        
        bool cutoff_occurred () const;
        
        bool can_join (const SplitPoint *sp) const;

        void split (Position &pos, Stack *ss, Value alpha, Value beta, Value &best_value, Move &best_move,
            Depth depth, u08 legals, MovePicker &movepicker, NodeT node_type, bool cut_node);

    };


    // MainThread and TimerThread are derived classes used to characterize the two
    // special threads: the main one and the recurring timer.

    struct MainThread
        : public Thread
    {
        volatile bool thinking = true; // Avoid a race with start_thinking()

        virtual void idle_loop ();
        void join ();
    };

    const i32 TIMER_RESOLUTION = 5; // Millisec between two check_time() calls

    struct TimerThread
        : public ThreadBase
    {

        bool run;
        i32 resolution; // Millisec between two check_time() calls
        void (*task) ();
        
        TimerThread () { stop (); }

        void start () { run = true ; }
        void stop  () { run = false; }

        virtual void idle_loop();

    };

    // ThreadPool struct handles all the threads related stuff like init, starting,
    // parking and, most importantly, launching a slave thread at a split point.
    // All the access to shared thread data is done through this class.

    struct ThreadPool
        : public ::std::vector<Thread*>
    {
        TimerThread *check_limits_th;
        TimerThread *auto_save_th;
        Depth        split_depth;

        MainThread* main () { return static_cast<MainThread*> (at (0)); }

        // No c'tor and d'tor, threadpool rely on globals that should
        // be initialized and valid during the whole thread lifetime.
        void initialize ();
        void exit ();

        Thread* available_slave (const SplitPoint *sp) const;

        void start_main (const Position &pos, const LimitsT &limit, StateInfoStackPtr &states);

        void configure ();

    };


    template<class T>
    extern T* new_thread ();

    extern void delete_thread (ThreadBase *th);

    extern void check_limits ();
    extern void auto_save ();

}


enum SyncT { IO_LOCK, IO_UNLOCK };

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK

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

extern Threading::ThreadPool  Threadpool;

//inline u32 cpu_count ()
//{
//#ifdef WIN32
//
//    SYSTEM_INFO sys_info;
//    GetSystemInfo (&sys_info);
//    return sys_info.dwNumberOfProcessors;
//
//#elif MACOS
//
//    u32 count;
//    u32 len = sizeof (count);
//
//    i32 nm[2];
//    nm[0] = CTL_HW;
//    nm[1] = HW_AVAILCPU;
//    sysctl (nm, 2, &count, &len, NULL, 0);
//    if (count < 1)
//    {
//        nm[1] = HW_NCPU;
//        sysctl (nm, 2, &count, &len, NULL, 0);
//        if (count < 1) count = 1;
//    }
//    return count;
//
//#elif _SC_NPROCESSORS_ONLN // LINUX, SOLARIS, & AIX and Mac OS X (for all OS releases >= 10.4)
//
//    return sysconf (_SC_NPROCESSORS_ONLN);
//
//#elif __IRIX
//
//    return sysconf (_SC_NPROC_ONLN);
//
//#elif __HPUX
//
//    pst_dynamic psd;
//    return pstat_getdynamic (&psd, sizeof (psd), 1, 0) == -1 ?
//        1 : psd.psd_proc_cnt;
//
//    //return mpctl (MPC_GETNUMSPUS, NULL, NULL);
//
//#else
//
//    return 1;
//
//#endif
//}


#endif // _THREAD_H_INC_
