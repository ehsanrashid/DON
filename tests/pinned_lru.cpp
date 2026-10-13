/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src \
    tests/pinned_lru.cpp \
    $(ls src/*.cpp | grep -v 'src/main.cpp') \
    src/nnue/*.cpp \
    src/nnue/features/*.cpp \
    src/book/*.cpp \
    src/tablebase/*.cpp \
    src/utility/*.cpp \
    -o build/pinned_lru \
    -lpthread
g++ -std=c++17 -O2 -I./src \
    tests/pinned_lru.cpp \
    $(for f in src/*.cpp; do [[ "$f" != "src/main.cpp" ]] && echo "$f"; done) \
    src/nnue/*.cpp \
    src/nnue/features/*.cpp \
    src/book/*.cpp \
    src/tablebase/*.cpp \
    src/utility/*.cpp \
    -o build/pinned_lru \
    -lpthread
g++ -std=c++17 -O2 -I./src `
    tests/pinned_lru.cpp `
    $(Get-ChildItem src/*.cpp | Where-Object { $_.Name -ne "main.cpp" } | ForEach-Object { $_.FullName }) `
    src/nnue/*.cpp `
    src/nnue/features/*.cpp `
    src/book/*.cpp `
    src/tablebase/*.cpp `
    src/utility/*.cpp `
    -o build/pinned_lru `
    -lpthread

./build/pinned_lru
*/

#include "utility/lru.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace DON;

namespace {

using LRU      = PinnedLRU<int, int>;
using ValuePtr = LRU::ValuePtr;

ValuePtr make_value(const int value) noexcept { return std::make_shared<int>(value); }

void test_insert_and_lookup() noexcept {
    LRU lru(3);

    const auto value1 = lru.insert(1, make_value(10));
    const auto value2 = lru.insert(2, make_value(20));
    const auto value3 = lru.insert(3, make_value(30));

    assert(value1 && *value1 == 10);
    assert(value2 && *value2 == 20);
    assert(value3 && *value3 == 30);
    assert(lru.size() == 3);
    assert(!lru.empty());
    assert(lru.capacity() == 3);

    const auto lookup1 = lru.lookup(1);
    const auto lookup2 = lru.lookup(2);
    const auto lookup3 = lru.lookup(3);

    assert(lookup1 && *lookup1 == 10);
    assert(lookup2 && *lookup2 == 20);
    assert(lookup3 && *lookup3 == 30);

    assert(lru.contains(1));
    assert(lru.contains(2));
    assert(lru.contains(3));
    assert(!lru.contains(4));
    assert(!lru.lookup(4));

    assert(lru.size() == 3);

    std::cout << "PASS: insert and lookup\n";
}

void test_lru_eviction() noexcept {
    LRU lru(3);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));
    (void) lru.insert(3, make_value(30));

    // Make key 1 the most recently used entry.
    assert(lru.lookup(1));

    // Key 2 becomes the least recently used entry.
    (void) lru.insert(4, make_value(40));

    assert(lru.size() == 3);
    assert(lru.contains(1));
    assert(!lru.contains(2));
    assert(lru.contains(3));
    assert(lru.contains(4));

    // Looking up key 3 makes key 1 the least recently used.
    assert(lru.lookup(3));
    (void) lru.insert(5, make_value(50));

    assert(lru.size() == 3);
    assert(!lru.contains(1));
    assert(lru.contains(3));
    assert(lru.contains(4));
    assert(lru.contains(5));

    std::cout << "PASS: LRU eviction\n";
}

void test_update_existing_key() noexcept {
    LRU lru(3);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));
    (void) lru.insert(3, make_value(30));

    const auto updated1 = lru.insert(1, make_value(100));

    assert(updated1 && *updated1 == 100);
    assert(lru.size() == 3);
    assert(lru.contains(1));
    assert(lru.contains(2));
    assert(lru.contains(3));

    const auto updated2 = lru.insert(2, make_value(200));

    assert(updated2 && *updated2 == 200);
    assert(lru.size() == 3);

    // Key 2 is MRU, key 1 is next, and key 3 is LRU.
    (void) lru.insert(4, make_value(40));

    assert(lru.size() == 3);
    assert(lru.contains(1));
    assert(lru.contains(2));
    assert(!lru.contains(3));
    assert(lru.contains(4));

    const auto lookup1 = lru.lookup(1);
    const auto lookup2 = lru.lookup(2);
    const auto lookup4 = lru.lookup(4);

    assert(lookup1 && *lookup1 == 100);
    assert(lookup2 && *lookup2 == 200);
    assert(lookup4 && *lookup4 == 40);

    std::cout << "PASS: update existing key\n";
}

