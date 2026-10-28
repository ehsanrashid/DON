/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I./src tests/key.cpp -o build/key
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -DNDEBUG -I./src tests/key.cpp -o build/key

./build/key
*/

#include <iostream>
#include <limits>
#include <random>

#include "utility/types.h"

using namespace DON;

using Key = u64;

/*
constexpr u8  KEY32_BITS = u8{32};
constexpr u32 KEY32_MASK = (u64{1} << KEY32_BITS) - 1;

constexpr u32 compress_key(const Key key) noexcept {
    return ((key >> 0 * KEY32_BITS) & (KEY32_MASK >> 0 * KEY32_BITS / 2))  //
         ^ ((key >> 1 * KEY32_BITS) & (KEY32_MASK >> 1 * KEY32_BITS / 2));
}

// Independent reference:
constexpr u32 reference_compress_key(const Key key) noexcept {
    return ((key >> 00) & u32{0xFFFFFFFF})  //
         ^ ((key >> 32) & u32{0x0000FFFF});
}
*/
/*
constexpr u8  KEY21_BITS = u8{21};
constexpr u32 KEY21_MASK = (u64{1} << KEY21_BITS) - 1;

constexpr u32 compress_key(const Key key) noexcept {
    return ((key >> 0 * KEY21_BITS) & (KEY21_MASK >> 0 * KEY21_BITS / 3))  //
         ^ ((key >> 1 * KEY21_BITS) & (KEY21_MASK >> 1 * KEY21_BITS / 3))
         ^ ((key >> 2 * KEY21_BITS) & (KEY21_MASK >> 2 * KEY21_BITS / 3));
}

// Independent reference:
constexpr u32 reference_compress_key(const Key key) noexcept {
    return ((key >> 00) & u32{0x1FFFFF})  //
         ^ ((key >> 21) & u32{0x003FFF})  //
         ^ ((key >> 42) & u32{0x00007F});
}

bool test_key(const Key key) {
    const u32 actual   = compress_key(key);
    const u32 expected = reference_compress_key(key);

    if (actual != expected)
    {
        std::cerr << "FAIL"
                  << " key=0x" << std::hex << key << " actual=0x" << actual << " expected=0x"
                  << expected << std::dec << '\n';
        return false;
    }

    return true;
}

int main() {
    constexpr Key edgeCases[] = {0,
                                 1,
                                 std::numeric_limits<Key>::max(),
                                 0xFFFFFFFF00000000ULL,
                                 0x00000000FFFFFFFFULL,
                                 0x123456789ABCDEF0ULL,
                                 0x0000FFFF00000000ULL,
                                 0xFFFF000000000000ULL,
                                 0x0000000100000001ULL};

    for (const Key key : edgeCases)
    {
        if (!test_key(key))
            return 1;
    }

    std::mt19937_64 rng(0xD0C0FFEEULL);
    constexpr u64   TestCount = 1'000'000;

    for (u64 i = 0; i < TestCount; ++i)
    {
        if (!test_key(rng()))
            return 1;
    }

    std::cout << "PASS: " << TestCount << " random 64-bit keys and "
              << (sizeof(edgeCases) / sizeof(edgeCases[0])) << " edge cases matched.\n";

    return 0;
}
*/

constexpr u8  KEY21_BITS = u8{21};
constexpr u64 KEY21_MASK = (u64{1} << KEY21_BITS) - 1;

constexpr u64 extract_key(const Key key, const usize index) noexcept {
    return (key >> (index * KEY21_BITS)) & KEY21_MASK;
}

// Independent reference implementation.
constexpr u64 reference_extract_key(const Key key, const usize index) noexcept {
    const usize shift = index * 21;
    return (key >> shift) % (u64{1} << 21);
}

bool test_key(const u64 key, const usize index) {
    const u64 actual   = extract_key(key, index);
    const u64 expected = reference_extract_key(key, index);

    if (actual != expected)
    {
        std::cerr << "FAIL"
                  << " key=0x" << std::hex << key << " index=" << std::dec << index << " actual=0x"
                  << std::hex << actual << " expected=0x" << expected << std::dec << '\n';
        return false;
    }

    return true;
}

int main() {
    constexpr u64 edgeCases[] = {0,
                                 1,
                                 std::numeric_limits<u64>::max(),
                                 0x123456789ABCDEF0ULL,
                                 0x00000000001FFFFFULL,
                                 0x000000003FFFFFULL,
                                 0x7FFFFFFFFFFFFFFFULL,
                                 0x8000000000000000ULL};

    constexpr usize IndexCount = 3;

    for (const u64 key : edgeCases)
    {
        for (usize index = 0; index < IndexCount; ++index)
        {
            if (!test_key(key, index))
                return 1;
        }
    }

    std::mt19937_64 rng(0xD0C0FFEEULL);
    constexpr u64   TestCount = 1'000'000;

    for (u64 i = 0; i < TestCount; ++i)
    {
        const u64 key = rng();

        for (usize index = 0; index < IndexCount; ++index)
        {
            if (!test_key(key, index))
                return 1;
        }
    }

    std::cout << "PASS: " << TestCount << " random keys x " << IndexCount << " indices and "
              << (sizeof(edgeCases) / sizeof(edgeCases[0]) * IndexCount) << " edge-case checks.\n";

    return 0;
}
