#include "Evaluator.h"

#include <cassert>
#include <iomanip>
#include <sstream>
#include <algorithm>

#include "BitCount.h"
#include "BitBoard.h"
#include "Position.h"
#include "Material.h"
#include "Pawns.h"
#include "Searcher.h"
#include "Thread.h"
#include "UCI.h"

using namespace std;
using namespace BitBoard;

namespace {

    // Used for tracing
    enum ExtendedPieceType
    { 
        PST = 8, IMBALANCE, MOBILITY, THREAT, PASSED, SPACE, TOTAL
    };

    // Struct EvalInfo contains various information computed and collected
    // by the evaluation functions.
    typedef struct EvalInfo
    {
        // Pointers to material and pawn hash table entries
        Material::Entry *mi;
        Pawns   ::Entry *pi;

        // attacked_by[color][piecetype] contains all squares attacked by a given color and piece type,
        // attacked_by[color][PT_NO] contains all squares attacked by the given color.
        Bitboard attacked_by[CLR_NO][PT_ALL];

        // king_ring[color] is the zone around the king which is considered
        // by the king safety evaluation. This consists of the squares directly
        // adjacent to the king, and the three (or two, for a king on an edge file)
        // squares two ranks in front of the king. For instance, if black's king
        // is on g8, king_ring[BLACK] is a bitboard containing the squares f8, h8,
        // f7, g7, h7, f6, g6 and h6.
        Bitboard king_ring[CLR_NO];

        // king_attackers_count[color] is the number of pieces of the given color
        // which attack a square in the king_ring of the enemy king.
        int32_t king_attackers_count[CLR_NO];

        // king_attackers_weight[color] is the sum of the "weight" of the pieces of the
        // given color which attack a square in the king_ring of the enemy king. The
        // weights of the individual piece types are given by the variables
        // QueenAttackWeight, RookAttackWeight, BishopAttackWeight and
        // KnightAttackWeight in evaluate.cpp
        int32_t king_attackers_weight[CLR_NO];

        // king_adjacent_zone_attacks_count[color] is the number of attacks to squares
        // directly adjacent to the king of the given color. Pieces which attack
        // more than one square are counted multiple times. For instance, if black's
        // king is on g8 and there's a white knight on g5, this knight adds
        // 2 to king_adjacent_zone_attacks_count[BLACK].
        int32_t king_adjacent_zone_attacks_count[CLR_NO];

        Bitboard pinned_pieces[CLR_NO];

    } EvalInfo;

    // Evaluation grain size, must be a power of 2
    const int32_t GrainSize = 4;

    // Evaluation weights, initialized from UCI options
    enum EvalWeights { Mobility, PawnStructure, PassedPawns, Space, KingDanger_C, KingDanger_C_ };

    Score Weights[6];

#define V         Value
#define S(mg, eg) mk_score (mg, eg)

    // Internal evaluation weights. These are applied on top of the evaluation
    // weights read from UCI parameters. The purpose is to be able to change
    // the evaluation weights while keeping the default values of the UCI
    // parameters at 100, which looks prettier.
    //
    // Values modified by Joona Kiiski
    const Score WeightsInternal[] =
    {
        S(289, 344), S(233, 201), S(221, 273), S(46, 0), S(271, 0), S(307, 0)
    };

    // MobilityBonus[PType][attacked] contains bonuses for middle and end
    // game, indexed by piece type and number of attacked squares not occupied by
    // friendly pieces.
    const Score MobilityBonus[PT_NO][32] =
    {
        {},
        // Knights
        {S(-35,-30), S(-22,-20), S(- 9,-10), S(  3,  0), S( 15, 10),
        S( 27, 20), S( 37, 28), S( 42, 31), S( 44, 33)
        },
        // Bishops
        {S(-22,-27), S(- 8,-13), S(  6,  1), S( 20, 15), S( 34, 29), S( 48, 43), S( 60, 55),
        S( 68, 63), S( 74, 68), S( 77, 72), S( 80, 75), S( 82, 77), S( 84, 79), S( 86, 81)
        },
        // Rooks
        {S(-17,-33), S(-11,-16), S(- 5,  0), S(  1, 16), S(  7, 32), S( 13, 48),
        S( 18, 64), S( 22, 80), S( 26, 96), S( 29,109), S( 31,115), S( 33,119),
        S( 35,122), S( 36,123), S( 37,124),
        },
        // Queens
        {S(-12,-20), S(- 8,-13), S(- 5, -7), S(- 2,- 1), S( 1,  5), S(  4, 11),
        S(  7, 17), S( 10, 23), S( 13, 29), S( 16, 34), S( 18, 38), S( 20, 40),
        S( 22, 41), S( 23, 41), S( 24, 41), S( 25, 41), S( 25, 41), S( 25, 41),
        S( 25, 41), S( 25, 41), S( 25, 41), S( 25, 41), S( 25, 41), S( 25, 41),
        S( 25, 41), S( 25, 41), S( 25, 41), S( 25, 41)
        },
    };

    // OutpostBonus[PType][Square] contains bonuses of knights and bishops, indexed
    // by piece type and square (from white's point of view).
    const Value OutpostBonus[2][SQ_NO] =
    {
        // A     B     C     D     E     F     G     H

        // KNIGHTS
        {V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        V(0), V(0), V(4), V(8), V(8), V(4), V(0), V(0),
        V(0), V(4),V(17),V(26),V(26),V(17), V(4), V(0),
        V(0), V(8),V(26),V(35),V(35),V(26), V(8), V(0),
        V(0), V(4),V(17),V(17),V(17),V(17), V(4), V(0),
        V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        },
        // BISHOPS
        {V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        V(0), V(0), V(5), V(5), V(5), V(5), V(0), V(0),
        V(0), V(5),V(10),V(10),V(10),V(10), V(5), V(0),
        V(0),V(10),V(21),V(21),V(21),V(21),V(10), V(0),
        V(0), V(5), V(8), V(8), V(8), V(8), V(5), V(0),
        V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        }
    };

