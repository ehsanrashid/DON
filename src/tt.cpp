/*
  DON, a UCI chess playing engine derived from Glaurung 2.1

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

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#include "misc.h"

namespace DON {

// Populates the TTEntry with a new node's data, possibly
// overwriting an old position. The update is not atomic and can be racy.
void TTEntry::save(
  Key k, Value v, bool pv, Bound b, Depth d, const Move& m, Value ev, std::uint8_t gen) noexcept {

    // Preserve any existing move
    if (m)
        move16 = m.raw();

    // Overwrite less valuable entries (cheapest checks first)
    if (b == BOUND_EXACT || std::uint16_t(k) != key16 || d - DEPTH_OFFSET + 2 * pv > depth8 - 4)
    {
        assert(d > DEPTH_OFFSET);
        assert(d <= std::numeric_limits<std::uint8_t>::max() + DEPTH_OFFSET);

        key16     = std::uint16_t(k);
        depth8    = std::uint8_t(d - DEPTH_OFFSET);
        genBound8 = std::uint8_t(gen | std::uint8_t(pv) << 2 | b);
        value16   = std::int16_t(v);
        eval16    = std::int16_t(ev);
    }
}

std::uint8_t TTEntry::relative_age(std::uint8_t gen) const noexcept {
    // Due to our packed storage format for generation and its cyclic
    // nature we add GENERATION_CYCLE (256 is the modulus, plus what
    // is needed to keep the unrelated lowest n bits from affecting
    // the result) to calculate the entry age correctly even after
    // generation8 overflows into the next cycle.

    return (TranspositionTable::GENERATION_CYCLE + gen - genBound8)
         & TranspositionTable::GENERATION_MASK;
}

TranspositionTable::~TranspositionTable() noexcept { free(); }

void TranspositionTable::free() noexcept {
    aligned_large_pages_free(table);
    table        = nullptr;
    clusterCount = 0;
    generation8  = 0;
}

// Sets the size of the transposition table, measured in megabytes.
// Transposition table consists of even number of clusters and
// each cluster consists of EntryCount number of TTEntry.
void TranspositionTable::resize(std::size_t mbSize, std::uint16_t threadCount) noexcept {
    free();

    clusterCount = mbSize * 1024 * 1024 / sizeof(Cluster);
    assert(clusterCount % 2 == 0);

    table = static_cast<Cluster*>(aligned_large_pages_alloc(clusterCount * sizeof(Cluster)));
    if (!table)
    {
        std::cerr << "Failed to allocate " << mbSize << "MB for transposition table.\n";
        exit(EXIT_FAILURE);
    }

    clear(threadCount);
}

// Initializes the entire transposition table to zero,
// in a multi-threaded way.
void TranspositionTable::clear(std::uint16_t threadCount) noexcept {
    std::vector<std::thread> threads;

    for (std::uint16_t idx = 0; idx < threadCount; ++idx)
    {
        threads.emplace_back([this, idx, threadCount]() {
            // Thread binding gives faster search on systems with a first-touch policy
            if (threadCount > 8)
                WinProcGroup::bind_thread(idx);

            // Each thread will zero its part of the hash table
            const std::size_t stride = clusterCount / threadCount;
            const std::size_t start  = stride * idx;
            const std::size_t count  = idx != threadCount - 1 ? stride : clusterCount - start;

            std::memset(static_cast<void*>(&table[start]), 0, count * sizeof(Cluster));
        });
    }

    for (auto& th : threads)
        th.join();
}

// Looks up the current position in the transposition table.
// It returns true and a pointer to the TTEntry if the position is found.
// Otherwise, it returns false and a pointer to an empty or least valuable TTEntry
// to be replaced later. The replacement value of an entry is calculated as its depth
// minus 2 times its relative age. TTEntry t1 is considered more valuable than TTEntry t2
// if replacement value of t1 is greater than that of t2.
TTEntry* TranspositionTable::probe(Key key, bool& found) const noexcept {

    TTEntry* const tte = first_entry(key);
    // Use the low 16 bits as key inside the cluster
    const auto key16 = std::uint16_t(key);

    for (std::uint8_t i = 0; i < EntryCount; ++i)
        if (tte[i].key16 == key16 || !tte[i].depth8)
        {
            // Refresh with new generation, keeping the lower bits the same.
            tte[i].genBound8 = generation8 | (tte[i].genBound8 & (GENERATION_DELTA - 1));

            return found = tte[i].depth8, &tte[i];
        }

    // Find an entry to be replaced according to the replacement strategy
    TTEntry*     replace  = tte;
    std::int16_t minWorth = tte->depth8 - 2 * tte->relative_age(generation8);
    for (std::uint8_t i = 1; i < EntryCount; ++i)
    {
        std::int16_t worth = tte[i].depth8 - 2 * tte[i].relative_age(generation8);
        if (minWorth > worth)
        {
            minWorth = worth;
            replace  = &tte[i];
        }
    }

    return found = false, replace;
}

// Returns an approximation of the hashtable occupation during a search.
// The hash is x permill full, as per UCI protocol.
// Only counts entries which match the current generation.
std::uint16_t TranspositionTable::hashfull() const noexcept {
    std::uint32_t cnt = 0;
    for (std::size_t t = 0; t < std::min<std::size_t>(clusterCount, 1000); ++t)
        for (const TTEntry& tte : table[t].entry)
            cnt += tte.depth8 && (tte.genBound8 & GENERATION_MASK) == generation8;

    return cnt / EntryCount;
}

bool TranspositionTable::save(const std::string& fname) const noexcept {
    std::ofstream ofstream(fname, std::ios_base::binary);
    if (ofstream)
        ofstream.write(reinterpret_cast<const char*>(table), clusterCount * sizeof(Cluster));
    return ofstream.good();
}

bool TranspositionTable::load(const std::string& fname) noexcept {
    std::ifstream ifstream(fname, std::ios_base::binary);
    if (ifstream)
    {
        ifstream.seekg(0, std::ios_base::end);
        std::streamsize fileSize = ifstream.tellg();
        ifstream.seekg(0, std::ios_base::beg);
        std::size_t mbSize = fileSize / (1024 * 1024);
        resize(mbSize, 4);
        ifstream.read(reinterpret_cast<char*>(table), clusterCount * sizeof(Cluster));
    }
    return ifstream.good();
}

}  // namespace DON
