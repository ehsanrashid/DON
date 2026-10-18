/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -DNDEBUG -I./src \
    src/utility/cpu.cpp \
    src/utility/mutex.cpp \
    tests/cache_bechmark.cpp \
    -o build/cache_bechmark
g++ -std=c++17 -O2 -I./src \
    src/utility/cpu.cpp \
    src/utility/mutex.cpp \
    tests/cache_bechmark.cpp \
    -o build/cache_bechmark
g++ -std=c++17 -O2 -I./src `
    src/utility/cpu.cpp `
    src/utility/mutex.cpp `
    tests/cache_bechmark.cpp `
    -o build/cache_bechmark

./build/cache_bechmark
*/

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "utility/cpu.h"
#include "utility/lfu_cache.h"
#include "utility/lru_cache.h"
#include "utility/mutex.h"
#include "utility/types.h"


namespace DON::CacheBenchmark {

using u64   = std::uint64_t;
using Clock = std::chrono::steady_clock;

constexpr usize Capacity       = 2048;
constexpr usize OperationCount = 1'600'000;
constexpr usize Repetitions    = 7;
constexpr usize HotKeyCount    = 256;

constexpr u64 ColdKeyBase = u64{1} << 40;

enum class Workload : usize {
    Hits,
    Misses,
    Mixed
};

struct Result {
    double nsPerOperation;
    double mopsPerSecond;
    u64    checksum;
};

[[nodiscard]] constexpr u64 value_for(u64 key) noexcept {
    return key ^ 0x9E3779B97F4A7C15ULL;
}

// Deterministic generator: no random-number generation during timing.
class Random {
   public:
    explicit Random(u64 seed) noexcept :
        state(seed) {}

    [[nodiscard]] u64 next() noexcept {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }

