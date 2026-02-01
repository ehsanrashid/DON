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

#ifndef THREAD_H_INCLUDED
#define THREAD_H_INCLUDED

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

// Ensure SUPPORTS_PTHREADS is defined only if the platform supports pthreads (macOS, MinGW, or explicitly enabled)
#undef SUPPORTS_PTHREADS
#if defined(__APPLE__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(USE_PTHREADS)
    #define SUPPORTS_PTHREADS
#endif

#if defined(SUPPORTS_PTHREADS)
    #include <pthread.h>

    #include "misc.h"
#else  // Default case: use STL classes
    #include <thread>
#endif

#include "memory.h"
#include "numa.h"
#include "position.h"
#include "search.h"

namespace DON {

using JobFunc = std::function<void()>;

#if defined(SUPPORTS_PTHREADS)

// On OSX threads other than the main thread are created with a reduced stack
// size of 512KB by default, this is too low for deep searches,
// which require somewhat more than 1MB stack, so adjust it to 8MB.
class NativeThread final {
   public:
    // Default thread is not joinable
    NativeThread() noexcept = default;

    template<typename Function, typename... Args>
    explicit NativeThread(Function&& func, Args&&... args) noexcept {
        // Use RAII to manage JobFunc memory
        auto jobFuncPtr = std::make_unique<JobFunc>(
          std::bind(std::forward<Function>(func), std::forward<Args>(args)...));

        auto start_routine = [](void* ptr) noexcept -> void* {
            // Take ownership of JobFunc and delete when done
            std::unique_ptr<JobFunc> fnPtr(static_cast<JobFunc*>(ptr));

            // Call the function
            (*fnPtr)();

            // std::unique_ptr deletes the object when lambda exits
            return nullptr;
        };

        pthread_attr_t threadAttr;

        if (pthread_attr_init(&threadAttr) != 0)
        {
            //DEBUG_LOG("pthread_attr_init() failed to init thread attributes.");
            return;
        }

        if (pthread_attr_setstacksize(&threadAttr, TH_STACK_SIZE) != 0)
        {
            //DEBUG_LOG("pthread_attr_setstacksize() failed to set thread stack size.");
        }

        // Pass the raw pointer to pthread_create
        // pthread_create takes ownership of jobFuncPtr only on success
        if (pthread_create(&thread, &threadAttr, start_routine, jobFuncPtr.get()) != 0)
        {
            //DEBUG_LOG("pthread_create() failed to create thread.");
            // Thread creation failed, jobFuncPtr will be deleted automatically
            joined = true;
        }
        else
        {
            // Mark thread as now joinable, not joined yet
            joined = false;
            // Thread now owns it
            jobFuncPtr.release();
        }

        // Destroy thread attr
        if (pthread_attr_destroy(&threadAttr) != 0)
        {
            //DEBUG_LOG("pthread_attr_destroy() failed to destroy thread attributes.");
        }
    }

    // Non-copyable
    NativeThread(const NativeThread&) noexcept            = delete;
    NativeThread& operator=(const NativeThread&) noexcept = delete;

    // Movable
    NativeThread(NativeThread&& nativeThread) noexcept :
        thread(nativeThread.thread),
        joined(nativeThread.joined) {
        nativeThread.joined = true;
    }
    NativeThread& operator=(NativeThread&& nativeThread) noexcept {
        if (this == &nativeThread)
            return *this;

        join();

        thread = nativeThread.thread;
        joined = nativeThread.joined;

        nativeThread.joined = true;

        return *this;
    }

    // RAII: join on destruction if thread is joinable
    ~NativeThread() noexcept { join(); }

    bool joinable() const noexcept { return !joined; }

    void join() noexcept {
        if (joinable())
        {
            pthread_join(thread, nullptr);

            joined = true;
        }
    }

   private:
    static constexpr std::size_t TH_STACK_SIZE = 8 * ONE_MB;

    pthread_t thread{};
    bool      joined = true;
};

#else

using NativeThread = std::thread;

#endif

// Sometimes don't want to actually bind the threads, but the recipient still needs
// to think it runs on *some* NUMA node, such that it can access structures that rely
// on NUMA node knowledge.
// This class encapsulates this optional process of binding a thread to a NUMA node
// such that the recipient does not need to know whether the binding happened or not.
class ThreadToNumaNodeBinder final {
   public:
    ThreadToNumaNodeBinder(NumaIndex numaIdx, const NumaConfig* numaCfgPtr) noexcept :
        numaId(numaIdx),
        numaConfigPtr(numaCfgPtr) {}

