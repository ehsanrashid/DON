/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src src/utility/cpu.cpp src/utility/mutex.cpp tests/arc_cache.cpp -o build/arc_cache
g++ -std=c++17 -O2 -DNDEBUG -I./src src/utility/cpu.cpp src/utility/mutex.cpp tests/arc_cache.cpp -o build/arc_cache

./build/arc_cache
*/

#include "utility/arc_cache.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <random>

#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cerr << "FAILED: " #condition << " at " << __FILE__ << ':' << __LINE__ << '\n'; \
            std::abort(); \
        } \
    } while (false)


using namespace DON;

namespace {
using TestCache = PoolARCCache<int, int>;
using FlatTestCache = PoolFlatARCCache<int, int>;

// Verify that the Mutex template parameter remains configurable.
class TestMutex final {
   public:
    void lock() noexcept { mutex.lock(); }

    void unlock() noexcept { mutex.unlock(); }

   private:
    std::mutex mutex;
};

using CustomMutexCache = PoolARCCache<int, int, TestMutex>;

void check_capacity(const TestCache& cache) noexcept {
    CHECK(cache.size() <= cache.get_capacity());
    CHECK(cache.empty() == (cache.size() == 0));
}

void test_basic_operations() noexcept {
    std::cout << "test_basic_operations\n";

    TestCache cache(3, 16);

    CHECK(cache.empty());
    CHECK(cache.size() == 0);
    CHECK(cache.get_capacity() == 3);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.read_or_write(3, 30) == 30);

    CHECK(cache.size() == 3);
    CHECK(cache.contains(1));
    CHECK(cache.contains(2));
    CHECK(cache.contains(3));

    // Existing keys return their cached values.
    CHECK(cache.read_or_write(1, 999) == 10);
    CHECK(cache.read_or_write(2, 999) == 20);
    CHECK(cache.size() == 3);

    CHECK(cache.remove(2));
    CHECK(!cache.remove(2));
    CHECK(!cache.contains(2));
    CHECK(cache.size() == 2);

    cache.clear();

    CHECK(cache.empty());
    CHECK(cache.size() == 0);

    // Reuse after clear.
    CHECK(cache.read_or_write(4, 40) == 40);
    CHECK(cache.contains(4));
    CHECK(cache.size() == 1);
}

void test_t1_eviction_and_b1_hit() noexcept {
    std::cout << "test_t1_eviction_and_b1_hit\n";

    TestCache cache(2, 16);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);

    // Promote key 1 from T1 to T2.
    CHECK(cache.read_or_write(1, 999) == 10);

    // Inserting key 3 should evict key 2 into B1.
    CHECK(cache.read_or_write(3, 30) == 30);

    CHECK(cache.size() == 2);
    CHECK(cache.contains(1));
    CHECK(cache.contains(3));
    CHECK(!cache.contains(2));

    // Reintroduce the key remembered in B1.
    CHECK(cache.read_or_write(2, 200) == 200);

    CHECK(cache.size() == 2);
    CHECK(cache.contains(2));
    CHECK(!cache.contains(1));
    CHECK(cache.contains(3));
}

void test_b2_ghost_hit() noexcept {
    std::cout << "test_b2_ghost_hit\n";

    TestCache cache(2, 16);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);

    // Promote key 1 to T2.
    CHECK(cache.read_or_write(1, 999) == 10);

    // Evict key 2 into B1.
    CHECK(cache.read_or_write(3, 30) == 30);
    CHECK(!cache.contains(2));

    // B1 hit adjusts the target and triggers replacement.
    CHECK(cache.read_or_write(2, 200) == 200);

    CHECK(cache.size() == 2);
    CHECK(cache.contains(2));
    CHECK(cache.contains(3));
    CHECK(!cache.contains(1));

    // Key 1 was evicted from T2 into B2.
    // Exercise B2 adaptation and replacement.
    CHECK(cache.read_or_write(1, 100) == 100);

    CHECK(cache.size() == 2);
    CHECK(cache.contains(1));
    CHECK(cache.contains(2));
    CHECK(!cache.contains(3));
}

