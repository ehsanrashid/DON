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
#include <functional>
#include <limits>
#include <ratio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "history.h"
#include "movegen.h"
#include "notation.h"
#include "option.h"
#include "types.h"
#include "tablebase/syzygy.h"

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
Thread::Thread(usize                         threadIdx,
               usize                         threadCnt,
               usize                         numaIdx,
               usize                         numaThreadCnt,
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

    usize threadCount = sharedState.options["Threads"];
    assert(threadCount != 0);

    // Create new thread(s)

    // Binding threads may be problematic when there's multiple NUMA nodes and
    // multiple engine instances running. In particular, if each instance runs
    // a single thread then they would all be mapped to the first NUMA node.
    // This is undesirable, and so the default behavior (i.e. when the user does not
    // change the NumaConfig UCI setting) is to not bind the threads to processors
    // unless we know for sure that we span NUMA nodes and replication is required.
    std::string_view NumaPolicy = sharedState.options["NumaPolicy"];

    bool threadBindable = false;

    if (NumaPolicy == "auto")
        threadBindable = numaConfig.suggests_binding_threads(threadCount);
    // "system", "hardware" or explicitly set by string
    else if (NumaPolicy != "none")
        threadBindable = true;

    // Assign threads to NUMA nodes
    std::vector<NumaIndex> thBoundNumaNodes;
    // Count threads per NUMA node
    std::unordered_map<NumaIndex, usize> numaThreadCounts;
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
    auto& sharedHistoriesMap = sharedState.sharedHistoriesMap;

    // Just clear and reserve as needed
    sharedHistoriesMap.clear();
    sharedHistoriesMap.reserve(numaThreadCounts.size());

    // Populate shared histories map (optionally NUMA-bound)
    for (const auto& _ : numaThreadCounts)
    {
        const NumaIndex numaId = _.first;
        const usize     count  = _.second;

        auto create_histories = [&]() noexcept {
            const usize roundedCount = round_up_to_pow2(count);

            sharedHistoriesMap.try_emplace(numaId, roundedCount);
        };

        if (threadBindable)
            numaConfig.execute_on_numa_node(numaId, create_histories);
        else
            create_histories();
    }

    const NumaConfig* numaConfigPtr = threadBindable ? &numaConfig : nullptr;

    // Track per-NUMA indices
    std::unordered_map<NumaIndex, usize> numaIds;
    numaIds.reserve(numaThreadCounts.size());

    reserve(threadCount);

    for (usize threadId = 0; threadId < threadCount; ++threadId)
    {
        NumaIndex numaId = thBoundNumaNodes[threadId];

        usize numaIdx       = numaIds[numaId]++;
        usize numaThreadCnt = numaThreadCounts[numaId];

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

    reset();
}

namespace {

// Metrics used to compare threads when selecting the best thread.
struct ThreadMetric final {
   public:
    // Build the comparison metrics for a thread.
    static ThreadMetric from_thread(const Thread*               th,
                                    const Array<u64, MOVE_MAX>& moveVotes) noexcept {
        const auto& rm = th->worker->root_moves()[0];

        // An aborted depth-1 search may leave the reported win/loss value inexact.
        const Value value   = rm.value;
        const bool  isBound = rm.is_bound();

        assert(rm.id != std::numeric_limits<u16>::max() && rm.id < moveVotes.size());
        const u64 voteWeight = moveVotes[rm.id];

        return {
          value,                                                   //
          value != +VALUE_INFINITE && is_win(value) && !isBound,   //
          value != -VALUE_INFINITE && is_loss(value) && !isBound,  //
          voteWeight,                                              //
          rm.size()                                                //
        };
    }

    Value value;       // Position evaluation.
    bool  win;         // Exact win (mate or tablebase win).
    bool  loss;        // Exact loss (mated or tablebase loss).
    u64   voteWeight;  // Accumulated vote weight for the move.
    usize pvSize;      // Principal variation size.
};

// Returns true when the candidate thread should replace the current best thread
struct MateBetterThread final {
   public:
    // Compare the candidate thread and the current best thread
    bool operator()(const ThreadMetric& bestThread, const ThreadMetric& candThread) const noexcept {
        // Case 1: Winning or losing mate positions
        // Both are mate results -> prefer the shorter mate (higher absolute evaluation)
        if (bestThread.win || bestThread.loss)
            return (candThread.win || candThread.loss)
                && constexpr_abs(bestThread.value) < constexpr_abs(candThread.value);

        // Case 2: Normal/Drawn positions
        return tie_break(bestThread, candThread);
    }

   private:
    // Compares normal or drawn positions using win/loss status and voting metrics
    static bool tie_break(const ThreadMetric& bestThread, const ThreadMetric& candThread) noexcept {
        // Case 3a: The current best is normal/drawn -> prefer any mate result
        if (candThread.win || candThread.loss)
            return true;

        // Case 3b: Both are normal/drawn -> compare voting metrics
        return  // Primary: vote count
          bestThread.voteWeight != candThread.voteWeight
            ? bestThread.voteWeight < candThread.voteWeight
            // Tie-break: Finally, prefer the longer PV
            : bestThread.pvSize < candThread.pvSize;
    }
};

// Returns true when the candidate thread should replace the current best thread
struct NormalBetterThread final {
   public:
    // Compare the candidate thread and the current best thread
    bool operator()(const ThreadMetric& bestThread, const ThreadMetric& candThread) const noexcept {
        // Case 1: Winning mate positions
        // Both are winning -> prefer the shorter mate (higher evaluation)
        if (bestThread.win)
            return candThread.win && bestThread.value < candThread.value;
        // Case 2: Losing mate positions
        // Prefer a non-loss; otherwise, prefer the longer mate (higher evaluation)
        if (bestThread.loss)
            return !candThread.loss || bestThread.value < candThread.value;

        // Case 3: Normal/Drawn positions
        return tie_break(bestThread, candThread);
    }

   private:
    // Compares normal or drawn positions using win/loss status and voting metrics
    static bool tie_break(const ThreadMetric& bestThread, const ThreadMetric& candThread) noexcept {
        // Case 3a: The current best is normal/drawn -> prefer a win
        if (candThread.win)
            return true;  // win beats normal/drawn result
        if (candThread.loss)
            return false;  // normal/drawn result beats loss

        // Case 3b: Both are normal/drawn -> compare voting metrics
        return  // Primary: vote count
          bestThread.voteWeight != candThread.voteWeight
            ? bestThread.voteWeight < candThread.voteWeight
            // Tie-break: Finally, prefer the longer PV
            : bestThread.pvSize < candThread.pvSize;
    }
};

}  // namespace

template<bool Mate>
const Thread* Threads::best_thread() const noexcept {
    assert(threads.size() > 1);
    // Snap threads pointers under read-lock
    std::vector<const Thread*> snapThreads;
    const auto*                fallbackThread = threads.front().get();
    Depth                      bestDepth      = fallbackThread->worker->rootDepth;
    {
        std::shared_lock readLock(sharedMutex);

        snapThreads.reserve(threads.size());

        for (auto&& th : threads)
        {
            const auto& rm = th->worker->rootMoves[0];

            if (rm.value != -VALUE_INFINITE)
                snapThreads.push_back(th.get());
            else if (th->worker->rootDepth > bestDepth)
            {
                fallbackThread = th.get();
                bestDepth      = fallbackThread->worker->rootDepth;
            }
        }
    }

    // Fallback: use completed-depth if no valid threads
    if (snapThreads.empty())
        return fallbackThread;

    Value minValue = +VALUE_INFINITE;
    for (const auto* th : snapThreads)
        minValue = std::min(th->worker->rootMoves[0].value, minValue);

    Array<u64, MOVE_MAX> moveVotes{};

    // Aggregate votes
    for (const auto* th : snapThreads)
    {
        assert(th->worker->rootMoves[0].id != std::numeric_limits<u16>::max()
               && th->worker->rootMoves[0].id < moveVotes.size());

        moveVotes[th->worker->rootMoves[0].id] += 14 + th->worker->rootMoves[0].value - minValue;
    }

    // Select the best thread

    // Initialize with first valid thread
    const auto* bestThread = snapThreads.front();

    // Compute and cache the best thread comparison metrics
    auto bestMetric = ThreadMetric::from_thread(bestThread, moveVotes);

    for (usize i = 1; i < snapThreads.size(); ++i)
    {
        const auto* candThread = snapThreads[i];

        // Compute the candidate thread comparison metrics
        const auto candMetric = ThreadMetric::from_thread(candThread, moveVotes);

        if constexpr (Mate)
        {
            if (MateBetterThread mateBetterThread; mateBetterThread(bestMetric, candMetric))
            {
                bestMetric = candMetric;
                bestThread = candThread;

                // Early exit: mate in one found (can't be improved further)
                if ((bestMetric.win || bestMetric.loss)
                    && constexpr_abs(bestMetric.value) >= VALUE_MATE_WIN_IN_1)
                    break;
            }
        }
        else
        {
            if (NormalBetterThread normalBetterThread; normalBetterThread(bestMetric, candMetric))
            {
                bestMetric = candMetric;
                bestThread = candThread;

                // Early exit: winning mate in one found (can't be improved further)
                if (bestMetric.win && bestMetric.value >= VALUE_MATE_WIN_IN_1)
                    break;
            }
        }
    }

    return bestThread;
}

// Explicit template instantiations:
template const Thread* Threads::best_thread<false>() const noexcept;
template const Thread* Threads::best_thread<true>() const noexcept;

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

            const Move m = mix_to_move(move, pos, legalMoves);

            emplace = m != Move::None && !rootMoves.contains(m);

            if (emplace)
                rootMoves.emplace_back(m);
        }
    }
    else
    {
        for (const Move m : legalMoves)
            rootMoves.emplace_back(m);
    }

    if (!limit.ignoreMoves.empty())
    {
        bool erase = true;
        for (const auto& move : limit.ignoreMoves)
        {
            if (erase && rootMoves.empty())
                break;

            const Move m = mix_to_move(move, pos, legalMoves);

            erase = m != Move::None;

            if (erase)
                erase = rootMoves.erase(m);
        }
    }

    // Assign stable IDs after rootMoves is finalized
    for (usize i = 0; i < rootMoves.size(); ++i)
        rootMoves[i].id = static_cast<u16>(i);

    auto& clock = limit.clocks[pos.active_color()];

    // If time manager is active, don't use more than 5% of clock time
    const auto startTime = std::chrono::steady_clock::now();

    auto time_to_abort = [&]() noexcept -> bool {
        const auto endTime = std::chrono::steady_clock::now();
        return limit.use_time_manager()
            && (options["NodesTime"] != 0
                || std::chrono::duration<double, std::milli>(endTime - startTime).count()
                     > (0.0500 + 0.0500 * std::clamp((clock.inc - clock.time) / 100.0, 0.0, 1.0))
                         * clock.time);
    };

    auto tbConfig =
      Tablebase::Syzygy::rank_root_moves(pos, rootMoves, options, false, time_to_abort);

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

            worker->nodes       = 0;
            worker->tbHits      = 0;
            worker->moveChanges = 0;

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

