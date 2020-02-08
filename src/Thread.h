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
    u16 timeNodes;

public:
    TimePoint startTime;
    TimePoint optimumTime;
    TimePoint maximumTime;

    u64 availableNodes;

    TimeManager()
    {
        reset();
    }
    TimeManager(const TimeManager&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;

    TimePoint elapsedTime() const;

    void reset() { availableNodes = 0; }
    void set(Color, i16);
    void update(Color);
};

// MaxLevel should be <= MaxDepth/9
const i16 MaxLevel = 25;

/// Skill Manager class is used to implement strength limit
class SkillManager
{
private:

public:
    static PRNG prng;

    i16  level;
    Move bestMove;

    SkillManager()
        : level(MaxLevel)
        , bestMove(MOVE_NONE)
    {}
    SkillManager(const SkillManager&) = delete;
    SkillManager& operator=(const SkillManager&) = delete;

    bool enabled() const { return level < MaxLevel; }

    void pickBestMove();
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
typedef Stats<i16, 10692, CLR_NO, SQ_NO*SQ_NO>          ButterflyHistory;
/// CaptureHistory stores capture history, indexed by [piece][square][captured type]
typedef Stats<i16, 10692, MAX_PIECE, SQ_NO, PT_NO>      CaptureHistory;
/// PieceDestinyHistory is like ButterflyHistory, indexed by [piece][square]
typedef Stats<i16, 29952, MAX_PIECE, SQ_NO>             PieceDestinyHistory;
/// ContinuationHistory is the combined history of a given pair of moves, usually the current one given a previous one.
/// The nested history table is based on PieceDestinyHistory, indexed by [piece][square]
typedef Stats<PieceDestinyHistory, 0, MAX_PIECE, SQ_NO> ContinuationHistory;

/// MoveHistory stores moves, indexed by [piece][square][size=2]
typedef std::array<std::array<Move, SQ_NO>, MAX_PIECE>  MoveHistory;


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
    std::condition_variable conditionVar;

    NativeThread nativeThread;

public:

    Position  rootPos;
    RootMoves rootMoves;

    Depth rootDepth
        , finishedDepth
        , selDepth;

    std::atomic<u64> nodes
        ,            tbHits;
    std::atomic<u32> pvChange;

    i16   nmpPly;
    Color nmpColor;

    u32   pvBeg
        , pvCur
        , pvEnd;

    u64   ttHitAvg;

    Score contempt;

    ButterflyHistory    butterflyHistory;
    CaptureHistory      captureHistory;
    MoveHistory         moveHistory;
    std::array<std::array<ContinuationHistory, 2>, 2> continuationHistory;

    Pawns::Table        pawnTable;
    Material::Table     matlTable;

    explicit Thread(size_t);
    Thread() = delete;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    virtual ~Thread();

    void start();
    void waitIdle();

    void idleFunction();

    i16 moveBestCount(Move) const;

    virtual void clear();
    virtual void search();
};

/// MainThread class is derived from Thread class used specific for main thread.
class MainThread
    : public Thread
{
private :

public:
    bool stopOnPonderhit;       // Stop search on ponderhit
    std::atomic<bool> ponder;   // Search on ponder move until the "stop"/"ponderhit" command

    TimeManager  timeMgr;
    SkillManager skillMgr;

    Value  prevBestValue;
    double prevTimeReduction;

    std::array<Value, 4> iterValues;
    Move bestMove;
    i16  bestMoveDepth;

    u64  tickCount;
    TimePoint debugTime;

    explicit MainThread(size_t);
    MainThread() = delete;
    MainThread(const MainThread&) = delete;
    MainThread& operator=(const MainThread&) = delete;

    void clear() override;
    void search() override;

    void setTickCount();
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

    u08       movestogo;   // Search <x> moves to the next time control

    TimePoint moveTime;    // Search <x> exact time in milli-seconds
    Depth     depth;       // Search <x> depth(plies) only
    u64       nodes;       // Search <x> nodes only
    u08       mate;        // Search mate in <x> moves
    bool      infinite;    // Search until the "stop" command

    Limit()
        : clock()
        , movestogo(0)
        , moveTime(0)
        , depth(DEP_ZERO)
        , nodes(0)
        , mate(0)
        , infinite(false)
    {}

    bool useTimeMgr() const
    {
        return !infinite
            && 0 == moveTime
            && DEP_ZERO == depth
            && 0 == nodes
            && 0 == mate;
    }

    bool mateOn() const
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

    StateListPtr setupStates;

public:

    double reductionFactor;

    Limit limit;
    u32   pvCount;

    std::atomic<bool> stop // Stop search forcefully
        ,             research;

    //std::ofstream outputStream;

    ThreadPool() = default;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    MainThread* mainThread() const { return static_cast<MainThread*>(front()); }

    template<typename T>
    T sum(std::atomic<T> Thread::*member) const
    {
        T s = {};
        for (auto *th : *this)
        {
            s += (th->*member).load(std::memory_order::memory_order_relaxed);
        }
        return s;
    }
    template<typename T>
    void reset(std::atomic<T> Thread::*member) const
    {
        for (auto *th : *this)
        {
            th->*member = {};
        }
    }

    Thread* bestThread() const;

    void clear();
    void configure(u32);

    void startThinking(Position&, StateListPtr&, const Limit&, const std::vector<Move>&, bool = false);
};

// Global ThreadPool
extern ThreadPool Threadpool;
