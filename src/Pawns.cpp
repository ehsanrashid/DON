#include "Pawns.h"

#include "BitBoard.h"
#include "Thread.h"

namespace Pawns {

    using namespace std;
    using namespace BitBoard;

    namespace {

    #define V(v) Value(v)

        // Weakness of our pawn shelter in front of the king indexed by [distance from edge][rank]
        const Value SHELTER_WEAKNESS[F_NO/2][R_NO] =
        {
            { V ( 97), V (21), V (26), V (51), V (87), V ( 89), V ( 99) },
            { V (120), V ( 0), V (28), V (76), V (88), V (103), V (104) },
            { V (101), V ( 7), V (54), V (78), V (77), V ( 92), V (101) },
            { V ( 80), V (11), V (44), V (68), V (87), V ( 90), V (119) }
        };

        enum { NO_FRIENDLY_PAWN, UNBLOCKED, BLOCKED_BY_PAWN, BLOCKED_BY_KING };
        // Danger of enemy pawns moving toward our king indexed by [type][distance from edge][rank]
        const Value STORM_DANGER[4][F_NO/2][R_NO] =
        {
            {
                { V (0), V (67), V (134), V (38), V (32) },
                { V (0), V (57), V (139), V (37), V (22) },
                { V (0), V (43), V (115), V (43), V (27) },
                { V (0), V (68), V (124), V (57), V (32) }
            },
            {
                { V (20), V (43), V (100), V (56), V (20) },
                { V (23), V (20), V ( 98), V (40), V (15) },
                { V (23), V (39), V (103), V (36), V (18) },
                { V (28), V (19), V (108), V (42), V (26) }
            },
            {
                { V (0), V (0), V ( 75), V (14), V ( 2) },
                { V (0), V (0), V (150), V (30), V ( 4) },
                { V (0), V (0), V (160), V (22), V ( 5) },
                { V (0), V (0), V (166), V (24), V (13) }
            },
            {
                { V (0), V (-283), V (-281), V (57), V (31) },
                { V (0), V (  58), V ( 141), V (39), V (18) },
                { V (0), V (  65), V ( 142), V (48), V (32) },
                { V (0), V (  60), V ( 126), V (51), V (19) }
            }
        };

        // Max bonus for king safety by pawns.
        // Corresponds to start position with all the pawns
        // in front of the king and no enemy pawn on the horizon.
        const Value KING_SAFETY_BY_PAWN = V(+258);

    #undef V

    #define S(mg, eg) mk_score(mg, eg)

        const Bitboard EXT_CENTER_bb[CLR_NO] =
        {
            (FB_bb | FC_bb | FD_bb | FE_bb | FF_bb | FG_bb) & (R2_bb | R3_bb | R4_bb | R5_bb | R6_bb),
            (FB_bb | FC_bb | FD_bb | FE_bb | FF_bb | FG_bb) & (R3_bb | R4_bb | R5_bb | R6_bb | R7_bb)
        };

        // Connected pawn bonus by [opposed][phalanx][rank] (by formula)
        Score CONNECTED[2][2][2][R_NO];

        // Doubled pawn penalty by [file]
        const Score DOUBLED[F_NO] =
        {
            S(13, 43), S(20, 48), S(23, 48), S(23, 48),
            S(23, 48), S(23, 48), S(20, 48), S(13, 43)
        };

        // Isolated pawn penalty by [opposed][file]
        const Score ISOLATED[2][F_NO] =
        {
            {
                S(37, 45), S(54, 52), S(60, 52), S(60, 52),
                S(60, 52), S(60, 52), S(54, 52), S(37, 45)
            },
            {
                S(25, 30), S(36, 35), S(40, 35), S(40, 35),
                S(40, 35), S(40, 35), S(36, 35), S(25, 30)
            }
        };

        // Backward pawn penalty by [opposed]
        const Score BACKWARD[2] = { S(67, 42), S(49, 24) };

        // Levers bonus by [rank]
        const Score LEVER[R_NO] = 
        {
            S( 0, 0), S( 0, 0), S(0, 0), S(0, 0),
            S(20,20), S(40,40), S(0, 0), S(0, 0)
        };

        const Score UNSTOPPABLE = S( 0, 20); // Bonus for unstoppable pawn going to promote
        const Score UNSUPPORTED = S(20, 10); // Penalty for unsupported pawn

        // Center bind mask
        const Bitboard CENTER_BIND_MASK[CLR_NO] =
        {
            (FD_bb | FE_bb) & (R5_bb | R6_bb | R7_bb),
            (FD_bb | FE_bb) & (R4_bb | R3_bb | R2_bb)
        };
        // Center bind bonus: Two pawns controlling the same central square
        const Score CENTER_BIND = S(16, 0);

