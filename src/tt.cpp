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
#include <numeric>
#include <system_error>
#include <vector>

#include "memory.h"
#include "misc.h"
#include "thread.h"
#include "utility/atomic.h"

namespace DON {

namespace {

// Pv, bound and generation are packed in a single byte
constexpr u8 GENERATION_BITS = 5;
constexpr u8 GENERATION_MASK = (u8{1} << GENERATION_BITS) - 1;
constexpr u8 BOUND_SHIFT     = GENERATION_BITS;
constexpr u8 BOUND_MASK      = u8{3} << BOUND_SHIFT;
constexpr u8 PV_SHIFT        = BOUND_SHIFT + 2;
constexpr u8 PV_MASK         = u8{1} << PV_SHIFT;

//constexpr int KEY_BITS = 21;
//constexpr u32 KEY_MASK = (u64{1} << KEY_BITS) - 1;

constexpr u16 compress_key(const Key key) noexcept {
    return ((key >> 00) & u16{0xFFFF})  //
         ^ ((key >> 16) & u16{0x0FFF})  //
         ^ ((key >> 32) & u16{0x00FF})  //
         ^ ((key >> 48) & u16{0x000F});
}

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
    TTEntry() noexcept                          = delete;
    TTEntry(const TTEntry&) noexcept            = delete;
    TTEntry& operator=(const TTEntry&) noexcept = delete;
    TTEntry(TTEntry&&) noexcept                 = delete;
    TTEntry& operator=(TTEntry&&) noexcept      = delete;

    // clang-format off
    [[nodiscard]] u16   key() const noexcept { return key16; }
    [[nodiscard]] bool  occupied() const noexcept { return depth8 != 0; }
    [[nodiscard]] Depth depth() const noexcept { return depth8 + DEPTH_OFFSET; }
    [[nodiscard]] Move  move() const noexcept { return move16; }
    [[nodiscard]] Value value() const noexcept { return value16; }
    [[nodiscard]] Value evalue() const noexcept { return evalue16; }
    [[nodiscard]] Bound bound() const noexcept { return Bound((meta8 & BOUND_MASK) >> BOUND_SHIFT); }
    [[nodiscard]] bool pv() const noexcept { return ((meta8 & PV_MASK) /* >> PV_SHIFT*/) != 0; }
    [[nodiscard]] u8   generation() const noexcept { return meta8 & GENERATION_MASK; }
    // clang-format on

    TTData read() const noexcept;

    u8 relative_age(u8 gen) const noexcept;

    i16 worth(u8 gen) const noexcept;

    void save(u16 k, Move m, Value v, Value ev, Depth d, Bound b, bool pv, u8 gen) noexcept;

    void penalize(u8 penalty) noexcept;

    void reset() noexcept;

   private:
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
// The update is non-atomic and can be racy. Convert external types to internal bitfields.
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
    if (key() != k || m.is_ok())
        move16 = m;

    // Overwrite less valuable entries (cheapest checks first)
    if (key() != k || b == Bound::EXACT || depth() < 4 + d + int(pv) * 2 || relative_age(gen) != 0)
    {
        key16    = k;
        value16  = v;
        evalue16 = ev;
        depth8   = d - DEPTH_OFFSET;
        meta8    = u8(pv) << PV_SHIFT | u8(b) << BOUND_SHIFT | gen;
    }
    // Secondary aging. Important for elementary mate finding.
    // (*Scaler) Secondary aging on entries relevant to singular extensions
    // generally scales poorly and requires VVLTC verification.
    else if (depth() > 4 && bound() != Bound::EXACT)
    {
        const auto val = value();
        if (-VALUE_INFINITE < val && val < +VALUE_INFINITE && is_decisive(val))
            penalize(1);
    }
}

// Decrement the stored depth by the penalty, clamping at DEPTH_ZERO
void TTEntry::penalize(const u8 penalty) noexcept {
    // Guard against racy underflows, default to "unoccupied"
    depth8 = std::max(depth8 - penalty, 0);
}

// Reset all entry fields to zero
void TTEntry::reset() noexcept { std::memset(static_cast<void*>(this), 0, sizeof(*this)); }


TTData TTData::empty() noexcept {
    return {Move::None, VALUE_NONE, VALUE_NONE, DEPTH_OFFSET, Bound::NONE, false, false};
}

TTData::TTData(const Move  m,
               const Value v,
               const Value ev,
               const Depth d,
               const Bound b,
               const bool  hit_,
               const bool  pv_) noexcept :
    move(m),
    value(v),
    evalue(ev),
    depth(d),
    bound(b),
    hit(hit_),
    pv(pv_) {}

// TTCluster consists of a bunch of TTEntry.
// TTCluster size should divide the size of a cache-line for best performance,
// as the cache-line is prefetched when possible.
struct TTCluster final {
   public:
    TTCluster() noexcept                            = delete;
    TTCluster(const TTCluster&) noexcept            = delete;
    TTCluster& operator=(const TTCluster&) noexcept = delete;
    TTCluster(TTCluster&&) noexcept                 = delete;
    TTCluster& operator=(TTCluster&&) noexcept      = delete;

