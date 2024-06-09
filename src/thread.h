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
#include <condition_variable>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "numa.h"
#include "search.h"
#include "thread_win32_osx.h"
#include "types.h"

namespace DON {

class Position;
class Options;

// Sometimes we don't want to actually bind the threads, but the recipent still
// needs to think it runs on *some* NUMA node, such that it can access structures
// that rely on NUMA node knowledge. This class encapsulates this optional process
// such that the recipent does not need to know whether the binding happened or not.
class OptionalThreadToNumaNodeBinder {
   public:
    OptionalThreadToNumaNodeBinder(NumaIndex nId) :
        numaConfig(nullptr),
        numaId(nId) {}

    OptionalThreadToNumaNodeBinder(const NumaConfig& nConfig, NumaIndex nId) :
        numaConfig(&nConfig),
        numaId(nId) {}

    NumaReplicatedAccessToken operator()() const {
        return numaConfig != nullptr ? numaConfig->bind_current_thread_to_numa_node(numaId)
                                     : NumaReplicatedAccessToken(numaId);
    }

   private:
    const NumaConfig* numaConfig;
    NumaIndex         numaId;
};

// Abstraction of a thread. It contains a pointer to the worker and a native thread.
// After construction, the native thread is started with idle_func()
// waiting for a signal to start searching.
// When the signal is received, the thread starts searching and when
// the search is finished, it goes back to idle_func() waiting for a new signal.
class Thread final {
   public:
    Thread(const Thread&)            = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(std::uint16_t                  id,
           const Search::SharedState&     sharedState,
           Search::ISearchManagerPtr      searchManager,
           OptionalThreadToNumaNodeBinder binder) noexcept;
    virtual ~Thread() noexcept;

    std::uint16_t id() const noexcept { return idx; }

    void idle_func() noexcept;

    void wait_finish() noexcept;

    void init() noexcept;
    void start_search() noexcept;

    void run_custom_job(std::function<void()> func) noexcept;

   private:
    // Set before starting nativeThread
    bool dead = false, busy = true;

    const std::uint16_t idx;
    const std::uint16_t threadCount;

   public:
    std::unique_ptr<Search::Worker> worker;

   private:
    std::mutex                mutex;
    std::condition_variable   condVar;
    NativeThread              nativeThread;
    std::function<void()>     jobFunc;
    NumaReplicatedAccessToken numaAccessToken;
};

// Blocks on the condition variable
// until the thread has finished job.
inline void Thread::wait_finish() noexcept {
    std::unique_lock uniqueLock(mutex);
    condVar.wait(uniqueLock, [this] { return !busy; });
    //uniqueLock.unlock();
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


// ThreadPool struct handles all the threads-related stuff like init, starting,
// parking and, most importantly, launching a thread.
// All the access to threads is done through this class.
class ThreadPool final {

   public:
    ThreadPool()                  = default;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)      = delete;

    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    ~ThreadPool() noexcept;

    void clear() noexcept;
    void set(const NumaConfig&            numaConfig,
             Search::SharedState          sharedState,
             const Search::UpdateContext& updateContext) noexcept;
    void init() noexcept;

    Thread*                    main_thread() const noexcept;
    Thread*                    best_thread() const noexcept;
    Search::MainSearchManager* main_manager() const noexcept;

    std::uint64_t nodes() const noexcept;
    std::uint64_t tbHits() const noexcept;

    void start(Position&             pos,
               StateListPtr&         states,
               const Search::Limits& limits,
               const Options&        options) noexcept;

    void start_search() const noexcept;
    void wait_finish() const noexcept;

    void run_on_thread(std::uint16_t threadId, std::function<void()> func);
    void wait_on_thread(std::uint16_t threadId);

    std::vector<std::size_t> get_bound_thread_counts() const noexcept;

    auto begin() noexcept { return threads.begin(); }
    auto end() noexcept { return threads.end(); }

    auto begin() const noexcept { return threads.begin(); }
    auto end() const noexcept { return threads.end(); }

    auto& front() const noexcept { return threads.front(); }

    auto size() const noexcept { return threads.size(); }
    auto empty() const noexcept { return threads.empty(); }

    std::atomic_bool stop, abort, research;

   private:
    // template<typename T>
    // void set(std::atomic<T> Search::Worker::*member, T value = T()) const noexcept {
    //    for (auto&& th : threads)
    //        th->worker.get()->*member = value;
    // }

    template<typename T>
    std::uint64_t accumulate(std::atomic<T> Search::Worker::*member,
                             std::uint64_t                   sum = T()) const noexcept {
        for (auto&& th : threads)
            sum += (th->worker.get()->*member).load(std::memory_order_relaxed);
        return sum;
    }

    std::vector<std::unique_ptr<Thread>> threads;
    std::vector<NumaIndex>               boundThreadToNumaNode;
    StateListPtr                         setupStates;
};

inline ThreadPool::~ThreadPool() noexcept { clear(); }

// Destroy any existing thread(s)
inline void ThreadPool::clear() noexcept {
    if (empty())
        return;

    main_thread()->wait_finish();

    threads.clear();
    boundThreadToNumaNode.clear();
}

// Sets threadPool data to initial values
inline void ThreadPool::init() noexcept {
    if (empty())
        return;

    for (auto&& th : threads)
        th->init();
    for (auto&& th : threads)
        th->wait_finish();

    main_manager()->init(size());
}

inline Thread* ThreadPool::main_thread() const noexcept { return front().get(); }

inline Search::MainSearchManager* ThreadPool::main_manager() const noexcept {
    return main_thread()->worker->main_manager();
}

inline std::uint64_t ThreadPool::nodes() const noexcept {
    return accumulate(&Search::Worker::nodes);
}

inline std::uint64_t ThreadPool::tbHits() const noexcept {
    return accumulate(&Search::Worker::tbHits);
}

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

}  // namespace DON

#endif  // #ifndef THREAD_H_INCLUDED