        template<Color Own>
        inline Score evaluate (const Position &pos, Entry *e)
        {
            const Color Opp     = WHITE == Own ? BLACK  : WHITE;
            const Delta Push    = WHITE == Own ? DEL_N  : DEL_S;
            const Delta Pull    = WHITE == Own ? DEL_S  : DEL_N;
            const Delta Left    = WHITE == Own ? DEL_NW : DEL_SE;
            const Delta Right   = WHITE == Own ? DEL_NE : DEL_SW;

            const Bitboard own_pawns = pos.pieces<PAWN> (Own);
            const Bitboard opp_pawns = pos.pieces<PAWN> (Opp);

            e->pawns_attacks  [Own] = shift_del<Left> (own_pawns) | shift_del<Right> (own_pawns);
            e->blocked_pawns  [Own] = own_pawns & shift_del<Pull> (opp_pawns);
            e->passed_pawns   [Own] = U64(0);
            e->semiopen_files [Own] = 0xFF;
            e->king_sq        [Own] = SQ_NO;

            Bitboard center_pawns = own_pawns & EXT_CENTER_bb[Own];
            if (center_pawns != U64(0))
            {
                Bitboard color_pawns;
                color_pawns = center_pawns & LIHT_bb;
                e->pawns_on_sqrs[Own][WHITE] = color_pawns != U64(0) ? u08(pop_count<MAX15> (color_pawns)) : 0;
                color_pawns = center_pawns & DARK_bb;
                e->pawns_on_sqrs[Own][BLACK] = color_pawns != U64(0) ? u08(pop_count<MAX15> (color_pawns)) : 0;
            }
            else
            {
                e->pawns_on_sqrs[Own][WHITE] = 0;
                e->pawns_on_sqrs[Own][BLACK] = 0;
            }

            Score pawn_score = SCORE_ZERO;

            Bitboard b;

            const Square *pl = pos.list<PAWN> (Own);
            Square s;
            while ((s = *pl++) != SQ_NO)
            {
                assert (pos[s] == (Own | PAWN));

                File f = _file (s);
                Rank r = rel_rank (Own, s);

                e->semiopen_files[Own] &= ~(1 << f);

                Bitboard adjacents = (own_pawns & ADJ_FILE_bb[f]);
                Bitboard phalanx   = (adjacents & rank_bb (s));
                Bitboard supported = (adjacents & rank_bb (s-Push));
                Bitboard doubled   = (own_pawns & FRONT_SQRS_bb[Own][s]);
                bool     opposed   = (opp_pawns & FRONT_SQRS_bb[Own][s]);
                bool     connected = (supported | phalanx);
                bool     levered   = (opp_pawns & PAWN_ATTACKS[Own][s]);
                bool     isolated  = !(adjacents);
                bool     passed    = !(opp_pawns & PAWN_PASS_SPAN[Own][s]);

                bool backward;
                // Test for backward pawn.
                // If the pawn is passed, isolated, connected or levered (it can capture an enemy pawn).
                // If there are friendly pawns behind on adjacent files and they are able to advance and support the pawn.
                // If it is sufficiently advanced (Rank 6), then it cannot be backward either.
                if (   passed || isolated || levered || connected || r >= R_6
                   // Partially checked the opp behind pawn, But need to check own behind attack span are not backward or rammed 
                    || (own_pawns & PAWN_ATTACK_SPAN[Opp][s] && !(opp_pawns & (s-Push)))
                   )
                {
                    backward = false;
                }
                else
                {
                    // Now know there are no friendly pawns beside or behind this pawn on adjacent files.
                    // Now check whether the pawn is backward by looking in the forward direction on the
                    // adjacent files, and picking the closest pawn there.
                    b = PAWN_ATTACK_SPAN[Own][s] & pos.pieces<PAWN> ();
                    b = PAWN_ATTACK_SPAN[Own][s] & rank_bb (scan_backmost_sq (Own, b));

                    // If have an enemy pawn in the same or next rank, the pawn is
                    // backward because it cannot advance without being captured.
                    backward = opp_pawns & (b | shift_del<Push> (b));
                }

                assert (passed ^ (opposed || (opp_pawns & PAWN_ATTACK_SPAN[Own][s])));

                Score score = SCORE_ZERO;

                if (connected)
                {
                    score += CONNECTED[opposed][phalanx != 0][more_than_one (supported)][r];
                }

                if (isolated)
                {
                    score -= ISOLATED[opposed][f];
                }
                else
                {
                    if (!supported)
                    {
                        score -= UNSUPPORTED;
                    }
                    if (backward)
                    {
                        score -= BACKWARD[opposed];
                    }
                }
                
                if (levered)
                {
                    score += LEVER[r];
                }

                if (doubled)
                {
                    score -= DOUBLED[f] / dist<Rank> (s, scan_frntmost_sq (Own, doubled));
                }
                else
                // Only the frontmost passed pawn on each file is considered a true passed pawn.
                // Passed pawns will be properly scored in evaluation
                // because complete attack info needed to evaluate passed pawns.
                if (passed)
                {
                    e->passed_pawns[Own] += s;
                }

#ifndef NDEBUG
                //cout << to_string (s) << " : " << mg_value (score) << ", " << eg_value (score) << endl;
#endif
                pawn_score += score;
            }

#ifndef NDEBUG
            //cout << pretty (e->unstopped_pawns[Own]) << endl;
            //cout << "-------------" << endl;
#endif


            b = e->semiopen_files[Own] ^ 0xFF;
            e->pawn_span[Own] = b != U64(0) ? u08(scan_msq (b)) - u08(scan_lsq (b)) : 0;

            // Center binds: Two pawns controlling the same central square
            b = shift_del<Left> (own_pawns) & shift_del<Right> (own_pawns) & CENTER_BIND_MASK[Own];
            pawn_score += CENTER_BIND * pop_count<MAX15> (b);

            return pawn_score;
        }

