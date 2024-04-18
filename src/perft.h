/*
  DON, a UCI chess playing engine derived from Glaurung 2.1

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

#ifndef PERFT_H_INCLUDED
#define PERFT_H_INCLUDED

#include <cstdint>
#include <iomanip>
#include <iosfwd>

#include "movegen.h"
#include "position.h"
#include "types.h"
#include "uci.h"

namespace DON {

namespace {

struct Perft final {

    void classify(Position& pos, const Move& m) noexcept;

    void operator+=(const Perft& perft) noexcept;

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

    Perft sumPerft;
    for (const auto& m : MoveList<LEGAL>(pos))
    {
        Perft iPerft;
        if (RootNode && depth <= 1)
        {
            ++iPerft.nodes;
            if (detail)
                iPerft.classify(pos, m);
        }
        else
        {
            StateInfo st;
            ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

            pos.do_move(m, st);

            if (depth <= 2)
                for (const auto& im : MoveList<LEGAL>(pos))
                {
                    ++iPerft.nodes;
                    if (detail)
                        iPerft.classify(pos, im);
                }
            else
                iPerft = perft<false>(pos, depth - 1, detail);

            pos.undo_move(m);
        }

        sumPerft += iPerft;

        if (RootNode)
        {
            ++sumPerft.count;

            std::ostringstream oss;
            oss << std::right << std::setfill('0') << std::setw(2) << sumPerft.count << " "
                << std::left << std::setfill(' ') << std::setw(7) << UCI::move_to_san(m, pos)
                << ": " << std::right << std::setfill('.') << std::setw(16) << iPerft.nodes;
            // clang-format off
            if (detail)
                oss << "   " << std::setw(14) << iPerft.capture
                    << "   " << std::setw(12) << iPerft.enpassant
                    << "   " << std::setw(12) << iPerft.anyCheck
                    << "   " << std::setw(12) << iPerft.dscCheck
                    << "   " << std::setw(12) << iPerft.dblCheck
                    << "   " << std::setw(12) << iPerft.castle
                    << "   " << std::setw(12) << iPerft.promotion
                    << "   " << std::setw(12) << iPerft.checkmate
                    << "   " << std::setw(12) << iPerft.stalemate;
            // clang-format on
            std::cout << oss.str() << '\n';
        }
    }

    if (RootNode)
    {
        std::ostringstream oss;
        oss << "Total     : " << std::right << std::setfill('.') << std::setw(16) << sumPerft.nodes;
        // clang-format off
        if (detail)
            oss << " " << std::setw(16) << sumPerft.capture
                << " " << std::setw(14) << sumPerft.enpassant
                << " " << std::setw(14) << sumPerft.anyCheck
                << " " << std::setw(14) << sumPerft.dscCheck
                << " " << std::setw(14) << sumPerft.dblCheck
                << " " << std::setw(14) << sumPerft.castle
                << " " << std::setw(14) << sumPerft.promotion
                << " " << std::setw(14) << sumPerft.checkmate
                << " " << std::setw(14) << sumPerft.stalemate;
        // clang-format on
        std::cout << oss.str() << '\n';
    }

    return sumPerft;
}

// Explicit template instantiations
template Perft perft<true>(Position& pos, Depth depth, bool detail) noexcept;
template Perft perft<false>(Position& pos, Depth depth, bool detail) noexcept;

void Perft::classify(Position& pos, const Move& m) noexcept {

    const Square org = m.org_sq(), dst = m.dst_sq();
    const Color  stm = pos.side_to_move();

    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    if (pos.capture(m))
    {
        ++capture;
        if (m.type_of() == EN_PASSANT)
            ++enpassant;
    }

    if (pos.gives_check(m))
    {
        ++anyCheck;
        if (!(pos.check_squares(m.type_of() != PROMOTION ? type_of(pos.piece_on(org))
                                                         : m.promotion_type())
              & dst))
        {
            if (pos.blockers(~stm) & org)
                ++dscCheck;
            else if (m.type_of() == EN_PASSANT)
            {
                const Bitboard occupied =
                  (pos.pieces() ^ org ^ make_square(file_of(dst), rank_of(org))) | dst;
                if ((pos.pieces(stm, BISHOP, QUEEN)
                     & attacks_bb<BISHOP>(pos.king_square(~stm), occupied))
                    | (pos.pieces(stm, ROOK, QUEEN)
                       & attacks_bb<ROOK>(pos.king_square(~stm), occupied)))
                    ++dscCheck;
            }
            //else if (m.type_of() == CASTLING && pos.check_squares(ROOK) & rook_castle_sq(stm, org, dst))
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

}  // namespace

inline void perft(Position& pos, Depth depth, bool detail = false) noexcept {

    std::uint64_t nodes = perft<true>(pos, depth, detail).nodes;
    sync_cout << "\nNodes searched: " << nodes << '\n' << sync_endl;
}

}  // namespace DON

#endif  // #ifndef PERFT_H_INCLUDED
