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

#include "../utility/mutex.h"

#include <chrono>
#include <thread>  // sleep_for()

#include "../utility/cpu.h"
#include "../utility/list.h"
#include "../utility/types.h"
#include "mutex.h"

namespace DON {

void Mutex::lock() noexcept { mutex.lock(); }

void Mutex::unlock() noexcept { mutex.unlock(); }

// ------------------

// A lightweight spin mutex that periodically yields and backs off under contention.
void SpinMutex::lock() noexcept {
    usize spins  = 0;
    usize yields = 0;

    while (true)
    {
        if (!locked.load(std::memory_order_relaxed))
        {
            bool expected = false;

            if (locked.compare_exchange_weak(expected, true, std::memory_order_acquire,
                                             std::memory_order_relaxed))
                break;
        }

        // Yield occasionally to reduce CPU usage under contention.
        if (++spins == SpinsMax)
        {
            spins = 0;

            if (++yields == YieldsMax)
            {
                yields = 0;

                std::this_thread::sleep_for(std::chrono::nanoseconds(SleepTimeNs));
                continue;
            }

            std::this_thread::yield();
            continue;
        }

        cpu_relax();
    }
}

void SpinMutex::unlock() noexcept { locked.store(false, std::memory_order_release); }

// ------------------

void SharedMutex::lock() noexcept {
    std::unique_lock uniqueLock{mutex};

    ++waitingWriterCount;

    commonCondVar.wait(uniqueLock, [this]() noexcept -> bool { return can_lock(); });

    --waitingWriterCount;

    writerActive = true;
}

bool SharedMutex::try_lock() noexcept {
    std::lock_guard lockGuard{mutex};

    if (!can_try_lock())
        return false;

    writerActive = true;
    return true;
}

void SharedMutex::unlock() noexcept {
    {
        std::lock_guard lockGuard{mutex};

        assert(writerActive);
        writerActive = false;
    }

    commonCondVar.notify_all();
}

void SharedMutex::lock_shared() noexcept {
    std::unique_lock uniqueLock{mutex};

    commonCondVar.wait(uniqueLock, [this]() noexcept -> bool { return can_lock_shared(); });

    ++activeReaderCount;
}

bool SharedMutex::try_lock_shared() noexcept {
    std::lock_guard lockGuard{mutex};

    if (!can_try_lock_shared())
        return false;

    ++activeReaderCount;
    return true;
}

void SharedMutex::unlock_shared() noexcept {
    {
        std::lock_guard lockGuard{mutex};

        assert(activeReaderCount != 0);
        --activeReaderCount;
    }

    // Wake waiters to recheck their eligibility
    commonCondVar.notify_all();
}

bool SharedMutex::can_lock() const noexcept {
    return !writerActive            // No active writer
        && activeReaderCount == 0;  // No active readers
}

bool SharedMutex::can_try_lock() const noexcept {
    return !writerActive            // No active writer
        && activeReaderCount == 0;  // No active readers
}

bool SharedMutex::can_lock_shared() const noexcept {
    return !writerActive;  // No active writer
}

bool SharedMutex::can_try_lock_shared() const noexcept {
    return !writerActive;  // No active writer
}

usize SharedMutex::active_reader_count() const noexcept {
    std::lock_guard lockGuard{mutex};

    return activeReaderCount;
}

usize SharedMutex::waiting_writer_count() const noexcept {
    std::lock_guard lockGuard{mutex};

    return waitingWriterCount;
}

// ------------------

void ReaderSharedMutex::lock() noexcept {
    std::unique_lock uniqueLock{mutex};

    ++waitingWriterCount;

    writerCondVar.wait(uniqueLock, [this]() noexcept -> bool { return can_lock(); });

    --waitingWriterCount;

    writerActive = true;
}

bool ReaderSharedMutex::try_lock() noexcept {
    std::lock_guard lockGuard{mutex};

    if (!can_try_lock())
        return false;

    writerActive = true;
    return true;
}

void ReaderSharedMutex::unlock() noexcept {
    {
        std::lock_guard lockGuard{mutex};

        assert(writerActive);
        writerActive = false;
    }

    // Wake waiting readers and a writer; readers may proceed concurrently.
    readerCondVar.notify_all();
    writerCondVar.notify_one();
}

void ReaderSharedMutex::lock_shared() noexcept {
    std::unique_lock uniqueLock{mutex};

    readerCondVar.wait(uniqueLock, [this]() noexcept -> bool { return can_lock_shared(); });

    ++activeReaderCount;
}

bool ReaderSharedMutex::try_lock_shared() noexcept {
    std::lock_guard lockGuard{mutex};

    if (!can_try_lock_shared())
        return false;

    ++activeReaderCount;
    return true;
}

void ReaderSharedMutex::unlock_shared() noexcept {
    bool notifyWriter;
    {
        std::lock_guard lockGuard{mutex};

        assert(activeReaderCount != 0);
        notifyWriter = --activeReaderCount == 0;
    }

    if (notifyWriter)
        writerCondVar.notify_one();
}

bool ReaderSharedMutex::can_lock() const noexcept {
    return !writerActive            // No active writer
        && activeReaderCount == 0;  // No active readers
}

bool ReaderSharedMutex::can_try_lock() const noexcept {
    return !writerActive            // No active writer
        && activeReaderCount == 0;  // No active readers
}

bool ReaderSharedMutex::can_lock_shared() const noexcept {
    return !writerActive;  // No active writer
}

bool ReaderSharedMutex::can_try_lock_shared() const noexcept {
    return !writerActive;  // No active writer
}

usize ReaderSharedMutex::active_reader_count() const noexcept {
    std::lock_guard lockGuard{mutex};

    return activeReaderCount;
}

usize ReaderSharedMutex::waiting_writer_count() const noexcept {
    std::lock_guard lockGuard{mutex};

    return waitingWriterCount;
}

// ------------------

void WriterSharedMutex::lock() noexcept {
    std::unique_lock uniqueLock{mutex};

    ++waitingWriterCount;

    writerCondVar.wait(uniqueLock, [this]() noexcept -> bool { return can_lock(); });

    --waitingWriterCount;

    writerActive = true;
}

bool WriterSharedMutex::try_lock() noexcept {
    std::lock_guard lockGuard{mutex};

    if (!can_try_lock())
        return false;

    writerActive = true;
    return true;
}

void WriterSharedMutex::unlock() noexcept {
    bool notifyWriter;
    {
        std::lock_guard lockGuard{mutex};

        assert(writerActive);
        writerActive = false;
        notifyWriter = waitingWriterCount != 0;
    }

    if (notifyWriter)
        writerCondVar.notify_one();
    else
        readerCondVar.notify_all();
}

void WriterSharedMutex::lock_shared() noexcept {
    std::unique_lock uniqueLock{mutex};

    readerCondVar.wait(uniqueLock, [this]() noexcept -> bool { return can_lock_shared(); });

    ++activeReaderCount;
}

bool WriterSharedMutex::try_lock_shared() noexcept {
    std::lock_guard lockGuard{mutex};

    if (!can_try_lock_shared())
        return false;

    ++activeReaderCount;
    return true;
}

void WriterSharedMutex::unlock_shared() noexcept {
    bool notifyWriter;
    {
        std::lock_guard lockGuard{mutex};

        assert(activeReaderCount != 0);
        notifyWriter = --activeReaderCount == 0;
    }

    if (notifyWriter)
        writerCondVar.notify_one();
}

bool WriterSharedMutex::can_lock() const noexcept {
    return !writerActive            // No active writer
        && activeReaderCount == 0;  // No active readers
}

bool WriterSharedMutex::can_try_lock() const noexcept {
    return !writerActive            // No active writer
        && waitingWriterCount == 0  // No waiting writer
        && activeReaderCount == 0;  // No active readers
}

bool WriterSharedMutex::can_lock_shared() const noexcept {
    return !writerActive             // No active writer
        && waitingWriterCount == 0;  // No waiting writers
}

bool WriterSharedMutex::can_try_lock_shared() const noexcept {
    return !writerActive             // No active writer
        && waitingWriterCount == 0;  // No waiting writers
}

usize WriterSharedMutex::active_reader_count() const noexcept {
    std::lock_guard lockGuard{mutex};

    return activeReaderCount;
}

usize WriterSharedMutex::waiting_writer_count() const noexcept {
    std::lock_guard lockGuard{mutex};

    return waitingWriterCount;
}

// ------------------

FairSharedMutex::Waiter::Waiter(bool writer_) noexcept :
    writer(writer_) {
    node.data = this;
}

bool FairSharedMutex::Waiter::is_writer() const noexcept { return writer; }

void FairSharedMutex::lock() noexcept {
    Waiter writer(true);

    std::unique_lock uniqueLock{mutex};

    ++waitingWriterCount;
    waitList.push_back(writer.node);

    commonCondVar.wait(uniqueLock, [this, &writer]() noexcept -> bool { return can_lock(writer); });

    waitList.erase(writer.node);
    --waitingWriterCount;

    writerActive = true;
}

bool FairSharedMutex::try_lock() noexcept {
    std::lock_guard lockGuard{mutex};

    if (!can_try_lock())
        return false;

    writerActive = true;
    return true;
}

void FairSharedMutex::unlock() noexcept {
    {
        std::lock_guard lockGuard{mutex};

        assert(writerActive);
        writerActive = false;
    }

    // Wake all waiters to recheck their eligibility
    commonCondVar.notify_all();
}

void FairSharedMutex::lock_shared() noexcept {
    Waiter reader(false);

    std::unique_lock uniqueLock{mutex};

    waitList.push_back(reader.node);

    commonCondVar.wait(uniqueLock,
                       [this, &reader]() noexcept -> bool { return can_lock_shared(reader); });

    waitList.erase(reader.node);

    ++activeReaderCount;
}

bool FairSharedMutex::try_lock_shared() noexcept {
    std::lock_guard lockGuard{mutex};

    if (!can_try_lock_shared())
        return false;

    ++activeReaderCount;
    return true;
}

void FairSharedMutex::unlock_shared() noexcept {
    {
        std::lock_guard lockGuard{mutex};

        assert(activeReaderCount != 0);
        --activeReaderCount;
    }

    // Wake waiters to recheck their eligibility
    commonCondVar.notify_all();
}

bool FairSharedMutex::can_lock(const Waiter& writer) const noexcept {
    return !writerActive                // No active writer
        && waitList.front() == &writer  // This writer is first in line
        && activeReaderCount == 0;      // No active readers
}

bool FairSharedMutex::can_try_lock() const noexcept {
    return !writerActive            // No active writer
        && waitList.empty()         // No waiting threads
        && activeReaderCount == 0;  // No active readers
}

bool FairSharedMutex::can_lock_shared(const Waiter& reader) const noexcept {
    return !writerActive                 // No active writer
        && !has_writer_before(&reader);  // No writer ahead of this reader
}

bool FairSharedMutex::can_try_lock_shared() const noexcept {
    return !writerActive          // No active writer
        && !has_writer_before();  // No waiting writers
}

bool FairSharedMutex::has_writer_before(const Waiter* const until) const noexcept {
    for (const Waiter* cur = waitList.front(); cur != nullptr; cur = waitList.next(*cur))
    {
        if (cur == until)
            break;

        if (cur->is_writer())
            return true;
    }

    return false;
}

usize FairSharedMutex::active_reader_count() const noexcept {
    std::lock_guard lockGuard{mutex};

    return activeReaderCount;
}

usize FairSharedMutex::waiting_writer_count() const noexcept {
    std::lock_guard lockGuard{mutex};

    return waitingWriterCount;
}

}  // namespace DON
