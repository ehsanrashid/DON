#include "Evaluator.h"

#include <iomanip>
#include <sstream>
#include <algorithm>

#include "Material.h"
#include "Pawns.h"
#include "MoveGenerator.h"
#include "Thread.h"
#include "UCI.h"

namespace Evaluator {

    using namespace std;
    using namespace BitBoard;
    using namespace MoveGenerator;
    using namespace Threads;

    namespace {

        // Struct EvalInfo contains various information computed and collected
        // by the evaluation functions.
        struct EvalInfo
        {
            // Pointers to material and pawn hash table entries
            Material::Entry *mi;
            Pawns   ::Entry *pi;

            // attacked_by[Color][PieceT] contains all squares attacked by a given color and piece type,
            // attacked_by[Color][NONE] contains all squares attacked by the given color.
            Bitboard attacked_by[CLR_NO][TOTL];

            // king_ring[Color] is the zone around the king which is considered
            // by the king safety evaluation. This consists of the squares directly
            // adjacent to the king, and the three (or two, for a king on an edge file)
            // squares two ranks in front of the king. For instance, if black's king
            // is on g8, king_ring[BLACK] is a bitboard containing the squares f8, h8,
            // f7, g7, h7, f6, g6 and h6.
            Bitboard king_ring[CLR_NO];

            // king_attackers_count[Color] is the number of pieces of the given color
            // which attack a square in the king_ring of the enemy king.
            u08 king_attackers_count[CLR_NO];

            // king_attackers_weight[Color] is the sum of the "weight" of the pieces of the
            // given color which attack a square in the king_ring of the enemy king. The weights
            // of the individual piece types are given by the variables KingAttackWeight[PieceT]
            i32 king_attackers_weight[CLR_NO];

            // king_zone_attacks_count[Color] is the number of attacks to squares
            // directly adjacent to the king of the given color. Pieces which attack
            // more than one square are counted multiple times. For instance, if black's
            // king is on g8 and there's a white knight on g5, this knight adds
            // 2 to king_zone_attacks_count[BLACK].
            i32 king_zone_attacks_count[CLR_NO];

            Bitboard pinned_pieces[CLR_NO];

        };

        namespace Tracer {

            // Used for tracing
            enum TermT
            {
                PST = 6, IMBALANCE, MOBILITY, THREAT, PASSED, SPACE, TOTAL, TERM_NO
            };

            Score       Terms[CLR_NO][TERM_NO];

            EvalInfo    Evalinfo;
            ScaleFactor Scalefactor;

            inline double value_to_cp (const Value &value) { return double (value) / double (VALUE_EG_PAWN); }

            inline void add_term (u08 term, Score w_score, Score b_score = SCORE_ZERO)
            {
                Terms[WHITE][term] = w_score;
                Terms[BLACK][term] = b_score;
            }

            inline void format_row (stringstream &ss, const string &name, u08 term)
            {
                Score score[CLR_NO] =
                {
                    Terms[WHITE][term],
                    Terms[BLACK][term]
                };

                switch (term)
                {
                case PST: case IMBALANCE: case PAWN: case TOTAL:
                    ss  << setw (20) << name << " |  ----  ---- |  ----  ---- | " << showpos
                        << setw ( 5) << value_to_cp (mg_value (score[WHITE] - score[BLACK])) << " "
                        << setw ( 5) << value_to_cp (eg_value (score[WHITE] - score[BLACK])) << "\n";
                    break;

                default:
                    ss  << setw (20) << name << " | " << showpos
                        << setw ( 5) << value_to_cp (mg_value (score[WHITE])) << " "
                        << setw ( 5) << value_to_cp (eg_value (score[WHITE])) << " | "
                        << setw ( 5) << value_to_cp (mg_value (score[BLACK])) << " "
                        << setw ( 5) << value_to_cp (eg_value (score[BLACK])) << " | "
                        << setw ( 5) << value_to_cp (mg_value (score[WHITE] - score[BLACK])) << " "
                        << setw ( 5) << value_to_cp (eg_value (score[WHITE] - score[BLACK])) << "\n";
                    break;
                }
            }

            string trace (const Position &pos);

        }

        // Evaluation weights, initialized from UCI options
        enum EvalWeightT { Mobility, PawnStructure, PassedPawns, Space, KingSafety };
        
        struct Weight { i32 mg, eg; };

        Weight Weights[5];

#define S(mg, eg) mk_score (mg, eg)

        // Internal evaluation weights. These are applied on top of the evaluation
        // weights read from UCI parameters. The purpose is to be able to change
        // the evaluation weights while keeping the default values of the UCI
        // parameters at 100, which looks prettier.
        const Score InternalWeights[5] =
        {
            S(+289,+344), // Mobility
            S(+233,+201), // PawnStructure
            S(+221,+273), // PassedPawns
            S(+ 46,+  0), // Space
            S(+289,+  0)  // KingSafety
        };

        // MobilityBonus[PieceT][attacked] contains bonuses for middle and end game,
        // indexed by piece type and number of attacked squares not occupied by friendly pieces.
        const Score MobilityBonus[NONE][28] =
        {
            {},
            // Knights
            {
                S(-65,-50), S(-42,-30), S(- 9,-10), S(+ 3,  0), S(+15,+10),
                S(+27,+20), S(+37,+28), S(+42,+31), S(+44,+33)
            },
            // Bishops
            {
                S(-52,-47), S(-28,-23), S(+ 6,+ 1), S(+20,+15), S(+34,+29),
                S(+48,+43), S(+60,+55), S(+68,+63), S(+74,+68), S(+77,+72),
                S(+80,+75), S(+82,+77), S(+84,+79), S(+86,+81)
            },
            // Rooks
            {
                S(-47,- 52), S(-31,- 26), S(- 5,   0), S(+ 1,+ 16), S(+ 7,+ 32),
                S(+13,+ 48), S(+18,+ 64), S(+22,+ 80), S(+26,+ 96), S(+29,+109),
                S(+31,+115), S(+33,+119), S(+35,+122), S(+36,+123), S(+37,+124),
            },
            // Queens
            {
                S(-42,-40), S(-28,-23), S(- 5,- 7), S(  0,  0), S(+ 6,+10),
                S(+10,+19), S(+14,+29), S(+18,+38), S(+20,+40), S(+21,+41),
                S(+22,+41), S(+22,+41), S(+22,+41), S(+23,+41), S(+24,+41),
                S(+25,+41), S(+25,+41), S(+25,+41), S(+25,+41), S(+25,+41),
                S(+25,+41), S(+25,+41), S(+25,+41), S(+25,+41), S(+25,+41),
                S(+25,+41), S(+25,+41), S(+25,+41)
            },
            {}
        };

