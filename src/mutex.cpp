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

#include "mutex.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#if defined(X86)
    #include <immintrin.h>
#endif

namespace DON {

void cpu_relax() noexcept {
#if defined(X86)
    _mm_pause();

#elif defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__("yield");

#elif defined(__riscv)
    #if defined(__riscv_zihintpause)
    __builtin_riscv_pause();
    #elif defined(__clang__) || defined(__GNUC__)
    __asm__ __volatile__("" ::: "memory");
    #endif

#elif defined(__powerpc__) || defined(__ppc__) || defined(__PPC64__) || defined(_ARCH_PPC)
    __asm__ __volatile__("or 27,27,27");

#elif defined(__clang__) || defined(__GNUC__)
    // Portable compiler-level no-op for unsupported architectures.
    __asm__ __volatile__("");
#endif
}

// ------------------

void SpinMutex::lock() noexcept {
    usize spins  = 0;
    usize yields = 0;
    bool  expected;

    do
    {
        while (locked.load(std::memory_order_relaxed))
        {
            cpu_relax();

            // Yield occasionally to reduce CPU usage under contention.
            if (++spins == 512)
            {
                spins = 0;

                if (++yields == 8)
                {
                    yields = 0;
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
                else
                    std::this_thread::yield();
            }
        }

        expected = false;
    } while (!locked.compare_exchange_weak(expected, true, std::memory_order_acquire,
                                           std::memory_order_relaxed));
}

void SpinMutex::unlock() noexcept { locked.store(false, std::memory_order_release); }

// ------------------

void ReaderSharedMutex::lock() noexcept {
    std::unique_lock lock(mutex);

    ++waitingWriterCount;

    writerCondVar.wait(lock, [this]() noexcept -> bool {
        return !writerActive            // No active writer
            && activeReaderCount == 0;  // No active readers
    });

    --waitingWriterCount;

    writerActive = true;
}

bool ReaderSharedMutex::try_lock() noexcept {
    std::lock_guard lock(mutex);

    if (writerActive                // An active writer
        || activeReaderCount != 0)  // Active readers
        return false;

    writerActive = true;
    return true;
}

void ReaderSharedMutex::unlock() noexcept {
    {
        std::lock_guard lock(mutex);

        writerActive = false;
    }

    readerCondVar.notify_all();
    writerCondVar.notify_one();
}

void ReaderSharedMutex::lock_shared() noexcept {
    std::unique_lock lock(mutex);

    readerCondVar.wait(lock, [this]() noexcept -> bool {
        return !writerActive;  // No active writer
    });

    ++activeReaderCount;
}

bool ReaderSharedMutex::try_lock_shared() noexcept {
    std::lock_guard lock(mutex);

    if (writerActive)  // An active writer
        return false;

    ++activeReaderCount;
    return true;
}

void ReaderSharedMutex::unlock_shared() noexcept {
    bool wakeWriter;
    {
        std::lock_guard lock(mutex);

        wakeWriter = --activeReaderCount == 0;
    }

    if (wakeWriter)
        writerCondVar.notify_one();
}

usize ReaderSharedMutex::active_reader_count() const noexcept {
    std::lock_guard lock(mutex);

    return activeReaderCount;
}

usize ReaderSharedMutex::waiting_writer_count() const noexcept {
    std::lock_guard lock(mutex);

    return waitingWriterCount;
}

// ------------------


/*
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
#if !defined(__INTELLISENSE__)
    void lock() noexcept ACQUIRE()
#else
    void lock() noexcept
#endif
    {
        std::unique_lock lock(mutex);

        ++waitingWriterCount;

        writerCondVar.wait(lock, [this]() noexcept -> bool {
            return !writerActive  // No active writer
                // waitingWriterCount includes this writer; checking for zero would deadlock.
                && activeReaderCount == 0;  // No active readers
        });

        --waitingWriterCount;
        writerActive = true;
    }

#if !defined(__INTELLISENSE__)
    bool try_lock() noexcept TRY_ACQUIRE(true)
#else
    bool try_lock() noexcept
#endif
    {
        std::lock_guard lock(mutex);

        if (writerActive                // An active writer
            || waitingWriterCount != 0  // Waiting writers; don't bypass them
            || activeReaderCount != 0)  // Active readers
            return false;

        writerActive = true;
        return true;
    }

#if !defined(__INTELLISENSE__)
    void unlock() noexcept RELEASE()
#else
    void unlock() noexcept
#endif
    {
        bool wakeWriter;
        {
            std::lock_guard lock(mutex);

            writerActive = false;
            wakeWriter   = waitingWriterCount != 0;
        }

        if (wakeWriter)
            writerCondVar.notify_one();
        else
            readerCondVar.notify_all();
    }

#if !defined(__INTELLISENSE__)
    void lock_shared() noexcept ACQUIRE_SHARED()
#else
    void lock_shared() noexcept
#endif
    {
        std::unique_lock lock(mutex);

        readerCondVar.wait(lock, [this]() noexcept -> bool {
            return !writerActive
                && waitingWriterCount == 0;  // Allow concurrent readers may coexist
        });

        ++activeReaderCount;
    }

#if !defined(__INTELLISENSE__)
    bool try_lock_shared() noexcept TRY_ACQUIRE_SHARED(true)
#else
    bool try_lock_shared() noexcept
#endif
    {
        std::lock_guard lock(mutex);

        if (writerActive || waitingWriterCount != 0)  // Allow concurrent readers may coexist
            return false;

        ++activeReaderCount;
        return true;
    }

#if !defined(__INTELLISENSE__)
    void unlock_shared() noexcept RELEASE_SHARED()
#else
    void unlock_shared() noexcept
#endif
    {
        bool wakeWriter;
        {
            std::lock_guard lock(mutex);

            wakeWriter = --activeReaderCount == 0;
        }

        if (wakeWriter)
            writerCondVar.notify_one();
    }

    usize active_reader_count() const noexcept {
        std::lock_guard lock(mutex);
        return activeReaderCount;
    }

    usize waiting_writer_count() const noexcept {
        std::lock_guard lock(mutex);
        return waitingWriterCount;
    }

   private:
    mutable std::mutex      mutex;
    std::condition_variable readerCondVar;
    std::condition_variable writerCondVar;
    // clang-format off
#if !defined(__INTELLISENSE__)
    usize activeReaderCount  GUARDED_BY(mutex) = 0;
    usize waitingWriterCount GUARDED_BY(mutex) = 0;
    bool  writerActive       GUARDED_BY(mutex) = false;
#else
    usize activeReaderCount  = 0;
    usize waitingWriterCount = 0;
    bool  writerActive       = false;
#endif
    // clang-format on
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
    class WaitQueue;

    // A node in the wait queue representing a waiting thread.
    struct Waiter final {
        explicit Waiter(bool shared_) noexcept :
            shared(shared_) {}

       private:
        Waiter() noexcept = delete;

        const bool shared;

        Waiter* next = nullptr;

        friend class WaitQueue;
    };

    // A simple FIFO queue to manage waiting threads.
    class WaitQueue final {
       public:
        void enqueue(Waiter& waiter) noexcept {
            Waiter*& nxt = tail != nullptr ? tail->next : head;

            nxt  = &waiter;
            tail = &waiter;
        }

        void dequeue(Waiter& waiter) noexcept {
            Waiter* pre = nullptr;
            auto*   cur = head;

            while (cur != nullptr && cur != &waiter)
            {
                pre = cur;
                cur = cur->next;
            }

            if (cur == nullptr)
                return;

            if (pre != nullptr)
                pre->next = cur->next;
            else
                head = cur->next;

            if (tail == cur)
                tail = pre;

            cur->next = nullptr;
        }

        Waiter* front() const noexcept { return head; }

        bool empty() const noexcept { return head == nullptr; }

        // Returns true if there is a writer before the specified waiter,
        // or anywhere in the queue if until is nullptr.
        bool has_writer_before(const Waiter* const until = nullptr) const noexcept {
            for (auto* cur = head; cur != nullptr; cur = cur->next)
            {
                if (cur == until)
                    break;

                if (!cur->shared)
                    return true;
            }

            return false;
        }

       private:
        Waiter* head = nullptr;
        Waiter* tail = nullptr;
    };

   public:
#if !defined(__INTELLISENSE__)
    void lock() noexcept ACQUIRE()
#else
    void lock() noexcept
#endif
    {
        Waiter writer(false);

        std::unique_lock lock(mutex);

        ++waitingWriterCount;
        waitQueue.enqueue(writer);

        condVar.wait(lock, [this, &writer]() noexcept -> bool {
            return !writerActive                 // No active writer
                && waitQueue.front() == &writer  // This writer's turn
                && activeReaderCount == 0;       // No active readers
        });

        waitQueue.dequeue(writer);
        --waitingWriterCount;

        writerActive = true;
    }

#if !defined(__INTELLISENSE__)
    bool try_lock() noexcept TRY_ACQUIRE(true)
#else
    bool try_lock() noexcept
#endif
    {
        std::lock_guard lock(mutex);

        if (writerActive                // An active writer
            || !waitQueue.empty()       // Threads already queued
            || activeReaderCount != 0)  // Active readers
            return false;

        writerActive = true;
        return true;
    }

#if !defined(__INTELLISENSE__)
    void unlock() noexcept RELEASE()
#else
    void unlock() noexcept
#endif
    {
        {
            std::lock_guard lock(mutex);

            writerActive = false;
        }

        condVar.notify_all();
    }

#if !defined(__INTELLISENSE__)
    void lock_shared() noexcept ACQUIRE_SHARED()
#else
    void lock_shared() noexcept
#endif
    {
        Waiter reader(true);

        std::unique_lock lock(mutex);

        waitQueue.enqueue(reader);

        condVar.wait(lock, [this, &reader]() noexcept -> bool {
            return !writerActive                           // No active writer
                && !waitQueue.has_writer_before(&reader);  // No writer ahead of this reader
        });

        waitQueue.dequeue(reader);

        ++activeReaderCount;

        lock.unlock();
        condVar.notify_all();
    }

#if !defined(__INTELLISENSE__)
    bool try_lock_shared() noexcept TRY_ACQUIRE_SHARED(true)
#else
    bool try_lock_shared() noexcept
#endif
    {
        std::lock_guard lock(mutex);

        // Do not bypass an active or waiting writer.
        if (writerActive                       // An active writer
            || waitQueue.has_writer_before())  // A queued writer
            return false;

        ++activeReaderCount;
        return true;
    }

#if !defined(__INTELLISENSE__)
    void unlock_shared() noexcept RELEASE_SHARED()
#else
    void unlock_shared() noexcept
#endif
    {
        {
            std::lock_guard lock(mutex);
            --activeReaderCount;
        }

        condVar.notify_all();
    }

    usize active_reader_count() const noexcept {
        std::lock_guard lock(mutex);
        return activeReaderCount;
    }

    usize waiting_writer_count() const noexcept {
        std::lock_guard lock(mutex);
        return waitingWriterCount;
    }

   private:
    mutable std::mutex      mutex;
    std::condition_variable condVar;
    // clang-format off
#if !defined(__INTELLISENSE__)
    WaitQueue waitQueue          GUARDED_BY(mutex);
    usize     activeReaderCount  GUARDED_BY(mutex) = 0;
    usize     waitingWriterCount GUARDED_BY(mutex) = 0;
    bool      writerActive       GUARDED_BY(mutex) = false;
#else
    WaitQueue waitQueue;
    usize     activeReaderCount  = 0;
    usize     waitingWriterCount = 0;
    bool      writerActive       = false;
#endif
    // clang-format on
};
*/

}  // namespace DON
