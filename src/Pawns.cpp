#include "Pawns.h"

namespace Pawns {

    using namespace std;
    using namespace BitBoard;

    namespace {

        const i16 PawnFileValue[8] = { 1, 3, 3, 4, 4, 3, 3, 1 };

    #define S(mg, eg) mk_score(mg, eg)

        // Doubled pawn penalty by file
        const Score DoubledPenalty[F_NO] =
        {
            S(+13,+43), S(+20,+48), S(+23,+48), S(+23,+48), S(+23,+48), S(+23,+48), S(+20,+48), S(+13,+43)
        };

        // Isolated pawn penalty by [!opposers][file]
        const Score IsolatedPenalty[2][F_NO] =
        {
            {S(+25,+30), S(+36,+35), S(+40,+35), S(+40,+35), S(+40,+35), S(+40,+35), S(+36,+35), S(+25,+30)},
            {S(+37,+45), S(+54,+52), S(+60,+52), S(+60,+52), S(+60,+52), S(+60,+52), S(+54,+52), S(+37,+45)}
        };

        // Backward pawn penalty by [!opposers][file]
        const Score BackwardPenalty[2][F_NO] =
        {
            {S(+20,+28), S(+29,+31), S(+33,+31), S(+33,+31), S(+33,+31), S(+33,+31), S(+29,+31), S(+20,+28)},
            {S(+30,+42), S(+43,+46), S(+49,+46), S(+49,+46), S(+49,+46), S(+49,+46), S(+43,+46), S(+30,+42)}
        };

        // Candidate passed pawn bonus by [rank]
        const Score CandidateBonus[R_NO] =
        {
            S(+ 0,+ 0), S(+ 6,+13), S(+ 6,+13), S(+14,+29), S(+34,+68), S(+83,166), S(+ 0,+ 0), S(+ 0,+ 0)
        };
        
        // Levers bonus by [rank]
        const Score LeverBonus[R_NO] = 
        {
                                 // S(+ 8,+ 8), S(+15,+15), S(+30,+30), S(+50,+50)
            S(+ 0,+ 0), S(+ 0,+ 0), S(+ 6,+ 6), S(+12,+12), S(+20,+20), S(+40,+40), S(+ 0,+ 0), S(+ 0,+ 0)
        };

        // Connected pawn bonus by [file] and [rank] (initialized by formula)
        /**/  Score ConnectedBonus[F_NO][R_NO];

        const Score FileSpanBonus      = S(+ 0,+15); // Bonus for file distance of the two outermost pawns
        const Score UnstoppableBonus   = S(+ 0,+20); // Bonus for pawn going to promote
        const Score UnsupportedPenalty = S(+20,+10); // Penalty for Unsupported pawn

    #undef S

    #define V Value

        // Weakness of our pawn shelter in front of the king indexed by [rank]
        const Value ShelterWeakness[R_NO] =
        {
            V(+100), V(+  0), V(+ 27), V(+ 73), V(+ 92), V(+101), V(+101), V(+  0)
        };

        // Danger of enemy pawns moving toward our king indexed by
        // [no friendly pawn | pawn unblocked | pawn blocked][rank of enemy pawn]
        const Value StormDanger[3][R_NO] =
        {
            { V(+ 0),  V(+66), V(+130), V(+52), V(+26),  V(+ 0),  V(+ 0),  V(+ 0) },
            { V(+26),  V(+32), V(+ 96), V(+38), V(+20),  V(+ 0),  V(+ 0),  V(+ 0) },
            { V(+ 0),  V(+ 0), V(+160), V(+25), V(+12),  V(+ 0),  V(+ 0),  V(+ 0) }
        };

        // Max bonus for king safety by pawns.
        // Corresponds to start position with all the pawns
        // in front of the king and no enemy pawn on the horizon.
        const Value KingSafetyByPawn = V(+263);

    #undef V

