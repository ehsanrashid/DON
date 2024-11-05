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
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>

#include "memory.h"
#include "thread.h"

namespace DON {

TranspositionTable::~TranspositionTable() noexcept { free(); }

void TranspositionTable::free() noexcept {
    free_aligned_lp(clusters);
    clusters     = nullptr;
    clusterCount = 0;
    generation8  = 0;
}

// Sets the size of the transposition table, measured in megabytes (MB).
// Transposition table consists of even number of clusters.
void TranspositionTable::resize(std::size_t ttSize, ThreadPool& threads) noexcept {

    constexpr std::size_t ClusterSize = sizeof(TTCluster);

    std::size_t newClusterCount = ttSize * 1024 * 1024 / ClusterSize;
    assert(newClusterCount % 2 == 0);

    if (clusterCount != newClusterCount)
    {
        free();

        clusterCount = newClusterCount;

        clusters = static_cast<TTCluster*>(alloc_aligned_lp(clusterCount * ClusterSize));
        if (clusters == nullptr)
        {
            clusterCount = 0;
            std::cerr << "Failed to allocate " << ttSize << "MB for transposition table.\n";
            std::exit(EXIT_FAILURE);
        }
    }

    init(threads);
}

// Initializes the entire transposition table to zero, in a multi-threaded way.
void TranspositionTable::init(ThreadPool& threads) noexcept {
    generation8 = 0;

    auto threadCount = threads.size();

    for (std::uint16_t threadId = 0; threadId < threadCount; ++threadId)
    {
        threads.run_on_thread(threadId, [this, threadId, threadCount]() {
            // Each thread will zero its part of the hash table
            std::size_t stride = clusterCount / threadCount;
            std::size_t start  = stride * threadId;
            std::size_t count  = threadId != threadCount - 1 ? stride : clusterCount - start;

            std::memset(static_cast<void*>(&clusters[start]), 0, count * sizeof(TTCluster));
        });
    }

    for (std::uint16_t threadId = 0; threadId < threadCount; ++threadId)
        threads.wait_on_thread(threadId);
}

// Looks up the current position in the transposition table.
// It returns true and a pointer to the TTEntry if the position is found.
// Otherwise, it returns false and a pointer to an empty or least valuable TTEntry.
// TTEntry t1 is considered more valuable than TTEntry t2
// if replacement value of t1 is greater than that of t2.
TTProbe TranspositionTable::probe(Key key, Key16 key16) const noexcept {

    TTEntry* const fte = first_entry(key);

    //TTCluster* ttc = reinterpret_cast<TTCluster*>(fte);

    for (std::uint8_t i = 0; i < TT_CLUSTER_ENTRY_COUNT; ++i)
        if (fte[i].key16 == key16)
            return {fte[i].occupied(), &fte[i], fte};

    return {false, fte, fte};
}

// Returns an approximation of the hashtable occupation during a search.
// The hash is x permill full, as per UCI protocol.
// Only counts entries which match the current generation.
std::uint16_t TranspositionTable::hashfull(std::uint16_t maxAge) const noexcept {

    std::uint16_t maxRelAge = maxAge << DATA_BITS;

    std::uint32_t cnt = 0;
    for (std::size_t idx = 0; idx < std::min<size_t>(clusterCount, 1000); ++idx)
        for (std::uint8_t i = 0; i < TT_CLUSTER_ENTRY_COUNT; ++i)
        {
            const auto& entry = clusters[idx].entry[i];
            cnt += entry.occupied() && entry.relative_age(generation8) <= maxRelAge;
        }
    return cnt / TT_CLUSTER_ENTRY_COUNT;
}

bool TranspositionTable::save(const std::string& hashFile) const noexcept {

    if (is_empty(hashFile))
        return false;
    std::ofstream ofstream(hashFile, std::ios_base::binary);
    if (ofstream)
        ofstream.write(reinterpret_cast<const char*>(clusters), clusterCount * sizeof(TTCluster));
    return ofstream.good();
}

bool TranspositionTable::load(const std::string& hashFile, ThreadPool& threads) noexcept {

    if (is_empty(hashFile))
        return false;
    std::ifstream ifstream(hashFile, std::ios_base::binary);
    if (ifstream)
    {
        ifstream.seekg(0, std::ios_base::end);
        std::streamsize fileSize = ifstream.tellg();
        ifstream.seekg(0, std::ios_base::beg);
        std::size_t ttSize = fileSize / (1024 * 1024);
        resize(ttSize, threads);
        ifstream.read(reinterpret_cast<char*>(clusters), clusterCount * sizeof(TTCluster));
    }
    return ifstream.good();
}

}  // namespace DON
