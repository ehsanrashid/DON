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
    Thread(const Search::SharedState& sharedState,
           Search::ISearchManagerPtr  searchManager,
           std::uint16_t              id) noexcept;
    virtual ~Thread() noexcept;

    void idle_func() noexcept;
    void wake_up() noexcept;
    void wait_idle() noexcept;

    std::uint16_t id() const noexcept { return idx; }

    std::unique_ptr<Search::Worker> worker;

   private:
    // Set before starting nativeThread
    bool dead = false, busy = true;

    const std::uint16_t idx;
    const std::uint16_t threadCount;

    std::mutex              mutex;
    std::condition_variable condVar;
    NativeThread            nativeThread;
};

// ThreadPool struct handles all the threads-related stuff like init, starting,
// parking and, most importantly, launching a thread.
// All the access to threads is done through this class.
class ThreadPool final {

   public:
    ThreadPool()                             = default;
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ~ThreadPool() noexcept;

    void destroy() noexcept;
    void clear() noexcept;
    void set(Search::SharedState sharedState, const Search::UpdateContext& updateContext) noexcept;

    Thread*                    main_thread() const noexcept;
    Thread*                    best_thread() const noexcept;
    Search::MainSearchManager* main_manager() const noexcept;

    std::uint64_t nodes() const noexcept;
    std::uint64_t tbHits() const noexcept;

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
    //template<typename T>
    //void set(std::atomic<T> Search::Worker::*member, T value) const noexcept {
    //
    //    for (Thread* th : threads)
    //        th->worker.get()->*member = value;
    //}

    template<typename T>
    T accumulate(std::atomic<T> Search::Worker::*member, T sum = {}) const noexcept {

        for (const Thread* th : threads)
            sum += (th->worker.get()->*member).load(std::memory_order_relaxed);
        return sum;
    }

    std::vector<Thread*> threads;
    StateListPtr         setupStates;
};

}  // namespace DON

#endif  // #ifndef THREAD_H_INCLUDED
