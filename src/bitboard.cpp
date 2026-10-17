/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "bitboard.h"

#if !defined(USE_POPCNT)
    #include <cstring>  // memcpy()
#endif

#include "utility/lru_cache.h"
#include "utility/mutex.h"
#include "utility/sharded_cache.h"

namespace DON {

namespace {

#if !defined(USE_POPCNT)

alignas(CACHE_LINE_SIZE) const auto POP_CNTS = []() noexcept {
    Array<u8, 0x10000> popCnts{};

    for (usize i = 0; i < popCnts.size(); ++i)
        popCnts[i] = constexpr_popcount(i);

    return popCnts;
}();

#endif

using BitboardCache =
  ShardedCache<Bitboard, std::string, PoolLRUCache, 32, SpinMutex, std::string_view>;

BitboardCache& bitboard_cache() noexcept {
    static BitboardCache bitboardCache{usize{256}, usize{64}, 0.75F};
    return bitboardCache;
}

}  // namespace

u8 popcount(const Bitboard b) noexcept {

#if !defined(USE_POPCNT)
    Array<u16, 4> b16;
    static_assert(sizeof(b16) == sizeof(b));

    std::memcpy(b16.data(), &b, sizeof(b16));

    return POP_CNTS[b16[0]] + POP_CNTS[b16[1]] + POP_CNTS[b16[2]] + POP_CNTS[b16[3]];

#elif defined(__GNUC__)  // GCC, Clang, ICX
    return static_cast<u8>(__builtin_popcountll(b));

#elif defined(_MSC_VER)
    return static_cast<u8>(_mm_popcnt_u64(b));

#else  // Compiler is neither GCC nor MSVC compatible
    // Using a fallback implementation
    return constexpr_popcount(b);

#endif
}

Square lsq(const Bitboard b) noexcept {
    assert(b != 0);

#if defined(__GNUC__)  // GCC, Clang, ICX
    return Square(__builtin_ctzll(b));

#elif defined(_MSC_VER)
    unsigned long idx;

    #if defined(_WIN64)  // (WIN64)
    _BitScanForward64(&idx, b);
    return Square(idx);

    #else  // (WIN32)
    if (u32 bb = u32(b); bb != 0)
    {
        _BitScanForward(&idx, bb);
        return Square(idx);
    }

    _BitScanForward(&idx, u32(b >> 32));
    return Square(idx + 32);

    #endif

#else  // Compiler is neither GCC nor MSVC compatible
    // Using a fallback implementation
    return Square(constexpr_lsb(b));

#endif
}

Square msq(const Bitboard b) noexcept {
    assert(b != 0);

#if defined(__GNUC__)  // GCC, Clang, ICX
    return Square(__builtin_clzll(b) ^ 63);

#elif defined(_MSC_VER)
    unsigned long idx;

    #if defined(_WIN64)  // (WIN64)
    _BitScanReverse64(&idx, b);
    return Square(idx);

    #else  // (WIN32)
    if (u32 bb = static_cast<u32>(b >> 32); bb != 0)
    {
        _BitScanReverse(&idx, bb);
        return Square(idx + 32);
    }

    _BitScanReverse(&idx, static_cast<u32>(b));
    return Square(idx);

    #endif

#else  // Compiler is neither GCC nor MSVC compatible
    // Using a fallback implementation
    return Square(constexpr_msb(b));

#endif
}

Square pop_lsq(Bitboard& b) noexcept {
    assert(b != 0);

    const Square s = lsq(b);

    b &= b - 1;

    return s;
}

Square pop_msq(Bitboard& b) noexcept {
    assert(b != 0);

    const Square s = msq(b);

    b ^= s;

    return s;
}

std::string pretty_str(const Bitboard b) noexcept {
    constexpr std::string_view Sep{"\n  +---+---+---+---+---+---+---+---+\n"};

    std::string bb;
    bb.reserve(646);

    bb.append(Sep);

    for (Rank r = RANK_8;; --r)
    {
        bb.push_back(to_char(r));

        for (File f = FILE_A; f <= FILE_H; ++f)
        {
            const char marker = (b & make_square(f, r)) != 0 ? '*' : ' ';
            bb.append(" | ").push_back(marker);
        }

        bb.append(" |").append(Sep);

        if (r == RANK_1)
            break;
    }

    bb.push_back(' ');

    for (File f = FILE_A; f <= FILE_H; ++f)
        bb.append("   ").push_back(to_char<true>(f));

    bb.push_back('\n');

    return bb;
}

std::string_view pretty(const Bitboard b) noexcept {
    return bitboard_cache().read_or_write_with(
      b, [](const Bitboard b_) noexcept -> std::string { return pretty_str(b_); }, b);
}

}  // namespace DON
