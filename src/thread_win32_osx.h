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

#ifndef THREAD_WIN32_OSX_H_INCLUDED
#define THREAD_WIN32_OSX_H_INCLUDED

// On OSX threads other than the main thread are created with a reduced stack
// size of 512KB by default, this is too low for deep searches, which require
// somewhat more than 1MB stack, so adjust it to 8MB.

#if defined(__APPLE__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(USE_PTHREADS)

    #include <functional>
    #include <pthread.h>
    #include <utility>

namespace DON {

class NativeThread final {
   public:
    NativeThread() noexcept = delete;

    template<typename Function, typename... Args>
    NativeThread(Function&& func, Args&&... args) noexcept {
        using Func = std::function<void()>;
        auto* funcPtr =
          new Func(std::bind(std::forward<Function>(func), std::forward<Args>(args)...));

        const auto start_routine = [](void* ptr) noexcept -> void* {
            auto* fnPtr = reinterpret_cast<Func*>(ptr);
            // Call the function
            (*fnPtr)();
            delete fnPtr;
            return nullptr;
        };

        pthread_attr_t attr;
        pthread_attr_init(&attr);
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

}  // namespace DON

#else  // Default case: use STL classes

    #include <thread>

namespace DON {

using NativeThread = std::thread;

}  // namespace DON

#endif

#endif  // #ifndef THREAD_WIN32_OSX_H_INCLUDED
