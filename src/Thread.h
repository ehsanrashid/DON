#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _THREAD_H_INC_
#define _THREAD_H_INC_

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
typedef HANDLE              NativeHandle;

// On Windows 95 and 98 parameter lpThreadId my not be null
inline DWORD* dwWin9xKludge () { static DWORD dw; return &dw; }

#   define lock_init(x)          InitializeCriticalSection (&(x))
#   define lock_grab(x)          EnterCriticalSection (&(x))
#   define lock_release(x)       LeaveCriticalSection (&(x))
#   define lock_destroy(x)       DeleteCriticalSection (&(x))
#   define cond_init(x)          x = CreateEvent (0, FALSE, FALSE, 0);
#   define cond_destroy(x)       CloseHandle (x)
#   define cond_signal(x)        SetEvent (x)
#   define cond_wait(x,y)        { lock_release (y); WaitForSingleObject (x, INFINITE); lock_grab (y); }
#   define cond_timedwait(x,y,z) { lock_release (y); WaitForSingleObject (x, z); lock_grab (y); }
#   define thread_create(x,f,t)  x = CreateThread (NULL, 0, LPTHREAD_START_ROUTINE (f), t, 0, dwWin9xKludge ())
#   define thread_join(x)        { WaitForSingleObject (x, INFINITE); CloseHandle (x); }

#else    // Linux - Unix

#   include <pthread.h>
#   include <unistd.h>  // for sysconf()

typedef pthread_mutex_t     Lock;
typedef pthread_cond_t      WaitCondition;
typedef pthread_t           NativeHandle;
typedef void* (*FnStart) (void*);

#   define lock_init(x)     pthread_mutex_init (&(x), NULL)
#   define lock_grab(x)     pthread_mutex_lock (&(x))
#   define lock_release(x)  pthread_mutex_unlock (&(x))
#   define lock_destroy(x)  pthread_mutex_destroy (&(x))
#   define cond_init(x)     pthread_cond_init (&(x), NULL)
#   define cond_destroy(x)  pthread_cond_destroy (&(x))
#   define cond_signal(x)   pthread_cond_signal (&(x))
#   define cond_wait(x,y)   pthread_cond_wait (&(x), &(y))
#   define cond_timedwait(x,y,z)    pthread_cond_timedwait (&(x), &(y), z)
#   define thread_create(x,f,t)     pthread_create (&(x), NULL, FnStart (f), t)
#   define thread_join(x)   pthread_join (x, NULL)

#endif

namespace Threads {

    const uint8_t MAX_THREADS            = 64; // Because SplitPoint::slaves_mask is a uint64_t
    const uint8_t MAX_SPLITPOINT_THREADS = 8;  // Maximum threads per splitpoint
    const uint8_t MAX_SPLIT_DEPTH        = 15; // Maximum split depth

    extern void timed_wait (WaitCondition &sleep_cond, Lock &sleep_lock, int32_t msec);

    struct Mutex
    {
    private:
        friend struct ConditionVariable;
        Lock _lock;

    public:
        Mutex () { lock_init (_lock); }
        ~Mutex () { lock_destroy (_lock); }

        void lock () { lock_grab (_lock); }

        void unlock () { lock_release (_lock); }
    };

    struct ConditionVariable
    {
    private:
        WaitCondition condition;

    public:
        ConditionVariable () { cond_init (condition); }
        ~ConditionVariable () { cond_destroy (condition); }

        void wait (Mutex &m) { cond_wait (condition, m._lock); }

        void wait_for (Mutex &m, int32_t ms) { timed_wait (condition, m._lock, ms); }

        void notify_one () { cond_signal (condition); }
    };

    struct Thread;

    struct SplitPoint
    {
        // Const data after splitpoint has been setup
        const Searcher::Stack *ss;
        const Position *pos;

        Thread *master_thread;
        Depth   depth;
        Value   beta;

        Searcher::NodeT node_type;
        bool    cut_node;

        // Const pointers to shared data
        MovePicker  *movepicker;
        SplitPoint  *parent_splitpoint;

        // Shared data
        Mutex   mutex;

        uint64_t volatile slaves_mask;
        uint64_t volatile nodes;
        uint8_t  volatile moves_count;
        Value    volatile alpha;
        Value    volatile best_value;
        Move     volatile best_move;
        bool     volatile cut_off;
    };

    // ThreadBase struct is the base of the hierarchy from where
    // we derive all the specialized thread classes.
    struct ThreadBase
    {
        Mutex   mutex;
        NativeHandle    handle;
        bool volatile   exit;

        ConditionVariable   sleep_condition;

        ThreadBase ()
            : exit (false)
        {}

        virtual ~ThreadBase () {}

        virtual void idle_loop () = 0;

        void notify_one ();

        void wait_for (volatile const bool &cond);
    };

