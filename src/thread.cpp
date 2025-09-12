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
#include <deque>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "misc.h"
#include "movegen.h"
#include "syzygy/tbbase.h"
#include "types.h"
#include "uci.h"
#include "ucioption.h"

namespace DON {

// Constructor launches the thread and waits until it goes to sleep
// in idle_func(). Note that 'dead' and 'busy' should be already set.
Thread::Thread(std::size_t                           id,
               const SharedState&                    sharedState,
               ISearchManagerPtr                     searchManager,
               const OptionalThreadToNumaNodeBinder& nodeBinder) noexcept :
    idx(id),
    threadCount(sharedState.options["Threads"]),
    nativeThread(&Thread::idle_func, this) {

    wait_finish();
    run_custom_job([this, id, &sharedState, &searchManager, &nodeBinder]() {
        // Use the binder to [maybe] bind the threads to a NUMA node before doing
        // the Worker allocation.
        // Ideally would also allocate the SearchManager here, but that's minor.
        worker = std::make_unique<Worker>(id, sharedState, std::move(searchManager), nodeBinder());
    });
    wait_finish();
}

// Destructor wakes up the thread in idle_func() and waits
// for its termination. Thread should be already waiting.
Thread::~Thread() noexcept {
    assert(!busy);

    dead = true;
    run_custom_job([]() { return; });
    nativeThread.join();
}

void Thread::ensure_network_replicated() const noexcept { worker->ensure_network_replicated(); }

// Thread gets parked here, blocked on the condition variable,
// when it has no work to do.
void Thread::idle_func() noexcept {
    while (true)
    {
        std::unique_lock uniqueLock(mutex);
        busy = false;
        condVar.notify_one();  // Wake up anyone waiting for search finished
        condVar.wait(uniqueLock, [this] { return busy; });

        if (dead)
            break;

        auto job = std::move(jobFunc);
        jobFunc  = nullptr;

        uniqueLock.unlock();

        if (job)
            job();
    }
}


// Creates/destroys threads to match the requested number.
// Created and launched threads will immediately go to sleep in idle_func.
// Upon resizing, threads are recreated to allow for binding if necessary.
void ThreadPool::set(const NumaConfig&    numaConfig,
                     const SharedState&   sharedState,
                     const UpdateContext& updateContext) noexcept {
    clear();

    std::size_t threadCount = sharedState.options["Threads"];
    assert(threadCount != 0);

    // Create new thread(s)

    // Binding threads may be problematic when there's multiple NUMA nodes and
    // multiple Stockfish instances running. In particular, if each instance
    // runs a single thread then they would all be mapped to the first NUMA node.
    // This is undesirable, and so the default behavior (i.e. when the user does not
    // change the NumaConfig UCI setting) is to not bind the threads to processors
    // unless we know for sure that we span NUMA nodes and replication is required.
    const auto numaPolicy = lower_case(sharedState.options["NumaPolicy"]);
    const auto threadBind = [&]() {
        if (numaPolicy == "none")
            return false;

        if (numaPolicy == "auto")
            return numaConfig.suggests_binding_threads(threadCount);

        // numaPolicy == "system", "hardware" or explicitly set by the user string
        return true;
    }();

    numaNodeBoundThreadIds = threadBind
                             ? numaConfig.distribute_threads_among_numa_nodes(threadCount)
                             : std::vector<NumaIndex>{};

    const auto* numaConfigPtr = threadBind ? &numaConfig : nullptr;

    for (std::size_t threadId = 0; threadId < threadCount; ++threadId)
    {
        NumaIndex numaIdx = threadBind ? numaNodeBoundThreadIds[threadId] : 0;

        ISearchManagerPtr searchManager;
        if (threadId == 0)
            searchManager = std::make_unique<MainSearchManager>(updateContext);
        else
            searchManager = std::make_unique<NullSearchManager>();

        // When not binding threads want to force all access to happen from the same
        // NUMA node, because in case of NUMA replicated memory accesses don't
        // want to trash cache in case the threads get scheduled on the same NUMA node.
        auto nodeBinder = OptionalThreadToNumaNodeBinder(numaIdx, numaConfigPtr);

        threads.emplace_back(
          std::make_unique<Thread>(threadId, sharedState, std::move(searchManager), nodeBinder));
    }

    init();
}

Thread* ThreadPool::best_thread() const noexcept {

    Thread* bestThread = main_thread();

    Value minCurValue = +VALUE_INFINITE;
    // Find the minimum value of all threads
    for (auto&& th : threads)
        minCurValue = std::min(th->worker->rootMoves.front().curValue, minCurValue);

    // Vote according to value and depth, and select the best thread
    const auto thread_voting_value = [=](const Thread* th) noexcept -> std::uint32_t {
        return (14 + th->worker->rootMoves.front().curValue - minCurValue)
             * th->worker->completedDepth;
    };

    std::unordered_map<Move, std::uint64_t, Move::Hash> votes(
      2 * std::min(size(), bestThread->worker->rootMoves.size()));

    for (auto&& th : threads)
        votes[th->worker->rootMoves.front().pv[0]] += thread_voting_value(th.get());

    for (auto&& nextThread : threads)
    {
        const auto bestThreadValue = bestThread->worker->rootMoves.front().curValue;
        const auto nextThreadValue = nextThread->worker->rootMoves.front().curValue;

        const auto& bestThreadPV = bestThread->worker->rootMoves.front().pv;
        const auto& nextThreadPV = nextThread->worker->rootMoves.front().pv;

        const auto bestThreadMoveVote = votes[bestThreadPV[0]];
        const auto nextThreadMoveVote = votes[nextThreadPV[0]];

        const auto bestThreadVotingValue = thread_voting_value(bestThread);
        const auto nextThreadVotingValue = thread_voting_value(nextThread.get());

        const bool bestThreadInProvenWin =
          bestThreadValue != +VALUE_INFINITE && is_win(bestThreadValue);
        const bool nextThreadInProvenWin =
          nextThreadValue != +VALUE_INFINITE && is_win(nextThreadValue);

        const bool bestThreadInProvenLoss =
          bestThreadValue != -VALUE_INFINITE && is_loss(bestThreadValue);
        const bool nextThreadInProvenLoss =
          nextThreadValue != -VALUE_INFINITE && is_loss(nextThreadValue);

        if (bestThreadInProvenWin)
        {
            // Make sure pick the shortest mate / TB conversion
            if (nextThreadInProvenWin && bestThreadValue < nextThreadValue)
                bestThread = nextThread.get();
        }
        else if (bestThreadInProvenLoss)
        {
            // Make sure pick the shortest mated / TB conversion
            if (nextThreadInProvenLoss && bestThreadValue > nextThreadValue)
                bestThread = nextThread.get();
        }
        // clang-format off
        else if (nextThreadInProvenWin || nextThreadInProvenLoss
              || (!is_loss(nextThreadValue)
                  && (bestThreadMoveVote < nextThreadMoveVote
                      || (bestThreadMoveVote == nextThreadMoveVote
                          // Note that make sure not to pick a thread with truncated-PV for better viewer experience.
                          && (bestThreadVotingValue < nextThreadVotingValue
                              || (bestThreadVotingValue == nextThreadVotingValue
                                  && bestThreadPV.size() < nextThreadPV.size()))))))
            bestThread = nextThread.get();
        // clang-format on
    }

    return bestThread;
}

// Wakes up main thread waiting in idle_func() and returns immediately.
// Main thread will wake up other threads and start the search.
void ThreadPool::start(Position&      pos,
                       StateListPtr&  states,
                       const Limit&   limit,
                       const Options& options) noexcept {
    main_thread()->wait_finish();

    stop = abort = research = false;

    RootMoves rootMoves;

    const MoveList<LEGAL> legalMoveList(pos);

    bool emplace = true;
    for (const auto& move : limit.searchMoves)
    {
        if (emplace && rootMoves.size() == legalMoveList.size())
            break;
        Move m  = UCI::mix_to_move(move, pos, legalMoveList);
        emplace = m != Move::None && !rootMoves.contains(m);
        if (emplace)
            rootMoves.emplace_back(m);
    }

    if (limit.searchMoves.empty())
        for (const auto& m : legalMoveList)
            rootMoves.emplace_back(m);

    bool erase = true;
    for (const auto& move : limit.ignoreMoves)
    {
        if (erase && rootMoves.empty())
            break;
        Move m = UCI::mix_to_move(move, pos, legalMoveList);
        erase  = m != Move::None;
        if (erase)
            erase = rootMoves.erase(m);
    }

    auto tbConfig = Tablebases::rank_root_moves(pos, rootMoves, options);

    // After ownership transfer 'states' becomes empty, so if stop the search
    // and call 'go' again without setting a new position states.get() == nullptr.
    assert(states.get() || setupStates.get());

    if (states.get())
        setupStates = std::move(states);  // Ownership transfer, states is now empty

    // Use Position::set() to set root position across threads. But there are some
    // State fields (rule50, nullPly, capturedPiece, preSt) that cannot be deduced
    // from the fen string, so rootState are set from setupStates->back() object later.
    // The rootState is per thread, earlier states are shared since they are read-only.
    for (auto&& th : threads)
    {
        th->run_custom_job([&]() {
            th->worker->rootPos.set(pos, &th->worker->rootState);
            th->worker->rootState = setupStates->back();
            th->worker->rootMoves = rootMoves;
            th->worker->limit     = limit;
            th->worker->tbConfig  = tbConfig;
        });
    }

    for (auto&& th : threads)
        th->wait_finish();

    main_thread()->start_search();
}

void ThreadPool::run_on_thread(std::size_t threadId, JobFunc func) noexcept {
    assert(threadId < size());
    threads[threadId]->run_custom_job(std::move(func));
}

void ThreadPool::wait_on_thread(std::size_t threadId) noexcept {
    assert(threadId < size());
    threads[threadId]->wait_finish();
}

std::vector<std::size_t> ThreadPool::get_bound_thread_counts() const noexcept {
    std::vector<std::size_t> threadCounts;

    if (!numaNodeBoundThreadIds.empty())
    {
        NumaIndex maxNumaIdx =
          *std::max_element(numaNodeBoundThreadIds.begin(), numaNodeBoundThreadIds.end());

        threadCounts.resize(1 + maxNumaIdx, 0);

        for (NumaIndex numaIdx : numaNodeBoundThreadIds)
            ++threadCounts[numaIdx];
    }

    return threadCounts;
}

}  // namespace DON
