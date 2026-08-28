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

#include "tt.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <system_error>
#include <numeric>
#include <vector>

#include "memory.h"
#include "thread.h"

namespace DON {

namespace {

// Pv, bound and generation are packed in a single byte
constexpr u8 GENERATION_BITS = 5;
constexpr u8 GENERATION_MASK = (u8{1} << GENERATION_BITS) - 1;
constexpr u8 BOUND_SHIFT     = GENERATION_BITS;
constexpr u8 BOUND_MASK      = u8{3 << BOUND_SHIFT};
constexpr u8 PV_SHIFT        = BOUND_SHIFT + 2;
constexpr u8 PV_MASK         = u8{1 << PV_SHIFT};

}  // namespace

// TTEntry is the 10 bytes transposition table entry
// Defined as below:
// key          16 bit
// move         16 bit
// value        16 bit
// evalValue    16 bit
// depth         8 bit
// meta          8 bit
//  - pv         1 bit
//  - bound      2 bit
//  - generation 5 bit
//
// These fields are in the same order as accessed by TT::probe(), since memory is fastest sequentially.
// Equally, the store order in save() matches this order.
struct TTEntry final {
   public:
    [[nodiscard]] u16   key() const noexcept { return key16; }
    [[nodiscard]] bool  occupied() const noexcept { return depth8 != 0; }
    [[nodiscard]] Depth depth() const noexcept { return depth8 + DEPTH_OFFSET; }
    [[nodiscard]] Move  move() const noexcept { return move16; }
    [[nodiscard]] Value value() const noexcept { return value16; }
    [[nodiscard]] Value evalue() const noexcept { return evalue16; }
    [[nodiscard]] Bound bound() const noexcept {
        return Bound((meta8 & BOUND_MASK) >> BOUND_SHIFT);
    }
    [[nodiscard]] bool pv() const noexcept { return ((meta8 & PV_MASK) /* >> PV_SHIFT*/) != 0; }
    [[nodiscard]] u8   generation() const noexcept { return meta8 & GENERATION_MASK; }

    TTData read() const noexcept;

    u8 relative_age(u8 gen) const noexcept;

    i16 worth(u8 gen) const noexcept;

    void save(u16 k, Move m, Value v, Value ev, Depth d, Bound b, bool pv, u8 gen) noexcept;

    void penalize(u8 penalty) noexcept;

    void reset() noexcept;

   private:
    TTEntry() noexcept                          = delete;
    TTEntry(const TTEntry&) noexcept            = delete;
    TTEntry& operator=(const TTEntry&) noexcept = delete;
    TTEntry(TTEntry&&) noexcept                 = delete;
    TTEntry& operator=(TTEntry&&) noexcept      = delete;

