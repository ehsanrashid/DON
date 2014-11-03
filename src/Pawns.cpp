#include "Pawns.h"

namespace Pawns {

    using namespace std;
    using namespace BitBoard;

    namespace {

    #define V Value

        // Weakness of our pawn shelter in front of the king indexed by [rank]
        const Value SHELTER_WEAKNESS[R_NO] =
        {
            V(+100), V(+  0), V(+ 27), V(+ 73), V(+ 92), V(+101), V(+101), V(+  0)
        };

        // Danger of enemy pawns moving toward our king indexed by
        // [no friendly pawn | pawn unblocked | pawn blocked][rank of enemy pawn]
        const Value STORM_DANGER[3][R_NO] =
        {
            { V(+ 0),  V(+64), V(+128), V(+51), V(+26),  V(+ 0),  V(+ 0),  V(+ 0) },
            { V(+26),  V(+32), V(+ 96), V(+38), V(+20),  V(+ 0),  V(+ 0),  V(+ 0) },
            { V(+ 0),  V(+ 0), V(+160), V(+25), V(+13),  V(+ 0),  V(+ 0),  V(+ 0) }
        };

        // Max bonus for king safety by pawns.
        // Corresponds to start position with all the pawns
        // in front of the king and no enemy pawn on the horizon.
        const Value KING_SAFETY_BY_PAWN = V(+263);

    #undef V

    #define S(mg, eg) mk_score(mg, eg)

        const i16 SEED[R_NO] = { 0, 6, 15, 10, 57, 75, 135, 258 };

        // Connected pawn bonus by [opposed][phalanx][rank] (by formula)
        Score CONNECTED[2][2][R_NO];

        // Doubled pawn penalty by [file]
        const Score DOUBLED[F_NO] =
        {
            S(+13,+43), S(+20,+48), S(+23,+48), S(+23,+48),
            S(+23,+48), S(+23,+48), S(+20,+48), S(+13,+43)
        };

        // Isolated pawn penalty by [opposed][file]
        const Score ISOLATED[2][F_NO] =
        {
            {S(+37,+45), S(+54,+52), S(+60,+52), S(+60,+52),
            S(+60,+52), S(+60,+52), S(+54,+52), S(+37,+45)},
            {S(+25,+30), S(+36,+35), S(+40,+35), S(+40,+35),
            S(+40,+35), S(+40,+35), S(+36,+35), S(+25,+30)}
        };

        // Backward pawn penalty by [opposed][file]
        const Score BACKWARD[2][F_NO] =
        {
            {S(+30,+42), S(+43,+46), S(+49,+46), S(+49,+46),
            S(+49,+46), S(+49,+46), S(+43,+46), S(+30,+42)},
            {S(+20,+28), S(+29,+31), S(+33,+31), S(+33,+31),
            S(+33,+31), S(+33,+31), S(+29,+31), S(+20,+28)}
        };

        // Levers bonus by [rank]
        const Score LEVER[R_NO] = 
        {
            S(+ 0,+ 0), S(+ 0,+ 0), S(+ 6,+ 6), S(+12,+12),
            S(+20,+20), S(+40,+40), S(+ 0,+ 0), S(+ 0,+ 0)
        };

        const Score UNSTOPPABLE = S(+ 0,+20); // Bonus for unstoppable pawn going to promote
        const Score UNSUPPORTED = S(+20,+10); // Penalty for unsupported pawn

