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

// Sets the size of the transposition table, measured in megabytes.
// Transposition table consists of even number of clusters.
void TranspositionTable::resize(std::size_t mbSize, ThreadPool& threads) noexcept {
    free();
    clusterCount = mbSize * 1024 * 1024 / sizeof(TTCluster);
    assert(clusterCount % 2 == 0);

    clusters = static_cast<TTCluster*>(alloc_aligned_lp(clusterCount * sizeof(TTCluster)));
    if (!clusters)
    {
        std::cerr << "Failed to allocate " << mbSize << "MB for transposition table.\n";
        std::exit(EXIT_FAILURE);
    }

    init(threads);
}

// Initializes the entire transposition table to zero, in a multi-threaded way.
void TranspositionTable::init(ThreadPool& threads) noexcept {
    generation8 = 0;

    const std::uint16_t threadCount = threads.size();

    for (std::uint16_t threadId = 0; threadId < threadCount; ++threadId)
    {
        threads.run_on_thread(threadId, [this, threadId, threadCount]() {
            // Each thread will zero its part of the hash table
            std::size_t stride = clusterCount / threadCount;
            std::size_t start  = stride * threadId;
            std::size_t count  = 1 + threadId != threadCount ? stride : clusterCount - start;

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
TTProbe TranspositionTable::probe(const Key key, const Key16 key16) const noexcept {

    TTEntry* const fte = first_entry(key);

    for (std::uint8_t i = 0; i < TT_CLUSTER_ENTRY_COUNT; ++i)
    {
        TTEntry* cte = fte + i;
        // Use the compress 16 bits as key inside the cluster
        if (key16 == cte->key16)
            return {cte->occupied(), cte, fte};
    }
    return {false, fte, fte};
}

// Returns an approximation of the hashtable occupation during a search.
// The hash is x permill full, as per UCI protocol.
// Only counts entries which match the current generation.
std::uint16_t TranspositionTable::hashfull() const noexcept {
    std::uint32_t cnt = 0;
    for (std::size_t idx = 0; idx < std::min(clusterCount, std::size_t(1000)); ++idx)
        for (const auto& tte : clusters[idx].entry)
            cnt += tte.occupied() && tte.generation() == generation8;

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
        std::size_t mbSize = fileSize / (1024 * 1024);
        resize(mbSize, threads);
        ifstream.read(reinterpret_cast<char*>(clusters), clusterCount * sizeof(TTCluster));
    }
    return ifstream.good();
}

}  // namespace DON
