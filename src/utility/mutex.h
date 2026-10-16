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

#ifndef UTILITY_MUTEX_H_INCLUDED
#define UTILITY_MUTEX_H_INCLUDED

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "../utility/list.h"
#include "../utility/thread_attributes.h"
#include "../utility/types.h"

namespace DON {

// A lightweight mutex that actively spins while waiting for the lock to
// become available. Under sustained contention, periodically yields the
// thread or briefly sleeps to reduce CPU usage and excessive busy-waiting.
//
// Best suited for short, low-contention critical sections where the lock is
// expected to become available quickly. Avoid holding the lock while blocking,
// performing I/O, or executing lengthy operations. If the lock holder is
// descheduled or preempted, waiting threads may waste CPU time before yielding or sleeping.
//
// Most effective when contention is brief and the lock holder can make prompt progress,
// particularly with pinned workers running on isolated cores.
// It is not generally suitable for interrupt-service-routine (ISR) contexts
// because yielding and sleeping are not normally permitted there.
// ISR use requires platform-specific guarantees.
class LOCKABLE SpinMutex final {
   public:
    SpinMutex() noexcept                            = default;
    SpinMutex(const SpinMutex&) noexcept            = delete;
    SpinMutex& operator=(const SpinMutex&) noexcept = delete;
    SpinMutex(SpinMutex&&) noexcept                 = delete;
    SpinMutex& operator=(SpinMutex&&) noexcept      = delete;

    void lock() noexcept ACQUIRE_CAPABILITY();

    void unlock() noexcept RELEASE_CAPABILITY();

   private:
    static constexpr usize SpinsMax    = 512;
    static constexpr usize YieldsMax   = 8;
    static constexpr usize SleepTimeUs = 5;

    std::atomic<bool> locked{false};
};

// A shared mutex that gives priority to readers over waiting writers.
// New readers may acquire the mutex while a writer is waiting,
// which can cause writer starvation under continuous reader contention.
class LOCKABLE ReaderSharedMutex final {
   public:
    ReaderSharedMutex() noexcept                                    = default;
    ReaderSharedMutex(const ReaderSharedMutex&) noexcept            = delete;
    ReaderSharedMutex& operator=(const ReaderSharedMutex&) noexcept = delete;
    ReaderSharedMutex(ReaderSharedMutex&&) noexcept                 = delete;
    ReaderSharedMutex& operator=(ReaderSharedMutex&&) noexcept      = delete;

    void lock() noexcept ACQUIRE_CAPABILITY();

    bool try_lock() noexcept TRY_ACQUIRE_CAPABILITY(true);

    void unlock() noexcept RELEASE_CAPABILITY();

    void lock_shared() noexcept ACQUIRE_SHARED_CAPABILITY();

    bool try_lock_shared() noexcept TRY_ACQUIRE_SHARED_CAPABILITY(true);

    void unlock_shared() noexcept RELEASE_SHARED_CAPABILITY();

    [[nodiscard]] usize active_reader_count() const noexcept;

    [[nodiscard]] usize waiting_writer_count() const noexcept;

   private:
    [[nodiscard]] bool can_lock() const noexcept;

    [[nodiscard]] bool can_try_lock() const noexcept;

    [[nodiscard]] bool can_lock_shared() const noexcept;

    [[nodiscard]] bool can_try_lock_shared() const noexcept;

    mutable std::mutex      mutex;
    std::condition_variable readerCondVar;
    std::condition_variable writerCondVar;

    usize activeReaderCount  GUARDED_BY(mutex) = 0;
    usize waitingWriterCount GUARDED_BY(mutex) = 0;

    bool writerActive GUARDED_BY(mutex) = false;
};

// A shared mutex that gives priority to waiting writers over new readers.
// New readers wait while a writer is waiting, which can cause reader
// starvation under continuous writer contention.
class LOCKABLE WriterSharedMutex final {
   public:
    WriterSharedMutex() noexcept                                    = default;
    WriterSharedMutex(const WriterSharedMutex&) noexcept            = delete;
    WriterSharedMutex& operator=(const WriterSharedMutex&) noexcept = delete;
    WriterSharedMutex(WriterSharedMutex&&) noexcept                 = delete;
    WriterSharedMutex& operator=(WriterSharedMutex&&) noexcept      = delete;

