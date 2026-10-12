/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src \
    tests/pinned_lru_cache.cpp \
    $(ls src/*.cpp | grep -v 'src/main.cpp') \
    src/nnue/*.cpp \
    src/nnue/features/*.cpp \
    src/book/*.cpp \
    src/tablebase/*.cpp \
    -o build/pinned_lru_cache \
    -lpthread
g++ -std=c++17 -O2 -I./src \
    tests/pinned_lru_cache.cpp \
    $(for f in src/*.cpp; do [[ "$f" != "src/main.cpp" ]] && echo "$f"; done) \
    src/nnue/*.cpp \
    src/nnue/features/*.cpp \
    src/book/*.cpp \
    src/tablebase/*.cpp \
    -o build/pinned_lru_cache \
    -lpthread
g++ -std=c++17 -O2 -I./src `
    tests/pinned_lru_cache.cpp `
    $(Get-ChildItem src/*.cpp | Where-Object { $_.Name -ne "main.cpp" } | ForEach-Object { $_.FullName }) `
    src/nnue/*.cpp `
    src/nnue/features/*.cpp `
    src/book/*.cpp `
    src/tablebase/*.cpp `
    -o build/pinned_lru_cache `
    -lpthread

./build/pinned_lru_cache
*/

#include "non_concurrent.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace DON;

namespace {

using Cache    = PinnedLRUCache<int, int>;
using ValuePtr = Cache::ValuePtr;

ValuePtr make_value(const int value) noexcept { return std::make_shared<int>(value); }

void test_insert_and_lookup() noexcept {
    Cache cache(3);

    const auto value1 = cache.insert(1, make_value(10));
    const auto value2 = cache.insert(2, make_value(20));
    const auto value3 = cache.insert(3, make_value(30));

    assert(value1 && *value1 == 10);
    assert(value2 && *value2 == 20);
    assert(value3 && *value3 == 30);
    assert(cache.size() == 3);
    assert(!cache.empty());
    assert(cache.capacity() == 3);

    const auto lookup1 = cache.lookup(1);
    const auto lookup2 = cache.lookup(2);
    const auto lookup3 = cache.lookup(3);

    assert(lookup1 && *lookup1 == 10);
    assert(lookup2 && *lookup2 == 20);
    assert(lookup3 && *lookup3 == 30);

    assert(cache.contains(1));
    assert(cache.contains(2));
    assert(cache.contains(3));
    assert(!cache.contains(4));
    assert(!cache.lookup(4));

    assert(cache.size() == 3);

    std::cout << "PASS: insert and lookup\n";
}

void test_lru_eviction() noexcept {
    Cache cache(3);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));
    (void) cache.insert(3, make_value(30));

    // Make key 1 the most recently used entry.
    assert(cache.lookup(1));

    // Key 2 becomes the least recently used entry.
    (void) cache.insert(4, make_value(40));

    assert(cache.size() == 3);
    assert(cache.contains(1));
    assert(!cache.contains(2));
    assert(cache.contains(3));
    assert(cache.contains(4));

    // Looking up key 3 makes key 1 the least recently used.
    assert(cache.lookup(3));
    (void) cache.insert(5, make_value(50));

    assert(cache.size() == 3);
    assert(!cache.contains(1));
    assert(cache.contains(3));
    assert(cache.contains(4));
    assert(cache.contains(5));

    std::cout << "PASS: LRU eviction\n";
}

