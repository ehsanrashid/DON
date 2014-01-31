#ifndef THREAD_H_
#define THREAD_H_

//#include <string>
//#include <vector>

#include "Pawns.h"
#include "Material.h"
#include "MovePicker.h"
#include "Searcher.h"

const int32_t MAX_THREADS             = 64; // Because SplitPoint::slaves_mask is a uint64_t
const int32_t MAX_THREADS_SPLIT_POINT = 8;  // Maximum threads per split point
const int32_t MAX_SPLIT_DEPTH         = 15; // Maximum split depth

#ifndef _WIN32 // Linux - Unix

#   include <pthread.h>

typedef pthread_mutex_t Lock;
typedef pthread_cond_t WaitCondition;
typedef pthread_t NativeHandle;
typedef void*(*pt_start_fn)(void*);

#   define lock_init(x)     pthread_mutex_init(&(x), NULL)
#   define lock_grab(x)     pthread_mutex_lock(&(x))
#   define lock_release(x)  pthread_mutex_unlock(&(x))
#   define lock_destroy(x)  pthread_mutex_destroy(&(x))
#   define cond_destroy(x)  pthread_cond_destroy(&(x))
#   define cond_init(x)     pthread_cond_init(&(x), NULL)
#   define cond_signal(x)   pthread_cond_signal(&(x))
#   define cond_wait(x,y)   pthread_cond_wait(&(x),&(y))
#   define cond_timedwait(x,y,z)    pthread_cond_timedwait(&(x),&(y),z)
#   define thread_create(x,f,t)     pthread_create(&(x),NULL,(pt_start_fn)f,t)
#   define thread_join(x)   pthread_join(x, NULL)

#else // Windows and MinGW

// disable macros min() and max()
#   ifndef  NOMINMAX
#       define NOMINMAX
#endif
#   ifndef  WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#endif

#   include <windows.h>

// We use critical sections on Windows to support Windows XP and older versions,
// unfortunatly cond_wait() is racy between lock_release() and WaitForSingleObject()
// but apart from this they have the same speed performance of SRW locks.
typedef CRITICAL_SECTION Lock;
typedef HANDLE WaitCondition;
typedef HANDLE NativeHandle;

// On Windows 95 and 98 parameter lpThreadId my not be null
inline DWORD* dwWin9xKludge () { static DWORD dw; return &dw; }

#   define lock_init(x)          InitializeCriticalSection (&(x))
#   define lock_grab(x)          EnterCriticalSection (&(x))
#   define lock_release(x)       LeaveCriticalSection (&(x))
#   define lock_destroy(x)       DeleteCriticalSection (&(x))
#   define cond_init(x)          { x = CreateEvent (0, FALSE, FALSE, 0); }
#   define cond_destroy(x)       CloseHandle (x)
#   define cond_signal(x)        SetEvent (x)
#   define cond_wait(x,y)        { lock_release (y); WaitForSingleObject (x, INFINITE); lock_grab (y); }
#   define cond_timedwait(x,y,z) { lock_release (y); WaitForSingleObject (x, z); lock_grab (y); }
#   define thread_create(x,f,t)  (x = CreateThread (NULL, 0, (LPTHREAD_START_ROUTINE)f, t, 0, dwWin9xKludge ()))
#   define thread_join(x)        { WaitForSingleObject (x, INFINITE); CloseHandle (x); }

#endif

extern void timed_wait (WaitCondition &sleep_cond, Lock &sleep_lock, int32_t msec);


struct Mutex
{
private:
    friend struct ConditionVariable;

    Lock l;

public:
    Mutex()         { lock_init (l); }
    ~Mutex()        { lock_destroy (l); }

    void lock()     { lock_grab (l); }
    void unlock()   { lock_release (l); }
};

struct ConditionVariable
{
    ConditionVariable()     { cond_init (c); }
    ~ConditionVariable()    { cond_destroy (c); }

    void wait(Mutex &m)     { cond_wait (c, m.l); }
    void wait_for(Mutex &m, int32_t ms) { timed_wait (c, m.l, ms); }
    void notify_one ()      { cond_signal (c); }

private:
    WaitCondition c;
};

