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

#include <cassert>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <tuple>

#include "bitboard.h"
#include "memory.h"
#include "misc.h"
#include "movegen.h"
#include "position.h"
#include "thread.h"
#include "uci.h"

namespace DON::Benchmark {

namespace {

struct Perft final {

    void classify(Position& pos, const Move& m) noexcept;

    void operator+=(const Perft& perft) noexcept;
    //void operator-=(const Perft& perft) noexcept;

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

void Perft::classify(Position& pos, const Move& m) noexcept {

    Square org = m.org_sq(), dst = m.dst_sq();

    State st;

    castle += m.type_of() == CASTLING;
    promotion += m.type_of() == PROMOTION;

    if (pos.capture(m))
    {
        capture += 1;
        enpassant += m.type_of() == EN_PASSANT;
    }

    if (pos.check(m))
    {
        anyCheck += 1;

        if (!(pos.checks(m.type_of() != PROMOTION ? type_of(pos.piece_on(org))  //
                                                  : m.promotion_type())
              & dst))
        {
            Color ac = pos.active_color();

            if (pos.blockers(~ac) & org)
                dscCheck += 1;
            else if (m.type_of() == EN_PASSANT)
            {
                Bitboard occupied = pos.pieces() ^ make_bitboard(org, dst, dst - pawn_spush(ac));
                dscCheck +=
                  !!(pos.slide_attackers_to(pos.king_square(~ac), occupied) & pos.pieces(ac));
            }
            //else if (m.type_of() == CASTLING)
            //    dscCheck += !!(pos.checks(ROOK) & rook_castle_sq(ac, org, dst));
        }

        dblCheck += pos.dbl_check(m);

        pos.do_move(m, st, true);
        //dblCheck += more_than_one(pos.checkers());
        checkmate += MoveList<LEGAL, true>(pos).empty();
    }
    else
    {
        pos.do_move(m, st, false);
        stalemate += MoveList<LEGAL, true>(pos).empty();
    }
    pos.undo_move(m);
}

// clang-format off

void Perft::operator+=(const Perft& perft) noexcept {
    nodes     += perft.nodes;
    capture   += perft.capture;
    enpassant += perft.enpassant;
    anyCheck  += perft.anyCheck;
    dscCheck  += perft.dscCheck;
    dblCheck  += perft.dblCheck;
    castle    += perft.castle;
    promotion += perft.promotion;
    checkmate += perft.checkmate;
    stalemate += perft.stalemate;
}
// void Perft::operator-=(const Perft& perft) noexcept {
//     nodes     -= perft.nodes;
//     capture   -= perft.capture;
//     enpassant -= perft.enpassant;
//     anyCheck  -= perft.anyCheck;
//     dscCheck  -= perft.dscCheck;
//     dblCheck  -= perft.dblCheck;
//     castle    -= perft.castle;
//     promotion -= perft.promotion;
//     checkmate -= perft.checkmate;
//     stalemate -= perft.stalemate;
// }

// clang-format on

class PerftTable;  // IWYU pragma: keep

struct PTEntry final {

    constexpr std::uint64_t nodes() const noexcept { return nodes64; }

