/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src \
    tests/lru_cache.cpp \
    -o build/lru_cache
g++ -std=c++17 -O2 -I./src \
    tests/lru_cache.cpp \
    -o build/lru_cache
g++ -std=c++17 -O2 -I./src `
    tests/lru_cache.cpp `
    -o build/lru_cache

./build/lru_cache
*/

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

#include "utility/lru_cache.h"

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

constexpr int   Capacity            = 128;
constexpr int   RandomOperations    = 50'000;
constexpr usize BenchmarkCapacity   = 8192;
constexpr usize BenchmarkOperations = 500'000;

// Exercise the shared_ptr storage path (> 128 bytes).
struct LargeValue {
    std::array<std::uint8_t, 256> data{};
    int                           id = 0;

    explicit LargeValue(const int value = 0) noexcept :
        id(value) {
        data[0] = static_cast<std::uint8_t>(value);
    }
};

template<typename Cache>
int correctness_tests() {
    Cache cache{3, 8};

    // Initial state.
    CHECK(cache.empty());
    CHECK(cache.size() == 0);
    CHECK(cache.get_capacity() == 3);

    // Insertions and hits.
    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.read_or_write(3, 30) == 30);
    CHECK(cache.size() == 3);
    CHECK(cache.read_or_write(2, 200) == 20);

    // LRU order: 2, 3, 1 (most to least recently used).
    CHECK(cache.read_or_write(4, 40) == 40);
    CHECK(cache.contains(2));
    CHECK(cache.contains(4));
    CHECK(!cache.contains(1));
    CHECK(cache.size() == 3);

    // Touch 3, then 2 should be evicted by inserting 5.
    CHECK(cache.read_or_write(3, 300) == 30);
    CHECK(cache.read_or_write(5, 50) == 50);
    CHECK(cache.contains(3));
    CHECK(cache.contains(4));
    CHECK(cache.contains(5));
    CHECK(!cache.contains(2));

    // Removal.
    CHECK(cache.remove(4));
    CHECK(!cache.remove(4));
    CHECK(!cache.contains(4));
    CHECK(cache.size() == 2);

    // Builder runs only on a cache miss.
    int        buildCount = 0;
    const auto builder    = [&buildCount](const int value) {
        ++buildCount;
        return value * 10;
    };

    CHECK(cache.read_or_write_with(6, builder, 7) == 70);
    CHECK(buildCount == 1);
    CHECK(cache.read_or_write_with(6, builder, 9) == 70);
    CHECK(buildCount == 1);

    // Explicit eviction keeps the most recently used entries.
    cache.evict_excess(1);
    CHECK(cache.size() == 1);
    CHECK(cache.contains(6));
    CHECK(!cache.contains(3));
    CHECK(!cache.contains(5));

    // Increase capacity and verify insertion.
    cache.set_capacity(5);
    CHECK(cache.get_capacity() == 5);
    CHECK(cache.read_or_write(7, 70) == 70);
    CHECK(cache.read_or_write(8, 80) == 80);
    CHECK(cache.size() == 3);

    // Reducing capacity evicts excess entries.
    cache.set_capacity(1);
    CHECK(cache.get_capacity() == 1);
    CHECK(cache.size() == 1);
    CHECK(cache.contains(8));

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

    std::cout << "Basic tests passed.\n";
    return EXIT_SUCCESS;
}

// Large values exercise shared_ptr-backed storage.
template<typename Cache>
int large_value_tests() {
    Cache cache{4, 8};

    CHECK(cache.read_or_write(1, 101).id == 101);
    CHECK(cache.read_or_write(2, 202).id == 202);
    CHECK(cache.read_or_write(1, 999).id == 101);
    CHECK(cache.size() == 2);
    CHECK(cache.remove(1));
    CHECK(cache.read_or_write(1, 303).id == 303);

    std::cout << "Basic larger tests passed.\n";
    return EXIT_SUCCESS;
}

