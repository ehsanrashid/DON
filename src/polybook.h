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
    PolyBook() = default;
    ~PolyBook() noexcept;

    void init(std::string_view bookFile) noexcept;

    bool enabled() const noexcept { return entries != nullptr && entryCount != 0; }

    std::string info() const noexcept;

    Move probe(Position& pos, bool pickBestActive = true) noexcept;

   private:
    struct KeyData final {
        std::size_t   begIndex, bestIndex, randIndex;
        std::uint16_t count;
        std::uint16_t bestWeight;
        std::uint32_t sumWeight;
    };

    void free() noexcept;

    bool can_probe(const Position& pos, Key key) noexcept;

    std::size_t find_key(Key key) const noexcept;

    KeyData get_key_data(std::size_t index) const noexcept;

    void show_key_data(const KeyData& keyData, Position& pos) const noexcept;

    std::string filename;

    PolyEntry*  entries = nullptr;
    std::size_t entryCount;

    // Last probe info
    Bitboard      occupied;
    std::uint16_t failCount;
};

}  // namespace DON

#endif  // #ifndef POLYBOOK_H_INCLUDED
