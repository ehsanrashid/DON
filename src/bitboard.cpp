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

#include "bitboard.h"

#include <bitset>
#include <initializer_list>
#if !defined(NDEBUG)
    #include <memory>
    #include <mutex>
    #include <shared_mutex>
    #include <unordered_map>
    #include <utility>
#endif

#if !defined(USE_BMI2)
    #include "misc.h"
#endif

namespace DON {

namespace {

constexpr Direction Directions[2][4]{{NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST},
                                     {NORTH, SOUTH, EAST, WEST}};

constexpr std::size_t AttacksSize[2]{0x1480, 0x19000};

alignas(CACHE_LINE_SIZE) Bitboard BishopAttacks[AttacksSize[0]];  // Stores bishop attacks
alignas(CACHE_LINE_SIZE) Bitboard RookAttacks[AttacksSize[1]];    // Stores rook attacks

alignas(CACHE_LINE_SIZE) Bitboard* Attacks[2]{BishopAttacks, RookAttacks};

// Returns the bitboard of target square from the given square for the given step.
// If the step is off the board, returns empty bitboard.
Bitboard safe_destination(Square s, Direction d, std::uint8_t dist = 1) noexcept {
    assert(is_ok(s));
    Square sq = s + d;
    return is_ok(sq) && distance(s, sq) <= dist ? square_bb(sq) : 0;
}

// Computes sliding attack
template<PieceType PT>
Bitboard sliding_attack(Square s, Bitboard occupied = 0) noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in sliding_attack()");
    assert(is_ok(s));

    Bitboard attacks = 0;
    for (Direction d : Directions[PT - BISHOP])
    {
        Square sq = s;

        for (Bitboard b; (b = safe_destination(sq, d));)
        {
            attacks |= b;
            sq += d;
            if (occupied & sq)
                break;
        }
    }
    return attacks;
}

// Computes all rook and bishop attacks at startup.
// Magic bitboards are used to look up attacks of sliding pieces.
// As a reference see https://www.chessprogramming.org/Magic_Bitboards.
// In particular, here use the so called "fancy" approach.
template<PieceType PT>
void init_magics() noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in init_magics()");

    constexpr std::size_t TableSize = 0x1000;

#if !defined(USE_BMI2)
    // Optimal PRNG seeds to pick the correct magics in the shortest time
    constexpr std::uint16_t MagicSeeds[RANK_NB]{
    // clang-format off
    #if defined(IS_64BIT)
      0x02D8, 0x284C, 0xD6E5, 0x8023, 0x2FF9, 0x3AFC, 0x4105, 0x00FF
    #else
      0x2311, 0xAE10, 0xD447, 0x9856, 0x1663, 0x73E5, 0x99D0, 0x427C
    #endif
      // clang-format on
    };

    Bitboard occupancy[TableSize];

    std::uint32_t epoch[TableSize]{}, cnt = 0;
#endif

    Bitboard reference[TableSize];

    std::uint16_t size = 0;

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        // Given a square 's', the mask is the bitboard of sliding attacks from 's' computed on an empty board.
        // The index must be big enough to contain all the attacks for each possible subset of the mask and so
        // is 2 power the number of 1's of the mask.
        // Hence, deduce the size of the shift to apply to the 64 or 32 bits word to get the index.
        auto& m = Magics[s][PT - BISHOP];

        assert(s == SQ_A1 || size < AttacksSize[PT - BISHOP]);
        // Set the offset for the attacks table of the square.
        // Individual table sizes for each square with "Fancy Magic Bitboards".
        m.attacks = s == SQ_A1 ? Attacks[PT - BISHOP] : Magics[s - 1][PT - BISHOP].attacks + size;
        assert(m.attacks != nullptr);
        size = 0;

        // Board edges are not considered in the relevant occupancies
        Bitboard edges = (EDGE_FILE_BB & ~file_bb(s)) | (PROMOTION_RANK_BB & ~rank_bb(s));

        m.mask = sliding_attack<PT>(s) & ~edges;

        // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
        // store the corresponding sliding attack bitboard in reference[].
        Bitboard b = 0;
        do
        {
            reference[size] = sliding_attack<PT>(s, b);
#if !defined(USE_BMI2)
            occupancy[size] = b;
#else
            m.attacks_bb(b, reference[size]);
#endif
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);

#if !defined(USE_BMI2)

        m.shift =
    #if defined(IS_64BIT)
          64
    #else
          32
    #endif
          - popcount(m.mask);

