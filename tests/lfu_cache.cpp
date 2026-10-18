/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -DNDEBUG -I./src \
    src/utility/cpu.cpp \
    src/utility/mutex.cpp \
    tests/lfu_cache.cpp \
    -o build/lfu_cache
g++ -std=c++17 -O2 -I./src \
    src/utility/cpu.cpp \
    src/utility/mutex.cpp \
    tests/lfu_cache.cpp \
    -o build/lfu_cache
g++ -std=c++17 -O2 -I./src `
    src/utility/cpu.cpp `
    src/utility/mutex.cpp `
    tests/lfu_cache.cpp `
    -o build/lfu_cache

./build/lfu_cache
*/

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

#include "utility/lfu_cache.h"
#include "utility/mutex.h"
#include "utility/types.h"

using namespace DON;

#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cerr << "FAILED: " #condition << " (line " << __LINE__ << ")\n"; \
            return EXIT_FAILURE; \
        } \
    } while (false)

namespace {

using Clock = std::chrono::steady_clock;

constexpr float MaxLoadFactor = 0.75F;

constexpr int Capacity         = 256;
constexpr int RandomOperations = 80'000;

// Keep Option A's benchmark manageable because list promotion is O(n).
constexpr usize BenchmarkCapacity   = 2 * KB;
constexpr usize BenchmarkOperations = 1200'000;

struct LargeValue {
    std::array<std::uint8_t, 256> data{};
    int id = 0;

    explicit LargeValue(const int value = 0) noexcept :
        id(value) {
        data[0] = static_cast<std::uint8_t>(value);
    }
};

template<typename Cache>
int correctness_tests() {
    Cache cache{3, 8};

    CHECK(cache.empty());
    CHECK(cache.size() == 0);
    CHECK(cache.get_capacity() == 3);

    // Insert three entries, all with frequency 1.
    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.read_or_write(3, 30) == 30);

    // Promote key 2 to frequency 2.
    CHECK(cache.read_or_write(2, 200) == 20);

    // Key 1 is the least-recently-used entry among frequency-1 entries.
    CHECK(cache.read_or_write(4, 40) == 40);
    CHECK(cache.contains(2));
    CHECK(cache.contains(3));
    CHECK(cache.contains(4));
    CHECK(!cache.contains(1));

    // Promote key 3 to frequency 2.
    CHECK(cache.read_or_write(3, 300) == 30);

    // Key 4 has frequency 1, so it is evicted before keys 2 and 3.
    CHECK(cache.read_or_write(5, 50) == 50);
    CHECK(cache.contains(2));
    CHECK(cache.contains(3));
    CHECK(cache.contains(5));
    CHECK(!cache.contains(4));
    CHECK(cache.size() == 3);

    // Removal.
    CHECK(cache.remove(2));
    CHECK(!cache.remove(2));
    CHECK(!cache.contains(2));
    CHECK(cache.size() == 2);

    // Builder runs only on a cache miss.
    int buildCount = 0;
    const auto builder = [&buildCount](const int value) {
        ++buildCount;
        return value * 10;
    };

    CHECK(cache.read_or_write_with(6, builder, 7) == 70);
    CHECK(buildCount == 1);
    CHECK(cache.read_or_write_with(6, builder, 9) == 70);
    CHECK(buildCount == 1);

    // Explicit eviction retains the highest-frequency entry.
    cache.evict_excess(1);
    CHECK(cache.size() == 1);
    CHECK(cache.contains(6));

    // Increase capacity and insert more entries.
    cache.set_capacity(5);
    CHECK(cache.get_capacity() == 5);
    CHECK(cache.read_or_write(7, 70) == 70);
    CHECK(cache.read_or_write(8, 80) == 80);
    CHECK(cache.size() == 3);

    // Reducing capacity retains the highest-frequency entry.
    cache.set_capacity(1);
    CHECK(cache.get_capacity() == 1);
    CHECK(cache.size() == 1);
    CHECK(cache.contains(6));

    // Zero capacity: values are returned but not retained.
    cache.set_capacity(0);
    CHECK(cache.empty());
    CHECK(cache.read_or_write(9, 90) == 90);
    CHECK(!cache.contains(9));
    CHECK(cache.read_or_write_with(10, builder, 11) == 110);
    CHECK(!cache.contains(10));
    CHECK(buildCount == 2);

