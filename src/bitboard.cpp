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
#include <sstream>

#include "misc.h"
#include "uci.h"

namespace DON {

#if !defined(USE_POPCNT)
std::uint8_t PopCnt16[PopCntSize];
#endif
std::uint8_t SquareDistance[SQUARE_NB][SQUARE_NB];

Bitboard LineBB[SQUARE_NB][SQUARE_NB];
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];

Magic BishopMagics[SQUARE_NB];
Magic RookMagics[SQUARE_NB];

namespace {

Bitboard BishopAttacks[0x1480];  // Stores bishop attacks
Bitboard RookAttacks[0x19000];   // Stores rook attacks

constexpr Direction BishopDirections[4]{NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST};
constexpr Direction RookDirections[4]{NORTH, SOUTH, EAST, WEST};

// Returns the bitboard of target square from the given square for the given step.
// If the step is off the board, returns empty bitboard.
Bitboard safe_destination(Square s, Direction step, std::uint8_t dist = 1) noexcept {
    Square sq = s + step;
    return is_ok(sq) && distance(s, sq) <= dist ? square_bb(sq) : 0;
}

// Computes sliding attacks
template<PieceType PT>
Bitboard sliding_attack(Square s, Bitboard occupied) noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in sliding_attack()");

    Bitboard attacks = 0;
    for (Direction d : (PT == BISHOP ? BishopDirections : RookDirections))
    {
        Square sq = s;
        while (safe_destination(sq, d) && !(occupied & sq))
            attacks |= (sq += d);
    }

    return attacks;
}

// Computes all rook and bishop attacks at startup.
// Magic bitboards are used to look up attacks of sliding pieces.
// As a reference see www.chessprogramming.org/Magic_Bitboards.
// In particular, here use the so called "fancy" approach.
template<PieceType PT>
void init_magics() noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in init_magics()");

    constexpr std::uint16_t TableSize = 4096;

#if !defined(USE_PEXT)
    // Optimal PRNG seeds to pick the correct magics in the shortest time
    constexpr std::uint32_t Seeds[RANK_NB] {
    #if defined(IS_64BIT)
        728, 10316, 55013, 32803, 12281, 15100, 16645, 255
    #else
        8977, 44560, 54343, 38998, 5731, 95205, 104912, 17020
    #endif
    };

    Bitboard      occupancy[TableSize];
    std::uint32_t epoch[TableSize]{}, cnt = 0;
#endif

    Bitboard      reference[TableSize];
    std::uint16_t size;

    Bitboard* attacks = (PT == BISHOP ? BishopAttacks : RookAttacks);
    Magic*    magics  = (PT == BISHOP ? BishopMagics : RookMagics);

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        // Board edges are not considered in the relevant occupancies
        Bitboard edges = ((Rank1BB | Rank8BB) & ~rank_bb(s)) | ((FileABB | FileHBB) & ~file_bb(s));

        // Given a square 's', the mask is the bitboard of sliding attacks from
        // 's' computed on an empty board. The index must be big enough to contain
        // all the attacks for each possible subset of the mask and so is 2 power
        // the number of 1s of the mask. Hence, deduce the size of the shift to
        // apply to the 64 or 32 bits word to get the index.
        Magic& m = magics[s];

        m.mask = sliding_attack<PT>(s, 0) & ~edges;
#if !defined(USE_PEXT)
        m.shift =
    #if defined(IS_64BIT)
          64
    #else
          32
    #endif
          - popcount(m.mask);
#endif
        // Set the offset for the attacks table of the square.
        // Individual table sizes for each square with "Fancy Magic Bitboards".
        m.attacks = (s == SQ_A1) ? attacks : magics[s - 1].attacks + size;

        // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
        // store the corresponding sliding attack bitboard in reference[].
        Bitboard b = size = 0;
        do
        {
            reference[size] = sliding_attack<PT>(s, b);
#if defined(USE_PEXT)
            m.attacks[m.index(b)] = reference[size];
#else
            occupancy[size] = b;
#endif
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);