void test_update_existing_key() noexcept {
    Cache cache(3);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));
    (void) cache.insert(3, make_value(30));

    const auto updated1 = cache.insert(1, make_value(100));

    assert(updated1 && *updated1 == 100);
    assert(cache.size() == 3);
    assert(cache.contains(1));
    assert(cache.contains(2));
    assert(cache.contains(3));

    const auto updated2 = cache.insert(2, make_value(200));

    assert(updated2 && *updated2 == 200);
    assert(cache.size() == 3);

    // Key 2 is MRU, key 1 is next, and key 3 is LRU.
    (void) cache.insert(4, make_value(40));

    assert(cache.size() == 3);
    assert(cache.contains(1));
    assert(cache.contains(2));
    assert(!cache.contains(3));
    assert(cache.contains(4));

    const auto lookup1 = cache.lookup(1);
    const auto lookup2 = cache.lookup(2);
    const auto lookup4 = cache.lookup(4);

    assert(lookup1 && *lookup1 == 100);
    assert(lookup2 && *lookup2 == 200);
    assert(lookup4 && *lookup4 == 40);

    std::cout << "PASS: update existing key\n";
}

void test_erase() noexcept {
    Cache cache(3);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));
    (void) cache.insert(3, make_value(30));

    const bool erased1     = cache.erase(1);
    const bool erasedAgain = cache.erase(1);

    assert(erased1);
    assert(!erasedAgain);
    assert(!cache.contains(1));
    assert(cache.contains(2));
    assert(cache.contains(3));
    assert(cache.size() == 2);

    const bool erased2 = cache.erase(2);

    assert(erased2);
    assert(!cache.contains(2));
    assert(cache.contains(3));
    assert(cache.size() == 1);

    const bool erased3 = cache.erase(3);

    assert(erased3);
    assert(cache.empty());
    assert(cache.size() == 0);

    std::cout << "PASS: erase\n";
}

void test_set_capacity() noexcept {
    Cache cache(4);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));
    (void) cache.insert(3, make_value(30));
    (void) cache.insert(4, make_value(40));

    // Make key 1 most recently used.
    assert(cache.lookup(1));

    cache.set_capacity(2);

    assert(cache.capacity() == 2);
    assert(cache.size() == 2);
    assert(cache.contains(1));
    assert(cache.contains(4));
    assert(!cache.contains(2));
    assert(!cache.contains(3));

    // Increasing capacity must not remove existing entries.
    cache.set_capacity(5);

    assert(cache.capacity() == 5);
    assert(cache.size() == 2);
    assert(cache.contains(1));
    assert(cache.contains(4));

    // Capacity zero removes all unpinned entries.
    cache.set_capacity(0);

    assert(cache.capacity() == 0);
    assert(cache.empty());

    // Inserting with zero capacity returns the value without caching it.
    const auto value = cache.insert(5, make_value(50));

    assert(value && *value == 50);
    assert(cache.empty());
    assert(!cache.contains(5));

    std::cout << "PASS: set capacity\n";
}

void test_zero_capacity_and_null_value() noexcept {
    Cache cache(0);

    const auto value = cache.insert(1, make_value(10));

    assert(value && *value == 10);
    assert(cache.empty());
    assert(cache.size() == 0);
    assert(!cache.contains(1));

    const auto nullValue = cache.insert(2, nullptr);

    assert(!nullValue);
    assert(cache.empty());

    cache.set_capacity(2);

    const auto inserted = cache.insert(3, make_value(30));

    assert(inserted && *inserted == 30);
    assert(cache.size() == 1);
    assert(cache.contains(3));

    const auto nullUpdate = cache.insert(3, nullptr);

    assert(!nullUpdate);

    // A null insertion must not replace the existing value.
    const auto existing = cache.lookup(3);

    assert(existing && *existing == 30);
    assert(cache.size() == 1);

    std::cout << "PASS: zero capacity and null value\n";
}

void test_lookup_and_pin() noexcept {
    Cache cache(3);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));

    const auto pinned1 = cache.lookup_and_pin(1);
    const auto pinned2 = cache.lookup_and_pin(2);

    assert(pinned1 && *pinned1 == 10);
    assert(pinned2 && *pinned2 == 20);
    assert(!cache.lookup_and_pin(3));

    // Pinned lookups must preserve the entries.
    assert(cache.contains(1));
    assert(cache.contains(2));
    assert(cache.size() == 2);

    assert(cache.unpin(1, pinned1));
    assert(cache.unpin(2, pinned2));

    // The pin counts have reached zero.
    assert(!cache.unpin(1, pinned1));
    assert(!cache.unpin(2, pinned2));

    assert(cache.size() == 2);

    std::cout << "PASS: lookup and pin\n";
}

