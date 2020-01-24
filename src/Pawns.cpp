#include "Pawns.h"

#include <algorithm>
#include <cassert>

#include "BitBoard.h"
#include "Thread.h"

namespace Pawns {

    using namespace std;
    using namespace BitBoard;

    namespace {

        // Connected pawn bonus
        constexpr array<i32, R_NO> Connected { 0, 7, 8, 12, 29, 48, 86, 0 };

#   define S(mg, eg) make_score(mg, eg)
        // Safety of friend pawns shelter for our king by [distance from edge][rank].
        // RANK_1 is used for files where we have no pawn, or pawn is behind our king.
        constexpr array<array<Score, R_NO>, (F_NO/2)> Shelter
        {{
            { S( -6, 0), S( 81, 0), S( 93, 0), S( 58, 0), S( 39, 0), S( 18, 0), S(  25, 0), S(0, 0) },
            { S(-43, 0), S( 61, 0), S( 35, 0), S(-49, 0), S(-29, 0), S(-11, 0), S( -63, 0), S(0, 0) },
            { S(-10, 0), S( 75, 0), S( 23, 0), S( -2, 0), S( 32, 0), S(  3, 0), S( -45, 0), S(0, 0) },
            { S(-39, 0), S(-13, 0), S(-29, 0), S(-52, 0), S(-48, 0), S(-67, 0), S(-166, 0), S(0, 0) }
        }};

        // Danger of unblocked enemy pawns storm toward our king by [distance from edge][rank].
        // RANK_1 is used for files where the enemy has no pawn, or their pawn is behind our king.
        // [0][1 - 2] accommodate opponent pawn on edge (likely blocked by king)
        constexpr array<array<Score, R_NO>, (F_NO/2)> Storm
        {{
            { S( 85, 0), S(-289, 0), S(-166, 0), S( 97, 0), S( 50, 0), S( 45, 0), S( 50, 0), S(0, 0) },
            { S( 46, 0), S( -25, 0), S( 122, 0), S( 45, 0), S( 37, 0), S(-10, 0), S( 20, 0), S(0, 0) },
            { S( -6, 0), S(  51, 0), S( 168, 0), S( 34, 0), S( -2, 0), S(-22, 0), S(-14, 0), S(0, 0) },
            { S(-15, 0), S( -11, 0), S( 101, 0), S(  4, 0), S( 11, 0), S(-15, 0), S(-29, 0), S(0, 0) }
        }};

        constexpr Score BlockedStorm =   S(82, 82);

        constexpr Score Initial =        S( 5, 5);
        constexpr Score Backward =       S( 9,24);
        constexpr Score Isolated =       S( 5,15);
        constexpr Score Unopposed =      S(13,27);
        constexpr Score WeakDoubled =    S(11,56);
        constexpr Score WeakTwiceLever = S( 0,56);

#   undef S


    }

    /// Entry::evaluate_safety() calculates shelter & storm for a king,
    /// looking at the king file and the two closest files.
    template<Color Own>
    Score Entry::evaluate_safety(const Position &pos, Square own_k_sq) const
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

        Bitboard front_pawns = ~front_rank_bb(Opp, own_k_sq) & pos.pieces(PAWN);
        Bitboard own_front_pawns = pos.pieces(Own) & front_pawns;
        Bitboard opp_front_pawns = pos.pieces(Opp) & front_pawns;

        Score safety = Initial;

        auto kf = ::clamp(_file(own_k_sq), F_B, F_G);
        for (const auto f : { kf - File(1), kf, kf + File(1) })
        {
            assert(F_A <= f && f <= F_H);
            Bitboard own_front_f_pawns = own_front_pawns & file_bb(f);
            auto own_r = 0 != own_front_f_pawns ?
                        rel_rank(Own, scan_frontmost_sq(Opp, own_front_f_pawns)) : R_1;
            Bitboard opp_front_f_pawns = opp_front_pawns & file_bb(f);
            auto opp_r = 0 != opp_front_f_pawns ?
                        rel_rank(Own, scan_frontmost_sq(Opp, opp_front_f_pawns)) : R_1;
            assert((own_r != opp_r)
                || (R_1 == own_r
                 && R_1 == opp_r));

            auto ff = map_file(f);
            assert(F_E > ff);

            safety += Shelter[ff][own_r];
            if (   R_1 != own_r
                && (own_r + 1) == opp_r)
            {
                safety -= BlockedStorm * (R_3 == opp_r);
            }
            else
            {
                safety -= Storm[ff][opp_r];
            }
        }

