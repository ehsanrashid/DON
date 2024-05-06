/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "thread.h"

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <utility>
#include <string>

#include "misc.h"
#include "movegen.h"
#include "timeman.h"
#include "tt.h"
#include "uci.h"
#include "ucioption.h"
#include "syzygy/tbprobe.h"

namespace DON {

// Constructor launches the thread and waits until it goes to sleep
// in idle_func(). Note that 'dead' and 'busy' should be already set.
Thread::Thread(const Search::SharedState& sharedState,
               Search::ISearchManagerPtr  searchManager,
               std::uint16_t              id) noexcept :
    worker(std::make_unique<Search::Worker>(sharedState, std::move(searchManager), id)),
    idx(id),
    threadCount(sharedState.options["Threads"]),
    nativeThread(&Thread::idle_func, this) {

    wait_idle();
}

// Destructor wakes up the thread in idle_func() and waits
// for its termination. Thread should be already waiting.
Thread::~Thread() noexcept {
    assert(!busy);

    dead = true;
    wake_up();
    nativeThread.join();
}

// Wakes up the thread that will start the search
void Thread::wake_up() noexcept {
    {
        std::lock_guard lockGuard(mutex);
        busy = true;
    }                      // Unlock before notifying saves a few CPU-cycles
    condVar.notify_one();  // Wake up the thread in idle_func()
}

// Blocks on the condition variable
// until the thread has finished searching.
void Thread::wait_idle() noexcept {
    std::unique_lock uniqueLock(mutex);
    condVar.wait(uniqueLock, [this] { return !busy; });
    //uniqueLock.unlock();
}

// Thread gets parked here, blocked on the
// condition variable, when it has no work to do.
void Thread::idle_func() noexcept {

    // If OS already scheduled us on a different group than 0 then don't overwrite
    // the choice, eventually we are one of many one-threaded processes running on
    // some Windows NUMA hardware, for instance in fishtest. To make it simple,
    // just check if running threads are below a threshold, in this case, all this
    // NUMA machinery is not needed.
    if (threadCount > 8)
        WinProcGroup::bind_thread(idx);

    while (true)
    {
        std::unique_lock uniqueLock(mutex);
        busy = false;
        condVar.notify_one();  // Wake up anyone waiting for search finished
        condVar.wait(uniqueLock, [this] { return busy; });

        if (dead)
            return;

        uniqueLock.unlock();

        worker->start_search();
    }
}


ThreadPool::~ThreadPool() noexcept { destroy(); }

// Destroy any existing thread(s)
void ThreadPool::destroy() noexcept {
    if (!empty())
    {
        main_thread()->wait_idle();

        while (!empty())
            delete threads.back(), threads.pop_back();
    }
}

// Sets threadPool data to initial values
void ThreadPool::clear() noexcept {
    if (empty())
        return;

    for (Thread* th : threads)
        th->worker->clear();

    auto mainManager               = main_manager();
    mainManager->prevBestValue     = -VALUE_INFINITE;
    mainManager->prevBestAvgValue  = -VALUE_INFINITE;
    mainManager->prevTimeReduction = 1.0;
    if (mainManager->tm.useNodesTime)
        mainManager->tm.clear_nodes_time();

    reductions[0] = 0;
    for (std::uint16_t i = 1; i < reductions.size(); ++i)
        reductions[i] = (18.93 + 0.5 * std::log(size())) * std::log(i);
}

// Creates/destroys threads to match the requested number.
// Created and launched threads will immediately go to sleep in idle_func.
// Upon resizing, threads are recreated to allow for binding if necessary.
void ThreadPool::set(Search::SharedState          sharedState,
                     const Search::UpdateContext& updateContext) noexcept {
    destroy();

    std::uint16_t threadCount = sharedState.options["Threads"];
    // If options["Threads"] allow 0 threads
    //if (threadCount == 0)
    //    threadCount = std::thread::hardware_concurrency();

    // create new thread(s)
    if (threadCount != 0)
    {
        assert(empty());

        auto mainManager = std::make_unique<Search::MainSearchManager>(updateContext);
        threads.push_back(new Thread(sharedState, std::move(mainManager), size()));

        while (size() < threadCount)
        {
            auto nullManager = std::make_unique<Search::NullSearchManager>();
            threads.push_back(new Thread(sharedState, std::move(nullManager), size()));
        }

        clear();

        main_thread()->wait_idle();
        // Reallocate the hash with the new thread-pool size
        sharedState.tt.resize(sharedState.options["Hash"], threadCount);
    }
}

Thread* ThreadPool::main_thread() const noexcept { return threads.front(); }

Thread* ThreadPool::best_thread() const noexcept {
    auto bestThread = main_thread();

    Value minValue = +VALUE_INFINITE;
    // Find the minimum score of all threads
    for (const Thread* th : threads)
        minValue = std::min(th->worker->rootMoves[0].value, minValue);

    // Vote according to score and depth, and select the best thread
    auto thread_voting_value = [minValue](const Thread* th) noexcept -> std::uint64_t {
        return (14 + th->worker->rootMoves[0].value - minValue) * th->worker->completedDepth;
    };

    std::unordered_map<Move, std::uint64_t, Move::MoveHash> votes(
      2 * std::min(size(), bestThread->worker->rootMoves.size()));

    for (const Thread* th : threads)
        votes[th->worker->rootMoves[0].pv[0]] += thread_voting_value(th);

    for (Thread* th : threads)
    {
        const auto bestThreadValue = bestThread->worker->rootMoves[0].value;
        const auto newThreadValue  = th->worker->rootMoves[0].value;

        const auto& bestThreadPV = bestThread->worker->rootMoves[0].pv;
        const auto& newThreadPV  = th->worker->rootMoves[0].pv;

        const auto bestThreadMoveVote = votes[bestThreadPV[0]];
        const auto newThreadMoveVote  = votes[newThreadPV[0]];

        const bool bestThreadInProvenWin =
          bestThreadValue != +VALUE_INFINITE && bestThreadValue >= VALUE_TB_WIN_IN_MAX_PLY;
        const bool newThreadInProvenWin =
          newThreadValue != +VALUE_INFINITE && newThreadValue >= VALUE_TB_WIN_IN_MAX_PLY;

        const bool bestThreadInProvenLoss =
          bestThreadValue != -VALUE_INFINITE && bestThreadValue <= VALUE_TB_LOSS_IN_MAX_PLY;
        const bool newThreadInProvenLoss =
          newThreadValue != -VALUE_INFINITE && newThreadValue <= VALUE_TB_LOSS_IN_MAX_PLY;

        if (bestThreadInProvenWin)
        {
            // Make sure we pick the shortest mate / TB conversion
            if (newThreadInProvenWin && newThreadValue > bestThreadValue)
                bestThread = th;
        }
        else if (bestThreadInProvenLoss)
        {
            // Make sure we pick the shortest mated / TB conversion
            if (newThreadInProvenLoss && newThreadValue < bestThreadValue)
                bestThread = th;
        }
        else if (
          newThreadInProvenWin || newThreadInProvenLoss
          || (newThreadValue > VALUE_TB_LOSS_IN_MAX_PLY
              && (newThreadMoveVote > bestThreadMoveVote
                  || (newThreadMoveVote == bestThreadMoveVote
                      // Note that we make sure not to pick a thread with truncated-PV for better viewer experience.
                      && thread_voting_value(th) * (newThreadPV.size() > 2)
                           > thread_voting_value(bestThread) * (bestThreadPV.size() > 2)))))
            bestThread = th;
    }

    return bestThread;
}

Search::MainSearchManager* ThreadPool::main_manager() const noexcept {
    return main_thread()->worker->main_manager();
}

std::uint64_t ThreadPool::nodes() const noexcept { return accumulate(&Search::Worker::nodes); }

std::uint64_t ThreadPool::tbHits() const noexcept { return accumulate(&Search::Worker::tbHits); }

// Wakes up main thread waiting in idle_func() and returns immediately.
// Main thread will wake up other threads and start the search.
void ThreadPool::start(Position&             pos,
                       StateListPtr&         states,
                       const Search::Limits& limits,
                       const OptionsMap&     options) noexcept {
    main_thread()->wait_idle();

    stop = aborted = false;
    depthIncrease  = true;

    Search::RootMoves rootMoves;

    const auto legalMoves = MoveList<LEGAL>(pos);
    for (const std::string& can : limits.searchMoves)
    {
        if (rootMoves.size() == legalMoves.size())
            break;
        Move m = UCI::can_to_move(can, legalMoves);
        if (m && std::find(rootMoves.begin(), rootMoves.end(), m) == rootMoves.end())
            rootMoves.emplace_back(m);
    }

    if (rootMoves.empty())
        for (auto m : legalMoves)
            rootMoves.emplace_back(m);

    for (const std::string& can : limits.ignoreMoves)
    {
        if (rootMoves.empty())
            break;
        Move m = UCI::can_to_move(can, legalMoves);
        if (Search::RootMoves::iterator itr;
            m && (itr = std::find(rootMoves.begin(), rootMoves.end(), m)) != rootMoves.end())
            rootMoves.erase(itr);
    }

    std::string rootFen = pos.fen();

    Tablebases::Config tbConfig = Tablebases::rank_root_moves(pos, rootMoves, options);

    // After ownership transfer 'states' becomes empty, so if we stop the search
    // and call 'go' again without setting a new position states.get() == nullptr.
    assert(states.get() || setupStates.get());

    if (states.get())
        setupStates = std::move(states);  // Ownership transfer, states is now empty

    // We use Position::set() to set root position across threads. But there are some
    // StateInfo fields (rule50, nullPly, capturedPiece, previous) that cannot be deduced
    // from a fen string, so set() clears them and they are set from setupStates->back() later.
    // The rootState is per thread, earlier states are shared since they are read-only.
    for (Thread* th : threads)
    {
        th->worker->nodes = th->worker->tbHits = th->worker->bestMoveChanges = 0;
        th->worker->selDepth = th->worker->nmpMinPly = 0;
        th->worker->rootDepth = th->worker->completedDepth = DEPTH_ZERO;
        th->worker->rootPos.set(rootFen, &th->worker->rootState);
        th->worker->rootState = setupStates->back();
        th->worker->limits    = limits;
        th->worker->rootMoves = rootMoves;
        th->worker->tbConfig  = tbConfig;
    }

    main_thread()->wake_up();
}

// Start non-main threads
// Will be invoked by main thread after it has started searching
void ThreadPool::start_search() const noexcept {

    for (Thread* th : threads)
        if (th != main_thread())
            th->wake_up();
}

// Wait for non-main threads
void ThreadPool::wait_finish() const noexcept {

    for (Thread* th : threads)
        if (th != main_thread())
            th->wait_idle();
}

}  // namespace DON
