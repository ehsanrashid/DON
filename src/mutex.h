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
#include <condition_variable>
#include <mutex>

#include "misc.h"

namespace DON {

// Architecture-specific processor hint for spin-wait loops.
// May reduce resource contention, improve progress of sibling hardware threads, or
// reduce power consumption, depending on the architecture and implementation.
void cpu_relax() noexcept;

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
#if !defined(__INTELLISENSE__)
class CAPABILITY("mutex") SpinMutex
#else
class SpinMutex
#endif
  final {
   public:
    SpinMutex() noexcept = default;

    void lock() noexcept
#if !defined(__INTELLISENSE__)
      ACQUIRE()
#endif
        ;

    void unlock() noexcept
#if !defined(__INTELLISENSE__)
      RELEASE()
#endif
        ;

   private:
    SpinMutex(const SpinMutex&) noexcept            = delete;
    SpinMutex& operator=(const SpinMutex&) noexcept = delete;
    SpinMutex(SpinMutex&&) noexcept                 = delete;
    SpinMutex& operator=(SpinMutex&&) noexcept      = delete;

    std::atomic<bool> locked{false};
};

// A shared mutex that gives priority to readers over waiting writers.
// New readers may acquire the mutex while a writer is waiting,
// which can cause writer starvation under continuous reader contention.
#if !defined(__INTELLISENSE__)
class CAPABILITY("mutex") ReaderSharedMutex
#else
class ReaderSharedMutex
#endif
  final {
   public:
    void lock() noexcept
#if !defined(__INTELLISENSE__)
      ACQUIRE()
#endif
        ;

    bool try_lock() noexcept
#if !defined(__INTELLISENSE__)
      TRY_ACQUIRE(true)
#endif
        ;

    void unlock() noexcept
#if !defined(__INTELLISENSE__)
      RELEASE()
#endif
        ;

    void lock_shared() noexcept
#if !defined(__INTELLISENSE__)
      ACQUIRE_SHARED()
#endif
        ;

    bool try_lock_shared() noexcept
#if !defined(__INTELLISENSE__)
      TRY_ACQUIRE_SHARED(true)
#endif
        ;

    void unlock_shared() noexcept
#if !defined(__INTELLISENSE__)
      RELEASE_SHARED()
#endif
        ;

    [[nodiscard]] usize active_reader_count() const noexcept;

    [[nodiscard]] usize waiting_writer_count() const noexcept;

   private:
    ReaderSharedMutex(const ReaderSharedMutex&) noexcept            = delete;
    ReaderSharedMutex& operator=(const ReaderSharedMutex&) noexcept = delete;
    ReaderSharedMutex(ReaderSharedMutex&&) noexcept                 = delete;
    ReaderSharedMutex& operator=(ReaderSharedMutex&&) noexcept      = delete;

    mutable std::mutex      mutex;
    std::condition_variable readerCondVar;
    std::condition_variable writerCondVar;

    usize activeReaderCount
#if !defined(__INTELLISENSE__)
      GUARDED_BY(mutex)
#endif
      = 0;
    usize waitingWriterCount
#if !defined(__INTELLISENSE__)
      GUARDED_BY(mutex)
#endif
      = 0;
    bool writerActive
#if !defined(__INTELLISENSE__)
      GUARDED_BY(mutex)
#endif
      = false;
};

// A shared mutex that gives priority to waiting writers over new readers.
// New readers wait while a writer is waiting, which can cause reader
// starvation under continuous writer contention.
#if !defined(__INTELLISENSE__)
class CAPABILITY("mutex") WriterSharedMutex
#else
class WriterSharedMutex
#endif
  final {
   public:
    void lock() noexcept
#if !defined(__INTELLISENSE__)
      ACQUIRE()
#endif
        ;

    bool try_lock() noexcept
#if !defined(__INTELLISENSE__)
      TRY_ACQUIRE(true)
#endif
        ;

    void unlock() noexcept
#if !defined(__INTELLISENSE__)
      RELEASE()
#endif
        ;

    void lock_shared() noexcept
#if !defined(__INTELLISENSE__)
      ACQUIRE_SHARED()
#endif
        ;

    bool try_lock_shared() noexcept
#if !defined(__INTELLISENSE__)
      TRY_ACQUIRE_SHARED(true)
#endif
        ;

    void unlock_shared() noexcept
#if !defined(__INTELLISENSE__)
      RELEASE_SHARED()
#endif
        ;


    [[nodiscard]] usize active_reader_count() const noexcept;

    [[nodiscard]] usize waiting_writer_count() const noexcept;

   private:
    mutable std::mutex      mutex;
    std::condition_variable readerCondVar;
    std::condition_variable writerCondVar;

    usize activeReaderCount
#if !defined(__INTELLISENSE__)
      GUARDED_BY(mutex)
#endif
      = 0;
    usize waitingWriterCount
#if !defined(__INTELLISENSE__)
      GUARDED_BY(mutex)
#endif
      = 0;
    bool writerActive
#if !defined(__INTELLISENSE__)
      GUARDED_BY(mutex)
#endif
      = false;
};

// A fair shared mutex that preserves queue order between writers and reader batches.
// Consecutive queued readers may acquire shared access together. try_lock_shared() succeeds
// only when no writer is active or waiting.
#if !defined(__INTELLISENSE__)
class CAPABILITY("mutex") FairSharedMutex
#else
class FairSharedMutex
#endif
  final {
   private:
    struct Waiter;

    // A simple FIFO queue to manage waiting threads.
    class WaitQueue final {
       public:
        void enqueue(Waiter& waiter) noexcept;

        void dequeue(Waiter& waiter) noexcept;

        Waiter* front() const noexcept;
        Waiter* back() const noexcept;

        usize size() const noexcept;
        bool  empty() const noexcept;

        // Returns true if there is a writer before the specified waiter,
        // or anywhere in the queue if until is nullptr.
        bool has_writer_before(const Waiter* const until = nullptr) const noexcept;

       private:
        Waiter* front_ = nullptr;
        Waiter* back_  = nullptr;
        usize   size_  = 0;
    };

    // A node in the wait queue representing a waiting thread.
    struct Waiter final {
        explicit Waiter(bool shared_) noexcept;

        bool is_writer() const noexcept;

       private:
        Waiter() noexcept = delete;

        const bool shared;

        Waiter* next = nullptr;

        friend class WaitQueue;
    };

   public:
    void lock() noexcept
#if !defined(__INTELLISENSE__)
      ACQUIRE()
#endif
        ;

    bool try_lock() noexcept
#if !defined(__INTELLISENSE__)
      TRY_ACQUIRE(true)
#endif
        ;

    void unlock() noexcept
#if !defined(__INTELLISENSE__)
      RELEASE()
#endif
        ;

    void lock_shared() noexcept
#if !defined(__INTELLISENSE__)
      ACQUIRE_SHARED()
#endif
        ;

    bool try_lock_shared() noexcept
#if !defined(__INTELLISENSE__)
      TRY_ACQUIRE_SHARED(true)
#endif
        ;

    void unlock_shared() noexcept
#if !defined(__INTELLISENSE__)
      RELEASE_SHARED()
#endif
        ;

    [[nodiscard]] usize active_reader_count() const noexcept;

    [[nodiscard]] usize waiting_writer_count() const noexcept;

   private:
    mutable std::mutex      mutex;
    std::condition_variable commonCondVar;

    WaitQueue waitQueue
#if !defined(__INTELLISENSE__)
      GUARDED_BY(mutex)
#endif
        ;
    usize activeReaderCount
#if !defined(__INTELLISENSE__)
      GUARDED_BY(mutex)
#endif
      = 0;
    usize waitingWriterCount
#if !defined(__INTELLISENSE__)
      GUARDED_BY(mutex)
#endif
      = 0;
    bool writerActive
#if !defined(__INTELLISENSE__)
      GUARDED_BY(mutex)
#endif
      = false;
};

}  // namespace DON

#endif  // MUTEX_H_INCLUDED