    static constexpr u8 EntryCount = 3;

    Array<TTEntry, EntryCount> entries;
    Array<char, 2>             padding;  // Pad to 32 bytes
};

constexpr usize TT_CLUSTER_SIZE = sizeof(TTCluster);
static_assert(TT_CLUSTER_SIZE == 32, "TTCluster size must be 32 bytes");

TTWriter::TTWriter(TTCluster* const tc, const u8 idx, const u16 k, const u8 gen) noexcept :
    ttc(tc),
    index(idx),
    key(k),
    generation(gen) {}

// Find an entry to be replaced according to the replacement strategy
u8 TTWriter::prepare_replacement() const noexcept {
    u8 replacement = 0;

    for (u8 i = 1; i < TTCluster::EntryCount; ++i)
        if (ttc->entries[replacement].worth(generation) > ttc->entries[i].worth(generation))
            replacement = i;

    for (; replacement != 0 && ttc->entries[replacement - 1].key() == key; --replacement)
        ttc->entries[replacement].reset();

    return replacement;
}

void TTWriter::write(const Move  m,
                     const Value v,
                     const Value ev,
                     const Depth d,
                     const Bound b,
                     const bool  pv) noexcept {
    if (index >= TTCluster::EntryCount)
        index = prepare_replacement();

    ttc->entries[index].save(key, m, v, ev, d, b, pv, generation);
}

void TTWriter::penalize(const u8 penalty) noexcept {
    assert(index < TTCluster::EntryCount);

    ttc->entries[index].penalize(penalty);
}

TranspositionTable::~TranspositionTable() noexcept { free(); }

void TranspositionTable::free() noexcept {
    [[maybe_unused]] const bool freed = free_aligned_large_page(clusters);
    assert(freed);
}

u8 TranspositionTable::generation() const noexcept { return generation8; }

void TranspositionTable::advance_generation() const noexcept {
    ++generation8;
    // Wrap generation within its mask
    generation8 &= GENERATION_MASK;
}

// Returns the size of the transposition table, measured in megabytes
usize TranspositionTable::size() const noexcept {
    assert(clusterCount * TT_CLUSTER_SIZE % MB == 0);

    return clusterCount * TT_CLUSTER_SIZE / MB;
}

void TranspositionTable::resize(const usize ttSize, const Threads& threads) noexcept {
    free();

    clusterCount = ttSize * MB / TT_CLUSTER_SIZE;
    //DEBUG_LOG("Clustering transposition table to " << clusterCount << " clusters.");

    const usize ttBytes = clusterCount * TT_CLUSTER_SIZE;

    // Request 1GB pages if get at least eight per NUMA node, to avoid memory oversubscription
    const bool hugePageHint = ttBytes >= usize{8} * threads.numa_nodes() * HUGE_PAGE_SIZE;

    clusters = static_cast<TTCluster*>(alloc_aligned_large_page_with_hint(ttBytes, hugePageHint));

    if (clusters == nullptr)
    {
        DEBUG_LOG("Failed to allocate transposition table for " << ttSize << "MB.");
        std::exit(EXIT_FAILURE);
    }

    reset(threads);
}

void TranspositionTable::reset(const Threads& threads) noexcept {
    generation8 = 0;

    const auto threadCount = threads.size();

    auto threadBoundNumaNodes = threads.thread_bound_numa_nodes();

    std::vector<usize> orderedThreads(usize{threadCount});
    std::iota(orderedThreads.begin(), orderedThreads.end(), 0);

    // To promote good NUMA distribution (esp. with huge pages), we permute threads so that
    // all threads in a NUMA node clear a contiguous region of the TT.
    if (threadBoundNumaNodes.size() == usize{threadCount})
        std::stable_sort(orderedThreads.begin(), orderedThreads.end(),
                         [&threadBoundNumaNodes](const usize t1, const usize t2) noexcept -> bool {
                             return threadBoundNumaNodes.at(t1) < threadBoundNumaNodes.at(t2);
                         });

    for (u16 threadId = 0; threadId < threadCount; ++threadId)
    {
        threads.run_on_thread(orderedThreads[threadId], [this, threadId, threadCount]() {
            // Each thread will zero its part of the hash table
            const auto [beg, end] = split_range(threadId, threadCount, clusterCount);

            std::memset(static_cast<void*>(&clusters[beg]), 0, (end - beg) * TT_CLUSTER_SIZE);
        });
    }

    for (u16 threadId = 0; threadId < threadCount; ++threadId)
        threads.wait_on_thread(orderedThreads[threadId]);
}

TTCluster* TranspositionTable::cluster(const Key key) const noexcept {
    return &clusters[static_cast<usize>(mul_hi64(key, clusterCount))];
}

ProbResult TranspositionTable::probe(const Key key) const noexcept {

    auto* const ttc = cluster(key);

    const u16 key16 = compress_key(key);

    for (u8 i = 0; i < TTCluster::EntryCount; ++i)
        if (ttc->entries[i].key() == key16)
            return {ttc->entries[i].read(), TTWriter{ttc, i, key16, generation8}};

    return {TTData::empty(), TTWriter{ttc, TTCluster::EntryCount, key16, generation8}};
}

u16 TranspositionTable::hashfull(const u8 maxAge) const noexcept {
    assert(maxAge <= GENERATION_MASK);

    constexpr usize RequiredCount = 1000;

    const usize ActualCount = std::min(RequiredCount, clusterCount);

    u32 count = 0;

    for (usize idx = 0; idx < ActualCount; ++idx)
        for (const auto& entry : clusters[idx].entries)
            count += entry.occupied() && entry.relative_age(generation8) <= maxAge;

    // Normalize per entries per cluster
    return u16(ceil_div(count * RequiredCount, ActualCount) / TTCluster::EntryCount);
}

bool TranspositionTable::load(const fs::path& hashPath, const Threads& threads) noexcept {

    if (hashPath.empty())
    {
        //DEBUG_LOG("No Hash file provided");
        return false;
    }

    std::error_code ec;
    const usize     fileSize = fs::file_size(hashPath, ec);
    if (ec)
    {
        //DEBUG_LOG("Failed to stat Hash file " << hashPath << ": " << ec.message());
        return false;
    }

    if (fileSize == 0)
    {
        //DEBUG_LOG("Warning: Empty Hash file " << hashPath);
        return true;
    }

    std::ifstream ifs{hashPath, std::ios::binary};

    if (!ifs)
    {
        //DEBUG_LOG("Failed to open Hash file " << hashPath);
        return false;
    }

    usize ttSize = fileSize / MB;

    resize(ttSize, threads);

    static_assert(TT_CLUSTER_SIZE > 0, "Cluster must have non-zero size");

    // Choose a chunk that balances system call overhead and memory pressure.
    // 2 MiB is a safe default; 4-64 MiB may be slightly faster on fast disks.
    constexpr usize ChunkSize = (2 * MB / TT_CLUSTER_SIZE) * TT_CLUSTER_SIZE;

    const usize dataSize = size() * MB;

    auto* data = reinterpret_cast<char*>(clusters);

    usize readedSize = 0;

    while (readedSize < dataSize)
    {
        std::streamsize readSize = std::min(ChunkSize, dataSize - readedSize);

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

    return readedSize == dataSize && ifs.good();
}

bool TranspositionTable::save(const fs::path& hashPath) const noexcept {

    if (hashPath.empty())
    {
        //DEBUG_LOG("No Hash file provided");
        return false;
    }

    std::ofstream ofs{hashPath, std::ios::binary};

    if (!ofs)
    {
        //DEBUG_LOG("Failed to open Hash file " << hashPath);
        return false;
    }

    static_assert(TT_CLUSTER_SIZE > 0, "Cluster must have non-zero size");

    // Choose a chunk that balances system call overhead and memory pressure.
    // 2 MiB is a safe default; 4-64 MiB may be slightly faster on fast disks.
    constexpr usize ChunkSize = (2 * MB / TT_CLUSTER_SIZE) * TT_CLUSTER_SIZE;

    const usize dataSize = size() * MB;

    const auto* data = reinterpret_cast<const char*>(clusters);

    usize writtenSize = 0;

    while (writtenSize < dataSize)
    {
        std::streamsize writeSize = std::min(ChunkSize, dataSize - writtenSize);

        ofs.write(data + writtenSize, writeSize);

        if (!ofs)  // write failed
            return false;

        writtenSize += writeSize;
    }

    ofs.flush();

    return writtenSize == dataSize && ofs.good();
}

}  // namespace DON