    // ThreatBonus[attacking][attacked] contains bonuses according to which piece
    // type attacks which one.
    const Score ThreatBonus[][PT_NO] =
    {
        { S(  0,  0), S(  7, 39), S( 24, 49), S( 24, 49), S( 41,100), S( 41,100) }, // Minor
        { S(  0,  0), S( 15, 39), S( 15, 45), S( 15, 45), S( 15, 45), S( 24, 49) }, // Major
    };

    // ThreatenedByPawnPenalty[PType] contains a penalty according to which piece
    // type is attacked by an enemy pawn.
    const Score ThreatenedByPawnPenalty[PT_NO] =
    {
        S(  0,  0), S( 56, 70), S( 56, 70), S( 76, 99), S( 86, 118), S(  0,  0),
    };


    const Score TempoBonus             = S( 24, 11);
    //const Score BishopPinBonus         = S( 66, 11);

    const Score RookOn7thBonus         = S( 11, 20);
    const Score QueenOn7thBonus        = S(  3,  8);

    const Score RookOnPawnBonus        = S( 10, 28);
    const Score QueenOnPawnBonus       = S(  4, 20);

    const Score RookOpenFileBonus      = S( 43, 21);
    const Score RookSemiopenFileBonus  = S( 19, 10);

    const Score BishopPawnsPenalty     = S(  8, 12);
    const Score KnightPawnsPenalty     = S(  8,  4);
    const Score MinorBehindPawnBonus   = S( 16,  0);
    const Score UndefendedMinorPenalty = S( 25, 10);
    const Score TrappedRookPenalty     = S( 90,  0);
    const Score UnstoppablePawnBonus   = S(  0, 20);

    // Penalty for a bishop on a1/h1 (a8/h8 for black) which is trapped by
    // a friendly pawn on b2/g2 (b7/g7 for black). This can obviously only
    // happen in Chess960 games.
    const Score TrappedBishopA1H1 = S( 50, 50);

#undef S
#undef V

    // The SpaceMask[Color] contains the area of the board which is considered
    // by the space evaluation. In the middle game, each side is given a bonus
    // based on how many squares inside this area are safe and available for
    // friendly minor pieces.
    const Bitboard SpaceMask[CLR_NO] =
    {
        (FC_bb | FD_bb | FE_bb | FB_bb) & (R2_bb | R3_bb | R4_bb),
        (FC_bb | FD_bb | FE_bb | FB_bb) & (R7_bb | R6_bb | R5_bb),
    };

    // King danger constants and variables. The king danger scores are taken
    // from the KingDanger[]. Various little "meta-bonuses" measuring
    // the strength of the enemy attack are added up into an integer, which
    // is used as an index to KingDanger[].
    //
    // KingAttackWeights[PType] contains king attack weights by piece type
    const int32_t KingAttackWeights[PT_NO] = { 0, 2, 2, 3, 5, 0, };

    // Bonuses for enemy's safe checks
    const int32_t KnightCheck       =  3;
    const int32_t BishopCheck       =  2;
    const int32_t RookCheck         =  8;
    const int32_t QueenCheck        = 12;
    const int32_t RookContactCheck  = 16;
    const int32_t QueenContactCheck = 24;

    //// KingExposed[Square] contains penalties based on the position of the
    //// defending king, indexed by king's square (from white's point of view).
    //const int32_t KingExposed[SQ_NO] =
    //{
    //    2,  0,  2,  5,  5,  2,  0,  2,
    //    2,  2,  4,  8,  8,  4,  2,  2,
    //    7, 10, 12, 12, 12, 12, 10,  7,
    //    15, 15, 15, 15, 15, 15, 15, 15,
    //    15, 15, 15, 15, 15, 15, 15, 15,
    //    15, 15, 15, 15, 15, 15, 15, 15,
    //    15, 15, 15, 15, 15, 15, 15, 15,
    //    15, 15, 15, 15, 15, 15, 15, 15
    //};

    // KingDanger[Color][attack_units] contains the actual king danger weighted
    // scores, indexed by color and by a calculated integer number.
    Score KingDanger[CLR_NO][128];

    template<bool TRACE>
    Value do_evaluate       (const Position &pos);

    template<Color C>
    void init_eval_info     (const Position &pos, EvalInfo &ei);

    template<Color C, bool TRACE>
    Score evaluate_pieces (const Position &pos, EvalInfo &ei, Score mobility[]);

    template<Color C, bool TRACE>
    Score evaluate_king     (const Position &pos, const EvalInfo &ei);

    template<Color C, bool TRACE>
    Score evaluate_threats  (const Position &pos, const EvalInfo &ei);

    template<Color C, bool TRACE>
    Score evaluate_passed_pawns (const Position &pos, const EvalInfo &ei);

    template<Color C>
    int32_t evaluate_space  (const Position &pos, const EvalInfo &ei);

    Score evaluate_unstoppable_pawns (const Position &pos, Color c, const EvalInfo &ei);

    Value interpolate (const Score &score, Phase ph, ScaleFactor sf);
    Score apply_weight (Score score, Score w);
    Score weight_option (const string &mg_opt, const string &eg_opt, Score internal_weight);
    double to_cp (Value value);

    namespace Tracing {

        Score scores[CLR_NO][TOTAL + 1];
        stringstream stream;

        void add (int32_t idx, Score term_w, Score term_b = SCORE_ZERO);
        void row (const char name[], int32_t idx);
        string do_trace (const Position &pos);

    }

    // --------------------------------------------------

