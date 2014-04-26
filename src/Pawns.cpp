#include "Pawns.h"

#include <algorithm>

#include "BitBoard.h"
#include "BitCount.h"

namespace Pawns {

    using namespace std;
    using namespace BitBoard;

    namespace {

    #define V Value
    #define S(mg, eg) mk_score(mg, eg)

        // Doubled pawn penalty by file
        const Score DoubledPenalty[F_NO] =
        {
            S(+13,+43), S(+20,+48), S(+23,+48), S(+23,+48),
            S(+23,+48), S(+23,+48), S(+20,+48), S(+13,+43)
        };

        // Isolated pawn penalty by opposed flag and file
        const Score IsolatedPenalty[CLR_NO][F_NO] =
        {
            {
                S(+37,+45), S(+54,+52), S(+60,+52), S(+60,+52),
                S(+60,+52), S(+60,+52), S(+54,+52), S(+37,+45)
            },
            {
                S(+25,+30), S(+36,+35), S(+40,+35), S(+40,+35),
                S(+40,+35), S(+40,+35), S(+36,+35), S(+25,+30)
            }
        };

        // Backward pawn penalty by opposed flag and file
        const Score BackwardPenalty[CLR_NO][F_NO] =
        {
            {
                S(+30,+42), S(+43,+46), S(+49,+46), S(+49,+46),
                S(+49,+46), S(+49,+46), S(+43,+46), S(+30,+42)
            },
            {
                S(+20,+28), S(+29,+31), S(+33,+31), S(+33,+31),
                S(+33,+31), S(+33,+31), S(+29,+31), S(+20,+28)
            }
        };

        // Connected pawn bonus by [file] and [rank] (initialized by formula)
        /**/  Score ConnectedBonus[F_NO][R_NO];

        // Candidate passed pawn bonus by [rank]
        const Score CandidatePassedBonus[R_NO] =
        {
            S(+ 0,+ 0), S(+ 6,+13), S(+ 6,+13), S(+14,+29),
            S(+34,+68), S(+83,166), S(+ 0,+ 0), S(+ 0,+ 0),
        };

        // Bonus for file distance of the two outermost pawns
        const Score PawnsFileSpanBonus      = S(+ 0,+15);
        // Unsupported pawn penalty
        const Score UnsupportedPawnPenalty  = S(+20,+10);

        // Weakness of our pawn shelter in front of the king indexed by [rank]
        const Value ShelterWeakness[R_NO] =
        {
            V(+100), V(+  0), V(+ 27), V(+ 73), V(+ 92), V(+101), V(+101), V(+ 0)
        };

        // Danger of enemy pawns moving toward our king indexed by
        // [no friendly pawn | pawn unblocked | pawn blocked][rank of enemy pawn]
        const Value StormDanger[3][R_NO] =
        {
            { V(+ 0),  V(+64), V(+128), V(+56), V(+32),  V(+ 4),  V(+ 0),  V(+ 0) },
            { V(+ 0),  V(+ 0), V(+  0), V(+48), V(+16),  V(+ 2),  V(+ 0),  V(+ 0) },
            { V(+ 0),  V(+ 0), V(+168), V(+30), V(+12),  V(+ 0),  V(+ 0),  V(+ 0) }
        };

        // Max bonus for king safety. Corresponds to start position with all the pawns
        // in front of the king and no enemy pawn on the horizont.
        const Value MaxSafetyBonus = V(+263);

    #undef S
    #undef V