        template<Color C>
        inline Score evaluate (const Position &pos, Pawns::Entry *e)
        {
            const Color  C_  = WHITE == C ? BLACK  : WHITE;
            const Delta PUSH = WHITE == C ? DEL_N  : DEL_S;
            //const Delta PULL = WHITE == C ? DEL_S  : DEL_N;
            const Delta RCAP = WHITE == C ? DEL_NE : DEL_SW;
            const Delta LCAP = WHITE == C ? DEL_NW : DEL_SE;

            const Bitboard pawns[CLR_NO] =
            {
                pos.pieces<PAWN> (C ),
                pos.pieces<PAWN> (C_)
            };

            e->pawns_attacks  [C] = shift_del<RCAP> (pawns[0]) | shift_del<LCAP> (pawns[0]);
            //e->blocked_pawns  [C] = pawns[0] & shift_del<PULL> (pawns[1]);
            e->passed_pawns   [C] = U64 (0);
            e->unstopped_pawns[C] = U64 (0);
            e->semiopen_files [C] = 0xFF;
            e->king_sq        [C] = SQ_NO;

            Bitboard center_pawns = pawns[0] & ExtCntr_bb[C];
            if (center_pawns)
            {
                Bitboard color_pawns;
                color_pawns = center_pawns & Liht_bb;
                e->pawns_on_sqrs[C][WHITE] = (color_pawns ? pop_count<Max15>(color_pawns) : 0);
                color_pawns = center_pawns & Dark_bb;
                e->pawns_on_sqrs[C][BLACK] = (color_pawns ? pop_count<Max15>(color_pawns) : 0);
            }
            else
            {
                e->pawns_on_sqrs[C][WHITE] = 0;
                e->pawns_on_sqrs[C][BLACK] = 0;
            }

            Score pawn_score = SCORE_ZERO;

            const Square *pl = pos.list<PAWN> (C);
            Square s;
            // Loop through all pawns of the current color and score each pawn
            while ((s = *pl++) != SQ_NO)
            {
                ASSERT (pos[s] == (C | PAWN));

                const File f = _file (s);
                const Rank r = rel_rank (C, s);

                // This file cannot be semi-open
                e->semiopen_files[C] &= ~(1 << f);

                // Supporter rank
                Bitboard sr_bb = rank_bb (s-PUSH);
                // Connector rank, for connectors pawn detection
                Bitboard cr_bb = rank_bb (s) | sr_bb; 

                Bitboard friend_adj_pawns = pawns[0] & AdjFile_bb[f];
                // Bitboard supporters, doublers, or leverers.
                Bitboard supporters = (friend_adj_pawns & sr_bb);
                Bitboard connectors = (friend_adj_pawns & cr_bb);
                Bitboard doublers   = (pawns[0] & FrontSqrs_bb[C][s]);
                Bitboard leverers   = (pawns[1] & PawnAttacks[C][s]);
                Bitboard opposers   = (pawns[1] & FrontSqrs_bb[C][s]);
                // Flag the pawn as passed, isolated (but not the backward one).
                bool isolated      = !(friend_adj_pawns);
                bool passed        = !(pawns[1] & PawnPassSpan[C][s]);

                bool backward;
                // Test for backward pawn.
                // If the pawn is passed, isolated, connected or levered (it can capture an enemy pawn).
                // If the rank is greater then Rank 6
                // If there are friendly pawns behind on adjacent files and they are able to advance and support the pawn.
                // Then it cannot be backward either.
                if (  passed || isolated || connectors || leverers
                   || r >= R_6
                   // Partially checked the opp behind pawn, But need to check own behind attack span are not backward or rammed 
                   || (pawns[0] & PawnAttackSpan[C_][s] && !(pawns[1] & (s-PUSH)))
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
                    b = PawnAttackSpan[C][s] & pos.pieces<PAWN> ();
                    b = PawnAttackSpan[C][s] & rank_bb (scan_backmost_sq (C, b));

                    // If have an enemy pawn in the same or next rank, the pawn is
                    // backward because it cannot advance without being captured.
                    backward = pawns[1] & (b | shift_del<PUSH> (b));
                }

                // A not passed pawn is a candidate to become passed, if it is free to
                // advance and if the number of friendly pawns beside or behind this
                // pawn on adjacent files is higher or equal than the number of
                // enemy pawns in the forward direction on the adjacent files.
                bool candidate = false;
                if (!(passed || isolated || backward || opposers)) // r > R_6
                {
                    Bitboard helpers = (friend_adj_pawns & PawnAttackSpan[C_][s+PUSH]); // Only behind friend adj pawns are Helpers
                    if (helpers)
                    {
                        
                        Bitboard sentries = pawns[1] & PawnAttackSpan[C][s];    // Only front enemy adj pawns
                        candidate = ((more_than_one (helpers) ? pop_count<Max15> (friend_adj_pawns) : 1)
                                  >= ((sentries) ? (more_than_one (sentries) ? pop_count<Max15> (sentries) : 1) : 0));
                        
                        /*
                        ASSERT (r+2 <= R_8);

                        u08 helpers_count = 0;
                        Bitboard sentries = pawns[1] & PawnAttackSpan[C][s];       // Only front enemy adj pawns
                        Bitboard helpers_copy = helpers;
                        while (helpers_copy)
                        {
                            Square sq = pop_lsq (helpers_copy);
                            Bitboard sentries_helper = pawns[1] & ~sentries & PawnPassSpan[C][sq] & FrontRank_bb[C_][r+2];
                            Bitboard helpers_helper  = pawns[0] & PawnAttackSpan[C][sq-PUSH] & FrontRank_bb[C_][r];
                            if (helpers_helper || !sentries_helper)
                            {
                                ++helpers_count;
                            }
                        }
                        candidate = helpers_count && (helpers_count >= pop_count<Max15> (sentries));
                        */
                    }
                }

                ASSERT (passed ^ (opposers || (pawns[1] & PawnAttackSpan[C][s])));

                // Score this pawn
                Score score = SCORE_ZERO;

                if (connectors)
                {
                    score += ConnectedBonus[f][r];
                }
                if (r > R_4 && leverers)
                {
                    score += LeverBonus[r]; //* (supporters ? 1 : 2); // TODO::
                }

                if (isolated)
                {
                    score -= IsolatedPenalty[!opposers][f];
                }
                else
                {
                    if (!supporters)
                    {
                        score -= UnsupportedPenalty;
                    }
                    if (backward)
                    {
                        score -= BackwardPenalty[!opposers][f];
                    }
                    if (candidate)
                    {
                        score += CandidateBonus[r];
                    }
                }
                
                if (doublers)
                {
                    score -= DoubledPenalty[f]
                            * (more_than_one (doublers) ? pop_count<Max15> (doublers) : 1)
                            / i32(rank_dist (s, scan_frntmost_sq (C, doublers)));
                    
                    //Bitboard doubly_doublers = (friend_adj_pawns & PawnAttackSpan[C][s]);
                    //if (doubly_doublers) score -= DoubledPenalty[f] * i32(rank_dist (s, scan_backmost_sq (C, doubly_doublers))) / 4;
                }
                else
                {
                    // Passed pawns will be properly scored in evaluation because need
                    // full attack info to evaluate passed pawns.
                    // Only the frontmost passed pawn on each file is considered a true passed pawn.
                    if (passed)
                    {
                        e->passed_pawns   [C] += s;
                    }
                    if (candidate || (r == R_6 && !opposers))
                    {
                        e->unstopped_pawns[C] += s;
                    }
                }
                
#ifndef NDEBUG
                //cout << to_string (s) << " : " << mg_value (score) << ", " << eg_value (score) << endl;
#endif
                pawn_score += score;
            }

#ifndef NDEBUG
            //cout << pretty (e->unstopped_pawns[C]) << endl;
            //cout << "-------------" << endl;
#endif

            // In endgame it's better to have pawns on both wings.
            // So give a bonus according to file distance between left and right outermost pawns span.
            if (pos.count<PAWN>(C) > 1)
            {
                i32 span = e->semiopen_files[C] ^ 0xFF;
                e->pawn_span[C] = u08(scan_msq (span)) - u08(scan_lsq (span));
                pawn_score += FileSpanBonus * i32(e->pawn_span[C]);
            }
            else
            {
                e->pawn_span[C] = 0;
            }

            return pawn_score;
        }

    } // namespace

