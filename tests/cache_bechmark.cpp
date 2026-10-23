/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src src/utility/cpu.cpp src/utility/mutex.cpp tests/cache_bechmark.cpp -o build/cache_bechmark
g++ -std=c++17 -O2 -DNDEBUG -I./src src/utility/cpu.cpp src/utility/mutex.cpp tests/cache_bechmark.cpp -o build/cache_bechmark

g++ -std=c++17 -O0 -g -fno-omit-frame-pointer -I./src \
    src/utility/cpu.cpp \
    src/utility/mutex.cpp \
    tests/cache_bechmark.cpp \
    -o build/cache_bechmark_debug

./build/cache_bechmark
*/

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "utility/cpu.h"
#include "utility/arc_cache.h"
#include "utility/lfu_cache.h"
#include "utility/lirs_cache.h"
#include "utility/lru_cache.h"
#include "utility/mutex.h"
#include "utility/types.h"


namespace DON::CacheBenchmark {

using u64   = std::uint64_t;
using Clock = std::chrono::steady_clock;

constexpr usize Capacity      = 6 * KB;
constexpr usize ReserveCount  = 2 * Capacity;
constexpr float MaxLoadFactor = 0.75F;

constexpr usize OperationCount = 2 * MB;
constexpr usize Repetitions    = 26;
constexpr usize HotKeyCount    = 4 * KB;

constexpr u64 ColdKeyBase = u64{1} << 40;
constexpr u64 RandomSeed  = u64{0xD0A5CACE12345678};

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

[[nodiscard]] constexpr u64 value_for(u64 key) noexcept { return key ^ u64{0x9E3779B97F4A7C15}; }

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

[[nodiscard]] std::vector<u64> make_workload(Workload workload, usize hotAccessesPerFive = 0) {
    std::vector<u64> keys;
    keys.reserve(OperationCount);

    Random random{RandomSeed};
    u64    coldIndex = 0;

    for (usize i = 0; i < OperationCount; ++i)
    {
        switch (workload)
        {
        case Workload::Hits :
            keys.push_back(random.next() % HotKeyCount);
            break;

        case Workload::Misses :
            keys.push_back(ColdKeyBase + i);
            break;

        case Workload::Mixed :
            // hotAccessesPerFive of every five accesses are hot.
            if (i % 5 < hotAccessesPerFive)
                keys.push_back(random.next() % HotKeyCount);
            else
                keys.push_back(ColdKeyBase + coldIndex++);
            break;
        }
    }

    return keys;
}

template<typename Cache>
[[nodiscard]] Result run_once(const std::vector<u64>& keys) {
    Cache cache{Capacity, ReserveCount, MaxLoadFactor};

    // Prepare the cache outside the timed region.
    for (usize i = 0; i < Capacity; ++i)
        (void) cache.read_or_write(u64{i}, value_for(u64{i}));

    u64 checksum = 0;

    const auto start = Clock::now();

    for (const u64 key : keys)
        checksum += cache.read_or_write(key, value_for(key));

    const auto finish = Clock::now();

    const double elapsedNs = std::chrono::duration<double, std::nano>(finish - start).count();

    const double nsPerOperation = elapsedNs / static_cast<double>(keys.size());

    return {nsPerOperation, 1000.0 / nsPerOperation, checksum};
}

template<typename T, usize N>
[[nodiscard]] double median(std::array<T, N> values) noexcept {
    std::sort(values.begin(), values.end());

    if constexpr (N % 2 == 0)
        return (static_cast<double>(values[N / 2 - 1]) + static_cast<double>(values[N / 2])) / 2.0;
    else
        return static_cast<double>(values[N / 2]);
}


template<typename LruCache, typename LfuCache, typename ArcCache, typename LirsCache>
void benchmark_workload(const char* name, Workload workload, usize hotAccessesPerFive = 0) {
    const std::vector<u64> keys = make_workload(workload, hotAccessesPerFive);

    Array<double, Repetitions> lruTimes{};
    Array<double, Repetitions> lfuTimes{};
    Array<double, Repetitions> arcTimes{};
    Array<double, Repetitions> lirsTimes{};

    u64 lruChecksum = 0;
    u64 lfuChecksum = 0;
    u64 arcChecksum = 0;
    u64 lirsChecksum = 0;

    // Warm-up all implementations.
    (void) run_once<LruCache>(keys);
    (void) run_once<LfuCache>(keys);
    (void) run_once<ArcCache>(keys);
    (void) run_once<LirsCache>(keys);

    for (usize i = 0; i < Repetitions; ++i)
    {
        Result lruResult{};
        Result lfuResult{};
        Result arcResult{};
        Result lirsResult{};

        // Rotate execution order to reduce order bias.
        switch (i % 4)
        {
        case 0 :
            lruResult = run_once<LruCache>(keys);
            lfuResult = run_once<LfuCache>(keys);
            arcResult = run_once<ArcCache>(keys);
            lirsResult = run_once<LirsCache>(keys);
            break;

        case 1 :
            lfuResult = run_once<LfuCache>(keys);
            arcResult = run_once<ArcCache>(keys);
            lirsResult = run_once<LirsCache>(keys);
            lruResult = run_once<LruCache>(keys);
            break;

        case 2 :
            arcResult = run_once<ArcCache>(keys);
            lirsResult = run_once<LirsCache>(keys);
            lruResult = run_once<LruCache>(keys);
            lfuResult = run_once<LfuCache>(keys);
            break;

        default :
            lirsResult = run_once<LirsCache>(keys);
            lruResult = run_once<LruCache>(keys);
            lfuResult = run_once<LfuCache>(keys);
            arcResult = run_once<ArcCache>(keys);
            break;
        }

        lruTimes[i] = lruResult.nsPerOperation;
        lfuTimes[i] = lfuResult.nsPerOperation;
        arcTimes[i] = arcResult.nsPerOperation;
        lirsTimes[i] = lirsResult.nsPerOperation;

        lruChecksum = lruResult.checksum;
        lfuChecksum = lfuResult.checksum;
        arcChecksum = arcResult.checksum;
        lirsChecksum = lirsResult.checksum;
    }

    const double lruNs = median(lruTimes);
    const double lfuNs = median(lfuTimes);
    const double arcNs = median(arcTimes);
    const double lirsNs = median(lirsTimes);

    std::printf("\n%s (%zu operations)\n", name, keys.size());
    std::printf("%-12s %12s %12s %16s\n", "Cache", "ns/op", "Mops/s", "Checksum");

    std::printf("%-12s %12.3f %12.3f %16llu\n", "PoolFlatLRU", lruNs, 1000.0 / lruNs,
                static_cast<unsigned long long>(lruChecksum));

    std::printf("%-12s %12.3f %12.3f %16llu\n", "PoolFlatLFU", lfuNs, 1000.0 / lfuNs,
                static_cast<unsigned long long>(lfuChecksum));

    std::printf("%-12s %12.3f %12.3f %16llu\n", "PoolFlatARC", arcNs, 1000.0 / arcNs,
                static_cast<unsigned long long>(arcChecksum));

    std::printf("%-12s %12.3f %12.3f %16llu\n", "PoolFlatLIRS", lirsNs, 1000.0 / lirsNs,
                static_cast<unsigned long long>(lirsChecksum));

    std::printf("LFU vs LRU: %+.2f%% %s\n", (lruNs - lfuNs) * 100.0 / lruNs,
                lfuNs < lruNs   ? "faster"
                : lfuNs > lruNs ? "slower"
                                : "equal");

    std::printf("ARC vs LRU: %+.2f%% %s\n", (lruNs - arcNs) * 100.0 / lruNs,
                arcNs < lruNs   ? "faster"
                : arcNs > lruNs ? "slower"
                                : "equal");

    std::printf("LIRS vs LRU: %+.2f%% %s\n", (lruNs - lirsNs) * 100.0 / lruNs,
                lirsNs < lruNs   ? "faster"
                : lirsNs > lruNs ? "slower"
                                 : "equal");

    if (lruChecksum != lfuChecksum || lruChecksum != arcChecksum || lruChecksum != lirsChecksum)
        std::printf("WARNING: checksums differ!\n");
}

// Small hot-set test.
constexpr usize TestCapacity       = 256;
constexpr usize TestHotKeyCount    = 100;
constexpr usize TestColdKeyCount   = 1000;
constexpr usize TestTrainingRounds = 20;

[[nodiscard]] constexpr double percentage(usize count, usize total) noexcept {
    return 100.0 * static_cast<double>(count) / static_cast<double>(total);
}

// Returns the number of hot keys surviving the cold-key stream.
template<typename Cache>
[[nodiscard]] usize run_small_hot_set_survival() {
    Cache cache{TestCapacity};

    for (usize round = 0; round < TestTrainingRounds; ++round)
        for (usize key = 0; key < TestHotKeyCount; ++key)
            (void) cache.read_or_write(u64{key}, value_for(u64{key}));

    for (usize key = 0; key < TestColdKeyCount; ++key)
    {
        const u64 coldKey = ColdKeyBase + key;
        (void) cache.read_or_write(coldKey, value_for(coldKey));
    }

    usize survivingHotKeys = 0;

    for (usize key = 0; key < TestHotKeyCount; ++key)
        if (cache.contains(u64{key}))
            ++survivingHotKeys;

    return survivingHotKeys;
}

template<typename LruCache, typename LfuCache, typename ArcCache, typename LirsCache>
void benchmark_small_hot_set_survival() {
    Array<double, Repetitions> lruResults{};
    Array<double, Repetitions> lfuResults{};
    Array<double, Repetitions> arcResults{};
    Array<double, Repetitions> lirsResults{};

    // Warm-up.
    (void) run_small_hot_set_survival<LruCache>();
    (void) run_small_hot_set_survival<LfuCache>();
    (void) run_small_hot_set_survival<ArcCache>();
    (void) run_small_hot_set_survival<LirsCache>();

    for (usize i = 0; i < Repetitions; ++i)
    {
        switch (i % 4)
        {
        case 0 :
            lruResults[i] = static_cast<double>(run_small_hot_set_survival<LruCache>());
            lfuResults[i] = static_cast<double>(run_small_hot_set_survival<LfuCache>());
            arcResults[i] = static_cast<double>(run_small_hot_set_survival<ArcCache>());
            lirsResults[i] = static_cast<double>(run_small_hot_set_survival<LirsCache>());
            break;
        case 1 :
            lfuResults[i] = static_cast<double>(run_small_hot_set_survival<LfuCache>());
            arcResults[i] = static_cast<double>(run_small_hot_set_survival<ArcCache>());
            lirsResults[i] = static_cast<double>(run_small_hot_set_survival<LirsCache>());
            lruResults[i] = static_cast<double>(run_small_hot_set_survival<LruCache>());
            break;
        case 2 :
            arcResults[i] = static_cast<double>(run_small_hot_set_survival<ArcCache>());
            lirsResults[i] = static_cast<double>(run_small_hot_set_survival<LirsCache>());
            lruResults[i] = static_cast<double>(run_small_hot_set_survival<LruCache>());
            lfuResults[i] = static_cast<double>(run_small_hot_set_survival<LfuCache>());
            break;
        default :
            lirsResults[i] = static_cast<double>(run_small_hot_set_survival<LirsCache>());
            lruResults[i] = static_cast<double>(run_small_hot_set_survival<LruCache>());
            lfuResults[i] = static_cast<double>(run_small_hot_set_survival<LfuCache>());
            arcResults[i] = static_cast<double>(run_small_hot_set_survival<ArcCache>());
            break;
        }
    }

    const double lruSurvivors = median(lruResults);
    const double lfuSurvivors = median(lfuResults);
    const double arcSurvivors = median(arcResults);
    const double lirsSurvivors = median(lirsResults);

    const double lruRate = percentage(static_cast<usize>(lruSurvivors + 0.5), TestHotKeyCount);
    const double lfuRate = percentage(static_cast<usize>(lfuSurvivors + 0.5), TestHotKeyCount);
    const double arcRate = percentage(static_cast<usize>(arcSurvivors + 0.5), TestHotKeyCount);
    const double lirsRate = percentage(static_cast<usize>(lirsSurvivors + 0.5), TestHotKeyCount);

    std::printf("\nSmall hot-set survival (capacity=%zu, %zu hot, %zu cold)\n", TestCapacity,
                TestHotKeyCount, TestColdKeyCount);

    std::printf("%-12s %12s %12s\n", "Cache", "Survivors", "Survival");
    std::printf("%-12s %12.1f %11.2f%%\n", "PoolFlatLRU", lruSurvivors, lruRate);
    std::printf("%-12s %12.1f %11.2f%%\n", "PoolFlatLFU", lfuSurvivors, lfuRate);
    std::printf("%-12s %12.1f %11.2f%%\n", "PoolFlatARC", arcSurvivors, arcRate);
    std::printf("%-12s %12.1f %11.2f%%\n", "PoolFlatLIRS", lirsSurvivors, lirsRate);

    std::printf("LFU vs LRU: %+.2f percentage points\n", lfuRate - lruRate);
    std::printf("ARC vs LRU: %+.2f percentage points\n", arcRate - lruRate);
    std::printf("LIRS vs LRU: %+.2f percentage points\n", lirsRate - lruRate);
}

// Main hot-set survival workload.
constexpr usize SurvivalCapacity       = 8 * KB;
constexpr usize SurvivalHotKeyCount    = 4 * KB;
constexpr usize SurvivalColdKeyCount   = 6 * KB;
constexpr usize SurvivalTrainingRounds = 100;

template<typename Cache>
[[nodiscard]] usize run_hot_set_survival() {
    Cache cache{SurvivalCapacity};

    // Train the cache on frequently accessed keys.
    for (usize round = 0; round < SurvivalTrainingRounds; ++round)
        for (usize key = 0; key < SurvivalHotKeyCount; ++key)
            (void) cache.read_or_write(u64{key}, value_for(u64{key}));

    // Insert unique cold keys to force eviction.
    for (usize key = 0; key < SurvivalColdKeyCount; ++key)
    {
        const u64 coldKey = ColdKeyBase + key;
        (void) cache.read_or_write(coldKey, value_for(coldKey));
    }

    usize survivingHotKeys = 0;

    for (usize key = 0; key < SurvivalHotKeyCount; ++key)
        if (cache.contains(u64{key}))
            ++survivingHotKeys;

    return survivingHotKeys;
}

template<typename LruCache, typename LfuCache, typename ArcCache, typename LirsCache>
void benchmark_hot_set_survival() {
    Array<double, Repetitions> lruResults{};
    Array<double, Repetitions> lfuResults{};
    Array<double, Repetitions> arcResults{};
    Array<double, Repetitions> lirsResults{};

    // Warm-up.
    (void) run_hot_set_survival<LruCache>();
    (void) run_hot_set_survival<LfuCache>();
    (void) run_hot_set_survival<ArcCache>();
    (void) run_hot_set_survival<LirsCache>();

    for (usize i = 0; i < Repetitions; ++i)
    {
        switch (i % 4)
        {
        case 0 :
            lruResults[i] = static_cast<double>(run_hot_set_survival<LruCache>());
            lfuResults[i] = static_cast<double>(run_hot_set_survival<LfuCache>());
            arcResults[i] = static_cast<double>(run_hot_set_survival<ArcCache>());
            lirsResults[i] = static_cast<double>(run_hot_set_survival<LirsCache>());
            break;

        case 1 :
            lfuResults[i] = static_cast<double>(run_hot_set_survival<LfuCache>());
            arcResults[i] = static_cast<double>(run_hot_set_survival<ArcCache>());
            lirsResults[i] = static_cast<double>(run_hot_set_survival<LirsCache>());
            lruResults[i] = static_cast<double>(run_hot_set_survival<LruCache>());
            break;

        case 2 :
            arcResults[i] = static_cast<double>(run_hot_set_survival<ArcCache>());
            lirsResults[i] = static_cast<double>(run_hot_set_survival<LirsCache>());
            lruResults[i] = static_cast<double>(run_hot_set_survival<LruCache>());
            lfuResults[i] = static_cast<double>(run_hot_set_survival<LfuCache>());
            break;

        default :
            lirsResults[i] = static_cast<double>(run_hot_set_survival<LirsCache>());
            lruResults[i] = static_cast<double>(run_hot_set_survival<LruCache>());
            lfuResults[i] = static_cast<double>(run_hot_set_survival<LfuCache>());
            arcResults[i] = static_cast<double>(run_hot_set_survival<ArcCache>());
            break;
        }
    }

    const double lruSurvivors = median(lruResults);
    const double lfuSurvivors = median(lfuResults);
    const double arcSurvivors = median(arcResults);
    const double lirsSurvivors = median(lirsResults);

    const double lruRate = percentage(static_cast<usize>(lruSurvivors + 0.5), SurvivalHotKeyCount);
    const double lfuRate = percentage(static_cast<usize>(lfuSurvivors + 0.5), SurvivalHotKeyCount);
    const double arcRate = percentage(static_cast<usize>(arcSurvivors + 0.5), SurvivalHotKeyCount);
    const double lirsRate = percentage(static_cast<usize>(lirsSurvivors + 0.5), SurvivalHotKeyCount);

    std::printf("\nHot-set survival (capacity=%zu, %zu hot, %zu cold)\n", SurvivalCapacity,
                SurvivalHotKeyCount, SurvivalColdKeyCount);

    std::printf("%-12s %12s %12s\n", "Cache", "Survivors", "Survival");
    std::printf("%-12s %12.1f %11.3f%%\n", "PoolFlatLRU", lruSurvivors, lruRate);
    std::printf("%-12s %12.1f %11.3f%%\n", "PoolFlatLFU", lfuSurvivors, lfuRate);
    std::printf("%-12s %12.1f %11.3f%%\n", "PoolFlatARC", arcSurvivors, arcRate);
    std::printf("%-12s %12.1f %11.3f%%\n", "PoolFlatLIRS", lirsSurvivors, lirsRate);

    std::printf("LFU vs LRU: %+.3f percentage points\n", lfuRate - lruRate);
    std::printf("ARC vs LRU: %+.3f percentage points\n", arcRate - lruRate);
    std::printf("LIRS vs LRU: %+.3f percentage points\n", lirsRate - lruRate);
}

template<typename LruCache, typename LfuCache, typename ArcCache, typename LirsCache>
void run() {
    std::printf("Cache benchmark: capacity=%zu, repetitions=%zu\n", Capacity, Repetitions);

    benchmark_workload<LruCache, LfuCache, ArcCache, LirsCache>("Hits", Workload::Hits);

    benchmark_workload<LruCache, LfuCache, ArcCache, LirsCache>("Misses + eviction", Workload::Misses);

    benchmark_workload<LruCache, LfuCache, ArcCache, LirsCache>("Mixed (80%% hot / 20%% cold)",
                                                                Workload::Mixed, 4);

    benchmark_workload<LruCache, LfuCache, ArcCache, LirsCache>("Mixed (60%% hot / 40%% cold)",
                                                                Workload::Mixed, 3);

    benchmark_workload<LruCache, LfuCache, ArcCache, LirsCache>("Mixed (40%% hot / 60%% cold)",
                                                                Workload::Mixed, 2);

    benchmark_workload<LruCache, LfuCache, ArcCache, LirsCache>("Mixed (20%% hot / 80%% cold)",
                                                                Workload::Mixed, 1);

    // Optional small-scale test.
    // benchmark_small_hot_set_survival<LruCache, LfuCache, ArcCache, LirsCache>();

    benchmark_hot_set_survival<LruCache, LfuCache, ArcCache, LirsCache>();
}

}  // namespace DON::CacheBenchmark

using namespace DON;

int main() {

    CacheBenchmark::run<PoolFlatLRUCache<u64, u64, SpinMutex>,  //
                        PoolFlatLFUCache<u64, u64, SpinMutex>,  //
                        PoolFlatARCCache<u64, u64, SpinMutex>,
                        PoolFlatLIRSCache<u64, u64, SpinMutex>>();

    return 0;
}