    void save(Key k, Depth d, std::uint64_t n) noexcept {
        Key32 k32 = k & 0xFFFFFFFF;
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

static_assert(sizeof(PTEntry) == 16, "Unexpected PTEntry size");

struct PTCluster final {
   public:
    static constexpr std::uint8_t EntryCount = 4;

    PTEntry entry[EntryCount];
};

static_assert(sizeof(PTCluster) == 64, "Unexpected PTCluster size");

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

    std::tuple<bool, PTEntry* const> probe(Key key, Depth depth) const noexcept;

   private:
    auto* cluster(Key key) const noexcept { return &clusters[mul_hi64(key, clusterCount)]; }

    PTCluster*  clusters     = nullptr;
    std::size_t clusterCount = 0;
};

PerftTable::~PerftTable() noexcept { free(); }

void PerftTable::free() noexcept {
    free_aligned_lp(clusters);
    clusters     = nullptr;
    clusterCount = 0;
}

void PerftTable::resize(std::size_t ptSize, ThreadPool& threads) noexcept {

    std::size_t newClusterCount = ptSize * 1024 * 1024 / sizeof(PTCluster);
    assert(newClusterCount % 2 == 0);

    if (clusterCount != newClusterCount)
    {
        free();

        clusterCount = newClusterCount;

        clusters = static_cast<PTCluster*>(alloc_aligned_lp(clusterCount * sizeof(PTCluster)));
        if (clusters == nullptr)
        {
            clusterCount = 0;
            std::cerr << "Failed to allocate " << ptSize << "MB for perft table." << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }

    init(threads);
}

// Initializes the entire perft table to zero, in a multi-threaded way.
void PerftTable::init(ThreadPool& threads) noexcept {

    std::size_t threadCount = threads.size();

    for (std::size_t threadId = 0; threadId < threadCount; ++threadId)
    {
        threads.run_on_thread(threadId, [this, threadId, threadCount]() {
            // Each thread will zero its part of the hash table
            std::size_t stride = clusterCount / threadCount;
            std::size_t remain = clusterCount % threadCount;

            std::size_t start = stride * threadId + std::min(threadId, remain);
            std::size_t count = stride + (threadId < remain);

            std::memset(static_cast<void*>(&clusters[start]), 0, count * sizeof(PTCluster));
        });
    }

    for (std::size_t threadId = 0; threadId < threadCount; ++threadId)
        threads.wait_on_thread(threadId);
}

std::tuple<bool, PTEntry* const> PerftTable::probe(Key key, Depth depth) const noexcept {

    auto* const ptc = cluster(key);
    auto* const fte = &ptc->entry[0];

    Key32 key32 = Key32(key);
    for (auto& entry : ptc->entry)
        if (entry.key32 == key32 && entry.depth16 == depth)
            return {true, &entry};

    auto* rte = fte;
    for (auto& entry : ptc->entry)
        if (rte->depth16 > entry.depth16)
            rte = &entry;

    return {false, rte->depth16 <= depth ? rte : fte + PTCluster::EntryCount - 1};
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
        std::cout << std::left                //
                  << std::setw(3) << "N"      //
                  << std::setw(10) << "Move"  //
                  << std::setw(19) << "Nodes";
        if (detail)
            std::cout << std::setw(17) << "Capture"    //
                      << std::setw(15) << "Enpassant"  //
                      << std::setw(15) << "AnyCheck"   //
                      << std::setw(15) << "DscCheck"   //
                      << std::setw(15) << "DblCheck"   //
                      << std::setw(15) << "Castle"     //
                      << std::setw(15) << "Promote"    //
                      << std::setw(15) << "Checkmate"  //
                      << std::setw(13) << "Stalemate";
        std::cout << std::endl;
    }

    [[maybe_unused]] std::uint16_t count = 0;

    Perft sPerft;

    for (const Move& m : MoveList<LEGAL>(pos))
    {
        Perft iPerft;
        if (RootNode && depth <= 1)
        {
            iPerft.nodes++;
            if (detail)
                iPerft.classify(pos, m);
        }
        else
        {
            State st;

            pos.do_move(m, st);

            if (depth <= 2)
            {
                const MoveList<LEGAL> iLegalMoveList(pos);
                iPerft.nodes += iLegalMoveList.size();
                if (detail)
                    for (const Move& im : iLegalMoveList)
                        iPerft.classify(pos, im);
            }
            else
            {
                if (use_perft_table(depth, detail))
                {
                    Key key           = pos.key(-pos.rule50_count());
                    auto [ptHit, pte] = perftTable.probe(key, depth - 1);
                    if (ptHit)
                        iPerft.nodes += pte->nodes();
                    else
                    {
                        iPerft = perft<false>(pos, depth - 1, detail);
                        pte->save(key, depth - 1, iPerft.nodes);
                    }
                }
                else
                    iPerft = perft<false>(pos, depth - 1, detail);
            }
            pos.undo_move(m);
        }

        sPerft += iPerft;

        if (RootNode)
        {
            ++count;

            std::cout << std::right << std::setfill('0')                  //
                      << std::setw(2) << count                            //
                      << std::left << std::setfill(' ')                   //
                      << " " << std::setw(7) << UCI::move_to_san(m, pos)  //
                      << std::right << std::setfill('.')                  //
                      << ": " << std::setw(16) << iPerft.nodes;
            if (detail)
                std::cout << "   " << std::setw(14) << iPerft.capture    //
                          << "   " << std::setw(12) << iPerft.enpassant  //
                          << "   " << std::setw(12) << iPerft.anyCheck   //
                          << "   " << std::setw(12) << iPerft.dscCheck   //
                          << "   " << std::setw(12) << iPerft.dblCheck   //
                          << "   " << std::setw(12) << iPerft.castle     //
                          << "   " << std::setw(12) << iPerft.promotion  //
                          << "   " << std::setw(12) << iPerft.checkmate  //
                          << "   " << std::setw(10) << iPerft.stalemate;
            std::cout << std::endl;
        }
    }

    if (RootNode)
    {
        std::cout << std::right << std::setfill('.');
        std::cout << "Total     : " << std::setw(16) << sPerft.nodes;
        if (detail)
            std::cout << " " << std::setw(16) << sPerft.capture    //
                      << " " << std::setw(14) << sPerft.enpassant  //
                      << " " << std::setw(14) << sPerft.anyCheck   //
                      << " " << std::setw(14) << sPerft.dscCheck   //
                      << " " << std::setw(14) << sPerft.dblCheck   //
                      << " " << std::setw(14) << sPerft.castle     //
                      << " " << std::setw(14) << sPerft.promotion  //
                      << " " << std::setw(14) << sPerft.checkmate  //
                      << " " << std::setw(12) << sPerft.stalemate;
        std::cout << std::setfill(' ') << std::endl;
    }

    return sPerft;
}

// Explicit template instantiations
template Perft perft<true>(Position& pos, Depth depth, bool detail) noexcept;
template Perft perft<false>(Position& pos, Depth depth, bool detail) noexcept;

}  // namespace

std::uint64_t
perft(Position& pos, std::size_t ptSize, ThreadPool& threads, Depth depth, bool detail) noexcept {

    if (use_perft_table(depth, detail))
        perftTable.resize(ptSize, threads);

    auto nodes = perft<true>(pos, depth, detail).nodes;
    std::cout << "\nTotal nodes: " << nodes << '\n' << std::endl;

    if (use_perft_table(depth, detail))
        perftTable.free();

    return nodes;
}

}  // namespace DON::Benchmark