    explicit ThreadToNumaNodeBinder(NumaIndex numaIdx) noexcept :
        ThreadToNumaNodeBinder(numaIdx, nullptr) {}

    NumaReplicatedAccessToken operator()() const noexcept {
        return numaConfigPtr != nullptr ? numaConfigPtr->bind_current_thread_to_numa_node(numaId)
                                        : NumaReplicatedAccessToken(numaId);
    }

   private:
    const NumaIndex         numaId;
    const NumaConfig* const numaConfigPtr;
};

using WorkerPtr = LargePagePtr<Worker>;

// Abstraction of a thread. It contains a pointer to the worker and a native thread.
// After construction, the native thread is started with idle_func()
// waiting for a signal to start searching.
// When the signal is received, the thread starts searching and when
// the search is finished, it goes back to idle_func() waiting for a new signal.
class Thread final {
   public:
    Thread(std::size_t                   threadIdx,
           std::size_t                   threadCnt,
           std::size_t                   numaIdx,
           std::size_t                   numaThreadCnt,
           const ThreadToNumaNodeBinder& nodeBinder,
           ISearchManagerPtr             searchManager,
           const SharedState&            sharedState,
           bool                          autoStart = true) noexcept;

    ~Thread() noexcept;

    constexpr std::size_t thread_id() const noexcept { return threadId; }

    constexpr std::size_t thread_count() const noexcept { return threadCount; }

    constexpr std::size_t numa_id() const noexcept { return numaId; }

    constexpr std::size_t numa_thread_count() const noexcept { return numaThreadCount; }

    NumaReplicatedAccessToken numa_access_token() const noexcept { return numaAccessToken; }

    void start() noexcept;

    void terminate() noexcept;

    void ensure_network_replicated() const noexcept;

    // Schedule a job to be executed by this thread.
    void run_custom_job(JobFunc job) noexcept;

    // Wakes up the thread that will initialize the worker
    void init() noexcept;

    // Wakes up the thread that will start the search on worker
    void start_search() noexcept;

    // Blocks on the condition variable until the thread has finished job
    void wait_finish() noexcept;

   private:
    // The main function of the thread
    void idle_func() noexcept;

    bool dead = false, busy = true;

    const std::size_t threadId, threadCount, numaId, numaThreadCount;

    std::mutex                mutex;
    std::condition_variable   condVar;
    NativeThread              nativeThread;
    NumaReplicatedAccessToken numaAccessToken;

    JobFunc jobFunc;

   public:
    WorkerPtr worker;
};

// Schedule a job to be executed by this thread.
// This function blocks only until the thread is ready to accept a new job.
// The actual job execution happens asynchronously in idle_func().
inline void Thread::run_custom_job(JobFunc jobFn) noexcept {
    {
        std::unique_lock lock(mutex);

        // Wait until the thread is idle or being terminated.
        // - If !busy, the thread is ready to accept a new job.
        // - If dead, the thread is shutting down, so we shouldn't schedule work.
        condVar.wait(lock, [this] { return !busy || dead; });

        // If the thread is still alive, schedule the job
        if (!dead)
        {
            // Move the job into the shared slot for idle_func() to pick up
            jobFunc = std::move(jobFn);
            jobFn   = nullptr;  // optional, defensive

            // Mark the thread as busy so that other run_custom_job calls
            // will wait until this job is complete.
            busy = true;
        }
        //// If thread is being torn down, don't schedule the job
        //else
        //{}
    }

    // Notify the thread that a new job is available.
    // This wakes idle_func() if it is currently waiting.
    condVar.notify_one();
}

// Wakes up the thread that will initialize the worker
inline void Thread::init() noexcept {
    assert(worker != nullptr);

    run_custom_job([this]() { worker->init(); });
}

// Wakes up the thread that will start the search on worker
inline void Thread::start_search() noexcept {
    assert(worker != nullptr);

    run_custom_job([this]() { worker->start_search(); });
}

// Blocks on the condition variable until the thread has finished job
inline void Thread::wait_finish() noexcept {
    std::unique_lock lock(mutex);

    condVar.wait(lock, [this] { return !busy || dead; });
}

// A list to keep track of the position states along the setup moves
// (from the start position to the position just before the search starts).
// Needed by 'draw by repetition' detection.
// Use a std::deque because pointers to elements are not invalidated upon list resizing.
using StateList    = std::deque<State>;
using StateListPtr = std::unique_ptr<StateList>;

using ThreadPtr = std::unique_ptr<Thread>;

class Options;

// Threads handles all the threads-related stuff like
// launching, initializing, starting and parking a thread.
// All the access to threads is done through this class.
class Threads final {
   public:
    Threads() noexcept = default;
    ~Threads() noexcept;

