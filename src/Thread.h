#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>

#include "Pawns.h"
#include "Material.h"
#include "Position.h"
#include "Option.h"
#include "PRNG.h"
#include "Searcher.h"
#include "thread_win32_osx.h"

/// TimeManager class is used to computes the optimal time to think depending on the
/// maximum available time, the move game number and other parameters.
class TimeManager
{
private:
    u16 time_nodes;

public:
    TimePoint start_time;
    TimePoint optimum_time;
    TimePoint maximum_time;

    double time_reduction;

    u64 available_nodes;

    TimeManager()
        : available_nodes(0)
    {}
    TimeManager(const TimeManager&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;

    TimePoint elapsed_time() const;

    void set(Color, i16);
    void update(Color);
};

// MaxLevel should be <= MaxDepth/4
const i16 MaxLevel = 24;

/// Skill Manager class is used to implement strength limit
class SkillManager
{
private:

public:
    static PRNG prng;

    i16 level;
    Move best_move;

    SkillManager()
        : level(MaxLevel)
        , best_move(MOVE_NONE)
    {}
    SkillManager(const SkillManager&) = delete;
    SkillManager& operator=(const SkillManager&) = delete;

    bool enabled() const { return level < MaxLevel; }

    void set_level(i16 lvl)
    {
        level = lvl;
    }

    void pick_best_move();
};

/// Thread class keeps together all the thread-related stuff.
/// It use pawn and material hash tables so that once get a pointer to
/// an entry its life time is unlimited and we don't have to care about
/// someone changing the entry under our feet.
class Thread
{
protected:
    bool dead   // false
       , busy;  // true

    size_t index;

    std::mutex mtx;
    std::condition_variable condition_var;

    NativeThread native_thread;

public:

    Position  root_pos;
    RootMoves root_moves;

    Depth root_depth
        , finished_depth
        , sel_depth;

    u64   tt_hit_avg;

    i16   nmp_ply;
    Color nmp_color;

    u32    pv_beg
        ,  pv_cur
        ,  pv_end;

    std::atomic<u64> nodes
        ,            tb_hits;
    std::atomic<u32> pv_change;

    Score contempt;

    ButterflyHistory    butterfly_history;
    CaptureHistory      capture_history;
    std::array<std::array<ContinuationHistory, 2>, 2> continuation_history;

    MoveHistory         move_history;

    Pawns::Table        pawn_table;
    Material::Table     matl_table;

    explicit Thread(size_t);
    Thread() = delete;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    virtual ~Thread();

    void start();
    void wait_while_busy();

    void idle_function();

    i16 move_best_count(Move) const;

    virtual void clear();
    virtual void search();
};

/// MainThread class is derived from Thread class used specific for main thread.
class MainThread
    : public Thread
{
private :

public:

    u64 check_count;

    bool stop_on_ponderhit; // Stop search on ponderhit
    std::atomic<bool> ponder; // Search on ponder move until the "stop"/"ponderhit" command

    Value best_value;

    TimeManager time_mgr;
    SkillManager skill_mgr;

    std::array<Value, 4> iter_value;
    Move   best_move;
    i16    best_move_depth;

    explicit MainThread(size_t);
    MainThread() = delete;
    MainThread(const MainThread&) = delete;
    MainThread& operator=(const MainThread&) = delete;

    void clear() override;
    void search() override;

    void set_check_count();
    void tick();
};

namespace WinProcGroup {

    extern std::vector<i16> Groups;

    extern void initialize();
    extern void bind(size_t);
}

/// ThreadPool class handles all the threads related stuff like,
/// initializing & deinitializing, starting, parking & launching a thread
/// All the access to shared thread data is done through this class.
class ThreadPool
    : public std::vector<Thread*>
{
private:

    StateListPtr setup_states;

    template<typename T>
    T sum(std::atomic<T> Thread::*member) const
    {
        T s = 0;
        for (const auto *th : *this)
        {
            s += (th->*member).load(std::memory_order::memory_order_relaxed);
        }
        return s;
    }

public:

    u32 pv_limit;
    double factor;

    std::atomic<bool> stop // Stop search forcefully
        ,             research;

    ThreadPool() = default;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    MainThread* main_thread() const { return static_cast<MainThread*>(front()); }
    u64      nodes() const { return sum(&Thread::nodes); }
    u64    tb_hits() const { return sum(&Thread::tb_hits); }
    u32  pv_change() const { return sum(&Thread::pv_change); }

    const Thread* best_thread() const;

    void clear();
    void configure(u32);

    void start_thinking(Position&, StateListPtr&, const Limit&, const std::vector<Move>&, bool = false);
};


enum OutputState : u08
{
    OS_LOCK,
    OS_UNLOCK,
};

extern std::mutex OutputMutex;

/// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
inline std::ostream& operator<<(std::ostream &os, OutputState state)
{
    switch (state)
    {
    case OutputState::OS_LOCK:   OutputMutex.lock();   break;
    case OutputState::OS_UNLOCK: OutputMutex.unlock(); break;
    default: break;
    }
    return os;
}

#define sync_cout std::cout << OS_LOCK
#define sync_endl std::endl << OS_UNLOCK

// Global ThreadPool
extern ThreadPool Threadpool;