void test_erase() noexcept {
    LRU lru(3);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));
    (void) lru.insert(3, make_value(30));

    const bool erased1     = lru.erase(1);
    const bool erasedAgain = lru.erase(1);

    assert(erased1);
    assert(!erasedAgain);
    assert(!lru.contains(1));
    assert(lru.contains(2));
    assert(lru.contains(3));
    assert(lru.size() == 2);

    const bool erased2 = lru.erase(2);

    assert(erased2);
    assert(!lru.contains(2));
    assert(lru.contains(3));
    assert(lru.size() == 1);

    const bool erased3 = lru.erase(3);

    assert(erased3);
    assert(lru.empty());
    assert(lru.size() == 0);

    std::cout << "PASS: erase\n";
}

void test_set_capacity() noexcept {
    LRU lru(4);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));
    (void) lru.insert(3, make_value(30));
    (void) lru.insert(4, make_value(40));

    // Make key 1 most recently used.
    assert(lru.lookup(1));

    lru.set_capacity(2);

    assert(lru.capacity() == 2);
    assert(lru.size() == 2);
    assert(lru.contains(1));
    assert(lru.contains(4));
    assert(!lru.contains(2));
    assert(!lru.contains(3));

    // Increasing capacity must not remove existing entries.
    lru.set_capacity(5);

    assert(lru.capacity() == 5);
    assert(lru.size() == 2);
    assert(lru.contains(1));
    assert(lru.contains(4));

    // Capacity zero removes all unpinned entries.
    lru.set_capacity(0);

    assert(lru.capacity() == 0);
    assert(lru.empty());

    // Inserting with zero capacity returns the value without caching it.
    const auto value = lru.insert(5, make_value(50));

    assert(value && *value == 50);
    assert(lru.empty());
    assert(!lru.contains(5));

    std::cout << "PASS: set capacity\n";
}

void test_zero_capacity_and_null_value() noexcept {
    LRU lru(0);

    const auto value = lru.insert(1, make_value(10));

    assert(value && *value == 10);
    assert(lru.empty());
    assert(lru.size() == 0);
    assert(!lru.contains(1));

    const auto nullValue = lru.insert(2, nullptr);

    assert(!nullValue);
    assert(lru.empty());

    lru.set_capacity(2);

    const auto inserted = lru.insert(3, make_value(30));

    assert(inserted && *inserted == 30);
    assert(lru.size() == 1);
    assert(lru.contains(3));

    const auto nullUpdate = lru.insert(3, nullptr);

    assert(!nullUpdate);

    // A null insertion must not replace the existing value.
    const auto existing = lru.lookup(3);

    assert(existing && *existing == 30);
    assert(lru.size() == 1);

    std::cout << "PASS: zero capacity and null value\n";
}

void test_lookup_and_pin() noexcept {
    LRU lru(3);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));

    const auto pinned1 = lru.lookup_and_pin(1);
    const auto pinned2 = lru.lookup_and_pin(2);

    assert(pinned1 && *pinned1 == 10);
    assert(pinned2 && *pinned2 == 20);
    assert(!lru.lookup_and_pin(3));

    // Pinned lookups must preserve the entries.
    assert(lru.contains(1));
    assert(lru.contains(2));
    assert(lru.size() == 2);

    assert(lru.unpin(1, pinned1));
    assert(lru.unpin(2, pinned2));

    // The pin counts have reached zero.
    assert(!lru.unpin(1, pinned1));
    assert(!lru.unpin(2, pinned2));

    assert(lru.size() == 2);

    std::cout << "PASS: lookup and pin\n";
}

void test_update_pinned_entry() noexcept {
    LRU lru(3);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));

    const auto oldValue = lru.lookup_and_pin(1);

    assert(oldValue && *oldValue == 10);

    const auto newValue = lru.insert(1, make_value(100));

    assert(newValue && *newValue == 100);
    assert(*oldValue == 10);
    assert(lru.size() == 2);
    assert(lru.contains(1));
    assert(lru.contains(2));

    // Active lookups must return the replacement.
    const auto currentValue = lru.lookup(1);

    assert(currentValue && *currentValue == 100);
    assert(currentValue != oldValue);

    // The old value must remain valid until it is unpinned.
    assert(lru.unpin(1, oldValue));
    assert(*oldValue == 10);

    // The retired entry has now been removed.
    assert(!lru.unpin(1, oldValue));

    // The replacement must remain active.
    const auto finalValue = lru.lookup(1);

    assert(finalValue && *finalValue == 100);
    assert(lru.size() == 2);

    std::cout << "PASS: update pinned entry\n";
}