    auto begin() noexcept { return threads.begin(); }
    auto end() noexcept { return threads.end(); }
    auto begin() const noexcept { return threads.begin(); }
    auto end() const noexcept { return threads.end(); }

    std::size_t size() const noexcept {
        std::shared_lock threadsLock(sharedMutex);

        return threads.size();
    }
    bool empty() const noexcept {
        std::shared_lock threadsLock(sharedMutex);

        return threads.empty();
    }

    void reserve(std::size_t threadCount) noexcept {
        std::unique_lock lock(sharedMutex);

        threads.reserve(threadCount);
    }

    void clear() noexcept {
        std::unique_lock lock(sharedMutex);

        threads.clear();
    }

    void destroy() noexcept;

    void set(const NumaConfig&                       numaConfig,
             SharedState&                            sharedState,
             const MainSearchManager::UpdateContext& updateContext) noexcept;

    void init() const noexcept;

    Thread* main_thread() const noexcept;

    MainSearchManager* main_manager() const noexcept;

    Thread* best_thread() const noexcept;

    void
    start(Position& pos, StateListPtr& states, const Limit& limit, const Options& options) noexcept;

    void start_search() const noexcept;

    void wait_finish() const noexcept;

    void ensure_network_replicated() const noexcept;

    void run_on_thread(std::size_t threadId, JobFunc job) noexcept;

    void wait_on_thread(std::size_t threadId) noexcept;

    std::vector<std::size_t> get_bound_thread_counts() const noexcept;

    // --- queries ---
    bool is_active() const noexcept {
        auto current = state.load(std::memory_order_acquire);

        return current == State::Active || current == State::Research;
    }

    bool is_researching() const noexcept {
        return state.load(std::memory_order_acquire) == State::Research;
    }

    bool is_stopped() const noexcept {
        auto current = state.load(std::memory_order_acquire);

        return current == State::Aborted || current == State::Stopped;
    }

    bool is_aborted() const noexcept {
        return state.load(std::memory_order_acquire) == State::Aborted;
    }

    // --- actions ---
    void request_research() noexcept {
        auto current = state.load(std::memory_order_relaxed);

        while (true)
        {
            // Don't override aborted or stopped states
            if (current == State::Aborted || current == State::Stopped)
                break;

            // Try to transition to Research
            if (state.compare_exchange_weak(current, State::Research, std::memory_order_release,
                                            std::memory_order_relaxed))
                break;
            // current is updated on failure, loop continues
        }
    }

    void request_stop() noexcept {
        auto current = state.load(std::memory_order_relaxed);

        while (true)
        {
            // Don't override aborted state
            if (current == State::Aborted)
                break;

            // Try to transition to Stopped
            if (state.compare_exchange_weak(current, State::Stopped, std::memory_order_release,
                                            std::memory_order_relaxed))
                break;
            // current is updated on failure, loop continues
        }

        notify_main_manager();
    }

    void request_abort() noexcept {
        // Always go to aborted
        state.store(State::Aborted, std::memory_order_release);

        notify_main_manager();
    }


    void notify_main_manager() const noexcept {
        std::shared_lock threadsLock(sharedMutex);

        assert(!threads.empty());

        auto* mainThread = threads.front().get();
        // Only proceed if main thread exists
        assert(mainThread != nullptr);

        auto* mainManager = mainThread->worker->main_manager();
        // Only proceed if main manager exists
        assert(mainManager != nullptr);

        // Try to acquire the main manager mutex to ensure the waiting thread
        // observes the updated state before it wakes.
        // If locking fails still notify — notify_one() is allowed without holding the lock.
        std::unique_lock lock(mainManager->mutex, std::try_to_lock);
        // Safe to call even if mutex not locked
        mainManager->condVar.notify_one();
    }

