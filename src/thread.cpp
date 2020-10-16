#include "thread.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_map>

#include "searcher.h"
#include "syzygytb.h"
#include "transposition.h"
#include "uci.h"
#include "helper/memoryhandler.h"

ThreadPool Threadpool;

/// Thread constructor launches the thread and waits until it goes to sleep in threadFunc().
/// Note that 'busy' and 'dead' should be already set.
Thread::Thread(uint16_t idx) :
    dead{ false },
    busy{ true },
    index(idx),
    nativeThread(&Thread::threadFunc, this) {

    waitIdle();
}

/// Thread destructor wakes up the thread in threadFunc() and waits for its termination.
/// Thread should be already waiting.
Thread::~Thread() {
    assert(!busy);
    dead = true;
    wakeUp();
    nativeThread.join();
}

/// Thread::wakeUp() wakes up the thread that will start the search.
void Thread::wakeUp() {
    std::lock_guard<std::mutex> lockGuard(mutex);
    busy = true;
    conditionVar.notify_one(); // Wake up the thread in threadFunc()
}

/// Thread::waitIdle() blocks on the condition variable while the thread is busy.
void Thread::waitIdle() {
    std::unique_lock<std::mutex> uniqueLock(mutex);
    conditionVar.wait(uniqueLock, [&]{ return !busy; });
    //uniqueLock.unlock();
}

/// Thread::threadFunc() is where the thread is parked.
/// Blocked on the condition variable, when it has no work to do.
void Thread::threadFunc() {
    // If OS already scheduled us on a different group than 0 then don't overwrite
    // the choice, eventually we are one of many one-threaded processes running on
    // some Windows NUMA hardware, for instance in fishtest. To make it simple,
    // just check if running threads are below a threshold, in this case all this
    // NUMA machinery is not needed.
    if (optionThreads() > 8) {
        WinProcGroup::bind(index);
    }

    while (true) {

        std::unique_lock<std::mutex> uniqueLock(mutex);
        busy = false;
        conditionVar.notify_one(); // Wake up anyone waiting for search finished
        conditionVar.wait(uniqueLock, [&]{ return busy; });
        if (dead) {
            return;
        }
        uniqueLock.unlock();

        search();
    }
}

/// Thread::clean() clears all the thread related stuff.
void Thread::clean() {
    butterFlyStats.fill(0);
    lowPlyStats.fill(0);

    captureStats.fill(0);

    counterMoves.fill(MOVE_NONE);

    for (bool inCheck : { false, true }) {
        for (bool capture : { false, true }) {
            continuationStats[inCheck][capture].fill(PieceSquareStatsTable{});
            continuationStats[inCheck][capture][NO_PIECE][0].fill(CounterMovePruneThreshold - 1);
        }
    }

    //kingHash.clear();
    //matlHash.clear();
    //pawnHash.clear();
}

/// MainThread::clean()
void MainThread::clean() {
    Thread::clean();

    bestValue = +VALUE_INFINITE;
    timeReduction = 1.00;
    iterValues.fill(VALUE_ZERO);
}


ThreadPool::~ThreadPool() {
    setup(0);
}

MainThread* ThreadPool::mainThread() const noexcept {
    return static_cast<MainThread*>(front());
}

Thread* ThreadPool::bestThread() const noexcept {
    Thread *bestTh{ front() };

    auto minValue{ +VALUE_INFINITE };
    for (auto *th : *this) {
        minValue = std::min(th->rootMoves[0].newValue, minValue);
    }
    // Vote according to value and depth
    std::unordered_map<Move, int64_t> votes;
    for (auto *th : *this) {
        votes[th->rootMoves[0][0]] += int32_t(th->rootMoves[0].newValue - minValue + 14) * int32_t(th->finishedDepth);

        if (std::abs(bestTh->rootMoves[0].newValue) < +VALUE_MATE_2_MAX_PLY) {
            if (  th->rootMoves[0].newValue >  -VALUE_MATE_2_MAX_PLY
             && ( th->rootMoves[0].newValue >= +VALUE_MATE_2_MAX_PLY
              ||  votes[bestTh->rootMoves[0][0]] <  votes[th->rootMoves[0][0]]
              || (votes[bestTh->rootMoves[0][0]] == votes[th->rootMoves[0][0]]
               && bestTh->finishedDepth < th->finishedDepth))) {
                bestTh = th;
            }
        }
        else {
            // Select the shortest mate for own/longest mate for opp
            if ( bestTh->rootMoves[0].newValue <  th->rootMoves[0].newValue
             || (bestTh->rootMoves[0].newValue == th->rootMoves[0].newValue
              && bestTh->finishedDepth < th->finishedDepth)) {
                bestTh = th;
            }
        }
    }
    //// Select best thread with max depth
    //auto bestMove{ bestTh->rootMoves[0][0] };
    //for (auto *th : *this) {
    //    if (bestMove == th->rootMoves[0][0]) {
    //        if (bestTh->finishedDepth < th->finishedDepth) {
    //            bestTh = th;
    //        }
    //    }
    //}

    return bestTh;
}

