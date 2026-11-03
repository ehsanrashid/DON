/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I./src tests/pow.cpp -o build/pow
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -DNDEBUG -I./src tests/pow.cpp -o build/pow

./build/pow
*/


#include <cassert>
#include <cmath>

#include "utility/types.h"

using namespace DON;

int main() {

    for (int ply = 0; ply <= 256; ++ply)
    {
        const double base     = 3.22713 + ply;
        const double expected = std::pow(base, 0.46866);
        const double actual   = constexpr_pow(base, 0.46866);

        const double error = std::abs(actual - expected);

        assert(error <= 1e-15 * expected);
    }

    return 0;
}