    template<bool TRACE>
    Value do_evaluate       (const Position &pos)
    {
        ASSERT (!pos.checkers());
        // Score is computed from the point of view of white.
        Score score;
        Score mobility[CLR_NO] = { SCORE_ZERO, SCORE_ZERO };

        Thread *thread = pos.thread ();

        // Initialize score by reading the incrementally updated scores included
        // in the position object (material + piece square tables) and adding Tempo bonus. 
        score = pos.psq_score () + (WHITE == pos.active () ? TempoBonus : -TempoBonus);
        
        EvalInfo ei;
        // Probe the material hash table
        ei.mi = Material::probe (pos, thread->material_table, thread->endgames);
        score += ei.mi->material_score ();

        // If we have a specialized evaluation function for the current material
        // configuration, call it and return.
        if (ei.mi->specialized_eval_exists ())
        {
            return ei.mi->evaluate (pos);
        }

        // Probe the pawn hash table
        ei.pi = Pawns::probe (pos, thread->pawns_table);
        score += apply_weight (ei.pi->pawn_score(), Weights[PawnStructure]);

        // Initialize attack and king safety bitboards
        init_eval_info<WHITE>(pos, ei);
        init_eval_info<BLACK>(pos, ei);

        // Evaluate pieces and mobility
        score += evaluate_pieces<WHITE, TRACE>(pos, ei, mobility)
            -    evaluate_pieces<BLACK, TRACE>(pos, ei, mobility);

        score += apply_weight (mobility[WHITE] - mobility[BLACK], Weights[Mobility]);

        // Evaluate kings after all other pieces because we need complete attack
        // information when computing the king safety evaluation.
        score += evaluate_king<WHITE, TRACE>(pos, ei)
            -    evaluate_king<BLACK, TRACE>(pos, ei);

        // Evaluate tactical threats, we need full attack information including king
        score += evaluate_threats<WHITE, TRACE>(pos, ei)
            -    evaluate_threats<BLACK, TRACE>(pos, ei);


        // Evaluate passed pawns, we need full attack information including king
        score += evaluate_passed_pawns<WHITE, TRACE>(pos, ei)
            -    evaluate_passed_pawns<BLACK, TRACE>(pos, ei);

        // If one side has only a king, score for potential unstoppable pawns
        if (!pos.non_pawn_material (WHITE) || !pos.non_pawn_material (BLACK))
        {
            score += evaluate_unstoppable_pawns(pos, WHITE, ei)
                -    evaluate_unstoppable_pawns(pos, BLACK, ei);
        }
        // Evaluate space for both sides, only in middle-game.
        if (ei.mi->space_weight())
        {
            int32_t s = evaluate_space<WHITE>(pos, ei) - evaluate_space<BLACK>(pos, ei);
            score += apply_weight (s * ei.mi->space_weight(), Weights[Space]);
        }

        // Scale winning side if position is more drawish that what it appears
        ScaleFactor sf = (eg_value (score) > VALUE_DRAW) ?
            ei.mi->scale_factor (pos, WHITE) : ei.mi->scale_factor (pos, BLACK);

        // If we don't already have an unusual scale factor, check for opposite
        // colored bishop endgames, and use a lower scale for those.
        if (ei.mi->game_phase () < PHASE_MIDGAME &&
            pos.opposite_bishops () &&
            (sf == SCALE_FACTOR_NORMAL || sf == SCALE_FACTOR_ONEPAWN))
        {
            // Ignoring any pawns, do both sides only have a single bishop and no
            // other pieces?
            if (pos.non_pawn_material (WHITE) == VALUE_MG_BISHOP &&
                pos.non_pawn_material (BLACK) == VALUE_MG_BISHOP)
            {
                // Check for KBP vs KB with only a single pawn that is almost
                // certainly a draw or at least two pawns.
                bool one_pawn = (pos.piece_count<PAWN>(WHITE) + pos.piece_count<PAWN>(BLACK) == 1);
                sf = one_pawn ? ScaleFactor (8) : ScaleFactor (32);
            }
            else
            {
                // Endgame with opposite-colored bishops, but also other pieces. Still
                // a bit drawish, but not as drawish as with only the two bishops.
                sf = ScaleFactor (50 * sf / SCALE_FACTOR_NORMAL);
            }
        }

        Value value = interpolate (score, ei.mi->game_phase (), sf);

        // In case of tracing add all single evaluation contributions for both white and black
        if (TRACE)
        {
            Tracing::add (PST, pos.psq_score ());
            Tracing::add (IMBALANCE, ei.mi->material_score ());
            Tracing::add (PAWN, ei.pi->pawn_score ());
            Score scr[CLR_NO] =
            {
                ei.mi->space_weight() * evaluate_space<WHITE>(pos, ei),
                ei.mi->space_weight() * evaluate_space<BLACK>(pos, ei),
            };

            Tracing::add (SPACE, apply_weight (scr[WHITE], Weights[Space]), apply_weight (scr[BLACK], Weights[Space]));
            Tracing::add (TOTAL, score);
            Tracing::stream
                << "-------\n"
                //<< "Uncertainty margin:"
                //<< " White: " << to_cp (margins[WHITE]) << "-"
                //<< " Black: " << to_cp (margins[BLACK]) << "\n"
                << "Scaling: " << noshowpos
                << setw(6) << (100.0 * ei.mi->game_phase ()) / 128.0 << "% MG, "
                << setw(6) << (100.0 * (1.0 - ei.mi->game_phase () / 128.0)) << "% * "
                << setw(6) << (100.0 * sf) / SCALE_FACTOR_NORMAL << "% EG.\n"
                << "Total evaluation: " << to_cp (value);
        }

        return (WHITE == pos.active ()) ? value : -value;
    }

    template<Color C>
    // init_eval_info() initializes king bitboards for given color adding
    // pawn attacks. To be done at the beginning of the evaluation.
    void init_eval_info     (const Position &pos, EvalInfo &ei)
    {
        const Color  C_  = ((WHITE == C) ? BLACK : WHITE);
        const Delta PULL = ((WHITE == C) ? DEL_S : DEL_N);

        ei.pinned_pieces[C] = pos.pinneds (C);

        Bitboard attacks = ei.attacked_by[C_][KING] = pos.attacks_from<KING>(pos.king_sq (C_));
        ei.attacked_by[C][PAWN] = ei.pi->pawn_attacks(C);

        // Init king safety tables only if we are going to use them
        if (pos.piece_count<QUEN>(C) && pos.non_pawn_material (C) > VALUE_MG_QUEEN + VALUE_MG_PAWN)
        {
            ei.king_ring[C_] = attacks | shift_del<PULL>(attacks);
            attacks &= ei.attacked_by[C][PAWN];
            ei.king_attackers_count[C] = attacks ? pop_count<MAX15>(attacks) / 2 : 0;
            ei.king_adjacent_zone_attacks_count[C] = ei.king_attackers_weight[C] = 0;
        }
        else
        {
            ei.king_ring[C_] = ei.king_attackers_count[C] = 0;
        }
    }

