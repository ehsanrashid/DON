#include "Evaluator.h"

#include <iomanip>
#include <sstream>

#include "Pawns.h"
#include "Material.h"
#include "MoveGenerator.h"
#include "Thread.h"

namespace Evaluate {

    using namespace std;
    using namespace BitBoard;
    using namespace MoveGen;
    using namespace Threads;
    using namespace UCI;

    namespace {

        // Struct EvalInfo contains various information computed and collected
        // by the evaluation functions.
        struct EvalInfo
        {
            // Pointers to material and pawn hash table entries
            Material::Entry *mi;
            Pawns   ::Entry *pi;

            // ful_attacked_by[Color][PieceT] contains all squares attacked by a given color and piece type,
            // ful_attacked_by[Color][NONE] contains all squares attacked by the given color.
            Bitboard ful_attacked_by[CLR_NO][TOTL];
            // pin_attacked_by[Color][PieceT] contains all squares attacked by a given color and piece type with pinned removed,
            Bitboard pin_attacked_by[CLR_NO][TOTL];

            // pinneds[Color] contains all the pinned pieces
            Bitboard pinneds[CLR_NO];

            // king_ring[Color] is the zone around the king which is considered
            // by the king safety evaluation. This consists of the squares directly
            // adjacent to the king, and the three (or two, for a king on an edge file)
            // squares two ranks in front of the king. For instance, if black's king
            // is on g8, king_ring[BLACK] is a bitboard containing the squares f8, h8,
            // f7, g7, h7, f6, g6 and h6.
            Bitboard king_ring[CLR_NO];

            // king_ring_attackers_count[color] is the number of pieces of the given color
            // which attack a square in the king_ring of the enemy king.
            u08 king_ring_attackers_count[CLR_NO];

            // king_ring_attackers_weight[Color] is the sum of the "weight" of the pieces
            // of the given color which attack a square in the king_ring of the enemy king.
            // The weights of the individual piece types are given by the variables KING_ATTACK[PieceT]
            u32 king_ring_attackers_weight[CLR_NO];

            // king_zone_attacks_count[Color] is the sum of attacks of the pieces
            // of the given color which attack a square directly adjacent to the enemy king.
            // The weights of the individual piece types are given by the variables KING_ATTACK[PieceT]
            // Pieces which attack more than one square are counted multiple times.
            // For instance, if black's king is on g8 and there's a white knight on g5,
            // this knight adds 2 to king_zone_attacks_count[WHITE].
            u08 king_zone_attacks_count[CLR_NO];

        };

        namespace Tracer {

            // Used for tracing
            enum TermT
            {
                MATERIAL = 6, IMBALANCE, MOBILITY, THREAT, PASSER, SPACE, TOTAL, TERM_NO
            };

            Score Scores[CLR_NO][TERM_NO];

            inline void set (u08 idx, Color c, Score score)
            {
                Scores[c][idx] = score;
            }
            inline void set (u08 idx, Score score_w, Score score_b = SCORE_ZERO)
            {
                set (idx, WHITE, score_w);
                set (idx, BLACK, score_b);
            }

            inline void write (stringstream &ss, const string &name, u08 idx)
            {
                Score score_w = Scores[WHITE][idx]
                    , score_b = Scores[BLACK][idx];

                switch (idx)
                {
                case MATERIAL: case IMBALANCE: case PAWN: case TOTAL:
                    ss  << setw (15) << name << " | ----- ----- | ----- ----- | " << showpos
                        << setw ( 5) << value_to_cp (mg_value (score_w - score_b)) << " "
                        << setw ( 5) << value_to_cp (eg_value (score_w - score_b)) << "\n";
                break;

                default:
                    ss  << setw (15) << name << " | " << showpos
                        << setw ( 5) << value_to_cp (mg_value (score_w)) << " "
                        << setw ( 5) << value_to_cp (eg_value (score_w)) << " | "
                        << setw ( 5) << value_to_cp (mg_value (score_b)) << " "
                        << setw ( 5) << value_to_cp (eg_value (score_b)) << " | "
                        << setw ( 5) << value_to_cp (mg_value (score_w - score_b)) << " "
                        << setw ( 5) << value_to_cp (eg_value (score_w - score_b)) << "\n";
                break;
                }
            }

            string trace (const Position &pos);

        }

        enum EvalWeightT { PIECE_MOBILITY, PAWN_STRUCTURE, PASSED_PAWN, SPACE_ACTIVITY, KING_SAFETY, EVAL_NO };
        
        struct Weight { i32 mg, eg; };
        
        // Evaluation weights, initialized from UCI options
        Weight Weights[EVAL_NO];

    #define S(mg, eg) mk_score (mg, eg)

        // Internal evaluation weights
        const Score INTERNAL_WEIGHTS[EVAL_NO] =
        {
            S(+289,+344), // Mobility
            S(+233,+201), // Pawn Structure
            S(+221,+273), // Passed Pawns
            S(+ 46,+  0), // Space Activity
            S(+318,+  0)  // King Safety
        };

        // PIECE_MOBILIZE[PieceT][Attacks] contains bonuses for mobility,
        const Score PIECE_MOBILIZE[NONE][28] =
        {
            {},
            // Knight
            {
                S(-65,-50), S(-42,-30), S(- 9,-10), S(+ 3,  0), S(+15,+10),
                S(+27,+20), S(+37,+28), S(+42,+31), S(+44,+33)
            },
            // Bishop
            {
                S(-52,-47), S(-28,-23), S(+ 6,+ 1), S(+20,+15), S(+34,+29),
                S(+48,+43), S(+60,+55), S(+68,+63), S(+74,+68), S(+77,+72),
                S(+80,+75), S(+82,+77), S(+84,+79), S(+86,+81)
            },
            // Rook
            {
                S(-47,- 53), S(-31,- 26), S(- 5,   0), S(+ 1,+ 16), S(+ 7,+ 32),
                S(+13,+ 48), S(+18,+ 64), S(+22,+ 80), S(+26,+ 96), S(+29,+109),
                S(+31,+115), S(+33,+119), S(+35,+122), S(+36,+123), S(+37,+124),
            },
            // Queen
            {
                S(-42,-40), S(-28,-23), S(- 5,- 7), S(  0,  0), S(+ 6,+10),
                S(+11,+19), S(+13,+29), S(+18,+38), S(+20,+40), S(+21,+41),
                S(+22,+41), S(+22,+41), S(+22,+41), S(+23,+41), S(+24,+41),
                S(+25,+41), S(+25,+41), S(+25,+41), S(+25,+41), S(+25,+41),
                S(+25,+41), S(+25,+41), S(+25,+41), S(+25,+41), S(+25,+41),
                S(+25,+41), S(+25,+41), S(+25,+41)
            },
            // King
            {
                S(  0,-32), S(  0,-16), S(  0,  0), S(  0,+12), S(  0,+21),
                S(  0,+27), S(  0,+30), S(  0,+31), S(  0,+32)
            }
        };

        enum ThreatT { PIADA, MINOR, MAJOR, ROYAL, THREAT_NO };
        // PIECE_THREATEN[attacking][attacked] contains bonuses
        // according to which piece type attacks which one.
        const Score PIECE_THREATEN[THREAT_NO][NONE] =
        {
            { S(+ 0,+ 0), S(+80,+119), S(+80,+119), S(+117,+199), S(+127,+218), S(+ 0,+ 0) }, // Pawns
            { S(+ 0,+39), S(+32, +45), S(+32, +45), S(+ 41,+100), S(+ 41,+104), S(+ 0,+ 0) }, // Minor
            { S(+ 7,+28), S(+20, +49), S(+20, +49), S(+ 21,+ 50), S(+ 23,+ 52), S(+ 0,+ 0) }, // Major
            { S(+ 2,+58), S(+ 6,+125), S(+ 0,+  0), S(+  0,+  0), S(+  0,+  0), S(+ 0,+ 0) }  // Royal
        };
        