void test_capacity_one() noexcept {
    std::cout << "test_capacity_one\n";

    TestCache cache(1, 8);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.size() == 1);

    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.size() == 1);
    CHECK(cache.contains(2));
    CHECK(!cache.contains(1));

    // Exercise a ghost hit with a single resident slot.
    CHECK(cache.read_or_write(1, 100) == 100);
    CHECK(cache.size() == 1);
    CHECK(cache.contains(1));

    CHECK(cache.read_or_write(3, 30) == 30);
    CHECK(cache.size() == 1);
    CHECK(cache.contains(3));

    check_capacity(cache);
}

void test_flat_cache_ghost_limit_churn() noexcept {
    std::cout << "test_flat_cache_ghost_limit_churn\n";

    FlatTestCache cache(2, 16);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.read_or_write(1, 999) == 10);
    CHECK(cache.read_or_write(3, 30) == 30);
    CHECK(cache.read_or_write(2, 200) == 200);
    CHECK(cache.contains(2));
    CHECK(cache.contains(3));
    CHECK(!cache.contains(1));

    for (int key = 0; key < 10'000; ++key)
    {
        const int coldKey = 100 + key;

        CHECK(cache.read_or_write(coldKey, coldKey * 10) == coldKey * 10);
        CHECK(cache.size() <= 2);
        CHECK(cache.contains(coldKey));
    }

    CHECK(cache.read_or_write(10'099, -1) == 100'990);
}

void test_builder() noexcept {
    std::cout << "test_builder\n";

    TestCache cache(2, 16);

    int calls = 0;

    const auto builder = [&calls](int value) noexcept {
        ++calls;
        return value * 10;
    };

    CHECK(cache.read_or_write_with(1, builder, 5) == 50);
    CHECK(calls == 1);

    // Cache hit: builder must not run again.
    CHECK(cache.read_or_write_with(1, builder, 99) == 50);
    CHECK(calls == 1);

    CHECK(cache.read_or_write_with(2, builder, 7) == 70);
    CHECK(calls == 2);

    // Exercise another insertion and subsequent access.
    CHECK(cache.read_or_write(3, 30) == 30);

    if (!cache.contains(1))
    {
        CHECK(cache.read_or_write_with(1, builder, 11) == 110);
        CHECK(calls == 3);
    }

    check_capacity(cache);
}

void test_zero_capacity() noexcept {
    std::cout << "test_zero_capacity\n";

    TestCache cache(0, 8);

    CHECK(cache.empty());
    CHECK(cache.get_capacity() == 0);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(1, 20) == 20);

    CHECK(cache.empty());
    CHECK(!cache.contains(1));

    int calls = 0;

    const auto builder = [&calls](int value) noexcept {
        ++calls;
        return value * 10;
    };

    CHECK(cache.read_or_write_with(2, builder, 5) == 50);
    CHECK(calls == 1);
    CHECK(cache.empty());

    CHECK(cache.read_or_write_with(2, builder, 6) == 60);
    CHECK(calls == 2);
    CHECK(cache.empty());

    // Restore a positive capacity and verify reuse.
    cache.set_capacity(2);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.contains(1));
    CHECK(cache.size() == 1);
}

void test_capacity_management() noexcept {
    std::cout << "test_capacity_management\n";

    TestCache cache(4, 16);

    for (int key = 1; key <= 4; ++key)
        CHECK(cache.read_or_write(key, key * 10) == key * 10);

    CHECK(cache.size() == 4);

    cache.set_capacity(2);

    CHECK(cache.get_capacity() == 2);
    CHECK(cache.size() <= 2);

    cache.evict_excess(1);
    CHECK(cache.size() <= 1);

    cache.set_capacity(0);

    CHECK(cache.get_capacity() == 0);
    CHECK(cache.empty());

    CHECK(cache.read_or_write(100, 1234) == 1234);
    CHECK(cache.empty());
    CHECK(!cache.contains(100));

    cache.set_capacity(2);

    CHECK(cache.read_or_write(100, 1234) == 1234);
    CHECK(cache.contains(100));
    CHECK(cache.size() == 1);

    check_capacity(cache);
}

void test_reset() noexcept {
    std::cout << "test_reset\n";

    TestCache cache(3, 16);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.read_or_write(3, 30) == 30);

    cache.set_capacity(1);
    CHECK(cache.get_capacity() == 1);
    CHECK(cache.size() <= 1);

    cache.reset();

    CHECK(cache.get_capacity() == 3);
    CHECK(cache.empty());

    CHECK(cache.read_or_write(4, 40) == 40);
    CHECK(cache.read_or_write(5, 50) == 50);

    CHECK(cache.size() == 2);
    CHECK(cache.contains(4));
    CHECK(cache.contains(5));

    check_capacity(cache);
}