    template<Color C>
    // pawn_shelter_storm() calculates shelter and storm penalties
    // for the file the king is on, as well as the two adjacent files.
    Value Entry::pawn_shelter_storm (const Position &pos, Square k_sq)
    {
        const Color C_ = WHITE == C ? BLACK : WHITE;

        const Rank kr = _rank (k_sq);
        const Bitboard front_pawns[CLR_NO] =
        {
            pos.pieces<PAWN> (C ) & (FrontRank_bb[C][kr] | Rank_bb[kr]),
            pos.pieces<PAWN> (C_) & (FrontRank_bb[C][kr] | Rank_bb[kr])
        };

        Value value = KingSafetyByPawn;

        const File kf = _file (k_sq);
        const i08 ckf = min (max (kf, F_B), F_G);
        for (i08 f = ckf - 1; f <= ckf + 1; ++f)
        {
            ASSERT (F_A <= f && f <= F_H);

            Bitboard mid_pawns;

            mid_pawns = front_pawns[1] & File_bb[f];
            u08 br = mid_pawns ? rel_rank (C, scan_frntmost_sq (C_, mid_pawns)) : R_1;
            if (  kf == f
               && EndEdge_bb & (File(f) | Rank(br))
               && rel_rank (C, k_sq) + 1 == br
               )
            {
                value += Value(200); // Enemy pawn in front Shelter
            }
            else
            {
                mid_pawns = front_pawns[0] & File_bb[f];
                u08 wr = mid_pawns ? rel_rank (C, scan_backmost_sq (C , mid_pawns)) : R_1;
                u08 danger = wr == R_1 ? 0 : wr + 1 != br ? 1 : 2;
                value -= ShelterWeakness[wr] + StormDanger[danger][br];
            }
        }

        return value;
    }

