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
#include <utility>
#include <vector>

#if defined(__APPLE__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(USE_PTHREADS)
    #include <pthread.h>
#else  // Default case: use STL classes
    #include <thread>
#endif

#include "memory.h"
#include "numa.h"
#include "position.h"
#include "search.h"

namespace DON {

#if defined(__APPLE__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(USE_PTHREADS)

// On OSX threads other than the main thread are created with a reduced stack
// size of 512KB by default, this is too low for deep searches,
// which require somewhat more than 1MB stack, so adjust it to 8MB.
class NativeThread final {
   public:
    NativeThread() noexcept = delete;

    template<typename Function, typename... Args>
    NativeThread(Function&& func, Args&&... args) noexcept {
        using Func = std::function<void()>;

        auto* funcPtr =
          new Func(std::bind(std::forward<Function>(func), std::forward<Args>(args)...));

        const auto start_routine = [](void* ptr) noexcept -> void* {
            auto* fnPtr = static_cast<Func*>(ptr);

            // Call the function
            (*fnPtr)();

            delete fnPtr;

            return nullptr;
        };

        pthread_attr_t attr;

        if (pthread_attr_init(&attr) != 0)
        {
            delete funcPtr;
            return;
        }

        pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);

        if (pthread_create(&thread, &attr, start_routine, funcPtr) != 0)
            delete funcPtr;

        pthread_attr_destroy(&attr);
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
    pthread_t thread;
    bool      joined = false;
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
    ThreadToNumaNodeBinder(NumaIndex numaIdx, const NumaConfig* const numaCfgPtr) noexcept :
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

using JobFunc   = std::function<void()>;
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
           const SharedState&            sharedState) noexcept;

    ~Thread() noexcept;

    constexpr std::size_t thread_id() const noexcept { return threadId; }

    constexpr std::size_t thread_count() const noexcept { return threadCount; }

    constexpr std::size_t numa_id() const noexcept { return numaId; }

    constexpr std::size_t numa_thread_count() const noexcept { return numaThreadCount; }

    NumaReplicatedAccessToken numa_access_token() const noexcept { return numaAccessToken; }

    void ensure_network_replicated() const noexcept;

    void wait_finish() noexcept;

    void run_custom_job(JobFunc job) noexcept;

    void idle_func() noexcept;

    void init() noexcept;

    void start_search() noexcept;

   private:
    // Set before starting nativeThread
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

// Blocks on the condition variable until the thread has finished job
inline void Thread::wait_finish() noexcept {
    std::unique_lock lock(mutex);

    condVar.wait(lock, [this] { return !busy; });
}

// Launching a job in the thread
inline void Thread::run_custom_job(JobFunc jobFn) noexcept {
    {
        std::unique_lock lock(mutex);

        condVar.wait(lock, [this] { return !busy; });

        jobFunc = std::move(jobFn);

        busy = true;
    }

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
    Threads() noexcept                          = default;
    Threads(const Threads&) noexcept            = delete;
    Threads(Threads&&) noexcept                 = delete;
    Threads& operator=(const Threads&) noexcept = delete;
    Threads& operator=(Threads&&) noexcept      = delete;
    ~Threads() noexcept;

    auto begin() noexcept { return threads.begin(); }
    auto end() noexcept { return threads.end(); }
    auto begin() const noexcept { return threads.begin(); }
    auto end() const noexcept { return threads.end(); }

    auto& front() const noexcept { return threads.front(); }
    auto& back() const noexcept { return threads.back(); }

    std::size_t size() const noexcept { return threads.size(); }
    bool        empty() const noexcept { return threads.empty(); }

    void reserve(std::size_t newCapacity) noexcept { threads.reserve(newCapacity); }

    void clear() noexcept;

    void set(const NumaConfig&                       numaConfig,
             SharedState                             sharedState,
             const MainSearchManager::UpdateContext& updateContext) noexcept;

    void init() noexcept;

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
        auto current = state.load(std::memory_order_relaxed);
        return current == State::Active || current == State::Research;
    }

    bool is_researching() const noexcept {
        return state.load(std::memory_order_relaxed) == State::Research;
    }

    bool is_stopped() const noexcept {
        auto current = state.load(std::memory_order_relaxed);
        return current == State::Aborted || current == State::Stopped;
    }

    bool is_aborted() const noexcept {
        return state.load(std::memory_order_relaxed) == State::Aborted;
    }

    // --- actions ---
    void request_research() noexcept {
        while (true)
        {
            // Do not override aborted or stopped
            if (is_stopped())
                return;

            auto current = state.load(std::memory_order_relaxed);

            if (state.compare_exchange_strong(current, State::Research, std::memory_order_relaxed))
                return;

            // If failed, loop with new state
        }
    }

    void request_stop() noexcept {
        while (true)
        {
            // Do not override aborted or stopped
            if (is_stopped())
                return;

            auto current = state.load(std::memory_order_relaxed);

            // Try to set to Stopped
            if (state.compare_exchange_strong(current, State::Stopped, std::memory_order_relaxed))
                return;

            // If failed, loop with new state
        }
    }

    void request_abort() noexcept {
        // Always go to aborted
        state.store(State::Aborted, std::memory_order_relaxed);
    }


    template<typename T>
    void set(std::atomic<T> Worker::* member, T value) noexcept {

        for (auto&& th : threads)
            (th->worker.get()->*member).store(value, std::memory_order_relaxed);
    }

    template<typename T>
    std::uint64_t sum(std::atomic<T> Worker::* member,
                      std::uint64_t            initialSum = 0) const noexcept {

        std::uint64_t sum = initialSum;

        for (auto&& th : threads)
            sum += (th->worker.get()->*member).load(std::memory_order_relaxed);

        return sum;
    }

   private:
    enum class State : std::uint8_t {
        Active,
        Research,
        Stopped,
        Aborted
    };

    std::vector<ThreadPtr> threads;
    std::vector<NumaIndex> threadBoundNumaNodes;
    StateListPtr           setupStates;

    std::atomic<State> state;
};

inline Threads::~Threads() noexcept { clear(); }

// Destroy any existing thread(s)
inline void Threads::clear() noexcept {
    if (empty())
        return;

    main_thread()->wait_finish();

    threads.clear();
    threadBoundNumaNodes.clear();
}

// Sets data to initial values
inline void Threads::init() noexcept {
    if (empty())
        return;

    for (auto&& th : threads)
        th->init();

    for (auto&& th : threads)
        th->wait_finish();

    main_manager()->init();
}

// Get pointer to the main thread
inline Thread* Threads::main_thread() const noexcept { return front().get(); }

// Get pointer to the main search manager
inline MainSearchManager* Threads::main_manager() const noexcept {
    return main_thread()->worker->main_manager();
}

// Start non-main threads
// Will be invoked by main thread after it has started searching
inline void Threads::start_search() const noexcept {

    for (auto&& th : threads)
        if (th != front())
            th->start_search();
}

// Wait for non-main threads
// Will be invoked by main thread after it has finished searching
inline void Threads::wait_finish() const noexcept {

    for (auto&& th : threads)
        if (th != front())
            th->wait_finish();
}

// Ensure that all threads have their network replicated
inline void Threads::ensure_network_replicated() const noexcept {

    for (auto&& th : threads)
        th->ensure_network_replicated();
}

}  // namespace DON

#endif  // #ifndef THREAD_H_INCLUDED
