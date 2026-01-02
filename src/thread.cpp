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
#include <chrono>
#include <ratio>
#include <string>
#include <unordered_map>

#include "bitboard.h"
#include "history.h"
#include "movegen.h"
#include "option.h"
#include "syzygy/tablebase.h"
#include "types.h"
#include "uci.h"

namespace DON {

// Constructor launches the thread and waits until it goes to sleep
// in idle_func(). Note that 'dead' and 'busy' should be already set.
Thread::Thread(std::size_t                   threadIdx,
               std::size_t                   threadCnt,
               std::size_t                   numaIdx,
               std::size_t                   numaThreadCnt,
               const ThreadToNumaNodeBinder& nodeBinder,
               ISearchManagerPtr             searchManager,
               const SharedState&            sharedState) noexcept :
    threadId(threadIdx),
    threadCount(threadCnt),
    numaId(numaIdx),
    numaThreadCount(numaThreadCnt),
    nativeThread(&Thread::idle_func, this) {
    assert(numa_thread_count() != 0 && numa_id() < numa_thread_count());

    wait_finish();

    numaAccessToken = nodeBinder();

    worker = make_unique_aligned_large_page<Worker>(thread_id(), thread_count(),     //
                                                    numa_id(), numa_thread_count(),  //
                                                    numa_access_token(), std::move(searchManager),
                                                    sharedState);
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
        std::unique_lock lock(mutex);

        busy = false;

        condVar.notify_one();  // Wake up anyone waiting for job finished
        condVar.wait(lock, [this] { return busy; });

        if (dead)
            break;

        JobFunc jobFn = std::move(jobFunc);
        jobFunc       = nullptr;

        lock.unlock();

        if (jobFn)
            jobFn();
    }
}


// Creates/destroys threads to match the requested number.
// Created and launched threads will immediately go to sleep in idle_func.
// Upon resizing, threads are recreated to allow for binding if necessary.
void Threads::set(const NumaConfig&                       numaConfig,
                  SharedState                             sharedState,
                  const MainSearchManager::UpdateContext& updateContext) noexcept {
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
    std::string numaPolicy = sharedState.options["NumaPolicy"];

    const bool threadBindable = [&]() {
        if (numaPolicy == "none")
            return false;

        if (numaPolicy == "auto")
            return numaConfig.suggests_binding_threads(threadCount);

        // numaPolicy == "system", "hardware" or explicitly set by the user string
        return true;
    }();

    threadBoundNumaNodes = threadBindable
                           ? numaConfig.distribute_threads_among_numa_nodes(threadCount)
                           : std::vector<NumaIndex>{};

    std::unordered_map<NumaIndex, std::size_t> numaThreadCounts;

    numaThreadCounts.reserve(threadBoundNumaNodes.empty() ? 1 : threadBoundNumaNodes.size());

    if (threadBoundNumaNodes.empty())
    {
        // All threads belong to NUMA node 0
        numaThreadCounts.emplace(0, threadCount);
    }
    else
    {
        for (NumaIndex numaIdx : threadBoundNumaNodes)
            ++numaThreadCounts[numaIdx];
    }

    // Prepare shared histories map
    auto& historiesMap = sharedState.historiesMap;

    historiesMap.clear();
    historiesMap.reserve(numaThreadCounts.size());
    historiesMap.max_load_factor(1.0f);

    // Populate shared histories map (optionally NUMA-bound)
    if (threadBindable)
    {
        for (const auto& [numaIdx, count] : numaThreadCounts)
        {
            std::size_t roundedCount = next_pow2(count);

            numaConfig.execute_on_numa_node(numaIdx,
                                            [&historiesMap, numaIdx, roundedCount]() noexcept {
                                                historiesMap.try_emplace(numaIdx, roundedCount);
                                            });
        }
    }
    else
    {
        for (const auto& [numaIdx, count] : numaThreadCounts)
        {
            std::size_t roundedCount = next_pow2(count);

            historiesMap.try_emplace(numaIdx, roundedCount);
        }
    }

    const auto* numaConfigPtr = threadBindable ? &numaConfig : nullptr;

    std::unordered_map<NumaIndex, std::size_t> numaIds;

    for (std::size_t threadId = 0; threadId < threadCount; ++threadId)
    {
        NumaIndex numaIdx = threadBindable ? threadBoundNumaNodes[threadId] : 0;

        ISearchManagerPtr searchManager;
        if (threadId == 0)
            searchManager = std::make_unique<MainSearchManager>(updateContext);
        else
            searchManager = std::make_unique<NullSearchManager>();

        // When not binding threads want to force all access to happen from the same
        // NUMA node, because in case of NUMA replicated memory accesses don't
        // want to trash cache in case the threads get scheduled on the same NUMA node.
        ThreadToNumaNodeBinder nodeBinder(numaIdx, numaConfigPtr);

        threads.emplace_back(std::make_unique<Thread>(threadId, threadCount, numaIds[numaIdx]++,
                                                      numaThreadCounts[numaIdx], nodeBinder,
                                                      std::move(searchManager), sharedState));
    }

    assert(size() == threadCount);

    init();
}

