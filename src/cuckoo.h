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

#ifndef CUCKOO_H_INCLUDED
#define CUCKOO_H_INCLUDED

#include "attacks.h"
#include "misc.h"
#include "types.h"
#include "zobrist.h"

namespace DON {

// Implements Marcel van Kervinck's cuckoo algorithm to detect repetition of positions for 3-fold repetition draws.
// The algorithm uses hash tables with Zobrist hashes to allow fast detection of recurring positions.
// For details see:
// http://web.archive.org/web/20201107002606/https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf

// Cuckoo Entry: stores a Zobrist hash key and the associated move
struct Cuckoo final {
   public:
    [[nodiscard]] bool empty() const noexcept;

    Key  key;
    Move move;
};

inline bool Cuckoo::empty() const noexcept { return key == 0; }

// Cuckoo Table: fixed-size hash table with two hash functions and cuckoo eviction,
// contains Zobrist hashes of valid reversible moves, and the moves themselves
template<usize Size>
class CuckooTable final {
    static_assert(is_power_of_2(Size), "Size has to be power of 2");

   public:
    constexpr CuckooTable() noexcept                    = default;
    CuckooTable(const CuckooTable&) noexcept            = delete;
    CuckooTable& operator=(const CuckooTable&) noexcept = delete;
    CuckooTable(CuckooTable&&) noexcept                 = delete;
    CuckooTable& operator=(CuckooTable&&) noexcept      = delete;

    [[nodiscard]] constexpr auto begin() noexcept { return cuckoos.begin(); }
    [[nodiscard]] constexpr auto end() noexcept { return cuckoos.end(); }
    [[nodiscard]] constexpr auto begin() const noexcept { return cuckoos.begin(); }
    [[nodiscard]] constexpr auto end() const noexcept { return cuckoos.end(); }

    [[nodiscard]] constexpr auto size() const noexcept { return cuckoos.size(); }
    [[nodiscard]] constexpr bool empty() const noexcept { return cuckoos.empty(); }

    [[nodiscard]] constexpr decltype(auto) operator[](usize idx) const noexcept {
        return cuckoos[idx];
    }
    [[nodiscard]] constexpr decltype(auto) operator[](usize idx) noexcept { return cuckoos[idx]; }

    // Hash function for indexing the cuckoo table
    template<usize Part>
    constexpr usize H(Key key) const noexcept {
        return (key >> (16 * Part)) & (size() - 1);
    }

    void insert(Cuckoo newCuckoo) noexcept {
        assert(!newCuckoo.empty());

        usize index = H<0>(newCuckoo.key);
        while (true)
        {
            std::swap(newCuckoo, cuckoos[index]);
            // Arrived at empty slot?
            if (newCuckoo.empty())
                break;
            // Push victim to alternative slot
            index ^= H<0>(newCuckoo.key) ^ H<1>(newCuckoo.key);
        }

        ++count;
    }

    void clear() noexcept {
        std::memset(cuckoos.data(), 0, sizeof(cuckoos));

        count = 0;
    }

    // Prepare the cuckoo tables
    void init(const Zobrist& zobrist) noexcept {
        clear();

        for (Color c : {WHITE, BLACK})
            for (PieceType pt : PIECE_TYPES)
            {
                if (pt == PAWN)
                    continue;
                for (Square s1 = SQ_A1; s1 < SQ_H8; ++s1)
                    for (Square s2 = s1 + 1; s2 <= SQ_H8; ++s2)
                    {
                        if ((Attacks::pseudo_attacks_bb(s1, pt) & s2) != 0)
                        {
                            const Key  key = zobrist.turn()  //
                                           ^ zobrist.piece_square(c, pt, s1)
                                           ^ zobrist.piece_square(c, pt, s2);
                            const Move move{s1, s2};
                            insert({key, move});
                        }
                    }
            }

        assert(count == 3668);
    }

    [[nodiscard]] usize find_key(Key key) const noexcept {
        if (usize index;  //
            (index = H<0>(key), cuckoos[index].key == key)
            || (index = H<1>(key), cuckoos[index].key == key))
            return index;

        return size();
    }

   private:
    Array<Cuckoo, Size> cuckoos;
    usize               count;
};

int loop();

}  // namespace DON

#endif  // CUCKOO_H_INCLUDED
