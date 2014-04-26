#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _THREAD_H_INC_
#define _THREAD_H_INC_

#include <bitset>
#include <vector>

#include "Position.h"
#include "Pawns.h"
#include "Material.h"
#include "MovePicker.h"
#include "Searcher.h"

// Windows or MinGW
#if defined(_WIN32) || defined(_MSC_VER) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

#   ifndef  NOMINMAX
#       define NOMINMAX // disable macros min() and max()
#   endif
#   ifndef  WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif

#   include <windows.h>

#   undef WIN32_LEAN_AND_MEAN
#   undef NOMINMAX


// We use critical sections on Windows to support Windows XP and older versions,
// unfortunatly cond_wait() is racy between lock_release() and WaitForSingleObject()
// but apart from this they have the same speed performance of SRW locks.
typedef CRITICAL_SECTION    Lock;
typedef HANDLE              WaitCondition;
typedef HANDLE              Handle;

// On Windows 95 and 98 parameter lpThreadId my not be null
inline DWORD* dwWin9xKludge () { static DWORD dw; return &dw; }

#   define lock_create(x)        InitializeCriticalSection (&(x))
#   define lock_grab(x)          EnterCriticalSection (&(x))
#   define lock_release(x)       LeaveCriticalSection (&(x))
#   define lock_destroy(x)       DeleteCriticalSection (&(x))
#   define cond_create(h)        h = CreateEvent (NULL, FALSE, FALSE, NULL);
#   define cond_destroy(h)       CloseHandle (h)
#   define cond_signal(h)        SetEvent (h)
#   define cond_wait(c,l)        { lock_release (l); WaitForSingleObject (c, INFINITE); lock_grab (l); }
#   define cond_timedwait(c,l,t) { lock_release (l); WaitForSingleObject (c, t); lock_grab (l); }
#   define thread_create(h,f,t)  h = CreateThread (NULL, 0, LPTHREAD_START_ROUTINE (f), t, 0, dwWin9xKludge ())
#   define thread_join(h)        { WaitForSingleObject (h, INFINITE); CloseHandle (h); }

#else    // Linux - Unix

#   include <pthread.h>
#   include <unistd.h>  // for sysconf()

typedef pthread_mutex_t     Lock;
typedef pthread_cond_t      WaitCondition;
typedef pthread_t           Handle;
typedef void* (*StartRoutine) (void*);

#   define lock_create(x)        pthread_mutex_init (&(x), NULL)
#   define lock_grab(x)          pthread_mutex_lock (&(x))
#   define lock_release(x)       pthread_mutex_unlock (&(x))
#   define lock_destroy(x)       pthread_mutex_destroy (&(x))
#   define cond_create(h)        pthread_cond_init (&(h), NULL)
#   define cond_destroy(h)       pthread_cond_destroy (&(h))
#   define cond_signal(h)        pthread_cond_signal (&(h))
#   define cond_wait(c,l)        pthread_cond_wait (&(c), &(l))
#   define cond_timedwait(c,l,t) pthread_cond_timedwait (&(c), &(l), t)
#   define thread_create(h,f,t)  pthread_create (&(h), NULL, StartRoutine (f), t)
#   define thread_join(h)        pthread_join (h, NULL)

#endif

namespace Threads {

    using namespace Searcher;

    const u08   MAX_THREADS            = 128; // Maximum threads
    const u08   MAX_SPLITPOINT_THREADS =   8; // Maximum threads per splitpoint
    const u08   MAX_SPLIT_DEPTH        =  15; // Maximum split depth

    struct Mutex
    {
    private:
        Lock _lock;
        
        friend struct Condition;

    public:
        Mutex () { lock_create (_lock); }
       ~Mutex () { lock_destroy (_lock); }

        void   lock () { lock_grab (_lock); }
        void unlock () { lock_release (_lock); }
    };

    // cond_timed_wait() waits for msec milliseconds. It is mainly an helper to wrap
    // conversion from milliseconds to struct timespec, as used by pthreads.
    inline void cond_timed_wait (WaitCondition &sleep_cond, Lock &sleep_lock, i32 msec)
    {

#if defined(_WIN32) || defined(_MSC_VER) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

        i32 tm = msec;

#else    // Linux - Unix

        timespec ts
            ,   *tm = &ts;
        u64 ms = Time::now() + msec;

        ts.tv_sec = ms / Time::M_SEC;
        ts.tv_nsec = (ms % Time::M_SEC) * 1000000LL;

#endif

        cond_timedwait (sleep_cond, sleep_lock, tm);

    }

    struct Condition
    {
    private:
        WaitCondition _condition;

    public:
        Condition () { cond_create  (_condition); }
       ~Condition () { cond_destroy (_condition); }

        void wait (Mutex &mutex) { cond_wait (_condition, mutex._lock); }

        void wait_for (Mutex &mutex, i32 ms) { cond_timed_wait (_condition, mutex._lock, ms); }

        void notify_one () { cond_signal (_condition); }

    };


    class Thread;

    // SplitPoint struct
    struct SplitPoint
    {

    public:
        // Const data after splitpoint has been setup
        const Stack    *ss;
        const Position *pos;

        Thread *master;
        Value   beta;
        Depth   depth;
        NodeT   node_type;
        bool    cut_node;
        Mutex   mutex;