    template<PType PT, Color C>
    // evaluate_outposts() evaluates bishop and knight outposts squares
    Score evaluate_outposts (const Position &pos, EvalInfo &ei, Square s)
    {
        //static_assert (BSHP == PT || NIHT == PT, "PT must be BISHOP or KNIGHT");
        ASSERT (BSHP == PT || NIHT == PT);

        const Color C_ = ((WHITE == C) ? BLACK : WHITE);

        // Initial bonus based on square
        Value bonus;
        switch (PT)
        {
        case NIHT: bonus = OutpostBonus[0][rel_sq (C, s)]; break;
        case BSHP: bonus = OutpostBonus[1][rel_sq (C, s)]; break;
        default:   bonus = VALUE_ZERO;                     break;
        }

        // Increase bonus if supported by pawn, especially if the opponent has
        // no minor piece which can exchange the outpost piece.
        if (bonus && (ei.attacked_by[C][PAWN] & s))
        {
            if (   !pos.pieces (C_, NIHT)
                && !(squares_of_color(s) & pos.pieces (C_, BSHP)))
            {
                bonus += bonus + bonus / 2;
            }
            else
            {
                bonus += bonus / 2;
            }
        }

        return mk_score (bonus, bonus);
    }

    template<PType PT, Color C, bool TRACE>
    // evaluate_pieces<>() assigns bonuses and penalties to the pieces of a given color
    Score evaluate_ptype (const Position &pos, EvalInfo &ei, Score mobility[], Bitboard mobility_area)
    {
        Score score = SCORE_ZERO;

        const Color C_ = ((WHITE == C) ? BLACK : WHITE);
        const Square fk_sq = pos.king_sq (C);
        const Square ek_sq = pos.king_sq (C_);

        ei.attacked_by[C][PT] = U64 (0);

        const Square *pl = pos.piece_list<PT>(C);
        Square s;
        while ((s = *pl++) != SQ_NO)
        {
            // Find attacked squares, including x-ray attacks for bishops and rooks
            Bitboard attacks =
                (BSHP == PT) ? attacks_bb<BSHP>(s, pos.pieces () ^ pos.pieces (C, QUEN, BSHP)) :
                (ROOK == PT) ? attacks_bb<ROOK>(s, pos.pieces () ^ pos.pieces (C, QUEN, ROOK)) :
                pos.attacks_from<PT> (s);

            if (ei.pinned_pieces[C] & s)
            {
                attacks &= _lines_sq_bb[fk_sq][s];
            }

            ei.attacked_by[C][PT] |= attacks;

            if (attacks & ei.king_ring[C_])
            {
                ei.king_attackers_count[C]++;
                ei.king_attackers_weight[C] += KingAttackWeights[PT];

                Bitboard attacks_king = (attacks & ei.attacked_by[C_][KING]);
                if (attacks_king)
                {
                    ei.king_adjacent_zone_attacks_count[C] += pop_count<MAX15>(attacks_king);
                }
            }

            int32_t mob = (QUEN != PT)
                ? pop_count<MAX15>(attacks & mobility_area)
                : pop_count<FULL >(attacks & mobility_area);

            mobility[C] += MobilityBonus[PT][mob];

            // Decrease score if we are attacked by an enemy pawn. Remaining part
            // of threat evaluation must be done later when we have full attack info.
            if (ei.attacked_by[C_][PAWN] & s)
            {
                score -= ThreatenedByPawnPenalty[PT];
            }
            //// Otherwise give a bonus if we are a bishop and can pin a piece or can
            //// give a discovered check through an x-ray attack.
            //else if (    BSHP == PT
            //    && (attacks_bb<BSHP>(ek_sq) & s)
            //    && !more_than_one (betwen_sq_bb (s, ek_sq) & pos.pieces ()))
            //{
            //    score += BishopPinBonus;
            //}

            // Penalty for bishop with same coloured pawns
            if (BSHP == PT)
            {
                score -= BishopPawnsPenalty * ei.pi->pawns_on_same_color_squares(C, s);
            }

            // Penalty for knight when there are few enemy pawns
            if (NIHT == PT)
            {
                score -= KnightPawnsPenalty * max (5 - pos.piece_count<PAWN>(C_), 0);
            }

            if (BSHP == PT || NIHT == PT)
            {
                // Bishop and knight outposts squares
                if (!(pos.pieces (C_, PAWN) & pawn_attack_span_bb (C, s)))
                {
                    score += evaluate_outposts<PT, C>(pos, ei, s);
                }
                // Bishop or knight behind a pawn
                if (rel_rank (C, s) < R_5 && (pos.pieces (PAWN) & (s + pawn_push (C))))
                {
                    score += MinorBehindPawnBonus;
                }
            }

            if ((ROOK == PT || QUEN == PT) &&
                R_5 <= rel_rank (C, s))
            {
                // Major piece on 7th rank and enemy king trapped on 8th
                if (   R_7 == rel_rank (C, s)
                    && R_8 == rel_rank (C, ek_sq))
                {
                    switch (PT)
                    {
                    case ROOK: score +=  RookOn7thBonus; break;
                    case QUEN: score += QueenOn7thBonus; break;
                    }
                }

                // Major piece attacking enemy pawns on the same rank/file
                Bitboard pawns = pos.pieces (C_, PAWN) & attacks_bb<ROOK>(s);
                if (pawns)
                {
                    score += int32_t (pop_count<MAX15>(pawns)) * ((ROOK == PT) ? RookOnPawnBonus : QueenOnPawnBonus);
                }
            }

            // Special extra evaluation for rooks
            if (ROOK == PT)
            {
                // Give a bonus for a rook on a open or semi-open file
                if (ei.pi->semiopen (C, _file (s)))
                {
                    score += ei.pi->semiopen (C_, _file (s)) ? RookOpenFileBonus : RookSemiopenFileBonus;
                }
                if (mob > 3 || ei.pi->semiopen (C, _file (s)))
                {
                    continue;
                }

                // Penalize rooks which are trapped by a king. Penalize more if the
                // king has lost its castling capability.
                if (((_file (fk_sq) < F_E) == (_file (s) < _file (fk_sq))) &&
                    (_rank (fk_sq) == _rank (s) || R_1 == rel_rank (C, fk_sq)) &&
                    !ei.pi->semiopen_on_side(C, _file (fk_sq), _file (fk_sq) < F_E))
                {
                    score -= (TrappedRookPenalty - mk_score (mob * 8, 0)) * (pos.can_castle (C) ? 1 : 2);
                }
            }

            // An important Chess960 pattern: A cornered bishop blocked by a friendly
            // pawn diagonally in front of it is a very serious problem, especially
            // when that pawn is also blocked.
            if (BSHP == PT && pos.chess960 () &&
                (s == rel_sq (C, SQ_A1) || s == rel_sq (C, SQ_H1)))
            {
                const Piece P = (C | PAWN);
                Delta d = pawn_push (C) + ((F_A == _file (s)) ? DEL_E : DEL_W);
                if (pos[s + d] == P)
                {
                    score -=
                        !pos.empty(s + d + pawn_push (C)) ?
                        TrappedBishopA1H1 * 4 : (pos[s + d + d] == P) ?
                        TrappedBishopA1H1 * 2 : TrappedBishopA1H1;
                }
            }
        }

        if (TRACE)
        {
            Tracing::scores[C][PT] = score;
        }

        return score;
    }

