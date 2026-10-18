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

#ifndef UTILITY_CALL_ONCE_H_INCLUDED
#define UTILITY_CALL_ONCE_H_INCLUDED

#include <atomic>
#include <mutex>    // once_flag, call_once
#include <utility>  // forward<>, move()

namespace DON {

// Thread-safe wrapper around std::call_once that tracks successful completion.
// Retries the callable if it throws and reports whether a call has succeeded.
struct CallOnce final {
   public:
    CallOnce() noexcept                           = default;
    CallOnce(const CallOnce&) noexcept            = delete;
    CallOnce& operator=(const CallOnce&) noexcept = delete;
    CallOnce(CallOnce&&) noexcept                 = delete;
    CallOnce& operator=(CallOnce&&) noexcept      = delete;

    // Invoke the callable once, retrying if it throws.
    // Concurrent calls execute the callable only once successfully.
    template<typename Func>
    void operator()(Func&& callFn) noexcept {
        std::call_once(onceFlag, [this, callFunc = std::forward<Func>(callFn)]() mutable {
            std::move(callFunc)();
            onceCalled.store(true, std::memory_order_release);
        });
    }

    // Check whether the callable has completed successfully.
    [[nodiscard]] bool once_called() const noexcept {
        return onceCalled.load(std::memory_order_acquire);
    }

   private:
    std::once_flag    onceFlag;
    std::atomic<bool> onceCalled{false};
};

}  // namespace DON

#endif  // UTILITY_CALL_ONCE_H_INCLUDED
