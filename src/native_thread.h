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

#ifndef NATIVE_THREAD_H_INCLUDED
#define NATIVE_THREAD_H_INCLUDED

#include <functional>
#include <utility>

// MSVC-compatible toolchains use std::thread because pthreads is not provided by default.
// All other platforms use pthreads.
#if !defined(_MSC_VER)
    #define USE_PTHREAD
#endif

#if defined(USE_PTHREAD)
    #include <pthread.h>

    #include "misc.h"
#else
    #include <thread>
#endif

namespace DON {

using JobFunc = std::function<void()>;

#if defined(USE_PTHREAD)

// On OSX threads other than the main-thread are created with a reduced stack
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
            std::unique_ptr<JobFunc> ptrFn(static_cast<JobFunc*>(ptr));

            // Call the function
            (*ptrFn)();

            // std::unique_ptr deletes the object when lambda exits
            return nullptr;
        };

        pthread_attr_t threadAttr;

        if (::pthread_attr_init(&threadAttr) != 0)
        {
            //DEBUG_LOG("::pthread_attr_init() failed to init thread attributes.");
            return;
        }

        if (::pthread_attr_setstacksize(&threadAttr, ThreadStackSize) != 0)
        {
            //DEBUG_LOG("::pthread_attr_setstacksize() failed to set thread stack size.");
        }

        // Pass the raw pointer to pthread_create
        // pthread_create takes ownership of jobFuncPtr only on success
        if (::pthread_create(&thread, &threadAttr, start_routine, jobFuncPtr.get()) != 0)
        {
            //DEBUG_LOG("::pthread_create() failed to create thread.");
            // Thread creation failed: jobFuncPtr will be deleted automatically
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
        if (::pthread_attr_destroy(&threadAttr) != 0)
        {
            //DEBUG_LOG("::pthread_attr_destroy() failed to destroy thread attributes.");
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
            ::pthread_join(thread, nullptr);

            joined = true;
        }
    }

   private:
    static constexpr usize ThreadStackSize = 8 * MB;

    pthread_t thread{};
    bool      joined = true;
};

#else

using NativeThread = std::thread;

#endif

}  // namespace DON

#endif  // NATIVE_THREAD_H_INCLUDED
