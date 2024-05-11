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

#include "movepick.h"

#include <cassert>
#include <iterator>
#include <utility>

#include "bitboard.h"
#include "position.h"

namespace DON {

// Constructors of the MovePicker class. As arguments, pass information
// to help it return the (presumably) good moves first, to decide which
// moves to return (in the quiescence search, for instance, only want to
// search captures, promotions, and some checks) and how important a good
// move ordering is at the current node.

// MovePicker constructor for the main search
MovePicker::MovePicker(const Position&               p,
                       Move                          ttm,
                       Depth                         d,
                       const ButterflyHistory*       mh,
                       const CapturePieceDstHistory* cph,
                       const PieceDstHistory**       ch,
                       const PawnHistory*            ph,
                       std::array<Move, 2>           km,
                       Move                          cm) noexcept :
    pos(p),
    mainHistory(mh),
    captureHistory(cph),
    continuationHistory(ch),
    pawnHistory(ph),
    ttMove(ttm),
    depth(d),
    refutations{{km[0], 0}, {km[1], 0}, {cm, 0}} {
    assert(depth > DEPTH_ZERO);
    assert(!ttMove || pos.pseudo_legal(ttMove));

    stage = (pos.checkers() ? EVASION_TT : MAIN_TT) + !ttMove;
}

// Constructor for quiescence search
MovePicker::MovePicker(const Position&               p,
                       Move                          ttm,
                       Depth                         d,
                       const ButterflyHistory*       mh,
                       const CapturePieceDstHistory* cph,
                       const PieceDstHistory**       ch,
                       const PawnHistory*            ph) noexcept :
    pos(p),
    mainHistory(mh),
    captureHistory(cph),
    continuationHistory(ch),
    pawnHistory(ph),
    ttMove(ttm),
    depth(d) {
    assert(depth <= DEPTH_ZERO);
    assert(!ttMove || pos.pseudo_legal(ttMove));

    stage = (pos.checkers() ? EVASION_TT : QSEARCH_TT) + !ttMove;
}

// Constructor for ProbCut: generate captures with SEE greater
// than or equal to the given threshold.
MovePicker::MovePicker(const Position&               p,
                       Move                          ttm,
                       int                           th,
                       const CapturePieceDstHistory* cph) noexcept :
    pos(p),
    captureHistory(cph),
    ttMove(ttm),
    threshold(th) {
    assert(!pos.checkers());
    assert(!ttMove || pos.pseudo_legal(ttMove));

    stage = PROBCUT_TT + !(ttMove && pos.capture_stage(ttMove) && pos.see_ge(ttMove, threshold));
}

// Assigns a numerical value to each move in a list, used
// for sorting. Captures are ordered by Most Valuable Victim (MVV), preferring
// captures with a good history. Quiets moves are ordered using the history tables.
template<GenType GT>
void MovePicker::score() noexcept {

    static_assert(GT == CAPTURES || GT == QUIETS || GT == EVASIONS, "Wrong type");

    Color stm  = pos.side_to_move();
    Color xstm = ~stm;

    for (auto& m : *this)
    {
        const Square org = m.org_sq(), dst = m.dst_sq();

        const Piece     pc = pos.piece_on(org);
        const PieceType pt = type_of(pc);

        if constexpr (GT == CAPTURES)
        {
            Piece capturedPiece = pos.captured_piece(m);

            m.value = 7 * PieceValue[capturedPiece] - pt
                    + (*captureHistory)[pc][dst][type_of(capturedPiece)]
                    + (pos.cap_square() == dst) * 256;
        }

        else if constexpr (GT == QUIETS)
        {
            // Histories
            m.value = 2 * (*mainHistory)[stm][m.org_dst()]
                    + 2 * (*pawnHistory)[pawn_index(pos.pawn_key())][pc][dst]
                    + 2 * (*continuationHistory[0])[pc][dst]  //
                    + (*continuationHistory[1])[pc][dst]      //
                    + (*continuationHistory[2])[pc][dst] / 4  //
                    + (*continuationHistory[3])[pc][dst]      //
                    + (*continuationHistory[5])[pc][dst];

            // Bonus for checks
            m.value += pos.gives_check(m) * 16384;

            // Bonus for escaping from capture
            m.value += (pos.threatens(stm) & org)
                       ? (pt == QUEEN && !(pos.attacks(xstm, ROOK) & dst)   ? 51700
                          : pt == ROOK && !(pos.attacks(xstm, MINOR) & dst) ? 25600
                          : !(pos.attacks(xstm, PAWN) & dst)                ? 14450
                                                                            : 0)
                       : 0;

            // Malus for putting piece en-prise
            m.value -= !(pos.threatens(stm) & org)
                       ? (pt == QUEEN ? bool(pos.attacks(xstm, ROOK) & dst) * 48150
                                          + bool(pos.attacks(xstm, MINOR) & dst) * 10650
                          : pt == ROOK ? bool(pos.attacks(xstm, MINOR) & dst) * 24335
                          : pt != PAWN ? bool(pos.attacks(xstm, PAWN) & dst) * 14950
                                       : 0)
                       : 0;
        }

        else  // GT == EVASIONS
        {
            if (pos.capture(m))
                m.value = PieceValue[pos.captured_piece(m)] - pt + (1 << 28);
            else
                m.value = (*mainHistory)[stm][m.org_dst()]    //
                        + (*continuationHistory[0])[pc][dst]  //
                        + (*pawnHistory)[pawn_index(pos.pawn_key())][pc][dst];
        }
    }
}

// Sort moves in descending order up to and including a given limit.
// The order of moves smaller than the limit is left unspecified.
void MovePicker::partial_sort(int limit) noexcept {

    for (ExtMove *endSorted = cur, *p = cur + 1; p < endMoves; ++p)
        if (p->value >= limit)
        {
            ExtMove tmp = *p, *q;
            *p          = *++endSorted;
            for (q = endSorted; q != cur && *(q - 1) < tmp; --q)
                *q = *(q - 1);
            *q = tmp;
        }
}

// Returns the next move satisfying a filter function.
// It never returns the TT move.
template<bool PickBest, typename Predicate>
Move MovePicker::pick(Predicate filter) noexcept {

    while (cur != endMoves)
    {
        if constexpr (PickBest)
            std::swap(*cur, *std::max_element(cur, endMoves));

        if (*cur != ttMove && filter())
            return *cur++;

        ++cur;
    }
    return Move::None();
}

// Most important method of the MovePicker class.
// It returns a new pseudo-legal move every time it is called until there are no more
// moves left, picking the move with the highest score from a list of generated moves.
Move MovePicker::next_move() noexcept {

    auto quiet_threshold = [](Depth d) noexcept -> int { return -3560 * d; };

top:
    switch (stage)
    {
    case MAIN_TT :
    case EVASION_TT :
    case QSEARCH_TT :
    case PROBCUT_TT :
        ++stage;
        return ttMove;

    case CAPTURE_INIT :
    case PROBCUT_INIT :
    case QCAPTURE_INIT :
        cur = endBadCaptures = moves;
        endMoves             = generate<CAPTURES>(pos, cur);

        score<CAPTURES>();
        partial_sort(std::numeric_limits<int>::min());

        ++stage;
        goto top;

    case CAPTURE_GOOD :
        if (pick([this]() noexcept -> bool {
                // Move losing capture to endBadCaptures to be tried later
                return pos.see_ge(*cur, -cur->value / 18) ? true
                                                          : (*endBadCaptures++ = *cur, false);
            }))
            return *(cur - 1);

        // Prepare the pointers to loop over the refutations array
        cur      = std::begin(refutations);
        endMoves = std::end(refutations);

        // If the counterMove is the same as a killerMoves, skip it
        if (refutations[2]
            && (refutations[2] == refutations[0] || refutations[2] == refutations[1]))
            refutations[2] = Move::None();

        std::replace_if(
          cur, endMoves,
          [&pos = pos](Move m) noexcept -> bool {
              return m && (pos.capture_stage(m) || !pos.pseudo_legal(m));
          },
          Move::None());

        ++stage;
        [[fallthrough]];

    case REFUTATION :
        if (pick([this]() noexcept -> bool { return bool(*cur); }))
            return *(cur - 1);

        ++stage;
        [[fallthrough]];

    case QUIET_INIT :
        if (pickQuiets)
        {
            cur      = endBadCaptures;
            endMoves = beginBadQuiets = endBadQuiets = generate<QUIETS>(pos, cur);

            score<QUIETS>();
            partial_sort(quiet_threshold(depth));
        }

        ++stage;
        [[fallthrough]];

    case QUIET_GOOD :
        if (pickQuiets && pick([this]() noexcept -> bool {
                return std::find(std::begin(refutations), std::end(refutations), *cur)
                    == std::end(refutations);
            }))
        {
            if ((cur - 1)->value > -7998 || (cur - 1)->value <= quiet_threshold(depth))
                return *(cur - 1);

            // Remaining quiets are bad
            beginBadQuiets = cur - 1;
        }

        // Prepare the pointers to loop over the bad captures
        cur      = moves;
        endMoves = endBadCaptures;

        ++stage;
        [[fallthrough]];

    case CAPTURE_BAD :
        if (pick([]() noexcept -> bool { return true; }))
            return *(cur - 1);

        // Prepare the pointers to loop over the bad quiets
        cur      = beginBadQuiets;
        endMoves = endBadQuiets;

        ++stage;
        [[fallthrough]];

    case QUIET_BAD :
        if (pickQuiets)
            return pick([this]() noexcept -> bool {
                return std::find(std::begin(refutations), std::end(refutations), *cur)
                    == std::end(refutations);
            });

        return Move::None();

    case EVASION_INIT :
        cur      = moves;
        endMoves = generate<EVASIONS>(pos, cur);

        score<EVASIONS>();

        ++stage;
        [[fallthrough]];

    case EVASION :
        return pick<true>([]() noexcept -> bool { return true; });

    case PROBCUT :
        return pick([this]() noexcept -> bool { return pos.see_ge(*cur, threshold); });

    case QCAPTURE :
        if (pick([]() noexcept -> bool { return true; }))
            return *(cur - 1);

        // If did not find any move and the depth is too low to try checks, then finished
        if (depth <= DEPTH_QS_NORMAL)
            return Move::None();

        ++stage;
        [[fallthrough]];

    case QCHECK_INIT :
        cur      = moves;
        endMoves = generate<QUIET_CHECKS>(pos, cur);

        ++stage;
        [[fallthrough]];

    case QCHECK :
        return pick([]() noexcept -> bool { return true; });

    case NO_STAGE :;
    }
    assert(false);
    return Move::None();  // Silence warning
}

}  // namespace DON
