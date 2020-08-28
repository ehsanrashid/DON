#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "ThreadWin32OSX.h"

#include "MovePicker.h"
#include "Position.h"
#include "RootMove.h"
#include "King.h"
#include "Material.h"
#include "Pawns.h"
#include "Type.h"

/// Thread class keeps together all the thread-related stuff.
/// It use pawn and material hash tables so that once get a pointer to
/// an entry its life time is unlimited and we don't have to care about
/// someone changing the entry under our feet.
class Thread {

private:

    bool dead{ false },
         busy{ true };

    std::mutex mutex;
    std::condition_variable conditionVar;
    u16 index; // indentity
    NativeThread nativeThread;

public:

    Position  rootPos;
    StateInfo rootState;
    RootMoves rootMoves;

    Depth rootDepth,
          finishedDepth,
          selDepth;

    std::atomic<u64> nodes,
                     tbHits;
    std::atomic<u32> pvChange;

    i16   nmpMinPly;
    Color nmpColor;

    u16 pvBeg,
        pvCur,
        pvEnd;

    u64 ttHitAvg;

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
    ContinuationStatsTable continuationStats[2][2];

    Material::Table matlHash{ Material::Table(0x2000 ) };
    Pawns   ::Table pawnHash{ Pawns   ::Table(0x20000) };
    King    ::Table kingHash{ King    ::Table(0x40000) };

    Thread() = delete;
    explicit Thread(u16);
    Thread(Thread const&) = delete;
    Thread(Thread&&) = delete;
    Thread& operator=(Thread const&) = delete;
    Thread& operator=(Thread&&) = delete;

    virtual ~Thread();

    void wakeUp();
    void waitIdle();

    void threadFunc();

    virtual void clear();
    virtual void search();
};

/// MainThread class is derived from Thread class used specific for main thread.
class MainThread :
    public Thread {

public:
    using Thread::Thread;

    bool stopOnPonderHit;       // Stop search on ponderhit
    std::atomic<bool> ponder;   // Search on ponder move until the "stop"/"ponderhit" command

    Value  bestValue;
    double timeReduction;
    Value  iterValues[4];

    Move bestMove;
    i16  bestDepth;
    i16  tickCount;

    void tick();

    void clear() override final;
    void search() override final;
};


/// ThreadPool class handles all the threads related stuff like,
/// initializing & deinitializing, starting, parking & launching a thread
/// All the access to shared thread data is done through this class.
class ThreadPool :
    public std::vector<Thread*> {

private:
    StateListPtr setupStates;

public:
    //using std::vector<Thread*>::vector;

    std::atomic<bool> stop, // Stop search forcefully
                      research;

    ThreadPool() = default;
    ThreadPool(ThreadPool const&) = delete;
    ThreadPool& operator=(ThreadPool const&) = delete;
    virtual ~ThreadPool();

    template<typename T>
    T accumulate(std::atomic<T> Thread::*member, T init) const {
        for (auto *th : *this) {
            init += (th->*member).load(std::memory_order::memory_order_relaxed);
        }
        return init;
    }
    template<typename T>
    void set(std::atomic<T> Thread::*member, T value) const {
        for (auto *th : *this) {
            th->*member = value;
        }
    }

    u16 size() const noexcept;

    MainThread* mainThread() const noexcept;
    Thread* bestThread() const noexcept;

    void setup(u16);
    void clean();

    void startThinking(Position&, StateListPtr&);

    void wakeUpThreads();
    void waitForThreads();

};

namespace WinProcGroup {

    extern void bind(u16);
}

// Global ThreadPool
extern ThreadPool Threadpool;

enum OutputState : u08 { OS_LOCK, OS_UNLOCK };

extern std::ostream& operator<<(std::ostream&, OutputState);

#define sync_cout std::cout << OS_LOCK
#define sync_endl std::endl << OS_UNLOCK

