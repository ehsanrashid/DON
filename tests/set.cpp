/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src tests/set.cpp -o build/set
g++ -std=c++17 -O2 -DNDEBUG -I./src tests/set.cpp -o build/set

./build/set
*/


#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <numeric>
#include <random>
#include <string_view>
#include <vector>
#include <set>

// Include your VectorSet header here.
#include "utility/set.h"

using namespace DON;

namespace {

using Value   = int;

using TestSet = VectorSet<Value, std::less<Value>>;
//using TestSet = VectorSet<Value, std::greater<Value>>;

constexpr usize TestSize = 100'000;

void print_block_distribution(const std::string_view name, const TestSet& set) {
    std::cout << '\n'
              << "=== " << name << " ===\n"
              << "Total elements = " << set.size() << '\n'
              << "Total blocks   = " << set.block_count() << '\n';

    for (usize i = 0; i < set.block_count(); ++i)
        std::cout << '[' << i << "] = " << set.block_size(i) << '\n';
}

void verify(const TestSet& set, const std::vector<Value>& expected) {
    assert(set.size() == expected.size());

    usize index = 0;

    set.for_each([&](const Value value) noexcept {
        assert(index < expected.size());
        assert(value == expected[index]);
        ++index;
    });

    assert(index == expected.size());

    usize totalBlockSize = 0;

    for (usize i = 0; i < set.block_count(); ++i)
    {
        const usize blockSize = set.block_size(i);

        assert(blockSize > 0);
        assert(blockSize < 128);

        totalBlockSize += blockSize;
    }

    assert(totalBlockSize == set.size());
}

void run_test(const std::string_view name, std::vector<Value> values) {
    TestSet set;
    set.reserve(values.size());

    for (const Value value : values)
    {
        const bool inserted = set.insert(value);
        assert(inserted);
    }

    // Expected iteration order is always ascending.
    std::sort(values.begin(), values.end());

    verify(set, values);
    print_block_distribution(name, set);
}

void print(TestSet& set) noexcept {
    for (usize i = 0; i < set.block_count(); ++i)
    {
        std::cout << '[' << i << "] = " << set.block_size(i) << '\n';
        //std::cout << set.block_size(i) << '\n';
        for (usize idx = 0; idx < set.block_size(i); ++idx)
            std::cout << set.block_content(i, idx) << " ";
        std::cout << '\n';
    }
    std::cout << "-----------------------\n";
}

void small_random_test() {
    constexpr usize TestSize = 1024;

    std::vector<Value> values(TestSize);
    std::iota(values.begin(), values.end(), 0);

    std::random_device rd;
    //std::mt19937       rng{123456};
    std::mt19937       rng{rd()};
    std::shuffle(values.begin(), values.end(), rng);

    TestSet set;
    set.reserve(values.size());

    for (const Value value : values)
        assert(set.insert(value));

    std::cout << "\n=== Small random insertion ===\n";
    std::cout << "Total elements = " << set.size() << '\n';
    std::cout << "Total blocks   = " << set.block_count() << '\n';

    for (usize i = 0; i < set.block_count(); ++i)
    {
        std::cout << '[' << i << "] = " << set.block_size(i) << '\n';
        //std::cout << set.block_size(i) << '\n';
        for (usize idx = 0; idx < set.block_size(i); ++idx)
            std::cout << set.block_content(i, idx) << " ";
        std::cout << '\n';
    }
}

void small_test() {
    std::vector<Value> values(TestSize);
    std::iota(values.begin(), values.end(), 0);

    // 1. Ascending insertion
    run_test("Ascending insertion", values);

    // 2. Descending insertion
    std::vector<Value> descending = values;
    std::reverse(descending.begin(), descending.end());
    run_test("Descending insertion", std::move(descending));

    // 3. Random insertion
    std::vector<Value> randomized = values;
    std::mt19937       rng{123456};
    std::shuffle(randomized.begin(), randomized.end(), rng);
    run_test("Random insertion", std::move(randomized));

    small_random_test();

    std::cout << "\nSmall VectorSet tests passed.\n";
}


struct Item final {
    int id;
    int priority;
};

struct ItemCompare final {
    bool operator()(const Item* lhs, const Item* rhs) const noexcept {
        if (lhs->priority != rhs->priority)
            return lhs->priority > rhs->priority;

        return lhs->id < rhs->id;
    }
};

using TestVectorSet = VectorSet<Item*, ItemCompare>;
using ReferenceSet  = std::set<Item*, ItemCompare>;

void verify(const TestVectorSet& actual, const ReferenceSet& expected) {
    assert(actual.size() == expected.size());

    auto  expectedItr = expected.begin();
    usize count       = 0;

    actual.for_each([&](Item* value) noexcept {
        assert(expectedItr != expected.end());
        assert(value == *expectedItr);
        ++expectedItr;
        ++count;
    });

    assert(expectedItr == expected.end());
    assert(count == actual.size());

    // Every block must be nonempty and internally sorted.
    for (usize b = 0; b < actual.block_count(); ++b)
    {
        const usize countInBlock = actual.block_size(b);
        assert(countInBlock > 0);

        for (usize i = 1; i < countInBlock; ++i)
        {
            Item* previous = actual.block_content(b, i - 1);
            Item* current  = actual.block_content(b, i);

            assert(ItemCompare{}(previous, current));
        }

        // Block boundaries must also be sorted.
        if (b > 0)
        {
            Item* previous = actual.block_content(b - 1, actual.block_size(b - 1) - 1);
            Item* current  = actual.block_content(b, 0);

            assert(ItemCompare{}(previous, current));
        }
    }
}

void stress_test_vector_set() {
    constexpr usize ItemCount        = 8192;
    constexpr usize PromotionCount   = 5000;
    constexpr usize RandomOperations = 50000;

    TestVectorSet actual;
    ReferenceSet  expected;

    std::vector<Item> items;
    items.reserve(ItemCount);

    for (usize i = 0; i < ItemCount; ++i)
        items.push_back({static_cast<int>(i), 0});

    // 1. Fill multiple blocks with equal-priority elements.
    for (Item& item : items)
    {
        assert(actual.insert(&item));
        assert(expected.insert(&item).second);
    }

    verify(actual, expected);

    // 2. Promote elements from the back toward the front.
    // Each promotion increases priority, potentially crossing many blocks.
    for (usize i = 0; i < PromotionCount; ++i)
    {
        Item& item = items[ItemCount - 1 - i];

        auto position = actual.find_position(&item);
        assert(position.has_value());

        assert(expected.erase(&item) == 1);

        ++item.priority;

        assert(actual.promote(*position));
        assert(expected.insert(&item).second);

        if (i % 100 == 0)
            verify(actual, expected);
    }

    verify(actual, expected);

    // 3. Repeatedly promote the same element.
    Item& hotItem = items.back();

    for (usize i = 0; i < 2000; ++i)
    {
        auto position = actual.find_position(&hotItem);
        assert(position.has_value());

        assert(expected.erase(&hotItem) == 1);

        ++hotItem.priority;

        assert(actual.promote(*position));
        assert(expected.insert(&hotItem).second);

        if (i % 100 == 0)
            verify(actual, expected);
    }

    verify(actual, expected);

    // 4. Randomized promotions, insertions, and erasures.
    std::mt19937                         rng(987654321);
    std::uniform_int_distribution<usize> pick(0, ItemCount - 1);
    std::uniform_int_distribution<int>   operation(0, 9);

    std::vector<bool> active(ItemCount, true);

    for (usize i = 0; i < RandomOperations; ++i)
    {
        const usize index = pick(rng);
        Item&       item  = items[index];

        switch (operation(rng))
        {
        case 0 :
        case 1 :
        case 2 :
        case 3 : {
            // Promote an existing element.
            if (!active[index])
                break;

            auto position = actual.find_position(&item);
            assert(position.has_value());

            assert(expected.erase(&item) == 1);

            ++item.priority;

            assert(actual.promote(*position));
            assert(expected.insert(&item).second);
            break;
        }

        case 4 :
        case 5 : {
            // Erase an existing element.
            if (!active[index])
                break;

            assert(actual.erase(&item) == 1);
            assert(expected.erase(&item) == 1);

            active[index] = false;
            break;
        }

        default : {
            // Insert an absent element.
            if (active[index])
                break;

            assert(actual.insert(&item));
            assert(expected.insert(&item).second);

            active[index] = true;
            break;
        }
        }

        if (i % 100 == 0)
            verify(actual, expected);
    }

    verify(actual, expected);

    // 5. Erase all remaining elements.
    for (usize i = 0; i < ItemCount; ++i)
    {
        if (!active[i])
            continue;

        assert(actual.erase(&items[i]) == 1);
        assert(expected.erase(&items[i]) == 1);
    }

    assert(actual.empty());
    assert(actual.block_count() == 0);
    verify(actual, expected);

    std::cout << "VectorSet stress tests passed.\n";
}

void test_vector_set_iterator() noexcept {
    std::cout << "\nVectorSet iterator test\n";

    TestVectorSet actual;
    ReferenceSet  expected;

    std::vector<Item> items;
    items.reserve(300);

    // Insert enough items to exercise multiple blocks.
    for (int i = 0; i < 300; ++i)
        items.push_back({i, (i * 37) % 101});

    for (Item& item : items)
    {
        assert(actual.insert(&item));
        assert(expected.insert(&item).second);
    }

    std::cout << "VectorSet contents:\n";

    auto expectedItr = expected.begin();

    for (auto itr = actual.begin(); itr != actual.end(); ++itr, ++expectedItr)
    {
        assert(expectedItr != expected.end());

        const Item* value = *itr;

        // Verify this element matches the reference set's ordering.
        assert(value == *expectedItr);

        std::cout << "{id=" << value->id << ", priority=" << value->priority << "}\n";
    }

    // Both iterators must finish at their respective end positions.
    assert(expectedItr == expected.end());
    assert(actual.begin() != actual.end());

    usize count = 0;
    for (auto itr = actual.begin(); itr != actual.end(); ++itr)
        ++count;

    assert(count == actual.size());
    assert(count == expected.size());

    std::cout << "Iterator test passed: " << count << " elements across " << actual.block_count()
              << " blocks.\n";
}


void test_vector_set_union_intersection() {
    std::cout << "\nVectorSet union/intersection tests\n";

    TestVectorSet first;
    TestVectorSet second;

    ReferenceSet referenceFirst;
    ReferenceSet referenceSecond;

    std::vector<Item> items;
    items.reserve(200);

    for (int i = 0; i < 200; ++i)
        items.push_back({i, (i * 37) % 101});

    // First set: even IDs.
    // Second set: IDs divisible by 3.
    for (Item& item : items)
    {
        if (item.id % 2 == 0)
        {
            assert(first.insert(&item));
            assert(referenceFirst.insert(&item).second);
        }

        if (item.id % 3 == 0)
        {
            assert(second.insert(&item));
            assert(referenceSecond.insert(&item).second);
        }
    }

    // Reference union and intersection using std::set.
    ReferenceSet expectedUnion;
    ReferenceSet expectedIntersection;

    std::set_union(referenceFirst.begin(), referenceFirst.end(), referenceSecond.begin(),
                   referenceSecond.end(), std::inserter(expectedUnion, expectedUnion.end()),
                   ItemCompare{});

    std::set_intersection(
      referenceFirst.begin(), referenceFirst.end(), referenceSecond.begin(), referenceSecond.end(),
      std::inserter(expectedIntersection, expectedIntersection.end()), ItemCompare{});

    // VectorSet union.
    TestVectorSet actualUnion;

    std::set_union(first.begin(), first.end(), second.begin(), second.end(),
                   sorted_back_inserter(actualUnion), ItemCompare{});

    // VectorSet intersection.
    TestVectorSet actualIntersection;

    std::set_intersection(first.begin(), first.end(), second.begin(), second.end(),
                          sorted_back_inserter(actualIntersection), ItemCompare{});

    // Validate ordering, contents, and block structure.
    verify(actualUnion, expectedUnion);
    verify(actualIntersection, expectedIntersection);

    assert(actualUnion.size() == expectedUnion.size());
    assert(actualIntersection.size() == expectedIntersection.size());

    std::cout << "VectorSet union: " << actualUnion.size() << " elements\n";

    std::cout << "VectorSet intersection: " << actualIntersection.size() << " elements\n";

    // Mixed input: VectorSet + std::set -> VectorSet.
    TestVectorSet mixedUnion;

    std::set_union(first.begin(), first.end(), referenceSecond.begin(), referenceSecond.end(),
                   sorted_back_inserter(mixedUnion), ItemCompare{});

    verify(mixedUnion, expectedUnion);

    TestVectorSet mixedIntersection;

    std::set_intersection(first.begin(), first.end(), referenceSecond.begin(),
                          referenceSecond.end(), sorted_back_inserter(mixedIntersection),
                          ItemCompare{});

    verify(mixedIntersection, expectedIntersection);

    // Reverse mixed input: std::set + VectorSet -> VectorSet.
    TestVectorSet reverseMixedUnion;

    std::set_union(referenceFirst.begin(), referenceFirst.end(), second.begin(), second.end(),
                   sorted_back_inserter(reverseMixedUnion), ItemCompare{});

    verify(reverseMixedUnion, expectedUnion);

    TestVectorSet reverseMixedIntersection;

    std::set_intersection(referenceFirst.begin(), referenceFirst.end(), second.begin(),
                          second.end(), sorted_back_inserter(reverseMixedIntersection),
                          ItemCompare{});

    verify(reverseMixedIntersection, expectedIntersection);

    // std::set output should also work with VectorSet inputs.
    ReferenceSet standardUnion;

    std::set_union(first.begin(), first.end(), second.begin(), second.end(),
                   std::inserter(standardUnion, standardUnion.end()), ItemCompare{});

    assert(standardUnion == expectedUnion);

    ReferenceSet standardIntersection;

    std::set_intersection(first.begin(), first.end(), second.begin(), second.end(),
                          std::inserter(standardIntersection, standardIntersection.end()),
                          ItemCompare{});

    assert(standardIntersection == expectedIntersection);

    std::cout << "All VectorSet union/intersection tests passed.\n";
}

void test_vector_set_to_vector() noexcept {
    std::cout << "\nVectorSet to std::vector test\n";

    TestVectorSet actual;
    ReferenceSet  expected;

    std::vector<Item> items;
    items.reserve(20);

    for (int i = 0; i < 20; ++i)
        items.push_back({i, (i * 37) % 101});

    for (Item& item : items)
    {
        assert(actual.insert(&item));
        assert(expected.insert(&item).second);
    }

    // Convert VectorSet to std::vector using its iterators.
    const std::vector<Item*> values(actual.begin(), actual.end());

    assert(values.size() == actual.size());
    assert(values.size() == expected.size());

    std::cout << "Converted vector contents:\n";

    auto expectedItr = expected.begin();

    for (const Item* value : values)
    {
        assert(expectedItr != expected.end());
        assert(value == *expectedItr);

        std::cout << "{id=" << value->id << ", priority=" << value->priority << "}\n";

        ++expectedItr;
    }

    assert(expectedItr == expected.end());

    std::cout << "Conversion passed: " << values.size() << " elements.\n";
}

}  // namespace

