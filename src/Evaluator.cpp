#include "Evaluator.h"

#include <iomanip>
#include <sstream>
#include <algorithm>

#include "BitBoard.h"
#include "BitCount.h"
#include "Position.h"
#include "Material.h"
#include "Pawns.h"
#include "Thread.h"
#include "UCI.h"

namespace Evaluator {

    using namespace std;
    using namespace BitBoard;
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
        // Cowardice  -> KingDanger to Self
        // Aggressive -> KingDanger to Opponent
        enum EvalWeightT { Mobility, PawnStructure, PassedPawns, Space, Cowardice, Aggressive };
        
        struct Weight { i32 mg, eg; };

        Weight Weights[6];

#define V         Value
#define S(mg, eg) mk_score (mg, eg)

        // Internal evaluation weights. These are applied on top of the evaluation
        // weights read from UCI parameters. The purpose is to be able to change
        // the evaluation weights while keeping the default values of the UCI
        // parameters at 100, which looks prettier.
        //
        // Values modified by Joona Kiiski
        const Score InternalWeights[6] =
        {
            S(+289,+344), // Mobility
            S(+233,+201), // PawnStructure
            S(+221,+273), // PassedPawns
            S(+ 46,+  0), // Space
            S(+271,+  0), // Cowardice
            S(+307,+  0)  // Aggressive
        };

        // MobilityBonus[PieceT][attacked] contains bonuses for middle and end game,
        // indexed by piece type and number of attacked squares not occupied by friendly pieces.
        const Score MobilityBonus[NONE][28] =
        {
            {},
            // Knights
            {
                S(-75,-50), S(-42,-30), S(- 9,-10), S(+ 3,  0), S(+15,+10),
                S(+27,+20), S(+37,+28), S(+42,+31), S(+44,+33)
            },
            // Bishops
            {
                S(-62,-47), S(-28,-23), S(+ 6,+ 1), S(+20,+15), S(+34,+29),
                S(+48,+43), S(+60,+55), S(+68,+63), S(+74,+68), S(+77,+72),
                S(+80,+75), S(+82,+77), S(+84,+79), S(+86,+81)
            },
            // Rooks
            {
                S(-57,- 52), S(-31,- 26), S(- 5,   0), S(+ 1,+ 16), S(+ 7,+ 32),
                S(+13,+ 48), S(+18,+ 64), S(+22,+ 80), S(+26,+ 96), S(+29,+109),
                S(+31,+115), S(+33,+119), S(+35,+122), S(+36,+123), S(+37,+124),
            },
            // Queens
            {
                S(-51,-40), S(-28,-23), S(- 5,- 7), S(  0,  0), S(+ 6,+10),
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
        const Value OutpostBonus[2][SQ_NO] =
        {       // A       B       C       D       E       F       G       H

            // KNIGHTS
            {
                V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
                V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
                V( 0), V( 0), V( 4), V( 8), V( 8), V( 4), V( 0), V( 0),
                V( 0), V( 4), V(17), V(26), V(26), V(17), V( 4), V( 0),
                V( 0), V( 8), V(26), V(35), V(35), V(26), V( 8), V( 0),
                V( 0), V( 4), V(17), V(17), V(17), V(17), V( 4), V( 0),
                V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
                V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0)
            },
            // BISHOPS
            {
                V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
                V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
                V( 0), V( 0), V( 5), V( 5), V( 5), V( 5), V( 0), V( 0),
                V( 0), V( 5), V(10), V(10), V(10), V(10), V( 5), V( 0),
                V( 0), V(10), V(21), V(21), V(21), V(21), V(10), V( 0),
                V( 0), V( 5), V( 8), V( 8), V( 8), V( 8), V( 5), V( 0),
                V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0),
                V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0), V( 0)
            }
        };

        // ThreatBonus[attacking][attacked] contains bonuses according to
        // which piece type attacks which one.
        const Score ThreatBonus[2][NONE] =
        {
            { S(+ 7,+39), S(+24,+49), S(+24,+49), S(+41,+100), S(+41,+100), S(+ 0,+ 0), }, // Minor
            { S(+15,+39), S(+15,+45), S(+15,+45), S(+15,+ 45), S(+24,+ 49), S(+ 0,+ 0), }, // Major
        };

        // PawnThreatenPenalty[PieceT] contains a penalty according to
        // which piece type is attacked by an enemy pawn.
        const Score PawnThreatenPenalty[NONE] =
        {
            S(+ 0,+ 0), S(+56,+70), S(+56,+70), S(+76,+99), S(+86,+118), S(+ 0,+ 0)
        };

        const Score TempoBonus              = S(+24,+11);
        const Score RookOnPawnBonus         = S(+10,+28);
        const Score RookOpenFileBonus       = S(+43,+21);
        const Score RookSemiOpenFileBonus   = S(+19,+10);
        const Score MinorBehindPawnBonus    = S(+16,+ 0);
        const Score PawnUnstoppableBonus    = S(+ 0,+20);

        const Score BishopPawnsPenalty      = S(+ 8,+14);
        const Score MinorUndefendedPenalty  = S(+25,+10);
        const Score RookTrappedPenalty      = S(+90,+ 0);

        //const Score CastleBlockedPenalty    = S(+150,+ 0);

        // Penalty for a bishop on a1/h1 (a8/h8 for black) which is trapped by
        // a friendly pawn on b2/g2 (b7/g7 for black).
        // This can obviously only happen in Chess960 games.
        const Score BishopTrappedA1H1Penalty= S(+50,+50);