    template<Color C, bool TRACE>
    // evaluate_pieces<>() assigns bonuses and penalties to all the pieces of a given color.
    Score evaluate_pieces (const Position &pos, EvalInfo &ei, Score mobility[])
    {
        const Color C_ = ((WHITE == C) ? BLACK : WHITE);

        // Do not include in mobility squares protected by enemy pawns or occupied by our pieces
        const Bitboard mobility_area = ~(ei.attacked_by[C_][PAWN] | pos.pieces (C, PAWN, KING));

        Score score =
            evaluate_ptype<NIHT, C, TRACE>(pos, ei, mobility, mobility_area);
        +   evaluate_ptype<BSHP, C, TRACE>(pos, ei, mobility, mobility_area);
        +   evaluate_ptype<ROOK, C, TRACE>(pos, ei, mobility, mobility_area);
        +   evaluate_ptype<QUEN, C, TRACE>(pos, ei, mobility, mobility_area);

        // Sum up all attacked squares
        ei.attacked_by[C][PT_NO] = 
            ei.attacked_by[C][PAWN] | ei.attacked_by[C][NIHT]
        |   ei.attacked_by[C][BSHP] | ei.attacked_by[C][ROOK]
        |   ei.attacked_by[C][QUEN] | ei.attacked_by[C][KING];

        if (TRACE)
        {
            Tracing::scores[C][MOBILITY] = apply_weight (mobility[C], Weights[Mobility]);
        }
        return score;
    }