        template<Color C>
        inline Score evaluate (const Position &pos, Pawns::Entry *e)
        {
            const Color  C_  = (WHITE == C) ? BLACK  : WHITE;
            const Delta PUSH = (WHITE == C) ? DEL_N  : DEL_S;
            const Delta RCAP = (WHITE == C) ? DEL_NE : DEL_SW;
            const Delta LCAP = (WHITE == C) ? DEL_NW : DEL_SE;

            const Bitboard pawns[CLR_NO] =
            {
                pos.pieces<PAWN> (C ),
                pos.pieces<PAWN> (C_),
            };

            e->_passed_pawns  [C] = e->_candidate_pawns[C] = U64 (0);
            e->_king_sq       [C] = SQ_NO;
            e->_semiopen_files[C] = 0xFF;
            e->_pawn_attacks  [C] = shift_del<RCAP> (pawns[0]) | shift_del<LCAP> (pawns[0]);
            e->_pawn_count_sq [C][WHITE] = pop_count<MAX15> (pawns[0] & LIHT_bb);
            e->_pawn_count_sq [C][BLACK] = pop_count<MAX15> (pawns[0] & DARK_bb);
            
            Score pawn_score = SCORE_ZERO;

            const Square *pl = pos.list<PAWN> (C);
            Square s;
            // Loop through all pawns of the current color and score each pawn
            while ((s = *pl++) != SQ_NO)
            {
                ASSERT (pos[s] == (C | PAWN));

                File f = _file (s);
                Rank r = rel_rank (C, s);

                // This file cannot be semi-open
                e->_semiopen_files[C] &= ~(1 << f);

                // Previous rank
                Bitboard pr_bb = rank_bb (s - PUSH);

                // Our rank plus previous one, for connected pawn detection
                Bitboard rr_bb = rank_bb (s) | pr_bb;

                // Flag the pawn as passed, isolated, doubled,
                // unsupported or connected (but not the backward one).
                bool connected   =  (pawns[0] & AdjFile_bb[f] & rr_bb);
                bool unsupported = !(pawns[0] & AdjFile_bb[f] & pr_bb);
                bool isolated    = !(pawns[0] & AdjFile_bb[f]);
                Bitboard doubled =   pawns[0] & FrontSqs_bb[C][s];
                bool opposed     =   pawns[1] & FrontSqs_bb[C][s];
                bool passed      = !(pawns[1] & PasserPawnSpan[C][s]);

                bool backward;
                // Test for backward pawn.
                // If the pawn is passed, isolated, or connected it cannot be backward.
                // If there are friendly pawns behind on adjacent files or
                // If it can capture an enemy pawn it cannot be backward either.
                if (   (passed || isolated || connected)
                    || (pawns[0] & PawnAttackSpan[C_][s])
                    || (pawns[1] & PawnAttacks[C][s]))
                {
                    backward = false;
                }
                else
                {
                    Bitboard b;
                    // We now know that there are no friendly pawns beside or behind this pawn on adjacent files.
                    // We now check whether the pawn is backward by looking in the forward direction on the
                    // adjacent files, and picking the closest pawn there.
                    b = PawnAttackSpan[C][s] & (pawns[0] | pawns[1]);
                    b = PawnAttackSpan[C][s] & rank_bb (scan_backmost_sq (C, b));

                    // If we have an enemy pawn in the same or next rank, the pawn is
                    // backward because it cannot advance without being captured.
                    backward = (b | shift_del<PUSH> (b)) & pawns[1];
                }

                ASSERT (opposed || passed || (PawnAttackSpan[C][s] & pawns[1]));

                // A not passed pawn is a candidate to become passed, if it is free to
                // advance and if the number of friendly pawns beside or behind this
                // pawn on adjacent files is higher or equal than the number of
                // enemy pawns in the forward direction on the adjacent files.
                Bitboard adj_pawns;
                bool candidate_passed = !(opposed || passed || backward || isolated)
                    && ((adj_pawns = PawnAttackSpan[C_][s + PUSH] & pawns[0]) != U64 (0))
                    && (pop_count<MAX15> (adj_pawns) >= pop_count<MAX15> (PawnAttackSpan[C][s] & pawns[1]));

                // Passed pawns will be properly scored in evaluation because we need
                // full attack info to evaluate passed pawns. Only the frontmost passed
                // pawn on each file is considered a true passed pawn.
                if (passed && !doubled) e->_passed_pawns[C] += s;

                // Score this pawn
                if (isolated)   pawn_score -= IsolatedPenalty[opposed][f];

                if (unsupported && !isolated) pawn_score -= UnsupportedPawnPenalty;

                if (doubled)    pawn_score -= DoubledPenalty[f] / i32 (rank_dist (s, scan_lsq (doubled)));
            
                if (backward)   pawn_score -= BackwardPenalty[opposed][f];

                if (connected)  pawn_score += ConnectedBonus[f][r];

                if (candidate_passed)
                {
                    pawn_score += CandidatePassedBonus[r];

                    if (!doubled) e->_candidate_pawns[C] += s;
                }
            }

            // In endgame it's better to have pawns on both wings. So give a bonus according
            // to file distance between left and right outermost pawns.
            if (pos.count<PAWN> (C) > 1)
            {
                Bitboard b = e->_semiopen_files[C] ^ 0xFF;
                pawn_score += PawnsFileSpanBonus * (i32 (scan_msq (b)) - i32 (scan_lsq (b)));
            }

            return pawn_score;
        }

    } // namespace