    // TimerThread is derived from ThreadBase
    // used for special purpose: the recurring timer.
    struct TimerThread
        : public ThreadBase
    {
        // This is the minimum interval in msec between two check_time() calls
        static const int32_t Resolution = 5;

        bool run;

        TimerThread ()
            : run (false)
        {}

        virtual void idle_loop ();

    };

    // Thread is derived from ThreadBase
    // Thread struct keeps together all the thread related stuff like locks, state
    // and especially splitpoints. We also use per-thread pawn-hash and material-hash tables
    // so that once get a pointer to a thread entry its life time is unlimited
    // and we don't have to care about someone changing the entry under our feet.
    struct Thread
        : public ThreadBase
    {
        SplitPoint splitpoints[MAX_SPLITPOINT_THREADS];
        
        Material::Table   material_table;
        Pawns   ::Table   pawns_table;
        EndGame::Endgames endgames;

        Position *active_pos;

        uint8_t idx
              , max_ply;

        SplitPoint* volatile active_splitpoint;

        uint8_t volatile splitpoint_threads;
        bool    volatile searching;

        Thread ();

        virtual void idle_loop ();

        bool cutoff_occurred () const;

        bool available_to (const Thread *master) const;

        template <bool FAKE>
        void split (Position &pos, const Searcher::Stack *ss, Value alpha, Value beta, Value &best_value, Move &best_move,
            Depth depth, uint8_t moves_count, MovePicker &movepicker, Searcher::NodeT node_type, bool cut_node);

    };

    // MainThread is derived from Thread
    // used for special purpose: the main thread.
    struct MainThread
        : public Thread
    {
        bool volatile thinking;

        MainThread ()
            : thinking (true)
        {} // Avoid a race with start_thinking ()

        virtual void idle_loop ();

    };

    // ThreadPool struct handles all the threads related stuff like initializing,
    // starting, parking and, the most important, launching a slave thread
    // at a splitpoint.
    // All the access to shared thread data is done through this class.
    struct ThreadPool
        : public std::vector<Thread*>
    {
        bool    sleep_idle;
        Depth   split_depth;
        Mutex   mutex;

        ConditionVariable sleep_condition;
        
        TimerThread *timer;

        MainThread* main () { return static_cast<MainThread*> ((*this)[0]); }

        // No c'tor and d'tor, threads rely on globals that should
        // be initialized and valid during the whole thread lifetime.
        void   initialize ();
        void deinitialize ();

        void configure ();

        Thread* available_slave (const Thread *master) const;

        void start_thinking (const Position &pos, const Searcher::LimitsT &limit, StateInfoStackPtr &states);

        void wait_for_think_finished ();
    };

    // timed_wait() waits for msec milliseconds. It is mainly an helper to wrap
    // conversion from milliseconds to struct timespec, as used by pthreads.
    inline void timed_wait (WaitCondition &sleep_cond, Lock &sleep_lock, int32_t msec)
    {

#if defined(_WIN32) || defined(_MSC_VER) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

        int32_t tm = msec;

#else    // Linux - Unix

        timespec ts
            ,   *tm = &ts;
        uint64_t ms = Time::now() + msec;

        ts.tv_sec = ms / Time::M_SEC;
        ts.tv_nsec = (ms % Time::M_SEC) * 1000000LL;

#endif

        cond_timedwait (sleep_cond, sleep_lock, tm);

    }

}

//#if __cplusplus > 199711L
//#   include <thread>
//#endif

inline uint32_t cpu_count ()
{

//#if __cplusplus > 199711L
//    // May return 0 when not able to detect
//    return std::thread::hardware_concurrency ();
//
//#else    

#   ifdef WIN32

    SYSTEM_INFO sys_info;
    GetSystemInfo (&sys_info);
    return sys_info.dwNumberOfProcessors;

#   elif MACOS

    uint32_t count;
    uint32_t len = sizeof (count);

    int32_t nm[2];
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

#   elif _SC_NPROCESSORS_ONLN // LINUX, SOLARIS, & AIX and Mac OS X (for all OS releases >= 10.4)

    return sysconf (_SC_NPROCESSORS_ONLN);

#   elif __IRIX

    return sysconf (_SC_NPROC_ONLN);

#   elif __HPUX

    pst_dynamic psd;
    return (pstat_getdynamic (&psd, sizeof (psd), 1, 0) == -1)
        ? 1 : psd.psd_proc_cnt;

    //return mpctl (MPC_GETNUMSPUS, NULL, NULL);

#   else

    return 1;

#   endif

//#endif

}

typedef enum SyncCout { IO_LOCK, IO_UNLOCK } SyncCout;

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK

// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
inline std::ostream& operator<< (std::ostream& os, const SyncCout &sc)
{
    static Threads::Mutex m;

    if      (sc == IO_LOCK)
    {
        m.lock ();
    }
    else if (sc == IO_UNLOCK)
    {
        m.unlock ();
    }
    return os;
}


extern Threads::ThreadPool  Threadpool;


#endif // _THREAD_H_INC_