    template<Color C>
    // Entry::evaluate_unstoppable_pawns<>() scores the most advanced among the passed and candidate pawns.
    // In case opponent has no pieces but pawns, this is somewhat
    // related to the possibility pawns are unstoppable.
    Score Entry::evaluate_unstoppable_pawns () const
    {
        Bitboard unstoppable_pawns = passed_pawns[C]|unstopped_pawns[C];
        return unstoppable_pawns ? UnstoppableBonus * i32(rel_rank (C, scan_frntmost_sq(C, unstoppable_pawns))) :
                                  SCORE_ZERO;
    }

    // explicit template instantiations
    // --------------------------------
    template Value Entry::pawn_shelter_storm<WHITE> (const Position &pos, Square k_sq);
    template Value Entry::pawn_shelter_storm<BLACK> (const Position &pos, Square k_sq);
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
            e->pawn_key    = pawn_key;
            e->pawn_score  = evaluate<WHITE> (pos, e)
                           - evaluate<BLACK> (pos, e);
        }
        return e;
    }

    // Initializes some tables by formula instead of hard-coding their values
    void initialize ()
    {
        for (i08 r = R_1; r <= R_8; ++r)
        {
            for (i08 f = F_A; f <= F_H; ++f)
            {
                i32 value = 1 * r * (r-1) * (r-2) + PawnFileValue[f] * (r/2 + 1);
                ConnectedBonus[f][r] = mk_score (value, value);
            }
        }
    }

} // namespace Pawns