    RelaxedAtomic<u16>   key16;
    RelaxedAtomic<Move>  move16;
    RelaxedAtomic<Value> value16;
    RelaxedAtomic<Value> evalue16;
    RelaxedAtomic<u8>    depth8;
    RelaxedAtomic<u8>    meta8;
};

static_assert(sizeof(TTEntry) == 10, "TTEntry size must be 10 bytes");

// Convert internal bit fields to TTData
TTData TTEntry::read() const noexcept {
    return {move(), value(), evalue(), depth(), bound(), occupied(), pv()};
}

u8 TTEntry::relative_age(const u8 gen) const noexcept {
    // Returns this entry's age. Count generations like clocks count hours,
    // i.e. require 0 - 1 == 31. Unsigned subtraction guarantees the required
    // borrowing regardless of the upper pv/bound bits.
    return (gen - meta8) & GENERATION_MASK;
}

i16 TTEntry::worth(const u8 gen) const noexcept { return depth8 - 8 * relative_age(gen); }

// Populates the TTEntry with a new node's data, possibly overwriting an old position.
// The update is non-atomic and can be racy.
void TTEntry::save(const u16   k,
                   const Move  m,
                   const Value v,
                   const Value ev,
                   const Depth d,
                   const Bound b,
                   const bool  pv,
                   const u8    gen) noexcept {
    assert(d > DEPTH_OFFSET);
    assert(d <= DEPTH_OFFSET + 0xFF);
    assert(gen <= GENERATION_MASK);

    // Preserve the old move if don't have a new one
    if (key() != k || m != Move::None)
        move16 = m;

    // Overwrite less valuable entries (cheapest checks first)
    if (key() != k || b == Bound::EXACT || depth() < 4 + d + int(pv) * 2 || relative_age(gen) != 0)
    {
        key16    = k;
        value16  = v;
        evalue16 = ev;
        depth8   = d - DEPTH_OFFSET;
        meta8    = static_cast<u8>(pv) << PV_SHIFT | static_cast<u8>(b) << BOUND_SHIFT | gen;
    }
    // Secondary aging. Important for elementary mate finding.
    // (*Scaler) Secondary aging on entries relevant to singular extensions
    // generally scales poorly and requires VVLTC verification.
    else if (depth() > 4 && bound() != Bound::EXACT)
    {
        const auto val = value();
        if (constexpr_abs(val) < VALUE_INFINITE && is_decisive(val))
            penalize(1);
    }
}

// Decrement the stored depth by the penalty, clamping at zero
void TTEntry::penalize(const u8 penalty) noexcept {
    // Guard against racy underflows, default to "unoccupied"
    depth8 = std::max(depth8 - penalty, 0);
}

// Reset all entry fields to zero
void TTEntry::reset() noexcept { std::memset(static_cast<void*>(this), 0, sizeof(*this)); }


TTData TTData::empty() noexcept {
    return {Move::None, VALUE_NONE, VALUE_NONE, DEPTH_OFFSET, Bound::NONE, false, false};
}

// TTCluster consists of a bunch of TTEntry.
// TTCluster size should divide the size of a cache-line for best performance,
// as the cache-line is prefetched when possible.
struct TTCluster final {
   public:
    Array<TTEntry, 3> entries;
    Array<char, 2>    padding;  // Pad to 32 bytes

