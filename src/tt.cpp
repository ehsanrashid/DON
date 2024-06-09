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
    free_aligned_lp(table);
    table        = nullptr;
    clusterCount = 0;
}

// Sets the size of the transposition table, measured in megabytes.
// Transposition table consists of even number of clusters and
// each cluster consists of EntryCount number of TTEntry.
void TranspositionTable::resize(std::size_t mbSize, ThreadPool& threads) noexcept {
    free();
    clusterCount = mbSize * 1024 * 1024 / sizeof(Cluster);
    assert(clusterCount % 2 == 0);

    table = static_cast<Cluster*>(alloc_aligned_lp(clusterCount * sizeof(Cluster)));
    if (!table)
    {
        std::cerr << "Failed to allocate " << mbSize << "MB for transposition table.\n";
        exit(EXIT_FAILURE);
    }

    init(threads);
}

// Initializes the entire transposition table to zero, in a multi-threaded way.
void TranspositionTable::init(ThreadPool& threads) noexcept {
    generation8                     = 0;
    const std::uint16_t threadCount = threads.size();

    for (std::uint16_t idx = 0; idx < threadCount; ++idx)
    {
        threads.run_on_thread(idx, [this, idx, threadCount]() {
            // Each thread will zero its part of the hash table
            std::size_t stride = clusterCount / threadCount;
            std::size_t start  = stride * idx;
            std::size_t count  = 1 + idx != threadCount ? stride : clusterCount - start;

            std::memset(static_cast<void*>(&table[start]), 0, count * sizeof(Cluster));
        });
    }

    for (std::uint16_t idx = 0; idx < threadCount; ++idx)
        threads.wait_on_thread(idx);
}

// Looks up the current position in the transposition table.
// It returns true and a pointer to the TTEntry if the position is found.
// Otherwise, it returns false and a pointer to an empty or least valuable TTEntry
// to be replaced later. The replacement value of an entry is calculated as its depth
// minus 2 times its relative age. TTEntry t1 is considered more valuable than TTEntry t2
// if replacement value of t1 is greater than that of t2.
TTProbe TranspositionTable::probe(Key key) const noexcept {

    auto* const tte = first_entry(key);
    auto*       rte = tte;
    for (std::uint8_t i = 0; i < EntryCount; ++i)
    {
        // Use the low 16 bits as key inside the cluster
        if (Key16(key) == (tte + i)->key16)
            return {(tte + i)->depth8 != 0, (tte + i)};

        // Find an entry to be replaced according to the replacement strategy
        if (i != 0 && rte->worth(generation8) > (tte + i)->worth(generation8))
            rte = (tte + i);
    }
    return {false, rte};
}

// Returns an approximation of the hashtable occupation during a search.
// The hash is x permill full, as per UCI protocol.
// Only counts entries which match the current generation.
std::uint16_t TranspositionTable::hashfull() const noexcept {
    std::uint32_t cnt = 0;
    for (std::size_t idx = 0; idx < std::min<std::size_t>(clusterCount, 1000); ++idx)
        for (const auto& tte : table[idx].entry)
            cnt += tte.depth8 != 0 && (tte.genBound8 & GENERATION_MASK) == generation8;

    return cnt / EntryCount;
}

bool TranspositionTable::save(const std::string& fname) const noexcept {
    std::ofstream ofstream(fname, std::ios_base::binary);
    if (ofstream)
        ofstream.write(reinterpret_cast<const char*>(table), clusterCount * sizeof(Cluster));
    return ofstream.good();
}

bool TranspositionTable::load(const std::string& fname, ThreadPool& threads) noexcept {
    std::ifstream ifstream(fname, std::ios_base::binary);
    if (ifstream)
    {
        ifstream.seekg(0, std::ios_base::end);
        std::streamsize fileSize = ifstream.tellg();
        ifstream.seekg(0, std::ios_base::beg);
        std::size_t mbSize = fileSize / (1024 * 1024);
        resize(mbSize, threads);
        ifstream.read(reinterpret_cast<char*>(table), clusterCount * sizeof(Cluster));
    }
    return ifstream.good();
}

}  // namespace DON
