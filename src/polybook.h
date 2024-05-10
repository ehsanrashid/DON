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

#include <string>

#include "bitboard.h"
#include "types.h"

namespace DON {

class Position;

struct PolyHash final {

    bool operator==(const PolyHash& ph) const noexcept;
    bool operator!=(const PolyHash& ph) const noexcept;

    bool operator<(const PolyHash& ph) const noexcept;
    bool operator>(const PolyHash& ph) const noexcept;

    bool operator==(Move m) const noexcept;
    bool operator!=(Move m) const noexcept;

    friend std::ostream& operator<<(std::ostream& os, const PolyHash& ph) noexcept;

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
    Move probe(Position& pos, bool pickBest = true) noexcept;

    bool is_enabled() const noexcept { return enabled; }

   private:
    bool can_probe(const Position& pos, Key key) noexcept;

    int find_first_key(Key key) noexcept;
    int get_key_data() noexcept;

    std::string show(std::uint8_t n) const noexcept;

    bool      enabled    = false;
    PolyHash* polyHash   = nullptr;
    int       entryCount = 0;
    // Last probe info
    Bitboard pieces    = 0;
    int      failCount = 0;
    // Key data
    int firstIndex, bestIndex, randIndex;
    int keyCount;
    int keyWeightSum;
};

}  // namespace DON

#endif  // #ifndef POLYBOOK_H_INCLUDED
