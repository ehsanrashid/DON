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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

#include "memory.h"
#include "misc.h"
#include "thread.h"

namespace DON {

namespace {

// Number of bits reserved for other fields in the data8 byte
constexpr std::uint8_t RESERVED_BITS = 3;
// Increment value for the generation field, used to bump generation
constexpr std::uint8_t GENERATION_DELTA = 1 << RESERVED_BITS;
// Mask to extract the generation field from data8 upper bits only
constexpr std::uint8_t GENERATION_MASK = (0xFF << RESERVED_BITS) & 0xFF;
// Generation cycle length, handles overflow correctly
// Maximum generation value before wrapping around
constexpr std::uint16_t GENERATION_CYCLE = 0xFF + GENERATION_DELTA;

}  // namespace

// TTEntry is the 10 bytes transposition table entry
// Defined as below:
// key          16 bit
// move         16 bit
// depth         8 bit
// data          8 bit
//  - generation 5 bit
//  - pv         1 bit
//  - bound      2 bit
// value        16 bit
// eval         16 bit
//
// These fields are in the same order as accessed by TT::probe(),
// since memory is fastest sequentially.
// Equally, the store order in save() matches this order.
struct TTEntry final {
   private:
    TTEntry() noexcept                          = delete;
    TTEntry(const TTEntry&) noexcept            = delete;
    TTEntry(TTEntry&&) noexcept                 = delete;
    TTEntry& operator=(const TTEntry&) noexcept = delete;
    TTEntry& operator=(TTEntry&&) noexcept      = delete;

    constexpr auto move() const noexcept { return move16; }
    constexpr auto occupied() const noexcept { return depth8 != 0; }
    constexpr auto depth() const noexcept { return Depth(depth8 + DEPTH_OFFSET); }
    constexpr auto pv() const noexcept { return (data8 & 0x4) != 0; }
    constexpr auto bound() const noexcept { return Bound(data8 & 0x3); }
    //constexpr auto generation() const noexcept { return std::uint8_t(data8 & GENERATION_MASK); }
    constexpr auto value() const noexcept { return value16; }
    constexpr auto eval() const noexcept { return eval16; }

   public:
    // Convert internal bitfields to TTData
    TTData read() const noexcept {
        return TTData{move(), value(), eval(), depth(), bound(), occupied(), pv()};
    }

    // The returned age is a multiple of GENERATION_DELTA
    std::uint8_t relative_age(std::uint8_t gen) const noexcept {
        // Due to packed storage format for generation and its cyclic nature
        // add GENERATION_CYCLE (256 is the modulus, plus what is needed to keep
        // the unrelated lowest n bits from affecting the relative age)
        // to calculate the entry age correctly even after gen overflows into the next cycle.
        return (GENERATION_CYCLE + gen - data8) & GENERATION_MASK;
    }

    std::int16_t worth(std::uint8_t gen) const noexcept { return depth8 - relative_age(gen); }

   private:
    // Populates the TTEntry with a new node's data, possibly
    // overwriting an old position. The update is not atomic and can be racy.
    void save(std::uint16_t k,
              Depth         d,
              Move          m,
              bool          pv,
              Bound         b,
              Value         v,
              Value         ev,
              std::uint8_t  gen) noexcept {
        assert(d > DEPTH_OFFSET);
        assert(d <= 0xFF + DEPTH_OFFSET);

        // Preserve the old move if don't have a new one
        if (key16 != k || m != Move::None)
            move16 = m;

        // Overwrite less valuable entries (cheapest checks first)
        if (key16 != k || b == BOUND_EXACT || depth() < 4 + d + 2 * pv || relative_age(gen) != 0)
        {
            key16   = k;
            depth8  = d - DEPTH_OFFSET;
            data8   = gen | (pv << 2) | b;
            value16 = v;
            eval16  = ev;
        }
    }

