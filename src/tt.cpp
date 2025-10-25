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

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

#include "memory.h"
#include "misc.h"
#include "thread.h"

namespace DON {

// Constants used to manipulate generation bits

// Number of bits reserved for data
constexpr std::uint8_t DATA_BITS = 3;
// Increment for generation field
constexpr std::uint8_t GENERATION_DELTA = 1 << DATA_BITS;
// Mask to pull out generation field
constexpr std::uint8_t GENERATION_MASK = (0xFF << DATA_BITS) & 0xFF;
// Generation cycle length
constexpr std::uint16_t GENERATION_CYCLE = 0xFF + GENERATION_DELTA;

// TTEntry struct is the 10 bytes transposition table entry, defined as below:
//
// key        16 bit
// depth       8 bit
// generation  5 bit
// data        3 bit
//  - pv       1 bit
//  - bound    2 bit
// move       16 bit
// value      16 bit
// eval       16 bit
//
// These fields are in the same order as accessed by TT::probe(), since memory is fastest sequentially.
// Equally, the store order in save() matches this order.
struct TTEntry final {
   private:
    TTEntry() noexcept                          = delete;
    TTEntry(const TTEntry&) noexcept            = delete;
    TTEntry(TTEntry&&) noexcept                 = delete;
    TTEntry& operator=(const TTEntry&) noexcept = delete;
    TTEntry& operator=(TTEntry&&) noexcept      = delete;

    constexpr auto move() const noexcept { return move16; }
    constexpr auto occupied() const noexcept { return bool(depth8); }
    constexpr auto depth() const noexcept { return Depth(depth8 + DEPTH_OFFSET); }
    constexpr auto pv_hit() const noexcept { return bool(genData8 & 0x4); }
    constexpr auto bound() const noexcept { return Bound(genData8 & 0x3); }
    //constexpr auto generation() const noexcept { return std::uint8_t(genData8 & GENERATION_MASK); }
    constexpr auto value() const noexcept { return value16; }
    constexpr auto eval() const noexcept { return eval16; }

    // Convert internal bitfields to TTData
    TTData read() const noexcept {
        return {occupied(), pv_hit(), bound(), depth(), move(), value(), eval()};
    }

    // Populates the TTEntry with a new node's data, possibly
    // overwriting an old position. The update is not atomic and can be racy.
    void save(
      Key16 k16, Depth d, bool pv, Bound b, Move m, Value v, Value ev, std::uint8_t gen) noexcept {
        assert(d > DEPTH_OFFSET);
        assert(d <= std::numeric_limits<std::uint8_t>::max() + DEPTH_OFFSET);

        // Preserve the old move if don't have a new one
        if (key16 != k16 || m != Move::None)
            move16 = m;
        // Overwrite less valuable entries (cheapest checks first)
        if (key16 != k16 || b == BOUND_EXACT  //
            || depth() < 4 + d + 2 * pv       //
            || relative_age(gen))
        {
            key16    = k16;
            depth8   = d - DEPTH_OFFSET;
            genData8 = gen | (pv << 2) | b;
            value16  = v;
            eval16   = ev;
        }
        else if (depth() > 4 && bound() != BOUND_EXACT)
            --depth8;
    }

    void clear() noexcept { std::memset(static_cast<void*>(this), 0, sizeof(*this)); }

    // The returned age is a multiple of GENERATION_DELTA
    std::uint8_t relative_age(std::uint8_t gen) const noexcept {
        // Due to packed storage format for generation and its cyclic nature
        // add GENERATION_CYCLE (256 is the modulus, plus what is needed to keep
        // the unrelated lowest n bits from affecting the relative age)
        // to calculate the entry age correctly even after gen overflows into the next cycle.
        return (GENERATION_CYCLE + gen - genData8) & GENERATION_MASK;
    }

    std::int16_t worth(std::uint8_t gen) const noexcept { return depth8 - relative_age(gen); }

    Key16        key16;
    Move         move16;
    std::uint8_t depth8;
    std::uint8_t genData8;
    Value        value16;
    Value        eval16;

    friend class TTUpdater;
    friend class TranspositionTable;
};

static_assert(sizeof(TTEntry) == 10, "Unexpected TTEntry size");

// TTCluster consists of EntryCount number of TTEntry.
// The size of a TTCluster should divide the size of a cache-line for best performance,
// as the cache-line is prefetched when possible.
struct TTCluster final {
   public:
    static constexpr std::size_t EntryCount = 3;

    TTEntry entries[EntryCount];
    char    padding[2];  // Pad to 32 bytes
};

static_assert(sizeof(TTCluster) == 32, "Unexpected TTCluster size");

void TTUpdater::update(Depth d, bool pv, Bound b, Move m, Value v, Value ev) noexcept {
    for (; tte != &ttc->entries[0] && (tte - 1)->key16 == key16; --tte)
        tte->clear();

    tte->save(key16, d, pv, b, m, v, ev, generation);
}

TranspositionTable::~TranspositionTable() noexcept { free(); }

void TranspositionTable::free() noexcept { free_aligned_lp(clusters); }

void TranspositionTable::increment_generation() noexcept { generation8 += GENERATION_DELTA; }

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

TTCluster* TranspositionTable::cluster(Key key) const noexcept {
    return &clusters[mul_hi64(key, clusterCount)];
}

// Looks up the current position (key) in the transposition table.
// It returns pointer to the TTEntry if the position is found.
ProbResult TranspositionTable::probe(Key key) const noexcept {

    auto* const ttc   = cluster(key);
    Key16       key16 = compress_key16(key);

    for (auto& entry : ttc->entries)
        if (entry.key16 == key16)
            return {entry.read(), TTUpdater{&entry, ttc, key16, generation8}};

    // Find an entry to be replaced according to the replacement strategy
    auto* rte = &ttc->entries[0];
    for (std::size_t i = 1; i < TTCluster::EntryCount; ++i)
        if (rte->worth(generation8) > ttc->entries[i].worth(generation8))
            rte = &ttc->entries[i];

    return {TTData{false, false, BOUND_NONE, DEPTH_OFFSET, Move::None, VALUE_NONE, VALUE_NONE},
            TTUpdater{rte, ttc, key16, generation8}};
}

// Prefetch the cache line which includes this key's entry
void TranspositionTable::prefetch_key(Key key) const noexcept { prefetch(cluster(key)); }

// Returns an approximation of the hashtable occupation during a search.
// The hash is x permill full, as per UCI protocol.
// Only counts entries which match the current generation. [maxAge: 0-31]
std::uint16_t TranspositionTable::hashfull(std::uint8_t maxAge) const noexcept {
    assert(maxAge < 32);

    const auto   clusterCnt = std::min(clusterCount, std::size_t(1000));
    std::uint8_t maxRelAge  = maxAge * GENERATION_DELTA;

    std::uint32_t cnt = 0;
    for (std::size_t idx = 0; idx < clusterCnt; ++idx)
        for (const auto& entry : clusters[idx].entries)
            cnt += entry.occupied() && entry.relative_age(generation8) <= maxRelAge;

    return cnt / TTCluster::EntryCount;
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
