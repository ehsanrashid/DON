/*
  Deterministic ordering stability test.

  This test builds a list of items with intentionally duplicated "score" values using
  a deterministic (seeded) RNG. It then checks that a simple insertion-stable algorithm
  preserves relative order of equal-score elements and matches std::stable_sort result.

  The test is a lightweight replacement for exercising sort stability in move picking code.
*/

#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "misc.h"

using namespace DON;

namespace {

struct Item final {
   public:
    std::string to_string() const noexcept {
        return "Item(score=" + std::to_string(score) + ", id=" + std::to_string(id) + ")";
    }

    int         score;
    std::size_t id;  // original insertion order
};

bool item_descending(const Item& item1, const Item& item2) noexcept {
    return item1.score > item2.score;
}

// Simple stable insertion-like sort (descending) used in movepick for small ranges.
void insertion_stable_desc(std::vector<Item>& v) noexcept {
    for (std::size_t i = 1; i < v.size(); ++i)
    {
        Item        tmp = std::move(v[i]);
        std::size_t j   = i;
        // stable: first element equal should remain earlier
        while (j > 0 && item_descending(tmp, v[j - 1]))
        {
            v[j] = std::move(v[j - 1]);
            --j;
        }
        v[j] = std::move(tmp);
    }
}


template<typename Iterator, typename T, typename Compare>
Iterator
upper_bound_unrolled(Iterator beg, Iterator end, const T& value, Compare comp) noexcept {
    Iterator ins = end;  // default = end (not found)

    std::size_t n = end - beg;

    std::size_t i = n;

    // Process blocks of 8 elements
    while (ins == end && i >= BLOCK_8)
    {
        i -= BLOCK_8;

        Iterator base = beg + i;

        ins = comp(value, base[0]) ? base + 0
            : comp(value, base[1]) ? base + 1
            : comp(value, base[2]) ? base + 2
            : comp(value, base[3]) ? base + 3
            : comp(value, base[4]) ? base + 4
            : comp(value, base[5]) ? base + 5
            : comp(value, base[6]) ? base + 6
            : comp(value, base[7]) ? base + 7
                                   : ins;
    }
    // Process blocks of 4 elements
    while (ins == end && i >= BLOCK_4)
    {
        i -= BLOCK_4;

        Iterator base = beg + i;

        ins = comp(value, base[0]) ? base + 0
            : comp(value, base[1]) ? base + 1
            : comp(value, base[2]) ? base + 2
            : comp(value, base[3]) ? base + 3
                                   : ins;
    }
    // Handle remaining elements
    while (i >= 1)
    {
        --i;

        Iterator base = beg + i;

        ins = comp(value, *base) ? base : ins;
    }

    return ins;
}

}  // namespace

int main() {

    /*
   
    // Number of test items
    constexpr std::size_t N = 256;

    // Deterministic RNG
    std::mt19937_64 rng(123456789ULL);

    // Build test vector with many duplicates
    std::vector<Item> vec;
    vec.reserve(N);

    for (std::size_t i = 0; i < N; ++i)
    {
        // produce a limited range of scores to force many ties
        int score = int(rng() % 16) - 8;  // range [-8,7]
        vec.push_back(Item{score, i});
    }

    // Reference result using std::stable_sort
    std::vector<Item> ref = vec;
    std::stable_sort(ref.begin(), ref.end(), item_descending);

    // Test insertion-style stable sort
    std::vector<Item> test = vec;
    insertion_stable_desc(test);

    // Compare results
    bool ok = true;
    if (ref.size() != test.size())
        ok = false;
    else
    {
        for (std::size_t i = 0; i < ref.size(); ++i)
        {
            if (ref[i].score != test[i].score || ref[i].id != test[i].id)
            {
                ok = false;
                std::cerr << "Mismatch at index " << i << ": ref(score,id)=(" << ref[i].score << ","
                          << ref[i].id << ") vs test(" << test[i].score << "," << test[i].id
                          << ")\n";
                break;
            }
        }
    }

    if (!ok)
    {
        std::cerr << "movepick_ordering: stability test failed\n";
        return 1;
    }

    std::cout << "movepick_ordering: stability test passed\n";
    */

    // Number of test items
    constexpr std::size_t N = 30;

    std::vector<Item> data;
    data.reserve(N);

    std::cout << N << "\n";

    for (std::size_t i = 0; i < N; ++i)
        data.push_back(Item{int((N - 1 - i) * 2), i});

    std::mt19937                       rng(123);
    std::uniform_int_distribution<int> dist(0, int(N * 2));

    constexpr std::size_t Trials = 5000000;

    std::vector<int> tryData;
    tryData.reserve(Trials);

    for (std::size_t i = 0; i < Trials; ++i)
        tryData.push_back(dist(rng));

    // ----------------------------
    // Correctness check
    // ----------------------------
    for (std::size_t i = 0; i < 10000; ++i)
    {
        Item item{dist(rng), i};

        auto beg = data.begin();
        auto end = data.end();

        auto itr1 = std::upper_bound(beg, end, item, item_descending);
        auto itr2 = upper_bound_unrolled(beg, end, item, item_descending);

        if (itr1 != itr2)
        {
            std::cerr << "Mismatch for value " << item.to_string() << "\n";
            return 1;
        }
    }

    std::cout << "Correctness test passed\n";

    // ----------------------------
    // Benchmark std::upper_bound
    // ----------------------------
    auto std_t0 = std::chrono::high_resolution_clock::now();

    volatile std::size_t stdSink = 0;

    for (std::size_t i = 0; i < Trials; ++i)
    {
        Item item{tryData[i], i};

        auto itr = std::upper_bound(data.begin(), data.end(), item, item_descending);
        stdSink += (itr - data.begin());
    }

    auto std_t1 = std::chrono::high_resolution_clock::now();

    auto stdTime = std::chrono::duration_cast<std::chrono::microseconds>(std_t1 - std_t0).count();

    std::cout << "std::upper_bound        time: " << stdTime << " ms\n";

    // ----------------------------
    // Benchmark exponential_upper_bound
    // ----------------------------
    auto exp_t0 = std::chrono::high_resolution_clock::now();

    volatile std::size_t expSink = 0;

    for (std::size_t i = 0; i < Trials; ++i)
    {
        Item item{tryData[i], i};

        auto itr = upper_bound_unrolled(data.begin(), data.end(), item, item_descending);
        expSink += (itr - data.begin());
    }

    auto exp_t1 = std::chrono::high_resolution_clock::now();

    auto expTime = std::chrono::duration_cast<std::chrono::microseconds>(exp_t1 - exp_t0).count();

    std::cout << "exponential_upper_bound time: " << expTime << " ms\n";

    std::cout << "Time diff: " << std::abs(int(stdTime) - int(expTime)) << "\n";

    return 0;
}
