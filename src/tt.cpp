/*
  DON, a UCI chess playing engine derived from Stockfish

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

#include "tt.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "memory.h"
#include "thread.h"

namespace DON {

TranspositionTable::~TranspositionTable() noexcept { free(); }

void TranspositionTable::free() noexcept { free_aligned_lp(clusters); }

// Sets the size of the transposition table, measured in megabytes (MB).
// Transposition table consists of even number of clusters.
void TranspositionTable::resize(std::size_t ttSize, ThreadPool& threads) noexcept {
    free();

    clusterCount = ttSize * 1024 * 1024 / sizeof(TTCluster);
    assert(clusterCount % 2 == 0);

    clusters = static_cast<TTCluster*>(alloc_aligned_lp(clusterCount * sizeof(TTCluster)));
    if (clusters == nullptr)
    {
        std::cerr << "Failed to allocate " << ttSize << "MB for transposition table." << std::endl;
        std::exit(EXIT_FAILURE);
    }

    init(threads);
}

// Initializes the entire transposition table to zero, in a multi-threaded way.
void TranspositionTable::init(ThreadPool& threads) noexcept {
    generation8 = 0;
    hashFull.store(0, std::memory_order_relaxed);

    std::size_t threadCount = threads.size();

    for (std::size_t threadId = 0; threadId < threadCount; ++threadId)
    {
        threads.run_on_thread(threadId, [this, threadId, threadCount]() {
            // Each thread will zero its part of the hash table
            std::size_t stride = clusterCount / threadCount;
            std::size_t remain = clusterCount % threadCount;

            std::size_t start = stride * threadId + std::min(threadId, remain);
            std::size_t count = stride + (threadId < remain);

            std::memset(static_cast<void*>(&clusters[start]), 0, count * sizeof(TTCluster));
        });
    }

    for (std::size_t threadId = 0; threadId < threadCount; ++threadId)
        threads.wait_on_thread(threadId);
}

// Looks up the current position (key) in the transposition table.
// It returns pointer to the TTEntry if the position is found.
ProbResult TranspositionTable::probe(Key key, Key16 key16) const noexcept {

    auto* const ttc = cluster(key);

    for (auto& entry : ttc->entry)
        if (entry.key16 == key16)
            return {entry.read(), &entry, ttc};

    return {{false, false, BOUND_NONE, Move::None, DEPTH_OFFSET, VALUE_NONE, VALUE_NONE},
            &ttc->entry[0],
            ttc};
}

ProbResult TranspositionTable::probe(Key key) const noexcept {
    return probe(key, compress_key16(key));
}

// Returns an approximation of the hashtable occupation during a search.
// The hash is x permill full, as per UCI protocol.
// Only counts entries which match the current generation. [maxAge: 0-31]
std::uint16_t TranspositionTable::hashfull(std::uint8_t maxAge) const noexcept {
    assert(maxAge < 32);

    const auto   clusterCnt = std::min(clusterCount, std::size_t(1000));
    std::uint8_t maxRelAge  = maxAge * GENERATION_DELTA;

    std::uint32_t cnt = 0;
    for (std::size_t idx = 0; idx < clusterCnt; ++idx)
        for (const auto& entry : clusters[idx].entry)
            cnt += entry.occupied() && entry.relative_age(generation8) <= maxRelAge;

    return (cnt + TTCluster::EntryCount / 2) / TTCluster::EntryCount;
}

std::uint16_t TranspositionTable::hashfull() noexcept {
    hashFull.store(hashfull(0), std::memory_order_relaxed);
    return hashFull.load(std::memory_order_relaxed);
}

bool TranspositionTable::save(std::string_view hashFile) const noexcept {

    if (hashFile.empty())
        return false;

    std::ofstream ofstream(std::string(hashFile), std::ios_base::binary);
    if (ofstream)
    {
        ofstream.write(reinterpret_cast<const char*>(clusters), clusterCount * sizeof(TTCluster));
    }
    return ofstream.good();
}

bool TranspositionTable::load(std::string_view hashFile, ThreadPool& threads) noexcept {

    if (hashFile.empty())
        return false;

    std::ifstream ifstream(std::string(hashFile), std::ios_base::binary);
    if (ifstream)
    {
        auto fileSize = get_file_size(ifstream);
        if (fileSize < 0)
            return false;
        std::size_t ttSize = fileSize / (1024 * 1024);
        resize(ttSize, threads);
        ifstream.read(reinterpret_cast<char*>(clusters), clusterCount * sizeof(TTCluster));
    }
    return ifstream.good();
}

}  // namespace DON