        // Const pointers to shared data
        MovePicker  *movepicker;
        SplitPoint  *parent_splitpoint;

        // Shared data
        std::bitset<MAX_THREADS> slaves_mask;
        volatile u08   moves_count;
        volatile Value alpha;
        volatile Value best_value;
        volatile Move  best_move;
        volatile u64   nodes;
        volatile bool  cut_off;
    };

    // ThreadBase class is the base of the hierarchy from where
    // we derive all the specialized thread classes.
    class ThreadBase
    {
    protected:
        Condition     sleep_condition;
        volatile bool  exit;

    public:
        Mutex   mutex;
        Handle  native_handle;

        ThreadBase ()
            : exit (false)
            , native_handle (Handle ())
        {}

        virtual ~ThreadBase () {}

        void stop () { exit = true; }
        void notify_one ();

        void wait_for (const volatile bool &condition);

        virtual void idle_loop () = 0;
    };

    // TimerThread is derived from ThreadBase class
    // It's used for special purpose: the recurring timer.
    class TimerThread
        : public ThreadBase
    {
    private:
        bool run;

    public:
        // This is the minimum interval in msec between two check_time() calls
        static const i32 Resolution = 5;

        TimerThread () : run (false) {}

        void start () { run = true ; }
        void stop  () { run = false; }
        bool running () const { return run; }

        virtual void idle_loop ();

    };

    // Thread is derived from ThreadBase class
    // Thread class keeps together all the thread related stuff like locks, state
    // and especially splitpoints. We also use per-thread pawn-hash and material-hash tables
    // so that once get a pointer to a thread entry its life time is unlimited
    // and we don't have to care about someone changing the entry under our feet.
    class Thread
        : public ThreadBase
    {

    public:
        SplitPoint splitpoints[MAX_SPLITPOINT_THREADS];
        
        Material::Table  material_table;
        Pawns   ::Table  pawns_table;

        Position        *active_pos;

        u08              idx
            ,            max_ply;

        SplitPoint* volatile active_splitpoint;
        volatile    u08      splitpoint_threads;
        volatile    bool     searching;

        Thread ();

        virtual void idle_loop ();

        bool cutoff_occurred () const;

        bool available_to (const Thread *master) const;

        template <bool FAKE>
        void split (Position &pos, const Stack *ss, Value alpha, Value beta, Value &best_value, Move &best_move,
            Depth depth, u08 moves_count, MovePicker &movepicker, NodeT node_type, bool cut_node);

    };

    // MainThread is derived from Thread
    // It's used for special purpose: the main thread.
    class MainThread
        : public Thread
    {

    public:
        volatile bool thinking;

        MainThread () : thinking (true) {} // Avoid a race with start_thinking ()

        virtual void idle_loop ();

    };

    // ThreadPool class handles all the threads related stuff like initializing,
    // starting, parking and, the most important, launching a slave thread
    // at a splitpoint.
    // All the access to shared thread data is done through this class.
    class ThreadPool
        : public std::vector<Thread*>
    {

    public:
        bool    idle_sleep;
        Depth   split_depth;
        Mutex   mutex;

        Condition   sleep_condition;
        
        TimerThread *timer;
        MainThread* main () { return static_cast<MainThread*> ((*this)[0]); }

        // No c'tor and d'tor, threads rely on globals that should
        // be initialized and valid during the whole thread lifetime.
        void   initialize ();
        void deinitialize ();

        void configure ();

        Thread* available_slave (const Thread *master) const;

        void start_thinking (const Position &pos, const LimitsT &limit, StateInfoStackPtr &states);

        void wait_for_think_finished ();

    };

}

inline u32 cpu_count ()
{
#ifdef WIN32

    SYSTEM_INFO sys_info;
    GetSystemInfo (&sys_info);
    return sys_info.dwNumberOfProcessors;

#elif MACOS

    u32 count;
    u32 len = sizeof (count);

    i32 nm[2];
    nm[0] = CTL_HW;
    nm[1] = HW_AVAILCPU;
    sysctl (nm, 2, &count, &len, NULL, 0);
    if (count < 1)
    {
        nm[1] = HW_NCPU;
        sysctl (nm, 2, &count, &len, NULL, 0);
        if (count < 1) count = 1;
    }
    return count;

#elif _SC_NPROCESSORS_ONLN // LINUX, SOLARIS, & AIX and Mac OS X (for all OS releases >= 10.4)

    return sysconf (_SC_NPROCESSORS_ONLN);

#elif __IRIX

    return sysconf (_SC_NPROC_ONLN);

#elif __HPUX

    pst_dynamic psd;
    return (pstat_getdynamic (&psd, sizeof (psd), 1, 0) == -1)
        ? 1 : psd.psd_proc_cnt;

    //return mpctl (MPC_GETNUMSPUS, NULL, NULL);

#else

    return 1;

#endif
}

enum SyncT { IO_LOCK, IO_UNLOCK };

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK

// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
inline std::ostream& operator<< (std::ostream &os, const SyncT &sync)
{
    static Threads::Mutex mutex;

    if      (sync == IO_LOCK)
    {
        mutex.lock ();
    }
    else if (sync == IO_UNLOCK)
    {
        mutex.unlock ();
    }
    return os;
}

extern Threads::ThreadPool  Threadpool;

#endif // _THREAD_H_INC_
