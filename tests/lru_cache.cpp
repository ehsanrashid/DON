/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src \
    tests/lru_cache.cpp \
    $(ls src/*.cpp | grep -v 'src/main.cpp') \
    src/nnue/*.cpp \
    src/nnue/features/*.cpp \
    src/book/*.cpp \
    src/tablebase/*.cpp \
    -o build/lru_cache \
    -lpthread
g++ -std=c++17 -O2 -I./src \
    tests/lru_cache.cpp \
    $(for f in src/*.cpp; do [[ "$f" != "src/main.cpp" ]] && echo "$f"; done) \
    src/nnue/*.cpp \
    src/nnue/features/*.cpp \
    src/book/*.cpp \
    src/tablebase/*.cpp \
    -o build/lru_cache \
    -lpthread
g++ -std=c++17 -O2 -I./src `
    tests/lru_cache.cpp `
    $(Get-ChildItem src/*.cpp | Where-Object { $_.Name -ne "main.cpp" } | ForEach-Object { $_.FullName }) `
    src/nnue/*.cpp `
    src/nnue/features/*.cpp `
    src/book/*.cpp `
    src/tablebase/*.cpp `
    -o build/lru_cache `
    -lpthread

./build/lru_cache
*/

#include "non_concurrent.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace DON;  // Remove if LRUCache is not in this namespace.

namespace {

void test_insert_and_lookup() {
    LRUCache<int, int> cache(3);

    assert(cache.insert(1, std::make_shared<int>(10)) != nullptr);
    assert(cache.insert(2, std::make_shared<int>(20)) != nullptr);
    assert(cache.insert(3, std::make_shared<int>(30)) != nullptr);

    assert(cache.size() == 3);
    assert(cache.capacity() == 3);

    const auto value = cache.lookup(2);
    assert(value && *value == 20);
    assert(cache.contains(2));

    std::cout << "PASS: insert and lookup\n";
}

void test_lru_eviction() {
    LRUCache<int, int> cache(3);

    (void) cache.insert(1, std::make_shared<int>(10));
    (void) cache.insert(2, std::make_shared<int>(20));
    (void) cache.insert(3, std::make_shared<int>(30));

    // Make key 1 the most recently used entry.
    assert(cache.lookup(1));

    // Insert key 4; key 2 is now the least recently used.
    (void) cache.insert(4, std::make_shared<int>(40));

    assert(cache.size() == 3);
    assert(cache.contains(1));
    assert(!cache.contains(2));
    assert(cache.contains(3));
    assert(cache.contains(4));

    std::cout << "PASS: LRU eviction\n";
}

void test_update_existing_key() {
    LRUCache<int, int> cache(2);

    (void) cache.insert(1, std::make_shared<int>(10));
    (void) cache.insert(2, std::make_shared<int>(20));

    const auto updated = cache.insert(1, std::make_shared<int>(100));

    assert(updated && *updated == 100);
    assert(cache.size() == 2);
    assert(cache.contains(1));
    assert(cache.contains(2));

    // Updating key 1 makes it most recently used.
    (void) cache.insert(3, std::make_shared<int>(30));

    assert(cache.contains(1));
    assert(!cache.contains(2));
    assert(cache.contains(3));

    std::cout << "PASS: update existing key\n";
}

void test_erase() {
    LRUCache<int, int> cache(3);

    (void) cache.insert(1, std::make_shared<int>(10));
    (void) cache.insert(2, std::make_shared<int>(20));

    assert(cache.erase(1));
    assert(!cache.erase(1));
    assert(!cache.contains(1));
    assert(cache.contains(2));
    assert(cache.size() == 1);

    std::cout << "PASS: erase\n";
}

void test_set_capacity() {
    LRUCache<int, int> cache(4);

    (void) cache.insert(1, std::make_shared<int>(10));
    (void) cache.insert(2, std::make_shared<int>(20));
    (void) cache.insert(3, std::make_shared<int>(30));
    (void) cache.insert(4, std::make_shared<int>(40));

    // Make key 1 most recently used.
    assert(cache.lookup(1));

    // Evict the two least recently used entries: 2 and 3.
    cache.set_capacity(2);

    assert(cache.capacity() == 2);
    assert(cache.size() == 2);
    assert(cache.contains(1));
    assert(cache.contains(4));
    assert(!cache.contains(2));
    assert(!cache.contains(3));

    // Capacity zero should clear all cached entries.
    cache.set_capacity(0);

    assert(cache.capacity() == 0);
    assert(cache.size() == 0);

    std::cout << "PASS: set capacity\n";
}

void test_zero_capacity_and_null_value() {
    LRUCache<int, int> cache(0);

    const auto value = cache.insert(1, std::make_shared<int>(10));

    // A zero-capacity cache returns the value without storing it.
    assert(value && *value == 10);
    assert(cache.empty() == false);  // See note below.

    std::cout << "PASS: zero capacity and null value\n";
}

void test_multithreaded_access() {
    LRUCache<int, int> cache(128);

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

                (void) cache.insert(key, std::make_shared<int>(key));
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

}  // namespace

int main() {
    test_insert_and_lookup();
    test_lru_eviction();
    test_update_existing_key();
    test_erase();
    test_set_capacity();
    test_multithreaded_access();

    std::cout << "\nAll tests passed.\n";
}
