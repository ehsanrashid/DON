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

#ifndef UTILITY_QUEUE_H_INCLUDED
#define UTILITY_QUEUE_H_INCLUDED

#include <thread>

#include "../utility/types.h"

namespace DON {

// A bounded single-producer, single-consumer (SPSC) ring buffer.
//
// The producer calls push(); it returns false if the queue is full.
// The consumer calls pop(); it returns false if the queue is empty.
// Exactly one thread may call push(), and exactly one thread may call pop().
// No mutex or dynamic allocation is used by push() or pop().
// Requires std::atomic<u32> to be always lock-free on the target.
template<typename T, u32 Capacity>
class SingleProducerSingleConsumerQueue final {
    static_assert(Capacity != 0,
                  "SingleProducerSingleConsumerQueue capacity must be greater than zero");
    static_assert(is_power_of_2(Capacity),
                  "SingleProducerSingleConsumerQueue capacity must be a power of two");
    static_assert(Capacity <= (u32(1) << 31),
                  "SingleProducerSingleConsumerQueue capacity must not exceed 2^31");
    static_assert(std::atomic<u32>::is_always_lock_free,
                  "SingleProducerSingleConsumerQueue queue requires lock-free u32 atomics");

   public:
    SingleProducerSingleConsumerQueue() noexcept                                = default;
    SingleProducerSingleConsumerQueue(const SingleProducerSingleConsumerQueue&) = delete;
    SingleProducerSingleConsumerQueue& operator=(const SingleProducerSingleConsumerQueue&) = delete;
    SingleProducerSingleConsumerQueue(SingleProducerSingleConsumerQueue&&)                 = delete;
    SingleProducerSingleConsumerQueue& operator=(SingleProducerSingleConsumerQueue&&)      = delete;

    static constexpr u32 capacity() noexcept { return Capacity; }

    // Called only by the single producer.
    // Returns false if the queue is full.
    bool push(const T& item) noexcept {
        const u32 head = head_.load(std::memory_order_relaxed);
        const u32 tail = tail_.load(std::memory_order_acquire);

        // Queue is full.
        if (head - tail >= Capacity)
            return false;

        buffer_[head & Mask] = item;

        // Publish the written item to the consumer.
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Called only by the single consumer.
    // Returns false if the queue is empty.
    bool pop(T& item) noexcept {
        const u32 tail = tail_.load(std::memory_order_relaxed);
        const u32 head = head_.load(std::memory_order_acquire);

        // Queue is empty.
        if (tail == head)
            return false;

        item = std::move(buffer_[tail & Mask]);

        // Publish the released slot to the producer.
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Observational queries; concurrent calls may see transient states.
    bool empty() const noexcept {
        const u32 tail = tail_.load(std::memory_order_relaxed);
        const u32 head = head_.load(std::memory_order_acquire);

        return tail == head;
    }

    bool full() const noexcept {
        const u32 head = head_.load(std::memory_order_relaxed);
        const u32 tail = tail_.load(std::memory_order_acquire);

        return head - tail >= Capacity;
    }

    // Returns the observed number of queued items; may be transient under concurrency.
    u32 size() const noexcept {
        const u32 head = head_.load(std::memory_order_acquire);
        const u32 tail = tail_.load(std::memory_order_acquire);

        return head - tail;
    }

   private:
    static constexpr u32 Mask = Capacity - 1;

    alignas(CACHE_LINE_SIZE) std::atomic<u32> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<u32> tail_{0};

    alignas(CACHE_LINE_SIZE) Array<T, Capacity> buffer_{};
};

}  // namespace DON

#endif  // UTILITY_QUEUE_H_INCLUDED
