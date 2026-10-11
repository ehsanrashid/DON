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
#include <shared_mutex>
#include <thread>

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

#include "misc.h"

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
class CAPABILITY("mutex") SpinMutex final {
   public:
    void lock() ACQUIRE() noexcept {
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

    void unlock() RELEASE() noexcept { locked.store(false, std::memory_order_release); }

   private:
    std::atomic<bool> locked{false};
};

class CAPABILITY("mutex") ReaderPreferredSharedMutex final {
   public:
    void lock() ACQUIRE() {
        std::unique_lock lock(stateMutex);
        //++waitingWriters;

        writers.wait(lock, [this] { return !writerActive && activeReaders == 0; });

        //--waitingWriters;
        writerActive = true;
    }

    void unlock() RELEASE() {
        {
            std::lock_guard lock(stateMutex);
            writerActive = false;
        }

        readers.notify_all();
        writers.notify_one();
    }

    void lock_shared() ACQUIRE_SHARED() {
        std::unique_lock lock(stateMutex);
        readers.wait(lock, [this] { return !writerActive; });

        ++activeReaders;
    }

    void unlock_shared() RELEASE_SHARED() {
        bool wakeWriter;
        {
            std::lock_guard lock(stateMutex);
            wakeWriter = --activeReaders == 0;
        }

        if (wakeWriter)
            writers.notify_one();
    }

    bool try_lock() TRY_ACQUIRE(true) {
        std::lock_guard lock(stateMutex);
        if (writerActive || activeReaders != 0)
            return false;

        writerActive = true;
        return true;
    }

    bool try_lock_shared() TRY_ACQUIRE_SHARED(true) {
        std::lock_guard lock(stateMutex);
        if (writerActive)
            return false;

        ++activeReaders;
        return true;
    }

    usize active_readers() const noexcept {
        std::lock_guard lock(stateMutex);
        return activeReaders;
    }

    //usize waiting_writers() const noexcept {
    //    std::lock_guard lock(stateMutex);
    //    return waitingWriters;
    //}

   private:
    mutable std::mutex      stateMutex;
    bool                    writerActive = false;
    std::condition_variable readers;
    std::condition_variable writers;
    usize                   activeReaders = 0;
    //usize                   waitingWriters = 0;
};

class CAPABILITY("mutex") WriterPreferredSharedMutex final {
   public:
    void lock() ACQUIRE() {
        std::unique_lock lock(stateMutex);
        ++waitingWriters;

        // try
        // {
        writers.wait(lock, [this] { return !writerActive && activeReaders == 0; });
        // } catch (...)
        // {
        //     --waitingWriters;

        //     const bool wakeWriter  = waitingWriters != 0 && !writerActive && activeReaders == 0;
        //     const bool wakeReaders = waitingWriters == 0 && !writerActive;

        //     lock.unlock();

        //     if (wakeWriter)
        //         writers.notify_one();
        //     else if (wakeReaders)
        //         readers.notify_all();

        //     throw;
        // }

        --waitingWriters;
        writerActive = true;
    }

    void unlock() RELEASE() {
        bool wakeWriter;
        {
            std::lock_guard lock(stateMutex);
            writerActive = false;
            wakeWriter   = waitingWriters != 0;
        }

        if (wakeWriter)
            writers.notify_one();
        else
            readers.notify_all();
    }

    void lock_shared() ACQUIRE_SHARED() {
        std::unique_lock lock(stateMutex);
        readers.wait(lock, [this] { return !writerActive && waitingWriters == 0; });

        ++activeReaders;
    }

    void unlock_shared() RELEASE_SHARED() {
        bool wakeWriter;
        {
            std::lock_guard lock(stateMutex);
            wakeWriter = (--activeReaders == 0);
        }

        if (wakeWriter)
            writers.notify_one();
    }

    bool try_lock() TRY_ACQUIRE(true) {
        std::lock_guard lock(stateMutex);

        if (writerActive || activeReaders != 0 || waitingWriters != 0)
            return false;

        writerActive = true;
        return true;
    }

    bool try_lock_shared() TRY_ACQUIRE_SHARED(true) {
        std::lock_guard lock(stateMutex);

        if (writerActive || waitingWriters != 0)
            return false;

        ++activeReaders;
        return true;
    }

    usize active_readers() const noexcept {
        std::lock_guard lock(stateMutex);
        return activeReaders;
    }

