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

#include <utility>  // forward<>, exchange(), swap()

// MSVC-compatible toolchains use std::thread because pthreads is not provided by default.
// All other platforms use pthreads.
#if !defined(_MSC_VER)
    #define USE_PTHREAD
#endif

#if defined(USE_PTHREAD)
    #include <pthread.h>    // pthread API
    #include <cstdlib>      // exit(), EXIT_FAILURE
    #include <iostream>     // cerr
    #include <memory>       // unique_ptr<>, make_unique()
    #include <tuple>        // tuple<>, make_tuple(), apply()
    #include <type_traits>  // decay_t<>
#else
    #include <thread>
#endif

#include "misc.h"

namespace DON {

struct ThreadOptions final {
   public:
    explicit constexpr ThreadOptions(bool sStackSize = false, bool sGuardSize = false) noexcept :
        useStackSize(sStackSize),
        useGuardSize(sGuardSize) {}

    bool useStackSize = false;
    bool useGuardSize = false;
};

#if defined(USE_PTHREAD)

// On macOS, threads other than the main thread have a default stack size of
// 512KB, which is too small for deep searches.
// Allow the stack size to be increased to 8MB when requested.
class NativeThread final {
   private:
    struct BaseCallable {  // Type-erased callable interface
        virtual ~BaseCallable() = default;

        virtual void invoke() noexcept = 0;
    };

    template<typename Function, typename... Args>
    struct Callable final: public BaseCallable {
        Callable(Function&& func, Args&&... args) :
            func_(std::forward<Function>(func)),
            args_(std::make_tuple(std::forward<Args>(args)...)) {}

        void invoke() noexcept override { std::apply(func_, args_); }

       private:
        std::decay_t<Function>            func_;
        std::tuple<std::decay_t<Args>...> args_;
    };

   public:
    // Default thread is not joinable
    NativeThread() noexcept = default;

    NativeThread(const NativeThread&) noexcept            = delete;
    NativeThread& operator=(const NativeThread&) noexcept = delete;

    NativeThread(NativeThread&& nativeThread) noexcept { move(std::move(nativeThread)); }
    NativeThread& operator=(NativeThread&& nativeThread) noexcept {
        if (this == &nativeThread)
            return *this;

        if (!join())
            return *this;

        move(std::move(nativeThread));

        return *this;
    }

    template<typename Function, typename... Args>
    NativeThread(const ThreadOptions& thOptions, Function&& func, Args&&... args) noexcept {
        using CallableFunc = Callable<Function, Args...>;

        auto callablePtr = std::make_unique<CallableFunc>(  //
          std::forward<Function>(func), std::forward<Args>(args)...);

        pthread_attr_t threadAttr;

        if (::pthread_attr_init(&threadAttr) != 0)
        {
            //DEBUG_LOG("::pthread_attr_init() failed.");
            return;
        }

        const auto destroy_thread_attr = [&threadAttr]() noexcept {
            if (::pthread_attr_destroy(&threadAttr) != 0)
            {
                //DEBUG_LOG("::pthread_attr_destroy() failed.");
            }
        };

        if (thOptions.useStackSize && ::pthread_attr_setstacksize(&threadAttr, StackSize) != 0)
        {
            //DEBUG_LOG("::pthread_attr_setstacksize() failed.");
            destroy_thread_attr();
            return;
        }

    #if !defined(__MINGW32__)
        if (thOptions.useGuardSize && ::pthread_attr_setguardsize(&threadAttr, GuardSize) != 0)
        {
            //DEBUG_LOG("::pthread_attr_setguardsize() failed.");
            destroy_thread_attr();
            return;
        }
    #endif

        const auto start_routine = [](void* ptr) noexcept -> void* {
            auto callable = std::unique_ptr<BaseCallable>(static_cast<BaseCallable*>(ptr));

            callable->invoke();

            return nullptr;
        };

        if (::pthread_create(&thread_, &threadAttr, start_routine, callablePtr.get()) != 0)
        {
            //DEBUG_LOG("::pthread_create() failed.");
        }
        else
        {
            // Mark the thread as joinable.
            joinable_ = true;
            // Transfer ownership to the new thread.
            callablePtr.release();
        }

        destroy_thread_attr();
    }

    template<typename Function, typename... Args>
    NativeThread(Function&& func, Args&&... args) noexcept :
        NativeThread{ThreadOptions{}, std::forward<Function>(func), std::forward<Args>(args)...} {}

    // RAII: join on destruction if thread is joinable
    ~NativeThread() noexcept {
        [[maybe_unused]] const bool joined = join();
        assert(joined);
    }

    bool joinable() const noexcept { return joinable_; }

    bool join() noexcept {
        if (!joinable())
            return true;

        if (::pthread_join(thread_, nullptr) != 0)
        {
            //DEBUG_LOG("::pthread_join() failed.");
            return false;
        }

        joinable_ = false;
        return true;
    }

   private:
    void move(NativeThread&& nativeThread) noexcept {
        thread_   = nativeThread.thread_;
        joinable_ = std::exchange(nativeThread.joinable_, false);
    }

    void swap(NativeThread& nativeThread) noexcept {
        std::swap(thread_, nativeThread.thread_);
        std::swap(joinable_, nativeThread.joinable_);
    }

    static constexpr usize StackSize = 8 * MB;
    #if !defined(__MINGW32__)
    static constexpr usize GuardSize = 4 * KB;
    #endif

    pthread_t thread_{};
    bool      joinable_ = false;
};

#else

using NativeThread = std::thread;

#endif

template<typename Function, typename... Args>
NativeThread create_native_thread_with_options(
#if defined(USE_PTHREAD)
  const ThreadOptions& thOptions,
#else
  const ThreadOptions&,
#endif
  Function&& func,
  Args&&... args) noexcept {
    return
#if defined(USE_PTHREAD)
      NativeThread(thOptions, std::forward<Function>(func), std::forward<Args>(args)...)
#else
      // TODO: implement fallible thread creation on MSVC
      NativeThread(std::forward<Function>(func), std::forward<Args>(args)...)
#endif
        ;
}

template<typename Function, typename... Args>
NativeThread create_native_thread(Function&& func, Args&&... args) noexcept {
    return create_native_thread_with_options(ThreadOptions{}, std::forward<Function>(func),
                                             std::forward<Args>(args)...);
}

}  // namespace DON

#endif  // NATIVE_THREAD_H_INCLUDED
