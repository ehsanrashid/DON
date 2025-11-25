/*
  DON, a UCI chess playing engine derived from Stockfish

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

#ifndef POLYBOOK_H_INCLUDED
#define POLYBOOK_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "types.h"

namespace DON {

class Position;

struct PolyEntry final {
   public:
    friend constexpr bool operator==(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return pe1.key == pe2.key && pe1.move == pe2.move && pe1.weight == pe2.weight;
    }
    friend constexpr bool operator!=(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return !(pe1 == pe2);
    }

    friend constexpr bool operator<(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return pe1.key != pe2.key       ? pe1.key < pe2.key
             : pe1.weight != pe2.weight ? pe1.weight < pe2.weight
                                        : pe1.move < pe2.move;
    }
    friend constexpr bool operator>(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return (pe2 < pe1);
    }
    friend constexpr bool operator<=(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return !(pe2 < pe1);
    }
    friend constexpr bool operator>=(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return !(pe1 < pe2);
    }

    Key           key;
    std::uint16_t move;
    std::uint16_t weight;
    std::uint32_t learn;
};

class PolyBook final {
   public:
    PolyBook() noexcept  = default;
    ~PolyBook() noexcept = default;

    void init(std::string_view bookFile) noexcept;

    bool enabled() const noexcept { return !entries.empty(); }

    std::string info() const noexcept;

    Move probe(Position& pos, bool pickBestActive = true) noexcept;

   private:
    void clear() noexcept;

    bool can_probe(const Position& pos, Key key) noexcept;

    std::size_t key_index(Key key) const noexcept;

    std::vector<PolyEntry> key_candidates(Key key) const noexcept;

    std::string filename;

    std::vector<PolyEntry> entries;

    // Last probe info
    Bitboard     occupied;
    std::uint8_t failCount;
};

}  // namespace DON

#endif  // #ifndef POLYBOOK_H_INCLUDED