#if !defined(USE_PEXT)

        PRNG rng(Seeds[rank_of(s)]);
        // Find a magic for square 's' picking up an (almost) random number
        // until find the one that passes the verification test.
        for (std::uint16_t i = 0; i < size;)
        {
            for (m.magic = 0; popcount((m.magic * m.mask) >> 56) < 6;)
                m.magic = rng.sparse_rand<Bitboard>();

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
                }
                else if (m.attacks[idx] != reference[i])
                    break;
            }
        }
#endif
    }
}

}  // namespace

namespace Bitboards {

// Initializes various bitboard tables.
// It is called at startup.
void init() noexcept {

#if !defined(USE_POPCNT)
    for (std::uint32_t i = 0U; i < PopCntSize; ++i)
        PopCnt16[i] = std::uint8_t(std::bitset<16>(i).count());
#endif
    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            SquareDistance[s1][s2] = std::max(distance<File>(s1, s2), distance<Rank>(s1, s2));

    init_magics<BISHOP>();
    init_magics<ROOK>();

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        for (Color c : {WHITE, BLACK})
            PawnAttacks[c][s] = pawn_attacks_bb(c, square_bb(s));

        PseudoAttacks[0][s] = PseudoAttacks[1][s] = PseudoAttacks[7][s] = 0ULL;

        PseudoAttacks[KNIGHT][s] = 0ULL;
        for (auto d : {SOUTH_2 + WEST, SOUTH_2 + EAST, WEST_2 + SOUTH, EAST_2 + SOUTH,
                       WEST_2 + NORTH, EAST_2 + NORTH, NORTH_2 + WEST, NORTH_2 + EAST})
            PseudoAttacks[KNIGHT][s] |= safe_destination(s, d, 2);
        PseudoAttacks[KING][s] = 0ULL;
        for (auto d : {SOUTH_WEST, SOUTH, SOUTH_EAST, WEST, EAST, NORTH_WEST, NORTH, NORTH_EAST})
            PseudoAttacks[KING][s] |= safe_destination(s, d);

        PseudoAttacks[BISHOP][s] = attacks_bb<BISHOP>(s, 0ULL);
        PseudoAttacks[ROOK][s]   = attacks_bb<ROOK>(s, 0ULL);
        PseudoAttacks[QUEEN][s]  = PseudoAttacks[BISHOP][s] | PseudoAttacks[ROOK][s];
    }
    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
        {
            LineBB[s1][s2] = BetweenBB[s1][s2] = 0ULL;
            for (PieceType pt : {BISHOP, ROOK})
            {
                if (PseudoAttacks[pt][s1] & s2)
                {
                    LineBB[s1][s2] = (PseudoAttacks[pt][s1] & PseudoAttacks[pt][s2]) | s1 | s2;
                    BetweenBB[s1][s2] =
                      (attacks_bb(pt, s1, square_bb(s2)) & attacks_bb(pt, s2, square_bb(s1)));
                }
                BetweenBB[s1][s2] |= s2;
            }
        }
}

#if !defined(NDEBUG)
// Returns an ASCII representation of a bitboard suitable
// to be printed to standard output. Useful for debugging.
std::string pretty(Bitboard b) noexcept {
    std::ostringstream oss;
    oss << "+---+---+---+---+---+---+---+---+\n";
    for (Rank r = RANK_8; r >= RANK_1; --r)
    {
        for (File f = FILE_A; f <= FILE_H; ++f)
            oss << "| " << ((b & make_square(f, r)) ? "X" : " ") << " ";

        oss << "| " << UCI::rank(r) << "\n+---+---+---+---+---+---+---+---+\n";
    }
    oss << "  ";
    for (File f = FILE_A; f <= FILE_H; ++f)
        oss << UCI::file(f) << "   ";
    oss << '\n';

    return oss.str();
}
#endif

}  // namespace Bitboards
}  // namespace DON