/// ThreadPool::setSize() creates/destroys threads to match the threadCount.
/// Created and launched threads will immediately go to sleep in threadFunc.
/// Upon resizing, threads are recreated to allow for binding if necessary.
void ThreadPool::setup(uint16_t threadCount) {
    stop = true;
    if (!empty()) {
        mainThread()->waitIdle();
        // Destroy any existing thread(s)
        while (size() > 0) {
            delete back(), pop_back();
        }
    }
    // Create new thread(s)
    if (threadCount != 0) {

        push_back(new MainThread(size()));
        while (size() < threadCount) {
            push_back(new Thread(size()));
        }

        clean();
        // Reallocate the hash with the new threadpool size
        uint32_t hash{ Options["Hash"] };
        TT.autoResize(hash);
        TTEx.autoResize(hash / 4);
        Searcher::initialize();
    }
}

/// ThreadPool::clean() clears all the threads in threadpool
void ThreadPool::clean() {

    for (auto *th : *this) {
        th->clean();
    }
}

/// ThreadPool::startThinking() wakes up main thread waiting in threadFunc() and returns immediately.
/// Main thread will wake up other threads and start the search.
void ThreadPool::startThinking(Position &pos, StateListPtr &states) {

    stop = false;
    stand = false;

    mainThread()->stopPonderhit = false;

    RootMoves rootMoves{ pos, Limits.searchMoves };

    if (!rootMoves.empty()) {
        SyzygyTB::rankRootMoves(pos, rootMoves);
    }

    // After ownership transfer 'states' becomes empty, so if we stop the search
    // and call 'go' again without setting a new position states.get() == nullptr.
    assert(states.get() != nullptr
        || setupStates.get() != nullptr);

    if (states.get() != nullptr) {
        setupStates = std::move(states); // Ownership transfer, states is now empty
    }

    // We use setup() to set root position across threads.
    // But there are some StateInfo fields (previous, nullPly, captured) that cannot be deduced
    // from a fen string, so setup() clears them and they are set from setupStates->back() later.
    // The rootState is per thread, earlier states are shared since they are read-only.
    auto const fen{ pos.fen() };
    for (auto *th : *this) {
        th->rootDepth     = DEPTH_ZERO;
        th->finishedDepth = DEPTH_ZERO;
        th->nodes         = 0;
        th->tbHits        = 0;
        th->pvChanges     = 0;
        th->nmpMinPly     = 0;
        th->nmpColor      = COLORS;
        th->rootMoves     = rootMoves;
        th->rootPos.setup(fen, th->rootState, th);
        assert(th->rootState.pawnKey == setupStates->back().pawnKey);
        assert(th->rootState.matlKey == setupStates->back().matlKey);
        assert(th->rootState.posiKey == setupStates->back().posiKey);
        assert(th->rootState.checkers == setupStates->back().checkers);
        th->rootState     = setupStates->back();
    }

    mainThread()->wakeUp();
}

void ThreadPool::wakeUpThreads() {
    for (auto *th : *this) {
        if (th != front()) {
            th->wakeUp();
        }
    }
}
void ThreadPool::waitForThreads() {
    for (auto *th : *this) {
        if (th != front()) {
            th->waitIdle();
        }
    }
}

/// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
std::ostream& operator<<(std::ostream &ostream, OutputState outputState) {
    static std::mutex mutex;

    switch (outputState) {
    case OS_LOCK:
        mutex.lock();
        break;
    case OS_UNLOCK:
        mutex.unlock();
        break;
    }
    return ostream;
}
