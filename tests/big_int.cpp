/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I./src tests/big_int.cpp -o build/big_int
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -DNDEBUG -I./src tests/big_int.cpp -o build/big_int

./build/big_int
*/

#include <cassert>

#include "utility/big_int.h"

using namespace DON;

int main() {

    assert((BigInt("12345678901234567890") + BigInt("98765432109876543210")).to_string()
           == "111111111011111111100");

    assert((BigInt("98765432109876543210") - BigInt("12345678901234567890")).to_string()
           == "86419753208641975320");

    assert((BigInt("12345678901234567890") * BigInt("9876543210")).to_string()
           == "121932631124828532111263526900");

    assert((BigInt("12345678901234567890") / BigInt("1234567890")).to_string() == "10000000001");

    assert((BigInt("12345678901234567890") % BigInt("1234567890")).to_string() == "0");

    assert((BigInt("-100") / BigInt("7")).to_string() == "-14");
    assert((BigInt("-100") % BigInt("7")).to_string() == "-2");

    assert((BigInt("-123") + BigInt("456")).to_string() == "333");
    assert((BigInt("-123") - BigInt("456")).to_string() == "-579");

    assert(gcd(BigInt("48"), BigInt("18")).to_string() == "6");
    assert(lcm(BigInt("48"), BigInt("18")).to_string() == "144");

    assert(pow(BigInt("2"), 100).to_string() == "1267650600228229401496703205376");

    assert(pow_mod(BigInt("2"), BigInt("100"), BigInt("1000")).to_string() == "376");

    assert(factorial(20).to_string() == "2432902008176640000");

    BigInt a("291");
    BigInt b("-123456789");
    BigInt c("0");

    std::cout << a.to_hex_string() << '\n';  // 0x123
    std::cout << b.to_hex_string() << '\n';  // -0x75BCD15
    std::cout << c.to_hex_string() << '\n';  // 0x0

    {
        BigInt u1{0xDEADBEEFDEADBEEF};
        BigInt u2{0xCAFEBABECAFEBABE};

        std::cout << (u1 * u2).to_hex_string() << '\n';  // 0xB092AB7CE9F4B259
    }
    {
        BigInt u1{0xFFFFFFFF00000000};
        BigInt u2{0x00000001FFFFFFFF};

        std::cout << (u1 * u2).to_hex_string() << '\n';  // 0x00000001FFFFFFFD
    }

    return 0;
}
