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

// MSVC-compatible toolchains use std::thread because pthreads is not provided by default.
// All other platforms use pthreads.
#if !defined(_MSC_VER)
    #define USE_PTHREAD
#endif

#if defined(USE_PTHREAD)
    #include <pthread.h>
    #include <cstdlib>
    #include <cstring>
    #include <iostream>
    #include <utility>
    #include <tuple>
#else
    #include <thread>
#endif

#include "misc.h"

namespace DON {

struct NativeThreadOptions final {
   public:
    bool setStackSize = false;
};

using JobFunc = std::function<void()>;

#if defined(USE_PTHREAD)

// On OSX threads other than the main-thread are created with a reduced stack
// size of 512KB by default, this is too low for deep searches,
// which require somewhat more than 1MB stack, so adjust it to 8MB.
class NativeThread final {
   private:
    struct BaseCallable {  // Type-erased callable interface
        virtual ~BaseCallable() = default;

        virtual void run() noexcept = 0;
    };

    template<typename Function, typename... Args>
    struct Callable final: BaseCallable {
        Callable(Function&& func, Args&&... args) :
            func_(std::forward<Function>(func)),
            args_(std::make_tuple(std::forward<Args>(args)...)) {}

        void run() noexcept override { std::apply(func_, args_); }

       private:
        Function            func_;
        std::tuple<Args...> args_;
    };

   public:
    // Default thread is not joinable
    NativeThread() noexcept = default;

    NativeThread(const NativeThread&) noexcept            = delete;
    NativeThread& operator=(const NativeThread&) noexcept = delete;

    NativeThread(NativeThread&& nativeThread) noexcept :
        thread_(nativeThread.thread_),
        joined_(std::exchange(nativeThread.joined_, true)) {}
    NativeThread& operator=(NativeThread&& nativeThread) noexcept {
        if (this == &nativeThread)
            return *this;

        join();

        thread_ = nativeThread.thread_;
        joined_ = std::exchange(nativeThread.joined_, true);

        return *this;
    }

    template<typename Function, typename... Args>
    NativeThread(Function&& func, const NativeThreadOptions options, Args&&... args) noexcept {
        using ThreadCallable = Callable<std::decay_t<Function>, std::decay_t<Args>...>;

        auto threadCallable = std::make_unique<ThreadCallable>(std::forward<Function>(func),
                                                               std::forward<Args>(args)...);

        pthread_attr_t threadAttr;

        if (::pthread_attr_init(&threadAttr) != 0)
        {
            // DEBUG_LOG("::pthread_attr_init() failed.");
            return;
        }

        const auto destroyThreadAttr = [&threadAttr]() noexcept {
            if (::pthread_attr_destroy(&threadAttr) != 0)
            {
                // DEBUG_LOG("::pthread_attr_destroy() failed.");
            }
        };

        if (options.setStackSize && ::pthread_attr_setstacksize(&threadAttr, StackSize) != 0)
        {
            // DEBUG_LOG("::pthread_attr_setstacksize() failed.");
            destroyThreadAttr();
            return;
        }

        const auto start_routine = [](void* ptr) noexcept -> void* {
            auto callable = std::unique_ptr<BaseCallable>(static_cast<BaseCallable*>(ptr));

            callable->run();

            return nullptr;
        };

        if (::pthread_create(&thread_, &threadAttr, start_routine, threadCallable.get()) != 0)
        {
            // DEBUG_LOG("::pthread_create() failed.");
        }
        else
        {
            // Mark thread as now joinable, not joined yet.
            joined_ = false;

            // Transfer ownership to the new thread.
            threadCallable.release();
        }

        destroyThreadAttr();
    }

    // RAII: join on destruction if thread is joinable
    ~NativeThread() noexcept { join(); }

    bool joinable() const noexcept { return !joined_; }

    void join() noexcept {
        if (joinable())
        {
            ::pthread_join(thread_, nullptr);

            joined_ = true;
        }
    }

   private:
    static constexpr usize StackSize = 8 * MB;

    pthread_t thread_{};
    bool      joined_ = true;
};

#else

using NativeThread = std::thread;

#endif

template<class Function, class... Args>
NativeThread create_native_thread(Function&&                                 func,
                                  [[maybe_unused]] const NativeThreadOptions options,
                                  Args&&... args) noexcept {
    return
#if defined(USE_PTHREAD)
      NativeThread(std::forward<Function>(func), options, std::forward<Args>(args)...)
#else
      // TODO: implement fallible thread creation on MSVC
      NativeThread(std::forward<Function>(func), std::forward<Args>(args)...)
#endif
        ;
}

template<class Function, class... Args>
NativeThread create_native_thread(Function&& func, Args&&... args) noexcept {
    return create_native_thread(std::forward<Function>(func), NativeThreadOptions{},
                                std::forward<Args>(args)...);
}

}  // namespace DON

#endif  // NATIVE_THREAD_H_INCLUDED
