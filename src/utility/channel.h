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

#ifndef UTILITY_CHANNEL_H_INCLUDED
#define UTILITY_CHANNEL_H_INCLUDED

#include <thread>

#include "../utility/queue.h"
#include "../utility/types.h"

namespace DON {

// A single-producer, single-consumer (SPSC) channel backed by a bounded queue.
//
// send() returns false if the queue is full.
// receive_loop() processes items until should_run() returns false.
// Exactly one thread may call send(), and exactly one thread may consume items.
// No mutex or dynamic allocation is used by the channel itself.
template<typename T, u32 Capacity>
class SingleProducerSingleConsumerChannel final {
   public:
    SingleProducerSingleConsumerChannel() noexcept = default;

    static constexpr u32 capacity() noexcept { return Capacity; }

    // Called only by the single producer.
    // Returns false if the queue is full.
    bool send(const T& item) noexcept { return queue_.push(item); }

    // Called only by the single consumer.
    // Processes items until should_run() returns false.
    template<typename Handler, typename ShouldRun>
    void receive_loop(Handler&& handler, ShouldRun&& should_run) noexcept {
        T item;

        while (should_run())
        {
            if (queue_.pop(item))
                handler(item);
            else
                std::this_thread::yield();
        }
    }

   private:
    // clang-format off
    SingleProducerSingleConsumerChannel(const SingleProducerSingleConsumerChannel&)            = delete;
    SingleProducerSingleConsumerChannel& operator=(const SingleProducerSingleConsumerChannel&) = delete;
    SingleProducerSingleConsumerChannel(SingleProducerSingleConsumerChannel&&)                 = delete;
    SingleProducerSingleConsumerChannel& operator=(SingleProducerSingleConsumerChannel&&)      = delete;
    // clang-format on

    SingleProducerSingleConsumerQueue<T, Capacity> queue_;
};


// Usage:
//
//// Example packet type.
//struct Packet {
//    u32 id   = 0;
//    u32 size = 0;
//};
//
//// Example packet handler.
//void handle(const Packet& packet) noexcept {
//    // Process the packet.
//}
//
//// Example consumer stop condition.
//bool running() noexcept {
//    return true;  // Replace with the actual stop condition.
//}
//
//void example() {
//    SingleProducerSingleConsumerChannel<Packet, 1024> channel;
//
//    // Producer: call from exactly one producer thread.
//    const Packet packet{1, 64};
//
//    if (!channel.send(packet))
//    {
//        // Handle queue overflow.
//    }
//
//    // Consumer: call from a separate consumer thread.
//    channel.receive_loop([](const Packet& packet) noexcept { handle(packet); },
//                         []() noexcept { return running(); });
//}

}  // namespace DON

#endif  // UTILITY_CHANNEL_H_INCLUDED
