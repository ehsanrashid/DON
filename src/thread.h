#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "thread_win32_osx.h"

#include "movepicker.h"
#include "position.h"
#include "rootmove.h"
#include "king.h"
#include "material.h"
#include "pawns.h"
#include "type.h"

/// Thread class keeps together all the thread-related stuff.
/// It use pawn and material hash tables so that once get a pointer to
/// an entry its life time is unlimited and we don't have to care about
/// someone changing the entry under our feet.
class Thread {

public:

    explicit Thread(uint16_t);
    virtual ~Thread();

    Thread() = delete;
    Thread(Thread const&) = delete;
    Thread(Thread&&) = delete;

    Thread& operator=(Thread const&) = delete;
    Thread& operator=(Thread&&) = delete;

    void wakeUp();
    void waitIdle();

    void threadFunc();

    virtual void clean();
    virtual void search();


    Position  rootPos;
    StateInfo rootState;
    RootMoves rootMoves;

    Depth rootDepth,
          finishedDepth,
          selDepth;

    std::atomic<uint64_t> nodes;
    std::atomic<uint64_t> tbHits;
    std::atomic<uint32_t> pvChanges;

    int16_t nmpMinPly;
    Color   nmpColor;

    uint16_t pvBeg,
             pvCur,
             pvEnd;

    uint64_t ttHitAvg;

    Score contempt;
    
    int16_t failHighCount;

    // butterFlyStats records how often quiet moves have been successful/unsuccessful
    // during the current search, and is used for reduction and move ordering decisions.
    ButterFlyStatsTable         butterFlyStats;
    // lowPlyStats records how often quiet moves have been successful/unsuccessful
    // at higher depths on plies 0 to 3 and in the PV (ttPv)
    // It get cleared with each new search and get filled during iterative deepening
    PlyIndexStatsTable          lowPlyStats;

    // captureStats records how often capture moves have been successful/unsuccessful
    // during the current search, and is used for reduction and move ordering decisions.
    PieceSquareTypeStatsTable   captureStats;

    // counterMoves stores counter moves
    PieceSquareMoveTable        counterMoves;

    // continuationStats is the combined stats of a given pair of moves,
    // usually the current one given a previous one. [inCheck][captureOrPromotion]
    ContinuationStatsTable      continuationStats[2][2];

    Material::Table matlHash;
    Pawns   ::Table pawnHash;
    King    ::Table kingHash;

private:

    bool dead;
    bool busy;
    std::mutex mutex;
    std::condition_variable conditionVar;
    uint16_t index; // indentity
    NativeThread nativeThread;
};

/// MainThread class is derived from Thread class used specific for main thread.
class MainThread :
    public Thread {

public:

    using Thread::Thread;

    MainThread() = delete;
    MainThread(MainThread const&) = delete;
    MainThread(MainThread&&) = delete;

    MainThread& operator=(MainThread const&) = delete;
    MainThread& operator=(MainThread &&) = delete;

    void tick();

    void clean() final;
    void search() final;

    int16_t tickCount;
};


/// ThreadPool class handles all the threads related stuff like,
/// initializing & deinitializing, starting, parking & launching a thread
/// All the access to shared thread data is done through this class.
class ThreadPool final :
    public std::vector<Thread*> {

public:

    //using std::vector<Thread*>::vector;

    ThreadPool() = default;
    ThreadPool(ThreadPool const&) = delete;

    ThreadPool& operator=(ThreadPool const&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template<typename T>
    void set(std::atomic<T> Thread::*member, T value) const noexcept {
        for (auto *th : *this) {
            th->*member = value;
        }
    }

    template<typename T>
    T accumulate(std::atomic<T> Thread::*member, T value = {}) const noexcept {
        for (auto const *th : *this) {
            value += (th->*member).load(std::memory_order::memory_order_relaxed);
        }
        return value;
    }

    MainThread* mainThread() const noexcept;
    Thread* bestThread() const noexcept;

    void setup(uint16_t);
    void clean();

    void startThinking(Position&, StateListPtr&);
    void stopThinking();

    void wakeUpThreads();
    void waitForThreads();

    uint16_t pvCount;

    std::atomic<bool> stop;     // Stop searching forcefully
    std::atomic<bool> stand;    // Stop increasing depth

    std::atomic<bool> ponder;   // Search in ponder mode, on ponder move until the "stop"/"ponderhit" command
    bool    stopPonderhit;      // Stop search on ponderhit

    double  pvChangesSum;
    double  timeReduction;

    Move    bestMove;
    int16_t bestDepth;
    Value   bestValue;
    std::array<Value, 4> iterValues;
    int16_t iterIdx;

private:

    StateListPtr setupStates;
};

// Global ThreadPool
extern ThreadPool Threadpool;

enum OutputState : uint8_t {
    OS_LOCK,
    OS_UNLOCK
};

extern std::ostream& operator<<(std::ostream&, OutputState);

#define sync_cout std::cout << OS_LOCK
#define sync_endl std::endl << OS_UNLOCK