void test_update_pinned_entry() noexcept {
    Cache cache(3);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));

    const auto oldValue = cache.lookup_and_pin(1);

    assert(oldValue && *oldValue == 10);

    const auto newValue = cache.insert(1, make_value(100));

    assert(newValue && *newValue == 100);
    assert(*oldValue == 10);
    assert(cache.size() == 2);
    assert(cache.contains(1));
    assert(cache.contains(2));

    // Active lookups must return the replacement.
    const auto currentValue = cache.lookup(1);

    assert(currentValue && *currentValue == 100);
    assert(currentValue != oldValue);

    // The old value must remain valid until it is unpinned.
    assert(cache.unpin(1, oldValue));
    assert(*oldValue == 10);

    // The retired entry has now been removed.
    assert(!cache.unpin(1, oldValue));

    // The replacement must remain active.
    const auto finalValue = cache.lookup(1);

    assert(finalValue && *finalValue == 100);
    assert(cache.size() == 2);

    std::cout << "PASS: update pinned entry\n";
}

void test_multiple_pins_and_replacement() noexcept {
    Cache cache(3);

    (void) cache.insert(1, make_value(10));

    const auto pinned1 = cache.lookup_and_pin(1);
    const auto pinned2 = cache.lookup_and_pin(1);

    assert(pinned1 && pinned2);
    assert(pinned1 == pinned2);

    (void) cache.insert(1, make_value(100));

    const auto currentValue = cache.lookup(1);

    assert(currentValue && *currentValue == 100);
    assert(*pinned1 == 10);

    // Releasing only one pin must preserve the retired entry.
    assert(cache.unpin(1, pinned1));
    assert(*pinned2 == 10);

    // Releasing the final pin removes the retired entry.
    assert(cache.unpin(1, pinned2));
    assert(!cache.unpin(1, pinned1));

    const auto finalValue = cache.lookup(1);

    assert(finalValue && *finalValue == 100);
    assert(cache.size() == 1);

    std::cout << "PASS: multiple pins and replacement\n";
}

void test_erase_pinned_entry() noexcept {
    Cache cache(3);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));

    const auto pinnedValue = cache.lookup_and_pin(1);

    assert(pinnedValue && *pinnedValue == 10);

    const bool erased = cache.erase(1);

    assert(erased);
    assert(!cache.contains(1));
    assert(cache.contains(2));
    assert(cache.size() == 1);

    // The retired value remains valid after erasure.
    assert(*pinnedValue == 10);
    assert(cache.unpin(1, pinnedValue));

    // The retired entry has been released.
    assert(!cache.unpin(1, pinnedValue));
    assert(cache.size() == 1);

    std::cout << "PASS: erase pinned entry\n";
}

void test_eviction_skips_pinned_entries() noexcept {
    Cache cache(2);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));

    const auto pinnedValue = cache.lookup_and_pin(1);

    assert(pinnedValue && *pinnedValue == 10);

    // Key 1 is pinned, so key 2 must be evicted.
    (void) cache.insert(3, make_value(30));

    assert(cache.size() == 2);
    assert(cache.contains(1));
    assert(!cache.contains(2));
    assert(cache.contains(3));
    assert(*pinnedValue == 10);

    assert(cache.unpin(1, pinnedValue));

    std::cout << "PASS: eviction skips pinned entries\n";
}

