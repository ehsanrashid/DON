/*

Steps:

mkdir -p build

g++ -std=c++17 -O1 -I./src tests/segment_tree.cpp -o build/segment_tree
g++ -std=c++17 -O1 -DNDEBUG -I./src tests/segment_tree.cpp -o build/segment_tree

./build/segment_tree
*/

#include <algorithm>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "utility/segment_tree.h"

using namespace DON;

int main() {
    try
    {
        std::cerr << "1. Before vector allocation\n";
        std::vector<int> values(32, std::numeric_limits<int>::max());
        std::cerr << "2. Vector allocation succeeded\n";

        std::cerr << "3. Before SegmentTreeMin(10)\n";
        SegmentTreeMin segTree(10);
        std::cerr << "4. SegmentTreeMin(10) succeeded\n";

        std::vector<int> input{8, 3, 6, 2, 9, 1, 7, 4, 5, 0};

        std::cerr << "5. Before SegmentTreeMin(input)\n";
        SegmentTreeMin valuesSegTree(input);
        std::cerr << "6. SegmentTreeMin(input) succeeded\n";

        std::cerr << "Minimum [0, 10): " << valuesSegTree.min(0, 10) << '\n';

        std::cerr << "Update [0] = -1:\n";
        valuesSegTree.update(0, -1);

        std::cerr << "Minimum after update: " << valuesSegTree.min(0, 10) << '\n';
    } catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << '\n';
        return 1;
    }

    try
    {
        std::cerr << "\n--- SegmentTreeSum ---\n";

        const std::vector<int> values{2, 4, 6, 8, 10};
        std::cerr << "Value: \n";
        for (size_t i = 0; i < values.size(); ++i)
            std::cerr << values[i] << " ";
        std::cerr << "\n";

        SegmentTreeSum segTree(values);

        std::cerr << "Sum [1, 4): " << segTree.sum(1, 4) << '\n';  // 18: 4 + 6 + 8

        std::cerr << "Update [2] = 100\n";
        segTree.update(2, 100);  // Assign index 2 to 100.

        std::cerr << "Sum [1, 4): " << segTree.sum(1, 4) << '\n';  // 112: 4 + 100 + 8
        std::cerr << "Sum [0, 5): " << segTree.sum(0, 5) << '\n';  // 124

    } catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << '\n';
        return 1;
    }

    try
    {
        std::cerr << "\n--- SegmentTreeSum ---\n";

        const std::vector<int> values{5, -4, 4, 3, -5};
        std::cerr << "Value: \n";
        for (size_t i = 0; i < values.size(); ++i)
            std::cerr << values[i] << " ";
        std::cerr << "\n";

        SegmentTreeSum segTree(values);

        std::cerr << "Sum [0, 4): " << segTree.sum(0, 4) << '\n';  // 8: 5 + -4 + 4 + 3

        std::cerr << "Update [4] = 3\n";
        segTree.update(4, 3);

        std::cerr << "Sum [0, 5): " << segTree.sum(0, 5) << '\n';  // 11: 5 + -4 + 4 + 3 + 3

        std::cerr << "Update [3] = -1\n";
        segTree.update(3, -1);

        std::cerr << "Sum [0, 5): " << segTree.sum(0, 5) << '\n';  // 7: 5 + -4 + 4 + -1 + 3

    } catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << '\n';
        return 1;
    }

    const std::vector<int> sequence{5, 1, 2, 2, 3, 1, 3, 4, 5, 4};
    std::cerr << "\nSequence: \n";
    for (size_t i = 0; i < sequence.size(); ++i)
        std::cerr << sequence[i] << " ";
    std::cerr << "\n";

    const std::vector<int> answer = count_nested_segments(sequence);

    for (usize i = 0; i < answer.size(); ++i)
        std::cout << answer[i] << (i + 1 == answer.size() ? '\n' : ' ');

    return 0;
}