void Threads::run_on_thread(const usize threadId, const JobFunc job) const noexcept {
    Thread* thread = nullptr;
    {
        std::shared_lock readLock(sharedMutex);

        assert(threadId < size());
        thread = threads[threadId].get();
    }
    assert(thread != nullptr);

    thread->run_custom_job(std::move(job));
}

void Threads::wait_on_thread(const usize threadId) const noexcept {
    Thread* thread = nullptr;
    {
        std::shared_lock readLock(sharedMutex);

        assert(threadId < size());
        thread = threads[threadId].get();
    }
    assert(thread != nullptr);

    thread->wait_finish();
}

std::vector<NumaIndex> Threads::thread_bound_numa_nodes() const noexcept {
    return threadBoundNumaNodes;
}

std::vector<usize> Threads::bound_thread_counts() const noexcept {
    std::vector<usize> threadCounts;
    {
        std::shared_lock readLock(sharedMutex);

        if (!threadBoundNumaNodes.empty())
        {
            const NumaIndex maxNumaId =
              *std::max_element(threadBoundNumaNodes.begin(), threadBoundNumaNodes.end());

            threadCounts.resize(maxNumaId + 1, 0);

            for (const NumaIndex numaId : threadBoundNumaNodes)
                ++threadCounts[numaId];
        }
    }
    return threadCounts;
}

NumaIndex Threads::numa_nodes() const noexcept {
    std::unordered_set<NumaIndex> seenNumaIds;
    {
        std::shared_lock readLock(sharedMutex);

        for (const NumaIndex numaId : threadBoundNumaNodes)
            seenNumaIds.insert(numaId);
    }
    return std::max(seenNumaIds.size(), NumaIndex{1});
}

}  // namespace DON
