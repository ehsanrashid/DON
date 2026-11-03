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

#ifndef UTILITY_TRACKER_H_INCLUDED
#define UTILITY_TRACKER_H_INCLUDED

#include <mutex>          // lock_guard<>
#include <optional>       // optional<>, nullopt
#include <shared_mutex>   // shared_lock<>, shared_mutex<>
#include <unordered_map>  // unordered_map<>
#include <utility>        // forward<>

#include "../utility/types.h"

namespace DON {

// AllocationTracker: thread-safe allocation and freeing, with allocation size tracking.
class AllocationTracker final {
   public:
    using AllocationMap = std::unordered_map<void*, usize>;

    AllocationTracker() noexcept                                    = default;
    AllocationTracker(const AllocationTracker&) noexcept            = delete;
    AllocationTracker& operator=(const AllocationTracker&) noexcept = delete;
    AllocationTracker(AllocationTracker&&) noexcept                 = delete;
    AllocationTracker& operator=(AllocationTracker&&) noexcept      = delete;
    ~AllocationTracker() noexcept                                   = default;

    template<typename AllocFunc>
    [[nodiscard]] void* alloc(const usize allocSize, AllocFunc&& allocFn) noexcept {
        void* const mem = std::forward<AllocFunc>(allocFn)(allocSize);

        if (mem != nullptr)
        {
            std::lock_guard lockGuard{mutex};

            //try
            //{
            allocationMap.emplace(mem, allocSize);
            //} catch (...)
            //{
            //    // Allocation succeeded, but tracking failed.
            //    // The caller still owns the allocated memory.
            //}
        }

        return mem;
    }

    template<typename FreeFunc>
    [[nodiscard]] bool free(void* const mem, FreeFunc&& freeFn) noexcept {
        std::lock_guard lockGuard{mutex};

        if (const auto itr = allocationMap.find(mem); itr != allocationMap.end())
        {
            const usize allocSize = itr->second;

            if (!std::forward<FreeFunc>(freeFn)(mem, allocSize))
            {
                //std::exit(EXIT_FAILURE);
                return false;
            }

            allocationMap.erase(itr);
            return true;
        }

        return false;
    }

    [[nodiscard]] usize size() const noexcept {
        std::shared_lock sharedLock{mutex};

        return allocationMap.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::shared_lock sharedLock{mutex};

        return allocationMap.empty();
    }

    [[nodiscard]] std::optional<usize> find(void* const mem) const noexcept {
        std::shared_lock sharedLock{mutex};

        if (const auto itr = allocationMap.find(mem); itr != allocationMap.end())
            return itr->second;

        return std::nullopt;
    }

   private:
    mutable std::shared_mutex mutex;
    AllocationMap             allocationMap;
};

}  // namespace DON

#endif  // UTILITY_TRACKER_H_INCLUDED