void test_capacity_reduction_with_pinned_entries() noexcept {
    Cache cache(3);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));
    (void) cache.insert(3, make_value(30));

    const auto value1 = cache.lookup_and_pin(1);
    const auto value2 = cache.lookup_and_pin(2);
    const auto value3 = cache.lookup_and_pin(3);

    assert(value1 && *value1 == 10);
    assert(value2 && *value2 == 20);
    assert(value3 && *value3 == 30);
    assert(cache.size() == 3);

    cache.set_capacity(1);

    assert(cache.capacity() == 1);
    assert(cache.size() == 3);
    assert(cache.contains(1));
    assert(cache.contains(2));
    assert(cache.contains(3));

    // Unpinning key 1 allows eviction of unpinned entries,
    // but keys 2 and 3 remain pinned.
    assert(cache.unpin(1, value1));

    assert(cache.capacity() == 1);
    assert(cache.size() == 2);
    assert(!cache.contains(1));
    assert(cache.contains(2));
    assert(cache.contains(3));

    // Unpinning key 2 allows the cache to reach capacity.
    assert(cache.unpin(2, value2));

    assert(cache.size() == 1);
    assert(!cache.contains(2));
    assert(cache.contains(3));

    // Key 3 is still pinned and its value remains valid.
    assert(*value3 == 30);

    assert(cache.unpin(3, value3));

    assert(cache.size() == 1);
    assert(cache.capacity() == 1);
    assert(cache.contains(3));

    std::cout << "PASS: capacity reduction with pinned entries\n";
}

void test_unpin_triggers_eviction_1() noexcept {
    Cache cache(3);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));
    (void) cache.insert(3, make_value(30));

    const auto pinned1 = cache.lookup_and_pin(1);
    const auto pinned2 = cache.lookup_and_pin(2);
    const auto pinned3 = cache.lookup_and_pin(3);

    assert(pinned1 && pinned2 && pinned3);
    assert(cache.size() == 3);

    // Reduce capacity while all entries are pinned.
    cache.set_capacity(1);

    assert(cache.capacity() == 1);
    assert(cache.size() == 3);

    // Unpinning retries eviction.
    assert(cache.unpin(1, pinned1));
    assert(cache.size() <= 3);
    assert(cache.size() >= 1);

    assert(cache.unpin(2, pinned2));
    assert(cache.size() <= 3);
    assert(cache.size() >= 1);

    assert(cache.unpin(3, pinned3));

    assert(cache.size() == 1);
    assert(cache.capacity() == 1);
    assert(cache.contains(3));

    std::cout << "PASS: unpin triggers eviction 1\n";
}

void test_unpin_triggers_eviction_2() noexcept {
    Cache cache(3);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));
    (void) cache.insert(3, make_value(30));

    const auto pinned1 = cache.lookup_and_pin(1);
    const auto pinned2 = cache.lookup_and_pin(2);
    const auto pinned3 = cache.lookup_and_pin(3);

    assert(pinned1 && *pinned1 == 10);
    assert(pinned2 && *pinned2 == 20);
    assert(pinned3 && *pinned3 == 30);
    assert(cache.size() == 3);

    // All entries are pinned, so reducing capacity cannot evict them.
    cache.set_capacity(1);

    assert(cache.capacity() == 1);
    assert(cache.size() == 3);
    assert(cache.contains(1));
    assert(cache.contains(2));
    assert(cache.contains(3));

    // Key 1 becomes unpinned and is the LRU entry, so it is evicted.
    assert(cache.unpin(1, pinned1));

    assert(cache.size() == 2);
    assert(!cache.contains(1));
    assert(cache.contains(2));
    assert(cache.contains(3));

    // Key 2 becomes unpinned and is now the LRU entry, so it is evicted.
    assert(cache.unpin(2, pinned2));

    assert(cache.size() == 1);
    assert(!cache.contains(1));
    assert(!cache.contains(2));
    assert(cache.contains(3));

    // Key 3 remains active; unpinning it does not require further eviction.
    assert(cache.unpin(3, pinned3));

    assert(cache.size() == 1);
    assert(cache.capacity() == 1);
    assert(cache.contains(3));

    // Previously returned shared pointers remain valid after eviction.
    assert(*pinned1 == 10);
    assert(*pinned2 == 20);
    assert(*pinned3 == 30);

    std::cout << "PASS: unpin triggers eviction 2\n";
}

