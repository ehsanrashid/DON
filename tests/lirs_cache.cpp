/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src src/utility/cpu.cpp src/utility/mutex.cpp tests/lirs_cache.cpp -o build/lirs_cache
g++ -std=c++17 -O2 -DNDEBUG -I./src src/utility/cpu.cpp src/utility/mutex.cpp tests/lirs_cache.cpp -o build/lirs_cache

./build/lirs_cache
*/

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <shared_mutex>
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

#include "utility/lirs_cache.h"
#include "utility/thread_attributes.h"
#include "utility/types.h"

using namespace DON;

namespace {

using PoolCache = PoolFlatLIRSCache<int, int>;

// Verify that the Mutex template parameter remains configurable.
class LOCKABLE TestMutex final {
   public:
    void lock() noexcept ACQUIRE_CAPABILITY() { mutex.lock(); }

    void unlock() noexcept RELEASE_CAPABILITY() { mutex.unlock(); }

   private:
    std::mutex mutex;
};

using CustomMutexCache = PoolFlatLIRSCache<int, int, TestMutex>;

void check_capacity(const PoolCache& cache) noexcept {
    CHECK(cache.size() <= cache.get_capacity());
    CHECK(cache.empty() == (cache.size() == 0));
}

void test_basic_operations() noexcept {
    std::cout << "test_basic_operations\n";

    PoolCache cache(3, 16);

    CHECK(cache.empty());
    CHECK(cache.size() == 0);
    CHECK(cache.get_capacity() == 3);
    CHECK(cache.ghost_size() == 0);

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
    CHECK(!cache.contains_ghost(2));
    CHECK(cache.size() == 2);

    cache.clear();

    CHECK(cache.empty());
    CHECK(cache.size() == 0);
    CHECK(cache.ghost_size() == 0);

    // Reuse after clear.
    CHECK(cache.read_or_write(4, 40) == 40);
    CHECK(cache.contains(4));
    CHECK(cache.size() == 1);
}

void test_lir_and_hir_entries() noexcept {
    std::cout << "test_lir_and_hir_entries\n";

    PoolCache cache(3, 16);

    // With capacity 3, the configured LIR target is 2.
    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.read_or_write(3, 30) == 30);

    CHECK(cache.size() == 3);

    // Accessing the HIR entry in Stack S promotes it to LIR.
    // The bottom LIR entry is demoted to HIR.
    CHECK(cache.read_or_write(3, 999) == 30);

    CHECK(cache.size() == 3);
    CHECK(cache.contains(1));
    CHECK(cache.contains(2));
    CHECK(cache.contains(3));

    // Repeated accesses must preserve the cached values.
    CHECK(cache.read_or_write(3, 300) == 30);
    CHECK(cache.read_or_write(2, 200) == 20);

    CHECK(cache.size() == 3);
    check_capacity(cache);
}

void test_stack_pruning() noexcept {
    std::cout << "test_stack_pruning\n";

    PoolCache cache(3, 16);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.read_or_write(3, 30) == 30);

    // Promote the HIR entry. The bottom LIR entry is demoted
    // and then pruned from Stack S.
    CHECK(cache.read_or_write(3, 999) == 30);

    CHECK(cache.contains(1));
    CHECK(cache.contains(2));
    CHECK(cache.contains(3));

    // Insert another key. The HIR queue must provide an eviction candidate.
    CHECK(cache.read_or_write(4, 40) == 40);

    CHECK(cache.size() == 3);
    CHECK(cache.contains(4));

    check_capacity(cache);
}

void test_ghost_entry_and_restore() noexcept {
    std::cout << "test_ghost_entry_and_restore\n";

    PoolCache cache(3, 16);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.read_or_write(3, 30) == 30);

    // Key 3 is HIR and is present in both Stack S and Queue Q.
    // Inserting key 4 evicts key 3 from Q, but it remains in S as a ghost.
    CHECK(cache.read_or_write(4, 40) == 40);

    CHECK(cache.size() == 3);
    CHECK(!cache.contains(3));
    CHECK(cache.contains_ghost(3));
    CHECK(cache.ghost_size() >= 1);

    // A ghost hit restores the entry as LIR.
    CHECK(cache.read_or_write(3, 300) == 300);

    CHECK(cache.size() == 3);
    CHECK(cache.contains(3));
    CHECK(!cache.contains_ghost(3));

    // The restored entry must return its newly supplied value.
    CHECK(cache.read_or_write(3, 999) == 300);

    check_capacity(cache);
}

void test_ghost_removal() noexcept {
    std::cout << "test_ghost_removal\n";

    PoolCache cache(3, 16);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.read_or_write(3, 30) == 30);
    CHECK(cache.read_or_write(4, 40) == 40);

    CHECK(cache.contains_ghost(3));
    CHECK(!cache.contains(3));

    // remove() must also remove a non-resident ghost entry.
    CHECK(cache.remove(3));
    CHECK(!cache.contains(3));
    CHECK(!cache.contains_ghost(3));

    CHECK(!cache.remove(3));
    check_capacity(cache);
}

void test_capacity_one() noexcept {
    std::cout << "test_capacity_one\n";

    PoolCache cache(1, 8);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.size() == 1);
    CHECK(cache.contains(1));

    CHECK(cache.read_or_write(2, 20) == 20);
    CHECK(cache.size() == 1);
    CHECK(cache.contains(2));
    CHECK(!cache.contains(1));

    // With capacity one, the LIR target is zero.
    // Entries are managed as HIR residents without ghost restoration.
    CHECK(cache.read_or_write(1, 100) == 100);

    CHECK(cache.size() == 1);
    CHECK(cache.contains(1));
    CHECK(!cache.contains_ghost(1));

    CHECK(cache.read_or_write(3, 30) == 30);

    CHECK(cache.size() == 1);
    CHECK(cache.contains(3));
    CHECK(!cache.contains(1));

    check_capacity(cache);
}

