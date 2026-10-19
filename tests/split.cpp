/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -DNDEBUG -I./src src/misc.cpp tests/split.cpp -o build/split

./build/split
*/


#include <iostream>
#include <string_view>
#include <vector>

#include "misc.h"

using namespace DON;

void test_split(std::string_view                     input,
                std::string_view                     delimiter,
                const std::vector<std::string_view>& expected,
                bool                                 trimPart = false) {

    std::vector<std::string_view> actual = split(input, delimiter, trimPart);

    if (actual != expected)
    {
        std::cerr << "FAILED: input=\"" << input << "\", delimiter=\"" << delimiter << "\"\n";

        std::cerr << "Expected: ";
        for (const auto part : expected)
            std::cerr << '[' << part << ']';
        std::cerr << '\n';

        std::cerr << "Actual:   ";
        for (const auto part : actual)
            std::cerr << '[' << part << ']';
        std::cerr << '\n';

        assert(false);
    }
}

int main() {
    // Basic splitting
    test_split("one,two,three", ",", {"one", "two", "three"});

    // No delimiter
    test_split("hello", ",", {"hello"});

    // Empty input
    test_split("", ",", {});

    // Leading, consecutive, and trailing delimiters
    test_split(",one,,two,", ",", {"", "one", "", "two", ""});

    // Multi-character delimiter
    test_split("one::two::::three::", "::", {"one", "two", "", "three", ""});

    // Delimiter not found
    test_split("abc", "::", {"abc"});

    // Trimming
    test_split("  one , two  ,  three  ", ",", {"one", "two", "three"}, true);

    // Trimming whitespace-only and empty parts
    test_split("  , \t ,value,  ", ",", {"", "", "value", ""}, true);

    // Multi-character delimiter with trimming
    test_split(" alpha :: beta :: gamma ", "::", {"alpha", "beta", "gamma"}, true);

    std::cout << "All split() tests passed.\n";
}
