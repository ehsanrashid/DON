#pragma once

#include <atomic>
#include <condition_variable>
//#include <fstream>
#include <mutex>
#include <vector>

#include "Material.h"
#include "Pawns.h"

#include "Option.h"
#include "Position.h"
#include "PRNG.h"
#include "RootMove.h"
#include "thread_win32_osx.h"
#include "Type.h"

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

    i16  level;
    Move best_move;

    SkillManager()
        : level(MaxLevel)
        , best_move(MOVE_NONE)
    {}
    SkillManager(const SkillManager&) = delete;
    SkillManager& operator=(const SkillManager&) = delete;

    bool enabled() const { return level < MaxLevel; }

    void set_level(i16 lvl) { level = lvl; }

    void pick_best_move();
};

// Threshold for counter moves based pruning
constexpr i32 CounterMovePruneThreshold = 0;

/// StatsEntry stores the stats table value. It is usually a number but could
/// be a move or even a nested history. We use a class instead of naked value
/// to directly call history update operator<<() on the entry so to use stats
/// tables at caller sites as simple multi-dim arrays.
template<typename T, i32 D>
class StatsEntry
{
private:
    T entry;

public:

    void operator=(const T &v) { entry = v; }

    T* operator&()             { return &entry; }
    T* operator->()            { return &entry; }

    operator const T&() const  { return entry; }

    void operator<<(i32 bonus)
    {
        static_assert (D <= std::numeric_limits<T>::max(), "D overflows T");
        assert(abs(bonus) <= D); // Ensure range is [-D, +D]

        entry += T(bonus - entry * abs(bonus) / D);

        assert(abs(entry) <= D);
    }
};

/// Stats is a generic N-dimensional array used to store various statistics.
/// The first template T parameter is the base type of the array,
/// the D parameter limits the range of updates (range is [-D, +D]), and
/// the last parameters (Size and Sizes) encode the dimensions of the array.
template <typename T, i32 D, i32 Size, i32... Sizes>
struct Stats
    : public std::array<Stats<T, D, Sizes...>, Size>
{
    typedef Stats<T, D, Size, Sizes...> stats;

    void fill(const T &v)
    {
        // For standard-layout 'this' points to first struct member
        assert(std::is_standard_layout<stats>::value);

        typedef StatsEntry<T, D> Entry;
        auto *p = reinterpret_cast<Entry*>(this);
        std::fill(p, p + sizeof (*this) / sizeof (Entry), v);
    }
};
template <typename T, i32 D, i32 Size>
struct Stats<T, D, Size>
    : public std::array<StatsEntry<T, D>, Size>
{};

/// ButterflyHistory records how often quiet moves have been successful or unsuccessful
/// during the current search, and is used for reduction and move ordering decisions, indexed by [color][move].
typedef Stats<i16, 10692, CLR_NO, SQ_NO*SQ_NO>              ButterflyHistory;
/// CaptureHistory stores capture history, indexed by [piece][square][captured type]
typedef Stats<i16, 10692, MAX_PIECE, SQ_NO, PT_NO>          CaptureHistory;
/// PieceDestinyHistory is like ButterflyHistory, indexed by [piece][square]
typedef Stats<i16, 29952, MAX_PIECE, SQ_NO>                 PieceDestinyHistory;
/// ContinuationHistory is the combined history of a given pair of moves, usually the current one given a previous one.
/// The nested history table is based on PieceDestinyHistory, indexed by [piece][square]
typedef Stats<PieceDestinyHistory, 0, MAX_PIECE, SQ_NO>     ContinuationHistory;

/// MoveHistory stores moves, indexed by [piece][square][size=2]
typedef std::array<std::array<Move, SQ_NO>, MAX_PIECE>   MoveHistory;


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

    u32   pv_beg
        , pv_cur
        , pv_end;

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

    TimeManager  time_mgr;
    SkillManager skill_mgr;

    std::array<Value, 4> iter_value;
    Move best_move;
    i16  best_move_depth;

    TimePoint debug_time;

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

/// Limit stores information sent by GUI about available time to search the current move.
///  - Time and Increment
///  - Moves to go
///  - Depth
///  - Nodes
///  - Mate
///  - Infinite analysis mode
struct Limit
{
public:
    // Clock struct stores the time and inc per move in milli-seconds.
    struct Clock
    {
        TimePoint time;
        TimePoint inc;

        Clock()
            : time(0)
            , inc(0)
        {}
    };
    std::array<Clock, CLR_NO> clock; // Search with Clock

    u08       movestogo;    // Search <x> moves to the next time control

    TimePoint movetime;    // Search <x> exact time in milli-seconds
    Depth     depth;       // Search <x> depth(plies) only
    u64       nodes;       // Search <x> nodes only
    u08       mate;        // Search mate in <x> moves
    bool      infinite;    // Search until the "stop" command

    Limit()
        : clock()
        , movestogo(0)
        , movetime(0)
        , depth(DEP_ZERO)
        , nodes(0)
        , mate(0)
        , infinite(false)
    {}

    bool time_mgr_used() const
    {
        return !infinite
            && 0 == movetime
            && DEP_ZERO == depth
            && 0 == nodes
            && 0 == mate;
    }

    bool mate_on() const
    {
        return 0 != mate;
    }
};

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

    double factor;

    Limit limit;
    u32   pv_limit;
    i32   basic_contempt;

    std::atomic<bool> stop // Stop search forcefully
        ,             research;

    //std::ofstream output_stream;

    ThreadPool() = default;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    MainThread* main_thread() const { return static_cast<MainThread*>(front()); }
    u64      nodes() const { return sum(&Thread::nodes); }
    u64    tb_hits() const { return sum(&Thread::tb_hits); }
    u32  pv_change() const { return sum(&Thread::pv_change); }

    Thread* best_thread() const;

    void clear();
    void configure(u32);

    void start_thinking(Position&, StateListPtr&, const Limit&, const std::vector<Move>&, bool = false);
};

// Global ThreadPool
extern ThreadPool Threadpool;