    void lock() noexcept ACQUIRE_CAPABILITY();

    bool try_lock() noexcept TRY_ACQUIRE_CAPABILITY(true);

    void unlock() noexcept RELEASE_CAPABILITY();

    void lock_shared() noexcept ACQUIRE_SHARED_CAPABILITY();

    bool try_lock_shared() noexcept TRY_ACQUIRE_SHARED_CAPABILITY(true);

    void unlock_shared() noexcept RELEASE_SHARED_CAPABILITY();


    [[nodiscard]] usize active_reader_count() const noexcept;

    [[nodiscard]] usize waiting_writer_count() const noexcept;

   private:
    [[nodiscard]] bool can_lock() const noexcept;

    [[nodiscard]] bool can_try_lock() const noexcept;

    [[nodiscard]] bool can_lock_shared() const noexcept;

    [[nodiscard]] bool can_try_lock_shared() const noexcept;

    mutable std::mutex      mutex;
    std::condition_variable readerCondVar;
    std::condition_variable writerCondVar;

    usize activeReaderCount  GUARDED_BY(mutex) = 0;
    usize waitingWriterCount GUARDED_BY(mutex) = 0;

    bool writerActive GUARDED_BY(mutex) = false;
};

// A fair shared mutex that preserves FIFO order between readers and writers.
// Consecutive queued readers may acquire shared access together, but readers cannot bypass
// a waiting writer. try_lock_shared() fails if a writer is active or waiting.
class LOCKABLE FairSharedMutex final {
   private:
    // A node in the wait queue representing a waiting thread.
    struct Waiter final {
        explicit Waiter(bool writer_ = false) noexcept;

        bool is_writer() const noexcept;

        OwnedIntrusiveList<Waiter>::Node node;

       private:
        Waiter() noexcept = delete;

        const bool writer;
    };

   public:
    FairSharedMutex() noexcept                                  = default;
    FairSharedMutex(const FairSharedMutex&) noexcept            = delete;
    FairSharedMutex& operator=(const FairSharedMutex&) noexcept = delete;
    FairSharedMutex(FairSharedMutex&&) noexcept                 = delete;
    FairSharedMutex& operator=(FairSharedMutex&&) noexcept      = delete;

    void lock() noexcept ACQUIRE_CAPABILITY();

    bool try_lock() noexcept TRY_ACQUIRE_CAPABILITY(true);

    void unlock() noexcept RELEASE_CAPABILITY();

    void lock_shared() noexcept ACQUIRE_SHARED_CAPABILITY();

    bool try_lock_shared() noexcept TRY_ACQUIRE_SHARED_CAPABILITY(true);

    void unlock_shared() noexcept RELEASE_SHARED_CAPABILITY();

    [[nodiscard]] usize active_reader_count() const noexcept;

    [[nodiscard]] usize waiting_writer_count() const noexcept;

   private:
    [[nodiscard]] bool can_lock(const Waiter& writer) const noexcept;

    [[nodiscard]] bool can_try_lock() const noexcept;

    [[nodiscard]] bool can_lock_shared(const Waiter& reader) const noexcept;

    [[nodiscard]] bool can_try_lock_shared() const noexcept;

    bool has_writer_before(const Waiter* until = nullptr) const noexcept;

    mutable std::mutex      mutex;
    std::condition_variable commonCondVar;

    OwnedIntrusiveList<Waiter> waitList GUARDED_BY(mutex);

    usize activeReaderCount  GUARDED_BY(mutex) = 0;
    usize waitingWriterCount GUARDED_BY(mutex) = 0;

    bool writerActive GUARDED_BY(mutex) = false;
};

}  // namespace DON

#endif  // UTILITY_MUTEX_H_INCLUDED