#undef S
#undef V

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

        const i32 PiecePinnedWeight = + 2;

        // KingDanger[Color][attack_units] contains the actual king danger weighted
        // scores, indexed by color and by a calculated integer number.
        Score KingDanger[CLR_NO][100];


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

        // interpolate() interpolates between a middle game and an endgame score,
        // based on game phase. It also scales the return value by a ScaleFactor array.
        inline Value interpolate (const Score &score, Phase phase, ScaleFactor scale_factor)
        {
            ASSERT (-VALUE_INFINITE < mg_value (score) && mg_value (score) < +VALUE_INFINITE);
            ASSERT (-VALUE_INFINITE < eg_value (score) && eg_value (score) < +VALUE_INFINITE);
            ASSERT (PHASE_ENDGAME <= phase && phase <= PHASE_MIDGAME);

            i32 mg = mg_value (score);
            i32 eg = eg_value (score) * i32 (scale_factor) / SCALE_FACTOR_NORMAL;
            return Value ((mg * i32 (phase) + eg * i32 (PHASE_MIDGAME - phase)) / PHASE_MIDGAME);
        }

        //  --- init evaluation info --->
        template<Color C>
        // init_eval_info() initializes king bitboards for given color adding
        // pawn attacks. To be done at the beginning of the evaluation.
        inline void init_eval_info (const Position &pos, EvalInfo &ei)
        {
            const Color  C_ = (WHITE == C) ? BLACK : WHITE;

            ei.pinned_pieces[C] = pos.pinneds (C);

            ei.attacked_by[C][NONE] = ei.attacked_by[C][PAWN] = ei.pi->pawn_attacks<C> ();

            Bitboard attacks = ei.attacked_by[C_][KING] = PieceAttacks[KING][pos.king_sq (C_)];

            // Init king safety tables only if we are going to use them
            if (   (pos.count<QUEN> (C) != 0) 
                && (pos.non_pawn_material (C) > VALUE_MG_QUEN + VALUE_MG_PAWN)
               )
            {
                ei.king_ring              [C_] = attacks | shift_del<(WHITE == C) ? DEL_S : DEL_N> (attacks);
                attacks                       &= ei.attacked_by [C ][PAWN];
                ei.king_attackers_count   [C ] = attacks ? pop_count<MAX15> (attacks) : 0;
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
        // evaluate_outposts() evaluates bishop and knight outposts squares
        inline Score evaluate_outposts (const Position &pos, EvalInfo &ei, Square s)
        {
            ASSERT (BSHP == PT || NIHT == PT);

            const Color C_ = (WHITE == C) ? BLACK : WHITE;
            
            Score score = SCORE_ZERO;

            // Initial bonus based on square
            Value bonus = 
                (NIHT == PT) ? OutpostBonus[0][rel_sq (C, s)] :
                (BSHP == PT) ? OutpostBonus[1][rel_sq (C, s)] :
                VALUE_ZERO;

            // Increase bonus if supported by pawn, especially if the opponent has
            // no minor piece which can exchange the outpost piece.
            if (bonus != VALUE_ZERO)
            {
                if (ei.attacked_by[C][PAWN] & s)
                {
                    if (   (pos.pieces<NIHT> (C_) == U64 (0))
                        && ((pos.pieces<BSHP> (C_) & squares_of_color (s)) == U64 (0))
                        )
                    {
                        bonus += i32 (bonus)*1.5;
                    }
                    else
                    {
                        bonus += i32 (bonus)*0.5;
                    }
                }
                score = mk_score (bonus, bonus);
            }

            return score;
        }

        template<Color C, PieceT PT, bool TRACE>
        // evaluate_piece<>() assigns bonuses and penalties to the pieces of a given color except PAWN
        inline Score evaluate_piece (const Position &pos, EvalInfo &ei, const Bitboard &mobility_area, Score &mobility)
        {
            Score score = SCORE_ZERO;

            const Color  C_    = (WHITE == C) ? BLACK : WHITE;
            const Square fk_sq = pos.king_sq (C );
            const Bitboard occ = pos.pieces ();

            ei.attacked_by[C][PT] = U64 (0);

            const Square *pl = pos.list<PT> (C);
            Square s;
            while ((s = *pl++) != SQ_NO)
            {
                // Find attacked squares, including x-ray attacks for bishops and rooks
                Bitboard attacks =
                    (BSHP == PT) ? attacks_bb<BSHP> (s, (occ ^ pos.pieces (C, QUEN, BSHP)) | ei.pinned_pieces[C]) :
                    (ROOK == PT) ? attacks_bb<ROOK> (s, (occ ^ pos.pieces (C, QUEN, ROOK)) | ei.pinned_pieces[C]) :
                    (QUEN == PT) ? attacks_bb<BSHP> (s, (occ ^ pos.pieces (C, QUEN, BSHP)) | ei.pinned_pieces[C])
                                 | attacks_bb<ROOK> (s, (occ ^ pos.pieces (C, QUEN, ROOK)) | ei.pinned_pieces[C]) :
                    PieceAttacks[PT][s];

                if (ei.pinned_pieces[C] & s)
                {
                    attacks &= LineRay_bb[fk_sq][s];
                }

                ei.attacked_by[C][NONE] |= ei.attacked_by[C][PT] |= attacks;

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

                if (ROOK == PT)
                {
                    attacks &= ~( ei.attacked_by[C_][NIHT]
                                | ei.attacked_by[C_][BSHP]
                                );
                }

                if (QUEN == PT)
                {
                    attacks &= ~( ei.attacked_by[C_][NIHT]
                                | ei.attacked_by[C_][BSHP]
                                | ei.attacked_by[C_][ROOK]
                                );
                }

                i32 mob = pop_count<(QUEN != PT) ? MAX15 : FULL> (attacks & mobility_area);
                mobility += MobilityBonus[PT][mob];

                // Decrease score if we are attacked by an enemy pawn. Remaining part
                // of threat evaluation must be done later when we have full attack info.
                if (ei.attacked_by[C_][PAWN] & s)
                {
                    score -= PawnThreatenPenalty[PT];
                }

                // Special extra evaluation for pieces

                if (BSHP == PT || NIHT == PT)
                {
                    // Penalty for bishop with same coloured pawns
                    if (BSHP == PT)
                    {
                        score -= BishopPawnsPenalty * ei.pi->pawns_on_same_color_squares<C> (s);

                        // An important Chess960 pattern: A cornered bishop blocked by a friendly
                        // pawn diagonally in front of it is a very serious problem, especially
                        // when that pawn is also blocked.
                        if (pos.chess960 ())
                        {
                            if (   s == rel_sq (C, SQ_A1)
                                || s == rel_sq (C, SQ_H1)
                               )
                            {
                                const Piece pawn = (C | PAWN);
                                Delta del = pawn_push (C) + ((F_A == _file (s)) ? DEL_E : DEL_W);
                                if (pos[s + del] == pawn)
                                {
                                    score -= BishopTrappedA1H1Penalty *
                                        ( (pos[s + del + pawn_push (C)]!=EMPTY) ? 4
                                        : (pos[s + del + del] == pawn)          ? 2 : 1);
                                }
                            }
                        }

                    }

                    if (NIHT == PT)
                    {
                        //Bitboard span = ei.pi->_semiopen_files[C] ^ 0xFF;
                        //if ( (pos.count<PAWN> (C) > 1)
                        //  && ((i32 (scan_msq (span)) - i32 (scan_lsq (span))) > 4)
                        //   )
                        //{
                        //    score -= KnightSpan;
                        //}
                    }

                    // Bishop and knight outposts squares
                    if ((pos.pieces<PAWN> (C_) & PawnAttackSpan[C][s]) == U64 (0))
                    {
                        score += evaluate_outposts<C, PT> (pos, ei, s);
                    }

                    // Bishop or knight behind a pawn
                    if (   (rel_rank (C, s) < R_5)
                        && (pos.pieces<PAWN> () & (s + pawn_push (C)))
                       )
                    {
                        score += MinorBehindPawnBonus;
                    }
                }

                if (ROOK == PT)
                {
                    Rank r = rel_rank (C, s);
                    if (R_4 <= r)
                    {
                        // Rook piece attacking enemy pawns on the same rank/file
                        Bitboard enemy_pawns = pos.pieces<PAWN> (C_) & PieceAttacks[ROOK][s];
                        if (enemy_pawns != U64 (0))
                        {
                            score += RookOnPawnBonus * i32 (pop_count<MAX15> (enemy_pawns));
                        }
                    }

                    // Give a bonus for a rook on a open or semi-open file
                    if (ei.pi->semiopen_file<C> (_file (s)) != 0)
                    {
                        score += ei.pi->semiopen_file<C_> (_file (s)) != 0
                               ? RookOpenFileBonus
                               : RookSemiOpenFileBonus;
                    }
                    else
                    {
                        if (mob <= 3)
                        {
                            File f = _file (fk_sq);
                            // Penalize rooks which are trapped by a king. Penalize more if the
                            // king has lost its castling capability.
                            if (  ((f < F_E) == (_file (s) < f))
                               && (_rank (s) == _rank (fk_sq) || R_1 == rel_rank (C, fk_sq))
                               && (ei.pi->semiopen_side<C> (f, _file (s) < f) == 0)
                               )
                            {
                                bool cant_castle = !pos.can_castle (C)
                                                || (pos.castle_path (mk_castle_right (C, (s > fk_sq) ? CS_K : CS_Q)) & ei.attacked_by[C_][NONE]) != U64 (0);
                                score -= (RookTrappedPenalty - mk_score (mob * 8, 0)) * (1 + cant_castle);
                            }
                        }
                    }
                }
            }

            if (TRACE)
            {
                Tracer::Terms[C][PT] = score;
            }

            return score;
        }
        //  --- init evaluation info <---

        template<Color C, bool TRACE>
        // evaluate_king<>() assigns bonuses and penalties to a king of a given color
        inline Score evaluate_king (const Position &pos, const EvalInfo &ei)
        {
            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            Square king_sq = pos.king_sq (C);
            // King shelter and enemy pawns storm
            Score score = ei.pi->king_safety<C> (pos, king_sq);

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
                    + min (20, (ei.king_attackers_count[C_] * ei.king_attackers_weight[C_]) / 2)
                    + 3 * (ei.king_zone_attacks_count[C_] + pop_count<MAX15> (undefended))
                    - mg_value (score) / 32;

                // Undefended squares not occupied by enemy's
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
                undefended_attacked &= PieceAttacks[ROOK][king_sq];
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
                
                Bitboard rook_check = attacks_bb<ROOK> (king_sq, occ) & safe_sq;
                Bitboard bshp_check = attacks_bb<BSHP> (king_sq, occ) & safe_sq;

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
                safe_check = PieceAttacks[NIHT][king_sq] & safe_sq & ei.attacked_by[C_][NIHT];
                if (safe_check != U64 (0)) attack_units += SafeCheckWeight[NIHT] * pop_count<MAX15> (safe_check);

                Bitboard pinned_pieces = ei.pinned_pieces[C];
                // Penalty for pinned pieces 
                if (pinned_pieces != U64 (0))
                {
                    attack_units += PiecePinnedWeight * pop_count<MAX15> (pinned_pieces);
                }

                // To index KingDanger[] attack_units must be in [0, 99] range
                if (attack_units <  0) attack_units =  0;
                if (attack_units > 99) attack_units = 99;

                // Finally, extract the king danger score from the KingDanger[]
                // array and subtract the score from evaluation.
                score -= KingDanger[Searcher::RootColor == C][attack_units];
            }

            //if (pos.can_castle (C))
            //{
            //    bool castle_blocked;
            //    if (pos.can_castle (Castling<C, CS_K>::Right))// && !pos.castle_impeded (Castling<C, CS_K>::Right))
            //    {
            //        castle_blocked = (pos.castle_path (Castling<C, CS_K>::Right) & ei.attacked_by[C_][NONE]);
            //        if (castle_blocked)
            //        {
            //            score -= CastleBlockedPenalty;
            //        }
            //    }
            //    if (pos.can_castle (Castling<C, CS_Q>::Right))// && !pos.castle_impeded (Castling<C, CS_Q>::Right))
            //    {
            //        castle_blocked = (pos.castle_path (Castling<C, CS_Q>::Right) & ei.attacked_by[C_][NONE]);
            //        if (castle_blocked)
            //        {
            //            score -= CastleBlockedPenalty;
            //        }
            //    }
            //}

            if (TRACE)
            {
                Tracer::Terms[C][KING] = score;
            }

            return score;
        }

        template<Color C, bool TRACE>
        // evaluate_threats<>() assigns bonuses according to the type of attacking piece
        // and the type of attacked one.
        inline Score evaluate_threats (const Position &pos, const EvalInfo &ei)
        {
            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            Score score = SCORE_ZERO;

            // Enemy undefended minors get penalized even if not under attack
            Bitboard undefended_minors = pos.pieces (C_, BSHP, NIHT) & ~ei.attacked_by[C_][NONE];
            if (undefended_minors != U64 (0)) score += MinorUndefendedPenalty;

            // Enemy pieces not defended by a pawn and under our attack
            Bitboard weak_enemies = pos.pieces (C_) & ~ei.attacked_by[C_][PAWN] & ei.attacked_by[C][NONE];
            // Add a bonus according if the attacking pieces are minor or major
            if (weak_enemies != U64 (0))
            {
                Bitboard attacked_enemies;
                // Minor
                attacked_enemies = weak_enemies & (ei.attacked_by[C][NIHT] | ei.attacked_by[C][BSHP]);
                if (attacked_enemies != U64 (0)) score += ThreatBonus[0][ptype (pos[scan_lsq (attacked_enemies)])];
                // Major
                attacked_enemies = weak_enemies & (ei.attacked_by[C][ROOK] | ei.attacked_by[C][QUEN]);
                if (attacked_enemies != U64 (0)) score += ThreatBonus[1][ptype (pos[scan_lsq (attacked_enemies)])];
            }

            if (TRACE)
            {
                Tracer::Terms[C][Tracer::THREAT] = score;
            }

            return score;
        }

        template<Color C, bool TRACE>
        // evaluate_passed_pawns<>() evaluates the passed pawns of the given color
        inline Score evaluate_passed_pawns (const Position &pos, const EvalInfo &ei)
        {
            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            Score score = SCORE_ZERO;

            Bitboard passed_pawns = ei.pi->passed_pawns<C> ();
            while (passed_pawns != U64 (0))
            {
                Square s = pop_lsq (passed_pawns);

                ASSERT (pos.passed_pawn (C, s));

                i32 r = i32 (rel_rank (C, s)) - i32 (R_2);
                i32 rr = r * (r - 1);

                // Base bonus depends on rank
                Value mg_bonus = Value (17 * rr);
                Value eg_bonus = Value (7 * (rr + r + 1));

                if (rr != 0)
                {
                    Square block_sq = s + pawn_push (C);
                    Square fk_sq = pos.king_sq (C );
                    Square ek_sq = pos.king_sq (C_);

                    // Adjust bonus based on kings proximity
                    eg_bonus += Value (5 * rr * SquareDist[ek_sq][block_sq])
                             -  Value (2 * rr * SquareDist[fk_sq][block_sq]);

                    // If block_sq is not the queening square then consider also a second push
                    if (rel_rank (C, block_sq) != R_8)
                    {
                        eg_bonus -= Value (rr * SquareDist[fk_sq][block_sq + pawn_push (C)]);
                    }

                    // If the pawn is free to advance, increase bonus
                    if (pos.empty (block_sq))
                    {
                        const Bitboard occ = pos.pieces ();
                        // squares to queen
                        Bitboard queen_squares = FrontSqs_bb[C][s];
                        
                        Bitboard unsafe_squares;
                        // If there is an enemy rook or queen attacking the pawn from behind,
                        // add all X-ray attacks by the rook or queen. Otherwise consider only
                        // the squares in the pawn's path attacked or occupied by the enemy.
                        if (UNLIKELY (FrontSqs_bb[C_][s] & pos.pieces (C_, ROOK, QUEN))
                                  && (FrontSqs_bb[C_][s] & pos.pieces (C_, ROOK, QUEN) & attacks_bb<ROOK> (s, occ)))
                        {
                            unsafe_squares = queen_squares;
                        }
                        else
                        {
                            unsafe_squares = queen_squares & (ei.attacked_by[C_][NONE] | pos.pieces (C_));
                        }

                        Bitboard defended_squares;
                        if (UNLIKELY (FrontSqs_bb[C_][s] & pos.pieces (C, ROOK, QUEN))
                                  && (FrontSqs_bb[C_][s] & pos.pieces (C, ROOK, QUEN) & attacks_bb<ROOK> (s, occ)))
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

                        mg_bonus += Value (k * rr);
                        eg_bonus += Value (k * rr);
                    }
                }

                if (eg_bonus != VALUE_ZERO)
                {
                    // Rook pawns are a special case: They are sometimes worse, and
                    // sometimes better than other passed pawns. It is difficult to find
                    // good rules for determining whether they are good or bad.
                    if ((file_bb (s) & (FA_bb | FH_bb)) != U64 (0))
                    {
                        i32 npm = pos.non_pawn_material (C) + pos.non_pawn_material (C_);
                        eg_bonus -= (npm - i32 (2 * VALUE_EG_QUEN)) * eg_bonus / VALUE_INFINITE;
                    }

                    // Increase the bonus if we have more non-pawn pieces
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

            if (TRACE)
            {
                Tracer::Terms[C][Tracer::PASSED] = score;
            }
            
            return score;
        }

        // evaluate_unstoppable_pawns() scores the most advanced among the passed and
        // candidate pawns. In case opponent has no pieces but pawns, this is somewhat
        // related to the possibility pawns are unstoppable.
        template<Color C>
        inline Score evaluate_unstoppable_pawns (const Position &pos, const EvalInfo &ei)
        {
            Bitboard unstoppable_pawns = ei.pi->passed_pawns<C> () | ei.pi->candidate_pawns<C> ();
            return (unstoppable_pawns == U64 (0) || pos.non_pawn_material (~C) != VALUE_ZERO)
                ? SCORE_ZERO
                : PawnUnstoppableBonus * i32 (rel_rank (C, scan_frntmost_sq (C, unstoppable_pawns)));
        }

        template<Color C>
        // evaluate_space() computes the space evaluation for a given side. The
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

        template<bool TRACE>
        inline Value evaluate (const Position &pos)
        {
            ASSERT (pos.checkers () == U64 (0));

            Thread *thread = pos.thread ();

            EvalInfo ei;
            // Probe the material hash table
            ei.mi = Material::probe (pos, thread->material_table);

            // If we have a specialized evaluation function for the current material
            // configuration, call it and return.
            if (ei.mi->specialized_eval_exists ())
            {
                if (TRACE)
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
            score += apply_weight (ei.pi->pawn_score (), Weights[PawnStructure]);

            // Initialize attack and king safety bitboards
            init_eval_info<WHITE> (pos, ei);
            init_eval_info<BLACK> (pos, ei);

            ei.attacked_by[WHITE][NONE] |= ei.attacked_by[WHITE][KING];
            ei.attacked_by[BLACK][NONE] |= ei.attacked_by[BLACK][KING];

            // Evaluate pieces and mobility
            Score mobility[CLR_NO] = { SCORE_ZERO, SCORE_ZERO };
            
            // Do not include in mobility squares protected by enemy pawns or occupied by our pieces
            const Bitboard mobility_area[CLR_NO] =
            {
                ~(pos.pieces (WHITE, PAWN, KING) | ei.attacked_by[BLACK][PAWN]),
                ~(pos.pieces (BLACK, PAWN, KING) | ei.attacked_by[WHITE][PAWN])
            };

            score += 
              + evaluate_piece<WHITE, NIHT, TRACE> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_piece<BLACK, NIHT, TRACE> (pos, ei, mobility_area[BLACK], mobility[BLACK]);
            score += 
              + evaluate_piece<WHITE, BSHP, TRACE> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_piece<BLACK, BSHP, TRACE> (pos, ei, mobility_area[BLACK], mobility[BLACK]);
            score += 
              + evaluate_piece<WHITE, ROOK, TRACE> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_piece<BLACK, ROOK, TRACE> (pos, ei, mobility_area[BLACK], mobility[BLACK]);
            score += 
              + evaluate_piece<WHITE, QUEN, TRACE> (pos, ei, mobility_area[WHITE], mobility[WHITE])
              - evaluate_piece<BLACK, QUEN, TRACE> (pos, ei, mobility_area[BLACK], mobility[BLACK]);

            // Weight mobility
            score += apply_weight (mobility[WHITE] - mobility[BLACK], Weights[Mobility]);

            // Evaluate kings after all other pieces because we need complete attack
            // information when computing the king safety evaluation.
            score += evaluate_king<WHITE, TRACE> (pos, ei)
                  -  evaluate_king<BLACK, TRACE> (pos, ei);

            // Evaluate tactical threats, we need full attack information including king
            score += evaluate_threats<WHITE, TRACE> (pos, ei)
                  -  evaluate_threats<BLACK, TRACE> (pos, ei);

            // Evaluate passed pawns, we need full attack information including king
            score += evaluate_passed_pawns<WHITE, TRACE> (pos, ei)
                  -  evaluate_passed_pawns<BLACK, TRACE> (pos, ei);

            // If one side has only a king, score for potential unstoppable pawns
            if (pos.non_pawn_material (BLACK) == VALUE_ZERO)
            {
                score += evaluate_unstoppable_pawns<WHITE> (pos, ei);
            }
            if (pos.non_pawn_material (WHITE) == VALUE_ZERO)
            {
                score -= evaluate_unstoppable_pawns<BLACK> (pos, ei);
            }

            Score space_weight = ei.mi->space_weight ();
            // Evaluate space for both sides, only in middle-game.
            if (space_weight != 0)
            {
                i32 scr = evaluate_space<WHITE> (pos, ei)
                        - evaluate_space<BLACK> (pos, ei);
                score += apply_weight (scr * space_weight, Weights[Space]);
            }

            // Scale winning side if position is more drawish than it appears
            ScaleFactor sf = (eg_value (score) > VALUE_DRAW)
                ? ei.mi->scale_factor<WHITE> (pos)
                : ei.mi->scale_factor<BLACK> (pos);

            // If we don't already have an unusual scale factor, check for opposite
            // colored bishop endgames, and use a lower scale for those.
            if (   (ei.mi->game_phase () < PHASE_MIDGAME)
                && (sf <= SCALE_FACTOR_NORMAL)
                && (pos.opposite_bishops ())
               )
            {
                // Ignoring any pawns, do both sides only have a single bishop
                // and no other pieces?
                if (   (pos.non_pawn_material (WHITE) == VALUE_MG_BSHP)
                    && (pos.non_pawn_material (BLACK) == VALUE_MG_BSHP)
                   )
                {
                    // Check for KBP vs KB with only a single pawn that is almost
                    // certainly a draw or at least two pawns.
                    u08 pawn_diff = abs (pos.count<PAWN> (WHITE) - pos.count<PAWN> (BLACK));
                    sf  = (pawn_diff == 0) ? SCALE_FACTOR_DRAW :
                          ScaleFactor (4 * pawn_diff * i32 (sf) / SCALE_FACTOR_NORMAL);
                }
                else
                {
                    // Endgame with opposite-colored bishops, but also other pieces. Still
                    // a bit drawish, but not as drawish as with only the two bishops.
                    sf = ScaleFactor (16 * i32 (sf) / SCALE_FACTOR_NORMAL);
                }
            }

            Value value = interpolate (score, ei.mi->game_phase (), sf);

            // In case of tracing add all single evaluation contributions for both white and black
            if (TRACE)
            {
                Tracer::add_term (PAWN             , ei.pi->pawn_score ());
                Tracer::add_term (Tracer::PST      , pos.psq_score () + (WHITE == pos.active () ? +TempoBonus : -TempoBonus));
                Tracer::add_term (Tracer::IMBALANCE, ei.mi->material_score ());

                Tracer::add_term (Tracer::MOBILITY
                    , apply_weight (mobility[WHITE], Weights[Mobility])
                    , apply_weight (mobility[BLACK], Weights[Mobility]));

                Score scr[CLR_NO] =
                {
                    ei.mi->space_weight () * evaluate_space<WHITE> (pos, ei),
                    ei.mi->space_weight () * evaluate_space<BLACK> (pos, ei)
                };

                Tracer::add_term (Tracer::SPACE
                    , apply_weight (scr[WHITE], Weights[Space])
                    , apply_weight (scr[BLACK], Weights[Space]));

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

    // evaluate() is the main evaluation function. It always computes two
    // values, an endgame score and a middle game score, and interpolates
    // between them based on the remaining material.
    Value evaluate (const Position &pos)
    {
        return evaluate<false> (pos);
    }

    // trace() is like evaluate() but instead of a value returns a string suitable
    // to be print on stdout with the detailed descriptions and values of each
    // evaluation term. Used mainly for debugging.
    string trace (const Position &pos)
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
        Weights[Cowardice    ] = weight_option ("Cowardice"                , "Cowardice"               , InternalWeights[Cowardice    ]);
        Weights[Aggressive   ] = weight_option ("Aggressive"               , "Aggressive"              , InternalWeights[Aggressive   ]);

        const i32 MaxSlope  =   30;
        const i32 PeakScore = 1280;

        i32 mg = 0;
        for (u08 i = 1; i < 100; ++i)
        {
            mg = min (PeakScore, min (i32 (0.4*i*i), mg + MaxSlope));

            KingDanger[1][i] = apply_weight (mk_score (mg, 0), Weights[Cowardice ]);
            KingDanger[0][i] = apply_weight (mk_score (mg, 0), Weights[Aggressive]);
        }
    }

}
