/*
  Deterministic ordering stability test.

  This test builds a list of items with intentionally duplicated "score" values using
  a deterministic (seeded) RNG. It then checks that a simple insertion-stable algorithm
  preserves relative order of equal-score elements and matches std::stable_sort result.

  The test is a lightweight replacement for exercising sort stability in move picking code.
*/

#include <algorithm>
#include <cassert>
#include <iostream>
#include <random>
#include <vector>

struct Item final {
   public:
    int score;
    int id;  // original insertion order
};

static bool score_desc(const Item& a, const Item& b) noexcept { return a.score > b.score; }

// Simple stable insertion-like sort (descending) used in movepick for small ranges.
static void insertion_stable_desc(std::vector<Item>& v) noexcept {
    for (std::size_t i = 1; i < v.size(); ++i)
    {
        Item        tmp = std::move(v[i]);
        std::size_t j   = i;
        // stable: first element equal should remain earlier
        while (j > 0 && score_desc(tmp, v[j - 1]))
        {
            v[j] = std::move(v[j - 1]);
            --j;
        }
        v[j] = std::move(tmp);
    }
}

int main() {
    // Deterministic RNG
    std::mt19937_64 rng(123456789ULL);

    // Build test vector with many duplicates
    constexpr std::size_t N = 256;
    std::vector<Item>     v;
    v.reserve(N);

    for (std::size_t i = 0; i < N; ++i)
    {
        // produce a limited range of scores to force many ties
        int score = int(rng() % 16) - 8;  // range [-8,7]
        v.push_back(Item{score, int(i)});
    }

    // Reference result using std::stable_sort
    std::vector<Item> ref = v;
    std::stable_sort(ref.begin(), ref.end(), score_desc);

    // Test insertion-style stable sort
    std::vector<Item> test = v;
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
    return 0;
}