        template<Color Own>
        inline Score evaluate (const Position &pos, Entry *e)
        {
            const Color  Opp = WHITE == Own ? BLACK  : WHITE;
            const Delta PUSH = WHITE == Own ? DEL_N  : DEL_S;

            const Bitboard own_pawns = pos.pieces<PAWN> (Own);
            const Bitboard opp_pawns = pos.pieces<PAWN> (Opp);

            e->pawns_attacks  [Own] = shift_del<WHITE == Own ? DEL_NE : DEL_SW> (own_pawns) |
                                      shift_del<WHITE == Own ? DEL_NW : DEL_SE> (own_pawns);
            //e->blocked_pawns  [Own] = own_pawns & shift_del<WHITE == C ? DEL_S : DEL_N> (opp_pawns);
            e->passed_pawns   [Own] = U64(0);
            e->semiopen_files [Own] = 0xFF;
            e->king_sq        [Own] = SQ_NO;

            Bitboard center_pawns = own_pawns & EXT_CENTER_bb[Own];
            if (center_pawns != U64(0))
            {
                Bitboard color_pawns;
                color_pawns = center_pawns & LIHT_bb;
                e->pawns_on_sqrs[Own][WHITE] = color_pawns != U64(0) ? pop_count<MAX15>(color_pawns) : 0;
                color_pawns = center_pawns & DARK_bb;
                e->pawns_on_sqrs[Own][BLACK] = color_pawns != U64(0) ? pop_count<MAX15>(color_pawns) : 0;
            }
            else
            {
                e->pawns_on_sqrs[Own][WHITE] = 0;
                e->pawns_on_sqrs[Own][BLACK] = 0;
            }

            Score pawn_score = SCORE_ZERO;

            const Square *pl = pos.list<PAWN> (Own);
            Square s;
            while ((s = *pl++) != SQ_NO)
            {
                assert (pos[s] == (Own | PAWN));

                File f = _file (s);
                Rank r = rel_rank (Own, s);
                
                e->semiopen_files[Own] &= ~(1 << f);

                Bitboard prank_bb  = rank_bb (s - PUSH);

                Bitboard adjacents = (own_pawns & ADJ_FILE_bb[f]);
                Bitboard supported = (adjacents & prank_bb);
                Bitboard connected = (adjacents & (prank_bb | rank_bb (s)));
                Bitboard doubled   = (own_pawns & FRONT_SQRS_bb[Own][s]);

                bool phalanx       = (connected & rank_bb (s));
                bool levered       = (opp_pawns & PAWN_ATTACKS[Own][s]);
                bool opposed       = (opp_pawns & FRONT_SQRS_bb[Own][s]);
                bool isolated      = !(adjacents);
                bool passed        = !(opp_pawns & PAWN_PASS_SPAN[Own][s]);

                bool backward;
                // Test for backward pawn.
                // If the pawn is passed, isolated, connected or levered (it can capture an enemy pawn).
                // If the rank is greater then Rank 6
                // If there are friendly pawns behind on adjacent files and they are able to advance and support the pawn.
                // Then it cannot be backward either.
                if (  passed || isolated || levered || connected || r >= R_6
                   // Partially checked the opp behind pawn, But need to check own behind attack span are not backward or rammed 
                   || (own_pawns & PAWN_ATTACK_SPAN[Opp][s] && !(opp_pawns & (s-PUSH)))
                   )
                {
                    backward = false;
                }
                else
                {
                    Bitboard b;
                    // Now know that there are no friendly pawns beside or behind this pawn on adjacent files.
                    // Now check whether the pawn is backward by looking in the forward direction on the
                    // adjacent files, and picking the closest pawn there.
                    b = PAWN_ATTACK_SPAN[Own][s] & pos.pieces<PAWN> ();
                    b = PAWN_ATTACK_SPAN[Own][s] & rank_bb (scan_backmost_sq (Own, b));

                    // If have an enemy pawn in the same or next rank, the pawn is
                    // backward because it cannot advance without being captured.
                    backward = opp_pawns & (b | shift_del<PUSH> (b));
                }

                assert (passed ^ (opposed || (opp_pawns & PAWN_ATTACK_SPAN[Own][s])));

                Score score = SCORE_ZERO;

                if (connected)
                {
                    score += CONNECTED[opposed][phalanx][r];
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
                        score -= BACKWARD[opposed][f];
                    }
                }
                
                if (r > R_4 && levered)
                {
                    score += LEVER[r];
                }

                if (doubled)
                {
                    score -= DOUBLED[f] / i32(rank_dist (s, scan_frntmost_sq (Own, doubled)));
                }
                else
                {
                    // Only the frontmost passed pawn on each file is considered a true passed pawn.
                    // Passed pawns will be properly scored in evaluation
                    // because complete attack info needed to evaluate passed pawns.
                    if (passed)
                    {
                        e->passed_pawns[Own] += s;
                    }
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

            i32 span = e->semiopen_files[Own] ^ 0xFF;
            e->pawn_span[Own] = span != 0 ? u08(scan_msq (span)) - u08(scan_lsq (span)) : 0;

            return pawn_score;
        }

    #undef S

    } // namespace

    template<Color Own>
    // pawn_shelter_storm() calculates shelter and storm penalties
    // for the file the king is on, as well as the two adjacent files.
    Value Entry::pawn_shelter_storm (const Position &pos, Square k_sq) const
    {
        const Color Opp = WHITE == Own ? BLACK : WHITE;

        Rank kr = _rank (k_sq);
        Bitboard front_pawns = pos.pieces<PAWN> () & (FRONT_RANK_bb[Own][kr] | RANK_bb[kr]);
        Bitboard our_front_pawns = front_pawns & pos.pieces (Own);
        Bitboard opp_front_pawns = front_pawns & pos.pieces (Opp);

        Value value = KING_SAFETY_BY_PAWN;

        File kf = _file (k_sq);
        i08 kfl = min (max (kf, F_B), F_G);
        for (i08 f = kfl - 1; f <= kfl + 1; ++f)
        {
            assert (F_A <= f && f <= F_H);

            Bitboard mid_pawns;

            mid_pawns = opp_front_pawns & FILE_bb[f];
            u08 r1 = mid_pawns != U64(0) ? rel_rank (Own, scan_frntmost_sq (Opp, mid_pawns)) : R_1;
            if (  kf == f
               && END_EDGE_bb & (File(f) | Rank(r1))
               && rel_rank (Own, k_sq) + 1 == r1
               )
            {
                value += Value(200); // Enemy pawn in front shelter
            }
            else
            {
                mid_pawns = our_front_pawns & FILE_bb[f];
                u08 r0 = mid_pawns != U64(0) ? rel_rank (Own, scan_backmost_sq (Own, mid_pawns)) : R_1;
                value -= 
                      + SHELTER_WEAKNESS[r0]
                      + STORM_DANGER[r0 == R_1 ? 0 : r0 + 1 != r1 ? 1 : 2][r1];
            }
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

    // probe() takes a position object as input, computes a Pawn::Entry object,
    // and returns a pointer to Pawn::Entry object.
    // The result is also stored in a hash table, so don't have
    // to recompute everything when the same pawn structure occurs again.
    Entry* probe (const Position &pos, Table &table)
    {
        Key pawn_key = pos.pawn_key ();
        Entry *e     = table[pawn_key];

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
        for (i08 opposed = 0; opposed <= 1; ++opposed)
        {
            for (i08 phalanx = 0; phalanx <= 1; ++phalanx)
            {
                for (Rank r = R_2; r < R_8; ++r)
                {
                    i32 value = SEED[r] + (phalanx != 0 ? (SEED[r + 1] - SEED[r])/2 : 0);
                    CONNECTED[opposed][phalanx][r] = mk_score (value/2, value >> opposed);
                }
            }
        }
    }

} // namespace Pawns