        // OutpostBonus[PieceT][Square] contains bonuses of knights and bishops,
        // indexed by piece type and square (from white's point of view).
        const Score OutpostBonus[2][SQ_NO] =
        {       //  A         B         C         D         E         F         G         H

            // Knights
            {
                S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0),
                S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0),
                S( 0, 0), S( 0, 0), S( 4, 4), S( 8, 8), S( 8, 8), S( 4, 4), S( 0, 0), S( 0, 0),
                S( 0, 0), S( 4, 4), S(17,17), S(26,26), S(26,26), S(17,17), S( 4, 4), S( 0, 0),
                S( 0, 0), S( 8, 8), S(26,26), S(35,35), S(35,35), S(26,26), S( 8, 8), S( 0, 0),
                S( 0, 0), S( 4, 4), S(17,17), S(17,17), S(17,17), S(17,17), S( 4, 4), S( 0, 0),
                S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0),
                S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0)
            },
            // Bishops
            {
                S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0),
                S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0),
                S( 0, 0), S( 0, 0), S( 5, 5), S( 5, 5), S( 5, 5), S( 5, 5), S( 0, 0), S( 0, 0),
                S( 0, 0), S( 5, 5), S(10,10), S(10,10), S(10,10), S(10,10), S( 5, 5), S( 0, 0),
                S( 0, 0), S(10,10), S(21,21), S(21,21), S(21,21), S(21,21), S(10,10), S( 0, 0),
                S( 0, 0), S( 5, 5), S( 8, 8), S( 8, 8), S( 8, 8), S( 8, 8), S( 5, 5), S( 0, 0),
                S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0),
                S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0), S( 0, 0)
            }
        };

        // ThreatBonus[attacking][attacked] contains bonuses according to
        // which piece type attacks which one.
        const Score ThreatBonus[NONE][TOTL] =
        {
            { S(+ 0,+ 0), S(+24,+49), S(+24,+49), S(+36,+96), S(+41,+104) }, // Pawn
            { S(+ 7,+39), S(+ 0,+ 0), S(+25,+49), S(+36,+96), S(+41,+104) }, // Knight
            { S(+ 7,+39), S(+23,+49), S(+ 0,+ 0), S(+36,+96), S(+41,+104) }, // Bishop
            { S(+10,+39), S(+15,+45), S(+15,+45), S(+ 0,+ 0), S(+24,+ 52) }, // Rook
            { S(+10,+39), S(+15,+45), S(+15,+45), S(+18,+48), S(+ 0,+  0) }, // Queen
            {}
        };

        // PawnThreatenPenalty[PieceT] contains a penalty according to
        // which piece type is attacked by an enemy pawn.
        const Score PawnThreatenPenalty[NONE] =
        {
            S(+ 0,+ 0), S(+56,+70), S(+56,+70), S(+76,+99), S(+86,+118), S(+ 0,+ 0)
        };


        const Score TempoBonus              = S(+24,+11); // Bonus for tempo

        const Score CenterAttacksBonus      = S(+ 2,+ 0); // Bonus for center attacks

        //const Score KnightPawnsBonus        = S(+ 1,+ 2); // Bonus for knight with pawns
        //const Score KnightWingPawnsPenalty  = S(+ 5,+15); // Penalty for knight with pawns on wing

        const Score BishopPawnsPenalty      = S(+16,+28); // Penalty for bishop with pawns on color
        const Score BishopWingPawnsBonus    = S(+ 5,+20); // Bonus for bishop with pawns on wing
        const Score BishopTrappedPenalty    = S(+50,+40);

        const Score RookOnPawnBonus         = S(+10,+28); // Bonus for rook on pawns
        const Score RookOnOpenFileBonus     = S(+43,+21); // Bonus for rook on open file
        const Score RookOnSemiOpenFileBonus = S(+19,+10); // Bonus for rook on semi-open file
        const Score RookTrappedPenalty      = S(+90,+ 5); // Penalty for rook trapped
        
        const Score PawnUnstoppableBonus    = S(+ 0,+20); // Bonus for pawn going to promote
        //const Score NonPawnMaterialBonus    = S(+40,+40); // Bonus for non-pawn material

        // Bonus for each enemy hanging piece [side to move]
        const Score PieceHangingBonus[CLR_NO] = { S(23, 20) , S(35, 45) };        

