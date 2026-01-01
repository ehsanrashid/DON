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

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

#include "bitboard.h"
#include "memory.h"
#include "misc.h"
#include "movegen.h"
#include "position.h"
#include "thread.h"
#include "uci.h"

namespace DON::Perft {

namespace {

struct PerftData final {

    void classify(Position& pos, Move m) noexcept;

    void operator+=(const PerftData& perftData) noexcept;

    std::uint64_t nodes     = 0;
    std::uint32_t capture   = 0;
    std::uint32_t enpassant = 0;
    std::uint32_t anyCheck  = 0;
    std::uint32_t dscCheck  = 0;
    std::uint32_t dblCheck  = 0;
    std::uint32_t castle    = 0;
    std::uint32_t promotion = 0;
    std::uint32_t checkmate = 0;
    std::uint32_t stalemate = 0;
};

void PerftData::classify(Position& pos, Move m) noexcept {

    Square orgSq = m.org_sq(), dstSq = m.dst_sq();

    State st;

    castle += m.type() == MT::CASTLING;

    promotion += m.type() == MT::PROMOTION;

    if (pos.capture(m))
    {
        ++capture;

        if (m.type() == MT::EN_PASSANT)
            ++enpassant;
    }

    if (pos.check(m))
    {
        ++anyCheck;

        if ((pos.checks_bb(m.type() != MT::PROMOTION ? type_of(pos[orgSq]) : m.promotion_type())
             & dstSq)
            == 0)
        {
            Color ac = pos.active_color();

            if (pos.blockers_bb(~ac) & orgSq)
            {
                ++dscCheck;
            }
            else if (m.type() == MT::EN_PASSANT)
            {
                Bitboard occupancyBB =
                  pos.pieces_bb() ^ make_bb(orgSq, dstSq, dstSq - pawn_spush(ac));
                if (pos.slide_attackers_bb(pos.square<KING>(~ac), occupancyBB) & pos.pieces_bb(ac))
                    ++dscCheck;
            }
            //else if (m.type() == MT::CASTLING)
            //{
            //    if ((pos.checks_bb(ROOK) & rook_castle_sq(orgSq, dstSq)) != 0)
            //        ++dscCheck;
            //}
        }

        if (pos.dbl_check(m))
            ++dblCheck;

        pos.do_move(m, st, true);

        //if (more_than_one(pos.checkers_bb()))
        //    ++dblCheck;

        if (MoveList<LEGAL, true>(pos).empty())
            ++checkmate;

        pos.undo_move(m);
    }
    else
    {
        pos.do_move(m, st, false);

        if (MoveList<LEGAL, true>(pos).empty())
            ++stalemate;

        pos.undo_move(m);
    }
}

void PerftData::operator+=(const PerftData& perftData) noexcept {
    nodes += perftData.nodes;
    capture += perftData.capture;
    enpassant += perftData.enpassant;
    anyCheck += perftData.anyCheck;
    dscCheck += perftData.dscCheck;
    dblCheck += perftData.dblCheck;
    castle += perftData.castle;
    promotion += perftData.promotion;
    checkmate += perftData.checkmate;
    stalemate += perftData.stalemate;
}

struct PTEntry final {
   public:
    PTEntry() noexcept                          = default;
    PTEntry(const PTEntry&) noexcept            = delete;
    PTEntry(PTEntry&&) noexcept                 = delete;
    PTEntry& operator=(const PTEntry&) noexcept = default;
    PTEntry& operator=(PTEntry&&) noexcept      = delete;

    constexpr std::uint64_t nodes() const noexcept { return nodes64; }

    void save(std::uint32_t k, Depth d, std::uint64_t n) noexcept {

        if ((key32 != k || depth16 < d) && nodes64 < 10000 + n)
        {
            key32   = k;
            depth16 = d;
            nodes64 = n;
        }
    }

   private:
    std::uint32_t key32;
    Depth         depth16;
    std::uint64_t nodes64;

    friend class PerftTable;
};

static_assert(sizeof(PTEntry) == 16, "Unexpected PTEntry size");

struct PTCluster final {
   public:
    PTCluster() noexcept                            = default;
    PTCluster(const PTCluster&) noexcept            = delete;
    PTCluster(PTCluster&&) noexcept                 = delete;
    PTCluster& operator=(const PTCluster&) noexcept = default;
    PTCluster& operator=(PTCluster&&) noexcept      = delete;

