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

#ifndef UTILITY_ACCESS_H_INCLUDED
#define UTILITY_ACCESS_H_INCLUDED

#include <mutex>         // unique_lock<>, mutex
#include <shared_mutex>  // shared_lock<>, shared_mutex<>
#include <utility>       // move()

namespace DON {

template<typename Value, typename Mutex>
class ReadAccess final {
   public:
    ReadAccess(const Value& valueRef, Mutex& mutex) :
        value(valueRef),
        sharedLock(mutex) {}

    ReadAccess(const Value& valueRef, std::shared_lock<Mutex>&& sharedLockRef) :
        value(valueRef),
        sharedLock(std::move(sharedLockRef)) {}

    ReadAccess(const ReadAccess&) noexcept            = delete;
    ReadAccess& operator=(const ReadAccess&) noexcept = delete;
    ReadAccess(ReadAccess&&) noexcept                 = delete;
    ReadAccess& operator=(ReadAccess&&) noexcept      = delete;

    template<typename T = Value, typename = typename T::const_iterator>
    [[nodiscard]] auto begin() const -> typename T::const_iterator {
        return value.begin();
    }

    template<typename T = Value, typename = typename T::const_iterator>
    [[nodiscard]] auto end() const -> typename T::const_iterator {
        return value.end();
    }

    [[nodiscard]] const Value& get() const noexcept { return value; }

    [[nodiscard]] const Value* operator->() const noexcept { return &value; }

    [[nodiscard]] const Value& operator*() const noexcept { return value; }

   private:
    const Value&            value;
    std::shared_lock<Mutex> sharedLock;
};

template<typename Value, typename Mutex>
class WriteAccess final {
   public:
    WriteAccess(Value& valueRef, Mutex& mutex) :
        value(valueRef),
        uniqueLock(mutex) {}

    WriteAccess(Value& valueRef, std::unique_lock<Mutex>&& uniqueLockRef) :
        value(valueRef),
        uniqueLock(std::move(uniqueLockRef)) {}

    WriteAccess(const WriteAccess&) noexcept            = delete;
    WriteAccess& operator=(const WriteAccess&) noexcept = delete;
    WriteAccess(WriteAccess&&) noexcept                 = delete;
    WriteAccess& operator=(WriteAccess&&) noexcept      = delete;

    template<typename T = Value, typename = typename T::iterator>
    [[nodiscard]] auto begin() -> typename T::iterator {
        return value.begin();
    }

    template<typename T = Value, typename = typename T::iterator>
    [[nodiscard]] auto end() -> typename T::iterator {
        return value.end();
    }

    [[nodiscard]] Value& get() const noexcept { return value; }

    [[nodiscard]] Value* operator->() const noexcept { return &value; }

    [[nodiscard]] Value& operator*() const noexcept { return value; }

   private:
    Value&                  value;
    std::unique_lock<Mutex> uniqueLock;
};

}  // namespace DON

#endif  // UTILITY_ACCESS_H_INCLUDED