   private:
    u64 state;
};

[[nodiscard]] std::vector<u64> make_workload(Workload workload) {
    std::vector<u64> keys;
    keys.reserve(OperationCount);

    Random random{0xD0A5CACE12345678ULL};
    u64    coldIndex = 0;

    for (usize i = 0; i < OperationCount; ++i)
    {
        switch (workload)
        {
        case Workload::Hits :
            keys.push_back(random.next() % Capacity);
            break;

        case Workload::Misses :
            keys.push_back(ColdKeyBase + i);
            break;

        case Workload::Mixed :
            // 80% hot-key accesses and 20% unique cold-key accesses.
            if (i % 5 != 0)
                keys.push_back(random.next() % HotKeyCount);
            else
                keys.push_back(ColdKeyBase + coldIndex++);
            break;
        }
    }

    return keys;
}

template<typename Cache>
[[nodiscard]] Result run_once(const std::vector<u64>& keys, Workload workload) {
    Cache cache{Capacity};

    // Prepare the cache outside the timed region.
    for (usize i = 0; i < Capacity; ++i)
        (void) cache.read_or_write(u64{i}, value_for(u64{i}));

    const auto start = Clock::now();

    u64 checksum = 0;

    for (const u64 key : keys)
        checksum += cache.read_or_write(key, value_for(key));

    const auto finish = Clock::now();

    const double elapsedNs =
        std::chrono::duration<double, std::nano>(finish - start).count();

    const double nsPerOperation = elapsedNs / static_cast<double>(keys.size());

    return {nsPerOperation, 1000.0 / nsPerOperation, checksum};
}

[[nodiscard]] double median(std::array<double, Repetitions> values) {
    std::sort(values.begin(), values.end());
    return values[Repetitions / 2];
}

template<typename LruCache, typename LfuCache>
void benchmark_workload(const char* name, Workload workload) {
    const std::vector<u64> keys = make_workload(workload);

    std::array<double, Repetitions> lruTimes{};
    std::array<double, Repetitions> lfuTimes{};

    u64 lruChecksum = 0;
    u64 lfuChecksum = 0;

    // Warm-up both implementations.
    (void) run_once<LruCache>(keys, workload);
    (void) run_once<LfuCache>(keys, workload);

    for (usize i = 0; i < Repetitions; ++i)
    {
        Result lruResult{};
        Result lfuResult{};

        if (i % 2 == 0)
        {
            lruResult = run_once<LruCache>(keys, workload);
            lfuResult = run_once<LfuCache>(keys, workload);
        }
        else
        {
            lfuResult = run_once<LfuCache>(keys, workload);
            lruResult = run_once<LruCache>(keys, workload);
        }

        lruTimes[i] = lruResult.nsPerOperation;
        lfuTimes[i] = lfuResult.nsPerOperation;

        lruChecksum = lruResult.checksum;
        lfuChecksum = lfuResult.checksum;
    }

    const double lruNs = median(lruTimes);
    const double lfuNs = median(lfuTimes);

    std::printf("\n%s (%zu operations)\n", name, keys.size());
    std::printf("%-12s %12s %12s %16s\n",
                "Cache", "ns/op", "Mops/s", "Checksum");
    std::printf("%-12s %12.3f %12.3f %16llu\n",
                "PoolFlatLRU", lruNs, 1000.0 / lruNs,
                static_cast<unsigned long long>(lruChecksum));
    std::printf("%-12s %12.3f %12.3f %16llu\n",
                "PoolFlatLFU", lfuNs, 1000.0 / lfuNs,
                static_cast<unsigned long long>(lfuChecksum));

    std::printf("LFU vs LRU: %.2f%% %s\n",
                (lruNs - lfuNs) * 100.0 / lruNs,
                lfuNs < lruNs   ? "faster"
                : lfuNs > lruNs ? "slower"
                                : "equal");

    if (lruChecksum != lfuChecksum)
        std::printf("WARNING: checksums differ!\n");
}

// Returns the hot-key hit rate after cold-key pollution.
template<typename Cache>
[[nodiscard]] double run_small_hot_set_survival() {
    constexpr usize TestCapacity      = 256;
    constexpr usize TestHotKeyCount   = 100;
    constexpr usize ColdKeyCount      = 1000;
    constexpr usize TrainingRounds    = 20;
    constexpr usize MeasurementRounds = 10;

    Cache cache{TestCapacity};

    // Train the cache on the hot keys.
    for (usize round = 0; round < TrainingRounds; ++round)
        for (usize key = 0; key < TestHotKeyCount; ++key)
            (void) cache.read_or_write(u64{key}, value_for(u64{key}));

    // Insert unique cold keys to force eviction.
    for (usize key = 0; key < ColdKeyCount; ++key)
    {
        const u64 coldKey = ColdKeyBase + key;
        (void) cache.read_or_write(coldKey, value_for(coldKey));
    }

    // Measure how many hot keys survived the cold-key stream.
    usize hits = 0;
    constexpr usize TotalAccesses = TestHotKeyCount * MeasurementRounds;

    for (usize round = 0; round < MeasurementRounds; ++round)
        for (usize key = 0; key < TestHotKeyCount; ++key)
        {
            const u64 hotKey = key;

            if (cache.contains(hotKey))
                ++hits;

            (void) cache.read_or_write(hotKey, value_for(hotKey));
        }

    return 100.0 * static_cast<double>(hits) / static_cast<double>(TotalAccesses);
}

template<typename LruCache, typename LfuCache>
void benchmark_small_hot_set_survival() {
    std::array<double, Repetitions> lruResults{};
    std::array<double, Repetitions> lfuResults{};

    // Warm-up.
    (void) run_small_hot_set_survival<LruCache>();
    (void) run_small_hot_set_survival<LfuCache>();

    for (usize i = 0; i < Repetitions; ++i)
    {
        if (i % 2 == 0)
        {
            lruResults[i] = run_small_hot_set_survival<LruCache>();
            lfuResults[i] = run_small_hot_set_survival<LfuCache>();
        }
        else
        {
            lfuResults[i] = run_small_hot_set_survival<LfuCache>();
            lruResults[i] = run_small_hot_set_survival<LruCache>();
        }
    }

    const double lruHitRate = median(lruResults);
    const double lfuHitRate = median(lfuResults);

    std::printf("\nHot-set survival (capacity=256, 100 hot, 1000 cold)\n");
    std::printf("%-12s %16s\n", "Cache", "Hot hit rate");
    std::printf("%-12s %15.2f%%\n", "PoolFlatLRU", lruHitRate);
    std::printf("%-12s %15.2f%%\n", "PoolFlatLFU", lfuHitRate);
    std::printf("LFU advantage: %+.2f percentage points\n",
                lfuHitRate - lruHitRate);
}

constexpr usize SurvivalCapacity       = 8 * KB;
constexpr usize SurvivalHotKeyCount    = 4 * KB;
constexpr usize SurvivalColdKeyCount   = 6 * KB;
constexpr usize SurvivalTrainingRounds = 100;
constexpr usize SurvivalMeasureRounds  = 1;

// Normal
//constexpr usize SurvivalCapacity       = 16 * KB;
//constexpr usize SurvivalHotKeyCount    = 4 * KB;
//constexpr usize SurvivalColdKeyCount   = 64 * KB;
//constexpr usize SurvivalTrainingRounds = 100;
//constexpr usize SurvivalMeasureRounds  = 100;

// Slight Favour LFU
//constexpr usize SurvivalCapacity       = 20 * KB;
//constexpr usize SurvivalHotKeyCount    = 16 * KB;
//constexpr usize SurvivalColdKeyCount   = 14 * KB;
//constexpr usize SurvivalTrainingRounds = 500;
//constexpr usize SurvivalMeasureRounds  = 1;

// Favour LFU
//constexpr usize SurvivalCapacity       = 16 * KB;
//constexpr usize SurvivalHotKeyCount    = 4 * KB;
//constexpr usize SurvivalColdKeyCount   = 256 * KB;
//constexpr usize SurvivalTrainingRounds = 20;
//constexpr usize SurvivalMeasureRounds  = 1;

template<typename Cache>
[[nodiscard]] double run_hot_set_survival() {
    Cache cache{SurvivalCapacity};

    // Train the cache on the hot keys.
    for (usize round = 0; round < SurvivalTrainingRounds; ++round)
        for (usize key = 0; key < SurvivalHotKeyCount; ++key)
            (void) cache.read_or_write(u64{key}, value_for(u64{key}));

    // Stream many unique cold keys through the cache.
    for (usize key = 0; key < SurvivalColdKeyCount; ++key)
    {
        const u64 coldKey = ColdKeyBase + key;
        (void) cache.read_or_write(coldKey, value_for(coldKey));
    }

    usize survivingHotKeys = 0;

    for (usize key = 0; key < SurvivalHotKeyCount; ++key)
    {
        if (cache.contains(u64{key}))
            ++survivingHotKeys;
    }

    //std::printf("Hot keys surviving cold stream: %zu / %zu\n", survivingHotKeys, SurvivalHotKeyCount);

    // Count hot-key hits over repeated measurement rounds.
    usize hits = 0;
    for (usize key = 0; key < SurvivalHotKeyCount; ++key)
    {
        if (cache.contains(u64{key}))
            ++hits;
    }

    return 100.0 * static_cast<double>(hits) / static_cast<double>(SurvivalHotKeyCount);

    /*
    constexpr usize TotalAccesses = SurvivalHotKeyCount * SurvivalMeasureRounds;

    for (usize round = 0; round < SurvivalMeasureRounds; ++round)
        for (usize key = 0; key < SurvivalHotKeyCount; ++key)
        {
            const u64 hotKey = key;

            if (cache.contains(hotKey))
                ++hits;

            (void) cache.read_or_write(hotKey, value_for(hotKey));
        }

    return 100.0 * static_cast<double>(hits) / static_cast<double>(TotalAccesses);
    */
}

template<typename LruCache, typename LfuCache>
void benchmark_hot_set_survival() {
    std::array<double, Repetitions> lruResults{};
    std::array<double, Repetitions> lfuResults{};

    // Warm-up.
    (void) run_hot_set_survival<LruCache>();
    (void) run_hot_set_survival<LfuCache>();

    for (usize i = 0; i < Repetitions; ++i)
    {
        if (i % 2 == 0)
        {
            lruResults[i] = run_hot_set_survival<LruCache>();
            lfuResults[i] = run_hot_set_survival<LfuCache>();
        }
        else
        {
            lfuResults[i] = run_hot_set_survival<LfuCache>();
            lruResults[i] = run_hot_set_survival<LruCache>();
        }
    }

    const double lruHitRate = median(lruResults);
    const double lfuHitRate = median(lfuResults);

    const usize totalAccesses = SurvivalHotKeyCount * SurvivalMeasureRounds;
    const auto lruHits = static_cast<usize>(
        lruHitRate * static_cast<double>(totalAccesses) / 100.0 + 0.5);
    const auto lfuHits = static_cast<usize>(
        lfuHitRate * static_cast<double>(totalAccesses) / 100.0 + 0.5);

    std::printf(
        "\nHot-set survival (capacity=%zu, %zu hot, %zu cold; %zu operations)\n",
        SurvivalCapacity, SurvivalHotKeyCount, SurvivalColdKeyCount, totalAccesses);
    std::printf("%-12s %12s %12s\n",      "Cache", "Hit rate", "Hits");
    std::printf("%-12s %11.3f%% %12zu\n", "PoolFlatLRU", lruHitRate, lruHits);
    std::printf("%-12s %11.3f%% %12zu\n", "PoolFlatLFU", lfuHitRate, lfuHits);

    if (lruHitRate > 0.0)
        std::printf("LFU vs LRU: %+.2f%% %s\n",
                    (lfuHitRate - lruHitRate) * 100.0 / lruHitRate,
                    lfuHitRate > lruHitRate ? "better hit rate"
                    : lfuHitRate < lruHitRate ? "worse hit rate"
                                              : "equal hit rate");
    else
        std::printf("LFU vs LRU: %+.2f percentage points\n",
                    lfuHitRate - lruHitRate);
}


template<typename LruCache, typename LfuCache>
void run() {
    std::printf("Cache benchmark: capacity=%zu, repetitions=%zu\n",
                Capacity, Repetitions);

    benchmark_workload<LruCache, LfuCache>("Hits", Workload::Hits);

    benchmark_workload<LruCache, LfuCache>(
        "Misses + eviction", Workload::Misses);

    benchmark_workload<LruCache, LfuCache>(
        "Mixed (80%% hot / 20%% cold)", Workload::Mixed);

    //benchmark_small_hot_set_survival<LruCache, LfuCache>();

    benchmark_hot_set_survival<LruCache, LfuCache>();
}

}  // namespace DON::CacheBenchmark

using namespace DON;

int main() {

    CacheBenchmark::run<PoolFlatLRUCache<u64, u64, SpinMutex>, PoolFlatLFUCache<u64, u64, SpinMutex>>();

    return 0;
}