    template<Color C, bool TRACE>
    // evaluate_king<>() assigns bonuses and penalties to a king of a given color
    Score evaluate_king     (const Position &pos, const EvalInfo &ei)
    {
        const Color C_ = ((WHITE == C) ? BLACK : WHITE);

        const Square k_sq = pos.king_sq (C);

        // King shelter and enemy pawns storm
        Score score = ei.pi->king_safety<C>(pos, k_sq);

        // Main king safety evaluation
        if (ei.king_attackers_count[C_])
        {
            // Find the attacked squares around the king which has no defenders
            // apart from the king itself
            Bitboard undefended = 
                ei.attacked_by[C_][PT_NO]
            &   ei.attacked_by[C][KING]
            & ~(ei.attacked_by[C][PAWN] | ei.attacked_by[C][NIHT]
            |   ei.attacked_by[C][BSHP] | ei.attacked_by[C][ROOK]
            |   ei.attacked_by[C][QUEN]);

            // Initialize the 'attack_units' variable, which is used later on as an
            // index to the KingDanger[] array. The initial value is based on the
            // number and types of the enemy's attacking pieces, the number of
            // attacked and undefended squares around our king, and the quality of
            // the pawn shelter (current 'score' value).
            int32_t attack_units =
                min (20, (ei.king_attackers_count[C_] * ei.king_attackers_weight[C_]) / 2)
                + 3 * (ei.king_adjacent_zone_attacks_count[C_] + pop_count<MAX15>(undefended))
                - mg_value (score) / 32;

            // Analyse enemy's safe queen contact checks. First find undefended
            // squares around the king attacked by enemy queen...
            Bitboard undefended_attacked = undefended & ei.attacked_by[C_][QUEN] & ~pos.pieces (C_);
            if (undefended_attacked)
            {
                // ...then remove squares not supported by another enemy piece
                undefended_attacked &=
                    (ei.attacked_by[C_][PAWN] | ei.attacked_by[C_][NIHT]
                |    ei.attacked_by[C_][BSHP] | ei.attacked_by[C_][ROOK]);

                if (undefended_attacked)
                {
                    attack_units += QueenContactCheck
                        *   pop_count<MAX15>(undefended_attacked)
                        *   (C_ == pos.active () ? 2 : 1);
                }
            }

            // Analyse enemy's safe rook contact checks. First find undefended
            // squares around the king attacked by enemy rooks...
            undefended_attacked = undefended & ei.attacked_by[C_][ROOK] & ~pos.pieces (C_);
            // Consider only squares where the enemy rook gives check
            undefended_attacked &= attacks_bb<ROOK>(k_sq);
            if (undefended_attacked)
            {
                // ...then remove squares not supported by another enemy piece
                undefended_attacked &= 
                    (ei.attacked_by[C_][PAWN] | ei.attacked_by[C_][NIHT]
                |    ei.attacked_by[C_][BSHP] | ei.attacked_by[C_][QUEN]);

                if (undefended_attacked)
                {
                    attack_units += RookContactCheck
                        *   pop_count<MAX15>(undefended_attacked)
                        *   (C_ == pos.active () ? 2 : 1);
                }
            }

            // Analyse enemy's safe distance checks for sliders and knights
            Bitboard safe_sq = ~(pos.pieces (C_) | ei.attacked_by[C][PT_NO]);

            Bitboard   rook_check = pos.attacks_from<ROOK>(k_sq) & safe_sq;
            Bitboard bishop_check = pos.attacks_from<BSHP>(k_sq) & safe_sq;

            Bitboard safe_check;
            // Enemy queen safe checks
            safe_check = (rook_check | bishop_check) & ei.attacked_by[C_][QUEN];
            if (safe_check) attack_units += QueenCheck * pop_count<MAX15>(safe_check);

            // Enemy rooks safe checks
            safe_check =   rook_check & ei.attacked_by[C_][ROOK];
            if (safe_check) attack_units += RookCheck * pop_count<MAX15>(safe_check);

            // Enemy bishops safe checks
            safe_check = bishop_check & ei.attacked_by[C_][BSHP];
            if (safe_check) attack_units += BishopCheck * pop_count<MAX15>(safe_check);

            // Enemy knights safe checks
            safe_check = pos.attacks_from<NIHT>(k_sq) & safe_sq & ei.attacked_by[C_][NIHT];
            if (safe_check) attack_units += KnightCheck * pop_count<MAX15>(safe_check);

            // To index KingDanger[] attack_units must be in [0, 99] range
            attack_units = min (max (0, attack_units), 99);

            // Finally, extract the king danger score from the KingDanger[]
            // array and subtract the score from evaluation. Set also margins[]
            // value that will be used for pruning because this value can sometimes
            // be very big, and so capturing a single attacking piece can therefore
            // result in a score change far bigger than the value of the captured piece.
            score -= KingDanger[C == Searcher::rootColor][attack_units];
        }

        if (TRACE)
        {
            Tracing::scores[C][KING] = score;
        }

        return score;
    }

    template<Color C, bool TRACE>
    // evaluate_threats<>() assigns bonuses according to the type of attacking piece
    // and the type of attacked one.
    Score evaluate_threats  (const Position &pos, const EvalInfo &ei)
    {
        const Color C_ = ((WHITE == C) ? BLACK : WHITE);

        Score score = SCORE_ZERO;

        // Undefended minors get penalized even if not under attack
        Bitboard undefended_minors = pos.pieces (C_, BSHP, NIHT) & ~ei.attacked_by[C_][PT_NO];
        if (undefended_minors)
        {
            score += UndefendedMinorPenalty;
        }

        // Enemy pieces not defended by a pawn and under our attack
        Bitboard weak_enemies = 
            pos.pieces (C_) &
            ~ei.attacked_by[C_][PAWN] &
            ei.attacked_by[C][PT_NO];
        // Add a bonus according if the attacking pieces are minor or major
        if (weak_enemies)
        {
            Bitboard attacked_enemies;
            attacked_enemies = weak_enemies & (ei.attacked_by[C][NIHT] | ei.attacked_by[C][BSHP]);
            if (attacked_enemies) score += ThreatBonus[0][p_type (pos[scan_lsq (attacked_enemies)])];

            attacked_enemies = weak_enemies & (ei.attacked_by[C][ROOK] | ei.attacked_by[C][QUEN]);
            if (attacked_enemies) score += ThreatBonus[1][p_type (pos[scan_lsq (attacked_enemies)])];
        }

        if (TRACE)
        {
            Tracing::scores[C][THREAT] = score;
        }

        return score;
    }

