#include <cassert>
#include <iostream>
#include <string>

#include "utility/map.h"

using namespace DON;

int main() {
    Map<int, std::string> map;

    // Basic state
    assert(map.empty());
    assert(map.size() == 0);
    assert(map.max_size() > 0);

    // Lookup on an empty map
    assert(!map.contains(1));
    assert(map.count(1) == 0);

    // Comparator
    const auto compare = map.key_comp();
    assert(!compare(1, 1));
    assert(compare(1, 2));
    assert(!compare(2, 1));

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

    std::cout << "All Map tests passed.\n";

    return 0;
}
