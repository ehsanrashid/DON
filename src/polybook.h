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
#include <iosfwd>
#include <string>
#include <tuple>

#include "types.h"

namespace DON {

class Position;

struct PolyEntry final {

    friend bool operator==(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return std::tie(pe1.key, pe1.move, pe1.weight)  //
            == std::tie(pe2.key, pe2.move, pe2.weight);
    }
    friend bool operator!=(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return !(pe1 == pe2);
    }

    friend bool operator<(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return std::tie(pe1.key, pe1.weight, pe1.move)  //
             < std::tie(pe2.key, pe2.weight, pe2.move);
    }
    friend bool operator>(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return (pe2 < pe1);
    }
    friend bool operator<=(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return !(pe1 > pe2);
    }
    friend bool operator>=(const PolyEntry& pe1, const PolyEntry& pe2) noexcept {
        return !(pe1 < pe2);
    }

    friend bool operator==(const PolyEntry& pe, const Move& m) noexcept {
        return pe.move == (m.raw() & ~MOVETYPE_MASK);
    }
    friend bool operator!=(const PolyEntry& pe, const Move& m) noexcept { return !(pe == m); }

    friend std::ostream& operator<<(std::ostream& os, const PolyEntry& ph) noexcept;

    Key           key;
    std::uint16_t move;
    std::uint16_t weight;
    std::uint32_t learn;
};

class PolyBook final {
   public:
    PolyBook() = default;
    ~PolyBook() noexcept;

    void clear() noexcept;

    void init(const std::string& bookFile) noexcept;

    Move probe(Position& pos, bool bestPick = true) noexcept;

    bool enabled() const noexcept { return enable; }

   private:
    struct KeyData final {
        std::size_t   begIndex, bestIndex, randIndex;
        std::uint16_t entryCount;
        std::uint16_t bestWeight;
        std::uint32_t sumWeight;
    };

    bool can_probe(const Position& pos, Key key) noexcept;

    void find_key(Key key) noexcept;

    void get_key_data(std::size_t begIndex) noexcept;

    void show_key_data() const noexcept;

    PolyEntry*  entries    = nullptr;
    std::size_t entryCount = 0;
    bool        enable     = false;

    // Last probe info
    Bitboard      pieces    = 0;
    std::uint16_t failCount = 0;

    // Key data
    KeyData keyData;
};

}  // namespace DON

#endif  // #ifndef POLYBOOK_H_INCLUDED
