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

#ifndef MUTEX_H_INCLUDED
#define MUTEX_H_INCLUDED

#include <atomic>

#include "misc.h"

#if defined(__i386__) || defined(_M_IX86)
    #define X86
    #define X86_32
#elif defined(__x86_64__) || defined(_M_X64)
    #define X86
    #define X86_64
#endif

#if defined(X86)
    #include <immintrin.h>
#endif

namespace DON {

inline void spin_pause() noexcept {
#if defined(X86)
    #if defined(_MSC_VER)
    //__asm {}
    _mm_pause();
    #else
    _mm_pause();
    #endif
#elif defined(__aarch64__) || defined(__arm__)
    #if defined(__clang__) || defined(__GNUC__)
    __asm__ __volatile__("yield");
    #endif
#else
        // Portable fallback: no architecture-specific pause instruction.
        //asm volatile("");
#endif
}

// A lightweight spin mutex that busy-waits until the lock becomes available.
class SpinMutex final {
   public:
    void lock() noexcept {
        usize spins = 0;
        bool  expected;
        do
        {
            while (locked.load(std::memory_order_relaxed))
            {
                spin_pause();

                // Yield occasionally to reduce CPU usage under contention.
                if (++spins == 512)
                {
                    spins = 0;
                    std::this_thread::yield();
                }
            }

            expected = false;
        } while (!locked.compare_exchange_weak(expected, true, std::memory_order_acquire,
                                               std::memory_order_relaxed));
    }

    void unlock() noexcept { locked.store(false, std::memory_order_release); }

   private:
    std::atomic<bool> locked{false};
};

}  // namespace DON

#endif  // MUTEX_H_INCLUDED
