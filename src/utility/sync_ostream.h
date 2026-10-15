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

#ifndef UTILITY_SYNC_OSTREAM_H_INCLUDED
#define UTILITY_SYNC_OSTREAM_H_INCLUDED

#include <cassert>
#include <iostream>
#include <mutex>
#include <utility>

namespace DON {

// SyncOStream
//
// Provides RAII-based, thread-safe output synchronization for a std::ostream.
//
// Each SyncOStream acquires the mutex associated with the given ostream through
// OStreamMutexes and holds the lock for its lifetime.
//
// Key features:
//  - Thread-safe: synchronizes access to the associated ostream.
//  - RAII-based: acquires the mutex on construction and releases it on destruction.
//  - Move-constructible: transfers ownership of the lock.
//  - Non-copyable and non-move-assignable: prevents accidental lock ownership changes.
//  - Supports stream insertion operators and standard stream manipulators.
//  - Asserts on use of a moved-from SyncOStream in debug builds.
//
// Usage:
//   SyncOStream{std::cout} << "Thread-safe message " << value << std::endl;
//
// Notes:
//  - The lock is held for the lifetime of the SyncOStream object.
//  - Keep SyncOStream objects short-lived to minimize lock contention.
//  - OStreamMutexes ensures that the same ostream uses the same mutex.
class [[nodiscard]] SyncOStream final {
   public:
    explicit SyncOStream(std::ostream& osRef) noexcept;

    SyncOStream(const SyncOStream&) noexcept            = delete;
    SyncOStream& operator=(const SyncOStream&) noexcept = delete;
    // Move-constructible to transfer ownership of the lock.
    SyncOStream(SyncOStream&& syncOs) noexcept;
    // Move-assignment is deleted because the stream reference and lock ownership must remain associated.
    SyncOStream& operator=(SyncOStream&&) noexcept = delete;

    using IOSManip = std::ios& (*) (std::ios&);

    SyncOStream&  operator<<(IOSManip manip) &;
    SyncOStream&& operator<<(IOSManip manip) &&;

    using OStreamManip = std::ostream& (*) (std::ostream&);

    SyncOStream&  operator<<(OStreamManip manip) &;
    SyncOStream&& operator<<(OStreamManip manip) &&;

    template<typename T>
    SyncOStream& operator<<(T&& x) & {
        assert(os != nullptr && "Cannot use a moved-from SyncOStream");

        *os << std::forward<T>(x);
        return *this;
    }
    template<typename T>
    SyncOStream&& operator<<(T&& x) && {
        assert(os != nullptr && "Cannot use a moved-from SyncOStream");

        *os << std::forward<T>(x);
        return std::move(*this);
    }

   private:
    std::ostream*                os;
    std::unique_lock<std::mutex> uniqueLock;
};

[[nodiscard]] SyncOStream sync_os(std::ostream& os = std::cout) noexcept;

}  // namespace DON

#endif  // UTILITY_SYNC_OSTREAM_H_INCLUDED