    // Clear and reuse.
    cache.set_capacity(3);
    CHECK(cache.read_or_write(11, 110) == 110);
    CHECK(cache.read_or_write(12, 120) == 120);
    cache.clear();
    CHECK(cache.empty());
    CHECK(!cache.contains(11));
    CHECK(!cache.contains(12));

    CHECK(cache.read_or_write(13, 130) == 130);
    cache.reset();
    CHECK(cache.empty());
    CHECK(cache.get_capacity() == 3);

    std::cout << "Basic LFU tests passed.\n";
    return EXIT_SUCCESS;
}

template<typename Cache>
int frequency_tie_tests() {
    Cache cache{2, 8};

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);

    // Key 1 becomes more frequent.
    CHECK(cache.read_or_write(1, 100) == 10);

    // Key 2 must be evicted because it has the lower frequency.
    CHECK(cache.read_or_write(3, 30) == 30);
    CHECK(cache.contains(1));
    CHECK(cache.contains(3));
    CHECK(!cache.contains(2));

    cache.clear();

    // Equal-frequency entries: the oldest entry must be evicted.
    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.read_or_write(3, 30) == 30);

    CHECK(!cache.contains(1));
    CHECK(cache.contains(2));
    CHECK(cache.contains(3));

    std::cout << "Frequency and LRU-tie tests passed.\n";
    return EXIT_SUCCESS;
}

template<typename Cache>
int large_value_tests() {
    Cache cache{4, 8, MaxLoadFactor};

    CHECK(cache.read_or_write(1, 101).id == 101);
    CHECK(cache.read_or_write(2, 202).id == 202);
    CHECK(cache.read_or_write(1, 999).id == 101);
    CHECK(cache.size() == 2);
    CHECK(cache.remove(1));
    CHECK(cache.read_or_write(1, 303).id == 303);

    std::cout << "Large-value tests passed.\n";
    return EXIT_SUCCESS;
}

template<typename Cache>
int growth_tests() {
    constexpr int EntryCount = 270'000;

    Cache cache{EntryCount, 16, MaxLoadFactor};

    //std::cerr << "Before insertion loop\n";

    for (int key = 0; key < EntryCount; ++key)
        (void) cache.read_or_write(key, key);

    //std::cerr << "After insertion loop\n";

    CHECK(cache.size() == EntryCount);
    //std::cerr << "After size check\n";

    CHECK(cache.read_or_write(EntryCount - 1, -1) == EntryCount - 1);
    //std::cerr << "After read check\n";

    CHECK(cache.contains(0));
    //std::cerr << "After contains check\n";

    cache.clear();
    //std::cerr << "After clear\n";

    std::cout << "Growth test passed.\n";
    return EXIT_SUCCESS;
}

template<typename Cache>
double benchmark_case(const char* name, const std::vector<int>& keys, const bool prefill) {
    Cache cache{BenchmarkCapacity, BenchmarkCapacity / 4, MaxLoadFactor};

    if (prefill)
    {
        for (usize i = 0; i < BenchmarkCapacity; ++i)
            (void) cache.read_or_write(static_cast<int>(i), static_cast<int>(i));
    }

    std::uint64_t checksum = 0;

    const auto start = Clock::now();

    for (const int key : keys)
        checksum += static_cast<std::uint64_t>(
          cache.read_or_write(key, key ^ 0x5A5A));

    const auto stop = Clock::now();

    const double seconds =
      std::chrono::duration<double>(stop - start).count();
    const double nsPerOperation =
      seconds * 1'000'000'000.0 / static_cast<double>(keys.size());
    const double millionOpsPerSecond =
      static_cast<double>(keys.size()) / seconds / 1'000'000.0;

    std::cout << "  " << name << ": " << nsPerOperation << " ns/op, "
              << millionOpsPerSecond << " Mops/s"
              << " (checksum=" << checksum << ")\n";

    return nsPerOperation;
}