    StdArray<PTEntry, 4> entries;
};

static_assert(sizeof(PTCluster) == 64, "Unexpected PTCluster size");

struct ProbResult final {
    bool           hit;
    PTEntry* const entry;
};

class PerftTable final {

   public:
    PerftTable() noexcept                             = default;
    PerftTable(const PerftTable&) noexcept            = delete;
    PerftTable(PerftTable&&) noexcept                 = delete;
    PerftTable& operator=(const PerftTable&) noexcept = delete;
    PerftTable& operator=(PerftTable&&) noexcept      = delete;
    ~PerftTable() noexcept;

    void resize(std::size_t mbSize, Threads& threads) noexcept;

    void init(Threads& threads) noexcept;

    PTCluster* cluster(Key key) const noexcept;

    ProbResult probe(Key key, Depth depth) const noexcept;

   private:
    void free() noexcept;

    PTCluster*  clusters = nullptr;
    std::size_t clusterCount;
};

PerftTable::~PerftTable() noexcept { free(); }

void PerftTable::free() noexcept {
    [[maybe_unused]] bool success = free_aligned_large_page(clusters);
    assert(success);
}

void PerftTable::resize(std::size_t ptSize, Threads& threads) noexcept {
    free();

    clusterCount = ptSize * 1024 * 1024 / sizeof(PTCluster);

    assert(clusterCount % 2 == 0);

    clusters = static_cast<PTCluster*>(alloc_aligned_large_page(clusterCount * sizeof(PTCluster)));

    if (clusters == nullptr)
    {
        std::cerr << "Failed to allocate " << ptSize << "MB for perft table." << std::endl;
        std::exit(EXIT_FAILURE);
    }

    init(threads);
}

// Initializes the entire perft table to zero, in a multi-threaded way.
void PerftTable::init(Threads& threads) noexcept {

    const std::size_t threadCount = threads.size();

    for (std::size_t threadId = 0; threadId < threadCount; ++threadId)
    {
        threads.run_on_thread(threadId, [this, threadId, threadCount]() {
            // Each thread will zero its part of the hash table
            std::size_t stride = clusterCount / threadCount;
            std::size_t remain = clusterCount % threadCount;

            std::size_t start = stride * threadId + std::min(threadId, remain);
            std::size_t count = stride + (threadId < remain);

            std::memset(&clusters[start], 0, count * sizeof(PTCluster));
        });
    }

    for (std::size_t threadId = 0; threadId < threadCount; ++threadId)
        threads.wait_on_thread(threadId);
}

PTCluster* PerftTable::cluster(Key key) const noexcept {
    return &clusters[mul_hi64(key, clusterCount)];
}

ProbResult PerftTable::probe(Key key, Depth depth) const noexcept {

    auto* const ptc = cluster(key);

    const std::uint32_t key32 = std::uint32_t(key);

    for (auto& entry : ptc->entries)
        if (entry.key32 == key32 && entry.depth16 == depth)
            return {true, &entry};

    auto* const fte = &ptc->entries[0];
    auto*       rte = fte;

    for (std::size_t i = 1; i < ptc->entries.size(); ++i)
        if (rte->depth16 > ptc->entries[i].depth16)
            rte = &ptc->entries[i];

    return {false, rte->depth16 <= depth ? rte : fte + ptc->entries.size() - 1};
}

PerftTable perftTable;

constexpr bool use_perft_table(Depth depth, bool detail) noexcept { return !detail && depth >= 4; }

// Utility to verify move generation.
// All the leaf nodes up to the given depth are generated and counted,
// and the sum is returned.
template<bool RootNode>
PerftData perft(Position& pos, Depth depth, bool detail) noexcept {

    if (RootNode)
    {
        std::cout << std::left                //
                  << std::setw(3) << "N"      //
                  << std::setw(10) << "Move"  //
                  << std::setw(19) << "Nodes";
        if (detail)
            std::cout << std::setw(15) << "Capture"    //
                      << std::setw(13) << "Enpassant"  //
                      << std::setw(13) << "AnyCheck"   //
                      << std::setw(13) << "DscCheck"   //
                      << std::setw(13) << "DblCheck"   //
                      << std::setw(13) << "Castle"     //
                      << std::setw(13) << "Promote"    //
                      << std::setw(13) << "Checkmate"  //
                      << std::setw(13) << "Stalemate";
        std::cout << std::endl;
    }

    std::uint16_t count = 0;

    PerftData perftData;

    for (auto m : MoveList<LEGAL>(pos))
    {
        PerftData iPerftData;

        if (RootNode && depth <= 1)
        {
            iPerftData.nodes++;

            if (detail)
                iPerftData.classify(pos, m);
        }
        else
        {
            State st;

            pos.do_move(m, st);

            if (depth <= 2)
            {
                const MoveList<LEGAL> iLegalMoves(pos);

                iPerftData.nodes += iLegalMoves.size();

                if (detail)
                    for (auto im : iLegalMoves)
                        iPerftData.classify(pos, im);
            }
            else
            {
                if (use_perft_table(depth, detail))
                {
                    Key key = pos.raw_key();

                    auto [ptHit, pte] = perftTable.probe(key, depth - 1);

                    if (ptHit)
                    {
                        iPerftData.nodes += pte->nodes();
                    }
                    else
                    {
                        iPerftData = perft<false>(pos, depth - 1, detail);

                        pte->save(std::uint32_t(key), depth - 1, iPerftData.nodes);
                    }
                }
                else
                {
                    iPerftData = perft<false>(pos, depth - 1, detail);
                }
            }

            pos.undo_move(m);
        }

        perftData += iPerftData;

        if (RootNode)
        {
            ++count;

            std::string move =
              //<< UCI::move_to_can(m)
              UCI::move_to_san(m, pos);

            std::size_t append = 10 - move.size();
            if (append != 0)
            {
                bool special = move.back() == '+' || move.back() == '#' || move.back() == '=';
                move.append(append - special, ' ');
            }

            std::cout << std::right << std::setfill('0') << std::setw(2) << count << " "  //
                      << std::left << move << ":"                                         //
                      << std::right << std::setfill('.') << std::setw(16) << iPerftData.nodes;

            if (detail)
                std::cout << "   " << std::setw(12) << iPerftData.capture    //
                          << "   " << std::setw(10) << iPerftData.enpassant  //
                          << "   " << std::setw(10) << iPerftData.anyCheck   //
                          << "   " << std::setw(10) << iPerftData.dscCheck   //
                          << "   " << std::setw(10) << iPerftData.dblCheck   //
                          << "   " << std::setw(10) << iPerftData.castle     //
                          << "   " << std::setw(10) << iPerftData.promotion  //
                          << "   " << std::setw(10) << iPerftData.checkmate  //
                          << "   " << std::setw(10) << iPerftData.stalemate;

            std::cout << std::setfill(' ') << std::left << std::endl;
        }
    }

    if (RootNode)
    {
        std::cout << "Sum         :";
        std::cout << std::right << std::setfill('.');
        std::cout << std::setw(16) << perftData.nodes;

        if (detail)
            std::cout << " " << std::setw(14) << perftData.capture    //
                      << " " << std::setw(12) << perftData.enpassant  //
                      << " " << std::setw(12) << perftData.anyCheck   //
                      << " " << std::setw(12) << perftData.dscCheck   //
                      << " " << std::setw(12) << perftData.dblCheck   //
                      << " " << std::setw(12) << perftData.castle     //
                      << " " << std::setw(12) << perftData.promotion  //
                      << " " << std::setw(12) << perftData.checkmate  //
                      << " " << std::setw(12) << perftData.stalemate;

        std::cout << std::setfill(' ') << std::left << std::endl;
    }

    return perftData;
}

// Explicit template instantiations:
template PerftData perft<false>(Position& pos, Depth depth, bool detail) noexcept;
template PerftData perft<true>(Position& pos, Depth depth, bool detail) noexcept;

}  // namespace

std::uint64_t
perft(Position& pos, std::size_t ptSize, Threads& threads, Depth depth, bool detail) noexcept {

    if (use_perft_table(depth, detail))
        perftTable.resize(ptSize, threads);

    return perft<true>(pos, depth, detail).nodes;
}

}  // namespace DON::Perft