    template<Color C, bool TRACE>
    // evaluate_passed_pawns<>() evaluates the passed pawns of the given color
    Score evaluate_passed_pawns(const Position &pos, const EvalInfo &ei)
    {
        const Color C_ = ((WHITE == C) ? BLACK : WHITE);
        Score score = SCORE_ZERO;

        Bitboard passed_pawns = ei.pi->passed_pawns (C);

        while (passed_pawns)
        {
            Square s = pop_lsq (passed_pawns);

            ASSERT (pos.passed_pawn (C, s));

            int32_t r = int32_t (rel_rank (C, s)) - int32_t (R_2);
            int32_t rr = r * (r - 1);

            // Base bonus based on rank
            Value mg_bonus = Value (17 * rr);
            Value eg_bonus = Value ( 7 * (rr + r + 1));

            if (0 != rr)
            {
                Square block_sq = s + pawn_push (C);

                // Adjust bonus based on kings proximity
                eg_bonus +=
                    Value (square_dist (pos.king_sq (C_), block_sq) * 5 * rr) -
                    Value (square_dist (pos.king_sq (C ), block_sq) * 2 * rr);

                // If block_sq is not the queening square then consider also a second push
                if (rel_rank (C, block_sq) != R_8)
                {
                    eg_bonus -= Value (square_dist (pos.king_sq (C), block_sq + pawn_push (C)) * rr);
                }

                // If the pawn is free to advance, increase bonus
                if (pos.empty (block_sq))
                {
                    // squares to queen
                    Bitboard squares_queen = front_squares_bb (C, s);
                    Bitboard squares_defended, squares_unsafe;

                    // If there is an enemy rook or queen attacking the pawn from behind,
                    // add all X-ray attacks by the rook or queen. Otherwise consider only
                    // the squares in the pawn's path attacked or occupied by the enemy.
                    if (    UNLIKELY(front_squares_bb (C_, s) & pos.pieces (C_, ROOK, QUEN))
                        && (front_squares_bb (C_, s) & pos.pieces (C_, ROOK, QUEN) & pos.attacks_from<ROOK>(s)))
                    {
                        squares_unsafe = squares_queen;
                    }
                    else
                    {
                        squares_unsafe = squares_queen & (ei.attacked_by[C_][PT_NO] | pos.pieces (C_));
                    }

                    if (    UNLIKELY(front_squares_bb (C_, s) & pos.pieces (C, ROOK, QUEN))
                        && (front_squares_bb (C_, s) & pos.pieces (C, ROOK, QUEN) & pos.attacks_from<ROOK>(s)))
                    {
                        squares_defended = squares_queen;
                    }
                    else
                    {
                        squares_defended = squares_queen & ei.attacked_by[C][PT_NO];
                    }

                    // If there aren't enemy attacks huge bonus, a bit smaller if at
                    // least block square is not attacked, otherwise smallest bonus.
                    int32_t k = !squares_unsafe ? 15 : !(squares_unsafe & block_sq) ? 9 : 3;

                    // Big bonus if the path to queen is fully defended, a bit less
                    // if at least block square is defended.
                    if (squares_defended == squares_queen)
                    {
                        k += 6;
                    }
                    else if (squares_defended & block_sq)
                    {
                        k += ((squares_unsafe & squares_defended) == squares_unsafe) ? 4 : 2;
                    }

                    mg_bonus += Value (k * rr);
                    eg_bonus += Value (k * rr);
                }
            } // 0 != rr

            // Increase the bonus if the passed pawn is supported by a friendly pawn
            // on the same rank and a bit smaller if it's on the previous rank.
            Bitboard sup_passed_pawns = pos.pieces (C, PAWN) & adj_files_bb (_file (s));
            if (sup_passed_pawns & rank_bb (s))
            {
                eg_bonus += Value (r * 20);
            }
            else if (sup_passed_pawns & rank_bb (s - pawn_push (C)))
            {
                eg_bonus += Value (r * 12);
            }

            // Rook pawns are a special case: They are sometimes worse, and
            // sometimes better than other passed pawns. It is difficult to find
            // good rules for determining whether they are good or bad. For now,
            // we try the following: Increase the value for rook pawns if the
            // other side has no pieces apart from a knight, and decrease the
            // value if the other side has a rook or queen.
            if (file_bb (s) & (FA_bb | FH_bb))
            {
                if (pos.non_pawn_material (C_) <= VALUE_MG_KNIGHT)
                {
                    eg_bonus += eg_bonus / 4;
                }
                else if (pos.pieces (C_, ROOK, QUEN))
                {
                    eg_bonus -= eg_bonus / 4;
                }
            }

            // Increase the bonus if we have more non-pawn pieces
            if (pos.piece_count<PAWN>(C) < pos.piece_count<PAWN>(C_))
            {
                eg_bonus += eg_bonus / 4;
            }

            score += mk_score (mg_bonus, eg_bonus);

        }

        if (TRACE)
        {
            Tracing::scores[C][PASSED] = apply_weight (score, Weights[PassedPawns]);
        }

        // Add the scores to the middle game and endgame eval
        return apply_weight (score, Weights[PassedPawns]);
    }

    // evaluate_unstoppable_pawns() scores the most advanced among the passed and
    // candidate pawns. In case opponent has no pieces but pawns, this is somewhat
    // related to the possibility pawns are unstoppable.
    Score evaluate_unstoppable_pawns(const Position &pos, Color c, const EvalInfo &ei)
    {
        Bitboard unstoppable_pawns = ei.pi->passed_pawns (c) | ei.pi->candidate_pawns (c);
        if (!unstoppable_pawns || pos.non_pawn_material (~c))
        {
            return SCORE_ZERO;
        }
        return UnstoppablePawnBonus * int32_t (rel_rank (c, scan_rel_frntmost_sq(c, unstoppable_pawns)));
    }

    template<Color C>
    // evaluate_space() computes the space evaluation for a given side. The
    // space evaluation is a simple bonus based on the number of safe squares
    // available for minor pieces on the central four files on ranks 2--4. Safe
    // squares one, two or three squares behind a friendly pawn are counted
    // twice. Finally, the space bonus is scaled by a weight taken from the
    // material hash table. The aim is to improve play on game opening.
    int32_t evaluate_space  (const Position &pos, const EvalInfo &ei)
    {
        const Color C_ = ((WHITE == C) ? BLACK : WHITE);

        // Find the safe squares for our pieces inside the area defined by
        // SpaceMask[]. A square is unsafe if it is attacked by an enemy
        // pawn, or if it is undefended and attacked by an enemy piece.
        Bitboard safe = 
            SpaceMask[C] &
            ~pos.pieces (C, PAWN) &
            ~ei.attacked_by[C_][PAWN] &
            (ei.attacked_by[C][PT_NO] | ~ei.attacked_by[C_][PT_NO]);

        // Find all squares which are at most three squares behind some friendly pawn
        Bitboard behind = pos.pieces (C, PAWN);
        behind |= ((WHITE == C) ? behind >>  8 : behind <<  8);
        behind |= ((WHITE == C) ? behind >> 16 : behind << 16);

        // Since SpaceMask[C] is fully on our half of the board
        ASSERT (uint32_t (safe >> ((WHITE == C) ? 32 : 0)) == 0);

        // Count safe + (behind & safe) with a single pop_count
        return pop_count<FULL>(((WHITE == C) ? safe << 32 : safe >> 32) | (behind & safe));
    }

