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

#ifndef THREAD_H_INCLUDED
#define THREAD_H_INCLUDED

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "numa.h"
#include "position.h"
#include "search.h"
#include "thread_win32_osx.h"

namespace DON {

class Options;

// Sometimes we don't want to actually bind the threads, but the recipient still
// needs to think it runs on *some* NUMA node, such that it can access structures
// that rely on NUMA node knowledge. This class encapsulates this optional process
// such that the recipient does not need to know whether the binding happened or not.
class OptionalThreadToNumaNodeBinder final {
   public:
    OptionalThreadToNumaNodeBinder(NumaIndex nId, const NumaConfig* nConfigPtr) noexcept :
        numaId(nId),
        numaConfigPtr(nConfigPtr) {}

    explicit OptionalThreadToNumaNodeBinder(NumaIndex nId) noexcept :
        OptionalThreadToNumaNodeBinder(nId, nullptr) {}

    NumaReplicatedAccessToken operator()() const noexcept {
        return numaConfigPtr != nullptr ? numaConfigPtr->bind_current_thread_to_numa_node(numaId)
                                        : NumaReplicatedAccessToken(numaId);
    }

   private:
    NumaIndex         numaId;
    const NumaConfig* numaConfigPtr;
};

using JobFunc   = std::function<void()>;
using WorkerPtr = std::unique_ptr<Worker>;

// Abstraction of a thread. It contains a pointer to the worker and a native thread.
// After construction, the native thread is started with idle_func()
// waiting for a signal to start searching.
// When the signal is received, the thread starts searching and when
// the search is finished, it goes back to idle_func() waiting for a new signal.
class Thread final {
   public:
    Thread(std::size_t                           id,
           const SharedState&                    sharedState,
           ISearchManagerPtr                     searchManager,
           const OptionalThreadToNumaNodeBinder& nodeBinder) noexcept;
    virtual ~Thread() noexcept;

    std::size_t id() const noexcept { return idx; }

    void ensure_network_replicated() const noexcept;

    void wait_finish() noexcept;

    void run_custom_job(JobFunc func) noexcept;

    void idle_func() noexcept;

    void init() noexcept;

    void start_search() noexcept;

   private:
    // Set before starting nativeThread
    bool dead = false, busy = true;

    const std::size_t       idx;
    const std::size_t       threadCount;
    std::mutex              mutex;
    std::condition_variable condVar;
    NativeThread            nativeThread;
    JobFunc                 jobFunc;

   public:
    WorkerPtr worker;
};

// Blocks on the condition variable
// until the thread has finished job.
inline void Thread::wait_finish() noexcept {
    std::unique_lock uniqueLock(mutex);
    condVar.wait(uniqueLock, [this] { return !busy; });
}

// Launching a function in the thread
inline void Thread::run_custom_job(JobFunc func) noexcept {
    {
        std::unique_lock uniqueLock(mutex);
        condVar.wait(uniqueLock, [this] { return !busy; });
        jobFunc = std::move(func);
        busy    = true;
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

using ThreadPtr = std::unique_ptr<Thread>;

// ThreadPool struct handles all the threads-related stuff like init, starting,
// parking and, most importantly, launching a thread.
// All the access to threads is done through this class.
class ThreadPool final {
   public:
    ThreadPool() noexcept                             = default;
    ThreadPool(const ThreadPool&) noexcept            = delete;
    ThreadPool(ThreadPool&&) noexcept                 = delete;
    ThreadPool& operator=(const ThreadPool&) noexcept = delete;
    ThreadPool& operator=(ThreadPool&&) noexcept      = delete;
    ~ThreadPool() noexcept;

    auto begin() const noexcept { return threads.begin(); }
    auto end() const noexcept { return threads.end(); }
    auto begin() noexcept { return threads.begin(); }
    auto end() noexcept { return threads.end(); }

    auto& front() const noexcept { return threads.front(); }

    auto size() const noexcept { return threads.size(); }
    auto empty() const noexcept { return threads.empty(); }

    void clear() noexcept;

    void set(const NumaConfig&    numaConfig,
             SharedState          sharedState,
             const UpdateContext& updateContext) noexcept;

    void init() noexcept;

    Thread*            main_thread() const noexcept;
    Thread*            best_thread() const noexcept;
    MainSearchManager* main_manager() const noexcept;

    auto nodes() const noexcept;
    auto tbHits() const noexcept;

    void start(Position&      pos,  //
               StateListPtr&  states,
               const Limit&   limit,
               const Options& options) noexcept;

    void start_search() const noexcept;
    void wait_finish() const noexcept;

    void ensure_network_replicated() const noexcept;

    void run_on_thread(std::size_t threadId, JobFunc func) noexcept;
    void wait_on_thread(std::size_t threadId) noexcept;

    std::vector<std::size_t> get_bound_thread_counts() const noexcept;

    std::atomic<bool> stop, abort, research;

   private:
    template<typename T>
    T accumulate(std::atomic<T> Worker::*member, T sum = T()) const noexcept {

        for (auto&& th : threads)
            sum += (th->worker.get()->*member).load(std::memory_order_relaxed);
        return sum;
    }

    std::vector<ThreadPtr> threads;
    std::vector<NumaIndex> numaNodeBoundThreadIds;
    StateListPtr           setupStates;
};

inline ThreadPool::~ThreadPool() noexcept { clear(); }

// Destroy any existing thread(s)
inline void ThreadPool::clear() noexcept {
    if (empty())
        return;

    main_thread()->wait_finish();

    threads.clear();
    numaNodeBoundThreadIds.clear();
}

// Sets threadPool data to initial values
inline void ThreadPool::init() noexcept {

    Search::init();

    if (empty())
        return;

    for (auto&& th : threads)
        th->init();
    for (auto&& th : threads)
        th->wait_finish();

    main_manager()->init();
}

inline Thread* ThreadPool::main_thread() const noexcept { return front().get(); }

inline MainSearchManager* ThreadPool::main_manager() const noexcept {
    return main_thread()->worker->main_manager();
}

inline auto ThreadPool::nodes() const noexcept { return accumulate(&Worker::nodes); }

inline auto ThreadPool::tbHits() const noexcept { return accumulate(&Worker::tbHits); }

// Start non-main threads
// Will be invoked by main thread after it has started searching
inline void ThreadPool::start_search() const noexcept {

    for (auto&& th : threads)
        if (th != front())
            th->start_search();
}

// Wait for non-main threads
inline void ThreadPool::wait_finish() const noexcept {

    for (auto&& th : threads)
        if (th != front())
            th->wait_finish();
}

inline void ThreadPool::ensure_network_replicated() const noexcept {

    for (auto&& th : threads)
        th->ensure_network_replicated();
}

}  // namespace DON

#endif  // #ifndef THREAD_H_INCLUDED