void test_custom_mutex() noexcept {
    std::cout << "test_custom_mutex\n";

    CustomMutexCache cache(2, 8);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.read_or_write(1, 999) == 10);

    CHECK(cache.size() == 2);
    CHECK(cache.contains(1));
    CHECK(cache.contains(2));

    cache.clear();

    CHECK(cache.empty());
}

void test_stress() noexcept {
    std::cout << "test_stress\n";

    constexpr int Capacity   = 8;
    constexpr int KeyCount   = 64;
    constexpr int Iterations = 20000;

    TestCache cache(Capacity, 128);

    for (int i = 0; i < Iterations; ++i)
    {
        const int key = (i * 37 + i / 7) % KeyCount;

        if (i % 13 == 0)
        {
            (void) cache.remove(key);
        }
        else
        {
            const int value  = key * 100 + i;
            const int result = cache.read_or_write(key, value);

            // A cached value originated from a supplied value for this key.
            CHECK(result >= key * 100);
        }

        CHECK(cache.size() <= Capacity);
        CHECK(cache.get_capacity() == Capacity);
    }

    cache.clear();

    CHECK(cache.empty());
    CHECK(cache.size() == 0);

    std::cout << "  Completed " << Iterations << " operations\n";
}

void test_heavy_stress() noexcept {
    std::cout << "test_heavy_stress\n";

    constexpr int InitialCapacity = 64;
    constexpr int KeyRange        = 512;
    constexpr int HotKeyRange     = 16;
    constexpr int Operations      = 1'000'000;

    TestCache       cache(InitialCapacity);
    std::mt19937_64 rng(0xD0A5CA7EULL);

    for (int i = 0; i < Operations; ++i)
    {
        const auto random = rng();
        int        key;

        // Mix hot keys, a larger working set, and sequential scans.
        switch (random % 4)
        {
        case 0 :
            key = static_cast<int>(rng() % HotKeyRange);
            break;

        case 1 :
            key = static_cast<int>(rng() % KeyRange);
            break;

        case 2 :
            key = i % KeyRange;
            break;

        default :
            key = (i / 7) % KeyRange;
            break;
        }

        const auto action = (random >> 8) % 100;

        switch (action)
        {
        case 0 :
        case 1 :
        case 2 :
            (void) cache.remove(key);
            CHECK(!cache.contains(key));
            break;

        case 3 :
            cache.clear();
            CHECK(cache.empty());
            break;

        case 4 : {
            const auto capacity = static_cast<usize>(1 + rng() % 128);

            cache.set_capacity(capacity);

            CHECK(cache.get_capacity() == capacity);
            CHECK(cache.size() <= capacity);
            break;
        }

        default : {
            const int value = i ^ key;
            (void) cache.read_or_write(key, value);

            // Capacity is positive, so the accessed key must be resident.
            CHECK(cache.contains(key));
            break;
        }
        }

        if (cache.size() > cache.get_capacity())
        {
            std::cerr << "Capacity invariant failed"
                      << ", action=" << action << ", operation=" << i << ", size=" << cache.size()
                      << ", capacity=" << cache.get_capacity() << ", key=" << key << '\n';

            std::abort();
        }

        CHECK(cache.empty() == (cache.size() == 0));

        // Repeatedly verify clear and reuse.
        if ((i % 100'003) == 100'002)
        {
            cache.clear();
            CHECK(cache.empty());

            const int reuseKey = i % KeyRange;

            (void) cache.read_or_write(reuseKey, i);

            CHECK(cache.contains(reuseKey));
            CHECK(cache.size() == 1);
        }
    }

    std::cout << "  Completed " << Operations << " operations\n";
}

}  // namespace

int main() noexcept {
    test_basic_operations();
    test_t1_eviction_and_b1_hit();
    test_b2_ghost_hit();
    test_capacity_one();
    test_flat_cache_ghost_limit_churn();
    test_builder();
    test_zero_capacity();
    test_capacity_management();
    test_reset();
    test_custom_mutex();
    test_stress();
    test_heavy_stress();

    std::cout << "\nAll ARC tests passed.\n";

    return 0;
}
