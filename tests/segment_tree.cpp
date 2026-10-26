/*

Steps:

mkdir -p build

g++ -std=c++17 -O1 -I./src tests/segment_tree.cpp -o build/segment_tree
g++ -std=c++17 -O1 -DNDEBUG -I./src tests/segment_tree.cpp -o build/segment_tree

./build/segment_tree
*/

#include <algorithm>
#include <cstddef>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include <iostream>

#include "utility/segment_tree.h"

using namespace DON;

// Keep your existing SegmentTreeRMQ class above this point.

int main() {
    try
    {
        std::cerr << "1. Before vector allocation\n";
        std::vector<int> values(32, std::numeric_limits<int>::max());
        std::cerr << "2. Vector allocation succeeded\n";

        std::cerr << "3. Before SegmentTreeRMQ(10)\n";
        SegmentTreeRMQ segTree(10);
        std::cerr << "4. SegmentTreeRMQ(10) succeeded\n";

        std::vector<int> input{8, 3, 6, 2, 9, 1, 7, 4, 5, 0};

        std::cerr << "5. Before SegmentTreeRMQ(input)\n";
        SegmentTreeRMQ valuesSegTree(input);
        std::cerr << "6. SegmentTreeRMQ(input) succeeded\n";

        std::cerr << "Minimum [0, 10): " << valuesSegTree.min(0, 10) << '\n';

        valuesSegTree.update(0, -1);

        std::cerr << "Minimum after update: " << valuesSegTree.min(0, 10) << '\n';

    } catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