void test_retired_entry_index() noexcept {
    Cache cache(4);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));

    const auto pinned1 = cache.lookup_and_pin(1);
    const auto pinned2 = cache.lookup_and_pin(2);

    assert(pinned1 && pinned2);

    // Retire both active entries.
    assert(cache.erase(1));
    assert(cache.erase(2));

    assert(cache.empty());
    assert(!cache.contains(1));
    assert(!cache.contains(2));

    // Both retired entries must still be discoverable by unpin().
    assert(cache.unpin(1, pinned1));
    assert(cache.unpin(2, pinned2));

    assert(!cache.unpin(1, pinned1));
    assert(!cache.unpin(2, pinned2));
    assert(cache.empty());

    std::cout << "PASS: retired entry index\n";
}

void test_multithreaded_access() noexcept {
    Cache cache(128);

    constexpr int ThreadCount = 4;
    constexpr int Iterations  = 10000;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&cache, t]() {
            for (int i = 0; i < Iterations; ++i)
            {
                const int key = (i + t * Iterations) % 256;

                (void) cache.insert(key, make_value(key));
                (void) cache.lookup(key);
                (void) cache.contains(key);

                if (i % 3 == 0)
                    (void) cache.erase(key);
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    assert(cache.size() <= cache.capacity());

    std::cout << "PASS: multithreaded access\n";
}

void test_repeated_pinning() noexcept {
    Cache cache(1);

    (void) cache.insert(1, make_value(10));

    const auto value = cache.lookup_and_pin(1);
    assert(value && *value == 10);

    const auto secondPin = cache.lookup_and_pin(1);
    assert(secondPin == value);

    (void) cache.insert(2, make_value(20));

    // Key 1 is pinned twice; key 2 is eligible for eviction.
    assert(cache.contains(1));
    assert(!cache.contains(2));
    assert(cache.size() == 1);

    // Releasing one pin must not make key 1 evictable.
    assert(cache.unpin(1, value));

    assert(cache.contains(1));
    assert(cache.size() == 1);

    // Releasing the final pin leaves key 1 active.
    assert(cache.unpin(1, value));

    assert(cache.contains(1));
    assert(cache.size() == 1);
    assert(cache.capacity() == 1);

    std::cout << "PASS: repeated pinning\n";
}

void test_wrong_value_on_unpin() noexcept {
    Cache cache(2);

    (void) cache.insert(1, make_value(10));

    const auto pinned     = cache.lookup_and_pin(1);
    const auto wrongValue = make_value(10);

    assert(pinned && wrongValue != pinned);
    assert(!cache.unpin(1, wrongValue));
    assert(cache.contains(1));
    assert(cache.size() == 1);

    // The original pin must still be valid.
    assert(cache.unpin(1, pinned));

    std::cout << "PASS: wrong value on unpin\n";
}

void test_double_unpin() noexcept {
    Cache cache(2);

    (void) cache.insert(1, make_value(10));

    const auto value = cache.lookup_and_pin(1);

    assert(value);
    assert(cache.unpin(1, value));
    assert(!cache.unpin(1, value));

    assert(cache.contains(1));
    assert(cache.size() == 1);

    std::cout << "PASS: double unpin\n";
}

void test_retired_entry_with_multiple_pins() noexcept {
    Cache cache(2);

    (void) cache.insert(1, make_value(10));

    const auto firstPin  = cache.lookup_and_pin(1);
    const auto secondPin = cache.lookup_and_pin(1);

    assert(firstPin && secondPin);
    assert(firstPin == secondPin);

    // Erase retires the entry because it still has two pins.
    assert(cache.erase(1));
    assert(!cache.contains(1));
    assert(cache.size() == 0);

    // The first unpin must leave one outstanding pin.
    assert(cache.unpin(1, firstPin));
    assert(!cache.contains(1));
    assert(*secondPin == 10);

    // The second unpin releases the final pin and removes the retired entry.
    assert(cache.unpin(1, secondPin));
    assert(!cache.unpin(1, secondPin));

    std::cout << "PASS: retired entry with multiple pins\n";
}

void test_zero_capacity_with_pinned_entries() noexcept {
    Cache cache(2);

    (void) cache.insert(1, make_value(10));
    (void) cache.insert(2, make_value(20));

    const auto pinned = cache.lookup_and_pin(1);

    cache.set_capacity(0);

    assert(cache.capacity() == 0);
    assert(cache.contains(1));
    assert(!cache.contains(2));
    assert(cache.size() == 1);

    assert(cache.unpin(1, pinned));

    assert(cache.empty());
    assert(cache.size() == 0);

    std::cout << "PASS: zero capacity with pinned entries\n";
}

void test_replacement_preserves_old_generation() noexcept {
    Cache cache(2);

    const auto oldValue  = cache.insert(1, make_value(10));
    const auto pinnedOld = cache.lookup_and_pin(1);

    assert(oldValue && pinnedOld == oldValue);

    const auto newValue = cache.insert(1, make_value(20));

    assert(newValue && *newValue == 20);
    assert(newValue != oldValue);
    assert(cache.size() == 1);
    assert(cache.contains(1));

    // The old generation remains alive while pinned.
    assert(*pinnedOld == 10);
    assert(cache.lookup(1) == newValue);

    assert(cache.unpin(1, pinnedOld));

    assert(cache.size() == 1);
    assert(cache.lookup(1) == newValue);

    std::cout << "PASS: replacement preserves old generation\n";
}

void test_reinsert_same_value_after_retirement() noexcept {
    Cache cache(2);

    const auto value = make_value(10);

    // First generation: insert, pin, and retire.
    assert(cache.insert(1, value) == value);

    const auto firstPin = cache.lookup_and_pin(1);
    assert(firstPin == value);

    assert(cache.erase(1));
    assert(!cache.contains(1));
    assert(cache.size() == 0);

    // Second generation: reinsert the exact same shared_ptr.
    assert(cache.insert(1, value) == value);

    const auto secondPin = cache.lookup_and_pin(1);
    assert(secondPin == value);

    // Retire the second generation while both generations remain pinned.
    assert(cache.erase(1));
    assert(!cache.contains(1));
    assert(cache.size() == 0);

    // Both generations use the same (key, ValuePtr) index.
    // The current implementation may assert during the second erase.
    assert(cache.unpin(1, firstPin));

    // If the first unpin succeeded, check whether the second generation
    // can also be released.
    assert(cache.unpin(1, secondPin));

    assert(!cache.unpin(1, value));
    assert(cache.empty());

    std::cout << "PASS: reinsert same value after retirement\n";
}

}  // namespace

int main() {
    test_insert_and_lookup();
    test_lru_eviction();
    test_update_existing_key();
    test_erase();
    test_set_capacity();
    test_zero_capacity_and_null_value();
    test_lookup_and_pin();
    test_update_pinned_entry();
    test_multiple_pins_and_replacement();
    test_erase_pinned_entry();
    test_eviction_skips_pinned_entries();
    test_capacity_reduction_with_pinned_entries();
    test_unpin_triggers_eviction_1();
    test_unpin_triggers_eviction_2();
    test_retired_entry_index();
    test_multithreaded_access();
    test_repeated_pinning();
    test_wrong_value_on_unpin();
    test_double_unpin();
    test_retired_entry_with_multiple_pins();
    test_zero_capacity_with_pinned_entries();
    test_replacement_preserves_old_generation();
    test_reinsert_same_value_after_retirement();

    std::cout << "\nAll tests passed.\n";
}