   private:
    TTCluster() noexcept                            = delete;
    TTCluster(const TTCluster&) noexcept            = delete;
    TTCluster& operator=(const TTCluster&) noexcept = delete;
    TTCluster(TTCluster&&) noexcept                 = delete;
    TTCluster& operator=(TTCluster&&) noexcept      = delete;
};

static_assert(sizeof(TTCluster) == 32, "TTCluster size must be 32 bytes");

TTWriter::TTWriter(TTEntry* const te, TTCluster* const tc, const u16 k, const u8 gen) noexcept :
    tte(te),
    ttc(tc),
    key(k),
    generation(gen) {}

void TTWriter::write(const Move  m,
                     const Value v,
                     const Value ev,
                     const Depth d,
                     const Bound b,
                     const bool  pv) noexcept {
    for (auto* fte = ttc->entries.data(); tte != fte && (tte - 1)->key() == key; --tte)
        tte->reset();

    tte->save(key, m, v, ev, d, b, pv, generation);
}

void TTWriter::penalize(const u8 penalty) noexcept { tte->penalize(penalty); }


TranspositionTable::~TranspositionTable() noexcept { free(); }

void TranspositionTable::free() noexcept {
    [[maybe_unused]] bool success = free_aligned_large_page(clusters);
    assert(success);
}

u8 TranspositionTable::generation() const noexcept { return generation8; }

void TranspositionTable::advance_generation() const noexcept {
    ++generation8;
    // Wrap generation within its mask
    generation8 &= GENERATION_MASK;
}

// Sets the size of the transposition table, measured in megabytes (MB).
// Transposition table consists of even number of clusters.
void TranspositionTable::resize(const usize ttSize, const Threads& threads) noexcept {
    constexpr usize ClusterSize = sizeof(TTCluster);

    free();

    clusterCount = ttSize * MB / ClusterSize;
    //DEBUG_LOG("Clustering transposition table to " << clusterCount << " clusters.");

    const usize ttBytes = clusterCount * ClusterSize;

    // Request 1GB pages if we'd get at least eight per NUMA node, to avoid
    // memory oversubscription
    const bool hugePageHint = ttBytes >= 8 * threads.numa_nodes() * HUGE_PAGE_SIZE;

    clusters = static_cast<TTCluster*>(alloc_aligned_large_page_with_hint(ttBytes, hugePageHint));

    if (clusters == nullptr)
    {
        DEBUG_LOG("Failed to allocate transposition table for " << ttSize << "MB.");
        std::exit(EXIT_FAILURE);
    }

    reset(threads);
}

// Resets the entire transposition table to zero, in a multi-threaded way
void TranspositionTable::reset(const Threads& threads) noexcept {
    generation8 = 0;

    const usize threadCount = threads.size();

    auto threadBoundNumaNodes = threads.thread_bound_numa_nodes();

    std::vector<size_t> orderedThreads(threadCount);
    std::iota(orderedThreads.begin(), orderedThreads.end(), 0);

    // To promote good NUMA distribution (esp. with huge pages), we permute threads so that
    // all threads in a NUMA node clear a contiguous region of the TT.
    if (threadBoundNumaNodes.size() == threadCount)
    {
        std::stable_sort(orderedThreads.begin(), orderedThreads.end(),
                         [&threadBoundNumaNodes](const usize t1, const usize t2) noexcept -> bool {
                             return threadBoundNumaNodes.at(t1) < threadBoundNumaNodes.at(t2);
                         });
    }

    for (usize threadId = 0; threadId < threadCount; ++threadId)
    {
        threads.run_on_thread(orderedThreads[threadId], [this, threadId, threadCount]() {
            // Each thread will zero its part of the hash table
            const auto [beg, end] = split_range(threadId, threadCount, clusterCount);

            std::memset(static_cast<void*>(&clusters[beg]), 0, (end - beg) * sizeof(TTCluster));
        });
    }

    for (usize threadId = 0; threadId < threadCount; ++threadId)
        threads.wait_on_thread(orderedThreads[threadId]);
}

TTCluster* TranspositionTable::cluster(const Key key) const noexcept {
    return &clusters[static_cast<usize>(mul_hi64(key, clusterCount))];
}

// `probe` is the primary method: looks up the current position (key) in the transposition table.
// On a hit, it returns:
//   1) copy of the existing data (which may be a collision or self-inconsistent due to read races)
//   2) writer for the corresponding entry
// On a miss, it returns empty data and writer for the least valuable entry selected for replacement.
ProbResult TranspositionTable::probe(const Key key) const noexcept {

    auto* const ttc = cluster(key);

    const u16 key16 = static_cast<u16>(key);

    for (const auto& entry : ttc->entries)
        if (entry.key() == key16)
            return {entry.read(), TTWriter{const_cast<TTEntry*>(&entry), ttc, key16, generation8}};

    // Find an entry to be replaced according to the replacement strategy
    const auto* rte = ttc->entries.data();

    for (usize i = 1; i < ttc->entries.size(); ++i)
        if (rte->worth(generation8) > ttc->entries[i].worth(generation8))
            rte = &ttc->entries[i];

    return {TTData::empty(), TTWriter{const_cast<TTEntry*>(rte), ttc, key16, generation8}};
}

// Returns an approximation of the hash table occupation during a search.
// The hash is x per mill full, as per UCI protocol.
// Only counts entries which match the current generation. [maxAge: 0-GENERATION_MASK]
u16 TranspositionTable::hashfull(const u8 maxAge) const noexcept {
    assert(maxAge <= GENERATION_MASK);

    constexpr usize RequiredCount = 1000;

    const usize ActualCount = std::min(RequiredCount, clusterCount);

    u32 count = 0;

    for (usize idx = 0; idx < ActualCount; ++idx)
        for (const auto& entry : clusters[idx].entries)
            count += entry.occupied() && entry.relative_age(generation8) <= maxAge;

    // Normalize per entries per cluster
    return ceil_div(count * RequiredCount, ActualCount) / clusters->entries.size();
}

bool TranspositionTable::load(const std::filesystem::path& hashFile,
                              const Threads&               threads) noexcept {

    if (hashFile.empty())
    {
        //DEBUG_LOG("No Hash file provided");
        return false;
    }

    std::error_code ec;

    usize fileSize = std::filesystem::file_size(hashFile, ec);

    if (ec)
    {
        //DEBUG_LOG("Failed to stat Hash file " << hashFile << ": " << ec.message());
        return false;
    }

    if (fileSize == 0)
    {
        //DEBUG_LOG("Warning: Empty Hash file " << hashFile);
        return true;
    }

    std::ifstream ifs{hashFile, std::ios::binary};

    if (!ifs)
    {
        //DEBUG_LOG("Failed to open Hash file " << hashFile);
        return false;
    }

    usize ttSize = fileSize / MB;

    resize(ttSize, threads);

    constexpr usize ClusterSize = sizeof(TTCluster);
    static_assert(ClusterSize > 0, "Cluster must have non-zero size");

    // Choose a chunk that balances system call overhead and memory pressure.
    // 2 MiB is a safe default; 4-64 MiB may be slightly faster on fast disks.
    constexpr usize ChunkSize = (2 * MB / ClusterSize) * ClusterSize;

    usize DataSize = clusterCount * ClusterSize;

    auto* data = reinterpret_cast<char*>(clusters);

    usize readedSize = 0;

    while (readedSize < DataSize)
    {
        std::streamsize readSize = std::min(ChunkSize, DataSize - readedSize);

        ifs.read(data + readedSize, readSize);

        std::streamsize gotSize = ifs.gcount();

        if (gotSize <= 0)  // read failed or EOF without data
            return false;

        //if (gotSize != readSize)  // partial read - treat as error for complete-file read
        //{
        //    //DEBUG_LOG("Partial read: expected " << readSize << " got " << gotSize);
        //    return false;
        //}

        readedSize += gotSize;
    }

    if (ifs.fail() || ifs.bad())
    {
        //DEBUG_LOG("I/O error while reading Hash file " << hashFile);
        return false;
    }

    return readedSize == DataSize && ifs.good();
}

bool TranspositionTable::save(const std::filesystem::path& hashFile) const noexcept {

    if (hashFile.empty())
    {
        //DEBUG_LOG("No Hash file provided");
        return false;
    }

    std::ofstream ofs{hashFile, std::ios::binary};

    if (!ofs)
    {
        //DEBUG_LOG("Failed to open Hash file " << hashFile);
        return false;
    }

    constexpr usize ClusterSize = sizeof(TTCluster);
    static_assert(ClusterSize > 0, "Cluster must have non-zero size");

    // Choose a chunk that balances system call overhead and memory pressure.
    // 2 MiB is a safe default; 4-64 MiB may be slightly faster on fast disks.
    constexpr usize ChunkSize = (2 * MB / ClusterSize) * ClusterSize;

    usize DataSize = clusterCount * ClusterSize;

    const auto* data = reinterpret_cast<const char*>(clusters);

    usize writtenSize = 0;

    while (writtenSize < DataSize)
    {
        std::streamsize writeSize = std::min(ChunkSize, DataSize - writtenSize);

        ofs.write(data + writtenSize, writeSize);

        if (!ofs)  // write failed
            return false;

        writtenSize += writeSize;
    }

    ofs.flush();

    return writtenSize == DataSize && ofs.good();
}

}  // namespace DON
