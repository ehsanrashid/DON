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

#include "../utility/mutex.h"  // IWYU pragma: associated

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>  // sleep_for()

#if defined(_WIN32)
    #include "../platform_win.h"  // YieldProcessor()
#elif defined(X86)
    #include <emmintrin.h>  // _mm_pause() intrinsic
#endif

namespace DON {

void cpu_relax() noexcept {
#if defined(_WIN32)
    YieldProcessor();

#elif defined(X86)
    _mm_pause();

#elif defined(__clang__) || defined(__GNUC__)
    #if defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__("yield");

    #elif defined(__riscv)
        #if defined(__riscv_zihintpause)
    __builtin_riscv_pause();

        #else
    __asm__ __volatile__("" ::: "memory");

        #endif

    #elif defined(__powerpc__) || defined(__ppc__) || defined(__PPC64__) || defined(_ARCH_PPC)
    __asm__ __volatile__("or 27,27,27");

    #else
    // Portable compiler-level no-op for unsupported architectures
    __asm__ __volatile__("" ::: "memory");

    #endif

#else
    // No architecture-specific pause hint available for this compiler
    //__asm__ __volatile__("");

#endif
}

// ------------------

// A lightweight spin mutex that periodically yields and backs off under contention.
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
            if (++spins == SpinsMax)
            {
                spins = 0;

                if (++yields == YieldsMax)
                {
                    yields = 0;

                    std::this_thread::sleep_for(std::chrono::microseconds(SleepIntervalUs));
                    continue;
                }

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

    // Wake waiting readers and a writer; readers may proceed concurrently.
    readerCondVar.notify_all();
    writerCondVar.notify_one();
}

void ReaderSharedMutex::lock_shared() noexcept {
    std::unique_lock lock(mutex);

    readerCondVar.wait(lock, [this]() noexcept -> bool {
        return !writerActive  // No active writer
          //&& waitingWriterCount == 0  // No waiting writers
          ;
    });

    ++activeReaderCount;
}

bool ReaderSharedMutex::try_lock_shared() noexcept {
    std::lock_guard lock(mutex);

    if (writerActive  // An active writer
        //|| waitingWriterCount != 0  // Waiting writers; don't bypass them
    )
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

void WriterSharedMutex::lock() noexcept {
    std::unique_lock lock(mutex);

    ++waitingWriterCount;

    writerCondVar.wait(lock, [this]() noexcept -> bool {
        return !writerActive  // No active writer
            // waitingWriterCount includes this writer and is not a readiness condition.
            && activeReaderCount == 0;  // No active readers
    });

    --waitingWriterCount;

    writerActive = true;
}

bool WriterSharedMutex::try_lock() noexcept {
    std::lock_guard lock(mutex);

    if (writerActive                // An active writer
        || waitingWriterCount != 0  // Waiting writers; don't bypass them
        || activeReaderCount != 0)  // Active readers
        return false;

    writerActive = true;
    return true;
}

void WriterSharedMutex::unlock() noexcept {
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

void WriterSharedMutex::lock_shared() noexcept {
    std::unique_lock lock(mutex);

    readerCondVar.wait(lock, [this]() noexcept -> bool {
        return !writerActive             // No active writer
            && waitingWriterCount == 0;  // No waiting writers
    });

    ++activeReaderCount;
}

bool WriterSharedMutex::try_lock_shared() noexcept {
    std::lock_guard lock(mutex);

    if (writerActive                 // Writer active
        || waitingWriterCount != 0)  // Waiting writers; don't bypass them
        return false;

    ++activeReaderCount;
    return true;
}

void WriterSharedMutex::unlock_shared() noexcept {
    bool wakeWriter;
    {
        std::lock_guard lock(mutex);

        wakeWriter = --activeReaderCount == 0;
    }

    if (wakeWriter)
        writerCondVar.notify_one();
}

usize WriterSharedMutex::active_reader_count() const noexcept {
    std::lock_guard lock(mutex);

    return activeReaderCount;
}

usize WriterSharedMutex::waiting_writer_count() const noexcept {
    std::lock_guard lock(mutex);

    return waitingWriterCount;
}

// ------------------

FairSharedMutex::Waiter::Waiter(bool writer_) noexcept :
    writer(writer_) {}

bool FairSharedMutex::Waiter::is_writer() const noexcept { return writer; }

void FairSharedMutex::lock() noexcept {
    Waiter writer(true);

    std::unique_lock lock(mutex);

    ++waitingWriterCount;
    waitList.push_back(writer);

    commonCondVar.wait(lock, [this, &writer]() noexcept -> bool {
        return !writerActive                // No active writer
            && waitList.front() == &writer  // This writer is first in line
            && activeReaderCount == 0;      // No active readers
    });

    waitList.erase(writer);
    --waitingWriterCount;

    writerActive = true;
}

bool FairSharedMutex::try_lock() noexcept {
    std::lock_guard lock(mutex);

    if (writerActive                // An active writer
        || !waitList.empty()        // Waiting threads
        || activeReaderCount != 0)  // Active readers
        return false;

    writerActive = true;
    return true;
}

void FairSharedMutex::unlock() noexcept {
    {
        std::lock_guard lock(mutex);

        writerActive = false;
    }

    // Wake all waiters to recheck their eligibility
    commonCondVar.notify_all();
}

void FairSharedMutex::lock_shared() noexcept {
    Waiter reader(false);

    std::unique_lock lock(mutex);

    waitList.push_back(reader);

    commonCondVar.wait(lock, [this, &reader]() noexcept -> bool {
        return !writerActive                 // No active writer
            && !has_writer_before(&reader);  // No writer ahead of this reader
    });

    waitList.erase(reader);

    ++activeReaderCount;
}

bool FairSharedMutex::try_lock_shared() noexcept {
    std::lock_guard lock(mutex);

    // Do not bypass an active writer or any waiting writer.
    if (writerActive             // An active writer
        || has_writer_before())  // A waiting writer
        return false;

    ++activeReaderCount;
    return true;
}

void FairSharedMutex::unlock_shared() noexcept {
    {
        std::lock_guard lock(mutex);

        --activeReaderCount;
    }

    // Wake waiters to recheck their eligibility
    commonCondVar.notify_all();
}

bool FairSharedMutex::has_writer_before(const Waiter* const until) const noexcept {
    for (auto* cur = waitList.front(); cur != nullptr; cur = cur->next)
    {
        if (cur == until)
            break;

        if (cur->is_writer())
            return true;
    }

    return false;
}

usize FairSharedMutex::active_reader_count() const noexcept {
    std::lock_guard lock(mutex);

    return activeReaderCount;
}

usize FairSharedMutex::waiting_writer_count() const noexcept {
    std::lock_guard lock(mutex);

    return waitingWriterCount;
}

// ------------------

}  // namespace DON
