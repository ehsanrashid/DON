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

#include "notation.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "attacks.h"
#include "bitboard.h"
#include "position.h"

namespace DON {

// Converts a Value to a Score object, considering the position for centipawn conversion
Score::Score(Value v, const Position& pos) noexcept {
    assert(is_ok(v));

    if (!is_decisive(v))
    {
        score = Unit{to_cp(v, pos)};
    }
    else if (!is_mate(v))
    {
        constexpr int TB_CP = 20000;

        int ply = VALUE_TB - constexpr_abs(v);
        score   = Tablebase{v > 0 ? +TB_CP - ply : -TB_CP + ply};
    }
    else
    {
        int ply = VALUE_MATE - constexpr_abs(v);
        score   = Mate{(v > 0 ? ply + 1 : -ply) / 2};
    }
}

namespace {

struct WinRateParams final {
    double a, b;
};

WinRateParams win_rate_params(const Position& pos) noexcept {

    // clang-format off
    constexpr Array<double, 4> A{-72.32565836,  185.93832038, -144.58862193, 416.44950446};
    constexpr Array<double, 4> B{ 83.86794042, -136.06112997,   69.98820887,  47.62901433};
    // clang-format on

    // The fitted model only uses data for material counts in [17, 78], and is anchored at count 58
    double m = std::clamp(pos.std_material(), 17, 78) / 58.0;
    // Return a = p_a(material) and b = p_b(material).
    double a = ((A[0] * m + A[1]) * m + A[2]) * m + A[3];
    double b = ((B[0] * m + B[1]) * m + B[2]) * m + B[3];

    return {a, b};
}

// The win rate model is 1 / (1 + exp((a - eval) / b)), where a = p_a(material) and b = p_b(material)
int win_rate_model(Value v, const Position& pos) noexcept {
    assert(is_ok(v));

    auto [a, b] = win_rate_params(pos);
    // Return the win rate in per mille units, rounded to the nearest integer
    return constexpr_ceil(1000.0 / (1.0 + std::exp((a - v) / b)));
}

template<typename... Ts>
struct Overload final: Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
Overload(Ts...) -> Overload<Ts...>;

}  // namespace

// Turns a Value to an integer centipawn number,
// without treatment of mate and similar special scores.
int to_cp(Value v, const Position& pos) noexcept {
    assert(is_ok(v));
    // In general, the score can be defined via the WDL as
    // (log(1/L - 1) - log(1/W - 1)) / (log(1/L - 1) + log(1/W - 1)).
    // Based on our win_rate_model, this simply yields v / a.

    auto [a, b] = win_rate_params(pos);

    return constexpr_round(100.0 * int(v) / a);
}

FixedText to_wdl(Value v, const Position& pos) noexcept {
    assert(is_ok(v));

    int w = win_rate_model(+v, pos);
    int l = win_rate_model(-v, pos);
    int d = 1000 - (w + l);

    return FixedText{}.write(" wdl ").write(w).write(' ').write(d).write(' ').write(l);
}

FixedText to_score(const Score& score) noexcept {
    return score.visit(Overload{
      [](Score::Unit unit) -> FixedText { return FixedText{}.write("cp ").write(unit.value); },
      [](Score::Tablebase tb) -> FixedText { return FixedText{}.write("cp ").write(tb.value); },
      [](Score::Mate mate) -> FixedText { return FixedText{}.write("mate ").write(mate.value); }});
}

std::string move_to_can(const Move m) noexcept {
    if (m == Move::None)
        return "(none)";
    if (m == Move::Null)
        return "0000";

    const Square orgSq = m.org_sq();
    Square       dstSq = m.dst_sq();

    if (!Position::Chess960 && m.type() == MT::CASTLING)
    {
        assert(rank_of(orgSq) == rank_of(dstSq));
        dstSq = king_castle_sq(orgSq, dstSq);
    }

    std::string can;
    can.reserve(5);

    can  //
      .append(to_square(orgSq))
      .append(to_square(dstSq));
    if (m.type() == MT::PROMOTION)
        can.push_back(lower_case(to_char(m.promotion_type())));

    return can;
}

// Converts a string representing a move in coordinate notation
// (g1f3, a7a8q) to the corresponding legal move, if any.
Move can_to_move(std::string can, const MoveList<GenType::LEGAL>& legalMoveList) noexcept {
    assert(4 <= can.size() && can.size() <= 5);

    can = lower_case(can);

    for (const Move m : legalMoveList)
        if (can == move_to_can(m))
            return m;

    return Move::None;
}

Move can_to_move(std::string_view can, const Position& pos) noexcept {
    return can_to_move(std::string{can}, MoveList<GenType::LEGAL>(pos));
}

namespace {

enum class Ambiguity : u8 {
    NONE,    // No ambiguity
    RANK,    // Same file, different rank
    FILE,    // Same rank, different file
    SQUARE,  // Same rank and file; must specify full square
};

// Ambiguity if more then one piece of same type can reach 'dstSq' with a legal move.
// NOTE: for pawns it is not needed because 'orgSq' file is explicit.
Ambiguity detect_ambiguity(Move m, const Position& pos) noexcept {
    assert(pos.legal(m));

    const Color ac = pos.active_color();

    const Square orgSq = m.org_sq(), dstSq = m.dst_sq();
    assert(color_of(pos[orgSq]) == ac);
    const auto movedPt = type_of(pos[orgSq]);

    // Only one piece of this piece-type -> no ambiguity
    if (pos.count(ac, movedPt) == 1)
        return Ambiguity::NONE;

    // Disambiguation if have more then one piece with same destination
    Bitboard candidatesBB =
      (Attacks::attacks_bb(dstSq, movedPt, pos.pieces_bb()) & pos.pieces_bb(ac, movedPt)) ^ orgSq;

    if (candidatesBB == 0)
        return Ambiguity::NONE;

    // Remove illegal moves (e.g., blocked or pinned pieces)
    Bitboard b = candidatesBB;
    while (b != 0)
    {
        const Square oSq = pop_lsq(b);

        const Move move{oSq, dstSq};

        if (!pos.legal(move))
            candidatesBB ^= oSq;
    }

    if ((candidatesBB & file_of(orgSq)) == 0)
        return Ambiguity::RANK;

    if ((candidatesBB & rank_of(orgSq)) == 0)
        return Ambiguity::FILE;

    return Ambiguity::SQUARE;
}

}  // namespace

std::string move_to_san(const Move m, Position& pos) noexcept {
    if (m == Move::None)
        return "(none)";
    if (m == Move::Null)
        return "0000";

    assert(MoveList<GenType::LEGAL>(pos).contains(m));

    const Square orgSq = m.org_sq(), dstSq = m.dst_sq();
    assert(color_of(pos[orgSq]) == pos.active_color());

    const auto movedPt = type_of(pos[orgSq]);

    std::string san;
    san.reserve(9);

    if (m.type() == MT::CASTLING)
    {
        assert(movedPt == KING && rank_of(orgSq) == rank_of(dstSq));
        san.append(to_string(make_cs(orgSq, dstSq)));
    }
    else
    {
        // Note:: Piece letter (skip pawn as not needed because starting file is explicit)
        if (movedPt != PAWN)
        {
            san.push_back(to_char(movedPt));

            if (movedPt != KING)
            {
                // Add disambiguation when more than one piece can reach destiny with legal move.
                switch (detect_ambiguity(m, pos))
                {
                case Ambiguity::RANK :
                    san.push_back(to_char(file_of(orgSq)));
                    break;
                case Ambiguity::FILE :
                    san.push_back(to_char(rank_of(orgSq)));
                    break;
                case Ambiguity::SQUARE :
                    san.append(to_square(orgSq));
                    break;
                default :;
                }
            }
        }

        if (pos.capture(m))
        {
            if (movedPt == PAWN)
                san.push_back(to_char(file_of(orgSq)));

            san.push_back('x');
        }

        san.append(to_square(dstSq));

        if (m.type() == MT::PROMOTION)
        {
            san.push_back('=');
            san.push_back(upper_case(to_char(m.promotion_type())));
        }
    }

    State st;
    pos.do_move(m, st);

    const bool legalMoveListEmpty = MoveList<GenType::LEGAL, true>(pos).empty();

    if (pos.checkers_bb() != 0)
        san.push_back(legalMoveListEmpty ? '#' : '+');
    else if (legalMoveListEmpty)
        san.push_back('=');

    pos.undo_move(m);

    return san;
}

Move san_to_move(std::string                     san,
                 Position&                       pos,
                 const MoveList<GenType::LEGAL>& legalMoveList) noexcept {
    assert(2 <= san.size() && san.size() <= 9);

    if (san.size() >= 2 && san[1] == '-' && (san[0] == '0' || lower_case(san[0]) == 'o'))
        std::replace_if(san.begin(), san.end(), [](char c) { return c == 'o' || c == '0'; }, 'O');

    for (const Move m : legalMoveList)
        if (san == move_to_san(m, pos))
            return m;

    return Move::None;
}

Move san_to_move(std::string_view san, Position& pos) noexcept {
    return san_to_move(std::string{san}, pos, MoveList<GenType::LEGAL>(pos));
}

Move mix_to_move(std::string                     mix,
                 Position&                       pos,
                 const MoveList<GenType::LEGAL>& legalMoveList) noexcept {
    assert(2 <= mix.size() && mix.size() <= 9);

    Move m = Move::None;

    if (!legalMoveList.empty() && mix.size() >= 2)
    {
        if (mix.size() <= 3 || (mix[1] == '-' && (mix[0] == '0' || lower_case(mix[0]) == 'o')))
        {
            m = san_to_move(mix, pos, legalMoveList);
            return m;
        }

        if (mix.size() <= 5)
            m = can_to_move(mix, legalMoveList);

        if (m == Move::None && mix.size() <= 9)
            m = san_to_move(mix, pos, legalMoveList);
    }

    return m;
}

}  // namespace DON