void test_builder() noexcept {
    std::cout << "test_builder\n";

    PoolCache cache(2, 16);

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

    CHECK(cache.read_or_write(3, 30) == 30);

    // If key 1 was evicted, it must be rebuilt.
    if (!cache.contains(1))
    {
        CHECK(cache.read_or_write_with(1, builder, 11) == 110);
        CHECK(calls == 3);
    }

    check_capacity(cache);
}

void test_zero_capacity() noexcept {
    std::cout << "test_zero_capacity\n";

    PoolCache cache(0, 8);

    CHECK(cache.empty());
    CHECK(cache.get_capacity() == 0);

    CHECK(cache.read_or_write(1, 10) == 10);
    CHECK(cache.read_or_write(1, 20) == 20);

    CHECK(cache.empty());
    CHECK(!cache.contains(1));
    CHECK(!cache.contains_ghost(1));

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

    check_capacity(cache);
}

void test_capacity_management() noexcept {
    std::cout << "test_capacity_management\n";

    PoolCache cache(4, 16);

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
    CHECK(cache.ghost_size() == 0);

    CHECK(cache.read_or_write(100, 1234) == 1234);
    CHECK(cache.empty());
    CHECK(!cache.contains(100));

    cache.set_capacity(2);

    CHECK(cache.read_or_write(100, 1234) == 1234);
    CHECK(cache.contains(100));
    CHECK(cache.size() == 1);

    check_capacity(cache);
}

template<typename Cache>
void test_capacity_reallocation_and_erase() noexcept {
    std::cout << "test_capacity_reallocation_and_erase\n";

    for (int removedKey = 1; removedKey <= 4; ++removedKey)
    {
        Cache cache(4, 0);

        for (int key = 1; key <= 4; ++key)
            CHECK(cache.read_or_write(key, key * 10) == key * 10);

        // Force the map to grow while entries are resident.
        cache.set_capacity(2048);
        CHECK(cache.size() == 4);

        CHECK(cache.remove(removedKey));

        cache.set_capacity(3);
        CHECK(cache.size() <= 3);

        cache.set_capacity(0);

        CHECK(cache.empty());
        CHECK(cache.size() == 0);
        CHECK(cache.ghost_size() == 0);
    }
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
    CHECK(cache.ghost_size() == 0);
}

template<typename Cache>
void test_stress() noexcept {
    std::cout << "test_stress\n";

    constexpr int Capacity   = 8;
    constexpr int KeyCount   = 64;
    constexpr int Iterations = 20'000;

    Cache cache(Capacity, 128);

    for (int i = 0; i < Iterations; ++i)
    {
        const int key = (i * 37 + i / 7) % KeyCount;

        if (i % 13 == 0)
        {
            (void) cache.remove(key);
            CHECK(!cache.contains(key));
            CHECK(!cache.contains_ghost(key));
        }
        else
        {
            const int value  = key * 100 + i;
            const int result = cache.read_or_write(key, value);

            CHECK(result >= key * 100);
            CHECK(cache.contains(key));
        }

        CHECK(cache.size() <= Capacity);
        CHECK(cache.get_capacity() == Capacity);
        CHECK(cache.empty() == (cache.size() == 0));
    }

    cache.clear();

    CHECK(cache.empty());
    CHECK(cache.size() == 0);
    CHECK(cache.ghost_size() == 0);

    std::cout << "  Completed " << Iterations << " operations\n";
}

template<typename Cache>
void test_heavy_stress() noexcept {
    std::cout << "test_heavy_stress\n";

    constexpr int InitialCapacity = 64;
    constexpr int KeyRange        = 512;
    constexpr int HotKeyRange     = 16;
    constexpr int Operations      = 1'000'000;

    Cache           cache(InitialCapacity);
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
            CHECK(!cache.contains_ghost(key));
            break;

        case 3 :
            cache.clear();
            CHECK(cache.empty());
            CHECK(cache.ghost_size() == 0);
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
            CHECK(cache.ghost_size() == 0);

            const int reuseKey = i % KeyRange;

            (void) cache.read_or_write(reuseKey, i);

            CHECK(cache.contains(reuseKey));
            CHECK(cache.size() == 1);
        }
    }

    std::cout << "  Completed " << Operations << " operations\n";
}

void test_ghost_churn() noexcept {
    std::cout << "test_ghost_churn\n";

    PoolCache cache(2, 16);

    for (int key = 1; key <= 10'000; ++key)
    {
        CHECK(cache.read_or_write(key, key * 10) == key * 10);

        CHECK(cache.size() <= 2);
        CHECK(cache.contains(key));
    }

    // A final access must still return the correct value.
    CHECK(cache.read_or_write(10'000, -1) == 100'000);

    check_capacity(cache);
}

}  // namespace

int main() noexcept {
    test_basic_operations();
    test_lir_and_hir_entries();
    test_stack_pruning();
    test_ghost_entry_and_restore();
    test_ghost_removal();
    test_capacity_one();
    test_builder();
    test_zero_capacity();
    test_capacity_management();
    test_capacity_reallocation_and_erase<PoolCache>();
    test_custom_mutex();
    test_stress<PoolCache>();
    test_heavy_stress<PoolCache>();
    test_ghost_churn();

    std::cout << "\nAll LIRS tests passed.\n";

    return 0;
}