void test_multiple_pins_and_replacement() noexcept {
    LRU lru(3);

    (void) lru.insert(1, make_value(10));

    const auto pinned1 = lru.lookup_and_pin(1);
    const auto pinned2 = lru.lookup_and_pin(1);

    assert(pinned1 && pinned2);
    assert(pinned1 == pinned2);

    (void) lru.insert(1, make_value(100));

    const auto currentValue = lru.lookup(1);

    assert(currentValue && *currentValue == 100);
    assert(*pinned1 == 10);

    // Releasing only one pin must preserve the retired entry.
    assert(lru.unpin(1, pinned1));
    assert(*pinned2 == 10);

    // Releasing the final pin removes the retired entry.
    assert(lru.unpin(1, pinned2));
    assert(!lru.unpin(1, pinned1));

    const auto finalValue = lru.lookup(1);

    assert(finalValue && *finalValue == 100);
    assert(lru.size() == 1);

    std::cout << "PASS: multiple pins and replacement\n";
}

void test_erase_pinned_entry() noexcept {
    LRU lru(3);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));

    const auto pinnedValue = lru.lookup_and_pin(1);

    assert(pinnedValue && *pinnedValue == 10);

    const bool erased = lru.erase(1);

    assert(erased);
    assert(!lru.contains(1));
    assert(lru.contains(2));
    assert(lru.size() == 1);

    // The retired value remains valid after erasure.
    assert(*pinnedValue == 10);
    assert(lru.unpin(1, pinnedValue));

    // The retired entry has been released.
    assert(!lru.unpin(1, pinnedValue));
    assert(lru.size() == 1);

    std::cout << "PASS: erase pinned entry\n";
}

void test_eviction_skips_pinned_entries() noexcept {
    LRU lru(2);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));

    const auto pinnedValue = lru.lookup_and_pin(1);

    assert(pinnedValue && *pinnedValue == 10);

    // Key 1 is pinned, so key 2 must be evicted.
    (void) lru.insert(3, make_value(30));

    assert(lru.size() == 2);
    assert(lru.contains(1));
    assert(!lru.contains(2));
    assert(lru.contains(3));
    assert(*pinnedValue == 10);

    assert(lru.unpin(1, pinnedValue));

    std::cout << "PASS: eviction skips pinned entries\n";
}

void test_capacity_reduction_with_pinned_entries() noexcept {
    LRU lru(3);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));
    (void) lru.insert(3, make_value(30));

    const auto value1 = lru.lookup_and_pin(1);
    const auto value2 = lru.lookup_and_pin(2);
    const auto value3 = lru.lookup_and_pin(3);

    assert(value1 && *value1 == 10);
    assert(value2 && *value2 == 20);
    assert(value3 && *value3 == 30);
    assert(lru.size() == 3);

    lru.set_capacity(1);

    assert(lru.capacity() == 1);
    assert(lru.size() == 3);
    assert(lru.contains(1));
    assert(lru.contains(2));
    assert(lru.contains(3));

    // Unpinning key 1 allows eviction of unpinned entries,
    // but keys 2 and 3 remain pinned.
    assert(lru.unpin(1, value1));

    assert(lru.capacity() == 1);
    assert(lru.size() == 2);
    assert(!lru.contains(1));
    assert(lru.contains(2));
    assert(lru.contains(3));

    // Unpinning key 2 allows the lru to reach capacity.
    assert(lru.unpin(2, value2));

    assert(lru.size() == 1);
    assert(!lru.contains(2));
    assert(lru.contains(3));

    // Key 3 is still pinned and its value remains valid.
    assert(*value3 == 30);

    assert(lru.unpin(3, value3));

    assert(lru.size() == 1);
    assert(lru.capacity() == 1);
    assert(lru.contains(3));

    std::cout << "PASS: capacity reduction with pinned entries\n";
}

