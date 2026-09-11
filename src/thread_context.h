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

#ifndef THREAD_CONTEXT_H_INCLUDED
#define THREAD_CONTEXT_H_INCLUDED

#include "misc.h"

namespace DON {

struct ThreadContext final {
   public:
    constexpr ThreadContext(u16 threadIdx, u16 threadCnt, u16 numaIdx, u16 numaThreadCnt) noexcept :
        threadId(threadIdx),
        threadCount(threadCnt),
        numaId(numaIdx),
        numaThreadCount(numaThreadCnt) {}

    [[nodiscard]] constexpr u16 thread_id() const noexcept { return threadId; }

    [[nodiscard]] constexpr bool is_main() const noexcept { return thread_id() == 0; }

    [[nodiscard]] constexpr u16 thread_count() const noexcept { return threadCount; }

    [[nodiscard]] constexpr u16 numa_id() const noexcept { return numaId; }

    [[nodiscard]] constexpr u16 numa_thread_count() const noexcept { return numaThreadCount; }

   private:
    const u16 threadId;
    const u16 threadCount;
    const u16 numaId;
    const u16 numaThreadCount;
};

}  // namespace DON

#endif  // THREAD_CONTEXT_H_INCLUDED