    usize waiting_writers() const noexcept {
        std::lock_guard lock(stateMutex);
        return waitingWriters;
    }

   private:
    mutable std::mutex      stateMutex;
    bool                    writerActive = false;
    std::condition_variable readers;
    std::condition_variable writers;
    usize                   activeReaders  = 0;
    usize                   waitingWriters = 0;
};

// A fair shared mutex that preserves queue order between writers and reader batches.
// Consecutive readers may acquire shared access together, including through try_lock_shared(),
// provided no writer is active or ahead of them in the queue.
class CAPABILITY("mutex") FairSharedMutex final {
   private:
    class WaitQueue;

    // A node in the wait queue representing a waiting thread.
    struct Waiter final {
        explicit Waiter(bool shared_) noexcept :
            shared(shared_) {}

       private:
        Waiter() noexcept = delete;

        bool    shared;
        Waiter* next = nullptr;

        friend class WaitQueue;
    };

    // A simple FIFO queue to manage waiting threads.
    class WaitQueue final {
       public:
        void enqueue(Waiter& waiter) noexcept {
            if (tail != nullptr)
                tail->next = &waiter;
            else
                head = &waiter;

            tail = &waiter;
        }

        void dequeue(Waiter& waiter) noexcept {
            Waiter* prev = nullptr;
            auto*   cur  = head;

            while (cur != nullptr && cur != &waiter)
            {
                prev = cur;
                cur  = cur->next;
            }

            if (cur == nullptr)
                return;

            if (prev != nullptr)
                prev->next = cur->next;
            else
                head = cur->next;

            if (tail == cur)
                tail = prev;

            cur->next = nullptr;
        }

        Waiter* front() const noexcept { return head; }

        // Returns true if the front of the queue is a writer.
        bool front_is_writer() const noexcept { return head != nullptr && !head->shared; }

        bool empty() const noexcept { return head == nullptr; }

        // Returns true if there is a writer in the queue before the specified waiter.
        bool has_writer(const Waiter* until = nullptr) const noexcept {
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
    void lock() ACQUIRE() {
        Waiter           waiter(false);
        std::unique_lock lock(stateMutex);

        ++waitingWriters;
        waitQueue.enqueue(waiter);

        changed.wait(lock, [this, &waiter] {
            return !writerActive && activeReaders == 0 && waitQueue.front() == &waiter;
        });

        waitQueue.dequeue(waiter);
        --waitingWriters;
        writerActive = true;
    }

    void unlock() RELEASE() {
        {
            std::lock_guard lock(stateMutex);
            writerActive = false;
        }

        changed.notify_all();
    }

    void lock_shared() ACQUIRE_SHARED() {
        Waiter waiter(true);

        std::unique_lock lock(stateMutex);
        waitQueue.enqueue(waiter);

        changed.wait(
          // Check writers before waiter
          lock, [this, &waiter] { return !writerActive && !waitQueue.has_writer(&waiter); });

        waitQueue.dequeue(waiter);
        ++activeReaders;

        lock.unlock();
        changed.notify_all();
    }

    void unlock_shared() RELEASE_SHARED() {
        {
            std::lock_guard lock(stateMutex);
            --activeReaders;
        }

        changed.notify_all();
    }

    bool try_lock() TRY_ACQUIRE(true) {
        std::lock_guard lock(stateMutex);

        if (writerActive || activeReaders != 0 || !waitQueue.empty())
            return false;

        writerActive = true;
        return true;
    }

    bool try_lock_shared() TRY_ACQUIRE_SHARED(true) {
        std::lock_guard lock(stateMutex);
        // If there is a writer active or waiting, we cannot acquire the lock shared.
        if (writerActive || waitQueue.front_is_writer())
            return false;

        ++activeReaders;
        return true;
    }

    usize active_readers() const noexcept {
        std::lock_guard lock(stateMutex);
        return activeReaders;
    }

    usize waiting_writers() const noexcept {
        std::lock_guard lock(stateMutex);
        return waitingWriters;
    }

   private:
    mutable std::mutex      stateMutex;
    bool                    writerActive = false;
    std::condition_variable changed;
    WaitQueue               waitQueue;
    usize                   activeReaders  = 0;
    usize                   waitingWriters = 0;
};

}  // namespace DON

#endif  // MUTEX_H_INCLUDED
