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

namespace DON {

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

                    std::this_thread::sleep_for(std::chrono::microseconds(SleepTimeUs));
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
    bool wakeWriter;
    {
        std::lock_guard lockGuard{mutex};

        wakeWriter = --activeReaderCount == 0;
    }

    if (wakeWriter)
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
    bool wakeWriter;
    {
        std::lock_guard lockGuard{mutex};

        writerActive = false;
        wakeWriter   = waitingWriterCount != 0;
    }

    if (wakeWriter)
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
    bool wakeWriter;
    {
        std::lock_guard lockGuard{mutex};

        wakeWriter = --activeReaderCount == 0;
    }

    if (wakeWriter)
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
    writer(writer_) {}

bool FairSharedMutex::Waiter::is_writer() const noexcept { return writer; }

void FairSharedMutex::lock() noexcept {
    Waiter writer(true);

    std::unique_lock uniqueLock{mutex};

    ++waitingWriterCount;
    (void) waitList.push_back(writer.node, writer);

    commonCondVar.wait(uniqueLock, [this, &writer]() noexcept -> bool { return can_lock(writer); });

    (void) waitList.erase(writer.node);
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

        writerActive = false;
    }

    // Wake all waiters to recheck their eligibility
    commonCondVar.notify_all();
}

void FairSharedMutex::lock_shared() noexcept {
    Waiter reader(false);

    std::unique_lock uniqueLock{mutex};

    (void) waitList.push_back(reader.node, reader);

    commonCondVar.wait(uniqueLock,
                       [this, &reader]() noexcept -> bool { return can_lock_shared(reader); });

    (void) waitList.erase(reader.node);

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
    for (auto* cur = waitList.front(); cur != nullptr; cur = waitList.next(*cur))
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

// ------------------

}  // namespace DON
