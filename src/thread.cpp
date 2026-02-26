/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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

#include "history.h"
#include "movegen.h"
#include "option.h"
#include "types.h"
#include "uci.h"
#include "syzygy/tablebase.h"

#if !defined(SUPPORTS_PTHREADS)
    #include "misc.h"
#endif

namespace DON {

// Constructor for a worker thread.
//
// Responsibilities:
//   - Initializes thread and NUMA-related identifiers.
//   - Optionally starts the thread immediately (if autoStart is true).
//      * The thread will execute idle_func() and go to sleep.
//      * The constructor waits until the thread reaches the idle state to ensure
//        it is ready to accept jobs safely.
//   - Acquires a NUMA access token from the provided nodeBinder.
//   - Constructs the Worker object for this thread, allocating on large pages
//      for performance, and passing thread/NUMA info along with shared state and
//      the search manager.
//
// Preconditions:
//   - numa_thread_count() != 0
//   - numa_id() < numa_thread_count()
Thread::Thread(std::size_t                   threadIdx,
               std::size_t                   threadCnt,
               std::size_t                   numaIdx,
               std::size_t                   numaThreadCnt,
               const ThreadToNumaNodeBinder& nodeBinder,
               ISearchManagerPtr             searchManager,
               const SharedState&            sharedState,
               bool                          autoStart) noexcept :
    threadId(threadIdx),
    threadCount(threadCnt),
    numaId(numaIdx),
    numaThreadCount(numaThreadCnt) {
    assert(numa_thread_count() != 0 && numa_id() < numa_thread_count());
    //DEBUG_LOG("Creating Thread id: " << thread_id() << "/" << thread_count() << " on NUMA node " << numa_id() << "/" << numa_thread_count());

    // Bind this thread to a NUMA node for memory affinity
    numaAccessToken = nodeBinder();

    // Create aligned Worker object with NUMA and thread info
    worker = make_unique_aligned_large_page<Worker>(thread_id(), thread_count(),     //
                                                    numa_id(), numa_thread_count(),  //
                                                    numa_access_token(), std::move(searchManager),
                                                    sharedState);

    // Start the thread only after full initialization
    // Launch thread and wait until idle_func() puts it to sleep
    if (autoStart)
        start();
}

// Destructor: ensures the thread is properly terminated and joined.
Thread::~Thread() noexcept {
    // Ensure thread is terminated and joined. Do not assert on 'busy'.
    // terminate() sets 'dead' and joins the native thread safely, even if a job is running.
    terminate();
}

// Starts the thread if it is not already running.
//
// Guarantees:
//   - After this function returns, the thread is alive and ready to accept jobs.
//   - The 'busy' flag is properly synchronized to avoid race conditions.
//   - If the thread is already running, this function does nothing.
//
// Working:
//   - Acquires the mutex to synchronize access to thread state.
//   - Checks if a native thread is already joinable (running); if so, returns immediately.
//   - Resets 'dead' and 'busy' flags to prepare for a new thread.
//   - Creates a new NativeThread that runs idle_func() on this Thread object.
//   - Waits on the condition variable until the new thread reports itself idle (busy == false),
//     and ready to accept jobs, ensuring that the thread is fully initialized before returning.
void Thread::start() noexcept {
    std::unique_lock condLock(mutex);

    // If thread is already running, do nothing
    if (nativeThread.joinable())
        return;

    // Reset flags before starting new nativeThread
    dead = false;
    busy = true;

    // Move new NativeThread in
    nativeThread = NativeThread(&Thread::idle_func, this);

    // Wait until the new thread reaches idle
    condVar.wait(condLock, [this] { return !busy; });
}

// Safely terminates the thread by setting the 'dead' flag,
// waking it if necessary, and joining the native thread.
void Thread::terminate() noexcept {
    {
        std::lock_guard writeLock(mutex);

        dead = true;
    }

    // Wake up the thread if it's waiting
    condVar.notify_one();

    // Join the native thread if joinable
    if (nativeThread.joinable())
        nativeThread.join();

    //DEBUG_LOG("Thread id: " << thread_id() << " terminated.");
}

// Thread main function: waits for work and executes jobs.
// When no job is scheduled, the thread parks here, blocked on the condition variable.
void Thread::idle_func() noexcept {
    //DEBUG_LOG("Thread id: " << thread_id() << " started.");

    while (true)
    {
        std::unique_lock condLock(mutex);

        // Mark thread as idle now.
        // Any thread trying to schedule work will see busy = false.
        busy = false;

        // Notify one waiting thread (e.g., run_custom_job)
        // that the thread is now idle and ready for work.
        condVar.notify_one();

        // Wait until either:
        // 1) A new job is scheduled (busy == true), or
        // 2) The thread is being stopped (dead == true)
        condVar.wait(condLock, [this] { return busy || dead; });

        // If thread is being torn down, exit immediately.
        if (dead)
            break;

        // Move the scheduled job out of the shared storage.
        // This allows run_custom_job to schedule another job
        // while we are executing the current one.
        JobFunc jobFn = std::move(jobFunc);
        jobFunc       = nullptr;  // optional, defensive

        // Unlock before executing the job to allow other threads
        // to schedule work or shut down concurrently.
        condLock.unlock();

        // Execute the job outside the lock to avoid holding the mutex
        // for the duration of potentially long-running work.
        if (jobFn)
            jobFn();
    }

    //DEBUG_LOG("Thread id: " << thread_id() << " exited.");
}

void Thread::ensure_network_replicated() const noexcept { worker->ensure_network_replicated(); }


// Destroys/Creates threads to match the thread-count.
// Created and launched threads will immediately go to sleep in idle_func.
// Upon resizing, threads are recreated to allow for binding if necessary.
void Threads::set(const NumaConfig&                       numaConfig,
                  SharedState&                            sharedState,
                  const MainSearchManager::UpdateContext& updateContext) noexcept {
    destroy();

    std::size_t threadCount = sharedState.options["Threads"];
    assert(threadCount != 0);

    // Create new thread(s)

    // Binding threads may be problematic when there's multiple NUMA nodes and
    // multiple engine instances running. In particular, if each instance runs
    // a single thread then they would all be mapped to the first NUMA node.
    // This is undesirable, and so the default behavior (i.e. when the user does not
    // change the NumaConfig UCI setting) is to not bind the threads to processors
    // unless we know for sure that we span NUMA nodes and replication is required.
    const std::string& NumaPolicy = sharedState.options["NumaPolicy"];

    bool threadBindable = false;

    if (NumaPolicy == "auto")
        threadBindable = numaConfig.suggests_binding_threads(threadCount);
    // "system", "hardware" or explicitly set by string
    else if (NumaPolicy != "none")
        threadBindable = true;

    // Assign threads to NUMA nodes
    std::vector<NumaIndex> thBoundNumaNodes;
    // Count threads per NUMA node
    std::unordered_map<NumaIndex, std::size_t> numaThreadCounts;
    if (threadBindable)
    {
        std::lock_guard writeLock(sharedMutex);

        threadBoundNumaNodes = numaConfig.distribute_threads_among_numa_nodes(threadCount);

        thBoundNumaNodes = threadBoundNumaNodes;

        numaThreadCounts.reserve(thBoundNumaNodes.size());
        for (NumaIndex numaId : thBoundNumaNodes)
            ++numaThreadCounts[numaId];
    }
    else
    {
        std::lock_guard writeLock(sharedMutex);

        threadBoundNumaNodes.clear();

        thBoundNumaNodes = std::vector(threadCount, NumaIndex{0});

        numaThreadCounts.reserve(1);
        // All threads belong to NUMA node 0
        numaThreadCounts.emplace(NumaIndex{0}, threadCount);
    }

    // Prepare shared histories map
    auto& historiesMap = sharedState.historiesMap;

    // Just clear and reserve as needed
    historiesMap.clear();
    historiesMap.reserve(numaThreadCounts.size());

    // Populate shared histories map (optionally NUMA-bound)
    for (const auto& _ : numaThreadCounts)
    {
        NumaIndex   numaId = _.first;
        std::size_t count  = _.second;

        auto create_histories = [&]() noexcept {
            std::size_t roundedCount = round_up_to_pow2(count);

            historiesMap.try_emplace(numaId, roundedCount);
        };

        if (threadBindable)
            numaConfig.execute_on_numa_node(numaId, create_histories);
        else
            create_histories();
    }

    const NumaConfig* numaConfigPtr = threadBindable ? &numaConfig : nullptr;

    // Track per-NUMA indices
    std::unordered_map<NumaIndex, std::size_t> numaIds;
    numaIds.reserve(numaThreadCounts.size());

    reserve(threadCount);

    for (std::size_t threadId = 0; threadId < threadCount; ++threadId)
    {
        NumaIndex numaId = thBoundNumaNodes[threadId];

        std::size_t numaIdx       = numaIds[numaId]++;
        std::size_t numaThreadCnt = numaThreadCounts[numaId];

        auto create_thread = [this, threadId, threadCount, numaId, numaIdx, numaThreadCnt,
                              numaConfigPtr, &sharedState, &updateContext]() noexcept {
            // Search manager for this thread
            ISearchManagerPtr searchManager;
            if (threadId == 0)
                searchManager = std::make_unique<MainSearchManager>(updateContext);
            else
                searchManager = std::make_unique<NullSearchManager>();

            // When not binding threads want to force all access to happen from the same
            // NUMA node, because in case of NUMA replicated memory accesses don't want
            // to trash cache in case the threads get scheduled on the same NUMA node.
            ThreadToNumaNodeBinder nodeBinder(numaId, numaConfigPtr);

            auto newThread =
              std::make_unique<Thread>(threadId, threadCount, numaIdx, numaThreadCnt, nodeBinder,
                                       std::move(searchManager), sharedState, true);
            // Mutate threads list under write lock to avoid races
            {
                std::lock_guard writeLock(sharedMutex);

                threads.emplace_back(std::move(newThread));
            }
        };

        // Create thread on its target NUMA node for proper memory affinity
        if (threadBindable)
            numaConfig.execute_on_numa_node(numaId, create_thread);
        else
            create_thread();
    }

    init();
}

namespace {
// Properties of thread used for best-thread selection
struct ThreadMetric final {
   public:
    // Factory function: build metrics for a thread {value, win/loss, votes, vote weight, PV size}
    template<typename VotingFunc>
    static ThreadMetric from_thread(const Thread*                            th,
                                    const StdArray<std::uint64_t, MOVE_MAX>& votes,
                                    VotingFunc&& calc_vote_weight) noexcept {
        const auto& rm = th->worker->root_moves()[0];

        Value value = rm.effective_value();

        assert(rm.id != UINT16_MAX && rm.id < votes.size());
        std::uint64_t voteCount = votes[rm.id];

        return {
          voteCount,                                       //
          std::forward<VotingFunc>(calc_vote_weight)(th),  //
          rm.pv.size(),                                    //
          value,                                           //
          is_win(value),                                   //
          is_loss(value)                                   //
        };
    }

