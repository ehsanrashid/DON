#ifndef THREAD_H_
#define THREAD_H_

#include <vector>

#include "Pawns.h"
#include "Material.h"
#include "MovePicker.h"
#include "Searcher.h"

// Because SplitPoint::slaves_mask is a uint64_t
const int32_t MAX_THREADS                = 64;
const int32_t MAX_SPLITPOINTS_PER_THREAD = 8;
const uint8_t MAX_SPLIT_DEPTH            = 99;

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

#   undef NOMINMAX
#   define NOMINMAX // disable macros min() and max()

#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   undef WIN32_LEAN_AND_MEAN
#   undef NOMINMAX

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
    int32_t                 node_type;
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

// ThreadBase struct is the base of the hierarchy from where we derive all the
// specialized thread classes.
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
    SplitPoint           split_points[MAX_SPLITPOINTS_PER_THREAD];
    Material::Table      material_table;
    Pawns   ::Table      pawns_table;
    EndGame ::Endgames   endgames;
    Position            *active_pos;
    size_t               idx;
    int32_t              max_ply;
    SplitPoint* volatile active_split_point;
    volatile int32_t     split_points_size;
    volatile bool        searching;

    Thread ();

    virtual void idle_loop();
    bool cutoff_occurred() const;
    bool available_to(const Thread* master) const;

    template <bool FAKE>
    void split (Position &pos, const Searcher::Stack* ss, Value alpha, Value beta, Value* best_value, Move* best_move,
        Depth depth, int32_t moves_count, MovePicker *move_picker, int32_t node_type, bool cut_node);

};

// MainThread and TimerThread are derived classes used to characterize the two
// special threads: the main one and the recurring timer.
struct MainThread
    : public Thread
{
    volatile bool thinking;

    MainThread()
        : thinking(true) {} // Avoid a race with start_thinking()

    virtual void idle_loop ();
};

struct TimerThread
    : public ThreadBase
{
    bool run;
    static const int32_t Resolution = 5; // msec between two check_time() calls

    TimerThread()
        : run(false) {}

    virtual void idle_loop();
};

// ThreadPool struct handles all the threads related stuff like init, starting,
// parking and, the most important, launching a slave thread at a split point.
// All the access to shared thread data is done through this class.
struct ThreadPool
    : public std::vector<Thread*>
{
    bool                sleep_while_idle;
    Depth               min_split_depth;
    size_t              max_threads_per_split_point;
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

extern ThreadPool Threads;

extern void prefetch (char *addr);

#endif // THREAD_H_