void test_unpin_triggers_eviction_1() noexcept {
    LRU lru(3);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));
    (void) lru.insert(3, make_value(30));

    const auto pinned1 = lru.lookup_and_pin(1);
    const auto pinned2 = lru.lookup_and_pin(2);
    const auto pinned3 = lru.lookup_and_pin(3);

    assert(pinned1 && pinned2 && pinned3);
    assert(lru.size() == 3);

    // Reduce capacity while all entries are pinned.
    lru.set_capacity(1);

    assert(lru.capacity() == 1);
    assert(lru.size() == 3);

    // Unpinning retries eviction.
    assert(lru.unpin(1, pinned1));
    assert(lru.size() <= 3);
    assert(lru.size() >= 1);

    assert(lru.unpin(2, pinned2));
    assert(lru.size() <= 3);
    assert(lru.size() >= 1);

    assert(lru.unpin(3, pinned3));

    assert(lru.size() == 1);
    assert(lru.capacity() == 1);
    assert(lru.contains(3));

    std::cout << "PASS: unpin triggers eviction 1\n";
}

void test_unpin_triggers_eviction_2() noexcept {
    LRU lru(3);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));
    (void) lru.insert(3, make_value(30));

    const auto pinned1 = lru.lookup_and_pin(1);
    const auto pinned2 = lru.lookup_and_pin(2);
    const auto pinned3 = lru.lookup_and_pin(3);

    assert(pinned1 && *pinned1 == 10);
    assert(pinned2 && *pinned2 == 20);
    assert(pinned3 && *pinned3 == 30);
    assert(lru.size() == 3);

    // All entries are pinned, so reducing capacity cannot evict them.
    lru.set_capacity(1);

    assert(lru.capacity() == 1);
    assert(lru.size() == 3);
    assert(lru.contains(1));
    assert(lru.contains(2));
    assert(lru.contains(3));

    // Key 1 becomes unpinned and is the LRU entry, so it is evicted.
    assert(lru.unpin(1, pinned1));

    assert(lru.size() == 2);
    assert(!lru.contains(1));
    assert(lru.contains(2));
    assert(lru.contains(3));

    // Key 2 becomes unpinned and is now the LRU entry, so it is evicted.
    assert(lru.unpin(2, pinned2));

    assert(lru.size() == 1);
    assert(!lru.contains(1));
    assert(!lru.contains(2));
    assert(lru.contains(3));

    // Key 3 remains active; unpinning it does not require further eviction.
    assert(lru.unpin(3, pinned3));

    assert(lru.size() == 1);
    assert(lru.capacity() == 1);
    assert(lru.contains(3));

    // Previously returned shared pointers remain valid after eviction.
    assert(*pinned1 == 10);
    assert(*pinned2 == 20);
    assert(*pinned3 == 30);

    std::cout << "PASS: unpin triggers eviction 2\n";
}

void test_retired_entry_index() noexcept {
    LRU lru(4);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));

    const auto pinned1 = lru.lookup_and_pin(1);
    const auto pinned2 = lru.lookup_and_pin(2);

    assert(pinned1 && pinned2);

    // Retire both active entries.
    assert(lru.erase(1));
    assert(lru.erase(2));

    assert(lru.empty());
    assert(!lru.contains(1));
    assert(!lru.contains(2));

    // Both retired entries must still be discoverable by unpin().
    assert(lru.unpin(1, pinned1));
    assert(lru.unpin(2, pinned2));

    assert(!lru.unpin(1, pinned1));
    assert(!lru.unpin(2, pinned2));
    assert(lru.empty());

    std::cout << "PASS: retired entry index\n";
}