        const Score BISHOP_PAWNS            = S(+ 8,+12); // Penalty for bishop with more pawns on same color
        const Score BISHOP_TRAPPED          = S(+50,+40); // Penalty for bishop trapped with pawns

        const Score MINOR_BEHIND_PAWN       = S(+16,+ 0);

        const Score ROOK_ON_OPENFILE        = S(+43,+21); // Bonus for rook on open file
        const Score ROOK_ON_SEMIOPENFILE    = S(+19,+10); // Bonus for rook on semi-open file
        const Score ROOK_ON_PAWNS           = S(+10,+28); // Bonus for rook on pawns
        const Score ROOK_TRAPPED            = S(+92,+ 0); // Penalty for rook trapped
        
        const Score PIECE_HANGED            = S(+23,+20); // Bonus for each enemy hanged piece       

    #undef S

    #define V Value

        // OUTPOSTS[Square] contains bonus of outpost,
        // indexed by square (from white's point of view).
        const Value OUTPOSTS[2][SQ_NO] =
        {   // A      B      C      D      E      F      G      H

            // Knights
           {V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
            V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
            V( 0), V( 0), V( 4), V( 8), V( 8), V( 4), V( 0), V( 0),
            V( 0), V( 4), V(17), V(26), V(26), V(17), V( 4), V( 0),
            V( 0), V( 8), V(26), V(35), V(35), V(26), V( 8), V( 0),
            V( 0), V( 4), V(17), V(17), V(17), V(17), V( 4), V( 0),
            V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
            V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0)},
            // Bishops
           {V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
            V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
            V( 0), V( 0), V( 5), V( 5), V( 5), V( 5), V( 0), V( 0),
            V( 0), V( 5), V(10), V(10), V(10), V(10), V( 5), V( 0),
            V( 0), V(10), V(21), V(21), V(21), V(21), V(10), V( 0),
            V( 0), V( 5), V( 8), V( 8), V( 8), V( 8), V( 5), V( 0),
            V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
            V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0)}
        };

    #undef V

        // The SPACE_MASK[Color] contains the area of the board which is considered
        // by the space evaluation. In the middle game, each side is given a bonus
        // based on how many squares inside this area are safe and available for
        // friendly minor pieces.
        const Bitboard SPACE_MASK[CLR_NO] =
        {
            ((FC_bb | FD_bb | FE_bb | FF_bb) & (R2_bb | R3_bb | R4_bb)),
            ((FC_bb | FD_bb | FE_bb | FF_bb) & (R7_bb | R6_bb | R5_bb))
        };

        // King danger constants and variables. The king danger scores are taken
        // from the KING_DANGER[]. Various little "meta-bonuses" measuring
        // the strength of the enemy attack are added up into an integer, which
        // is used as an index to KING_DANGER[].
        const u08 MAX_ATTACK_UNITS = 100;
        // KING_DANGER[attack_units] contains the king danger weighted score
        // indexed by a calculated integer number.
        Score KING_DANGER[MAX_ATTACK_UNITS];

        // KING_ATTACK[PieceT] contains king attack weights by piece type
        const i32   KING_ATTACK[NONE] = { + 1, + 4, + 4, + 6, +10, + 1 };

        // Bonuses for safe checks
        const i32    SAFE_CHECK[NONE] = { + 0, + 3, + 2, + 8, +12, + 0 };

        // Bonuses for contact safe checks
        const i32 CONTACT_CHECK[NONE] = { + 0, + 0, + 3, +16, +24, + 0 };

        const ScaleFactor PAWN_SPAN_SCALE[2] = { ScaleFactor(38), ScaleFactor(56) };

        // weight_option() computes the value of an evaluation weight,
        // by combining UCI-configurable weights with an internal weight.
        inline Weight weight_option (i32 opt_value, const Score &internal_weight)
        {
            Weight weight =
            {
                opt_value * mg_value (internal_weight) / 1000,
                opt_value * eg_value (internal_weight) / 1000
            };
            return weight;
        }

        // apply_weight() weighs 'score' by factor 'weight' trying to prevent overflow
        inline Score apply_weight (Score score, const Weight &weight)
        {
            return mk_score (
                mg_value (score) * weight.mg / 0x100,
                eg_value (score) * weight.eg / 0x100);
        }

        //  --- init evaluation info --->
        template<Color Own>
        // init_evaluation<>() initializes king bitboards for given color adding
        // pawn attacks. To be done at the beginning of the evaluation.
        inline void init_evaluation (const Position &pos, EvalInfo &ei)
        {
            const Color  Opp = WHITE == Own ? BLACK : WHITE;
            const Delta PUSH = WHITE == Own ? DEL_N: DEL_S;

            Square fk_sq = pos.king_sq (Own);
            Square ek_sq = pos.king_sq (Opp);

            Bitboard pinneds = ei.pinneds[Own] = pos.pinneds (Own);
            ei.ful_attacked_by[Own][NONE] |= ei.ful_attacked_by[Own][PAWN] = ei.pi->pawns_attacks[Own];
            
            Bitboard pinned_pawns = pinneds & pos.pieces <PAWN> (Own);
            if (pinned_pawns != U64(0))
            {
                Bitboard free_pawns    = pos.pieces <PAWN> (Own) & ~pinned_pawns;
                Bitboard pawns_attacks = shift_del<WHITE == Own ? DEL_NE : DEL_SW> (free_pawns) |
                                         shift_del<WHITE == Own ? DEL_NW : DEL_SE> (free_pawns);
                while (pinned_pawns != U64(0))
                {
                    Square s = pop_lsq (pinned_pawns);
                    pawns_attacks |= PAWN_ATTACKS[Own][s] & RAY_LINE_bb[fk_sq][s];
                }
                ei.pin_attacked_by[Own][NONE] |= ei.pin_attacked_by[Own][PAWN] = pawns_attacks;
            }
            else
            {
                ei.pin_attacked_by[Own][NONE] |= ei.pin_attacked_by[Own][PAWN] = ei.pi->pawns_attacks[Own];
            }

            Bitboard king_attacks         = PIECE_ATTACKS[KING][ek_sq];
            ei.ful_attacked_by[Opp][KING] = king_attacks;
            ei.pin_attacked_by[Opp][KING] = king_attacks;
            
            ei.king_ring_attackers_count [Own] = 0;
            ei.king_ring_attackers_weight[Own] = 0;
            ei.king_zone_attacks_count   [Own] = 0;
            ei.king_ring                 [Opp] = U64(0);

            // Init king safety tables only if going to use them
            // Do not evaluate king safety when you are close to the endgame so the weight of king safety is small
            //if (ei.mi->game_phase > PHASE_KINGSAFETY)
            if (pos.non_pawn_material (Own) > VALUE_MG_QUEN + VALUE_MG_PAWN)
            {
                king_attacks += ek_sq;
                Rank ekr = rel_rank (Opp, ek_sq);
                ei.king_ring[Opp] = king_attacks|(DIST_RINGS_bb[ek_sq][1] &
                                                        (ekr < R_5 ? (PAWN_PASS_SPAN[Opp][ek_sq]) :
                                                         ekr < R_7 ? (PAWN_PASS_SPAN[Opp][ek_sq]|PAWN_PASS_SPAN[Own][ek_sq]) :
                                                                     (PAWN_PASS_SPAN[Own][ek_sq])));

                if (king_attacks & ei.pin_attacked_by[Own][PAWN])
                {
                    Bitboard attackers = pos.pieces<PAWN> (Own) & (king_attacks|(DIST_RINGS_bb[ek_sq][1] & (rank_bb (ek_sq-PUSH)|rank_bb (ek_sq))));
                    ei.king_ring_attackers_count [Own] += attackers != U64(0) ? (more_than_one (attackers) ? pop_count<MAX15> (attackers) : 1) : 0;
                    ei.king_ring_attackers_weight[Own] += ei.king_ring_attackers_count [Own]*KING_ATTACK[PAWN];
                }
            }
        }

        template<Color Own, PieceT PT, bool Trace>
        // evaluate_pieces<>() assigns bonuses and penalties to the pieces of a given color except PAWN
        inline Score evaluate_pieces (const Position &pos, EvalInfo &ei, Bitboard mobility_area, Score &mobility)
        {
            const Color  Opp = WHITE == Own ? BLACK : WHITE;
            const Delta PUSH = WHITE == Own ? DEL_N : DEL_S;

            Score score = SCORE_ZERO;

            ei.ful_attacked_by[Own][PT] = U64(0);
            ei.pin_attacked_by[Own][PT] = U64(0);
            
            u08 king_ring_attackers_count = 0;
            u08 king_zone_attacks_count   = 0;

            const Square *pl = pos.list<PT> (Own);
            Square s;
            while ((s = *pl++) != SQ_NO)
            {
                File f = _file (s);
                Rank r = rel_rank (Own, s);

                // Find attacked squares, including x-ray attacks for bishops and rooks
                Bitboard attacks =
                    (BSHP == PT) ? attacks_bb<BSHP> (s, (pos.pieces () ^ pos.pieces (Own, QUEN, BSHP)) | ei.pinneds[Own]) :
                    (ROOK == PT) ? attacks_bb<ROOK> (s, (pos.pieces () ^ pos.pieces (Own, QUEN, ROOK)) | ei.pinneds[Own]) :
                    (QUEN == PT) ? attacks_bb<BSHP> (s, pos.pieces ()) | attacks_bb<ROOK> (s, pos.pieces ()) :
                                   PIECE_ATTACKS[PT][s];

                ei.ful_attacked_by[Own][PT] |= attacks;

                if ((ei.king_ring[Opp] & attacks) != U64(0))
                {
                    ++king_ring_attackers_count;
                    Bitboard zone_attacks = ei.ful_attacked_by[Opp][KING] & attacks;
                    king_zone_attacks_count += zone_attacks != U64(0) ? (more_than_one (zone_attacks) ? pop_count<MAX15> (zone_attacks) : 1) : 0;
                }

                // Decrease score if attacked by an enemy pawn. Remaining part
                // of threat evaluation must be done later when have full attack info.
                if (ei.pin_attacked_by[Opp][PAWN] & s)
                {
                    score -= PIECE_THREATEN[PIADA][PT];
                }

                // Special extra evaluation for pieces
                
                if (NIHT == PT || BSHP == PT)
                {
                if (NIHT == PT)
                {
                    // Outpost bonus for Knight
                    if (!(ei.pin_attacked_by[Opp][PAWN] & s))
                    {
                        // Initial bonus based on square
                        Value value = OUTPOSTS[0][rel_sq (Own, s)];

                        // Increase bonus if supported by pawn, especially if the opponent has
                        // no minor piece which can exchange the outpost piece.
                        if (value != VALUE_ZERO)
                        {
                            // Supporting pawns
                            if (ei.pin_attacked_by[Own][PAWN] & s)
                            {
                                value *= (  (pos.pieces<NIHT> (Opp) & PIECE_ATTACKS[NIHT][s]) != U64(0)
                                         || (pos.pieces<BSHP> (Opp) & PIECE_ATTACKS[BSHP][s]) != U64(0)) ?
                                            1.50f : // If attacked by enemy Knights or Bishops
                                            (  (pos.pieces<NIHT> (Opp)) != U64(0)
                                            || (pos.pieces<BSHP> (Opp) & squares_of_color (s)) != U64(0)) ?
                                                1.75f : // If there are enemy Knights or Bishops
                                                2.50f;  // If there are no enemy Knights or Bishops
                            }

                            score += mk_score (value * 2, value / 2);
                        }
                    }
                }

                if (BSHP == PT)
                {
                    score -= BISHOP_PAWNS * ei.pi->pawns_on_squarecolor<Own> (s);

                    // Outpost bonus for Bishop
                    if (!(ei.pin_attacked_by[Opp][PAWN] & s))
                    {
                        // Initial bonus based on square
                        Value value = OUTPOSTS[1][rel_sq (Own, s)];

                        // Increase bonus if supported by pawn, especially if the opponent has
                        // no minor piece which can exchange the outpost piece.
                        if (value != VALUE_ZERO)
                        {
                            // Supporting pawns
                            if (ei.pin_attacked_by[Own][PAWN] & s)
                            {
                                value *= (  (pos.pieces<NIHT> (Opp) & PIECE_ATTACKS[NIHT][s]) != U64(0)
                                         || (pos.pieces<BSHP> (Opp) & PIECE_ATTACKS[BSHP][s]) != U64(0)) ?
                                            1.50f : // If attacked by enemy Knights or Bishops
                                            (  (pos.pieces<NIHT> (Opp)) != U64(0)
                                            || (pos.pieces<BSHP> (Opp) & squares_of_color (s)) != U64(0)) ?
                                                1.75f : // If there are enemy Knights or Bishops
                                                2.50f;  // If there are no enemy Knights or Bishops
                            }

                            score += mk_score (value * 2, value / 2);
                        }
                    }

                    // An important Chess960 pattern: A cornered bishop blocked by a friendly
                    // pawn diagonally in front of it is a very serious problem, especially
                    // when that pawn is also blocked.
                    // Penalty for a bishop on a1/h1 (a8/h8 for black) which is trapped by
                    // a friendly pawn on b2/g2 (b7/g7 for black).
                    if (pos.chess960 ())
                    {
                        if ((FILE_EDGE_bb & R1_bb) & rel_sq (Own, s))
                        {
                            Delta del = PUSH + ((F_A == f) ? DEL_E : DEL_W);
                            if (pos[s + del] == (Own | PAWN))
                            {
                                score -= BISHOP_TRAPPED *
                                    (  !pos.empty (s + del + PUSH) ?
                                       4 : pos[s + del + del] == (Own | PAWN) ?
                                       2 : 1);
                            }
                        }
                    }
                }

                // Bishop or knight behind a pawn
                if (  r < R_5
                   && pos.pieces<PAWN> () & (s + PUSH)
                   )
                {
                    score += MINOR_BEHIND_PAWN;
                }
                }

                if (ROOK == PT)
                {
                    
                    if (R_4 < r)
                    {
                        // Rook piece attacking enemy pawns on the same rank/file
                        Bitboard rook_on_pawns = pos.pieces<PAWN> (Opp) & PIECE_ATTACKS[ROOK][s];
                        score += rook_on_pawns != U64(0) ? ROOK_ON_PAWNS * (more_than_one (rook_on_pawns) ? pop_count<MAX15> (rook_on_pawns) : 1) : SCORE_ZERO;
                    }

                    // Give a bonus for a rook on a open or semi-open file
                    if (ei.pi->semiopen_file<Own> (f) != 0)
                    {
                        score += ei.pi->semiopen_file<Opp> (f) != 0 ?
                                 ROOK_ON_OPENFILE :
                                 ROOK_ON_SEMIOPENFILE;
                    }
                }

                if (ei.pinneds[Own] & s)
                {
                    attacks &= RAY_LINE_bb[pos.king_sq (Own)][s];
                }
                ei.pin_attacked_by[Own][PT] |= attacks;

                Bitboard mobile = attacks & mobility_area;
                i32 mob = mobile != U64(0) ? pop_count<QUEN != PT ? MAX15 : FULL> (mobile) : 0;
                mobility += PIECE_MOBILIZE[PT][mob];

                if (ROOK == PT)
                {
                    if (mob <= 3 && ei.pi->semiopen_file<Own> (f) == 0)
                    {
                        File kf = _file (pos.king_sq (Own));
                        Rank kr = rel_rank (Own, pos.king_sq (Own));
                        // Penalize rooks which are trapped by a king.
                        // Penalize more if the king has lost its castling capability.
                        if (  (kf < F_E) == (f < kf)
                           && (kr == R_1 || kr == r)
                           && (ei.pi->semiopen_side<Own> (kf, f < kf) == 0)
                           )
                        {
                            score -= (ROOK_TRAPPED - mk_score (22 * mob, 0)) * (1 + !pos.can_castle (Own));
                        }
                    }
                }
            }

            if (king_ring_attackers_count != 0)
            {
                ei.king_ring_attackers_count [Own] += king_ring_attackers_count;
                ei.king_ring_attackers_weight[Own] += king_ring_attackers_count*KING_ATTACK[PT];
                ei.king_zone_attacks_count   [Own] += king_zone_attacks_count;
            }

            if (Trace)
            {
                Tracer::set (PT, Own, score);
            }

            return score;
        }
        //  --- init evaluation info <---

        template<Color Own, bool Trace>
        // evaluate_king<>() assigns bonuses and penalties to a king of a given color
        inline Score evaluate_king (const Position &pos, EvalInfo &ei)
        {
            const Color Opp = WHITE == Own ? BLACK : WHITE;

            Square fk_sq = pos.king_sq (Own);

            // King shelter and enemy pawns storm
            ei.pi->evaluate_king_pawn_safety<Own> (pos);

            Value value = VALUE_ZERO;
            Rank kr = rel_rank (Own, fk_sq);
            // If can castle use the value after the castle if is bigger
            if (kr == R_1 && pos.can_castle (Own))
            {
                value = ei.pi->shelter_storm[Own][CS_NO];

                if (    pos.can_castle (Castling<Own, CS_K>::Right)
                    && !pos.castle_impeded (Castling<Own, CS_K>::Right)
                    && (pos.king_path (Castling<Own, CS_K>::Right) & ei.ful_attacked_by[Opp][NONE]) == U64(0)
                   )
                {
                    value = max (value, ei.pi->shelter_storm[Own][CS_K]);
                }
                if (    pos.can_castle (Castling<Own, CS_Q>::Right)
                    && !pos.castle_impeded (Castling<Own, CS_Q>::Right)
                    && (pos.king_path (Castling<Own, CS_Q>::Right) & ei.ful_attacked_by[Opp][NONE]) == U64(0)
                   )
                {
                    value = max (value, ei.pi->shelter_storm[Own][CS_Q]);
                }
            }
            else
            if (kr <= R_4)
            {
                value = ei.pi->shelter_storm[Own][CS_NO];
            }

            Score score = mk_score (value, -0x10 * ei.pi->kp_dist[Own]);

            // Main king safety evaluation
            if (ei.king_ring_attackers_count[Opp] != 0)
            {
                Bitboard occ = pos.pieces ();

                Bitboard king_ex_attacks =
                    ( ei.pin_attacked_by[Own][PAWN]
                    | ei.pin_attacked_by[Own][NIHT]
                    | ei.pin_attacked_by[Own][BSHP]
                    | ei.pin_attacked_by[Own][ROOK]
                    | ei.pin_attacked_by[Own][QUEN]);
                // Attacked squares around the king which has no defenders
                // apart from the king itself
                Bitboard undefended =
                    ei.ful_attacked_by[Own][KING] // King-zone
                  & ei.pin_attacked_by[Opp][NONE]
                  & ~king_ex_attacks;

                // Initialize the 'attack_units' variable, which is used later on as an
                // index to the KING_DANGER[] array. The initial value is based on the
                // number and types of the enemy's attacking pieces, the number of
                // attacked and undefended squares around our king, and the quality of
                // the pawn shelter (current 'mg score' value).
                i32 attack_units =
                    + min ((ei.king_ring_attackers_count[Opp]*ei.king_ring_attackers_weight[Opp])/4, 20U) // King-ring attacks
                    +  3 *  ei.king_zone_attacks_count[Opp] // King-zone attacks
                    +  3 * (undefended != U64(0) ? (more_than_one (undefended) ? pop_count<MAX15> (undefended) : 1) : 0) // King-zone undefended pieces
                    +  2 * (ei.pinneds[Own] != U64(0) ? (more_than_one (ei.pinneds[Own]) ? pop_count<MAX15> (ei.pinneds[Own]) : 1) : 0) // King pinned piece
                    - 15 * (pos.count<QUEN>(Opp) == 0)
                    - i32(value) / 32;

                // Undefended squares around king not occupied by enemy's
                undefended &= ~pos.pieces (Opp);
                if (undefended != U64(0))
                {
                    Bitboard undefended_attacked;
                    if (pos.count<QUEN> (Opp) > 0)
                    {
                        // Analyse enemy's safe queen contact checks.
                        // Undefended squares around the king attacked by enemy queen...
                        undefended_attacked = undefended & ei.pin_attacked_by[Opp][QUEN];
                        Bitboard unsafe = ei.ful_attacked_by[Opp][PAWN]|ei.ful_attacked_by[Opp][NIHT]|ei.ful_attacked_by[Opp][BSHP]|ei.ful_attacked_by[Opp][ROOK]|ei.ful_attacked_by[Opp][KING];
                        while (undefended_attacked != U64(0))
                        {
                            Square sq = pop_lsq (undefended_attacked);
                            if (  (unsafe & sq)
                               || (  pos.count<QUEN> (Opp) > 1
                                  && more_than_one (pos.pieces<QUEN> (Opp) & (PIECE_ATTACKS[BSHP][sq]|PIECE_ATTACKS[ROOK][sq]))
                                  && more_than_one (pos.pieces<QUEN> (Opp) & (attacks_bb<BSHP> (sq, occ ^ pos.pieces<QUEN> (Opp))|attacks_bb<ROOK> (sq, occ ^ pos.pieces<QUEN> (Opp))))
                                  )
                               )
                            {
                                attack_units += CONTACT_CHECK[QUEN];
                            }
                        }
                    }
                    if (pos.count<ROOK> (Opp) > 0)
                    {
                        // Analyse enemy's safe rook contact checks.
                        // Undefended squares around the king attacked by enemy rooks...
                        undefended_attacked = undefended & ei.pin_attacked_by[Opp][ROOK];
                        // Consider only squares where the enemy rook gives check
                        undefended_attacked &= PIECE_ATTACKS[ROOK][fk_sq];
                        Bitboard unsafe = ei.ful_attacked_by[Opp][PAWN]|ei.ful_attacked_by[Opp][NIHT]|ei.ful_attacked_by[Opp][BSHP]|ei.ful_attacked_by[Opp][QUEN]|ei.ful_attacked_by[Opp][KING];
                        while (undefended_attacked != U64(0))
                        {
                            Square sq = pop_lsq (undefended_attacked);
                            if (  (unsafe & sq)
                               || (  pos.count<ROOK> (Opp) > 1
                                  && more_than_one (pos.pieces<ROOK> (Opp) & PIECE_ATTACKS[ROOK][sq])
                                  && more_than_one (pos.pieces<ROOK> (Opp) & attacks_bb<ROOK> (sq, occ ^ pos.pieces<ROOK> (Opp)))
                                  )
                               )
                            {
                                attack_units += CONTACT_CHECK[ROOK];
                            }
                        }
                    }
                    if (pos.count<BSHP> (Opp) > 0)
                    {
                        // Analyse enemy's safe rook contact checks.
                        // Undefended squares around the king attacked by enemy bishop...
                        undefended_attacked = undefended & ei.pin_attacked_by[Opp][BSHP];
                        // Consider only squares where the enemy bishop gives check
                        undefended_attacked &= PIECE_ATTACKS[BSHP][fk_sq];
                        Bitboard unsafe = ei.ful_attacked_by[Opp][PAWN]|ei.ful_attacked_by[Opp][NIHT]|ei.ful_attacked_by[Opp][ROOK]|ei.ful_attacked_by[Opp][QUEN]|ei.ful_attacked_by[Opp][KING];
                        while (undefended_attacked != U64(0))
                        {
                            Square sq = pop_lsq (undefended_attacked);
                            Bitboard bishops = U64(0);
                            if (  (unsafe & sq)
                               || (  pos.count<BSHP> (Opp) > 1
                                  && (bishops = pos.pieces<BSHP> (Opp) & squares_of_color (sq)) != U64(0)
                                  && more_than_one (bishops)
                                  && more_than_one (bishops & PIECE_ATTACKS[BSHP][sq])
                                  && more_than_one (bishops & attacks_bb<BSHP> (sq, occ ^ pos.pieces<BSHP> (Opp)))
                                  )
                               )
                            {
                                attack_units += CONTACT_CHECK[BSHP];
                            }
                        }
                    }

                    // Knight can't give contact check but safe distance check
                }

                // Analyse the enemies safe distance checks for sliders and knights
                Bitboard safe_area = ~(pos.pieces (Opp) | ei.pin_attacked_by[Own][NONE]);
                Bitboard rook_check = (ei.pin_attacked_by[Opp][ROOK]|ei.pin_attacked_by[Opp][QUEN]) != U64(0) ? attacks_bb<ROOK> (fk_sq, occ) & safe_area : U64(0);
                Bitboard bshp_check = (ei.pin_attacked_by[Opp][BSHP]|ei.pin_attacked_by[Opp][QUEN]) != U64(0) ? attacks_bb<BSHP> (fk_sq, occ) & safe_area : U64(0);

                // Enemies safe-checks
                Bitboard safe_check;
                // Queens safe-checks
                safe_check = (rook_check | bshp_check) & ei.pin_attacked_by[Opp][QUEN];
                if (safe_check != U64(0)) attack_units += SAFE_CHECK[QUEN] * (more_than_one (safe_check) ? pop_count<MAX15> (safe_check) : 1);
                // Rooks safe-checks
                safe_check = rook_check & ei.pin_attacked_by[Opp][ROOK];
                if (safe_check != U64(0)) attack_units += SAFE_CHECK[ROOK] * (more_than_one (safe_check) ? pop_count<MAX15> (safe_check) : 1);
                // Bishops safe-checks
                safe_check = bshp_check & ei.pin_attacked_by[Opp][BSHP];
                if (safe_check != U64(0)) attack_units += SAFE_CHECK[BSHP] * (more_than_one (safe_check) ? pop_count<MAX15> (safe_check) : 1);
                // Knights safe-checks
                safe_check = PIECE_ATTACKS[NIHT][fk_sq] & safe_area & ei.pin_attacked_by[Opp][NIHT];
                if (safe_check != U64(0)) attack_units += SAFE_CHECK[NIHT] * (more_than_one (safe_check) ? pop_count<MAX15> (safe_check) : 1);

                // To index KING_DANGER[] attack_units must be in [0, MAX_ATTACK_UNITS-1] range
                attack_units = min (max (attack_units, 0), MAX_ATTACK_UNITS-1);

                // Finally, extract the king danger score from the KING_DANGER[]
                // array and subtract the score from evaluation.
                score -= KING_DANGER[attack_units];

            }

            if (Trace)
            {
                Tracer::set (KING, Own, score);
            }

            return score;
        }

        template<Color Own, bool Trace>
        // evaluate_threats<>() assigns bonuses according to the type of attacking piece
        // and the type of attacked one.
        inline Score evaluate_threats (const Position &pos, const EvalInfo &ei)
        {
            const Color Opp = WHITE == Own ? BLACK : WHITE;

            Bitboard enemies = pos.pieces (Opp);

            // Enemies protected by pawn and attacked by minors
            Bitboard protected_pieces = 
                   (enemies ^ pos.pieces<PAWN> (Opp))
                &  ei.pin_attacked_by[Opp][PAWN]
                & (ei.pin_attacked_by[Own][NIHT]|ei.pin_attacked_by[Own][BSHP]);

            // Enemies not defended by pawn and attacked by any piece
            Bitboard weak_pieces = 
                   enemies
                & ~ei.pin_attacked_by[Opp][PAWN]
                &  ei.pin_attacked_by[Own][NONE];
            
            Score score = SCORE_ZERO;

            if (protected_pieces != U64(0))
            {
                score += ((protected_pieces & pos.pieces<QUEN> ()) != U64(0) ? PIECE_THREATEN[MINOR][QUEN] :
                          (protected_pieces & pos.pieces<ROOK> ()) != U64(0) ? PIECE_THREATEN[MINOR][ROOK] :
                          (protected_pieces & pos.pieces<BSHP> ()) != U64(0) ? PIECE_THREATEN[MINOR][BSHP] :
                          (protected_pieces & pos.pieces<NIHT> ()) != U64(0) ? PIECE_THREATEN[MINOR][NIHT] : PIECE_THREATEN[MINOR][PAWN]);
            }

            // Add a bonus according if the attacking pieces are minor or major
            if (weak_pieces != U64(0))
            {
                // Threaten pieces
                Bitboard threaten_pieces;
                // Threaten pieces by Minor
                threaten_pieces = weak_pieces & (ei.pin_attacked_by[Own][NIHT]|ei.pin_attacked_by[Own][BSHP]);
                if (threaten_pieces != U64(0))
                {
                    score += ((threaten_pieces & pos.pieces<QUEN> ()) != U64(0) ? PIECE_THREATEN[MINOR][QUEN] :
                              (threaten_pieces & pos.pieces<ROOK> ()) != U64(0) ? PIECE_THREATEN[MINOR][ROOK] :
                              (threaten_pieces & pos.pieces<BSHP> ()) != U64(0) ? PIECE_THREATEN[MINOR][BSHP] :
                              (threaten_pieces & pos.pieces<NIHT> ()) != U64(0) ? PIECE_THREATEN[MINOR][NIHT] : PIECE_THREATEN[MINOR][PAWN]);
                }
                // Threaten pieces by Major
                threaten_pieces = weak_pieces & (ei.pin_attacked_by[Own][ROOK]|ei.pin_attacked_by[Own][QUEN]);
                if (threaten_pieces != U64(0))
                {
                    score += ((threaten_pieces & pos.pieces<QUEN> ()) != U64(0) ? PIECE_THREATEN[MAJOR][QUEN] :
                              (threaten_pieces & pos.pieces<ROOK> ()) != U64(0) ? PIECE_THREATEN[MAJOR][ROOK] :
                              (threaten_pieces & pos.pieces<BSHP> ()) != U64(0) ? PIECE_THREATEN[MAJOR][BSHP] :
                              (threaten_pieces & pos.pieces<NIHT> ()) != U64(0) ? PIECE_THREATEN[MAJOR][NIHT] : PIECE_THREATEN[MAJOR][PAWN]);
                }

                // Threaten pieces by King
                threaten_pieces = weak_pieces & ei.ful_attacked_by[Own][KING];
                if (threaten_pieces != U64(0)) score += (more_than_one (threaten_pieces) ? PIECE_THREATEN[ROYAL][1] : PIECE_THREATEN[ROYAL][0]); 

                // Hanged pieces
                Bitboard hanged_pieces = weak_pieces & ~ei.pin_attacked_by[Opp][NONE];
                if (hanged_pieces != U64(0)) score += PIECE_HANGED * (more_than_one (hanged_pieces) ? pop_count<MAX15> (hanged_pieces) : 1);
            }

            if (Trace)
            {
                Tracer::set (Tracer::THREAT, Own, score);
            }

            return score;
        }

        template<Color Own>
        // evaluate_passed_pawns<>() evaluates the passed pawns of the given color
        inline Score evaluate_passed_pawns (const Position &pos, const EvalInfo &ei)
        {
            const Color Opp  = WHITE == Own ? BLACK : WHITE;
            const Delta PUSH = WHITE == Own ? DEL_N : DEL_S;

            bool piece_majority = pos.count<NONPAWN>(Own) > pos.count<NONPAWN>(Opp);

            Score score = SCORE_ZERO;

            Bitboard passed_pawns = ei.pi->passed_pawns[Own];
            while (passed_pawns != U64(0))
            {
                Square s = pop_lsq (passed_pawns);
                ASSERT (pos.passed_pawn (Own, s));
                
                i32 r = max (i32(rel_rank (Own, s)) - i32(R_2), 1);
                i32 rr = r * (r - 1);

                // Base bonus depends on rank
                Value mg_value = Value(17 * (rr + 0*r + 0));
                Value eg_value = Value( 7 * (rr + 1*r + 1));
                Square block_sq = s + PUSH;

                if (rr != 0)
                {
                    Square fk_sq = pos.king_sq (Own);
                    Square ek_sq = pos.king_sq (Opp);

                    // Adjust bonus based on kings proximity
                    eg_value += 
                        + (5 * rr * SQR_DIST[ek_sq][block_sq])
                        - (2 * rr * SQR_DIST[fk_sq][block_sq]);
                    // If block square is not the queening square then consider also a second push
                    if (rel_rank (Own, block_sq) != R_8)
                    {
                        eg_value -= (1 * rr * SQR_DIST[fk_sq][block_sq + PUSH]);
                    }

                    bool pinned = (ei.pinneds[Own] & s);
                    if (pinned)
                    {
                        // Only one real pinner exist other are fake pinner
                        Bitboard pawn_pinners = pos.pieces (Opp) & RAY_LINE_bb[fk_sq][s] &
                            ( (attacks_bb<ROOK> (s, pos.pieces ()) & pos.pieces (ROOK, QUEN))
                            | (attacks_bb<BSHP> (s, pos.pieces ()) & pos.pieces (BSHP, QUEN))
                            );
                        pinned = !(BETWEEN_SQRS_bb[fk_sq][scan_lsq (pawn_pinners)] & block_sq);
                    }

                    // If the pawn is free to advance, increase bonus
                    if (!pinned && pos.empty (block_sq))
                    {
                        // Squares to queen
                        Bitboard front_squares = FRONT_SQRS_bb[Own][s];
                        Bitboard behind_majors = FRONT_SQRS_bb[Opp][s] & pos.pieces (ROOK, QUEN) & attacks_bb<ROOK> (s, pos.pieces ());
                        Bitboard unsafe_squares = front_squares
                            ,      safe_squares = front_squares;
                        // If there is an enemy rook or queen attacking the pawn from behind,
                        // add all X-ray attacks by the rook or queen. Otherwise consider only
                        // the squares in the pawn's path attacked or occupied by the enemy.
                        if ((behind_majors & pos.pieces (Opp)) == U64(0))
                        {
                            unsafe_squares &= (ei.pin_attacked_by[Opp][NONE]|pos.pieces());
                        }

                        if ((behind_majors & pos.pieces (Own)) == U64(0))
                        {
                              safe_squares &= (ei.pin_attacked_by[Own][NONE]);
                        }

                        // Give a big bonus if there aren't enemy attacks, otherwise
                        // a smaller bonus if block square is not attacked.
                        i32 k = unsafe_squares != U64(0) ?
                                    unsafe_squares & block_sq ?
                                        0 : 9 : 15;

                        if (safe_squares != U64(0))
                        {
                            // Give a big bonus if the path to queen is fully defended,
                            // a smaller bonus if at least block square is defended.
                            k += safe_squares == front_squares ?
                                    6 : safe_squares & block_sq ?
                                        4 : 0;

                            // If the block square is defended by a pawn add more small bonus.
                            if (ei.ful_attacked_by[Own][PAWN] & block_sq) k += 1;
                        }

                        if (k != 0)
                        {
                            mg_value += k*rr;
                            eg_value += k*rr;
                        }
                    }
                    else
                    {
                        if (pinned || (pos.pieces (Own) & block_sq))
                        {
                            mg_value += 3*rr + 2*r + 3;
                            eg_value += 1*rr + 2*r + 0;
                        }
                    }
                
                }

                // Increase the bonus if more non-pawn pieces
                if (piece_majority)
                {
                    eg_value += eg_value / 4;
                }

                score += mk_score (mg_value, eg_value);
            }
            
            return score;
        }

        template<Color Own>
        // evaluate_space_activity<>() computes the space evaluation for a given side. The
        // space evaluation is a simple bonus based on the number of safe squares
        // available for minor pieces on the central four files on ranks 2--4. Safe
        // squares one, two or three squares behind a friendly pawn are counted
        // twice. Finally, the space bonus is scaled by a weight taken from the
        // material hash table. The aim is to improve play on game opening.
        inline i32 evaluate_space_activity (const Position &pos, const EvalInfo &ei)
        {
            const Color Opp = WHITE == Own ? BLACK : WHITE;

            // Find the safe squares for our pieces inside the area defined by
            // SPACE_MASK[]. A square is unsafe if it is attacked by an enemy
            // pawn, or if it is undefended and attacked by an enemy piece.
            Bitboard safe_space =
                  SPACE_MASK[Own]
                & ~pos.pieces<PAWN> (Own)
                & ~ei.pin_attacked_by[Opp][PAWN]
                & (ei.pin_attacked_by[Own][NONE]|~ei.pin_attacked_by[Opp][NONE]);

            // Since SPACE_MASK[Own] is fully on our half of the board
            ASSERT (u32(safe_space >> (WHITE == Own ? 32 : 00)) == 0);

            // Find all squares which are at most three squares behind some friendly pawn
            Bitboard behind = pos.pieces<PAWN> (Own);
            behind |= shift_del<WHITE == Own ? DEL_S  : DEL_N > (behind);
            behind |= shift_del<WHITE == Own ? DEL_SS : DEL_NN> (behind);

            // Count safe_space + (behind & safe_space) with a single pop_count
            return pop_count<FULL> ((WHITE == Own ? safe_space << 32 : safe_space >> 32) | (behind & safe_space));
        }

        template<bool Trace>
        // evaluate<>()
        inline Value evaluate (const Position &pos)
        {
            ASSERT (pos.checkers () == U64(0));

            Thread *thread = pos.thread ();

            EvalInfo ei;
            // Probe the material hash table
            ei.mi  = Material::probe (pos, thread->matl_table);

            // If have a specialized evaluation function for the current material
            // configuration, call it and return.
            if (ei.mi->specialized_eval_exists ())
            {
                return ei.mi->evaluate (pos) + TEMPO;
            }

            // Score is computed from the point of view of white.
            Score score;

            // Initialize score by reading the incrementally updated scores included
            // in the position object (material + piece square tables) and adding Tempo bonus. 
            score  = pos.psq_score ();
            score += ei.mi->matl_score;

            // Probe the pawn hash table
            ei.pi  = Pawns::probe (pos, thread->pawn_table);
            score += apply_weight (ei.pi->pawn_score, Weights[PAWN_STRUCTURE]);

            ei.ful_attacked_by[WHITE][NONE] = ei.pin_attacked_by[WHITE][NONE] = U64(0);
            ei.ful_attacked_by[BLACK][NONE] = ei.pin_attacked_by[BLACK][NONE] = U64(0);
            // Initialize attack and king safety bitboards
            init_evaluation<WHITE> (pos, ei);
            init_evaluation<BLACK> (pos, ei);

            // Evaluate pieces and mobility
            Score mobility_w = SCORE_ZERO
                , mobility_b = SCORE_ZERO;
            // Do not include in mobility squares occupied by friend pawns or king or protected by enemy pawns 
            Bitboard mobility_area_w = ~pos.pieces (WHITE, PAWN, KING) & ~ei.pin_attacked_by[BLACK][NONE]
                   , mobility_area_b = ~pos.pieces (BLACK, PAWN, KING) & ~ei.pin_attacked_by[WHITE][NONE];

            score += 
                + evaluate_pieces<WHITE, NIHT, Trace> (pos, ei, mobility_area_w, mobility_w)
                - evaluate_pieces<BLACK, NIHT, Trace> (pos, ei, mobility_area_b, mobility_b);
            ei.ful_attacked_by[WHITE][NONE] |= ei.ful_attacked_by[WHITE][NIHT];
            ei.ful_attacked_by[BLACK][NONE] |= ei.ful_attacked_by[BLACK][NIHT];
            ei.pin_attacked_by[WHITE][NONE] |= ei.pin_attacked_by[WHITE][NIHT];
            ei.pin_attacked_by[BLACK][NONE] |= ei.pin_attacked_by[BLACK][NIHT];

            score += 
                + evaluate_pieces<WHITE, BSHP, Trace> (pos, ei, mobility_area_w, mobility_w)
                - evaluate_pieces<BLACK, BSHP, Trace> (pos, ei, mobility_area_b, mobility_b);
            ei.ful_attacked_by[WHITE][NONE] |= ei.ful_attacked_by[WHITE][BSHP];
            ei.ful_attacked_by[BLACK][NONE] |= ei.ful_attacked_by[BLACK][BSHP];
            ei.pin_attacked_by[WHITE][NONE] |= ei.pin_attacked_by[WHITE][BSHP];
            ei.pin_attacked_by[BLACK][NONE] |= ei.pin_attacked_by[BLACK][BSHP];

            score += 
                + evaluate_pieces<WHITE, ROOK, Trace> (pos, ei, mobility_area_w, mobility_w)
                - evaluate_pieces<BLACK, ROOK, Trace> (pos, ei, mobility_area_b, mobility_b);
            ei.ful_attacked_by[WHITE][NONE] |= ei.ful_attacked_by[WHITE][ROOK];
            ei.ful_attacked_by[BLACK][NONE] |= ei.ful_attacked_by[BLACK][ROOK];
            ei.pin_attacked_by[WHITE][NONE] |= ei.pin_attacked_by[WHITE][ROOK];
            ei.pin_attacked_by[BLACK][NONE] |= ei.pin_attacked_by[BLACK][ROOK];

            mobility_area_w &= ~ei.pin_attacked_by[BLACK][NONE];
            mobility_area_b &= ~ei.pin_attacked_by[WHITE][NONE];

            score += 
                + evaluate_pieces<WHITE, QUEN, Trace> (pos, ei, mobility_area_w, mobility_w)
                - evaluate_pieces<BLACK, QUEN, Trace> (pos, ei, mobility_area_b, mobility_b);
            ei.ful_attacked_by[WHITE][NONE] |= ei.ful_attacked_by[WHITE][QUEN];
            ei.ful_attacked_by[BLACK][NONE] |= ei.ful_attacked_by[BLACK][QUEN];
            ei.pin_attacked_by[WHITE][NONE] |= ei.pin_attacked_by[WHITE][QUEN];
            ei.pin_attacked_by[BLACK][NONE] |= ei.pin_attacked_by[BLACK][QUEN];

            ei.ful_attacked_by[WHITE][NONE] |= ei.ful_attacked_by[WHITE][KING];
            ei.ful_attacked_by[BLACK][NONE] |= ei.ful_attacked_by[BLACK][KING];
            ei.pin_attacked_by[WHITE][NONE] |= ei.pin_attacked_by[WHITE][KING];
            ei.pin_attacked_by[BLACK][NONE] |= ei.pin_attacked_by[BLACK][KING];

            // Weight mobility
            score += apply_weight (mobility_w - mobility_b, Weights[PIECE_MOBILITY]);

            // Evaluate kings after all other pieces because needed complete attack
            // information when computing the king safety evaluation.
            score +=
                + evaluate_king<WHITE, Trace> (pos, ei)
                - evaluate_king<BLACK, Trace> (pos, ei);

            // Evaluate tactical threats, needed full attack information including king
            score += 
                + evaluate_threats<WHITE, Trace> (pos, ei)
                - evaluate_threats<BLACK, Trace> (pos, ei);

            // Evaluate passed pawns, needed full attack information including king
            Score passed_pawn_w = evaluate_passed_pawns<WHITE> (pos, ei)
                , passed_pawn_b = evaluate_passed_pawns<BLACK> (pos, ei);
            // Weight passed pawns
            score += apply_weight (passed_pawn_w - passed_pawn_b, Weights[PASSED_PAWN]);

            Value npm_w = pos.non_pawn_material (WHITE)
                , npm_b = pos.non_pawn_material (BLACK);

            // If one side has only a king, score for potential unstoppable pawns
            if (npm_b == VALUE_ZERO)
            {
                score += ei.pi->evaluate_unstoppable_pawns<WHITE> ();
            }
            if (npm_w == VALUE_ZERO)
            {
                score -= ei.pi->evaluate_unstoppable_pawns<BLACK> ();
            }

            Phase game_phase = ei.mi->game_phase;
            ASSERT (PHASE_ENDGAME <= game_phase && game_phase <= PHASE_MIDGAME);

            // Evaluate space for both sides, only in middle-game.
            i32 space_w = SCORE_ZERO
              , space_b = SCORE_ZERO;
            Score space_weight = ei.mi->space_weight;
            if (space_weight != SCORE_ZERO)
            {
                space_w = evaluate_space_activity<WHITE> (pos, ei);
                space_b = evaluate_space_activity<BLACK> (pos, ei);

                score += apply_weight ((space_w - space_b)*space_weight, Weights[SPACE_ACTIVITY]);
            }

            // In case of tracing add each evaluation contributions for both white and black
            if (Trace)
            {
                Tracer::set (PAWN             , ei.pi->pawn_score);
                Tracer::set (Tracer::MATERIAL , pos.psq_score ());
                Tracer::set (Tracer::IMBALANCE, ei.mi->matl_score);

                Tracer::set (Tracer::MOBILITY
                    , apply_weight (mobility_w, Weights[PIECE_MOBILITY])
                    , apply_weight (mobility_b, Weights[PIECE_MOBILITY]));

                Tracer::set (Tracer::PASSER
                    , apply_weight (passed_pawn_w, Weights[PASSED_PAWN])
                    , apply_weight (passed_pawn_b, Weights[PASSED_PAWN]));

                Tracer::set (Tracer::SPACE
                    , apply_weight (space_w != 0 ? space_w * space_weight : SCORE_ZERO, Weights[SPACE_ACTIVITY])
                    , apply_weight (space_b != 0 ? space_b * space_weight : SCORE_ZERO, Weights[SPACE_ACTIVITY]));

                Tracer::set (Tracer::TOTAL    , score);

            }

            // --------------------------------------------------

            i32 mg = i32(mg_value (score));
            i32 eg = i32(eg_value (score));
            ASSERT (-VALUE_INFINITE < mg && mg < +VALUE_INFINITE);
            ASSERT (-VALUE_INFINITE < eg && eg < +VALUE_INFINITE);

            Color strong_side = (eg > VALUE_DRAW) ? WHITE : BLACK;
            // Scale winning side if position is more drawish than it appears
            ScaleFactor scale_fac = (strong_side == WHITE) ?
                ei.mi->scale_factor<WHITE> (pos) :
                ei.mi->scale_factor<BLACK> (pos);

            // If don't already have an unusual scale factor, check for opposite
            // colored bishop endgames, and use a lower scale for those.
            if (  game_phase < PHASE_MIDGAME
               && (scale_fac == SCALE_FACTOR_NORMAL || scale_fac == SCALE_FACTOR_ONEPAWN)
               )
            {
                if (pos.opposite_bishops ())
                {
                    // Both sides with opposite-colored bishops only ignoring any pawns.
                    if (  npm_w == VALUE_MG_BSHP
                       && npm_b == VALUE_MG_BSHP
                       )
                    {
                        // It is almost certainly a draw even with pawns.
                        i32 pawn_diff = abs (pos.count<PAWN> (WHITE) - pos.count<PAWN> (BLACK));
                        scale_fac = pawn_diff == 0 ? ScaleFactor (4) : ScaleFactor (8 * pawn_diff);
                    }
                    // Both sides with opposite-colored bishops, but also other pieces. 
                    else
                    {
                        // Still a bit drawish, but not as drawish as with only the two bishops.
                        scale_fac = ScaleFactor (50 * i32(scale_fac) / i32(SCALE_FACTOR_NORMAL));
                    }
                }
                else
                if (  abs (eg) <= VALUE_EG_BSHP
                   && ei.pi->pawn_span[strong_side] <= 1
                   && !pos.passed_pawn (~strong_side, pos.king_sq (~strong_side))
                   )
                {
                    // Endings where weaker side can place his king in front of the strong side pawns are drawish.
                    scale_fac = PAWN_SPAN_SCALE[ei.pi->pawn_span[strong_side]];
                }
            }

            // Interpolates between a middle game and a (scaled by 'scale_fac') endgame score, based on game phase.
            eg = eg * i32(scale_fac) / i32(SCALE_FACTOR_NORMAL);
            
            Value value = Value((mg * i32(game_phase) + eg * i32(PHASE_MIDGAME - game_phase)) / i32(PHASE_MIDGAME));

            return (WHITE == pos.active () ? +value : -value) + TEMPO;
        }

        namespace Tracer {

            string trace (const Position &pos)
            {
                fill (*Scores, *Scores + sizeof (Scores) / sizeof (**Scores), SCORE_ZERO);

                Value value = evaluate<true> (pos);
                value = WHITE == pos.active () ? +value : -value; // White's point of view

                stringstream ss;

                ss  << showpoint << showpos << setprecision (2) << fixed
                    << "         Entity |    White    |    Black    |     Total    \n"
                    << "                |   MG    EG  |   MG    EG  |   MG    EG   \n"
                    << "----------------+-------------+-------------+--------------\n";
                write (ss, "Material"  , MATERIAL);
                write (ss, "Imbalance" , IMBALANCE);
                write (ss, "Pawn"      , PAWN);
                write (ss, "Knight"    , NIHT);
                write (ss, "Bishop"    , BSHP);
                write (ss, "Rook"      , ROOK);
                write (ss, "Queen"     , QUEN);
                write (ss, "King"      , KING);
                write (ss, "Mobility"  , MOBILITY);
                write (ss, "Threat"    , THREAT);
                write (ss, "Passer"    , PASSER);
                write (ss, "Space"     , SPACE);
                ss  << "----------------+-------------+-------------+--------------\n";
                write (ss, "Total"     , TOTAL);
                ss  << "\n"
                    << "Evaluation: " << value_to_cp (value) << " (white side)\n";

                return ss.str ();
            }
        }

    }

    // evaluate() is the main evaluation function.
    // It always computes two values, an endgame value and a middle game value, in score
    // and interpolates between them based on the remaining material.
    Value evaluate (const Position &pos)
    {
        return evaluate<false> (pos);
    }

    // trace() is like evaluate() but instead of a value returns a string suitable
    // to be print on stdout with the detailed descriptions and values of each
    // evaluation term. Used mainly for debugging.
    string trace   (const Position &pos)
    {
        return Tracer::trace (pos);
    }

    // initialize() computes evaluation weights from the corresponding UCI parameters
    // and setup king danger tables.
    void initialize ()
    {
        Weights[PIECE_MOBILITY] = weight_option (1000 , INTERNAL_WEIGHTS[PIECE_MOBILITY ]);
        Weights[PAWN_STRUCTURE] = weight_option (1000 , INTERNAL_WEIGHTS[PAWN_STRUCTURE ]);
        Weights[PASSED_PAWN   ] = weight_option (1000 , INTERNAL_WEIGHTS[PASSED_PAWN    ]);
        Weights[SPACE_ACTIVITY] = weight_option (1000 , INTERNAL_WEIGHTS[SPACE_ACTIVITY]);
        Weights[KING_SAFETY   ] = weight_option (1000 , INTERNAL_WEIGHTS[KING_SAFETY   ]);

        const i32 MAX_SLOPE  =   30;
        const i32 PEAK_VALUE = 1280;

        KING_DANGER[0] = SCORE_ZERO;
        i32 mg = 0;
        for (u08 i = 1; i < MAX_ATTACK_UNITS; ++i)
        {
            mg = min (min (i32(0.4*i*i), mg + MAX_SLOPE), PEAK_VALUE);
            KING_DANGER[i] = apply_weight (mk_score (mg, 0), Weights[KING_SAFETY]);
        }
    }

}
