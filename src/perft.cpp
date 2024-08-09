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

#include "perft.h"

#include <iomanip>
#include <iostream>
#include <sstream>

#include "memory.h"
#include "misc.h"
#include "movegen.h"
#include "position.h"
#include "thread.h"
#include "uci.h"

namespace DON::Benchmark {

namespace {

struct Perft final {

    void classify(Position& pos, Move m) noexcept;

    void operator+=(const Perft& perft) noexcept;
    //void operator-=(const Perft& perft) noexcept;

    std::uint16_t count     = 0;
    std::uint64_t nodes     = 0;
    std::uint64_t capture   = 0;
    std::uint64_t enpassant = 0;
    std::uint64_t anyCheck  = 0;
    std::uint64_t dscCheck  = 0;
    std::uint64_t dblCheck  = 0;
    std::uint64_t castle    = 0;
    std::uint64_t promotion = 0;
    std::uint64_t checkmate = 0;
    std::uint64_t stalemate = 0;
};

void Perft::classify(Position& pos, Move m) noexcept {

    const Square org = m.org_sq(), dst = m.dst_sq();
    const Color  stm = pos.side_to_move();

    StateInfo st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    if (pos.capture(m))
    {
        ++capture;
        if (m.type_of() == EN_PASSANT)
            ++enpassant;
    }

    if (pos.gives_check(m))
    {
        ++anyCheck;
        if (!(pos.checks(m.type_of() != PROMOTION ? type_of(pos.piece_on(org))  //
                                                  : m.promotion_type())
              & dst))
        {
            if (pos.blockers(~stm) & org)
                ++dscCheck;
            else if (m.type_of() == EN_PASSANT)
            {
                const Bitboard occupied =
                  (pos.pieces() ^ org ^ make_square(file_of(dst), rank_of(org))) | dst;
                if ((pos.pieces(stm, QUEEN, BISHOP)
                     & attacks_bb<BISHOP>(pos.king_square(~stm), occupied))
                    | (pos.pieces(stm, QUEEN, ROOK)
                       & attacks_bb<ROOK>(pos.king_square(~stm), occupied)))
                    ++dscCheck;
            }
            //else if (m.type_of() == CASTLING && pos.checks(ROOK) & rook_castle_sq(stm, org, dst))
            //    ++dscCheck;
        }

        if (pos.gives_dbl_check(m))
            ++dblCheck;

        pos.do_move(m, st, true);
        assert(pos.checkers() && popcount(pos.checkers()) <= 2);
        //if (more_than_one(pos.checkers()))
        //    ++dblCheck;
        if (MoveList<LEGAL>(pos).size() == 0)
            ++checkmate;
        pos.undo_move(m);
    }
    else
    {
        pos.do_move(m, st, false);
        if (MoveList<LEGAL>(pos).size() == 0)
            ++stalemate;
        pos.undo_move(m);
    }

    if (m.type_of() == CASTLING)
        ++castle;

    if (m.type_of() == PROMOTION)
        ++promotion;
}

void Perft::operator+=(const Perft& perft) noexcept {
    nodes += perft.nodes;
    capture += perft.capture;
    enpassant += perft.enpassant;
    anyCheck += perft.anyCheck;
    dscCheck += perft.dscCheck;
    dblCheck += perft.dblCheck;
    castle += perft.castle;
    promotion += perft.promotion;
    checkmate += perft.checkmate;
    stalemate += perft.stalemate;
}
// void Perft::operator-=(const Perft& perft) noexcept {
//     nodes -= perft.nodes;
//     capture -= perft.capture;
//     enpassant -= perft.enpassant;
//     anyCheck -= perft.anyCheck;
//     dscCheck -= perft.dscCheck;
//     dblCheck -= perft.dblCheck;
//     castle -= perft.castle;
//     promotion -= perft.promotion;
//     checkmate -= perft.checkmate;
//     stalemate -= perft.stalemate;
// }

class PerftTable;

struct PerftEntry final {

    constexpr std::uint64_t nodes() const noexcept { return nodes64; }

    void save(Key32 k32, Depth d, std::uint64_t n) noexcept {
        if ((key32 == k32 && depth16 >= d) || nodes64 >= 10000 + n)
            return;
        key32   = k32;
        depth16 = d;
        nodes64 = n;
    }

   private:
    Key32         key32;
    Depth         depth16;
    std::uint64_t nodes64;