    // interpolate () interpolates between a middle game and an endgame score,
    // based on game phase. It also scales the return value by a ScaleFactor array.
    Value interpolate (const Score &score, Phase ph, ScaleFactor sf)
    {
        ASSERT (-VALUE_INFINITE < mg_value (score) && mg_value (score) < +VALUE_INFINITE);
        ASSERT (-VALUE_INFINITE < eg_value (score) && eg_value (score) < +VALUE_INFINITE);
        ASSERT (PHASE_ENDGAME <= ph && ph <= PHASE_MIDGAME);

        int32_t e = (eg_value (score) * int32_t (sf)) / SCALE_FACTOR_NORMAL;
        int32_t r = (mg_value (score) * int32_t (ph) + e * int32_t (PHASE_MIDGAME - ph)) / PHASE_MIDGAME;
        return Value ((r / GrainSize) * GrainSize); // Sign independent
    }

    // apply_weight () weights 'score' by factor 'w' trying to prevent overflow
    Score apply_weight (Score score, Score w)
    {
        return mk_score ((int32_t (mg_value (score)) * mg_value (w)) / 0x100, (int32_t (eg_value (score)) * eg_value (w)) / 0x100);
    }

    // weight_option () computes the value of an evaluation weight, by combining
    // two UCI-configurable weights (midgame and endgame) with an internal weight.
    Score weight_option (const string &mg_opt, const string &eg_opt, Score internal_weight)
    {
        // Scale option value from 100 to 256
        int16_t mg = int32_t (*(Options[mg_opt])) * 256 / 100;
        int16_t eg = int32_t (*(Options[eg_opt])) * 256 / 100;

        return apply_weight (mk_score (mg, eg), internal_weight);
    }

    double to_cp (Value value) { return double (value) / double (VALUE_MG_PAWN); }

    namespace Tracing {

        void add (int32_t idx, Score w_score, Score b_score)
        {
            scores[WHITE][idx] = w_score;
            scores[BLACK][idx] = b_score;
        }

        void row (const char name[], int32_t idx)
        {
            Score w_score = scores[WHITE][idx];
            Score b_score = scores[BLACK][idx];
            switch (idx)
            {
            case PST: case IMBALANCE: case PAWN: case TOTAL:
                stream << setw(20) << name << " |   ---   --- |   ---   --- | "
                    << setw(6)  << to_cp (mg_value (w_score)) << " "
                    << setw(6)  << to_cp (eg_value (w_score)) << " \n";
                break;

            default:
                stream << setw(20) << name << " | " << noshowpos
                    << setw(5)  << to_cp (mg_value (w_score)) << " "
                    << setw(5)  << to_cp (eg_value (w_score)) << " | "
                    << setw(5)  << to_cp (mg_value (b_score)) << " "
                    << setw(5)  << to_cp (eg_value (b_score)) << " | "
                    << showpos
                    << setw(6)  << to_cp (mg_value (w_score - b_score)) << " "
                    << setw(6)  << to_cp (eg_value (w_score - b_score)) << " \n";
            }
        }

        string do_trace (const Position &pos)
        {
            stream.str ("");
            stream << showpoint << showpos << fixed << setprecision(2);
            memset (scores, 0, 2 * (TOTAL + 1) * sizeof (Score));

            do_evaluate<true>(pos);

            string totals = stream.str ();
            stream.str ("");

            stream << setw(21) << "Eval term |    White    |    Black    |     Total     \n"
                <<          "                     |   MG    EG  |   MG    EG  |   MG     EG   \n"
                <<          "---------------------+-------------+-------------+---------------\n";

            row ("Material, PST, Tempo", PST);
            row ("Material imbalance",   IMBALANCE);
            row ("Pawns",                PAWN);
            row ("Knights",              NIHT);
            row ("Bishops",              BSHP);
            row ("Rooks",                ROOK);
            row ("Queens",               QUEN);
            row ("Mobility",             MOBILITY);
            row ("King safety",          KING);
            row ("Threats",              THREAT);
            row ("Passed pawns",         PASSED);
            row ("Space",                SPACE);
            stream <<       "---------------------+-------------+-------------+---------------\n";
            row ("Total",                TOTAL);
            stream << totals;

            return stream.str ();
        }
    }

}

namespace Evaluator {

    // evaluate() is the main evaluation function. It always computes two
    // values, an endgame score and a middle game score, and interpolates
    // between them based on the remaining material.
    Value evaluate  (const Position &pos)
    {
        return do_evaluate<false> (pos);
    }

    // trace() is like evaluate() but instead of a value returns a string suitable
    // to be print on stdout with the detailed descriptions and values of each
    // evaluation term. Used mainly for debugging.
    string trace(const Position &pos)
    {
        return Tracing::do_trace (pos);
    }

    // initialize() computes evaluation weights from the corresponding UCI parameters
    // and setup king tables.
    void initialize ()
    {
        Weights[Mobility]       = weight_option ("Mobility (Midgame)",       "Mobility (Endgame)",       WeightsInternal[Mobility]);
        Weights[PawnStructure]  = weight_option ("Pawn Structure (Midgame)", "Pawn Structure (Endgame)", WeightsInternal[PawnStructure]);
        Weights[PassedPawns]    = weight_option ("Passed Pawns (Midgame)",   "Passed Pawns (Endgame)",   WeightsInternal[PassedPawns]);
        Weights[Space]          = weight_option ("Space",                    "Space",                    WeightsInternal[Space]);
        Weights[KingDanger_C]   = weight_option ("Cowardice",                "Cowardice",                WeightsInternal[KingDanger_C]);
        Weights[KingDanger_C_]  = weight_option ("Aggressiveness",           "Aggressiveness",           WeightsInternal[KingDanger_C_]);

        const int32_t MaxSlope  = 30;
        const int32_t PeakScore = 1280; // 0x500

        for (int32_t t = 0, i = 1; i < 100; ++i)
        {
            t = min (PeakScore, min (int32_t (0.4 * i * i), t + MaxSlope));

            KingDanger[1][i] = apply_weight (mk_score (t, 0), Weights[KingDanger_C]);
            KingDanger[0][i] = apply_weight (mk_score (t, 0), Weights[KingDanger_C_]);
        }
    }

} // namespace Eval