template<typename CacheA, typename CacheB, typename CacheC>
int randomized_tests() {
    CacheA a{Capacity, Capacity * 2};
    CacheB b{Capacity, Capacity * 2};

    std::mt19937 rng{0xD04CACE};
    int          buildsA = 0;
    int          buildsB = 0;

    for (int i = 0; i < RandomOperations; ++i)
    {
        const int      key       = static_cast<int>(rng() % 256);
        const int      value     = static_cast<int>(rng() % 100000);
        const unsigned operation = rng() % 6;

        switch (operation)
        {
        case 0 :
        case 1 : {
            CHECK(a.read_or_write(key, value) == b.read_or_write(key, value));
            break;
        }
        case 2 : {
            const auto builderA = [&buildsA](const int v) {
                ++buildsA;
                return v * 3 + 1;
            };
            const auto builderB = [&buildsB](const int v) {
                ++buildsB;
                return v * 3 + 1;
            };

            CHECK(a.read_or_write_with(key, builderA, value)
                  == b.read_or_write_with(key, builderB, value));
            CHECK(buildsA == buildsB);
            break;
        }
        case 3 :
            CHECK(a.remove(key) == b.remove(key));
            break;
        case 4 : {
            const usize capacity = rng() % 40;
            a.set_capacity(capacity);
            b.set_capacity(capacity);
            break;
        }
        case 5 : {
            const usize size = rng() % 40;
            a.evict_excess(size);
            b.evict_excess(size);
            break;
        }
        }

        CHECK(a.size() == b.size());
        CHECK(a.empty() == b.empty());

        for (int k = 0; k < 256; ++k)
            CHECK(a.contains(k) == b.contains(k));
    }

    std::cout << "Randomized differential tests passed (" << RandomOperations << " operations).\n";
    return EXIT_SUCCESS;
}

template<typename Cache>
double benchmark_case(const char* name, const std::vector<int>& keys, const bool prefill) {
    Cache cache{BenchmarkCapacity, BenchmarkCapacity * 2};

    if (prefill)
    {
        for (usize i = 0; i < BenchmarkCapacity; ++i)
            (void) cache.read_or_write(static_cast<int>(i), static_cast<int>(i));
    }

    std::uint64_t checksum = 0;

    const auto start = Clock::now();

    for (const int key : keys)
        checksum += static_cast<std::uint64_t>(cache.read_or_write(key, key ^ 0x5A5A));

    const auto stop = Clock::now();

    const double seconds             = std::chrono::duration<double>(stop - start).count();
    const double nsPerOperation      = seconds * 1'000'000'000.0 / static_cast<double>(keys.size());
    const double millionOpsPerSecond = static_cast<double>(keys.size()) / seconds / 1'000'000.0;

    std::cout << "  " << name << ": " << nsPerOperation << " ns/op, " << millionOpsPerSecond
              << " Mops/s"
              << " (checksum=" << checksum << ")\n";

    return nsPerOperation;
}

template<typename Cache>
void benchmark_cache(const char*             cacheName,
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
    using ListCache      = ListLRUCache<int, int>;
    using IntrusiveCache = IntrusiveLRUCache<int, int>;
    using PoolCache      = PoolLRUCache<int, int>;

    using LargeListCache      = ListLRUCache<int, LargeValue>;
    using LargeIntrusiveCache = IntrusiveLRUCache<int, LargeValue>;
    using LargePoolCache      = PoolLRUCache<int, LargeValue>;

    // ------------------------------------------------
    if (correctness_tests<ListCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (correctness_tests<IntrusiveCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (correctness_tests<PoolCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    // ------------------------------------------------
    if (large_value_tests<LargeListCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (large_value_tests<LargeIntrusiveCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (large_value_tests<LargePoolCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    // ------------------------------------------------
    if (randomized_tests<ListCache, IntrusiveCache, PoolCache>() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    std::cout << "\nAll correctness tests passed.\n\n";

    // Generate identical workloads before timing either implementation.
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

    std::cout << "Benchmark: " << BenchmarkOperations << " operations per workload\n"
              << "Lower ns/op is better; higher Mops/s is better.\n\n";

    benchmark_cache<ListCache>("ListLRUCache", hitKeys, missKeys, mixedKeys);

    std::cout << '\n';

    benchmark_cache<IntrusiveCache>("IntrusiveLRUCache", hitKeys, missKeys, mixedKeys);

    std::cout << '\n';

    benchmark_cache<PoolCache>("PoolLRUCache", hitKeys, missKeys, mixedKeys);

    std::cout << "\nDone!!!\n";

    return EXIT_SUCCESS;
}
