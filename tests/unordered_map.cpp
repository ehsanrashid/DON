/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -DNDEBUG -I./src tests/unordered_map.cpp -o build/unordered_map

./build/unordered_map
*/

#include <cassert>
#include <iostream>
#include <string>

#include "utility/unordered_map.h"

using namespace DON;

template<typename MapType>
void test_map() {
    MapType map;

    // Basic state
    assert(map.empty());
    assert(map.size() == 0);
    assert(map.max_size() > 0);

    // Lookup on an empty map
    assert(!map.contains(1));
    assert(map.count(1) == 0);

    // Read access
    {
        auto access = map.read();

        assert(access.begin() == access.end());
    }

    // Write access
    {
        auto access = map.write();

        assert(access.begin() == access.end());
    }

    // Clear an empty map
    map.clear();

    assert(map.empty());
    assert(map.size() == 0);

    // Write access
    {
        auto access = map.write();

        access->insert_or_assign(1, "one");
        access->insert_or_assign(2, "two");
        access->insert_or_assign(3, "three");
    }

    assert(!map.empty());
    assert(map.size() == 3);
    assert(map.contains(1));
    assert(map.count(2) == 1);
    assert(!map.contains(4));

    // Read access
    {
        auto access = map.read();

        assert(access->at(1) == "one");
        assert(access->at(2) == "two");

        usize count = 0;
        for (const auto& [key, value] : access)
        {
            (void) key;
            (void) value;
            ++count;
        }
        assert(count == 3);
    }

    // Modify an existing value
    {
        auto access   = map.write();
        access->at(2) = "updated";
    }

    // Verify the updated value
    {
        auto access = map.read();
        assert(access->at(2) == "updated");
    }

    map.clear();

    assert(map.empty());
    assert(map.size() == 0);
}

int main() {
    test_map<UnorderedMap<int, std::string>>();

    // Verify custom allocator template arguments compile.
    using Pair      = std::pair<const int, std::string>;
    using Allocator = std::allocator<Pair>;

    test_map<UnorderedMap<int, std::string, std::shared_mutex, std::hash<int>, std::equal_to<int>,
                          Allocator>>();

    static_assert(std::is_same_v<UnorderedMap<int, std::string>::allocator_type,
                                 std::allocator<std::pair<const int, std::string>>>);

    std::cout << "All Map tests passed.\n";

    return 0;
}