void test_multithreaded_access() noexcept {
    LRU lru(128);

    constexpr int ThreadCount = 4;
    constexpr int Iterations  = 10000;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&lru, t]() {
            for (int i = 0; i < Iterations; ++i)
            {
                const int key = (i + t * Iterations) % 256;

                (void) lru.insert(key, make_value(key));
                (void) lru.lookup(key);
                (void) lru.contains(key);

                if (i % 3 == 0)
                    (void) lru.erase(key);
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    assert(lru.size() <= lru.capacity());

    std::cout << "PASS: multithreaded access\n";
}

void test_repeated_pinning() noexcept {
    LRU lru(1);

    (void) lru.insert(1, make_value(10));

    const auto value = lru.lookup_and_pin(1);
    assert(value && *value == 10);

    const auto secondPin = lru.lookup_and_pin(1);
    assert(secondPin == value);

    (void) lru.insert(2, make_value(20));

    // Key 1 is pinned twice; key 2 is eligible for eviction.
    assert(lru.contains(1));
    assert(!lru.contains(2));
    assert(lru.size() == 1);

    // Releasing one pin must not make key 1 evictable.
    assert(lru.unpin(1, value));

    assert(lru.contains(1));
    assert(lru.size() == 1);

    // Releasing the final pin leaves key 1 active.
    assert(lru.unpin(1, value));

    assert(lru.contains(1));
    assert(lru.size() == 1);
    assert(lru.capacity() == 1);

    std::cout << "PASS: repeated pinning\n";
}

void test_wrong_value_on_unpin() noexcept {
    LRU lru(2);

    (void) lru.insert(1, make_value(10));

    const auto pinned     = lru.lookup_and_pin(1);
    const auto wrongValue = make_value(10);

    assert(pinned && wrongValue != pinned);
    assert(!lru.unpin(1, wrongValue));
    assert(lru.contains(1));
    assert(lru.size() == 1);

    // The original pin must still be valid.
    assert(lru.unpin(1, pinned));

    std::cout << "PASS: wrong value on unpin\n";
}

void test_double_unpin() noexcept {
    LRU lru(2);

    (void) lru.insert(1, make_value(10));

    const auto value = lru.lookup_and_pin(1);

    assert(value);
    assert(lru.unpin(1, value));
    assert(!lru.unpin(1, value));

    assert(lru.contains(1));
    assert(lru.size() == 1);

    std::cout << "PASS: double unpin\n";
}

void test_retired_entry_with_multiple_pins() noexcept {
    LRU lru(2);

    (void) lru.insert(1, make_value(10));

    const auto firstPin  = lru.lookup_and_pin(1);
    const auto secondPin = lru.lookup_and_pin(1);

    assert(firstPin && secondPin);
    assert(firstPin == secondPin);

    // Erase retires the entry because it still has two pins.
    assert(lru.erase(1));
    assert(!lru.contains(1));
    assert(lru.size() == 0);

    // The first unpin must leave one outstanding pin.
    assert(lru.unpin(1, firstPin));
    assert(!lru.contains(1));
    assert(*secondPin == 10);

    // The second unpin releases the final pin and removes the retired entry.
    assert(lru.unpin(1, secondPin));
    assert(!lru.unpin(1, secondPin));

    std::cout << "PASS: retired entry with multiple pins\n";
}

void test_zero_capacity_with_pinned_entries() noexcept {
    LRU lru(2);

    (void) lru.insert(1, make_value(10));
    (void) lru.insert(2, make_value(20));

    const auto pinned = lru.lookup_and_pin(1);

    lru.set_capacity(0);

    assert(lru.capacity() == 0);
    assert(lru.contains(1));
    assert(!lru.contains(2));
    assert(lru.size() == 1);

    assert(lru.unpin(1, pinned));

    assert(lru.empty());
    assert(lru.size() == 0);

    std::cout << "PASS: zero capacity with pinned entries\n";
}

void test_replacement_preserves_old_generation() noexcept {
    LRU lru(2);

    const auto oldValue  = lru.insert(1, make_value(10));
    const auto pinnedOld = lru.lookup_and_pin(1);

    assert(oldValue && pinnedOld == oldValue);

    const auto newValue = lru.insert(1, make_value(20));

    assert(newValue && *newValue == 20);
    assert(newValue != oldValue);
    assert(lru.size() == 1);
    assert(lru.contains(1));

    // The old generation remains alive while pinned.
    assert(*pinnedOld == 10);
    assert(lru.lookup(1) == newValue);

    assert(lru.unpin(1, pinnedOld));

    assert(lru.size() == 1);
    assert(lru.lookup(1) == newValue);

    std::cout << "PASS: replacement preserves old generation\n";
}

void test_reinsert_same_value_after_retirement() noexcept {
    LRU lru(2);

    const auto value = make_value(10);

    // First generation: insert, pin, and retire.
    assert(lru.insert(1, value) == value);

    const auto firstPin = lru.lookup_and_pin(1);
    assert(firstPin == value);

    assert(lru.erase(1));
    assert(!lru.contains(1));
    assert(lru.size() == 0);

    // Second generation: reinsert the exact same shared_ptr.
    assert(lru.insert(1, value) == value);

    const auto secondPin = lru.lookup_and_pin(1);
    assert(secondPin == value);

    // Retire the second generation while both generations remain pinned.
    assert(lru.erase(1));
    assert(!lru.contains(1));
    assert(lru.size() == 0);

    // Both generations use the same (key, ValuePtr) index.
    // The current implementation may assert during the second erase.
    assert(lru.unpin(1, firstPin));

    // If the first unpin succeeded, check whether the second generation
    // can also be released.
    assert(lru.unpin(1, secondPin));

    assert(!lru.unpin(1, value));
    assert(lru.empty());

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
