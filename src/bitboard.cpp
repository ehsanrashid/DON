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

#include <algorithm>
#if !defined(USE_POPCNT)
    #include <bitset>
#endif
#include <initializer_list>
#if !defined(NDEBUG)
    #include <memory>
    #include <mutex>
    #include <shared_mutex>
    #include <unordered_map>
    #include <utility>
#endif

#if !defined(USE_BMI2)
    #include "prng.h"
#endif

namespace DON {

namespace {

constexpr StdArray<std::size_t, 2> TABLE_SIZES{0x1480, 0x19000};

// Stores bishop & rook attacks
alignas(CACHE_LINE_SIZE) StdArray<Bitboard, TABLE_SIZES[0] + TABLE_SIZES[1]> AttacksTable;

alignas(CACHE_LINE_SIZE) constexpr StdArray<TableView<Bitboard>, 2> TableViews{
  TableView{AttacksTable.data() + 0x000000000000, TABLE_SIZES[0]},
  TableView{AttacksTable.data() + TABLE_SIZES[0], TABLE_SIZES[1]}};

// Computes sliding attack
template<PieceType PT>
Bitboard sliding_attacks_bb(Square s, Bitboard occupancyBB = 0) noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in sliding_attacks_bb()");
    assert(is_ok(s));
    constexpr StdArray<Direction, 2, 4> Directions{{
      {NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST},  //
      {NORTH, SOUTH, EAST, WEST}                         //
    }};

    Bitboard attacksBB = 0;

    for (Direction d : Directions[PT - BISHOP])
    {
        Square sq = s;

        for (Bitboard dstBB; (dstBB = destination_bb(sq, d));)
        {
            attacksBB |= dstBB;

            sq += d;

            if (occupancyBB & sq)
                break;
        }
    }

    return attacksBB;
}

// Computes all rook and bishop attacks at startup.
// Magic bitboards are used to look up attacks of sliding pieces.
// As a reference see https://www.chessprogramming.org/Magic_Bitboards.
// In particular, here use the so called "fancy" approach.
template<PieceType PT>
void init_magics() noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in init_magics()");

#if !defined(USE_BMI2)
    constexpr StdArray<std::size_t, 2> SubSizes{0x200, 0x1000};

    // Optimal PRNG seeds to pick the correct magics in the shortest time
    constexpr StdArray<std::uint16_t, RANK_NB> Seeds{
    // clang-format off
    #if defined(IS_64BIT)
      0x076F, 0x3763, 0x1048, 0x0B94, 0x2CC3, 0x04FE, 0x161F, 0x60F9
    #else
      0x5307, 0x125E, 0x0951, 0x01F5, 0x015A, 0x1B4A, 0x00CE, 0x142A
    #endif
      // clang-format on
    };
#endif

    [[maybe_unused]] std::size_t totalSize = 0;

    std::uint16_t size = 0;

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        // Given a square 's', the mask is the bitboard of sliding attacks from 's' computed on an empty board.
        // The index must be big enough to contain all the attacks for each possible subset of the mask and so
        // is 2 power the number of 1's of the mask.
        // Hence, deduce the size of the shift to apply to the 64 or 32 bits word to get the index.
        auto& magic = Magics[s][PT - BISHOP];

        // Set the offset for the attacks table of the square.
        // Individual table sizes for each square with "Fancy Magic Bitboards".
        //assert(s == SQ_A1 || size <= RefSizes[PT - BISHOP]);
        magic.attacksBB = s == SQ_A1 ? TableViews[PT - BISHOP].data()  //
                                     : &Magics[s - 1][PT - BISHOP].attacksBB[size];
        assert(magic.attacksBB != nullptr);

        // Board edges are not considered in the relevant occupancies
        Bitboard edgesBB = (EDGE_FILES_BB & ~file_bb(s)) | (PROMOTION_RANKS_BB & ~rank_bb(s));
        // Mask excludes edges
        magic.maskBB = sliding_attacks_bb<PT>(s) & ~edgesBB;

#if !defined(USE_BMI2)
        StdArray<Bitboard, SubSizes[PT - BISHOP]> referenceBBs;
        StdArray<Bitboard, SubSizes[PT - BISHOP]> occupancyBBs;
#endif
        size = 0;
        // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
        // store the corresponding sliding attack bitboard in referenceBBs[].
        Bitboard occupancyBB = 0;
        do
        {
#if !defined(USE_BMI2)
            referenceBBs[size] = sliding_attacks_bb<PT>(s, occupancyBB);
            occupancyBBs[size] = occupancyBB;
#else
            magic.attacks_bb(occupancyBB, sliding_attacks_bb<PT>(s, occupancyBB));
#endif
            ++size;
            occupancyBB = (occupancyBB - magic.maskBB) & magic.maskBB;

        } while (occupancyBB != 0);

