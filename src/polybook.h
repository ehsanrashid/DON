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
class RootMoves;
class Options;

class PolyBook final {
   public:
    struct Entry final {
       public:
        friend constexpr bool operator==(const Entry& e1, const Entry& e2) noexcept {
            return e1.key == e2.key && e1.move == e2.move && e1.weight == e2.weight;
        }
        friend constexpr bool operator!=(const Entry& e1, const Entry& e2) noexcept {
            return !(e1 == e2);
        }

        friend constexpr bool operator<(const Entry& e1, const Entry& e2) noexcept {
            return e1.key != e2.key       ? e1.key < e2.key
                 : e1.weight != e2.weight ? e1.weight < e2.weight
                                          : e1.move < e2.move;
        }
        friend constexpr bool operator>(const Entry& e1, const Entry& e2) noexcept {
            return (e2 < e1);
        }
        friend constexpr bool operator<=(const Entry& e1, const Entry& e2) noexcept {
            return !(e2 < e1);
        }
        friend constexpr bool operator>=(const Entry& e1, const Entry& e2) noexcept {
            return !(e1 < e2);
        }

        Key           key;
        std::uint16_t move;
        std::uint16_t weight;
        std::uint32_t learn;
    };

    using Entries = std::vector<Entry>;

    PolyBook() noexcept                           = default;
    PolyBook(const PolyBook&) noexcept            = delete;
    PolyBook(PolyBook&&) noexcept                 = delete;
    PolyBook& operator=(const PolyBook&) noexcept = delete;
    PolyBook& operator=(PolyBook&&) noexcept      = delete;
    ~PolyBook() noexcept                          = default;

    bool load(std::string_view bookFile) noexcept;

    bool empty() const noexcept { return entries.empty(); }

    std::string info() const noexcept;

    Move probe(Position& pos, const RootMoves& rootMoves, const Options& options) noexcept;

   private:
    void clear() noexcept;

    std::size_t key_index(Key key) const noexcept;

    Entries key_candidates(Key key) const noexcept;

    std::string filename;

    Entries entries;
};

}  // namespace DON

#endif  // #ifndef POLYBOOK_H_INCLUDED