Thread* Threads::best_thread() const noexcept {

    Thread* bestThread = main_thread();

    Value minCurValue = +VALUE_NONE;
    // Find the minimum value of all threads
    for (auto&& th : threads)
    {
        Value curValue = th->worker->rootMoves[0].curValue;

        if (minCurValue > curValue)
            minCurValue = curValue;
    }

    // Vote according to value and depth, and select the best thread
    const auto thread_voting_value = [=](const Thread* th) noexcept -> std::uint32_t {
        return (14 + th->worker->rootMoves[0].curValue - minCurValue) * th->worker->completedDepth;
    };

    std::unordered_map<Move, std::uint64_t> votes(
      2 * std::min(size(), bestThread->worker->rootMoves.size()));

    for (auto&& th : threads)
        votes[th->worker->rootMoves[0].pv[0]] += thread_voting_value(th.get());

    for (auto&& nextThread : threads)
    {
        const auto bestThreadValue = bestThread->worker->rootMoves[0].curValue;
        const auto nextThreadValue = nextThread->worker->rootMoves[0].curValue;

        const auto& bestThreadPV = bestThread->worker->rootMoves[0].pv;
        const auto& nextThreadPV = nextThread->worker->rootMoves[0].pv;

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
void Threads::start(Position&      pos,
                    StateListPtr&  states,
                    const Limit&   limit,
                    const Options& options) noexcept {
    main_thread()->wait_finish();

    state.store(State::Active, std::memory_order_relaxed);

    RootMoves rootMoves;

    const MoveList<LEGAL> legalMoves(pos);

    if (!limit.searchMoves.empty())
    {
        bool emplace = true;
        for (const auto& move : limit.searchMoves)
        {
            if (emplace && rootMoves.size() == legalMoves.size())
                break;

            Move m = UCI::mix_to_move(move, pos, legalMoves);

            emplace = m != Move::None && !rootMoves.contains(m);

            if (emplace)
                rootMoves.emplace_back(m);
        }
    }
    else
    {
        for (auto m : legalMoves)
            rootMoves.emplace_back(m);
    }

    if (!limit.ignoreMoves.empty())
    {
        bool erase = true;
        for (const auto& move : limit.ignoreMoves)
        {
            if (erase && rootMoves.empty())
                break;

            Move m = UCI::mix_to_move(move, pos, legalMoves);

            erase = m != Move::None;

            if (erase)
                erase = rootMoves.erase(m);
        }
    }

    bool useTimeManager = limit.use_time_manager() && options["NodesTime"] == 0;

    auto& clock = limit.clocks[pos.active_color()];

    // If time manager is active, don't use more than 5% of clock time
    auto startTime = std::chrono::steady_clock::now();

    const auto time_to_abort = [&]() noexcept -> bool {
        auto endTime = std::chrono::steady_clock::now();
        return useTimeManager
            && std::chrono::duration<double, std::milli>(endTime - startTime).count()
                 > (0.0500 + 0.0500 * std::clamp((clock.inc - clock.time) / 100.0, 0.0, 1.0))
                     * clock.time;
    };

    auto tbConfig = Tablebase::rank_root_moves(pos, rootMoves, options, false, time_to_abort);

    // After ownership transfer 'states' becomes empty, so if stop the search
    // and call 'go' again without setting a new position states.get() == nullptr.
    assert(states.get() != nullptr || setupStates.get() != nullptr);

    if (states.get() != nullptr)
        setupStates = std::move(states);  // Ownership transfer, states is now empty

    // Use Position::set() to set root position across threads.
    // The rootState is per thread, earlier states are shared since they are read-only.
    for (auto&& th : threads)
    {
        th->run_custom_job([&]() {
            th->worker->nodes.store(0, std::memory_order_relaxed);
            th->worker->tbHits.store(0, std::memory_order_relaxed);
            th->worker->moveChanges.store(0, std::memory_order_relaxed);

            th->worker->rootPos.set(pos, &th->worker->rootState);
            th->worker->rootMoves = rootMoves;
            th->worker->limit     = limit;
            th->worker->tbConfig  = tbConfig;
        });
    }

    for (auto&& th : threads)
        th->wait_finish();

    main_thread()->start_search();
}

void Threads::run_on_thread(std::size_t threadId, JobFunc job) noexcept {
    assert(threadId < size());

    threads[threadId]->run_custom_job(std::move(job));
}

void Threads::wait_on_thread(std::size_t threadId) noexcept {
    assert(threadId < size());

    threads[threadId]->wait_finish();
}

std::vector<std::size_t> Threads::get_bound_thread_counts() const noexcept {
    std::vector<std::size_t> threadCounts;

    if (!threadBoundNumaNodes.empty())
    {
        NumaIndex maxNumaIdx =
          *std::max_element(threadBoundNumaNodes.begin(), threadBoundNumaNodes.end());

        threadCounts.resize(1 + maxNumaIdx, 0);

        for (NumaIndex numaIdx : threadBoundNumaNodes)
            ++threadCounts[numaIdx];
    }

    return threadCounts;
}

}  // namespace DON