    std::uint64_t voteCount;   // Number of votes for this thread's move
    std::uint64_t voteWeight;  // Weighted voting value (depth-adjusted)
    std::size_t   pvSize;      // Principal variation size
    Value         value;       // Position evaluation
    bool          win;         // Proven win (mate or TB win)
    bool          loss;        // Proven loss (mated or TB loss)
};

// Predicate: returns true if candidate-thread is better than best-thread
struct BetterThread final {
   public:
    // Returns true if next thread is better than current best
    bool operator()(const ThreadMetric& best, const ThreadMetric& cand) const noexcept {
        // Case 1: Winning positions
        // Both winning -> prefer shorter mates (higher eval)
        if (best.win)
            return cand.win && best.value < cand.value;
        // Case 2: Losing positions
        // Best is losing -> prefer escape to non-loss, or longer mated (delay defeat)
        if (best.loss)
            return !cand.loss || best.value < cand.value;

        // Case 3: Normal/Draw positions
        return tie_break(best, cand);
    }

   private:
    // Tie-break for normal/draw positions
    static bool tie_break(const ThreadMetric& best, const ThreadMetric& cand) noexcept {
        // Case 3a: Best is normal (draw) -> win dominates, ignore loss
        if (cand.win)
            return true;  // Win beats draw
        if (cand.loss)
            return false;  // Draw beats loss

        // Case 3b: Both normal -> compare by voting metrics
        if (best.voteCount != cand.voteCount)
            return best.voteCount < cand.voteCount;  // Primary: vote count
        if (best.voteWeight != cand.voteWeight)
            return best.voteWeight < cand.voteWeight;  // Tie-break 1: depth-weighted value
        return best.pvSize < cand.pvSize;              // Tie-break 2: PV size
    }
};

}  // namespace

const Thread* Threads::best_thread() const noexcept {
    assert(threads.size() > 1);
    // Snap threads pointers under read-lock
    std::vector<const Thread*> snapThreads;
    const Thread*              fallbackThread = threads.front().get();
    Depth                      bestDepth      = fallbackThread->worker->completedDepth;
    {
        std::shared_lock readLock(sharedMutex);

        snapThreads.reserve(threads.size());

        for (auto&& th : threads)
        {
            const auto& rm = th->worker->rootMoves[0];

            if (rm.effective_value() != -VALUE_INFINITE && !rm.pv.empty())
                snapThreads.push_back(th.get());
            else if (th->worker->completedDepth > bestDepth)
            {
                fallbackThread = th.get();
                bestDepth      = fallbackThread->worker->completedDepth;
            }
        }
    }

    // Fallback: use completed-depth if no valid threads
    if (snapThreads.empty())
        return fallbackThread;

    // Initialize with first valid thread
    const Thread* bestThread = snapThreads.front();

    // Find the minimum value of all threads
    Value minValue = bestThread->worker->rootMoves[0].effective_value();
    for (std::size_t i = 1; i < snapThreads.size(); ++i)
    {
        const auto& rm = snapThreads[i]->worker->rootMoves[0];

        minValue = std::min(rm.effective_value(), minValue);
    }

    // Vote according to value and depth, and select the best thread
    auto calc_vote_weight = [minValue](const Thread* th) noexcept -> std::uint64_t {
        const auto& rm = th->worker->rootMoves[0];

        Value value   = rm.effective_value();
        bool  penalty = rm.curValue == -VALUE_INFINITE;
        assert(value >= minValue);

        return std::uint64_t(14 + value - minValue)
             * std::uint64_t(std::max(th->worker->completedDepth - int(penalty), 1));
    };

    StdArray<std::uint64_t, MOVE_MAX> votes{};

    // Aggregate votes
    for (const auto* th : snapThreads)
    {
        const auto& rm = th->worker->rootMoves[0];

        assert(rm.id != UINT16_MAX && rm.id < votes.size());

        votes[rm.id] += calc_vote_weight(th);
    }

    // Cache best thread properties
    auto bestMetric = ThreadMetric::from_thread(bestThread, votes, calc_vote_weight);
    // Find best-thread
    for (std::size_t i = 1; i < snapThreads.size(); ++i)
    {
        const auto* candThread = snapThreads[i];

        // Get candidate thread properties
        auto candMetric = ThreadMetric::from_thread(candThread, votes, calc_vote_weight);

        if (BetterThread betterThread; betterThread(bestMetric, candMetric))
        {
            bestMetric = candMetric;
            bestThread = candThread;

            // Early exit: mate in 1 found (can't improve further)
            if (bestMetric.win && bestMetric.value >= VALUE_MATE_WIN_IN_1)
                break;
        }
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

    MoveList<GenType::LEGAL> legalMoves(pos);

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
        for (Move m : legalMoves)
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

    // Assign stable IDs after rootMoves is finalized
    for (std::uint16_t i = 0; i < rootMoves.size(); ++i)
        rootMoves[i].id = i;

    auto& clock = limit.clocks[pos.active_color()];

    // If time manager is active, don't use more than 5% of clock time
    auto startTime = std::chrono::steady_clock::now();

    auto time_to_abort = [&]() noexcept -> bool {
        auto endTime = std::chrono::steady_clock::now();
        return limit.use_time_manager()
            && (options["NodesTime"] != 0
                || std::chrono::duration<double, std::milli>(endTime - startTime).count()
                     > (0.0500
                        + 0.0500 * std::clamp(double(clock.inc - clock.time) / 100.0, 0.0, 1.0))
                         * double(clock.time));
    };

    auto tbConfig = Tablebase::rank_root_moves(pos, rootMoves, options, false, time_to_abort);

    // After ownership transfer 'states' becomes empty, so if stop the search
    // and call 'go' again without setting a new position states.get() == nullptr.
    assert(states.get() != nullptr || setupStates.get() != nullptr);

    if (states.get() != nullptr)
        setupStates = std::move(states);  // Ownership transfer, states is now empty

    // snap-shot pointers under shared lock
    std::vector<Thread*> snapThreads;
    {
        std::shared_lock readLock(sharedMutex);

        snapThreads.reserve(threads.size());

        for (auto&& th : threads)
            snapThreads.push_back(th.get());
    }

    // Use Position::set() to set root position across threads.
    // The rootState is per thread, earlier states are shared since they are read-only.
    for (auto* th : snapThreads)
    {
        th->run_custom_job([th, &pos, &rootMoves, &limit, &tbConfig]() noexcept {
            auto* worker = th->worker.get();

            worker->nodes.store(0, std::memory_order_relaxed);
            worker->tbHits.store(0, std::memory_order_relaxed);
            worker->moveChanges.store(0, std::memory_order_relaxed);

            worker->rootPos.set(pos, &worker->rootState);
            worker->rootMoves = rootMoves;
            worker->limit     = limit;
            worker->tbConfig  = tbConfig;
        });
    }

    for (auto* th : snapThreads)
        th->wait_finish();

    main_thread()->start_search();
}

void Threads::run_on_thread(std::size_t threadId, JobFunc job) const noexcept {
    Thread* thread = nullptr;
    {
        std::shared_lock readLock(sharedMutex);

        assert(threadId < size());
        thread = threads[threadId].get();
    }
    assert(thread != nullptr);

    thread->run_custom_job(std::move(job));
}

void Threads::wait_on_thread(std::size_t threadId) const noexcept {
    Thread* thread = nullptr;
    {
        std::shared_lock readLock(sharedMutex);

        assert(threadId < size());
        thread = threads[threadId].get();
    }
    assert(thread != nullptr);

    thread->wait_finish();
}

std::vector<std::size_t> Threads::bound_thread_counts() const noexcept {
    std::vector<std::size_t> threadCounts;
    {
        std::shared_lock readLock(sharedMutex);

        if (!threadBoundNumaNodes.empty())
        {
            NumaIndex maxNumaId =
              *std::max_element(threadBoundNumaNodes.begin(), threadBoundNumaNodes.end());

            threadCounts.resize(maxNumaId + 1, 0);

            for (NumaIndex numaId : threadBoundNumaNodes)
                ++threadCounts[numaId];
        }
    }
    return threadCounts;
}

}  // namespace DON
