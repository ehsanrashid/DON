#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "ThreadWin32OSX.h"

#include "MovePicker.h"
#include "Position.h"
#include "RootMove.h"
#include "Material.h"
#include "Pawns.h"
#include "Type.h"

/// Thread class keeps together all the thread-related stuff.
/// It use pawn and material hash tables so that once get a pointer to
/// an entry its life time is unlimited and we don't have to care about
/// someone changing the entry under our feet.
class Thread {

private:

    bool
        _dead{ false },
        _busy{ true };

    std::mutex _mutex;
    std::condition_variable _conditionVar;
    u16 _index;
    NativeThread _nativeThread;

protected:
    using Base = Thread;

public:

    Position  rootPos;
    RootMoves rootMoves;

    Depth
        rootDepth,
        finishedDepth,
        selDepth;

    std::atomic<u64>
        nodes,
        tbHits;
    std::atomic<u32>
        pvChange;

    i16   nmpPly;
    Color nmpColor;

    u16
        pvBeg,
        pvCur,
        pvEnd;

    u64   ttHitAvg;

    Score contempt;

    // butterFlyStats records how often quiet moves have been successful/unsuccessful
    // during the current search, and is used for reduction and move ordering decisions.
    ColorIndexStatsTable butterFlyStats;
    // lowPlyStats records how often quiet moves have been successful/unsuccessful
    // at higher depths on plies 0 to 3 and in the PV (ttPv)
    // It get cleared with each new search and get filled during iterative deepening
    PlyIndexStatsTable lowPlyStats;

    // captureStats records how often capture moves have been successful/unsuccessful
    // during the current search, and is used for reduction and move ordering decisions.
    PieceSquareTypeStatsTable captureStats;

    // counterMoves stores counter moves
    PieceSquareMoveTable counterMoves;

    // continuationStats is the combined stats of a given pair of moves,
    // usually the current one given a previous one.
    Array<ContinuationStatsTable, 2, 2> continuationStats;

    Pawns   ::Table pawnHash;
    Material::Table matlHash;

    Thread() = delete;
    explicit Thread(u16);
    Thread(Thread const&) = delete;
    Thread(Thread&&) = delete;
    Thread& operator=(Thread const&) = delete;
    Thread& operator=(Thread&&) = delete;

    virtual ~Thread();

    void wakeUp();
    void waitIdle();

    void idleFunction();

    virtual void clear();
    virtual void search();
};

/// MainThread class is derived from Thread class used specific for main thread.
class MainThread :
    public Thread {

private:
    i16  _ticks;

public:
    using Base::Base;

    bool stopOnPonderhit;       // Stop search on ponderhit
    std::atomic<bool> ponder;   // Search on ponder move until the "stop"/"ponderhit" command

    Value  bestValue;
    double timeReduction;
    Array<Value, 4> iterValues;

    Move bestMove;
    i16  bestDepth;

    void setTicks(i16);
    void doTick();

    void clear() override final;
    void search() override final;
};

namespace WinProcGroup {

    extern void initialize();

    extern void bind(u16);
}


/// ThreadPool class handles all the threads related stuff like,
/// initializing & deinitializing, starting, parking & launching a thread
/// All the access to shared thread data is done through this class.
class ThreadPool :
    public std::vector<Thread*> {

private:

    StateListPtr _states;

protected:
    using Base = std::vector<Thread*>;

public:

    double reductionFactor{ 0.0 };

    std::atomic<bool>
        stop, // Stop search forcefully
        research;

    ThreadPool() = default;
    ThreadPool(ThreadPool const&) = delete;
    ThreadPool& operator=(ThreadPool const&) = delete;

    template<typename T>
    T sum(std::atomic<T> Thread::*member) const {
        T s{};
        for (auto *th : *this) {
            s += (th->*member).load(std::memory_order::memory_order_relaxed);
        }
        return s;
    }
    template<typename T>
    void reset(std::atomic<T> Thread::*member) const {
        for (auto *th : *this) {
            th->*member = {};
        }
    }

    u16 size() const;

    MainThread* mainThread() const;

    Thread* bestThread() const;

    void setup(u16);
    void clean();

    void startThinking(Position&, StateListPtr&);
};

// Global ThreadPool
extern ThreadPool Threadpool;