    void clear() noexcept { std::memset(static_cast<void*>(this), 0, sizeof(*this)); }

    std::uint16_t key16;
    Move          move16;
    std::uint8_t  depth8;
    std::uint8_t  data8;
    Value         value16;
    Value         eval16;

    friend class TTUpdater;
    friend class TranspositionTable;
};

static_assert(sizeof(TTEntry) == 10, "Unexpected TTEntry size");

// TTCluster consists of bunch of TTEntry.
// TTCluster size should divide the size of a cache-line for best performance,
// as the cache-line is prefetched when possible.
struct TTCluster final {
   private:
    TTCluster() noexcept                            = delete;
    TTCluster(const TTCluster&) noexcept            = delete;
    TTCluster(TTCluster&&) noexcept                 = delete;
    TTCluster& operator=(const TTCluster&) noexcept = delete;
    TTCluster& operator=(TTCluster&&) noexcept      = delete;

   public:
    StdArray<TTEntry, 3> entries;
    StdArray<char, 2>    padding;  // Pad to 32 bytes
};

static_assert(sizeof(TTCluster) == 32, "Unexpected TTCluster size");

void TTUpdater::update(Depth d, Move m, bool pv, Bound b, Value v, Value ev) noexcept {
    for (; tte != &ttc->entries[0] && (tte - 1)->key16 == key16; --tte)
        tte->clear();

    tte->save(key16, d, m, pv, b, v, ev, generation);
}

TranspositionTable::~TranspositionTable() noexcept { free(); }

void TranspositionTable::free() noexcept {
    [[maybe_unused]] bool success = free_aligned_large_page(clusters);
    assert(success);
}

void TranspositionTable::increment_generation() noexcept { generation8 += GENERATION_DELTA; }

// Sets the size of the transposition table, measured in megabytes (MB).
// Transposition table consists of even number of clusters.
void TranspositionTable::resize(std::size_t ttSize, Threads& threads) noexcept {
    free();

    clusterCount = ttSize * 1024 * 1024 / sizeof(TTCluster);
    assert(clusterCount % 2 == 0);

    clusters = static_cast<TTCluster*>(alloc_aligned_large_page(clusterCount * sizeof(TTCluster)));

    if (clusters == nullptr)
    {
        std::cerr << "Failed to allocate " << ttSize << "MB for transposition table." << std::endl;
        std::exit(EXIT_FAILURE);
    }

    init(threads);
}

// Initializes the entire transposition table to zero, in a multi-threaded way.
void TranspositionTable::init(Threads& threads) noexcept {
    generation8 = 0;

    const std::size_t threadCount = threads.size();

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

    auto* const         ttc   = cluster(key);
    const std::uint16_t key16 = compress_key16(key);

    for (auto& entry : ttc->entries)
        if (entry.key16 == key16)
            return {entry.read(), TTUpdater{&entry, ttc, key16, generation8}};

    // Find an entry to be replaced according to the replacement strategy
    auto* rte = &ttc->entries[0];
    for (std::size_t i = 1; i < ttc->entries.size(); ++i)
        if (rte->worth(generation8) > ttc->entries[i].worth(generation8))
            rte = &ttc->entries[i];

    return {TTData{Move::None, VALUE_NONE, VALUE_NONE, DEPTH_OFFSET, BOUND_NONE, false, false},
            TTUpdater{rte, ttc, key16, generation8}};
}

// Returns an approximation of the hashtable occupation during a search.
// The hash is x permill full, as per UCI protocol.
// Only counts entries which match the current generation. [maxAge: 0-31]
std::uint16_t TranspositionTable::hashfull(std::uint8_t maxAge) const noexcept {
    assert(maxAge < 32);

    std::size_t  clusterCnt = std::min(clusterCount, std::size_t(1000));
    std::uint8_t relMaxAge  = maxAge * GENERATION_DELTA;

    std::uint32_t cnt = 0;
    for (std::size_t idx = 0; idx < clusterCnt; ++idx)
        for (const auto& entry : clusters[idx].entries)
            cnt += entry.occupied() && entry.relative_age(generation8) <= relMaxAge;

    return cnt / clusters->entries.size();
}

bool TranspositionTable::save(std::string_view hashFile) const noexcept {

    if (hashFile.empty())
    {
        std::cerr << "No Hash file provided" << std::endl;
        return false;
    }

    std::ofstream ofs(std::string(hashFile), std::ios::binary);

    if (!ofs.is_open())
    {
        std::cerr << "Failed to open Hash file " << hashFile << std::endl;
        return false;
    }

    constexpr std::size_t ClusterSize = sizeof(TTCluster);
    static_assert(ClusterSize > 0, "Cluster must have non-zero size");

    // Choose a chunk that balances system call overhead and memory pressure.
    // 2 MiB is a safe default; 4-64 MiB may be slightly faster on fast disks.
    constexpr std::size_t ChunkSize = (2ULL * 1024 * 1024 / ClusterSize) * ClusterSize;

    const std::size_t DataSize = clusterCount * ClusterSize;

    const char* data = reinterpret_cast<const char*>(clusters);

    std::size_t writtenSize = 0;
    while (writtenSize < DataSize)
    {
        std::size_t writeSize = std::min(ChunkSize, DataSize - writtenSize);

        ofs.write(data + writtenSize, std::streamsize(writeSize));

        if (!ofs)  // write failed
            return false;

        writtenSize += writeSize;
    }

    ofs.flush();

    return writtenSize == DataSize && ofs.good();
}

bool TranspositionTable::load(std::string_view hashFile, Threads& threads) noexcept {

    if (hashFile.empty())
    {
        std::cerr << "No Hash file provided" << std::endl;
        return false;
    }

    std::error_code ec;

    std::size_t fileSize = std::filesystem::file_size(std::string(hashFile), ec);

    if (ec)
    {
        std::cerr << "Failed to stat Hash file " << hashFile << ": " << ec.message() << std::endl;
        return false;
    }

    if (fileSize == 0)
    {
        std::cerr << "Warning: Empty Hash file " << hashFile << std::endl;
        return true;
    }

    std::ifstream ifs(std::string(hashFile), std::ios::binary);

    if (!ifs.is_open())
    {
        std::cerr << "Failed to open Hash file " << hashFile << std::endl;
        return false;
    }

    std::size_t ttSize = fileSize / (1024 * 1024);

    resize(ttSize, threads);

    constexpr std::size_t ClusterSize = sizeof(TTCluster);
    static_assert(ClusterSize > 0, "Cluster must have non-zero size");

    // Choose a chunk that balances system call overhead and memory pressure.
    // 2 MiB is a safe default; 4-64 MiB may be slightly faster on fast disks.
    constexpr std::size_t ChunkSize = (2ULL * 1024 * 1024 / ClusterSize) * ClusterSize;

    const std::size_t DataSize = clusterCount * ClusterSize;

    char* data = reinterpret_cast<char*>(clusters);

    std::size_t readedSize = 0;
    while (readedSize < DataSize)
    {
        std::size_t readSize = std::min(ChunkSize, DataSize - readedSize);

        ifs.read(data + readedSize, std::streamsize(readSize));

        std::streamsize gotSize = ifs.gcount();

        if (gotSize <= 0)  // read failed or EOF without data
            return false;

        //if (gotSize != readSize)  // partial read - treat as error for complete-file read
        //{
        //    std::cerr << "Partial read: expected " << readSize << " got " << gotSize << std::endl;
        //    return false;
        //}

        readedSize += gotSize;
    }

    if (ifs.fail() || ifs.bad())
    {
        std::cerr << "I/O error while reading Hash file " << hashFile << std::endl;
        return false;
    }

    return readedSize == DataSize && ifs.good();
}

}  // namespace DON