        totalSize += size;

#if !defined(USE_BMI2)
        assert(size <= SubSizes[PT - BISHOP]);

        magic.shift =
    #if defined(IS_64BIT)
          64
    #else
          32
    #endif
          - popcount(magic.maskBB);

        PRNG<XoShiRo256Star> prng(Seeds[rank_of(s)]);

        StdArray<std::uint32_t, SubSizes[PT - BISHOP]> epoch{};
        std::uint32_t                                  cnt = 0;

        // Find a magic for square 's' picking up an (almost) random number
        // until find the one that passes the verification test.
        // this is trial−and−error iteration.
        while (true)
        {
            // Pick a candidate magic until it is "sparse enough"
            do
                magic.magicBB = prng.sparse_rand<Bitboard>();
            while (popcount((magic.magicBB * magic.maskBB) >> 56) < 6);

            bool valid = true;
            // A good magic must map every possible occupancy to an index that
            // looks up the correct sliding attack in the attacks[s] database.
            // Note that build up the database for square 's' as a side effect
            // of verifying the magic.
            // Keep track of the attempt count and save it in epoch[], little speed-up
            // trick to avoid resetting m.attacks[] after every failed attempt.
            ++cnt;
            for (std::uint16_t i = 0; i < size; ++i)
            {
                auto idx = magic.index(occupancyBBs[i]);

                if (epoch[idx] < cnt)
                {
                    epoch[idx]           = cnt;
                    magic.attacksBB[idx] = referenceBBs[i];
                }
                else if (magic.attacksBB[idx] != referenceBBs[i])
                {
                    valid = false;
                    break;
                }
            }

            if (valid)
                break;  // Found magic
        }
#endif
    }
    assert(totalSize == TABLE_SIZES[PT - BISHOP]);
}

// Explicit template instantiations:
template void init_magics<BISHOP>() noexcept;
template void init_magics<ROOK>() noexcept;

}  // namespace

namespace BitBoard {

// Initializes various bitboard tables.
// It is called at startup.
void init() noexcept {

#if !defined(USE_POPCNT)
    for (std::size_t i = 0; i < PopCnt.size(); ++i)
        PopCnt[i] = std::bitset<16>(i).count();
#endif

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
            Distances[s1][s2] = std::max(distance<File>(s1, s2), distance<Rank>(s1, s2));

    init_magics<BISHOP>();
    init_magics<ROOK>();

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
    {
        AttacksBBs[s1][WHITE] = pawn_attacks_bb<WHITE>(square_bb(s1));
        AttacksBBs[s1][BLACK] = pawn_attacks_bb<BLACK>(square_bb(s1));

        for (auto dir : {SOUTH_2 + WEST, SOUTH_2 + EAST, WEST_2 + SOUTH, EAST_2 + SOUTH,
                         WEST_2 + NORTH, EAST_2 + NORTH, NORTH_2 + WEST, NORTH_2 + EAST})
            AttacksBBs[s1][KNIGHT] |= destination_bb(s1, dir, 2);

        AttacksBBs[s1][BISHOP] = attacks_bb<BISHOP>(s1, 0);
        AttacksBBs[s1][ROOK]   = attacks_bb<ROOK>(s1, 0);
        AttacksBBs[s1][QUEEN]  = AttacksBBs[s1][BISHOP] | AttacksBBs[s1][ROOK];

        for (auto dir : {SOUTH_WEST, SOUTH, SOUTH_EAST, WEST, EAST, NORTH_WEST, NORTH, NORTH_EAST})
            AttacksBBs[s1][KING] |= destination_bb(s1, dir);

        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
        {
            for (PieceType pt : {BISHOP, ROOK})
                if (AttacksBBs[s1][pt] & s2)
                {
                    // clang-format off
                    LineBBs   [s1][s2] = (attacks_bb(s1, pt, 0) & attacks_bb(s2, pt, 0)) | s1 | s2;
                    BetweenBBs[s1][s2] = (attacks_bb(s1, pt, square_bb(s2)) & attacks_bb(s2, pt, square_bb(s1)));
                    PassRayBBs[s1][s2] = (attacks_bb(s1, pt, 0) & (attacks_bb(s2, pt, square_bb(s1)) | s2));
                    // clang-format on
                }
            BetweenBBs[s1][s2] |= s2;
            PassRayBBs[s1][s2] &= ~BetweenBBs[s1][s2];
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
        {
            str += " | ";
            str += (b & make_square(f, r)) ? '*' : ' ';
        }

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
        std::shared_lock readLock(mutex);
        if (auto itr = cache.find(b); itr != cache.end())
            return std::string_view{*itr->second};
    }

    // Build outside locks (may throw) — reduces lock contention
    auto str = std::make_unique<std::string>(pretty_str(b));

    // Slow path: exclusive (write) lock to insert (double-check to avoid races)
    {
        std::unique_lock writeLock(mutex);
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