struct Thread;

struct SplitPoint
{
    // Const data after split point has been setup
    const Position         *pos;
    const Searcher::Stack  *ss;
    Thread                 *master_thread;
    Depth                   depth;
    Value                   beta;
    int8_t                  node_type;
    bool                    cut_node;

    // Const pointers to shared data
    MovePicker             *move_picker;
    SplitPoint             *parent_split_point;

    // Shared data
    Mutex                   mutex;
    volatile uint64_t       slaves_mask;
    volatile int64_t        nodes;
    volatile Value          alpha;
    volatile Value          best_value;
    volatile Move           best_move;
    volatile int32_t        moves_count;
    volatile bool           cut_off;
};

// ThreadBase struct is the base of the hierarchy from where
// we derive all the specialized thread classes.
struct ThreadBase
{
    Mutex               mutex;
    ConditionVariable   sleep_condition;
    NativeHandle        handle;
    volatile bool       exit;

    ThreadBase()
        : exit(false) {}
    virtual ~ThreadBase() {}

    virtual void idle_loop() = 0;

    void notify_one ();
    void wait_for (volatile const bool &b);
};

// Thread struct keeps together all the thread related stuff like locks, state
// and especially split points. We also use per-thread pawn and material hash
// tables so that once we get a pointer to an entry its life time is unlimited
// and we don't have to care about someone changing the entry under our feet.
struct Thread
    : public ThreadBase
{
    SplitPoint           split_points[MAX_THREADS_SPLIT_POINT];
    Material::Table      material_table;
    Pawns   ::Table      pawns_table;
    EndGame ::Endgames   endgames;
    Position            *active_pos;
    uint32_t             idx;
    uint8_t              max_ply;
    SplitPoint* volatile active_split_point;
    volatile uint8_t     threads_split_point;
    volatile bool        searching;

    Thread ();

    virtual void idle_loop();
    bool cutoff_occurred() const;
    bool available_to(const Thread* master) const;

    template <bool FAKE>
    void split (Position &pos, const Searcher::Stack ss[], Value alpha, Value beta, Value* best_value, Move* best_move,
        Depth depth, int32_t moves_count, MovePicker *move_picker, int8_t node_type, bool cut_node);

};

// MainThread and TimerThread are derived classes used to characterize the two
// special threads: the main one and the recurring timer.
struct MainThread
    : public Thread
{
    volatile bool thinking;

    MainThread()
        : thinking (true) {} // Avoid a race with start_thinking()

    virtual void idle_loop ();
};

struct TimerThread
    : public ThreadBase
{
    // This is the minimum interval in msec between two check_time() calls
    static const int32_t Resolution = 5;

    bool run;

    TimerThread()
        : run(false) {}

    virtual void idle_loop ();

};

// ThreadPool struct handles all the threads related stuff like init, starting,
// parking and, the most important, launching a slave thread at a split point.
// All the access to shared thread data is done through this class.
struct ThreadPool
    : public std::vector<Thread*>
{
    bool                sleep_idle;
    Depth               split_depth;
    size_t              threads_split_point;
    Mutex               mutex;
    ConditionVariable   sleep_condition;
    TimerThread        *timer;

    // No c'tor and d'tor, threads rely on globals that should
    // be initialized and valid during the whole thread lifetime.
    void initialize (); 
    void deinitialize (); 

    MainThread* main () { return static_cast<MainThread*> ((*this)[0]); }

    void read_uci_options();

    Thread* available_slave (const Thread *master) const;

    void start_thinking (const Position &pos, const Searcher::Limits_t &limit, StateInfoStackPtr &states);

    void wait_for_think_finished ();
};

// timed_wait() waits for msec milliseconds. It is mainly an helper to wrap
// conversion from milliseconds to struct timespec, as used by pthreads.
inline void timed_wait (WaitCondition &sleep_cond, Lock &sleep_lock, int32_t msec)
{
#ifdef _WIN32
    int32_t tm = msec;
#else
    timespec ts, *tm = &ts;
    uint64_t ms = Time::now() + msec;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000LL;
#endif

    cond_timedwait (sleep_cond, sleep_lock, tm);
}

