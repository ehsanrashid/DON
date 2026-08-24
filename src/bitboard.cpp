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

#include <memory>

namespace DON {

// Returns an ASCII representation of a bitboard suitable
// to be printed to standard output. Useful for debugging.
std::string pretty_str(const Bitboard b) noexcept {
    constexpr std::string_view Sep{"\n  +---+---+---+---+---+---+---+---+\n"};

    std::string bb;
    bb.reserve(646);

    bb.assign(Sep);

    for (Rank r = RANK_8;; --r)
    {
        bb.push_back(to_char(r));

        for (File f = FILE_A; f <= FILE_H; ++f)
            bb.append(" | ").push_back((b & make_square(f, r)) != 0 ? '*' : ' ');

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
    constexpr usize ReserveCount  = 1024;
    constexpr float MaxLoadFactor = 0.75f;

    // Thread-safe static initialization

    // Fully RAII-compliant — destructor runs at program exit
    //static auto cache = ConcurrentCache<Bitboard, std::string>(ReserveCount, MaxLoadFactor);

    // Standard intentional "leaky singleton" pattern.
    // Ensures the cache lives for the entire program, never deleted.
    //static auto& cache = *new ConcurrentCache<Bitboard, std::string>(ReserveCount, MaxLoadFactor);
    static auto& cache = *[=] {
        static auto cachePtr =
          std::make_unique<ConcurrentCache<Bitboard, std::string>>(ReserveCount, MaxLoadFactor);
        return cachePtr.get();
    }();

    //return cache.access_or_build(b, pretty_str(b));
    return cache.transform_access_or_build(
      b, [](const std::string& str) noexcept -> std::string_view { return str; }, pretty_str(b));
}

}  // namespace DON