template<typename Cache>
void benchmark_cache(const char* cacheName,
                     const std::vector<int>& hitKeys,
                     const std::vector<int>& missKeys,
                     const std::vector<int>& mixedKeys) {
    std::cout << cacheName << '\n';
    benchmark_case<Cache>("hits", hitKeys, true);
    benchmark_case<Cache>("misses + eviction", missKeys, false);
    benchmark_case<Cache>("mixed workload", mixedKeys, false);
}

}  // namespace

int main() {
    //using ListCache      = ListLFUCache<int, int, SpinMutex>;
    //using LargeListCache = ListLFUCache<int, LargeValue, SpinMutex>;

    using SetCache      = SetLFUCache<int, int, SpinMutex>;
    using LargeSetCache = SetLFUCache<int, LargeValue, SpinMutex>;

    using BucketCache      = BucketLFUCache<int, int, SpinMutex>;
    using LargeBucketCache = BucketLFUCache<int, LargeValue, SpinMutex>;

    using PoolCache      = PoolLFUCache<int, int, SpinMutex>;
    using LargePoolCache = PoolLFUCache<int, LargeValue, SpinMutex>;

    using FlatCache      = PoolFlatLFUCache<int, int, SpinMutex>;
    using LargeFlatCache = PoolFlatLFUCache<int, LargeValue, SpinMutex>;

    //if (correctness_tests<ListCache>() != EXIT_SUCCESS)
    //    return EXIT_FAILURE;

    if (correctness_tests<SetCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (correctness_tests<BucketCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (correctness_tests<PoolCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (correctness_tests<FlatCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    // --------------------------------------------------
    //if (frequency_tie_tests<ListCache>() != EXIT_SUCCESS)
    //    return EXIT_FAILURE;

    if (frequency_tie_tests<SetCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (frequency_tie_tests<BucketCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (frequency_tie_tests<PoolCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (frequency_tie_tests<FlatCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    // --------------------------------------------------
    //if (large_value_tests<LargeListCache>() != EXIT_SUCCESS)
    //    return EXIT_FAILURE;

    if (large_value_tests<LargeSetCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (large_value_tests<LargeBucketCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (large_value_tests<LargePoolCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (large_value_tests<LargeFlatCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    // --------------------------------------------------
    //if (growth_tests<ListCache>() != EXIT_SUCCESS)
    //    return EXIT_FAILURE;

    if (growth_tests<SetCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (growth_tests<BucketCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (growth_tests<PoolCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (growth_tests<FlatCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    std::cout << "\nAll correctness tests passed.\n\n";

    std::vector<int> hitKeys;
    std::vector<int> missKeys;
    std::vector<int> mixedKeys;

    hitKeys.reserve(BenchmarkOperations);
    missKeys.reserve(BenchmarkOperations);
    mixedKeys.reserve(BenchmarkOperations);

    std::mt19937 rng{0xB3A5C};

    for (usize i = 0; i < BenchmarkOperations; ++i)
    {
        hitKeys.push_back(static_cast<int>(i % BenchmarkCapacity));
        missKeys.push_back(static_cast<int>(BenchmarkCapacity + i));
        mixedKeys.push_back(static_cast<int>(rng() % (BenchmarkCapacity * 2)));
    }

    std::cout << "Benchmark: " << BenchmarkOperations
              << " operations per workload\n"
              << "Capacity: " << BenchmarkCapacity << '\n'
              << "Lower ns/op is better; higher Mops/s is better.\n\n";

    //benchmark_cache<ListCache>("ListLFUCache", hitKeys, missKeys, mixedKeys);

    //std::cout << '\n';

    benchmark_cache<SetCache>("SetLFUCache", hitKeys, missKeys, mixedKeys);

    std::cout << '\n';

    benchmark_cache<BucketCache>("BucketLFUCache", hitKeys, missKeys, mixedKeys);

    std::cout << '\n';

    benchmark_cache<PoolCache>("PoolLFUCache", hitKeys, missKeys, mixedKeys);

    std::cout << '\n';

    benchmark_cache<FlatCache>("FlatLFUCache", hitKeys, missKeys, mixedKeys);

    std::cout << "\nDone!\n";
    return EXIT_SUCCESS;
}