#if __cplusplus > 199711L
#   include<thread>
#endif

inline int32_t cpu_count ()
{

#if __cplusplus > 199711L
    // May return 0 when not able to detect
    return std::thread::hardware_concurrency ();

#else    

#   if defined(WIN32)

    SYSTEM_INFO sys_info;
    GetSystemInfo (&sys_info);
    return sys_info.dwNumberOfProcessors;

#   elif defined(MACOS)

    uint32_t count;
    size_t len = sizeof (count);

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

#   elif defined(_SC_NPROCESSORS_ONLN) // LINUX, SOLARIS, & AIX and Mac OS X (for all OS releases >= 10.4)

    return sysconf (_SC_NPROCESSORS_ONLN);

#   elif defined(__HPUX)

    pst_dynamic psd;
    return (pstat_getdynamic (&psd, sizeof (psd), size_t (1), 0) == -1) ?
        1 : psd.psd_proc_cnt;

    //return mpctl (MPC_GETNUMSPUS, NULL, NULL);

#   elif defined(__IRIX)

    return sysconf (_SC_NPROC_ONLN);

#   else

    return 1;

#   endif

#endif

}


typedef enum SyncCout { IO_LOCK, IO_UNLOCK } SyncCout;

// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
inline std::ostream& operator<< (std::ostream& os, SyncCout sc)
{
  static Mutex m;

  if      (IO_LOCK == sc)
      m.lock ();
  else if (IO_UNLOCK == sc)
      m.unlock ();

  return os;
}

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK


extern ThreadPool Threads;

extern void prefetch (char *addr);

//inline void cpu_id (uint32_t regs[4], int32_t i)
//{
//#ifdef _WIN32
//
//    __cpuid ((int32_t *) regs, i);
//
//#else
//
//    asm volatile
//        ("cpuid" : "=a" (regs[0]), "=b" (regs[1]), "=c" (regs[2]), "=d" (regs[3])
//        : "a" (i), "c" (0));
//    // ECX is set to zero for CPUID function 4
//#endif
//}
//
//inline std::string cpu_feature ()
//{
//    uint32_t regs[4];
//
//    // Get vendor
//    char vendor[12];
//    cpu_id (regs, 0);
//    ((int32_t *)vendor)[0] = regs[1]; // EBX
//    ((int32_t *)vendor)[1] = regs[3]; // EDX
//    ((int32_t *)vendor)[2] = regs[2]; // ECX
//    std::string cpu_vendor = std::string (vendor);
//
//    std::ostringstream ss;
//
//    cpu_id (regs, 1);
//    // Get CPU features
//    uint32_t cpuFeatures = regs[3]; // EDX
//    // Logical core count per CPU
//    uint32_t logical = (regs[1] >> 16) & 0xff; // EBX[23:16]
//    ss << " logical cpus: " << logical << std::endl;
//    uint32_t cores = logical;
//
//    if (cpu_vendor == "GenuineIntel")
//    {
//        // Get DCP cache info
//        cpu_id (regs, 2);
//        cores = ((regs[0] >> 26) & 0x3f) + 1; // EAX[31:26] + 1
//    } 
//    else if (cpu_vendor == "AuthenticAMD")
//    {
//        // Get NC: Number of CPU cores - 1
//        cpu_id (regs, 0x80000008);
//        cores = ((uint32_t)(regs[2] & 0xff)) + 1; // ECX[7:0] + 1
//    }
//
//    ss << "    cpu cores: " << cores << std::endl;
//
//    // Detect hyper-threads  
//    bool hyper_threads = cpuFeatures & (1 << 28) && cores < logical;
//
//    ss << "hyper-threads: " << std::boolalpha <<  hyper_threads << std::endl;
//
//    return ss.str ();
//}


#endif // THREAD_H_