        return safety;
    }
    // Explicit template instantiations
    template Score Entry::evaluate_safety<WHITE>(const Position&, Square) const;
    template Score Entry::evaluate_safety<BLACK>(const Position&, Square) const;

    template<Color Own>
    void Entry::evaluate(const Position &pos)
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;
        constexpr auto Push = pawn_push(Own);
        const auto Attack = PawnAttacks[Own];

        Bitboard pawns = pos.pieces(PAWN);
        Bitboard own_pawns = pos.pieces(Own) & pawns;
        Bitboard opp_pawns = pos.pieces(Opp) & pawns;

        Bitboard opp_pawn_dbl_att = pawn_dbl_attacks_bb(Opp, opp_pawns);

        attack_span[Own] = pawn_sgl_attacks_bb(Own, own_pawns);
        passers[Own] = 0;

        index[Own] = 0;
        king_square[Own].fill(SQ_NO);
        king_pawn_dist[Own].fill(0);
        king_safety[Own].fill(SCORE_ZERO);

        king_safety_on<Own>(pos, rel_sq(Own, SQ_G1));
        king_safety_on<Own>(pos, rel_sq(Own, SQ_C1));

        // Unsupported enemy pawns attacked twice by friend pawns
        Score score = SCORE_ZERO;

        for (const auto s : pos.squares[Own|PAWN])
        {
            assert((Own|PAWN) == pos[s]);

            auto r = rel_rank(Own, s);

            Bitboard neighbours = own_pawns & adj_file_bb(s);
            Bitboard supporters = neighbours & rank_bb(s - Push);
            Bitboard phalanxes  = neighbours & rank_bb(s);
            Bitboard stoppers   = opp_pawns & pawn_pass_span(Own, s);
            Bitboard levers     = opp_pawns & Attack[s];
            Bitboard escapes    = opp_pawns & Attack[s + Push]; // Push levers
            Bitboard opposers   = opp_pawns & front_squares_bb(Own, s);
            Bitboard blockers   = opp_pawns & (s + Push);

            bool doubled    = contains(own_pawns, s - Push);
            // Backward: A pawn is backward when it is behind all pawns of the same color
            // on the adjacent files and cannot be safely advanced.
            bool backward   = 0 == (neighbours & front_rank_bb(Opp, s + Push))
                           && 0 != (escapes | blockers);

            // Compute additional span if pawn is not backward nor blocked
            if (   !backward
                && 0 == blockers)
            {
                attack_span[Own] |= pawn_attack_span(Own, s);
            }

            // A pawn is passed if one of the three following conditions is true:
            // - there is no stoppers except the levers
            // - there is no stoppers except the escapes, but we outnumber them
            // - there is only one front stopper which can be levered.
            // Passed pawns will be properly scored later in evaluation when we have full attack info.
            if (   (stoppers == levers)
                || (   stoppers == (levers | escapes)
                    && pop_count(phalanxes) >= pop_count(escapes))
                || (   R_4 < r
                    && stoppers == blockers
                    && (  pawn_sgl_pushes_bb(Own, supporters)
                        & ~(opp_pawns | opp_pawn_dbl_att)) != 0))
            {
                passers[Own] |= s;
            }

            if (   0 != supporters
                || 0 != phalanxes)
            {
                i32 v = Connected[r] * (2 + (0 != phalanxes) - (0 != opposers))
                      + 21 * pop_count(supporters);
                score += make_score(v, v * (r - R_3) / 4);
            }
            else
            if (0 == neighbours)
            {
                score -= Isolated
                       + Unopposed * (0 == opposers);
            }
            else
            if (backward)
            {
                score -= Backward
                       + Unopposed * (0 == opposers);
            }

            if (0 == supporters)
            {
                score -= WeakDoubled * doubled
                        // Attacked twice by enemy pawns
                       + WeakTwiceLever * more_than_one(levers);
            }
        }

        scores[Own] = score;
    }
    // Explicit template instantiations
    template void Entry::evaluate<WHITE>(const Position&);
    template void Entry::evaluate<BLACK>(const Position&);

    /// Pawns::probe() looks up a current position's pawn configuration in the pawn hash table
    /// and returns a pointer to it if found, otherwise a new Entry is computed and stored there.
    Entry* probe(const Position &pos)
    {
        auto *e = pos.thread->pawn_table[pos.si->pawn_key];

        if (e->key == pos.si->pawn_key)
        {
            return e;
        }

        e->key = pos.si->pawn_key;
        e->evaluate<WHITE>(pos),
        e->evaluate<BLACK>(pos);

        return e;
    }
}
