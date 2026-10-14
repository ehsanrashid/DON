#include <cassert>
#include <iostream>
#include <string>

#include "utility/map.h"

using namespace DON;

int main() {
    Map<int, std::string> map;

    assert(map.empty());
    assert(map.size() == 0);
    assert(map.max_size() > 0);

    map.clear();

    assert(map.empty());

    std::cout << "All Map tests passed.\n";

    return 0;
}
