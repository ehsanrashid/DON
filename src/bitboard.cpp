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

#include <memory>

#include "concurrent.h"  // ConcurrentCache<>

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
    constexpr usize ReserveCount  = 1 * KB;
    constexpr float MaxLoadFactor = 0.75f;

    // Thread-safe static initialization

    // Fully RAII-compliant — destructor runs at program exit
    //static auto cache = ConcurrentCache<Bitboard, std::string>(ReserveCount, MaxLoadFactor);

    // Standard intentional "leaky singleton" pattern.
    // Ensures the cache lives for the entire program, never deleted.
    //static auto& cache = *new ConcurrentCache<Bitboard, std::string>(ReserveCount, MaxLoadFactor);
    static auto& cache = *[=]() noexcept -> auto {
        static auto cachePtr =
          std::make_unique<ConcurrentCache<Bitboard, std::string>>(ReserveCount, MaxLoadFactor);
        return cachePtr.get();
    }();

    //return cache.access_or_build(b, pretty_str(b));
    //return cache.transform_access_or_build(
    //  b, [](const std::string& str) noexcept -> std::string_view { return str; }, pretty_str(b));
    return cache.transform_access_or_build_with(
      b, [](const std::string& str) noexcept -> std::string_view { return str; },
      [](const Bitboard _b) noexcept -> std::string { return pretty_str(_b); }, b);
}

}  // namespace DON
