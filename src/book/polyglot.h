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

#ifndef BOOK_POLYGLOT_H_INCLUDED
#define BOOK_POLYGLOT_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "../types.h"

namespace DON {

class Position;
class RootMoves;
class Options;

namespace Book {

class PolyGlot final {
   public:
    struct Entry final {
       public:
        constexpr bool operator==(const Entry& e) const noexcept {
            return key == e.key && move == e.move && weight == e.weight;
        }
        constexpr bool operator!=(const Entry& e) const noexcept { return !(*this == e); }

        constexpr bool operator<(const Entry& e) const noexcept {
            return key != e.key       ? key < e.key
                 : weight != e.weight ? weight < e.weight
                                      : move < e.move;
        }
        constexpr bool operator>(const Entry& e) const noexcept { return (e < *this); }
        constexpr bool operator<=(const Entry& e) const noexcept { return !(e < *this); }
        constexpr bool operator>=(const Entry& e) const noexcept { return !(*this < e); }

        Key           key;
        std::uint16_t move;
        std::uint16_t weight;
        std::uint32_t learn;
    };

    using Entries = std::vector<Entry>;

    PolyGlot() noexcept                           = default;
    PolyGlot(const PolyGlot&) noexcept            = delete;
    PolyGlot(PolyGlot&&) noexcept                 = delete;
    PolyGlot& operator=(const PolyGlot&) noexcept = delete;
    PolyGlot& operator=(PolyGlot&&) noexcept      = delete;

    bool load(const std::filesystem::path& bookFile) noexcept;

    [[nodiscard]] bool empty() const noexcept { return entries.empty(); }

    [[nodiscard]] std::string info() const noexcept;

    Move probe(Position& pos, const RootMoves& rootMoves, const Options& options) noexcept;

   private:
    void clear() noexcept;

    [[nodiscard]] std::size_t key_index(Key key) const noexcept;

    [[nodiscard]] Entries key_candidates(Key key) const noexcept;

    std::string filename;

    Entries entries;
};

}  // namespace Book
}  // namespace DON

#endif  // #ifndef BOOK_POLYGLOT_H_INCLUDED