#undef S

        // The SpaceMask[Color] contains the area of the board which is considered
        // by the space evaluation. In the middle game, each side is given a bonus
        // based on how many squares inside this area are safe and available for
        // friendly minor pieces.
        const Bitboard SpaceMask[CLR_NO] =
        {
            (FC_bb | FD_bb | FE_bb | FF_bb) & (R2_bb | R3_bb | R4_bb),
            (FC_bb | FD_bb | FE_bb | FF_bb) & (R7_bb | R6_bb | R5_bb),
        };

        // King danger constants and variables. The king danger scores are taken
        // from the KingDanger[]. Various little "meta-bonuses" measuring
        // the strength of the enemy attack are added up into an integer, which
        // is used as an index to KingDanger[].
        //
        // KingAttackWeight[PieceT] contains king attack weights by piece type
        const i32   KingAttackWeight[NONE] = { 0, + 2, + 2, + 3, + 5, 0 };

        // Bonuses for enemy's safe checks
        const i32    SafeCheckWeight[NONE] = { 0, + 3, + 2, + 8, +12, 0 };

        // Bonuses for enemy's contact safe checks
        const i32 ContactCheckWeight[NONE] = { 0, + 0, + 0, +16, +24, 0 };

        const u08 MAX_ATTACK_UNITS = 100;
        // KingDanger[attack_units] contains the king danger weighted score
        // indexed by a calculated integer number.
        Score KingDanger[MAX_ATTACK_UNITS];


        // apply_weight() weighs 'score' by factor 'weight' trying to prevent overflow
        inline Score apply_weight (const Score &score, const Weight &weight)
        {
            return mk_score (
                mg_value (score) * weight.mg / 0x100,
                eg_value (score) * weight.eg / 0x100);
        }

        // weight_option() computes the value of an evaluation weight, by combining
        // two UCI-configurable weights (midgame and endgame) with an internal weight.
        inline Weight weight_option (const string &mg_opt, const string &eg_opt, const Score &internal_weight)
        {
            Weight weight =
            {
                i32 (Options[mg_opt]) * mg_value (internal_weight) / 100, // =mg
                i32 (Options[eg_opt]) * eg_value (internal_weight) / 100  // =eg
            };
            return weight;
        }

        //  --- init evaluation info --->
        template<Color C>
        // init_eval_info<>() initializes king bitboards for given color adding
        // pawn attacks. To be done at the beginning of the evaluation.
        inline void init_eval_info (const Position &pos, EvalInfo &ei)
        {
            const Color  C_ = (WHITE == C) ? BLACK : WHITE;

            const Square ek_sq  = pos.king_sq (C_);
            ei.pinned_pieces[C] = pos.pinneds (C);

            ei.attacked_by[C][NONE] = ei.attacked_by[C][PAWN] = ei.pi->pawn_attacks[C];

            Bitboard attacks = ei.attacked_by[C_][KING] = PieceAttacks[KING][ek_sq];

            // Init king safety tables only if going to use them
            if (   (pos.count<QUEN> (C) > 0) 
                && (pos.non_pawn_material (C) > VALUE_MG_QUEN + VALUE_MG_PAWN)
               )
            {
                ei.king_ring              [C_] = attacks | shift_del<(WHITE == C) ? DEL_S : DEL_N> (attacks);
                                                 //DistanceRings[ek_sq][0]|DistanceRings[ek_sq][1];
                attacks                       &= ei.attacked_by [C ][PAWN];
                ei.king_attackers_count   [C ] = attacks != U64 (0) ? pop_count<MAX15> (attacks) : 0;
                ei.king_zone_attacks_count[C ] = 0;
                ei.king_attackers_weight  [C ] = 0;
            }
            else
            {
                ei.king_ring              [C_] = U64 (0);
                ei.king_attackers_count   [C ] = 0;
                //ei.king_zone_attacks_count[C ] = 0;
                //ei.king_attackers_weight  [C ] = 0;
            }
        }

        template<Color C, PieceT PT>
        // evaluate_outposts<>() evaluates bishop and knight outposts squares
        inline Score evaluate_outposts (const Position &pos, EvalInfo &ei, Square s)
        {
            ASSERT (BSHP == PT || NIHT == PT);

            const Color C_ = (WHITE == C) ? BLACK : WHITE;
            
            // Initial bonus based on square
            Score score = 
                (NIHT == PT) ? OutpostBonus[0][rel_sq (C, s)] :
                (BSHP == PT) ? OutpostBonus[1][rel_sq (C, s)] :
                SCORE_ZERO;

            // Increase bonus if supported by pawn, especially if the opponent has
            // no minor piece which can exchange the outpost piece.
            if (score != SCORE_ZERO)
            {
                if (ei.attacked_by[C][PAWN] & s)
                {
                    if (   (pos.pieces<NIHT> (C_) == U64 (0))
                        && ((pos.pieces<BSHP> (C_) & squares_of_color (s)) == U64 (0))
                       )
                    {
                        score *= 2.5;
                    }
                    else
                    {
                        score *= 1.5;
                    }
                }
            }

            return score;
        }

        template<Color C, PieceT PT, bool Trace>
        // evaluate_piece<>() assigns bonuses and penalties to the pieces of a given color except PAWN
        inline Score evaluate_piece (const Position &pos, EvalInfo &ei, const Bitboard &mobility_area, Score &mobility)
        {
            const Color  C_      = (WHITE == C) ? BLACK : WHITE;
            const Square fk_sq   = pos.king_sq (C);
            const Bitboard occ   = pos.pieces ();
            const Bitboard pinned_pieces = ei.pinned_pieces[C];
            //const Bitboard pawn_span     = ei.pi->semiopen_files[C_] ^ 0xFF;

            ei.attacked_by[C][PT] = U64 (0);
            
            Score score = SCORE_ZERO;

            const Square *pl = pos.list<PT> (C);
            Square s;
            while ((s = *pl++) != SQ_NO)
            {
                // Find attacked squares, including x-ray attacks for bishops and rooks
                Bitboard attacks =
                    (BSHP == PT) ? attacks_bb<BSHP> (s, (occ ^ pos.pieces (C, QUEN, BSHP)) | pinned_pieces) :
                    (ROOK == PT) ? attacks_bb<ROOK> (s, (occ ^ pos.pieces (C, QUEN, ROOK)) | pinned_pieces) :
                    (QUEN == PT) ? attacks_bb<BSHP> (s, (occ ^ pos.pieces (C, QUEN, BSHP)) | pinned_pieces)
                                 | attacks_bb<ROOK> (s, (occ ^ pos.pieces (C, QUEN, ROOK)) | pinned_pieces) :
                    PieceAttacks[PT][s];

                if (pinned_pieces & s)
                {
                    attacks &= LineRay_bb[fk_sq][s];
                }

                ei.attacked_by[C][NONE] |= ei.attacked_by[C][PT] |= attacks;

                score += CenterAttacksBonus * i32 (pop_count <MAX15> (Center_bb[C] & attacks));

                if (attacks & ei.king_ring[C_])
                {
                    ei.king_attackers_count [C]++;
                    ei.king_attackers_weight[C] += KingAttackWeight[PT];

                    Bitboard attacks_king = (attacks & ei.attacked_by[C_][KING]);
                    if (attacks_king != U64 (0))
                    {
                        ei.king_zone_attacks_count[C] += pop_count<MAX15> (attacks_king);
                    }
                }

                // Decrease score if attacked by an enemy pawn. Remaining part
                // of threat evaluation must be done later when have full attack info.
                if (ei.attacked_by[C_][PAWN] & s)
                {
                    score -= PawnThreatenPenalty[PT];
                }

                // Special extra evaluation for pieces

                if (NIHT == PT || BSHP == PT)
                {
                    if (NIHT == PT)
                    {
                        //const Bitboard knight_pawns = pos.pieces<PAWN> (C) & (DistanceRings[s][0]|DistanceRings[s][1]);
                        //if (knight_pawns != U64 (0))
                        //{
                        //    score += KnightPawnsBonus * i32 (pop_count<MAX15> (knight_pawns));
                        //}

                        //if (pos.count<PAWN> (C_) > 1)
                        //{
                        //    Bitboard pawns = pos.pieces<PAWN> (C_);
                        //    if (     pawns & WingABC_bb
                        //        &&   pawns & WingFGH_bb
                        //       )
                        //    {
                        //        score -= KnightWingPawnsPenalty;
                        //    }
                        //}
                    }

                    if (BSHP == PT)
                    {
                        if (pos.count<PAWN> (C) > 1)
                        {
                            Bitboard pawns = pos.pieces<PAWN> (C);

                            // Penalty for bishop with same coloured front pawns
                            Bitboard bishop_front_pawns = PieceAttacks[BSHP][s] & FrontRank_bb[C][_rank (s)] & pawns;
                            if (bishop_front_pawns != U64 (0))
                            {
                                score -= BishopPawnsPenalty * i32 (pop_count<MAX15> (bishop_front_pawns & attacks_bb<BSHP> (s, pos.pieces <PAWN> ())));
                            }
                            
                            if (   pawns & WingABC_bb
                                && pawns & WingFGH_bb
                                && !(pawns & WingDE_bb)
                               )
                            {
                                score += BishopWingPawnsBonus;
                            }
                        }

                        Square rsq = rel_sq (C, s);

                        if (   rsq == SQ_A7
                            || rsq == SQ_H7
                           )
                        {
                            const Piece opp_pawn = (C_ | PAWN);
                            Delta del = pawn_push (C_) + ((F_A == _file (s)) ? DEL_E : DEL_W);
                            if (   (pos[s + del] == opp_pawn)
                                && (ei.attacked_by[C_][PAWN] & ~ei.attacked_by[C][PAWN] & (s + del))
                               )
                            {
                                score -= BishopTrappedPenalty * 2;
                            }
                        }
                        if (   rsq == SQ_A6
                            || rsq == SQ_H6
                           )
                        {
                            const Piece opp_pawn = (C_ | PAWN);
                            Delta del = pawn_push (C_) + ((F_A == _file (s)) ? DEL_E : DEL_W);
                            if (   (pos[s + del] == opp_pawn)
                                && (ei.attacked_by[C_][PAWN] & ~ei.attacked_by[C][PAWN] & (s + del))
                               )
                            {
                                score -= BishopTrappedPenalty;
                            }
                        }

                        // An important Chess960 pattern: A cornered bishop blocked by a friendly
                        // pawn diagonally in front of it is a very serious problem, especially
                        // when that pawn is also blocked.
                        // Penalty for a bishop on a1/h1 (a8/h8 for black) which is trapped by
                        // a friendly pawn on b2/g2 (b7/g7 for black).
                        if (pos.chess960 ())
                        {
                            if (   rsq == SQ_A1
                                || rsq == SQ_H1
                               )
                            {
                                const Piece own_pawn = (C | PAWN);
                                Delta del = pawn_push (C) + ((F_A == _file (s)) ? DEL_E : DEL_W);
                                if (pos[s + del] == own_pawn)
                                {
                                    score -= BishopTrappedPenalty *
                                        ( (pos[s + del + pawn_push (C)]!=EMPTY) ? 4
                                        : (pos[s + del + del] == own_pawn)      ? 2 : 1);
                                }
                            }
                        }

                    }

                    // Outposts squares for knight and bishop 
                    if ((pos.pieces<PAWN> (C_) & PawnAttackSpan[C][s]) == U64 (0))
                    {
                        score += evaluate_outposts<C, PT> (pos, ei, s);
                    }
                }

                if (ROOK == PT)
                {
                    attacks &= ~( ei.attacked_by[C_][NIHT]
                                | ei.attacked_by[C_][BSHP]
                                );

                    Rank r = rel_rank (C, s);
                    if (R_4 <= r)
                    {
                        // Rook piece attacking enemy pawns on the same rank/file
                        const Bitboard rook_on_enemy_pawns = pos.pieces<PAWN> (C_) & PieceAttacks[ROOK][s];
                        if (rook_on_enemy_pawns != U64 (0))
                        {
                            score += RookOnPawnBonus * i32 (pop_count<MAX15> (rook_on_enemy_pawns));
                        }
                    }
                }

                if (QUEN == PT)
                {
                    attacks &= ~( ei.attacked_by[C_][NIHT]
                                | ei.attacked_by[C_][BSHP]
                                | ei.attacked_by[C_][ROOK]
                                );
                }

                u08 mob = pop_count<(QUEN != PT) ? MAX15 : FULL> (attacks & mobility_area);
                mobility += MobilityBonus[PT][mob];

                if (ROOK == PT)
                {
                    // Give a bonus for a rook on a open or semi-open file
                    if (ei.pi->semiopen_file<C > (_file (s)) != 0)
                    {
                        score += (ei.pi->semiopen_file<C_> (_file (s)) != 0)
                               ? RookOnOpenFileBonus
                               : RookOnSemiOpenFileBonus;
                    }
                    else
                    {
                        if (mob <= 3)
                        {
                            const File kf = _file (fk_sq);
                            const Rank kr = rel_rank (C, fk_sq);
                            // Penalize rooks which are trapped by a king. Penalize more if the
                            // king has lost its castling capability.
                            if (  ((kf < F_E) == (_file (s) < kf))
                               && (_rank (s) == _rank (fk_sq) || R_1 == kr)
                               && (ei.pi->semiopen_side<C> (kf, _file (s) < kf) == 0)
                               )
                            {
                                score -= (RookTrappedPenalty - mk_score (8 * mob, 0)) * (1 + i32 (R_1 == kr && !pos.can_castle (C)));
                            }
                        }
                    }

                }
            }

            if (Trace)
            {
                Tracer::Terms[C][PT] = score;
            }

            return score;
        }
        //  --- init evaluation info <---

        template<Color C, bool Trace>
        // evaluate_king<>() assigns bonuses and penalties to a king of a given color
        inline Score evaluate_king (const Position &pos, const EvalInfo &ei)
        {
            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            Square fk_sq = pos.king_sq (C);
            // King shelter and enemy pawns storm
            Score score = ei.pi->king_safety<C> (pos, fk_sq);

            // Main king safety evaluation
            if (ei.king_attackers_count[C_] != 0)
            {
                // Find the attacked squares around the king which has no defenders
                // apart from the king itself
                Bitboard undefended =
                    ei.attacked_by[C_][NONE]
                  & ei.attacked_by[C][KING]
                  & ~(ei.attacked_by[C][PAWN]
                    | ei.attacked_by[C][NIHT]
                    | ei.attacked_by[C][BSHP]
                    | ei.attacked_by[C][ROOK]
                    | ei.attacked_by[C][QUEN]);

                // Initialize the 'attack_units' variable, which is used later on as an
                // index to the KingDanger[] array. The initial value is based on the
                // number and types of the enemy's attacking pieces, the number of
                // attacked and undefended squares around our king, and the quality of
                // the pawn shelter (current 'score' value).
                i32 attack_units =
                    + min (ei.king_attackers_count[C_] * ei.king_attackers_weight[C_] / 2, 20)
                    + 3 * (ei.king_zone_attacks_count[C_])                      // King-zone attacker piece weight
                    + (undefended != U64 (0) ? 3 * (pop_count<MAX15> (undefended)) : 0) // King-zone undefended piece weight
                    + (ei.pinned_pieces[C] != U64 (0) ? 2 * pop_count<MAX15> (ei.pinned_pieces[C]) : 0) // King-pinned piece weight
                    - mg_value (score) / 32;

                // Undefended squares around king not occupied by enemy's
                undefended &= ~pos.pieces (C_);

                Bitboard undefended_attacked;
                // Analyse enemy's safe queen contact checks. First find undefended
                // squares around the king attacked by enemy queen...
                undefended_attacked = undefended & ei.attacked_by[C_][QUEN];
                if (undefended_attacked != U64 (0))
                {
                    // ...then remove squares not supported by another enemy piece
                    undefended_attacked &=
                      ( ei.attacked_by[C_][PAWN]
                      | ei.attacked_by[C_][NIHT]
                      | ei.attacked_by[C_][BSHP]
                      | ei.attacked_by[C_][ROOK]);

                    if (undefended_attacked != U64 (0))
                    {
                        attack_units += 
                            ContactCheckWeight[QUEN]
                          * pop_count<MAX15> (undefended_attacked)
                          * (C_ == pos.active () ? 2 : 1);
                    }
                }

                // Analyse enemy's safe rook contact checks. First find undefended
                // squares around the king attacked by enemy rooks...
                undefended_attacked = undefended & ei.attacked_by[C_][ROOK];
                // Consider only squares where the enemy rook gives check
                undefended_attacked &= PieceAttacks[ROOK][fk_sq];
                if (undefended_attacked != U64 (0))
                {
                    // ...and then remove squares not supported by another enemy piece
                    undefended_attacked &=
                      ( ei.attacked_by[C_][PAWN]
                      | ei.attacked_by[C_][NIHT]
                      | ei.attacked_by[C_][BSHP]
                      | ei.attacked_by[C_][QUEN]);

                    if (undefended_attacked != U64 (0))
                    {
                        attack_units +=
                            ContactCheckWeight[ROOK]
                          * pop_count<MAX15> (undefended_attacked)
                          * (C_ == pos.active () ? 2 : 1);
                    }
                }

                const Bitboard occ = pos.pieces ();
                // Analyse the enemy's safe distance checks for sliders and knights
                Bitboard safe_sq = ~(pos.pieces (C_) | ei.attacked_by[C][NONE]);
                
                Bitboard rook_check = attacks_bb<ROOK> (fk_sq, occ) & safe_sq;
                Bitboard bshp_check = attacks_bb<BSHP> (fk_sq, occ) & safe_sq;

                Bitboard safe_check;
                // Enemy queen safe checks
                safe_check = (rook_check | bshp_check) & ei.attacked_by[C_][QUEN];
                if (safe_check != U64 (0)) attack_units += SafeCheckWeight[QUEN] * pop_count<MAX15> (safe_check);

                // Enemy rooks safe checks
                safe_check = rook_check & ei.attacked_by[C_][ROOK];
                if (safe_check != U64 (0)) attack_units += SafeCheckWeight[ROOK] * pop_count<MAX15> (safe_check);

                // Enemy bishops safe checks
                safe_check = bshp_check & ei.attacked_by[C_][BSHP];
                if (safe_check != U64 (0)) attack_units += SafeCheckWeight[BSHP] * pop_count<MAX15> (safe_check);

                // Enemy knights safe checks
                safe_check = PieceAttacks[NIHT][fk_sq] & safe_sq & ei.attacked_by[C_][NIHT];
                if (safe_check != U64 (0)) attack_units += SafeCheckWeight[NIHT] * pop_count<MAX15> (safe_check);

                // To index KingDanger[] attack_units must be in [0, MAX_ATTACK_UNITS] range
                if (attack_units <  0               ) attack_units =  0;
                if (attack_units >= MAX_ATTACK_UNITS) attack_units = MAX_ATTACK_UNITS-1;

                // Finally, extract the king danger score from the KingDanger[]
                // array and subtract the score from evaluation.
                score -= KingDanger[attack_units];
            }

            // King mobility is good in the endgame
            Bitboard mobility = ei.attacked_by[C][KING] & ~(ei.attacked_by[C_][NONE]);
            u08 mob = pop_count<MAX15> (mobility);
            if (mob < 3) score -= mk_score (0, 8 * (3 - mob));

            if (Trace)
            {
                Tracer::Terms[C][KING] = score;
            }

            return score;
        }

        template<Color C, bool Trace>
        // evaluate_threats<>() assigns bonuses according to the type of attacking piece
        // and the type of attacked one.
        inline Score evaluate_threats (const Position &pos, const EvalInfo &ei)
        {
            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            // Enemies under our attack
            const Bitboard attacked_enemies = 
                  pos.pieces (C_) 
                & ei.attacked_by[C][NONE];

            // Enemies under our attack and not defended by a pawn
            const Bitboard weak_enemies = 
                  attacked_enemies 
                & ~ei.attacked_by[C_][PAWN];
            
            Score score = SCORE_ZERO;

            // Add a bonus according if the attacking pieces are minor or major
            if (weak_enemies != U64 (0))
            {
                for (i08 pt = PAWN; pt < KING; ++pt)
                {
                    Bitboard threaten_enemies = weak_enemies & ei.attacked_by[C][pt];
                    while (threaten_enemies != U64 (0))
                    {
                        score += ThreatBonus[pt][ptype (pos[pop_lsq (threaten_enemies)])];
                    }
                }

                // Hanging piece
                Bitboard hanging_enemies = weak_enemies & ~ei.attacked_by[C_][NONE];
                if (hanging_enemies != U64 (0))
                {
                    score += PieceHangingBonus[C == pos.active ()] * i32 (pop_count<MAX15> (hanging_enemies));
                }
            }

            if (Trace)
            {
                Tracer::Terms[C][Tracer::THREAT] = score;
            }

            return score;
        }

        template<Color C, bool Trace>
        // evaluate_passed_pawns<>() evaluates the passed pawns of the given color
        inline Score evaluate_passed_pawns (const Position &pos, const EvalInfo &ei)
        {
            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            Score score = SCORE_ZERO;

            Bitboard passed_pawns = ei.pi->passed_pawns[C];
            while (passed_pawns != U64 (0))
            {
                Square s = pop_lsq (passed_pawns);

                ASSERT (pos.passed_pawn (C, s));

                i32 r = i32 (rel_rank (C, s)) - i32 (R_2);
                i32 rr = r * (r - 1);

                // Base bonus depends on rank
                Value mg_bonus = Value (20 * rr);
                Value eg_bonus = Value (10 * (rr + r + 1));

                if (rr != 0)
                {
                    Square block_sq = s + pawn_push (C);
                    Square fk_sq = pos.king_sq (C );
                    Square ek_sq = pos.king_sq (C_);

                    // Adjust bonus based on kings proximity
                    eg_bonus += (5 * rr * SquareDist[ek_sq][block_sq])
                             -  (2 * rr * SquareDist[fk_sq][block_sq]);

                    // If block_sq is not the queening square then consider also a second push
                    if (rel_rank (C, block_sq) != R_8)
                    {
                        eg_bonus -= (rr * SquareDist[fk_sq][block_sq + pawn_push (C)]);
                    }

                    // If the pawn is free to advance, increase bonus
                    if (pos.empty (block_sq))
                    {
                        const Bitboard occ = pos.pieces ();
                        // squares to queen
                        Bitboard queen_squares = FrontSqrs_bb[C][s];
                        
                        Bitboard unsafe_squares;
                        // If there is an enemy rook or queen attacking the pawn from behind,
                        // add all X-ray attacks by the rook or queen. Otherwise consider only
                        // the squares in the pawn's path attacked or occupied by the enemy.
                        if (UNLIKELY (FrontSqrs_bb[C_][s] & pos.pieces (C_, ROOK, QUEN))
                                  && (FrontSqrs_bb[C_][s] & pos.pieces (C_, ROOK, QUEN) & attacks_bb<ROOK> (s, occ)))
                        {
                            unsafe_squares = queen_squares;
                        }
                        else
                        {
                            unsafe_squares = queen_squares & (ei.attacked_by[C_][NONE] | pos.pieces (C_));
                        }

                        Bitboard defended_squares;
                        if (UNLIKELY (FrontSqrs_bb[C_][s] & pos.pieces (C, ROOK, QUEN))
                                  && (FrontSqrs_bb[C_][s] & pos.pieces (C, ROOK, QUEN) & attacks_bb<ROOK> (s, occ)))
                        {
                            defended_squares = queen_squares;
                        }
                        else
                        {
                            defended_squares = queen_squares & ei.attacked_by[C][NONE];
                        }

                        // Give a big bonus if there aren't enemy attacks, otherwise
                        // a smaller bonus if block square is not attacked.
                        i32 k = (unsafe_squares == U64 (0)) ? 15 : !(unsafe_squares & block_sq) ? 9 : 0;

                        // Give a big bonus if the path to queen is fully defended,
                        // a smaller bonus if at least block square is defended.
                        k += (defended_squares == queen_squares) ? 6 : (defended_squares & block_sq) ? 4 : 0;

                        mg_bonus += k * rr;
                        eg_bonus += k * rr;
                    }
                }

                // Increase the bonus if the passed pawn is supported by a friendly pawn
                // on the same rank and a bit smaller if it's on the previous rank.
                Bitboard supporting_pawns = pos.pieces<PAWN> (C) & AdjFile_bb[_file (s)];
                if (supporting_pawns != U64 (0))
                {
                    if (supporting_pawns & rank_bb (s))
                    {
                        eg_bonus += Value (r * 20);
                    }
                    else if (supporting_pawns & rank_bb (s - pawn_push (C)))
                    {
                        eg_bonus += Value (r * 12);
                    }
                }

                if (eg_bonus > VALUE_ZERO)
                {
                    // Increase the bonus if have more non-pawn pieces
                    if (pos.count<NONPAWN> (C) > pos.count<NONPAWN> (C_))
                    {
                        eg_bonus += eg_bonus / 4;
                    }
                }

                score += mk_score (mg_bonus, eg_bonus);
            }
            
            if (score != SCORE_ZERO)
            {
                // Add the scores to the middle game and endgame eval
                score = apply_weight (score, Weights[PassedPawns]);
            }

            if (Trace)
            {
                Tracer::Terms[C][Tracer::PASSED] = score;
            }
            
            return score;
        }

        template<Color C>
        // evaluate_unstoppable_pawns<>() scores the most advanced among the passed and
        // candidate pawns. In case opponent has no pieces but pawns, this is somewhat
        // related to the possibility pawns are unstoppable.
        inline Score evaluate_unstoppable_pawns (const Position &, const EvalInfo &ei)
        {
            Bitboard unstoppable_pawns = ei.pi->passed_pawns[C] | ei.pi->candidate_pawns[C];
            return (unstoppable_pawns != U64 (0)) ?
                PawnUnstoppableBonus * i32 (rel_rank (C, scan_frntmost_sq (C, unstoppable_pawns))) :
                SCORE_ZERO;
        }

        template<Color C>
        // evaluate_space<>() computes the space evaluation for a given side. The
        // space evaluation is a simple bonus based on the number of safe squares
        // available for minor pieces on the central four files on ranks 2--4. Safe
        // squares one, two or three squares behind a friendly pawn are counted
        // twice. Finally, the space bonus is scaled by a weight taken from the
        // material hash table. The aim is to improve play on game opening.
        inline i32 evaluate_space (const Position &pos, const EvalInfo &ei)
        {
            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            // Find the safe squares for our pieces inside the area defined by
            // SpaceMask[]. A square is unsafe if it is attacked by an enemy
            // pawn, or if it is undefended and attacked by an enemy piece.
            Bitboard safe_space =
                  SpaceMask[C]
                & ~pos.pieces<PAWN> (C)
                & ~ei.attacked_by[C_][PAWN]
                & (ei.attacked_by[C ][NONE]
                | ~ei.attacked_by[C_][NONE]);

            // Since SpaceMask[C] is fully on our half of the board
            ASSERT (u32 (safe_space >> ((WHITE == C) ? 32 : 0)) == 0);

            // Find all squares which are at most three squares behind some friendly pawn
            Bitboard behind = pos.pieces<PAWN> (C);
            behind |= shift_del<(WHITE == C) ? DEL_S  : DEL_N > (behind);
            behind |= shift_del<(WHITE == C) ? DEL_SS : DEL_NN> (behind);

            // Count safe_space + (behind & safe_space) with a single pop_count
            return pop_count<FULL> (((WHITE == C) ? safe_space << 32 : safe_space >> 32) | (behind & safe_space));
        }

        template<bool Trace>
        // evaluate()
        inline Value evaluate (const Position &pos)
        {
            ASSERT (pos.checkers () == U64 (0));

            Thread *thread = pos.thread ();

            EvalInfo ei;
            // Probe the material hash table
            ei.mi = Material::probe (pos, thread->material_table);

            // If have a specialized evaluation function for the current material
            // configuration, call it and return.
            if (ei.mi->specialized_eval_exists ())
            {
                if (Trace)
                {
                    Tracer::Evalinfo    = ei;
                }

                return ei.mi->evaluate (pos);
            }

            // Score is computed from the point of view of white.
            Score score;

            // Initialize score by reading the incrementally updated scores included
            // in the position object (material + piece square tables) and adding Tempo bonus. 
            score  = pos.psq_score () + (WHITE == pos.active () ? +TempoBonus : -TempoBonus);
            score += ei.mi->material_score ();

            // Probe the pawn hash table
            ei.pi = Pawns::probe (pos, thread->pawns_table);
            score += apply_weight (ei.pi->pawn_score, Weights[PawnStructure]);

            // Initialize attack and king safety bitboards
            init_eval_info<WHITE> (pos, ei);
            init_eval_info<BLACK> (pos, ei);

            ei.attacked_by[WHITE][NONE] |= ei.attacked_by[WHITE][KING];
            ei.attacked_by[BLACK][NONE] |= ei.attacked_by[BLACK][KING];

            // Evaluate pieces and mobility
            Score mobility[CLR_NO] = { SCORE_ZERO, SCORE_ZERO };
            
            // Do not include in mobility squares occupied by our pawns or king or protected by enemy pawns 
            const Bitboard mobility_area[CLR_NO] =
            {
                ~(pos.pieces (WHITE, PAWN, KING)|ei.attacked_by[BLACK][NONE]),
                ~(pos.pieces (BLACK, PAWN, KING)|ei.attacked_by[WHITE][NONE])
            };

            score += 
              + evaluate_piece<WHITE, NIHT, Trace> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_piece<BLACK, NIHT, Trace> (pos, ei, mobility_area[BLACK], mobility[BLACK]);
            score += 
              + evaluate_piece<WHITE, BSHP, Trace> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_piece<BLACK, BSHP, Trace> (pos, ei, mobility_area[BLACK], mobility[BLACK]);
            score += 
              + evaluate_piece<WHITE, ROOK, Trace> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_piece<BLACK, ROOK, Trace> (pos, ei, mobility_area[BLACK], mobility[BLACK]);
            score += 
              + evaluate_piece<WHITE, QUEN, Trace> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_piece<BLACK, QUEN, Trace> (pos, ei, mobility_area[BLACK], mobility[BLACK]);

            // Weight mobility
            score += apply_weight (mobility[WHITE] - mobility[BLACK], Weights[Mobility]);

            // Evaluate kings after all other pieces because needed complete attack
            // information when computing the king safety evaluation.
            score += evaluate_king<WHITE, Trace> (pos, ei)
                  -  evaluate_king<BLACK, Trace> (pos, ei);

            // Evaluate tactical threats, needed full attack information including king
            score += evaluate_threats<WHITE, Trace> (pos, ei)
                  -  evaluate_threats<BLACK, Trace> (pos, ei);

            // Evaluate passed pawns, needed full attack information including king
            score += evaluate_passed_pawns<WHITE, Trace> (pos, ei)
                  -  evaluate_passed_pawns<BLACK, Trace> (pos, ei);

            const Value npm[CLR_NO] =
            {
                pos.non_pawn_material (WHITE),
                pos.non_pawn_material (BLACK)
            };

            // If one side has only a king, score for potential unstoppable pawns
            if (npm[BLACK] == VALUE_ZERO)
            {
                score += evaluate_unstoppable_pawns<WHITE> (pos, ei);
            }
            if (npm[WHITE] == VALUE_ZERO)
            {
                score -= evaluate_unstoppable_pawns<BLACK> (pos, ei);
            }

            Phase game_phase = ei.mi->game_phase ();
            ASSERT (PHASE_ENDGAME <= game_phase && game_phase <= PHASE_MIDGAME);

            // Evaluate space for both sides, only in middle-game.
            Score sw = ei.mi->space_weight ();
            if (sw != 0)
            {
                i32 space = evaluate_space<WHITE> (pos, ei)
                          - evaluate_space<BLACK> (pos, ei);

                score += apply_weight (space * sw, Weights[Space]);
            }

            //Value diff = npm[WHITE] - npm[BLACK];
            //if (abs (diff) >= VALUE_MG_PAWN)
            //{
            //    score += ((diff > VALUE_ZERO) - (diff < VALUE_ZERO)) * NonPawnMaterialBonus;
            //}

            i32 mg = i32 (mg_value (score));
            i32 eg = i32 (eg_value (score));
            ASSERT (-VALUE_INFINITE < mg && mg < +VALUE_INFINITE);
            ASSERT (-VALUE_INFINITE < eg && eg < +VALUE_INFINITE);

            ScaleFactor sf;

            //// Stalemate detection
            //Color stm = pos.active ();
            //if (   (game_phase < (PHASE_MIDGAME - 64))
            //    && (ei.attacked_by[stm][NIHT] == U64 (0))
            //    && (ei.attacked_by[stm][BSHP] == U64 (0))
            //    && (ei.attacked_by[stm][ROOK] == U64 (0))
            //    && (ei.attacked_by[stm][QUEN] == U64 (0))
            //    && (ei.attacked_by[stm][KING] & ~(pos.pieces (stm) | ei.attacked_by[~stm][NONE])) == U64 (0)
            //    && (MoveList<LEGAL> (pos).size () == 0)
            //   )
            //{
            //    sf = SCALE_FACTOR_DRAW;
            //}
            //else
            {
                // Scale winning side if position is more drawish than it appears
                sf = (eg > VALUE_DRAW)
                   ? ei.mi->scale_factor<WHITE> (pos)
                   : ei.mi->scale_factor<BLACK> (pos);

                // If don't already have an unusual scale factor, check for opposite
                // colored bishop endgames, and use a lower scale for those.
                if (   (game_phase < (PHASE_MIDGAME - 16))
                    && (sf == SCALE_FACTOR_NORMAL || sf == SCALE_FACTOR_PAWNS)
                    && (pos.opposite_bishops ())
                   )
                {
                    // Both sides with opposite-colored bishops only ignoring any pawns.
                    if (   (game_phase < (PHASE_MIDGAME - 96))
                        && (npm[WHITE] == VALUE_MG_BSHP)
                        && (npm[BLACK] == VALUE_MG_BSHP)
                       )
                    {
                        // It is almost certainly a draw even with pawns.
                        u08 pawn_diff = abs (pos.count<PAWN> (WHITE) - pos.count<PAWN> (BLACK));
                        sf  = (pawn_diff == 0) ? ScaleFactor (4) : ScaleFactor (8 * pawn_diff);
                    }
                    // Both sides with opposite-colored bishops, but also other pieces. 
                    else
                    {
                        // Still a bit drawish, but not as drawish as with only the two bishops.
                        sf = ScaleFactor (48 * i32 (sf) / i32 (SCALE_FACTOR_NORMAL));
                    }
                }
            }

            // Interpolates between a middle game and a (scaled by 'sf') endgame score, based on game phase.
            eg = eg * i32 (sf) / i32 (SCALE_FACTOR_NORMAL);
            
            Value value = Value (((mg * i32 (game_phase)) + (eg * i32 (PHASE_MIDGAME - game_phase))) / i32 (PHASE_MIDGAME));

            // In case of tracing add all single evaluation contributions for both white and black
            if (Trace)
            {
                Tracer::add_term (PAWN             , ei.pi->pawn_score);
                Tracer::add_term (Tracer::PST      , pos.psq_score () + (WHITE == pos.active () ? +TempoBonus : -TempoBonus));
                Tracer::add_term (Tracer::IMBALANCE, ei.mi->material_score ());

                Tracer::add_term (Tracer::MOBILITY
                    , apply_weight (mobility[WHITE], Weights[Mobility])
                    , apply_weight (mobility[BLACK], Weights[Mobility]));

                Tracer::add_term (Tracer::SPACE
                    , apply_weight (evaluate_space<WHITE> (pos, ei) * ei.mi->space_weight (), Weights[Space])
                    , apply_weight (evaluate_space<BLACK> (pos, ei) * ei.mi->space_weight (), Weights[Space]));

                Tracer::add_term (Tracer::TOTAL    , score);

                Tracer::Evalinfo    = ei;
                Tracer::Scalefactor = sf;
            }

            return (WHITE == pos.active ()) ? +value : -value;
        }

        namespace Tracer {

            string trace (const Position &pos)
            {
                memset (Terms, 0x00, sizeof (Terms));

                Value value = evaluate<true> (pos);
                value = (WHITE == pos.active ()) ? +value : -value; // White's point of view

                stringstream ss;

                ss  << showpoint << showpos << setprecision (2) << fixed
                    << "           Eval term |    White    |    Black    |     Total    \n"
                    << "                     |   MG    EG  |   MG    EG  |   MG    EG   \n"
                    << "---------------------+-------------+-------------+--------------\n";
                format_row (ss, "Material PST, Tempo", PST);
                format_row (ss, "Material imbalance" , IMBALANCE);
                format_row (ss, "Pawns"              , PAWN);
                format_row (ss, "Knights"            , NIHT);
                format_row (ss, "Bishops"            , BSHP);
                format_row (ss, "Rooks"              , ROOK);
                format_row (ss, "Queens"             , QUEN);
                format_row (ss, "Mobility"           , MOBILITY);
                format_row (ss, "King safety"        , KING);
                format_row (ss, "Threats"            , THREAT);
                format_row (ss, "Passed pawns"       , PASSED);
                format_row (ss, "Space"              , SPACE);
                ss  << "---------------------+-------------+-------------+--------------\n";
                format_row (ss, "Total"              , TOTAL);
                ss  << "\n"
                    << "Total evaluation: " << value_to_cp (value) << " (white side)\n";

                return ss.str ();
            }
        }

    }

    // evaluate() is the main evaluation function.
    // It always computes two values, an endgame value and a middle game value, in score
    // and interpolates between them based on the remaining material.
    Value evaluate  (const Position &pos)
    {
        return evaluate<false> (pos);
    }

    // trace() is like evaluate() but instead of a value returns a string suitable
    // to be print on stdout with the detailed descriptions and values of each
    // evaluation term. Used mainly for debugging.
    string trace    (const Position &pos)
    {
        return Tracer::trace (pos);
    }

    // initialize() computes evaluation weights from the corresponding UCI parameters
    // and setup king danger tables.
    void initialize ()
    {
        Weights[Mobility     ] = weight_option ("Mobility (Midgame)"       , "Mobility (Endgame)"      , InternalWeights[Mobility     ]);
        Weights[PawnStructure] = weight_option ("Pawn Structure (Midgame)" , "Pawn Structure (Endgame)", InternalWeights[PawnStructure]);
        Weights[PassedPawns  ] = weight_option ("Passed Pawns (Midgame)"   , "Passed Pawns (Endgame)"  , InternalWeights[PassedPawns  ]);
        Weights[Space        ] = weight_option ("Space"                    , "Space"                   , InternalWeights[Space        ]);
        Weights[KingSafety   ] = weight_option ("King Safety"              , "King Safety"             , InternalWeights[KingSafety   ]);

        const i32 MaxSlope  =   30;
        const i32 PeakScore = 1280;

        i32 mg = 0;
        for (u08 i = 1; i < MAX_ATTACK_UNITS; ++i)
        {
            mg = min (PeakScore, min (i32 (0.4*i*i), mg + MaxSlope));

            KingDanger[i] = apply_weight (mk_score (mg, 0), Weights[KingSafety]);
        }
    }

}