        PRNG rng(MagicSeeds[rank_of(s)]);
        // Find a magic for square 's' picking up an (almost) random number
        // until find the one that passes the verification test.
        for (std::uint16_t i = 0; i < size;)
        {
            do
                m.magic = rng.sparse_rand<Bitboard>();
            while (popcount((m.magic * m.mask) >> 56) < 6);

            // A good magic must map every possible occupancy to an index that
            // looks up the correct sliding attack in the attacks[s] database.
            // Note that build up the database for square 's' as a side effect
            // of verifying the magic. Keep track of the attempt count and
            // save it in epoch[], little speed-up trick to avoid resetting
            // m.attacks[] after every failed attempt.
            for (++cnt, i = 0; i < size; ++i)
            {
                auto idx = m.index(occupancy[i]);

                if (epoch[idx] < cnt)
                {
                    epoch[idx]     = cnt;
                    m.attacks[idx] = reference[i];
                    continue;
                }
                if (m.attacks[idx] != reference[i])
                    break;
            }
        }
#endif
    }
}

}  // namespace

namespace BitBoard {

// Initializes various bitboard tables.
// It is called at startup.
void init() noexcept {

#if !defined(USE_POPCNT)
    for (unsigned int i = 0; i < POPCNT_SIZE; ++i)
        PopCnt[i] = std::bitset<16>(i).count();
#endif
    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
    {
        assert(msb_index(fill_prefix_bb(square_bb(s1))) == s1);

        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            Distances[s1][s2] = std::max(distance<File>(s1, s2), distance<Rank>(s1, s2));
    }

    init_magics<BISHOP>();
    init_magics<ROOK>();

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
    {
        PieceAttacks[s1][WHITE] = pawn_attacks_bb<WHITE>(square_bb(s1));
        PieceAttacks[s1][BLACK] = pawn_attacks_bb<BLACK>(square_bb(s1));

        for (auto dir : {SOUTH_2 + WEST, SOUTH_2 + EAST, WEST_2 + SOUTH, EAST_2 + SOUTH,
                         WEST_2 + NORTH, EAST_2 + NORTH, NORTH_2 + WEST, NORTH_2 + EAST})
            PieceAttacks[s1][KNIGHT] |= safe_destination(s1, dir, 2);

        PieceAttacks[s1][BISHOP] = attacks_bb<BISHOP>(s1, 0);
        PieceAttacks[s1][ROOK]   = attacks_bb<ROOK>(s1, 0);
        PieceAttacks[s1][QUEEN]  = PieceAttacks[s1][BISHOP] | PieceAttacks[s1][ROOK];

        for (auto dir : {SOUTH_WEST, SOUTH, SOUTH_EAST, WEST, EAST, NORTH_WEST, NORTH, NORTH_EAST})
            PieceAttacks[s1][KING] |= safe_destination(s1, dir);

        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
        {
            for (PieceType pt : {BISHOP, ROOK})
                if (PieceAttacks[s1][pt] & s2)
                {
                    Lines[s1][s2] = (attacks_bb(pt, s1, 0) & attacks_bb(pt, s2, 0)) | s1 | s2;
                    Betweens[s1][s2] =
                      (attacks_bb(pt, s1, square_bb(s2)) & attacks_bb(pt, s2, square_bb(s1)));
                }
            Betweens[s1][s2] |= s2;
        }
    }
}

#if !defined(NDEBUG)
// Returns an ASCII representation of a bitboard suitable
// to be printed to standard output. Useful for debugging.
std::string pretty_str(Bitboard b) noexcept {
    constexpr std::string_view Sep{"\n  +---+---+---+---+---+---+---+---+\n"};

    std::string str;
    str.reserve(646);
    str += Sep;
    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
        str += to_char(r);
        for (File f = FILE_A; f <= FILE_H; ++f)
            str += ((b & make_square(f, r)) ? " | X" : " |  ");
        str += " |";
        str += Sep;
    }
    str += " ";
    for (File f = FILE_A; f <= FILE_H; ++f)
    {
        str += "   ";
        str += to_char<true>(f);
    }
    return str;
}

std::string_view pretty(Bitboard b) noexcept {
    static std::shared_mutex                                          mutex;
    static std::unordered_map<Bitboard, std::unique_ptr<std::string>> cache;
    // One-time reserve to reduce rehashes (runs once, thread-safe since C++11)
    [[maybe_unused]] static const bool reserved = []() {
        cache.reserve(1024);           // choose an appropriate capacity
        cache.max_load_factor(0.75f);  // optional: tune load factor
        return true;
    }();
    assert(reserved);

    // Fast path: shared (read) lock
    {
        // Read Lock
        std::shared_lock sharedLock(mutex);
        if (auto itr = cache.find(b); itr != cache.end())
            return std::string_view{*itr->second};
    }

    // Build outside locks (may throw) — reduces lock contention
    auto str = std::make_unique<std::string>(pretty_str(b));

    // Slow path: exclusive (write) lock to insert (double-check to avoid races)
    {
        // Write Lock
        std::unique_lock uniqueLock(mutex);
        // Check again to avoid duplicate insertion if another thread inserted meanwhile
        if (auto itr = cache.find(b); itr != cache.end())
            return std::string_view{*itr->second};
        auto [itr, inserted] = cache.emplace(b, std::move(str));
        return std::string_view{*itr->second};
    }
}
#endif

}  // namespace BitBoard
}  // namespace DON
