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

#include "bitboard.h"

#include <chrono>
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
    #if defined(USE_COMP)
  Bitboard16
    #else
  Bitboard
    #endif
#else
  Bitboard
#endif
  ,
  TABLE_SIZES[0] + TABLE_SIZES[1]> AttacksTable;

alignas(CACHE_LINE_SIZE) StdArray<TableView<
#if defined(USE_BMI2)
    #if defined(USE_COMP)
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

// Computes all bishop & rook attacks at startup.
// Magic bitboards are used to look up attacks of sliding pieces.
// As a reference see https://www.chessprogramming.org/Magic_Bitboards.
// In particular, here use the so called "fancy" approach.
template<PieceType PT>
void init_magics() noexcept {
    static_assert(PT == BISHOP || PT == ROOK, "Unsupported piece type in init_magics()");

#if !defined(USE_BMI2)
    constexpr StdArray<std::size_t, 2> BlockSizes{0x200, 0x1000};

    // Optimal PRNG seeds to pick the correct magics in the shortest time
    constexpr StdArray<std::uint16_t, 2, RANK_NB> Seeds{{
    #if defined(IS_64BIT)
      {0xE4D9, 0xB1E5, 0x4F73, 0x82A9, 0x323A, 0xFFF4, 0x0C61, 0x5EFA},
      {0x8B99, 0x9A36, 0xD27A, 0x5F4C, 0xFC29, 0x0982, 0x10E1, 0x00AA}
    #else
      {0xFE9A, 0x4968, 0xA30A, 0x3429, 0xAA36, 0xAEAF, 0x228A, 0xAA4C},
      {0x02F6, 0x00C0, 0x8522, 0x0972, 0xF31A, 0xF6D0, 0xDA74, 0x98E5}
    #endif
    }};
#endif

    [[maybe_unused]] std::size_t totalSize = 0;

    std::uint16_t size = 0;

    for (Square s = SQ_A1; s <= SQ_H8; ++s)
    {
        // Get the magic for the square and piece type
        auto& magic = MAGICS[s][PT - BISHOP];

        // Set the offset for the attacks table of the square.
        // Individual table sizes for each square with "Fancy Magic Bitboards".
        //assert(s == SQ_A1 || size <= RefSizes[PT - BISHOP]);
        magic.attacksBBs = s == SQ_A1 ? TableViews[PT - BISHOP].data()  //
                                      : &MAGICS[s - 1][PT - BISHOP].attacksBBs[size];
        assert(magic.attacksBBs != nullptr);

        // Get the pseudo attacks on an empty board
        Bitboard pseudoAttacksBB = attacks_bb(s, PT);

        // Board edges are not considered in the relevant occupancies
        Bitboard edgesBB = (EDGE_FILES_BB & ~file_bb(s)) | (PROMOTION_RANKS_BB & ~rank_bb(s));

        // Compute the mask of relevant occupancy bits for the square and piece type
        magic.maskBB = pseudoAttacksBB & ~edgesBB;

#if defined(USE_BMI2)
    #if defined(USE_COMP)
        magic.reMaskBB = pseudoAttacksBB;
    #endif
#else
        StdArray<Bitboard, BlockSizes[PT - BISHOP]> occupancyBBs;
        StdArray<Bitboard, BlockSizes[PT - BISHOP]> referenceBBs;
#endif

        size = 0;
        // Use Carry-Rippler trick to enumerate all subsets of masks[s] and
        // store the corresponding sliding attack bitboard in referenceBBs[].
        Bitboard occupancyBB = 0;
        do
        {
            Bitboard slidingAttacksBB = sliding_attacks_bb<PT>(s, occupancyBB);

#if defined(USE_BMI2)
            magic.attacks_bb(occupancyBB, slidingAttacksBB);
#else
            occupancyBBs[size] = occupancyBB;
            referenceBBs[size] = slidingAttacksBB;
#endif
            ++size;
            occupancyBB = (occupancyBB - magic.maskBB) & magic.maskBB;

        } while (occupancyBB != 0);

        totalSize += size;

#if !defined(USE_BMI2)
        assert(size <= BlockSizes[PT - BISHOP]);

        // Compute the shift value (to apply to the 64-bits or 32-bits) used in the index computation
        magic.shift =
    #if defined(IS_64BIT)
          64
    #else
          32
    #endif
          - popcount(magic.maskBB);

        XorShift64Star prng(Seeds[PT - BISHOP][rank_of(s)]);

        // Epoch array to speed-up the magic verification process
        StdArray<std::uint32_t, BlockSizes[PT - BISHOP]> epoch{};
        std::uint32_t                                    cnt = 0;

        // Find a magic for square picking up an (almost) random number
        // until find the one that passes the verification test.
        // This is trial−and−error iteration.
        while (true)
        {
            // Pick a candidate magic until it is "sparse enough"
            do
                magic.magicBB = prng.sparse_rand<Bitboard>();
            while (popcount((magic.magicBB * magic.maskBB) >> 56) < 6);

            ++cnt;

            bool magicOk = true;

            // A good magic must map every possible occupancy to an index that
            // looks up the correct sliding attack in the attacks[s] database.
            // Note that build up the database for square as a side effect of verifying the magic.
            // Keep track of the attempt count and save it in epoch[], little speed-up
            // trick to avoid resetting magic.attacksBBs[] after every failed attempt.
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

// Evaluate candidate seed for a given rank and piece type
template<PieceType PT>
std::uint64_t evaluate_seed_for_rank(Rank r, std::uint16_t candidateSeed) noexcept {

    constexpr StdArray<std::size_t, 2> BlockSizes{0x200, 0x1000};

    std::uint64_t totalAttempts = 0;

    // Evaluate all squares on the given rank (file 0..7)
    for (File f = FILE_A; f <= FILE_H; ++f)
    {
        Square s = make_square(f, r);

        auto magic = Magic();

        Bitboard pseudoAttacksBB = attacks_bb(s, PT);

        // Board edges are not considered in the relevant occupancies
        Bitboard edgesBB = (EDGE_FILES_BB & ~file_bb(s)) | (PROMOTION_RANKS_BB & ~rank_bb(s));

        // Mask excludes edges
        magic.maskBB = pseudoAttacksBB & ~edgesBB;

        StdArray<Bitboard, BlockSizes[PT - BISHOP]> occupancyBBs;
        StdArray<Bitboard, BlockSizes[PT - BISHOP]> referenceBBs;

        std::uint16_t size = 0;

        // Use Carry-Rippler trick to enumerate all subsets of masks[s]
        Bitboard occupancyBB = 0;
        do
        {
            Bitboard slidingAttacksBB = sliding_attacks_bb<PT>(s, occupancyBB);

            occupancyBBs[size] = occupancyBB;
            referenceBBs[size] = slidingAttacksBB;

            ++size;
            occupancyBB = (occupancyBB - magic.maskBB) & magic.maskBB;

        } while (occupancyBB != 0);

        assert(size <= BlockSizes[PT - BISHOP]);

        magic.shift =
#if defined(IS_64BIT)
          64
#else
          32
#endif
          - popcount(magic.maskBB);

        XorShift64Star prng(candidateSeed);

        std::uint32_t attempts = 0;

        StdArray<std::uint32_t, BlockSizes[PT - BISHOP]> epoch{};
        std::uint32_t                                    cnt = 0;

        StdArray<Bitboard, BlockSizes[PT - BISHOP]> attacksBBs{};

        while (true)
        {
            // Pick a candidate magic until it is "sparse enough"
            do
                magic.magicBB = prng.sparse_rand<Bitboard>();
            while (popcount((magic.magicBB * magic.maskBB) >> 56) < 6);

            ++attempts;  // count this candidate draw
            ++cnt;

            bool magicOk = true;

            // A good magic must map every possible occupancy to an index that
            // looks up the correct sliding attack in the attacks[s] database.
            // Note that build up the database for square 's' as a side effect
            // of verifying the magic.
            // Keep track of the attempt count and save it in epoch[], little speed-up
            // trick to avoid resetting m.attacks[] after every failed attempt.
            for (std::uint16_t i = 0; i < size; ++i)
            {
                std::uint16_t idx = magic.index(occupancyBBs[i]);

                // Slot unused for this candidate -> record mapping
                if (epoch[idx] < cnt)
                {
                    epoch[idx]      = cnt;
                    attacksBBs[idx] = referenceBBs[i];
                }
                // Slot already used for this candidate: verify it matches
                else if (attacksBBs[idx] != referenceBBs[i])
                {
                    magicOk = false;
                    break;
                }
            }

            if (magicOk)
                break;
        }

        totalAttempts += attempts;
    }

    return totalAttempts;
}

// Brute force search for best seed for one rank
template<PieceType PT>
std::uint16_t find_best_seed_for_rank(Rank r) noexcept {

    constexpr StdArray<std::uint16_t, 2, RANK_NB> InitialSeeds{{
#if defined(IS_64BIT)
      {0xE4D9, 0xB1E5, 0x4F73, 0x82A9, 0x323A, 0xFFF4, 0x0C61, 0x5EFA},
      {0x8B99, 0x9A36, 0xD27A, 0x5F4C, 0xFC29, 0x0982, 0x10E1, 0x00AA}
#else
      {0xFE9A, 0x4968, 0xA30A, 0x3429, 0xAA36, 0xAEAF, 0x228A, 0xAA4C},
      {0x02F6, 0x00C0, 0x8522, 0x0972, 0xF31A, 0xF6D0, 0xDA74, 0x98E5}
#endif
    }};

    std::uint16_t bestSeed = 1;

    std::uint64_t bestAttempts = 0xFFFFFFFFFFFFFFFFULL;

    for (std::uint32_t candSeed = InitialSeeds[PT - BISHOP][r]; candSeed <= 0xFFFFU; ++candSeed)
    {
        std::uint64_t attempts = evaluate_seed_for_rank<PT>(r, std::uint16_t(candSeed));

        if (bestAttempts > attempts)
        {
            bestAttempts = attempts;

            bestSeed = std::uint16_t(candSeed);

            std::cout << "Rank " << to_char(r) << " attempts " << bestAttempts  //
                      << " seed = 0x" << std::hex << bestSeed << std::dec << "\n";

            if (bestAttempts <= 2000)
                break;
        }
    }

    return bestSeed;
}

// Driver that finds best seeds for all ranks for a given piece type
template<PieceType PT>
auto find_best_seeds() noexcept {
    StdArray<std::uint16_t, RANK_NB> seeds;

    std::cout << "Finding best seeds for piece type (" << to_char(PT) << ")...\n";

    for (Rank r = RANK_1; r <= RANK_8; ++r)
    {
        std::cout << "Searching Rank " << to_char(r) << "...\n";

        seeds[r] = find_best_seed_for_rank<PT>(r);
    }

    return seeds;
}

void print_seeds(const StdArray<std::uint16_t, RANK_NB>& seeds) noexcept {
    std::cout << "{ ";

    for (std::size_t i = 0; i < seeds.size(); ++i)
        std::cout << "0x" << std::hex << seeds[i] << (i + 1 < seeds.size() ? ", " : " ");

    std::cout << std::dec << "}\n";
}

}  // namespace

namespace BitBoard {

// Initializes various bitboard tables.
// It is called at startup.
void init() noexcept {

    init_magics<BISHOP>();
    init_magics<ROOK>();

    for (Square s1 = SQ_A1; s1 <= SQ_H8; ++s1)
    {
        Bitboard s1BB = make_bb(s1);

        for (Square s2 = SQ_A1; s2 <= SQ_H8; ++s2)
        {
            Bitboard s2BB = make_bb(s2);

            for (PieceType pt : {BISHOP, ROOK})
            {
                if ((attacks_bb(s1, pt) & s2BB) != 0)
                {
                    // clang-format off
                    BETWEEN_BBs[s1][s2] = attacks_bb(s1, pt, s2BB) & attacks_bb(s2, pt, s1BB);

                    PASS_RAY_BBs[s1][s2] = attacks_bb(s1, pt, 0) & (attacks_bb(s2, pt, s1BB) | s2BB);
                    // clang-format on
                }
            }

            BETWEEN_BBs[s1][s2] |= s2BB;
        }
    }
}

// Harness to find optimal magic bitboard seeds
void find_magic_seeds() noexcept {

    std::cout << "Seed search harness for magic bitboards" << std::endl;
#if defined(IS_64BIT)
    std::cout << "Compiled for 64-bit Bitboard\n" << std::endl;
#else
    std::cout << "Compiled for 32-bit Bitboard\n" << std::endl;
#endif

    // You can choose to optimize for BISHOP, ROOK or both.
    // Here we run for both and print two arrays.
    // If you prefer to combine results
    // (e.g. pick a seed that minimizes sum of attempts across both piece types),
    // gather both arrays and post-process.

    std::chrono::steady_clock::time_point startTime, endTime;

    startTime = std::chrono::steady_clock::now();

    auto bishopSeeds = find_best_seeds<BISHOP>();

    endTime = std::chrono::steady_clock::now();

    std::cout << "Bishop search took "
              << std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count()
              << "s\n"
              << std::endl;

    startTime = std::chrono::steady_clock::now();

    auto rookSeeds = find_best_seeds<ROOK>();

    endTime = std::chrono::steady_clock::now();

    std::cout << "Rook search took "
              << std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime).count()
              << "s\n"
              << std::endl;

    std::cout << "Best Seeds for BISHOP: ";
    print_seeds(bishopSeeds);

    std::cout << "Best Seeds for ROOK: ";
    print_seeds(rookSeeds);

    std::cout << "Done!!!\n" << std::endl;
}

// Returns an ASCII representation of a bitboard suitable
// to be printed to standard output. Useful for debugging.
std::string pretty_str(Bitboard b) noexcept {
    constexpr std::string_view Sep{"\n  +---+---+---+---+---+---+---+---+\n"};

    std::string str;
    str.reserve(646);

    str = Sep;

    for (Rank r = RANK_8;; --r)
    {
        str += to_char(r);

        for (File f = FILE_A; f <= FILE_H; ++f)
        {
            str += " | ";
            str += (b & make_square(f, r)) ? '*' : ' ';
        }

        str += " |";
        str += Sep;

        if (r == RANK_1)
            break;
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
    constexpr std::size_t ReserveCount  = 1024;
    constexpr float       MaxLoadFactor = 0.75f;

    // Thread-safe static initialization

    // Fully RAII-compliant — destructor runs at program exit
    //static auto cache = ConcurrentCache<Bitboard, std::string>(ReserveCount, MaxLoadFactor);

    // Standard intentional "leaky singleton" pattern.
    // Ensures the cache lives for the entire program, never deleted.
    //static auto& cache = *new ConcurrentCache<Bitboard, std::string>(ReserveCount, MaxLoadFactor);
    static auto& cache = *([=] {
        static auto cachePtr =
          std::make_unique<ConcurrentCache<Bitboard, std::string>>(ReserveCount, MaxLoadFactor);
        return cachePtr.get();
    })();

    //return cache.access_or_build(b, pretty_str(b));
    return cache.transform_access_or_build(
      b, [](const std::string& str) noexcept -> std::string_view { return str; }, pretty_str(b));
}

}  // namespace BitBoard
}  // namespace DON
