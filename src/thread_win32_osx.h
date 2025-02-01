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

#include <thread>

// On OSX threads other than the main thread are created with a reduced stack
// size of 512KB by default, this is too low for deep searches, which require
// somewhat more than 1MB stack, so adjust it to THREAD_STACK_SIZE.
// The implementation calls pthread_create() with the stack size parameter
// equal to the Linux 8MB default, on platforms that support it.

#if defined(__APPLE__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(USE_PTHREADS)

    #include <pthread.h>
    #include <functional>

namespace DON {

class NativeThread final {
   public:
    template<class Function, class... Args>
    explicit NativeThread(Function&& func, Args&&... args) noexcept {

        pthread_attr_t attribute;
        pthread_attr_init(&attribute);
        pthread_attr_setstacksize(&attribute, THREAD_STACK_SIZE);

        using Func = std::function<void()>;

        const auto start_routine = [](void* ptrFunc) noexcept -> void* {
            auto* f = reinterpret_cast<Func*>(ptrFunc);
            // Call the function
            (*f)();
            delete f;
            return nullptr;
        };

        auto* ptrFunc =
          new Func(std::bind(std::forward<Function>(func), std::forward<Args>(args)...));

        pthread_create(&thread, &attribute, start_routine, ptrFunc);
    }

    void join() const noexcept { pthread_join(thread, nullptr); }

   private:
    static constexpr std::size_t THREAD_STACK_SIZE = 8 * 1024 * 1024;

    pthread_t thread;
};

}  // namespace DON

#else  // Default case: use STL classes

namespace DON {

using NativeThread = std::thread;

}  // namespace DON

#endif

#endif  // #ifndef THREAD_WIN32_OSX_H_INCLUDED
