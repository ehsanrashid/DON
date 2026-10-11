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
            wakeWriter = (--activeReaders == 0);
        }

        if (wakeWriter)
            writers.notify_one();
    }

    bool try_lock() {
        std::lock_guard lock(stateMutex);
        if (writerActive || activeReaders != 0)
            return false;

        writerActive = true;
        return true;
    }

    bool try_lock_shared() {
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

        writers.wait(lock, [this] { return !writerActive && activeReaders == 0; });

        --waitingWriters;
        writerActive = true;
    }

    void unlock() RELEASE() {
        {
            std::lock_guard lock(stateMutex);
            writerActive = false;
        }
        writers.notify_one();
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

    bool try_lock() {
        std::lock_guard lock(stateMutex);
        if (writerActive || activeReaders != 0)
            return false;
        writerActive = true;
        return true;
    }

    bool try_lock_shared() {
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

class CAPABILITY("mutex") FairSharedMutex final {
   private:
    struct Waiter {
        Waiter* next = nullptr;
        //bool    shared;
    };

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
            head = waiter.next;

            if (empty())
                tail = nullptr;
        }

        Waiter* front() const noexcept { return head; }

        bool empty() const noexcept { return head == nullptr; }

       private:
        Waiter* head = nullptr;
        Waiter* tail = nullptr;
    };

   public:
    void lock() ACQUIRE() {
        Waiter waiter;
        //waiter.shared = false;

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
        Waiter waiter;
        //waiter.shared = true;

        std::unique_lock lock(stateMutex);
        waitQueue.enqueue(waiter);

        changed.wait(lock,
                     [this, &waiter] { return !writerActive && waitQueue.front() == &waiter; });

        waitQueue.dequeue(waiter);
        ++activeReaders;
        changed.notify_all();
    }

    void unlock_shared() RELEASE_SHARED() {
        {
            std::lock_guard lock(stateMutex);
            --activeReaders;
        }

        changed.notify_all();
    }

    bool try_lock() {
        std::lock_guard lock(stateMutex);

        if (writerActive || activeReaders != 0 || !waitQueue.empty())
            return false;

        writerActive = true;
        return true;
    }

    bool try_lock_shared() {
        std::lock_guard lock(stateMutex);

        if (writerActive || !waitQueue.empty())
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