    friend class PerftTable;
};

static_assert(sizeof(PerftEntry) == 16, "Unexpected PerftEntry size");

constexpr inline std::uint8_t PERFT_CLUSTER_ENTRY_COUNT = 4;

struct PerftCluster final {
    PerftEntry entry[PERFT_CLUSTER_ENTRY_COUNT];
};

static_assert(sizeof(PerftCluster) == 64, "Unexpected PerftCluster size");

struct PerftProbe final {
    bool const        ptHit;
    PerftEntry* const pte;
};

class PerftTable final {

   public:
    PerftTable() noexcept                             = default;
    PerftTable(const PerftTable&) noexcept            = delete;
    PerftTable(PerftTable&&) noexcept                 = delete;
    PerftTable& operator=(const PerftTable&) noexcept = delete;
    PerftTable& operator=(PerftTable&&) noexcept      = delete;
    ~PerftTable() noexcept;

    void free() noexcept;
    void resize(std::size_t mbSize, ThreadPool& threads) noexcept;
    void init(ThreadPool& threads) noexcept;

    PerftProbe probe(Key key, Depth depth) const noexcept;

   private:
    PerftEntry* first_entry(Key key) const noexcept {
        return &clusters[mul_hi64(key, clusterCount)].entry[0];
    }

    PerftCluster* clusters     = nullptr;
    std::size_t   clusterCount = 0;
};

PerftTable::~PerftTable() noexcept { free(); }

void PerftTable::free() noexcept {
    free_aligned_lp(clusters);
    clusters     = nullptr;
    clusterCount = 0;
}

void PerftTable::resize(std::size_t mbSize, ThreadPool& threads) noexcept {
    free();
    clusterCount = mbSize * 1024 * 1024 / sizeof(PerftCluster);
    assert(clusterCount % 2 == 0);

    clusters = static_cast<PerftCluster*>(alloc_aligned_lp(clusterCount * sizeof(PerftCluster)));
    if (!clusters)
    {
        std::cerr << "Failed to allocate " << mbSize << "MB for perft table.\n";
        std::exit(EXIT_FAILURE);
    }

    init(threads);
}

// Initializes the entire perft table to zero, in a multi-threaded way.
void PerftTable::init(ThreadPool& threads) noexcept {
    const std::uint16_t threadCount = threads.size();

    for (std::uint16_t threadId = 0; threadId < threadCount; ++threadId)
    {
        threads.run_on_thread(threadId, [this, threadId, threadCount]() {
            // Each thread will zero its part of the hash table
            std::size_t stride = clusterCount / threadCount;
            std::size_t start  = stride * threadId;
            std::size_t count  = 1 + threadId != threadCount ? stride : clusterCount - start;

            std::memset(static_cast<void*>(&clusters[start]), 0, count * sizeof(PerftCluster));
        });
    }

    for (std::uint16_t threadId = 0; threadId < threadCount; ++threadId)
        threads.wait_on_thread(threadId);
}

PerftProbe PerftTable::probe(Key key, Depth depth) const noexcept {

    PerftEntry* const pte = first_entry(key);

    for (std::uint8_t i = 0; i < PERFT_CLUSTER_ENTRY_COUNT; ++i)
        // Use the low 32 bits as key inside the cluster
        if ((pte + i)->key32 == Key32(key) && (pte + i)->depth16 == depth)
            return {true, (pte + i)};

    PerftEntry* const lte = pte + PERFT_CLUSTER_ENTRY_COUNT - 1;
    PerftEntry*       rte = pte;
    for (std::uint8_t i = 1; i < PERFT_CLUSTER_ENTRY_COUNT; ++i)
        if (rte->depth16 > (pte + i)->depth16 && depth > (pte + i)->depth16)
            rte = (pte + i);

    return {false, rte->depth16 <= depth ? rte : lte};
}

PerftTable perftTable;

constexpr bool use_perft_table(Depth depth, bool detail) noexcept { return !detail && depth >= 4; }

// Utility to verify move generation.
// All the leaf nodes up to the given depth are generated and counted,
// and the sum is returned.
template<bool RootNode>
Perft perft(Position& pos, Depth depth, bool detail) noexcept {

    if (RootNode)
    {
        std::ostringstream oss;
        oss << std::left << std::setw(3) << "N" << std::setw(10) << "Move" << std::setw(19)
            << "Nodes";
        // clang-format off
        if (detail)
            oss << std::setw(17) << "Capture"
                << std::setw(15) << "Enpassant"
                << std::setw(15) << "AnyCheck"
                << std::setw(15) << "DscCheck"
                << std::setw(15) << "DblCheck"
                << std::setw(15) << "Castle"
                << std::setw(15) << "Promote"
                << std::setw(15) << "Checkmate"
                << std::setw(15) << "Stalemate";
        // clang-format on
        std::cout << oss.str() << '\n';
    }

    Perft sperft;
    for (const auto& m : MoveList<LEGAL>(pos))
    {
        Perft iperft;
        if (RootNode && depth <= 1)
        {
            ++iperft.nodes;
            if (detail)
                iperft.classify(pos, m);
        }
        else
        {
            StateInfo st;
            ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

            pos.do_move(m, st);

            if (depth <= 2)
            {
                const MoveList<LEGAL> legalMoves(pos);
                iperft.nodes += legalMoves.size();
                if (detail)
                    for (const auto& im : legalMoves)
                        iperft.classify(pos, im);
            }
            else
            {
                if (use_perft_table(depth, detail))
                {
                    Key key           = pos.key(-pos.rule50_count());
                    auto [ptHit, pte] = perftTable.probe(key, depth - 1);
                    if (ptHit)
                        iperft.nodes += pte->nodes();
                    else
                    {
                        iperft = perft<false>(pos, depth - 1, detail);
                        pte->save(Key32(key), depth - 1, iperft.nodes);
                    }
                }
                else
                    iperft = perft<false>(pos, depth - 1, detail);
            }
            pos.undo_move(m);
        }

        if (detail)
            sperft += iperft;
        else
            sperft.nodes += iperft.nodes;

        if (RootNode)
        {
            ++sperft.count;

            std::ostringstream oss;
            oss << std::right << std::setfill('0') << std::setw(2) << sperft.count << " "
                << std::left << std::setfill(' ') << std::setw(7) << UCI::move_to_san(m, pos)
                << ": " << std::right << std::setfill('.') << std::setw(16) << iperft.nodes;
            // clang-format off
            if (detail)
                oss << "   " << std::setw(14) << iperft.capture
                    << "   " << std::setw(12) << iperft.enpassant
                    << "   " << std::setw(12) << iperft.anyCheck
                    << "   " << std::setw(12) << iperft.dscCheck
                    << "   " << std::setw(12) << iperft.dblCheck
                    << "   " << std::setw(12) << iperft.castle
                    << "   " << std::setw(12) << iperft.promotion
                    << "   " << std::setw(12) << iperft.checkmate
                    << "   " << std::setw(12) << iperft.stalemate;
            // clang-format on
            std::cout << oss.str() << '\n';
        }
    }

    if (RootNode)
    {
        std::ostringstream oss;
        oss << "Total     : " << std::right << std::setfill('.') << std::setw(16) << sperft.nodes;
        // clang-format off
        if (detail)
            oss << " " << std::setw(16) << sperft.capture
                << " " << std::setw(14) << sperft.enpassant
                << " " << std::setw(14) << sperft.anyCheck
                << " " << std::setw(14) << sperft.dscCheck
                << " " << std::setw(14) << sperft.dblCheck
                << " " << std::setw(14) << sperft.castle
                << " " << std::setw(14) << sperft.promotion
                << " " << std::setw(14) << sperft.checkmate
                << " " << std::setw(14) << sperft.stalemate;
        // clang-format on
        std::cout << oss.str() << '\n';
    }

    return sperft;
}

// Explicit template instantiations
template Perft perft<true>(Position& pos, Depth depth, bool detail) noexcept;
template Perft perft<false>(Position& pos, Depth depth, bool detail) noexcept;

}  // namespace

std::uint64_t
perft(Position& pos, Depth depth, std::size_t mbSize, ThreadPool& threads, bool detail) noexcept {

    if (use_perft_table(depth, detail))
        perftTable.resize(mbSize, threads);

    std::uint64_t nodes = perft<true>(pos, depth, detail).nodes;
    sync_cout << "\nTotal Nodes : " << nodes << '\n' << sync_endl;

    if (use_perft_table(depth, detail))
        perftTable.free();

    return nodes;
}

}  // namespace DON::Benchmark