int main() {
    constexpr usize ItemCount      = 2048;
    constexpr usize PromotionCount = 10000;

    //small_test();

    TestVectorSet actual;
    ReferenceSet  expected;

    std::vector<Item> items;
    items.reserve(ItemCount);

    // Stable addresses: VectorSet stores Item*.
    for (usize i = 0; i < ItemCount; ++i)
        items.push_back({static_cast<int>(i), static_cast<int>(i)});

    // Insert in ascending ID order.
    for (Item& item : items)
    {
        const bool inserted          = actual.insert(&item);
        const bool referenceInserted = expected.insert(&item).second;

        assert(inserted == referenceInserted);
    }

    verify(actual, expected);

    // Duplicate insertions must be rejected.
    for (Item& item : items)
        assert(!actual.insert(&item));

    verify(actual, expected);

    // Deterministic random promotions.
    std::mt19937                         rng(123456);
    std::uniform_int_distribution<usize> pick(0, ItemCount - 1);

    for (usize i = 0; i < PromotionCount; ++i)
    {
        Item& item = items[pick(rng)];

        // Capture the position before changing comparator fields.
        auto position = actual.find_position(&item);
        assert(position.has_value());

        // Remove from the reference set while its old key is valid.
        assert(expected.erase(&item) == 1);

        // Increase priority so the item moves toward the front.
        ++item.priority;

        // Promote the item in VectorSet.
        assert(actual.promote(*position));

        // Reinsert with its updated ordering.
        assert(expected.insert(&item).second);

        verify(actual, expected);
    }

    // Erase every third item.
    for (usize i = 0; i < ItemCount; i += 3)
    {
        Item* item = &items[i];

        assert(actual.erase(item) == 1);
        assert(expected.erase(item) == 1);
    }

    verify(actual, expected);

    // Erasing missing elements must not change the container.
    Item missing{static_cast<int>(ItemCount + 1), -1};
    assert(actual.erase(&missing) == 0);

    verify(actual, expected);

    actual.clear();
    expected.clear();

    assert(actual.empty());
    assert(actual.size() == 0);
    assert(actual.block_count() == 0);

    stress_test_vector_set();

    test_vector_set_iterator();

    test_vector_set_union_intersection();

    test_vector_set_to_vector();

    std::cout << "All VectorSet tests passed.\n";
}