    // Initializes some tables by formula instead of hard-coding their values
    void initialize ()
    {
        const i16 FileBonus[8] = { 1, 3, 3, 4, 4, 3, 3, 1 };

        for (i08 r = R_1; r < R_8; ++r)
        {
            for (i08 f = F_A; f <= F_H; ++f)
            {
                i16 bonus = 1 * r * (r-1) * (r-2) + FileBonus[f] * (r/2 + 1);
                ConnectedBonus[f][r] = mk_score (bonus, bonus);
            }
        }
    }

    // probe() takes a position object as input, computes a Entry object, and returns
    // a pointer to it. The result is also stored in a hash table, so we don't have
    // to recompute everything when the same pawn structure occurs again.
    Entry* probe (const Position &pos, Table &table)
    {
        Key pawn_key = pos.pawn_key ();
        Entry *e     = table[pawn_key];

        if (e->_pawn_key != pawn_key)
        {
            e->_pawn_key    = pawn_key;
            e->_pawn_score  = evaluate<WHITE> (pos, e)
                            - evaluate<BLACK> (pos, e);
        }

        return e;
    }

    template<Color C>
    // Entry::shelter_storm() calculates shelter and storm penalties for the file
    // the king is on, as well as the two adjacent files.
    Value Entry::shelter_storm (const Position &pos, Square king_sq)
    {
        const Color C_ = (WHITE == C) ? BLACK : WHITE;

        Value safety = MaxSafetyBonus;

        Bitboard front_pawns = pos.pieces<PAWN> () & (FrontRank_bb[C][_rank (king_sq)] | rank_bb (king_sq));
        Bitboard pawns[CLR_NO] =
        {
            front_pawns & pos.pieces (C ),
            front_pawns & pos.pieces (C_),
        };

        const i08 kf = _file (king_sq);
        const i08 w_del = 1 + (kf==F_C || kf==F_H) - (kf==F_A);
        const i08 e_del = 1 + (kf==F_F || kf==F_A) - (kf==F_H);
        for (i08 f = kf - w_del; f <= kf + e_del; ++f)
        {
            Bitboard mid_pawns;

            mid_pawns  = pawns[1] & File_bb[f];
            u08 b_rk = (mid_pawns != U64 (0))
                ? rel_rank (C, scan_frntmost_sq (C_, mid_pawns))
                : R_1;

            if (   (MIDEDGE_bb & (f | b_rk))
                && (kf == f)
                && (rel_rank (C, king_sq) == b_rk - 1)
               )
            {
                safety += Value (200);
            }
            else
            {
                mid_pawns = pawns[0] & File_bb[f];
                
                u08 w_rk = (mid_pawns != U64 (0))
                    ? rel_rank (C, scan_backmost_sq (C , mid_pawns))
                    : R_1;

                u08 danger = (w_rk == R_1 || w_rk > b_rk) ? 0 : ((w_rk + 1) != b_rk) ? 1 : 2;
                safety -= (ShelterWeakness[w_rk] + StormDanger[danger][b_rk]);
            }
        }

        return safety;
    }

    template<Color C>
    // Entry::king_safety() calculates and caches a bonus for king safety. It is
    // called only when king square changes, about 20% of total king_safety() calls.
    Score Entry::do_king_safety (const Position &pos, Square king_sq)
    {
        _king_sq      [C] = king_sq;
        _castle_rights[C] = pos.can_castle (C);
        _kp_min_dist  [C] = 0;

        Bitboard pawns = pos.pieces<PAWN> (C);
        if (pawns != U64 (0))
        {
            while ((DistanceRings[king_sq][_kp_min_dist[C]++] & pawns) == U64 (0)) {}
        }

        Value bonus = VALUE_ZERO;
        if (rel_rank (C, king_sq) < R_4)
        {
            // If we can castle use the bonus after the castle if is bigger
            if (pos.can_castle (C))
            {
                if (pos.can_castle (Castling<C, CS_K>::Right))
                {
                    bonus = max (bonus, shelter_storm<C> (pos, rel_sq (C, SQ_G1)));
                }
                if (pos.can_castle (Castling<C, CS_Q>::Right))
                {
                    bonus = max (bonus, shelter_storm<C> (pos, rel_sq (C, SQ_C1)));
                }
            }
            if (bonus < MaxSafetyBonus)
            {
                bonus = max (bonus, shelter_storm<C> (pos, king_sq));
            }
        }

        return mk_score (bonus, -16 * _kp_min_dist[C]);
    }

    // Explicit template instantiation
    // -------------------------------
    template Score Entry::do_king_safety<WHITE> (const Position &pos, Square king_sq);
    template Score Entry::do_king_safety<BLACK> (const Position &pos, Square king_sq);

} // namespace Pawns