    template<typename Func>
    void for_each_thread(Func&& func, bool includeMain = true) const noexcept {
        std::shared_lock threadsLock(sharedMutex);

        for (auto&& th : threads)
        {
            if (!includeMain && th == threads.front())
                continue;

            func(th.get());
        }
    }

    template<typename T>
    void set(std::atomic<T> Worker::* member, T value) noexcept {
        std::shared_lock threadsLock(sharedMutex);

        for (auto&& th : threads)
            (th->worker.get()->*member).store(value, std::memory_order_relaxed);
    }

    template<typename T>
    std::uint64_t sum(std::atomic<T> Worker::* member,
                      std::uint64_t            initialSum = 0) const noexcept {
        std::shared_lock threadsLock(sharedMutex);

        std::uint64_t sum = initialSum;
        for (auto&& th : threads)
            sum += (th->worker.get()->*member).load(std::memory_order_relaxed);

        return sum;
    }

   private:
    // State transition diagram:
    // Active -> Research -> Stopped
    //   |          |           |
    //   ---------->----------->-
    //   |          |           |
    //   -------------------------> Aborted (final, cannot transition out)
    enum class State : std::uint8_t {
        Active,
        Research,
        Stopped,
        Aborted
    };

    Threads(const Threads&) noexcept            = delete;
    Threads(Threads&&) noexcept                 = delete;
    Threads& operator=(const Threads&) noexcept = delete;
    Threads& operator=(Threads&&) noexcept      = delete;

    // Protects concurrent access to the threads vector for short snapshots.
    // Use shared lock for readers and unique lock for writers when mutating threads.
    mutable std::shared_mutex sharedMutex;

    std::atomic<State> state{State::Active};

    std::vector<ThreadPtr> threads;
    std::vector<NumaIndex> threadBoundNumaNodes;
    StateListPtr           setupStates;
};

inline Threads::~Threads() noexcept { destroy(); }

// Destroy any existing thread(s)
inline void Threads::destroy() noexcept {
    Thread* mainThread = nullptr;
    // Acquire shared lock once to safely snapshot main thread
    {
        std::shared_lock threadsLock(sharedMutex);

        if (!threads.empty())
            mainThread = threads.front().get();
    }

    if (mainThread != nullptr)
    {
        // Wake main manager (in case it is waiting)
        notify_main_manager();

        mainThread->wait_finish();
    }

    // Clear threads and thread binding nodes
    std::unique_lock lock(sharedMutex);

    threads.clear();
    threadBoundNumaNodes.clear();
}

// Sets data to initial values
inline void Threads::init() const noexcept {
    if (empty())
        return;

    // Initialize all threads (including main)
    for_each_thread([](Thread* th) noexcept { th->init(); });
    for_each_thread([](Thread* th) noexcept { th->wait_finish(); });

    // Initialize main manager
    if (auto mainManager = main_manager())
        mainManager->init();
}

// Get pointer to the main thread
inline Thread* Threads::main_thread() const noexcept {
    std::shared_lock threadsLock(sharedMutex);

    return threads.empty() ? nullptr : threads.front().get();
}

// Get pointer to the main search manager
inline MainSearchManager* Threads::main_manager() const noexcept {
    std::shared_lock threadsLock(sharedMutex);

    if (threads.empty())
        return nullptr;

    // Avoid calling main_thread() here because it would try to lock sharedMutex again.
    // Snapshot the main thread pointer under the shared lock and return its manager.
    return threads.front()->worker != nullptr ? threads.front()->worker->main_manager() : nullptr;
}

// Start non-main threads
// Will be invoked by main thread after it has started searching
inline void Threads::start_search() const noexcept {
    for_each_thread([](Thread* th) noexcept { th->start_search(); }, false);  // skip main
}

// Wait for non-main threads
// Will be invoked by main thread after it has finished searching
inline void Threads::wait_finish() const noexcept {
    for_each_thread([](Thread* th) noexcept { th->wait_finish(); }, false);  // skip main
}

// Ensure that all threads have their network replicated
inline void Threads::ensure_network_replicated() const noexcept {
    for_each_thread([](Thread* th) noexcept { th->ensure_network_replicated(); });
}

}  // namespace DON

#endif  // #ifndef THREAD_H_INCLUDED