    #undef S

    }

    template<Color Own>
    // pawn_shelter_storm() calculates shelter and storm penalties
    // for the file the king is on, as well as the two adjacent files.
    Value Entry::pawn_shelter_storm (const Position &pos, Square k_sq) const
    {
        const Color Opp = WHITE == Own ? BLACK : WHITE;

        Value value = KING_SAFETY_BY_PAWN;

        Bitboard front_pawns = pos.pieces<PAWN> () & (FRONT_RANK_bb[Own][_rank (k_sq)] | RANK_bb[_rank (k_sq)]);
        Bitboard own_front_pawns = pos.pieces (Own) & front_pawns;
        Bitboard opp_front_pawns = pos.pieces (Opp) & front_pawns;

        i32 kfc = min (max (_file (k_sq), F_B), F_G);
        for (i32 f = kfc - 1; f <= kfc + 1; ++f)
        {
            assert (F_A <= f && f <= F_H);

            Bitboard mid_pawns;
            
            mid_pawns = own_front_pawns & FILE_bb[f];
            Rank r0 = mid_pawns != U64(0) ? rel_rank (Own, scan_backmost_sq (Own, mid_pawns)) : R_1;

            mid_pawns = opp_front_pawns & FILE_bb[f];
            Rank r1 = mid_pawns != U64(0) ? rel_rank (Own, scan_frntmost_sq (Opp, mid_pawns)) : R_1;

            value -= 
                  +  SHELTER_WEAKNESS[min (f, i32(F_H) - f)][r0]
                  +  STORM_DANGER
                        [f  == _file (k_sq) && r1 == rel_rank (Own, k_sq) + 1 ? BLOCKED_BY_KING  :
                         r0 == R_1                                            ? NO_FRIENDLY_PAWN :
                         r1 == r0 + 1                                         ? BLOCKED_BY_PAWN  : UNBLOCKED]
                        [min (f, i32(F_H) - f)][r1];
        }

        return value;
    }

    template<Color Own>
    // Entry::evaluate_unstoppable_pawns<>() scores the most advanced passed pawns.
    // In case opponent has no pieces but pawns, this is somewhat
    // related to the possibility pawns are unstoppable.
    Score Entry::evaluate_unstoppable_pawns () const
    {
        return passed_pawns[Own] != U64(0) ?
                    UNSTOPPABLE * i32(rel_rank (Own, scan_frntmost_sq (Own, passed_pawns[Own]))) :
                    SCORE_ZERO;
    }

    // explicit template instantiations
    // --------------------------------
    template Value Entry::pawn_shelter_storm<WHITE> (const Position &pos, Square k_sq) const;
    template Value Entry::pawn_shelter_storm<BLACK> (const Position &pos, Square k_sq) const;

    template Score Entry::evaluate_unstoppable_pawns<WHITE> () const;
    template Score Entry::evaluate_unstoppable_pawns<BLACK> () const;
    // --------------------------------

    // probe() takes a position object as input, computes a Pawn::Entry object,
    // and returns a pointer to Pawn::Entry object.
    // The result is also stored in a hash table, so don't have
    // to recompute everything when the same pawn structure occurs again.
    Entry* probe (const Position &pos)
    {
        Key pawn_key = pos.pawn_key ();
        Entry *e     = pos.thread ()->pawn_table[pawn_key];

        if (e->pawn_key != pawn_key)
        {
            e->pawn_key   = pawn_key;
            e->pawn_score =
                + evaluate<WHITE> (pos, e)
                - evaluate<BLACK> (pos, e);
        }
        return e;
    }

    // initialize() Instead of hard-coded tables, when makes sense,
    // prefer to calculate them with a formula to reduce independent parameters
    // and to allow easier tuning and better insight.
    void initialize ()
    {
        const int SEED[R_NO] = { 0, 6, 15, 10, 57, 75, 135, 258 };

        for (i08 opposed = 0; opposed <= 1; ++opposed)
        {
            for (i08 phalanx = 0; phalanx <= 1; ++phalanx)
            {
                for (i08 apex = 0; apex <= 1; ++apex)
                {
                    for (i08 r = R_2; r < R_8; ++r)
                    {
                        i32 v = (SEED[r] + (phalanx != 0 ? (SEED[r + 1] - SEED[r]) / 2 : 0)) >> opposed;
                        v += (apex ? v / 2 : 0);
                        CONNECTED[opposed][phalanx][apex][r] = mk_score (3 * v / 2, v);
                    }
                }
            }
        }
    }

}
