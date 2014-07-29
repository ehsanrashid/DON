#include "Evaluator.h"

#include <iomanip>
#include <sstream>

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

            // king_ring_attacks_weight[Color] is the sum of product of "count" and "weight" of the pieces
            // of the given color which attack a square in the king_ring of the enemy king.
            // The weights of the individual piece types are given by the variables KingAttackWeight[PieceT]
            i32 king_ring_attacks_weight[CLR_NO];

            // king_zone_attacks[Color] is the sum of product of "count" and "weight" of the pieces
            // of the given color which attack a square directly adjacent to the enemy king.
            // The weights of the individual piece types are given by the variables KingAttackWeight[PieceT]
            // Pieces which attack more than one square are counted multiple times.
            // For instance, if black's king is on g8 and there's a white knight on g5,
            // this knight adds 2 to king_zone_attacks_count[WHITE].
            u08 king_zone_attacks[CLR_NO];

        };

        namespace Tracer {

            // Used for tracing
            enum TermT
            {
                MATERIAL = 6, IMBALANCE, MOBILITY, THREAT, PASSED, SPACE, TOTAL, TERM_NO
            };

            Score Terms[CLR_NO][TERM_NO];

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
                case MATERIAL: case IMBALANCE: case PAWN: case TOTAL:
                    ss  << setw (15) << name << " |  ----  ---- |  ----  ---- | " << showpos
                        << setw ( 5) << value_to_cp (mg_value (score[WHITE] - score[BLACK])) << " "
                        << setw ( 5) << value_to_cp (eg_value (score[WHITE] - score[BLACK])) << "\n";
                    break;

                default:
                    ss  << setw (15) << name << " | " << showpos
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

        enum EvalWeightT { MOBILITY, PAWN_STRUCT, PASSED_PAWN, SPACE, KING_SAFETY, EVAL_NO };
        
        struct Weight { i32 mg, eg; };
        
        // Evaluation weights, initialized from UCI options
        Weight Weights[EVAL_NO];

    #define S(mg, eg) mk_score (mg, eg)

        // Internal evaluation weights. These are applied on top of the evaluation
        // weights read from UCI parameters. The purpose is to be able to change
        // the evaluation weights while keeping the default values of the UCI
        // parameters at 100, which looks prettier.
        const Score InternalWeights[EVAL_NO] =
        {
            S(+289,+344), // Mobility
            S(+233,+201), // Pawn Structure
            S(+221,+273), // Passed Pawns
            S(+ 46,+  0), // Space
            S(+318,+  0)  // King Safety
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
                S(-47,- 53), S(-31,- 26), S(- 5,   0), S(+ 1,+ 16), S(+ 7,+ 32),
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

        // ThreatBonus[attacking][attacked] contains bonuses according to
        // which piece type attacks which one.
        const Score ThreatBonus[NONE][TOTL] =
        {
            { S(+ 7,+39), S(+24,+49), S(+24,+49), S(+38,+100), S(+41,+104) }, // Protected attacked by Minor
            { S(+ 7,+39), S(+24,+49), S(+25,+49), S(+38,+100), S(+41,+104) }, // Un-Protected attacked by Knight
            { S(+ 7,+39), S(+23,+49), S(+24,+49), S(+38,+100), S(+41,+104) }, // Un-Protected attacked by Bishop
            { S(+10,+39), S(+15,+45), S(+15,+45), S(+18,+ 49), S(+24,+ 52) }, // Un-Protected attacked by Rook
            { S(+10,+39), S(+15,+45), S(+15,+45), S(+18,+ 49), S(+24,+ 52) }, // Un-Protected attacked by Queen
            {}
        };

        // PawnThreatenPenalty[PieceT] contains a penalty according to
        // which piece type is attacked by an enemy pawn.
        const Score PawnThreatenPenalty[NONE] =
        {
            S(+ 0,+ 0), S(+80,+119), S(+80,+119), S(+117,+199), S(+127,+218), S(+ 0,+ 0)
        };
        
        const Score BishopPawnsPenalty            = S(+ 8,+12); // Penalty for bishop with pawns on same color
        const Score BishopTrappedPenalty          = S(+50,+40); // Penalty for bishop trapped with pawns

        const Score RookOnPawnBonus               = S(+10,+28); // Bonus for rook on pawns
        const Score RookOnOpenFileBonus           = S(+43,+21); // Bonus for rook on open file
        const Score RookOnSemiOpenFileBonus       = S(+19,+10); // Bonus for rook on semi-open file
        const Score RookDoubledOnOpenFileBonus    = S(+23,+10); // Bonus for double rook on open file
        const Score RookDoubledOnSemiOpenFileBonus= S(+12,+ 6); // Bonus for double rook on semi-open file
        const Score RookTrappedPenalty            = S(+92,+ 5); // Penalty for rook trapped
        
        const Score HangingBonus                  = S(+23,+20); // Bonus for each enemy hanging piece       

    #undef S

    #define V Value

        // OutpostValue[Square] contains bonus of outpost,
        // indexed by square (from white's point of view).
        const Value OutpostValue[SQ_NO] =
        {   // A      B      C      D      E      F      G      H
            V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
            V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
            V( 0), V( 0), V( 4), V( 8), V( 8), V( 4), V( 0), V( 0),
            V( 0), V( 4), V(17), V(26), V(26), V(17), V( 4), V( 0),
            V( 0), V( 8), V(26), V(35), V(35), V(26), V( 8), V( 0),
            V( 0), V( 4), V(17), V(17), V(17), V(17), V( 4), V( 0),
            V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
            V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0)
        };

    #undef V

        // The SpaceMask[Color] contains the area of the board which is considered
        // by the space evaluation. In the middle game, each side is given a bonus
        // based on how many squares inside this area are safe and available for
        // friendly minor pieces.
        const Bitboard SpaceMask[CLR_NO] =
        {
            ((FC_bb | FD_bb | FE_bb | FF_bb) & (R2_bb | R3_bb | R4_bb)),
            ((FC_bb | FD_bb | FE_bb | FF_bb) & (R7_bb | R6_bb | R5_bb))
        };


        // King danger constants and variables. The king danger scores are taken
        // from the KingDanger[]. Various little "meta-bonuses" measuring
        // the strength of the enemy attack are added up into an integer, which
        // is used as an index to KingDanger[].
        const u08 MAX_ATTACK_UNITS = 100;
        // KingDanger[attack_units] contains the king danger weighted score
        // indexed by a calculated integer number.
        Score KingDanger[MAX_ATTACK_UNITS];

        // KingAttackWeight[PieceT] contains king attack weights by piece type
        const i32   KingAttackWeight[NONE] = { + 1, + 4, + 4, + 6, +10, 0 };

        // Bonuses for safe checks
        const i32    SafeCheckWeight[NONE] = { + 0, + 3, + 2, + 8, +12, 0 };

        // Bonuses for contact safe checks
        const i32 ContactCheckWeight[NONE] = { + 0, + 0, + 3, +16, +24, 0 };


        const ScaleFactor PawnSpanScale[2] = { ScaleFactor(38), ScaleFactor(56) };

        // weight_option() computes the value of an evaluation weight,
        // by combining UCI-configurable weights with an internal weight.
        inline Weight weight_option (i32 opt_value, const Score &internal_weight)
        {
            Weight weight =
            {
                opt_value * mg_value (internal_weight) / 100, // =mg
                opt_value * eg_value (internal_weight) / 100  // =eg
            };
            return weight;
        }

        // apply_weight() weighs 'score' by factor 'weight' trying to prevent overflow
        inline Score apply_weight (const Score &score, const Weight &weight)
        {
            return mk_score (
                mg_value (score) * weight.mg / 0x100,
                eg_value (score) * weight.eg / 0x100);
        }

        //  --- init evaluation info --->
        template<Color C>
        // init_eval_info<>() initializes king bitboards for given color adding
        // pawn attacks. To be done at the beginning of the evaluation.
        inline void init_eval_info (const Position &pos, EvalInfo &ei)
        {
            const Color  C_ = (WHITE == C) ? BLACK : WHITE;

            Square ek_sq = pos.king_sq (C_);

            ei.pinneds[C] = pos.pinneds (C);
            ei.ful_attacked_by[C][NONE] = ei.ful_attacked_by[C][PAWN] = ei.pi->pawns_attacks[C];
            ei.pin_attacked_by[C][NONE] = ei.pin_attacked_by[C][PAWN] = ei.pi->pawns_attacks[C];
            
            Bitboard king_attacks        = PieceAttacks[KING][ek_sq];
            ei.ful_attacked_by[C_][KING] = ei.pin_attacked_by[C_][KING] = king_attacks;
            
            // Init king safety tables only if going to use them
            if (pos.non_pawn_material (C) > VALUE_MG_QUEN)
            {
                Bitboard king_zone = king_attacks + ek_sq;
                if (pos.count<QUEN> ())
                {
                    Rank ekr = rel_rank (C_, ek_sq);
                    ei.king_ring[C_] = king_zone | (ekr < R_6 ? DistanceRings[ek_sq][1] & PawnPassSpan[C_][ek_sq] :
                                                                DistanceRings[ek_sq][1] & (PawnPassSpan[C_][ek_sq]|PawnPassSpan[C][ek_sq]));
                }
                else
                {
                    ei.king_ring[C_] = king_zone;
                }

                ei.king_ring_attacks_weight[C ] = 0;
                ei.king_zone_attacks       [C ] = 0;

                Bitboard pawn_attacks = ei.pin_attacked_by[C][PAWN];
                if (ei.king_ring[C_] & pawn_attacks)
                {
                    ei.king_ring_attacks_weight[C] += KingAttackWeight[PAWN];
                    Bitboard zone_attacks = king_zone & pawn_attacks;
                    if (zone_attacks) ei.king_zone_attacks[C] += (more_than_one (zone_attacks) ? pop_count<MAX15> (zone_attacks) : 1);// * KingAttackWeight[PAWN];
                }
            }
            else
            {
                ei.king_ring[C_] = U64 (0);
            }
        }

        template<Color C, PieceT PT, bool Trace>
        // evaluate_pieces<>() assigns bonuses and penalties to the pieces of a given color except PAWN
        inline Score evaluate_pieces (const Position &pos, EvalInfo &ei, Bitboard mobility_area, Score &mobility)
        {
            const Color  C_      = (WHITE == C) ? BLACK : WHITE;
            const Delta PUSH     = (WHITE == C) ? DEL_N : DEL_S;
            const Delta PULL     = (WHITE == C) ? DEL_S : DEL_N;
            const Square fk_sq   = pos.king_sq (C);
            const Bitboard occ   = pos.pieces ();
            const Bitboard pinneds = ei.pinneds[C];

            ei.ful_attacked_by[C][PT] = U64 (0);
            ei.pin_attacked_by[C][PT] = U64 (0);

            Score score = SCORE_ZERO;

            const Square *pl = pos.list<PT> (C);
            Square s;
            while ((s = *pl++) != SQ_NO)
            {
                const File f = _file (s);
                Rank r ;

                // Find attacked squares, including x-ray attacks for bishops and rooks
                Bitboard attacks =
                    (BSHP == PT) ? attacks_bb<BSHP> (s, (occ ^ pos.pieces (C, QUEN, BSHP)) | pinneds) :
                    (ROOK == PT) ? attacks_bb<ROOK> (s, (occ ^ pos.pieces (C, QUEN, ROOK)) | pinneds) :
                    (QUEN == PT) ? attacks_bb<BSHP> (s, (occ ^ pos.pieces (C, QUEN, BSHP)) | pinneds)
                                 | attacks_bb<ROOK> (s, (occ ^ pos.pieces (C, QUEN, ROOK)) | pinneds) :
                                   PieceAttacks[PT][s];

                ei.ful_attacked_by[C][NONE] |= ei.ful_attacked_by[C][PT] |= attacks;

                if (ei.king_ring[C_] & attacks)
                {
                    ei.king_ring_attacks_weight[C] += KingAttackWeight[PT];
                    Bitboard zone_attacks = ei.ful_attacked_by[C_][KING] & attacks;
                    if (zone_attacks) ei.king_zone_attacks[C] += (more_than_one (zone_attacks) ? pop_count<MAX15> (zone_attacks) : 1);// * KingAttackWeight[PT];
                }

                // Decrease score if attacked by an enemy pawn. Remaining part
                // of threat evaluation must be done later when have full attack info.
                if (ei.pin_attacked_by[C_][PAWN] & s)
                {
                    score -= PawnThreatenPenalty[PT];
                }

                // Special extra evaluation for pieces
                
                //if (NIHT == PT || BSHP == PT)
                {
                if (NIHT == PT)
                {
                    // Outpost bonus for knight
                    if (!(pos.pieces<PAWN> (C_) & PawnAttackSpan[C][s] /*& ~(ei.pi->blocked_pawns[C_] & FrontRank_bb[C][rel_rank (C, s+PUSH)])*/))
                    {
                        // Initial bonus based on square
                        Value value = OutpostValue[rel_sq (C, s)];

                        // Increase bonus if supported by pawn, especially if the opponent has
                        // no minor piece which can exchange the outpost piece.
                        if (value != VALUE_ZERO)
                        {
                            // Supporting pawns
                            if (ei.pin_attacked_by[C][PAWN] & s)
                            {
                                if (  (ei.pin_attacked_by[C_][NIHT] & s)
                                   || (ei.pin_attacked_by[C_][BSHP] & s)
                                   )
                                {
                                    value *= 1.50;
                                }
                                else
                                {
                                    value *= 2.50;
                                }
                            }
                            score += mk_score (value * 2, value / 2);
                        }
                    }
                }

                if (BSHP == PT)
                {
                    score -= BishopPawnsPenalty * ei.pi->pawns_on_squares<C> (s);

                    Square rsq = rel_sq (C, s);

                    if (   rsq == SQ_A7
                        || rsq == SQ_H7
                       )
                    {
                        Delta del = PULL + ((F_A == f) ? DEL_E : DEL_W);
                        if (   (pos[s + del] == (C_ | PAWN))
                            && (ei.pin_attacked_by[C_][PAWN] & ~ei.pin_attacked_by[C][PAWN] & (s + del))
                           )
                        {
                            score -= BishopTrappedPenalty * 2;
                        }
                    }
                    if (   rsq == SQ_A6
                        || rsq == SQ_H6
                       )
                    {
                        Delta del = PULL + ((F_A == f) ? DEL_E : DEL_W);
                        if (   (pos[s + del] == (C_ | PAWN))
                            && (ei.pin_attacked_by[C_][PAWN] & ~ei.pin_attacked_by[C][PAWN] & (s + del))
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
                            Delta del = PUSH + ((F_A == f) ? DEL_E : DEL_W);
                            if (pos[s + del] == own_pawn)
                            {
                                score -= BishopTrappedPenalty *
                                    ( (pos[s + del + PUSH]!=EMPTY) ? 4 :
                                      (pos[s + del + del] == own_pawn) ? 2 : 1);
                            }
                        }
                    }
                }
                }

                if (ROOK == PT)
                {
                    r = rel_rank (C, s);
                    if (R_4 <= r)
                    {
                        // Rook piece attacking enemy pawns on the same rank/file
                        const Bitboard rook_on_enemy_pawns = pos.pieces<PAWN> (C_) & PieceAttacks[ROOK][s];
                        if (rook_on_enemy_pawns)
                        {
                            score += RookOnPawnBonus * (more_than_one (rook_on_enemy_pawns) ? pop_count<MAX15> (rook_on_enemy_pawns) : 1);
                        }
                    }

                    // Give a bonus for a rook on a open or semi-open file
                    if (ei.pi->semiopen_file<C > (f))
                    {
                        score += (ei.pi->semiopen_file<C_> (f)) ?
                                 RookOnOpenFileBonus :
                                 RookOnSemiOpenFileBonus;
                        
                        // Give more if the rook is doubled
                        if (pos.count<ROOK> (C) > 1 && (File_bb[f] & pos.pieces<ROOK> (C) & attacks))
                        {
                            score += (ei.pi->semiopen_file<C_> (f)) ?
                                     RookDoubledOnOpenFileBonus :
                                     RookDoubledOnSemiOpenFileBonus;
                        }
                        
                    }
                }

                if (pinneds & s)
                {
                    attacks &= LineRay_bb[fk_sq][s];
                }

                ei.pin_attacked_by[C][NONE] |= ei.pin_attacked_by[C][PT] |= attacks;

                if (ROOK == PT)
                {
                    attacks &= (~(ei.pin_attacked_by[C_][NIHT]|ei.pin_attacked_by[C_][BSHP])|ei.pin_attacked_by[C][NONE]);
                }

                if (QUEN == PT)
                {
                    attacks &= (~(ei.pin_attacked_by[C_][NIHT]|ei.pin_attacked_by[C_][BSHP]|ei.pin_attacked_by[C_][ROOK])|ei.pin_attacked_by[C][NONE]);
                }
                
                Bitboard mobile = attacks & mobility_area;
                
                u08 mob = mobile ? pop_count<(QUEN != PT) ? MAX15 : FULL> (mobile) : 0;
                
                mobility += MobilityBonus[PT][mob];

                if (ROOK == PT)
                {
                    if (mob <= 3 && !ei.pi->semiopen_file<C > (f))
                    {
                        const File kf = _file (fk_sq);
                        const Rank kr = rel_rank (C, fk_sq);
                        // Penalize rooks which are trapped by a king.
                        // Penalize more if the king has lost its castling capability.
                        if (   ((kf < F_E) == (f < kf))
                            && (kr == R_1 || kr == r)
                            && (!ei.pi->semiopen_side<C> (kf, f < kf))
                           )
                        {
                            score -= (RookTrappedPenalty - mk_score (22 * mob, 0)) * (1 + !pos.can_castle (C));
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

            const Square fk_sq = pos.king_sq (C);

            // King shelter and enemy pawns storm
            ei.pi->evaluate_king_pawn_safety<C> (pos);

            Value value = VALUE_ZERO;
            Rank kr = rel_rank (C, fk_sq);
            if (kr <= R_4)
            {
                // If can castle use the value after the castle if is bigger
                if (kr == R_1 && pos.can_castle (C))
                {
                    if (    pos.can_castle (Castling<C, CS_K>::Right)
                        && !pos.castle_impeded (Castling<C, CS_K>::Right)
                        && !(pos.king_path (Castling<C, CS_K>::Right) & ei.ful_attacked_by[C_][NONE])
                       )
                    {
                        value = max (value, ei.pi->shelter_storm[C][CS_K]);
                    }
                    if (    pos.can_castle (Castling<C, CS_Q>::Right)
                        && !pos.castle_impeded (Castling<C, CS_Q>::Right)
                        && !(pos.king_path (Castling<C, CS_Q>::Right) & ei.ful_attacked_by[C_][NONE])
                       )
                    {
                        value = max (value, ei.pi->shelter_storm[C][CS_Q]);
                    }
                    
                    value = max (value, ei.pi->shelter_storm[C][CS_NO]);
                }
                else
                {
                    value = ei.pi->shelter_storm[C][CS_NO];
                }
            }

            Score score = mk_score (value, -16 * ei.pi->min_kp_dist[C]);
            
            // Main king safety evaluation
            if (ei.king_ring_attacks_weight[C_] > KingAttackWeight[PAWN])
            {
                const Bitboard occ = pos.pieces ();

                // Find the attacked squares around the king which has no defenders
                // apart from the king itself
                Bitboard undefended =
                    ei.ful_attacked_by[C ][KING] // King zone
                  & ei.ful_attacked_by[C_][NONE]
                  & ~(ei.pin_attacked_by[C ][PAWN]
                    | ei.pin_attacked_by[C ][NIHT]
                    | ei.pin_attacked_by[C ][BSHP]
                    | ei.pin_attacked_by[C ][ROOK]
                    | ei.pin_attacked_by[C ][QUEN]);

                // Initialize the 'attack_units' variable, which is used later on as an
                // index to the KingDanger[] array. The initial value is based on the
                // number and types of the enemy's attacking pieces, the number of
                // attacked and undefended squares around our king, and the quality of
                // the pawn shelter (current 'mg score' value).
                i32 attack_units =
                    //+ min (ei.king_attackers_count[C_] * ei.king_attackers_weight[C_] / 2, 20)
                    + min (ei.king_ring_attacks_weight[C_], 20) // King-ring attacks piece weight
                    + 3 * ei.king_zone_attacks[C_] // King-zone attacks piece weight
                    + 3 * (undefended ? (more_than_one (undefended) ? pop_count<MAX15> (undefended) : 1) : 0)          // King-zone undefended piece weight
                    + 2 * (ei.pinneds[C] ? (more_than_one (ei.pinneds[C]) ? pop_count<MAX15> (ei.pinneds[C]) : 1) : 0) // King-pinned piece weight
                    - i32 (value) / 32;

                // Undefended squares around king not occupied by enemy's
                undefended &= ~pos.pieces (C_);
                if (undefended)
                {
                    Bitboard undefended_attacked;
                    if (pos.count<QUEN> (C_) > 0)
                    {
                        // Analyse enemy's safe queen contact checks.
                        // Undefended squares around the king attacked by enemy queen...
                        undefended_attacked = undefended & ei.pin_attacked_by[C_][QUEN];
                        while (undefended_attacked)
                        {
                            Square sq = pop_lsq (undefended_attacked);

                            if (  ((ei.ful_attacked_by[C_][PAWN]|ei.ful_attacked_by[C_][NIHT]|ei.ful_attacked_by[C_][BSHP]|ei.ful_attacked_by[C_][ROOK]|ei.ful_attacked_by[C_][KING]) & sq)
                               || (  pos.count<QUEN> (C_) > 1
                                  && more_than_one (pos.pieces<QUEN> (C_) & (attacks_bb<BSHP> (sq, occ ^ pos.pieces<QUEN> (C_))|attacks_bb<ROOK> (sq, occ ^ pos.pieces<QUEN> (C_))))
                                  )
                               )
                            {
                                attack_units += ContactCheckWeight[QUEN];
                            }
                        }
                    }
                    if (pos.count<ROOK> (C_) > 0)
                    {
                        // Analyse enemy's safe rook contact checks.
                        // Undefended squares around the king attacked by enemy rooks...
                        undefended_attacked = undefended & ei.pin_attacked_by[C_][ROOK];
                        // Consider only squares where the enemy rook gives check
                        undefended_attacked &= PieceAttacks[ROOK][fk_sq];
                        while (undefended_attacked)
                        {
                            Square sq = pop_lsq (undefended_attacked);

                            if (  ((ei.ful_attacked_by[C_][PAWN]|ei.ful_attacked_by[C_][NIHT]|ei.ful_attacked_by[C_][BSHP]|ei.ful_attacked_by[C_][QUEN]|ei.ful_attacked_by[C_][KING]) & sq)
                               || (  pos.count<ROOK> (C_) > 1
                                  && more_than_one (pos.pieces<ROOK> (C_) & attacks_bb<ROOK> (sq, occ ^ pos.pieces<ROOK> (C_)))
                                  )
                               )
                            {
                                attack_units += ContactCheckWeight[ROOK];
                            }
                        }
                    }
                    if (pos.count<BSHP> (C_) > 0)
                    {
                        // Analyse enemy's safe rook contact checks.
                        // Undefended squares around the king attacked by enemy bishop...
                        undefended_attacked = undefended & ei.pin_attacked_by[C_][BSHP];
                        // Consider only squares where the enemy bishop gives check
                        undefended_attacked &= PieceAttacks[BSHP][fk_sq];
                        while (undefended_attacked)
                        {
                            Square sq = pop_lsq (undefended_attacked);

                            if (  ((ei.ful_attacked_by[C_][PAWN]|ei.ful_attacked_by[C_][NIHT]|ei.ful_attacked_by[C_][ROOK]|ei.ful_attacked_by[C_][QUEN]|ei.ful_attacked_by[C_][KING]) & sq)
                               || (  pos.count<BSHP> (C_) > 1
                                  && more_than_one (pos.pieces<BSHP> (C_) & squares_of_color (sq))
                                  && more_than_one (pos.pieces<BSHP> (C_) & attacks_bb<BSHP> (sq, occ ^ pos.pieces<BSHP> (C_)))
                                  )
                               )
                            {
                                attack_units += ContactCheckWeight[BSHP];
                            }
                        }
                    }
                    // Knight can't give contact check but safe distance check
                }

                // Analyse the enemy's safe distance checks for sliders and knights
                Bitboard safe_area = ~(pos.pieces (C_) | ei.pin_attacked_by[C][NONE]);

                Bitboard rook_check = attacks_bb<ROOK> (fk_sq, occ) & safe_area;
                Bitboard bshp_check = attacks_bb<BSHP> (fk_sq, occ) & safe_area;

                Bitboard safe_check;
                // Enemy queen safe checks
                safe_check = (rook_check | bshp_check) & ei.pin_attacked_by[C_][QUEN];
                if (safe_check) attack_units += SafeCheckWeight[QUEN] * (more_than_one (safe_check) ? pop_count<MAX15> (safe_check) : 1);

                // Enemy rooks safe checks
                safe_check = rook_check & ei.pin_attacked_by[C_][ROOK];
                if (safe_check) attack_units += SafeCheckWeight[ROOK] * (more_than_one (safe_check) ? pop_count<MAX15> (safe_check) : 1);

                // Enemy bishops safe checks
                safe_check = bshp_check & ei.pin_attacked_by[C_][BSHP];
                if (safe_check) attack_units += SafeCheckWeight[BSHP] * (more_than_one (safe_check) ? pop_count<MAX15> (safe_check) : 1);

                // Enemy knights safe checks
                safe_check = PieceAttacks[NIHT][fk_sq] & safe_area & ei.pin_attacked_by[C_][NIHT];
                if (safe_check) attack_units += SafeCheckWeight[NIHT] * (more_than_one (safe_check) ? pop_count<MAX15> (safe_check) : 1);

                // To index KingDanger[] attack_units must be in [0, MAX_ATTACK_UNITS-1] range
                attack_units = min (max (attack_units, 0), MAX_ATTACK_UNITS-1);

                // Finally, extract the king danger score from the KingDanger[]
                // array and subtract the score from evaluation.
                score -= KingDanger[attack_units];

                if (ei.king_zone_attacks[C_] >= 3)
                {
                    // King mobility is good in the endgame
                    Bitboard mobile = ei.ful_attacked_by[C][KING] & ~(pos.pieces (C) | ei.ful_attacked_by[C_][NONE]);
                    u08 mob = mobile ? more_than_one (mobile) ? pop_count<MAX15> (mobile) : 1 : 0;
                    if (mob < 3) score -= mk_score (0, 8 * (8 - mob*mob));
                }
            }

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

            Bitboard enemies = pos.pieces (C_);

            // Enemies protected by pawn and attacked by minors
            Bitboard protected_enemies = 
                   enemies
                & ~pos.pieces<PAWN>(C_)
                &  ei.pin_attacked_by[C_][PAWN]
                & (ei.pin_attacked_by[C ][NIHT]|ei.pin_attacked_by[C ][BSHP]);

            // Enemies not defended by pawn and attacked by any piece
            Bitboard weak_enemies = 
                   enemies
                & ~ei.pin_attacked_by[C_][PAWN]
                &  ei.pin_attacked_by[C ][NONE];
            
            Score score = SCORE_ZERO;

            Score s;
            
            s = SCORE_ZERO;
            while (protected_enemies)
            {
                s = max (ThreatBonus[0][ptype (pos[pop_lsq (protected_enemies)])], s);
            }
            score += s;

            // Add a bonus according if the attacking pieces are minor or major
            if (weak_enemies)
            {
                // Threaten enemies
                for (i08 pt = NIHT; pt <= QUEN; ++pt)
                {
                    Bitboard threaten_enemies = weak_enemies & ei.pin_attacked_by[C][pt];
                    s = SCORE_ZERO;
                    while (threaten_enemies)
                    {
                        s = max (ThreatBonus[pt][ptype (pos[pop_lsq (threaten_enemies)])], s);
                    }
                    score += s;
                }

                // Hanging enemies
                Bitboard hanging_enemies = weak_enemies & ~ei.pin_attacked_by[C_][NONE];
                if (hanging_enemies)
                {
                    score += HangingBonus * (more_than_one (hanging_enemies) ? pop_count<MAX15> (hanging_enemies) : 1);
                }
            }

            if (Trace)
            {
                Tracer::Terms[C][Tracer::THREAT] = score;
            }

            return score;
        }

        template<Color C>
        // evaluate_passed_pawns<>() evaluates the passed pawns of the given color
        inline Score evaluate_passed_pawns (const Position &pos, const EvalInfo &ei)
        {
            const Color C_   = (WHITE == C) ? BLACK : WHITE;
            const Delta PUSH = (WHITE == C) ? DEL_N : DEL_S;

            Score score = SCORE_ZERO;

            Bitboard passed_pawns = ei.pi->passed_pawns[C];
            while (passed_pawns)
            {
                Square s = pop_lsq (passed_pawns);

                ASSERT (pos.passed_pawn (C, s));
                Rank pr = rel_rank (C, s);
                i32 r = i32 (pr) - i32 (R_2);
                i32 rr = r * (r - 1);

                // Base bonus depends on rank
                Value mg_value = Value (17 * rr);
                Value eg_value = Value ( 7 * (rr + r + 1));

                if (rr)
                {
                    Square block_sq = s + PUSH;
                    Square fk_sq = pos.king_sq (C );
                    Square ek_sq = pos.king_sq (C_);

                    // Adjust bonus based on kings proximity
                    eg_value += (5 * rr * SquareDist[ek_sq][block_sq])
                             -  (2 * rr * SquareDist[fk_sq][block_sq]);

                    // If block square is not the queening square then consider also a second push
                    if (rel_rank (C, block_sq) != R_8)
                    {
                        eg_value -= (rr * SquareDist[fk_sq][block_sq + PUSH]);
                    }

                    bool can_advance = true;
                    if (ei.pinneds[C] & s)
                    {
                        // Only one real pinner exist other are fake pinner
                        Bitboard pawn_pinners =
                            ( (PieceAttacks[ROOK][fk_sq] & pos.pieces (QUEN, ROOK))
                            | (PieceAttacks[BSHP][fk_sq] & pos.pieces (QUEN, BSHP))
                            ) &  pos.pieces (C_) & LineRay_bb[fk_sq][s];

                        can_advance = Between_bb[fk_sq][pop_lsq (pawn_pinners)] & block_sq;
                    }

                    if (can_advance)
                    {

                        Bitboard pawnR7_capture = (pr == R_7) ? pos.pieces (C_) & PawnAttacks[C][s] : U64 (0);

                        // If the pawn is free to advance, increase bonus
                        if (   pos.empty (block_sq)
                            || pawnR7_capture
                           )
                        {
                            // Squares to queen
                            const Bitboard front_squares = FrontSqrs_bb[C ][s];
                            const Bitboard queen_squares = (pr == R_7) ? front_squares | pawnR7_capture : front_squares;
                            const Bitboard back_squares  = FrontSqrs_bb[C_][s];

                            Bitboard unsafe_squares;
                            // If there is an enemy rook or queen attacking the pawn from behind,
                            // add all X-ray attacks by the rook or queen. Otherwise consider only
                            // the squares in the pawn's path attacked or occupied by the enemy.
                            if (   (  ((back_squares & pos.pieces<ROOK> (C_)) && (ei.pin_attacked_by[C_][ROOK] & s))
                                   || ((back_squares & pos.pieces<QUEN> (C_)) && (ei.pin_attacked_by[C_][QUEN] & s))
                                   )
                                && (back_squares & ei.pin_attacked_by[C ][NONE]) != back_squares
                                && (back_squares & pos.pieces (C_, ROOK, QUEN) & attacks_bb<ROOK> (s, pos.pieces ()))
                               )
                            {
                                unsafe_squares = (pr == R_7) ?
                                                 front_squares | (queen_squares & (ei.pin_attacked_by[C_][NONE])) :
                                                 front_squares;
                            }
                            else
                            {
                                unsafe_squares = (pr == R_7) ?
                                                 (front_squares & pos.pieces (C_)) | (queen_squares & (ei.pin_attacked_by[C_][NONE])) :
                                                 front_squares & (ei.pin_attacked_by[C_][NONE]|pos.pieces (C_));
                            }

                            Bitboard defended_squares;
                            if (   (  ((back_squares & pos.pieces<ROOK> (C )) && (ei.pin_attacked_by[C ][ROOK] & s))
                                   || ((back_squares & pos.pieces<QUEN> (C )) && (ei.pin_attacked_by[C ][QUEN] & s))
                                   )
                                && (back_squares & ei.pin_attacked_by[C_][NONE]) != back_squares
                                && (back_squares & pos.pieces (C , ROOK, QUEN) & attacks_bb<ROOK> (s, pos.pieces ()))
                               )
                            {
                                defended_squares = front_squares;
                            }
                            else
                            {
                                defended_squares = (pr == R_7) ?
                                                    ((front_squares & ei.pin_attacked_by[C ][NONE])
                                                    |(pawnR7_capture & (ei.pin_attacked_by[C ][NIHT]|ei.pin_attacked_by[C ][BSHP]|ei.pin_attacked_by[C ][ROOK]|ei.pin_attacked_by[C ][QUEN]|ei.pin_attacked_by[C ][KING]))) :
                                                    (front_squares & ei.pin_attacked_by[C ][NONE]);
                            }

                            // Give a big bonus if there aren't enemy attacks, otherwise
                            // a smaller bonus if block square is not attacked.
                            i32 k = (pr == R_7) ? ((unsafe_squares != queen_squares) ? 15 : 0) :
                                                   (unsafe_squares) ? ((unsafe_squares & block_sq) ? 0 : 9) :
                                                                      15;

                            if (defended_squares)
                            {
                                // Give a big bonus if the path to queen is fully defended,
                                // a smaller bonus if at least block square is defended.
                                k += (pr == R_7) ? ((defended_squares & queen_squares) ? 6 : 0) :
                                                    (defended_squares == front_squares) ? 6 : (defended_squares & block_sq) ? 4 : 0;

                                // If the block square is defended by a pawn add more small bonus.
                                if (ei.pin_attacked_by[C][PAWN] & block_sq) k += 1;
                            }
                        
                            if (k)
                            {
                                mg_value += k * rr;
                                eg_value += k * rr;
                            }
                        }
                        else
                        if (pos.pieces (C) & block_sq)
                        {
                            mg_value += 3 * rr + 2 * r + 3;
                            eg_value += 1 * rr + 2 * r + 0;
                        }
                    }
                }

                // Increase the bonus if have more non-pawn pieces
                if (pos.count<NONPAWN> (C ) > pos.count<NONPAWN> (C_))
                {
                    eg_value += eg_value / 4;
                }

                score += mk_score (mg_value, eg_value);
            }
            
            return score;
        }

        template<Color C>
        // evaluate_space<>() computes the space evaluation for a given side. The
        // space evaluation is a simple bonus based on the number of safe squares
        // available for minor pieces on the central four files on ranks 2--4. Safe
        // squares one, two or three squares behind a friendly pawn are counted
        // twice. Finally, the space bonus is scaled by a weight taken from the
        // material hash table. The aim is to improve play on game opening.
        inline i32 evaluate_space (const EvalInfo &ei)
        {
            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            // Find the safe squares for our pieces inside the area defined by
            // SpaceMask[]. A square is unsafe if it is attacked by an enemy
            // pawn, or if it is undefended and attacked by an enemy piece.
            Bitboard safe_space =
                  SpaceMask[C]
                & ~ei.pi->pawns[C]//~ei.pi->blocked_pawns[C]
                & ~ei.pin_attacked_by[C_][PAWN]
                & (ei.pin_attacked_by[C ][NONE]|~ei.pin_attacked_by[C_][NONE]);

            // Since SpaceMask[C] is fully on our half of the board
            ASSERT (u32 (safe_space >> ((WHITE == C) ? 32 : 0)) == 0);

            // Find all squares which are at most three squares behind some friendly pawn
            Bitboard behind = ei.pi->pawns[C];
            behind |= shift_del<(WHITE == C) ? DEL_S  : DEL_N > (behind);
            behind |= shift_del<(WHITE == C) ? DEL_SS : DEL_NN> (behind);

            // Count safe_space + (behind & safe_space) with a single pop_count
            return pop_count<FULL> (((WHITE == C) ? safe_space << 32 : safe_space >> 32) | (behind & safe_space));
        }

        template<bool Trace>
        // evaluate<>()
        inline Value evaluate (const Position &pos)
        {
            ASSERT (!pos.checkers ());

            Thread *thread = pos.thread ();

            EvalInfo ei;
            // Probe the material hash table
            ei.mi  = Material::probe (pos, thread->material_table);

            // If have a specialized evaluation function for the current material
            // configuration, call it and return.
            if (ei.mi->specialized_eval_exists ())
            {
                return ei.mi->evaluate (pos);
            }

            // Score is computed from the point of view of white.
            Score score;

            // Initialize score by reading the incrementally updated scores included
            // in the position object (material + piece square tables) and adding Tempo bonus. 
            score  = pos.psq_score ();
            score += ei.mi->matl_score;

            // Probe the pawn hash table
            ei.pi  = Pawns::probe (pos, thread->pawns_table);
            score += apply_weight (ei.pi->pawn_score, Weights[PAWN_STRUCT]);

            // Initialize attack and king safety bitboards
            init_eval_info<WHITE> (pos, ei);
            init_eval_info<BLACK> (pos, ei);

            ei.ful_attacked_by[WHITE][NONE] |= ei.ful_attacked_by[WHITE][KING];
            ei.ful_attacked_by[BLACK][NONE] |= ei.ful_attacked_by[BLACK][KING];
            ei.pin_attacked_by[WHITE][NONE] |= ei.pin_attacked_by[WHITE][KING];
            ei.pin_attacked_by[BLACK][NONE] |= ei.pin_attacked_by[BLACK][KING];

            // Evaluate pieces and mobility
            Score mobility[CLR_NO] = { SCORE_ZERO, SCORE_ZERO };
            
            // Do not include in mobility squares occupied by our pawns or king or protected by enemy pawns 
            const Bitboard mobility_area[CLR_NO] =
            {
                ~(pos.pieces (WHITE, PAWN, KING)|ei.pin_attacked_by[BLACK][PAWN]),
                ~(pos.pieces (BLACK, PAWN, KING)|ei.pin_attacked_by[WHITE][PAWN])
            };

            score += 
              + evaluate_pieces<WHITE, NIHT, Trace> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_pieces<BLACK, NIHT, Trace> (pos, ei, mobility_area[BLACK], mobility[BLACK]);
            score += 
              + evaluate_pieces<WHITE, BSHP, Trace> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_pieces<BLACK, BSHP, Trace> (pos, ei, mobility_area[BLACK], mobility[BLACK]);
            score += 
              + evaluate_pieces<WHITE, ROOK, Trace> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_pieces<BLACK, ROOK, Trace> (pos, ei, mobility_area[BLACK], mobility[BLACK]);
            score += 
              + evaluate_pieces<WHITE, QUEN, Trace> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_pieces<BLACK, QUEN, Trace> (pos, ei, mobility_area[BLACK], mobility[BLACK]);

            // Weight mobility
            score += apply_weight (mobility[WHITE] - mobility[BLACK], Weights[MOBILITY]);

            // Evaluate kings after all other pieces because needed complete attack
            // information when computing the king safety evaluation.
            score += evaluate_king<WHITE, Trace> (pos, ei)
                  -  evaluate_king<BLACK, Trace> (pos, ei);

            // Evaluate tactical threats, needed full attack information including king
            score += evaluate_threats<WHITE, Trace> (pos, ei)
                  -  evaluate_threats<BLACK, Trace> (pos, ei);

            // Evaluate passed pawns, needed full attack information including king
            Score passed_pawn[CLR_NO] =
            {
                evaluate_passed_pawns<WHITE> (pos, ei),
                evaluate_passed_pawns<BLACK> (pos, ei)
            };

            score += apply_weight (passed_pawn[WHITE] - passed_pawn[BLACK], Weights[PASSED_PAWN]);

            const Value npm[CLR_NO] =
            {
                pos.non_pawn_material (WHITE),
                pos.non_pawn_material (BLACK)
            };

            // If one side has only a king, score for potential unstoppable pawns
            if (npm[BLACK] == VALUE_ZERO)
            {
                score += ei.pi->evaluate_unstoppable_pawns<WHITE> ();
            }
            if (npm[WHITE] == VALUE_ZERO)
            {
                score -= ei.pi->evaluate_unstoppable_pawns<BLACK> ();
            }

            Phase game_phase = ei.mi->game_phase;
            ASSERT (PHASE_ENDGAME <= game_phase && game_phase <= PHASE_MIDGAME);

            // Evaluate space for both sides, only in middle-game.
            i32 space[CLR_NO] = { SCORE_ZERO, SCORE_ZERO };
            Score space_weight = ei.mi->space_weight;
            if (space_weight)
            {
                space[WHITE] = evaluate_space<WHITE> (ei);
                space[BLACK] = evaluate_space<BLACK> (ei);

                score += apply_weight ((space[WHITE] - space[BLACK]) * space_weight, Weights[SPACE]);
            }

            // In case of tracing add each evaluation contributions for both white and black
            if (Trace)
            {
                Tracer::add_term (PAWN             , ei.pi->pawn_score);
                Tracer::add_term (Tracer::MATERIAL , pos.psq_score ());
                Tracer::add_term (Tracer::IMBALANCE, ei.mi->matl_score);

                Tracer::add_term (Tracer::MOBILITY
                    , apply_weight (mobility[WHITE], Weights[MOBILITY])
                    , apply_weight (mobility[BLACK], Weights[MOBILITY]));

                Tracer::add_term (Tracer::PASSED
                    , apply_weight (passed_pawn[WHITE], Weights[PASSED_PAWN])
                    , apply_weight (passed_pawn[BLACK], Weights[PASSED_PAWN]));

                Tracer::add_term (Tracer::SPACE
                    , apply_weight (space[WHITE] ? space[WHITE] * space_weight : SCORE_ZERO, Weights[SPACE])
                    , apply_weight (space[BLACK] ? space[BLACK] * space_weight : SCORE_ZERO, Weights[SPACE]));

                Tracer::add_term (Tracer::TOTAL    , score);

            }

            // --------------------------------------------------

            i32 mg = i32 (mg_value (score));
            i32 eg = i32 (eg_value (score));
            ASSERT (-VALUE_INFINITE < mg && mg < +VALUE_INFINITE);
            ASSERT (-VALUE_INFINITE < eg && eg < +VALUE_INFINITE);

            ScaleFactor scale_fac;

            Color strong_side = (eg > VALUE_DRAW) ? WHITE : BLACK;

            // Scale winning side if position is more drawish than it appears
            scale_fac = (strong_side == WHITE) ?
                ei.mi->scale_factor<WHITE> (pos) :
                ei.mi->scale_factor<BLACK> (pos);

            // If don't already have an unusual scale factor, check for opposite
            // colored bishop endgames, and use a lower scale for those.
            if (   (game_phase < PHASE_MIDGAME)
                && (scale_fac == SCALE_FACTOR_NORMAL || scale_fac == SCALE_FACTOR_PAWNS)
               )
            {
                if (pos.opposite_bishops ())
                {
                    // Both sides with opposite-colored bishops only ignoring any pawns.
                    if (   (npm[WHITE] == VALUE_MG_BSHP)
                        && (npm[BLACK] == VALUE_MG_BSHP)
                       )
                    {
                        // It is almost certainly a draw even with pawns.
                        i32 pawn_diff = abs (pos.count<PAWN> (WHITE) - pos.count<PAWN> (BLACK));
                        scale_fac = (pawn_diff == 0) ? ScaleFactor (4) : ScaleFactor (8 * pawn_diff);
                    }
                    // Both sides with opposite-colored bishops, but also other pieces. 
                    else
                    {
                        // Still a bit drawish, but not as drawish as with only the two bishops.
                        scale_fac = ScaleFactor (50 * i32 (scale_fac) / i32 (SCALE_FACTOR_NORMAL));
                    }
                }
                else
                if (    (abs (eg) <= VALUE_EG_BSHP)
                    &&  (ei.pi->pawn_span[strong_side] <= 1)
                    && !pos.passed_pawn (~strong_side, pos.king_sq (~strong_side))
                   )
                {
                    // Endings where weaker side can place his king in front of the strong side pawns are drawish.
                    scale_fac = PawnSpanScale[ei.pi->pawn_span[strong_side]];
                }
            }

            // Interpolates between a middle game and a (scaled by 'scale_fac') endgame score, based on game phase.
            eg = eg * i32 (scale_fac) / i32 (SCALE_FACTOR_NORMAL);
            
            Value value = Value (((mg * i32 (game_phase)) + (eg * i32 (PHASE_MIDGAME - game_phase))) / i32 (PHASE_MIDGAME));

            return (WHITE == pos.active ()) ? +value : -value;
        }

        namespace Tracer {

            string trace (const Position &pos)
            {
                memset (Terms, 0x00, sizeof (Terms));

                Value value = evaluate<true> (pos);// + TempoBonus;    // Tempo bonus = 0.07
                value = (WHITE == pos.active ()) ? +value : -value; // White's point of view

                stringstream ss;

                ss  << showpoint << showpos << setprecision (2) << fixed
                    << "      Eval term |    White    |    Black    |     Total    \n"
                    << "                |   MG    EG  |   MG    EG  |   MG    EG   \n"
                    << "----------------+-------------+-------------+--------------\n";
                format_row (ss, "Material"      , MATERIAL);
                format_row (ss, "Imbalance"     , IMBALANCE);
                format_row (ss, "Pawn"          , PAWN);
                format_row (ss, "Knight"        , NIHT);
                format_row (ss, "Bishop"        , BSHP);
                format_row (ss, "Rook"          , ROOK);
                format_row (ss, "Queen"         , QUEN);
                format_row (ss, "King safety"   , KING);
                format_row (ss, "Mobility"      , MOBILITY);
                format_row (ss, "Threat"        , THREAT);
                format_row (ss, "Passed pawn"   , PASSED);
                format_row (ss, "Space"         , SPACE);
                ss  << "---------------------+-------------+-------------+--------------\n";
                format_row (ss, "Total"         , TOTAL);
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
        return evaluate<false> (pos) + TempoBonus;
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
        Weights[MOBILITY   ] = weight_option (100                         , InternalWeights[MOBILITY   ]);
        Weights[PAWN_STRUCT] = weight_option (100                         , InternalWeights[PAWN_STRUCT]);
        Weights[PASSED_PAWN] = weight_option (100                         , InternalWeights[PASSED_PAWN]);
        Weights[SPACE      ] = weight_option (i32 (Options["Space"      ]), InternalWeights[SPACE      ]);
        Weights[KING_SAFETY] = weight_option (i32 (Options["King Safety"]), InternalWeights[KING_SAFETY]);

        const i32 MaxSlope  =   30;
        const i32 PeakScore = 1280;

        KingDanger[0] = SCORE_ZERO;
        i32 mg = 0;
        for (u08 i = 1; i < MAX_ATTACK_UNITS; ++i)
        {
            mg = min (min (i32 (0.4*i*i), mg + MaxSlope), PeakScore);
            //if (80 - MaxSlope < i && i < 80) mg = i32 (PeakScore - 0.5 * (80 - i) * (80 - i));
            KingDanger[i] = apply_weight (mk_score (mg, 0), Weights[KING_SAFETY]);
        }
    }

}
