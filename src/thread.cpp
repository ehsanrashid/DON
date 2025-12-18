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
#include "movegen.h"
#include "syzygy/tbbase.h"
#include "types.h"
#include "uci.h"
#include "ucioption.h"

namespace DON {

namespace {

std::size_t next_pow2(std::uint64_t count) noexcept {
    return count <= 1 ? 1 : 2ULL << constexpr_msb(count - 1);
}

}  // namespace

// Constructor launches the thread and waits until it goes to sleep
// in idle_func(). Note that 'dead' and 'busy' should be already set.
Thread::Thread(std::size_t                           thId,
               std::size_t                           nId,
               const SharedState&                    sharedState,
               ISearchManagerPtr                     searchManager,
               const OptionalThreadToNumaNodeBinder& nodeBinder) noexcept :
    threadId(thId),
    numaId(nId),
    nativeThread(&Thread::idle_func, this) {

    wait_finish();

    run_custom_job([this, &sharedState, &searchManager, &nodeBinder]() {
        // Use the binder to [maybe] bind the threads to a NUMA node before doing
        // the Worker allocation.
        // Ideally would also allocate the SearchManager here, but that's minor.
        worker = make_unique_aligned_large_pages<Worker>(threadId, sharedState,
                                                         std::move(searchManager), nodeBinder());
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

        condVar.notify_one();  // Wake up anyone waiting for job finished
        condVar.wait(uniqueLock, [this] { return busy; });

        if (dead)
            break;

        JobFunc jobFn = std::move(jobFunc);
        jobFunc       = nullptr;

        uniqueLock.unlock();

        if (jobFn)
            jobFn();
    }
}


// Creates/destroys threads to match the requested number.
// Created and launched threads will immediately go to sleep in idle_func.
// Upon resizing, threads are recreated to allow for binding if necessary.
void ThreadPool::set(const NumaConfig&                       numaConfig,
                     const SharedState&                      sharedState,
                     const MainSearchManager::UpdateContext& updateContext) noexcept {
    clear();

    const std::size_t threadCount = sharedState.options["Threads"];
    assert(threadCount != 0);

    // Create new thread(s)

    // Binding threads may be problematic when there's multiple NUMA nodes and
    // multiple Stockfish instances running. In particular, if each instance
    // runs a single thread then they would all be mapped to the first NUMA node.
    // This is undesirable, and so the default behavior (i.e. when the user does not
    // change the NumaConfig UCI setting) is to not bind the threads to processors
    // unless we know for sure that we span NUMA nodes and replication is required.
    const std::string numaPolicy = sharedState.options["NumaPolicy"];

    const bool threadBindable = [&]() {
        if (numaPolicy == "none")
            return false;

        if (numaPolicy == "auto")
            return numaConfig.suggests_binding_threads(threadCount);

        // numaPolicy == "system", "hardware" or explicitly set by the user string
        return true;
    }();

    numaNodeBoundThreadIds = threadBindable
                             ? numaConfig.distribute_threads_among_numa_nodes(threadCount)
                             : std::vector<NumaIndex>{};

    std::unordered_map<NumaIndex, std::size_t> numaIds;

    if (numaNodeBoundThreadIds.empty())
    {
        // Pretend all threads are part of numa node 0
        numaIds[0] = threadCount;
    }
    else
    {
        for (std::size_t i = 0; i < numaNodeBoundThreadIds.size(); ++i)
            ++numaIds[numaNodeBoundThreadIds[i]];
    }

    for (const auto& [numaIdx, count] : numaIds)
    {
        const auto f = [_numaIdx = numaIdx, _count = count]() {
            //sharedState.sharedHistories.try_emplace(_numaIdx, next_pow2(_count));
            next_pow2(_count);
            (void) _numaIdx;
            (void) _count;
        };

        if (threadBindable)
            numaConfig.execute_on_numa_node(numaIdx, f);
        else
            f();
    }

    numaIds.clear();

    const auto* numaConfigPtr = threadBindable ? &numaConfig : nullptr;

    for (std::size_t threadId = 0; threadId < threadCount; ++threadId)
    {
        NumaIndex numaIdx = threadBindable ? numaNodeBoundThreadIds[threadId] : 0;

        ISearchManagerPtr searchManager;
        if (threadId == 0)
            searchManager = std::make_unique<MainSearchManager>(updateContext);
        else
            searchManager = std::make_unique<NullSearchManager>();

        // When not binding threads want to force all access to happen from the same
        // NUMA node, because in case of NUMA replicated memory accesses don't
        // want to trash cache in case the threads get scheduled on the same NUMA node.
        OptionalThreadToNumaNodeBinder nodeBinder(numaIdx, numaConfigPtr);

        threads.emplace_back(std::make_unique<Thread>(threadId, numaIds[numaIdx], sharedState,
                                                      std::move(searchManager), nodeBinder));

        ++numaIds[numaIdx];
    }

    init();
}

Thread* ThreadPool::best_thread() const noexcept {

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

    std::unordered_map<Move, std::uint64_t, Move::Hash> votes(
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
void ThreadPool::start(Position&      pos,
                       StateListPtr&  states,
                       const Limit&   limit,
                       const Options& options) noexcept {
    main_thread()->wait_finish();

    stop.store(false, std::memory_order_relaxed);
    abort.store(false, std::memory_order_relaxed);
    research.store(false, std::memory_order_relaxed);

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

    auto tbConfig = Tablebases::rank_root_moves(pos, rootMoves, options, false, time_to_abort);

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

void ThreadPool::run_on_thread(std::size_t threadId, JobFunc job) noexcept {
    assert(threadId < size());

    threads[threadId]->run_custom_job(std::move(job));
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
