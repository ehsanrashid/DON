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

#include <memory>

#if !defined(USE_BMI2)
    #include "prng.h"
#endif

namespace DON {

namespace {

constexpr StdArray<std::size_t, 2> TABLE_SIZES{0x1480, 0x19000};

// Stores bishop & rook attacks
alignas(CACHE_LINE_SIZE) StdArray<
#if defined(USE_BMI2)
    #if defined(USE_COMPRESSED)
  Bitboard16
    #else
  Bitboard
    #endif
#else
  Bitboard
#endif
  ,
  TABLE_SIZES[0] + TABLE_SIZES[1]> AttacksTable;

alignas(CACHE_LINE_SIZE) constexpr StdArray<TableView<
#if defined(USE_BMI2)
    #if defined(USE_COMPRESSED)
                                              Bitboard16
    #else
                                              Bitboard
    #endif
#else
                                              Bitboard
#endif
                                              >,
                                            2> TableViews{
  TableView{AttacksTable.data() + 0x000000000000, TABLE_SIZES[0]},
  TableView{AttacksTable.data() + TABLE_SIZES[0], TABLE_SIZES[1]}};

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
      0x02D8, 0x284C, 0xD6E5, 0x8023, 0x2FF9, 0x3AFC, 0x4105, 0x00FF
    #else
      0x2311, 0xAE10, 0xD447, 0x9856, 0x1663, 0x73E5, 0x99D0, 0x427C
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
        magic.attacksBBs = s == SQ_A1 ? TableViews[PT - BISHOP].data()  //
                                      : &Magics[s - 1][PT - BISHOP].attacksBBs[size];
        assert(magic.attacksBBs != nullptr);

        Bitboard slidingAttacksBB = sliding_attacks_bb<PT>(s);

        // Board edges are not considered in the relevant occupancies
        Bitboard edgesBB = (EDGE_FILES_BB & ~file_bb(s)) | (PROMOTION_RANKS_BB & ~rank_bb(s));

        // Mask excludes edges
        magic.maskBB = slidingAttacksBB & ~edgesBB;

#if defined(USE_BMI2)
    #if defined(USE_COMPRESSED)
        magic.reMaskBB = slidingAttacksBB;
    #endif
#else
        StdArray<Bitboard, SubSizes[PT - BISHOP]> referenceBBs;
        StdArray<Bitboard, SubSizes[PT - BISHOP]> occupancyBBs;
#endif

        size = 0;
        // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
        // store the corresponding sliding attack bitboard in referenceBBs[].
        Bitboard occupancyBB = 0;
        do
        {
#if defined(USE_BMI2)
            magic.attacks_bb(occupancyBB, sliding_attacks_bb<PT>(s, occupancyBB));
#else
            referenceBBs[size] = sliding_attacks_bb<PT>(s, occupancyBB);
            occupancyBBs[size] = occupancyBB;
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

        XorShift64Star prng(Seeds[rank_of(s)]);

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

            bool magicOk = true;
            // A good magic must map every possible occupancy to an index that
            // looks up the correct sliding attack in the attacks[s] database.
            // Note that build up the database for square 's' as a side effect
            // of verifying the magic.
            // Keep track of the attempt count and save it in epoch[], little speed-up
            // trick to avoid resetting m.attacks[] after every failed attempt.
            ++cnt;
            for (std::uint16_t i = 0; i < size; ++i)
            {
                std::uint16_t idx = magic.index(occupancyBBs[i]);

                if (epoch[idx] < cnt)
                {
                    epoch[idx]            = cnt;
                    magic.attacksBBs[idx] = referenceBBs[i];
                }
                else if (magic.attacksBBs[idx] != referenceBBs[i])
                {
                    magicOk = false;
                    break;
                }
            }

            if (magicOk)
                break;
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

    init_magics<BISHOP>();
    init_magics<ROOK>();

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
    {
        Bitboard s1BB = square_bb(s1);

        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
        {
            Bitboard s2BB = square_bb(s2);

            for (PieceType pt : {BISHOP, ROOK})
                if (AttacksBBs[s1][pt] & s2)
                {
                    BetweenBBs[s1][s2] = attacks_bb(s1, pt, s2BB) & attacks_bb(s2, pt, s1BB);
                    PassRayBBs[s1][s2] = attacks_bb(s1, pt, 0) & (attacks_bb(s2, pt, s1BB) | s2BB);
                }

            BetweenBBs[s1][s2] |= s2BB;
        }
    }
}

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
    constexpr std::size_t Reserve    = 1024;
    constexpr float       LoadFactor = 0.75f;

    // Thread-safe static initialization

    // Fully RAII-compliant — destructor runs at program exit
    //static auto cache = ConcurrentCache<Bitboard, std::string>(Reserve, LoadFactor);

    // Standard intentional "leaky singleton" pattern.
    // Ensures the cache lives for the entire program, never deleted.
    //static auto& cache = *new ConcurrentCache<Bitboard, std::string>(Reserve, LoadFactor);
    static auto& cache = *([=] {
        static auto pCache =
          std::make_unique<ConcurrentCache<Bitboard, std::string>>(Reserve, LoadFactor);
        return pCache.get();
    })();

    //return cache.access_or_build(b, pretty_str(b));
    return cache.transform_access_or_build(
      b, [](const std::string& str) noexcept -> std::string_view { return str; }, pretty_str(b));
}

}  // namespace BitBoard
}  // namespace DON
