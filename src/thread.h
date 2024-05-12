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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "position.h"
#include "search.h"
#include "thread_win32_osx.h"
#include "types.h"

namespace DON {

class ThreadPool;
class OptionsMap;

// Abstraction of a thread. It contains a pointer to the worker and a native thread.
// After construction, the native thread is started with idle_func()
// waiting for a signal to start searching.
// When the signal is received, the thread starts searching and when
// the search is finished, it goes back to idle_func() waiting for a new signal.
class Thread final {
   public:
    Thread(const Thread&)            = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(std::uint16_t              id,
           const Search::SharedState& sharedState,
           Search::ISearchManagerPtr  searchManager) noexcept;
    virtual ~Thread() noexcept;

    void idle_func() noexcept;
    void wake_up() noexcept;
    void wait_idle() noexcept;

    std::uint16_t id() const noexcept { return idx; }

   private:
    // Set before starting nativeThread
    bool dead = false, busy = true;

    const std::uint16_t idx;
    const std::uint16_t threadCount;

   public:
    std::unique_ptr<Search::Worker> worker;

   private:
    std::mutex              mutex;
    std::condition_variable condVar;
    NativeThread            nativeThread;

    friend class ThreadPool;
};

// Wakes up the thread that will start the search
inline void Thread::wake_up() noexcept {
    {
        std::lock_guard lockGuard(mutex);
        busy = true;
    }                      // Unlock before notifying saves a few CPU-cycles
    condVar.notify_one();  // Wake up the thread in idle_func()
}

// Blocks on the condition variable
// until the thread has finished searching.
inline void Thread::wait_idle() noexcept {
    std::unique_lock uniqueLock(mutex);
    condVar.wait(uniqueLock, [this] { return !busy; });
    //uniqueLock.unlock();
}

// ThreadPool struct handles all the threads-related stuff like init, starting,
// parking and, most importantly, launching a thread.
// All the access to threads is done through this class.
class ThreadPool final {

   public:
    ThreadPool()                             = default;
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ~ThreadPool() noexcept;

    // static Search::Worker* worker(const Thread* th) noexcept;

    void destroy() noexcept;
    void clear() noexcept;
    void set(Search::SharedState sharedState, const Search::UpdateContext& updateContext) noexcept;

    Thread*                    main_thread() const noexcept;
    Thread*                    best_thread() const noexcept;
    Search::MainSearchManager* main_manager() const noexcept;

    std::uint64_t nodes() const noexcept;
    std::uint64_t tbHits() const noexcept;
    // std::uint32_t bestMoveChanges() const noexcept;

    void start(Position&             pos,
               StateListPtr&         states,
               const Search::Limits& limits,
               const OptionsMap&     options) noexcept;

    void start_search() const noexcept;
    void wait_finish() const noexcept;

    auto cbegin() const noexcept { return threads.cbegin(); }
    auto cend() const noexcept { return threads.cend(); }

    auto begin() noexcept { return threads.begin(); }
    auto end() noexcept { return threads.end(); }

    auto size() const noexcept { return threads.size(); }
    auto empty() const noexcept { return threads.empty(); }

    std::atomic_bool stop, abort, research;

   private:
    // template<typename T>
    // void set(std::atomic<T> Search::Worker::*member, T value) const noexcept {
    //
    //    for (const Thread* th : threads)
    //        th->worker.get()->*member = value;
    // }

    template<typename T>
    T accumulate(std::atomic<T> Search::Worker::*member, T sum = {}) const noexcept {

        for (const Thread* th : threads)
            sum += (th->worker.get()->*member).load(std::memory_order_relaxed);
        return sum;
    }

    std::vector<Thread*> threads;
    StateListPtr         setupStates;
};

inline ThreadPool::~ThreadPool() noexcept { destroy(); }

// inline Search::Worker* ThreadPool::worker(const Thread* th) noexcept { return th->worker.get(); }

// Destroy any existing thread(s)
inline void ThreadPool::destroy() noexcept {
    if (!empty())
    {
        main_thread()->wait_idle();

        while (!empty())
            delete threads.back(), threads.pop_back();
    }
}

// Sets threadPool data to initial values
inline void ThreadPool::clear() noexcept {
    if (empty())
        return;

    for (Thread* th : threads)
        th->worker->clear();

    main_manager()->clear(size());
}

inline Thread* ThreadPool::main_thread() const noexcept { return threads.front(); }

inline Search::MainSearchManager* ThreadPool::main_manager() const noexcept {
    return main_thread()->worker->main_manager();
}

inline std::uint64_t ThreadPool::nodes() const noexcept {
    return accumulate(&Search::Worker::nodes);
}

inline std::uint64_t ThreadPool::tbHits() const noexcept {
    return accumulate(&Search::Worker::tbHits);
}

// inline std::uint32_t ThreadPool::bestMoveChanges() const noexcept {
//     return accumulate(&Search::Worker::bestMoveChanges);
// }

// Start non-main threads
// Will be invoked by main thread after it has started searching
inline void ThreadPool::start_search() const noexcept {

    for (Thread* th : threads)
        if (th != main_thread())
            th->wake_up();
}

// Wait for non-main threads
inline void ThreadPool::wait_finish() const noexcept {

    for (Thread* th : threads)
        if (th != main_thread())
            th->wait_idle();
}

}  // namespace DON

#endif  // #ifndef THREAD_H_INCLUDED
