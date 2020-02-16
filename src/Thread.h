#pragma once

#include <atomic>
#include <condition_variable>
//#include <fstream>
#include <mutex>
#include <vector>

#include "ThreadWin32OSX.h"

#include "Limit.h"
#include "Position.h"
#include "RootMove.h"
#include "SkillManager.h"
#include "TimeManager.h"
#include "Table.h"
#include "Material.h"
#include "Pawns.h"
#include "Type.h"


/// Thread class keeps together all the thread-related stuff.
/// It use pawn and material hash tables so that once get a pointer to
/// an entry its life time is unlimited and we don't have to care about
/// someone changing the entry under our feet.
class Thread
{
private:
    bool  dead{false}
        , busy{true};

    std::mutex mtx;
    std::condition_variable conditionVar;

protected:

    size_t index;
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

    ColorIndexStatsTable quietStats;
    PieceSquareTypeStatsTable captureStats;
    Array<ContinuationStatsTable, 2, 2> continuationStats;

    PieceSquareMoveTable quietCounterMoves;

    Pawns   ::Table pawnHash;
    Material::Table matlHash;

    explicit Thread(size_t);
    Thread() = delete;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    virtual ~Thread();

    void startSearch();
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
public:
    bool stopOnPonderhit;       // Stop search on ponderhit
    std::atomic<bool> ponder;   // Search on ponder move until the "stop"/"ponderhit" command

    TimeManager  timeMgr;
    SkillManager skillMgr;

    Value  prevBestValue;
    double prevTimeReduction;

    Array<Value, 4> iterValues;
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

namespace WinProcGroup
{
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

    template<typename T>
    T sum(std::atomic<T> Thread::*member) const
    {
        T s{};
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

    MainThread* mainThread() const { return static_cast<MainThread*>(front()); }

    Thread* bestThread() const;

    void clear();
    void configure(u32);

    void startThinking(Position&, StateListPtr&, const Limit&, const std::vector<Move>&, bool = false);
};

// Global ThreadPool
extern ThreadPool Threadpool;
