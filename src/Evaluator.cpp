#include "Evaluator.h"
#include <cassert>
#include <iomanip>
#include <sstream>
#include <algorithm>

#include "BitCount.h"
#include "BitBoard.h"
#include "UCI.h"

#include "Position.h"

using namespace std;
using namespace BitBoard;

namespace {

    // Used for tracing
    enum ExtendedPieceType
    { 
        PST = 8, IMBALANCE, MOBILITY, THREAT, PASSED, SPACE, TOTAL
    };

    namespace Tracing {

        Score scores[CLR_NO][TOTAL + 1];
        std::stringstream stream;

        void add(int32_t idx, Score term_w, Score term_b = SCORE_ZERO);
        void row(const char* name, int32_t idx);
        std::string do_trace(const Position &pos);

    }

    // Struct EvalInfo contains various information computed and collected
    // by the evaluation functions.
    struct EvalInfo {

        // Pointers to material and pawn hash table entries
        //Material::Entry* mi;
        //Pawns::Entry* pi;

        // attackedBy[color][piece type] is a bitboard representing all squares
        // attacked by a given color and piece type, attackedBy[color][ALL_PIECES]
        // contains all squares attacked by the given color.
        Bitboard attackedBy[CLR_NO][PT_NO];

        // kingRing[color] is the zone around the king which is considered
        // by the king safety evaluation. This consists of the squares directly
        // adjacent to the king, and the three (or two, for a king on an edge file)
        // squares two ranks in front of the king. For instance, if black's king
        // is on g8, kingRing[BLACK] is a bitboard containing the squares f8, h8,
        // f7, g7, h7, f6, g6 and h6.
        Bitboard kingRing[CLR_NO];

        // kingAttackersCount[color] is the number of pieces of the given color
        // which attack a square in the kingRing of the enemy king.
        int32_t kingAttackersCount[CLR_NO];

        // kingAttackersWeight[color] is the sum of the "weight" of the pieces of the
        // given color which attack a square in the kingRing of the enemy king. The
        // weights of the individual piece types are given by the variables
        // QueenAttackWeight, RookAttackWeight, BishopAttackWeight and
        // KnightAttackWeight in evaluate.cpp
        int32_t kingAttackersWeight[CLR_NO];

        // kingAdjacentZoneAttacksCount[color] is the number of attacks to squares
        // directly adjacent to the king of the given color. Pieces which attack
        // more than one square are counted multiple times. For instance, if black's
        // king is on g8 and there's a white knight on g5, this knight adds
        // 2 to kingAdjacentZoneAttacksCount[BLACK].
        int32_t kingAdjacentZoneAttacksCount[CLR_NO];
    };

    // Evaluation grain size, must be a power of 2
    const int32_t GrainSize = 4;

    // Evaluation weights, initialized from UCI options
    enum EvalWeights { Mobility, PawnStructure, PassedPawns, Space, KingDangerUs, KingDangerThem };

    Score Weights[6];

    typedef Value V;

#define S(mg, eg) make_score(mg, eg)

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

    // MobilityBonus[PieceType][attacked] contains bonuses for middle and end
    // game, indexed by piece type and number of attacked squares not occupied by
    // friendly pieces.
    const Score MobilityBonus[][32] =
    {
        {},
        {},
        // Knights
        {S(-35,-30), S(-22,-20), S(-9,-10), S( 3,  0), S(15, 10),
        S(27, 20), S( 37, 28), S( 42, 31), S(44, 33)
        },
        // Bishops
        {S(-22,-27), S( -8,-13), S( 6,  1), S(20, 15), S(34, 29), S(48, 43), S( 60, 55),
        S( 68, 63), S(74, 68), S(77, 72), S(80, 75), S(82, 77), S( 84, 79), S( 86, 81)
        },
        // Rooks
        {S(-17,-33), S(-11,-16), S(-5,  0), S( 1, 16), S( 7, 32), S(13, 48),
        S( 18, 64), S( 22, 80), S(26, 96), S(29,109), S(31,115), S(33,119),
        S( 35,122), S( 36,123), S(37,124),
        },
        // Queens
        {S(-12,-20), S( -8,-13), S(-5, -7), S(-2, -1), S( 1,  5), S( 4, 11),
        S(  7, 17), S( 10, 23), S(13, 29), S(16, 34), S(18, 38), S(20, 40),
        S( 22, 41), S( 23, 41), S(24, 41), S(25, 41), S(25, 41), S(25, 41),
        S( 25, 41), S( 25, 41), S(25, 41), S(25, 41), S(25, 41), S(25, 41),
        S( 25, 41), S( 25, 41), S(25, 41), S(25, 41)
        },
    };

    // Outpost[PieceType][Square] contains bonuses of knights and bishops, indexed
    // by piece type and square (from white's point of view).
    const Value Outpost[][SQ_NO] =
    {
        // A     B     C     D     E     F     G     H
        // KNIGHTS
        {V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        V(0), V(0), V(4), V(8), V(8), V(4), V(0), V(0),
        V(0), V(4),V(17),V(26),V(26),V(17), V(4), V(0),
        V(0), V(8),V(26),V(35),V(35),V(26), V(8), V(0),
        V(0), V(4),V(17),V(17),V(17),V(17), V(4), V(0)
        },
        // BISHOPS
        {V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        V(0), V(0), V(0), V(0), V(0), V(0), V(0), V(0),
        V(0), V(0), V(5), V(5), V(5), V(5), V(0), V(0),
        V(0), V(5),V(10),V(10),V(10),V(10), V(5), V(0),
        V(0),V(10),V(21),V(21),V(21),V(21),V(10), V(0),
        V(0), V(5), V(8), V(8), V(8), V(8), V(5), V(0)
        }
    };

    // Threat[attacking][attacked] contains bonuses according to which piece
    // type attacks which one.
    const Score Threat[][PT_NO] =
    {
        {}, {},
        { S(0, 0), S( 7, 39), S( 0,  0), S(24, 49), S(41,100), S(41,100) }, // NIHT
        { S(0, 0), S( 7, 39), S(24, 49), S( 0,  0), S(41,100), S(41,100) }, // BSHP
        { S(0, 0), S( 0, 22), S(15, 49), S(15, 49), S( 0,  0), S(24, 49) }, // ROOK
        { S(0, 0), S(15, 39), S(15, 39), S(15, 39), S(15, 39), S( 0,  0) }  // QUEEN
    };

    // ThreatenedByPawn[PieceType] contains a penalty according to which piece
    // type is attacked by an enemy pawn.
    const Score ThreatenedByPawn[] =
    {
        S(0, 0), S(0, 0), S(56, 70), S(56, 70), S(76, 99), S(86, 118)
    };

#undef S

    const Score Tempo            = make_score(24, 11);
    const Score BishopPin        = make_score(66, 11);
    const Score RookOn7th        = make_score(11, 20);
    const Score QueenOn7th       = make_score( 3,  8);
    const Score RookOnPawn       = make_score(10, 28);
    const Score QueenOnPawn      = make_score( 4, 20);
    const Score RookOpenFile     = make_score(43, 21);
    const Score RookSemiopenFile = make_score(19, 10);
    const Score BishopPawns      = make_score( 8, 12);
    const Score MinorBehindPawn  = make_score(16,  0);
    const Score UndefendedMinor  = make_score(25, 10);
    const Score TrappedRook      = make_score(90,  0);
    const Score Unstoppable      = make_score( 0, 20);

    // Penalty for a bishop on a1/h1 (a8/h8 for black) which is trapped by
    // a friendly pawn on b2/g2 (b7/g7 for black). This can obviously only
    // happen in Chess960 games.
    const Score TrappedBishopA1H1 = make_score(50, 50);

    // The SpaceMask[Color] contains the area of the board which is considered
    // by the space evaluation. In the middle game, each side is given a bonus
    // based on how many squares inside this area are safe and available for
    // friendly minor pieces.
    const Bitboard SpaceMask[] =
    {
        (bb_FC | bb_FD | bb_FE | bb_FB) & (bb_R2 | bb_R3 | bb_R4),
        (bb_FC | bb_FD | bb_FE | bb_FB) & (bb_R7 | bb_R6 | bb_R5)
    };

    // King danger constants and variables. The king danger scores are taken
    // from the KingDanger[]. Various little "meta-bonuses" measuring
    // the strength of the enemy attack are added up into an integer, which
    // is used as an index to KingDanger[].
    //
    // KingAttackWeights[PieceType] contains king attack weights by piece type
    const int32_t KingAttackWeights[] = { 0, 0, 2, 2, 3, 5 };

    // Bonuses for enemy's safe checks
    const int32_t QueenContactCheck = 6;
    const int32_t RookContactCheck  = 4;
    const int32_t QueenCheck        = 3;
    const int32_t RookCheck         = 2;
    const int32_t BishopCheck       = 1;
    const int32_t KnightCheck       = 1;

    // KingExposed[Square] contains penalties based on the position of the
    // defending king, indexed by king's square (from white's point of view).
    const int32_t KingExposed[] =
    {
        2,  0,  2,  5,  5,  2,  0,  2,
        2,  2,  4,  8,  8,  4,  2,  2,
        7, 10, 12, 12, 12, 12, 10,  7,
        15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15,
        15, 15, 15, 15, 15, 15, 15, 15
    };

    // KingDanger[Color][attackUnits] contains the actual king danger weighted
    // scores, indexed by color and by a calculated integer number.
    Score KingDanger[CLR_NO][128];

    // Function prototypes
    template<bool Trace>
    Value do_evaluate(const Position &pos, Value& margin);

    template<Color C>
    void init_eval_info(const Position &pos, EvalInfo& ei);

    template<Color C, bool Trace>
    Score evaluate_pieces_of_color(const Position &pos, EvalInfo& ei, Score& mobility);

    template<Color C, bool Trace>
    Score evaluate_king(const Position &pos, const EvalInfo& ei, Value margins[]);

    template<Color C, bool Trace>
    Score evaluate_threats(const Position &pos, const EvalInfo& ei);

    template<Color C, bool Trace>
    Score evaluate_passed_pawns(const Position &pos, const EvalInfo& ei);

    template<Color C>
    int32_t evaluate_space(const Position &pos, const EvalInfo& ei);

    Score evaluate_unstoppable_pawns(const Position &pos, Color us, const EvalInfo& ei);

    Value interpolate(const Score& v, Phase ph, ScaleFactor sf);
    Score apply_weight(Score v, Score w);
    Score weight_option(const std::string& mgOpt, const std::string& egOpt, Score internalWeight);
    double to_cp(Value v);


    template<bool Trace>
    Value do_evaluate(const Position &pos, Value &margin)
    {
        // TODO::

        assert(!pos.checkers());

        EvalInfo ei;
        Value margins[CLR_NO];
        Score score, mobilityWhite, mobilityBlack;
        //Thread* th = pos.this_thread();

        /*
        // margins[] store the uncertainty estimation of position's evaluation
        // that typically is used by the search for pruning decisions.
        margins[WHITE] = margins[BLACK] = VALUE_ZERO;

        // Initialize score by reading the incrementally updated scores included
        // in the position object (material + piece square tables) and adding
        // Tempo bonus. Score is computed from the point of view of white.
        score = pos.psq_score() + (pos.active () == WHITE ? Tempo : -Tempo);

        // Probe the material hash table
        ei.mi = Material::probe(pos, th->materialTable, th->endgames);
        score += ei.mi->material_value();

        // If we have a specialized evaluation function for the current material
        // configuration, call it and return.
        if (ei.mi->specialized_eval_exists())
        {
        margin = VALUE_ZERO;
        return ei.mi->evaluate(pos);
        }

        // Probe the pawn hash table
        ei.pi = Pawns::probe(pos, th->pawnsTable);
        score += apply_weight(ei.pi->pawns_value(), Weights[PawnStructure]);

        // Initialize attack and king safety bitboards
        init_eval_info<WHITE>(pos, ei);
        init_eval_info<BLACK>(pos, ei);
        */
        /*
        // Evaluate pieces and mobility
        score +=  evaluate_pieces_of_color<WHITE, Trace>(pos, ei, mobilityWhite)
        - evaluate_pieces_of_color<BLACK, Trace>(pos, ei, mobilityBlack);

        score += apply_weight(mobilityWhite - mobilityBlack, Weights[Mobility]);

        // Evaluate kings after all other pieces because we need complete attack
        // information when computing the king safety evaluation.
        score +=  evaluate_king<WHITE, Trace>(pos, ei, margins)
        - evaluate_king<BLACK, Trace>(pos, ei, margins);

        // Evaluate tactical threats, we need full attack information including king
        score +=  evaluate_threats<WHITE, Trace>(pos, ei)
        - evaluate_threats<BLACK, Trace>(pos, ei);

        // Evaluate passed pawns, we need full attack information including king
        score +=  evaluate_passed_pawns<WHITE, Trace>(pos, ei)
        - evaluate_passed_pawns<BLACK, Trace>(pos, ei);

        // If one side has only a king, score for potential unstoppable pawns
        if (!pos.non_pawn_material(WHITE) || !pos.non_pawn_material(BLACK))
        score +=  evaluate_unstoppable_pawns(pos, WHITE, ei)
        - evaluate_unstoppable_pawns(pos, BLACK, ei);

        // Evaluate space for both sides, only in middle-game.
        if (ei.mi->space_weight())
        {
        int32_t s = evaluate_space<WHITE>(pos, ei) - evaluate_space<BLACK>(pos, ei);
        score += apply_weight(s * ei.mi->space_weight(), Weights[Space]);
        }

        // Scale winning side if position is more drawish that what it appears
        ScaleFactor sf = eg_value(score) > VALUE_DRAW ? ei.mi->scale_factor(pos, WHITE)
        : ei.mi->scale_factor(pos, BLACK);

        // If we don't already have an unusual scale factor, check for opposite
        // colored bishop endgames, and use a lower scale for those.
        if (   ei.mi->game_phase() < PHASE_MIDGAME
        && pos.has_opposite_bishops()
        && sf == SCALE_FACTOR_NORMAL)
        {
        // Only the two bishops ?
        if (   pos.non_pawn_material(WHITE) == VALUE_MG_BISHOP
        && pos.non_pawn_material(BLACK) == VALUE_MG_BISHOP)
        {
        // Check for KBP vs KB with only a single pawn that is almost
        // certainly a draw or at least two pawns.
        bool one_pawn = (pos.piece_count<PAWN>(WHITE) + pos.piece_count<PAWN>(BLACK) == 1);
        sf = one_pawn ? ScaleFactor(8) : ScaleFactor(32);
        }
        else
        // Endgame with opposite-colored bishops, but also other pieces. Still
        // a bit drawish, but not as drawish as with only the two bishops.
        sf = ScaleFactor(50);
        }

        margin = margins[pos.active ()];
        Value v = interpolate(score, ei.mi->game_phase(), sf);

        // In case of tracing add all single evaluation contributions for both white and black
        if (Trace)
        {
        Tracing::add(PST, pos.psq_score());
        Tracing::add(IMBALANCE, ei.mi->material_value());
        Tracing::add(PAWN, ei.pi->pawns_value());
        Score w = ei.mi->space_weight() * evaluate_space<WHITE>(pos, ei);
        Score b = ei.mi->space_weight() * evaluate_space<BLACK>(pos, ei);
        Tracing::add(SPACE, apply_weight(w, Weights[Space]), apply_weight(b, Weights[Space]));
        Tracing::add(TOTAL, score);
        Tracing::stream << "\nUncertainty margin: White: " << to_cp(margins[WHITE])
        << ", Black: " << to_cp(margins[BLACK])
        << "\nScaling: " << std::noshowpos
        << std::setw(6) << 100.0 * ei.mi->game_phase() / 128.0 << "% MG, "
        << std::setw(6) << 100.0 * (1.0 - ei.mi->game_phase() / 128.0) << "% * "
        << std::setw(6) << (100.0 * sf) / SCALE_FACTOR_NORMAL << "% EG.\n"
        << "Total evaluation: " << to_cp(v);
        }

        return pos.active () == WHITE ? v : -v;
        */
        return VALUE_DRAW;
    }


    // init_eval_info() initializes king bitboards for given color adding
    // pawn attacks. To be done at the beginning of the evaluation.

    template<Color C>
    void init_eval_info(const Position &pos, EvalInfo& ei)
    {

        const Color  _C = ~C;
        const Square Down = (C == WHITE ? DELTA_S : DELTA_N);

        Bitboard b = ei.attackedBy[_C][KING] = pos.attacks_from<KING>(pos.king_square(_C));
        ei.attackedBy[C][PAWN] = ei.pi->pawn_attacks(C);

        // Init king safety tables only if we are going to use them
        if (pos.piece_count<QUEEN>(C) && pos.non_pawn_material(C) > QueenValueMg + PawnValueMg)
        {
            ei.kingRing[_C] = b | shift_bb<Down>(b);
            b &= ei.attackedBy[C][PAWN];
            ei.kingAttackersCount[C] = b ? pop_count<Max15>(b) / 2 : 0;
            ei.kingAdjacentZoneAttacksCount[C] = ei.kingAttackersWeight[C] = 0;
        } else
            ei.kingRing[_C] = ei.kingAttackersCount[C] = 0;
    }

    // evaluate_outposts() evaluates bishop and knight outposts squares
    template<PType Piece, Color C>
    Score evaluate_outposts(const Position &pos, EvalInfo& ei, Square s)
    {

        const Color _C = ~C;

        assert (Piece == BSHP || Piece == NIHT);

        // Initial bonus based on square
        Value bonus = Outpost[Piece == BSHP][rel_sq(C, s)];

        // Increase bonus if supported by pawn, especially if the opponent has
        // no minor piece which can exchange the outpost piece.
        if (bonus && (ei.attackedBy[C][PAWN] & s))
        {
            if (   !pos.pieces(_C, NIHT)
                && !(squares_of_color(s) & pos.pieces(_C, BSHP)))
                bonus += bonus + bonus / 2;
            else
                bonus += bonus / 2;
        }
        return make_score(bonus, bonus);
    }

    // evaluate_pieces<>() assigns bonuses and penalties to the pieces of a given color
    template<PType T, Color C, bool Trace>
    Score evaluate_pieces(const Position &pos, EvalInfo& ei, Score& mobility, Bitboard mobilityArea)
    {
        Bitboard b;
        Square s;
        Score score = SCORE_ZERO;

        const Color _C = ~C;
        const Square* pl = pos.list<T>(C);

        ei.attackedBy[C][T] = 0;

        while ((s = *pl++) != SQ_NONE)
        {
            // Find attacked squares, including x-ray attacks for bishops and rooks
            b = T == BSHP ? attacks_bb<BSHP>(s, pos.pieces() ^ pos.pieces(C, QUEEN))
                : T ==   ROOK ? attacks_bb<  ROOK>(s, pos.pieces() ^ pos.pieces(C, ROOK, QUEEN))
                : pos.attacks_from<T>(s);

            ei.attackedBy[C][T] |= b;

            if (b & ei.kingRing[_C])
            {
                ++ei.kingAttackersCount[C];
                ei.kingAttackersWeight[C] += KingAttackWeights[T];
                Bitboard bb = (b & ei.attackedBy[_C][KING]);
                if (bb)
                    ei.kingAdjacentZoneAttacksCount[C] += pop_count<Max15>(bb);
            }

            int32_t mob = T != QUEEN ? pop_count<Max15>(b & mobilityArea)
                : pop_count<FULL>(b & mobilityArea);

            mobility += MobilityBonus[T][mob];

            // Decrease score if we are attacked by an enemy pawn. Remaining part
            // of threat evaluation must be done later when we have full attack info.
            if (ei.attackedBy[_C][PAWN] & s)
                score -= ThreatenedByPawn[T];

            // Otherwise give a bonus if we are a bishop and can pin a piece or can
            // give a discovered check through an x-ray attack.
            else if (    T == BSHP
                && (PseudoAttacks[T][pos.king_square(_C)] & s)
                && !more_than_one(BetweenBB[s][pos.king_square(_C)] & pos.pieces()))
                score += BishopPin;

            // Penalty for bishop with same coloured pawns
            if (T == BSHP)
            {
                score -= BishopPawns * ei.pi->pawns_on_same_color_squares(C, s);
            }

            if (T == BSHP || T == NIHT)
            {
                // Bishop and knight outposts squares
                if (!(pos.pieces(_C, PAWN) & pawn_attack_span(C, s)))
                    score += evaluate_outposts<T, C>(pos, ei, s);

                // Bishop or knight behind a pawn
                if (    rel_rank(C, s) < RANK_5
                    && (pos.pieces(PAWN) & (s + pawn_push(C))))
                    score += MinorBehindPawn;
            }

            if (  (T == ROOK || T == QUEEN)
                && rel_rank(C, s) >= RANK_5)
            {
                // Major piece on 7th rank and enemy king trapped on 8th
                if (   rel_rank(C, s) == RANK_7
                    && rel_rank(C, pos.king_square(_C)) == RANK_8)
                    score += T == ROOK ? RookOn7th : QueenOn7th;

                // Major piece attacking enemy pawns on the same rank/file
                Bitboard pawns = pos.pieces(_C, PAWN) & PseudoAttacks[ROOK][s];
                if (pawns)
                    score += pop_count<Max15>(pawns) * (T == ROOK ? RookOnPawn : QueenOnPawn);
            }

            // Special extra evaluation for rooks
            if (T == ROOK)
            {
                // Give a bonus for a rook on a open or semi-open file
                if (ei.pi->semiopen(C, _file (s)))
                    score += ei.pi->semiopen(_C, _file (s)) ? RookOpenFile : RookSemiopenFile;

                if (mob > 3 || ei.pi->semiopen(C, _file (s)))
                    continue;

                Square ksq = pos.king_square(C);

                // Penalize rooks which are trapped inside a king. Penalize more if
                // king has lost right to castle.
                if (   ((_file (ksq) < FILE_E) == (_file (s) < _file (ksq)))
                    && (rank_of(ksq) == rank_of(s) || rel_rank(C, ksq) == RANK_1)
                    && !ei.pi->semiopen_on_side(C, _file (ksq), _file (ksq) < FILE_E))
                    score -= (TrappedRook - make_score(mob * 8, 0)) * (pos.can_castle(C) ? 1 : 2);
            }

            // An important Chess960 pattern: A cornered bishop blocked by a friendly
            // pawn diagonally in front of it is a very serious problem, especially
            // when that pawn is also blocked.
            if (   T == BSHP
                && pos.chess960()
                && (s == rel_sq(C, SQ_A1) || s == rel_sq(C, SQ_H1)))
            {
                const enum T P = make_piece(C, PAWN);
                Square d = pawn_push(C) + (_file (s) == FILE_A ? DELTA_E : DELTA_W);
                if (pos.piece_on(s + d) == P)
                    score -= !pos.empty(s + d + pawn_push(C)) ? TrappedBishopA1H1 * 4
                    : pos.piece_on(s + d + d) == P     ? TrappedBishopA1H1 * 2
                    : TrappedBishopA1H1;
            }
        }

        if (Trace)
            Tracing::scores[C][T] = score;

        return score;
    }


    // evaluate_threats<>() assigns bonuses according to the type of attacking piece
    // and the type of attacked one.

    template<Color C, bool Trace>
    Score evaluate_threats(const Position &pos, const EvalInfo& ei) {

        const Color _C = ~C;

        Bitboard b, undefendedMinors, weakEnemies;
        Score score = SCORE_ZERO;

        // Undefended minors get penalized even if not under attack
        undefendedMinors =  pos.pieces(_C, BSHP, NIHT)
            & ~ei.attackedBy[_C][ALL_PIECES];

        if (undefendedMinors)
            score += UndefendedMinor;

        // Enemy pieces not defended by a pawn and under our attack
        weakEnemies =  pos.pieces(_C)
            & ~ei.attackedBy[_C][PAWN]
        & ei.attackedBy[C][ALL_PIECES];

        // Add bonus according to type of attacked enemy piece and to the
        // type of attacking piece, from knights to queens. Kings are not
        // considered because are already handled in king evaluation.
        if (weakEnemies)
            for (PieceType pt1 = NIHT; pt1 < KING; ++pt1)
            {
                b = ei.attackedBy[C][pt1] & weakEnemies;
                if (b)
                    for (PieceType pt2 = PAWN; pt2 < KING; ++pt2)
                        if (b & pos.pieces(pt2))
                            score += Threat[pt1][pt2];
            }

            if (Trace)
                Tracing::scores[C][THREAT] = score;

            return score;
    }


    // evaluate_pieces_of_color<>() assigns bonuses and penalties to all the
    // pieces of a given color.

    template<Color C, bool Trace>
    Score evaluate_pieces_of_color(const Position &pos, EvalInfo& ei, Score& mobility) {

        const Color _C = ~C;

        Score score = mobility = SCORE_ZERO;

        // Do not include in mobility squares protected by enemy pawns or occupied by our pieces
        const Bitboard mobilityArea = ~(ei.attackedBy[_C][PAWN] | pos.pieces(C, PAWN, KING));

        score += evaluate_pieces<NIHT, C, Trace>(pos, ei, mobility, mobilityArea);
        score += evaluate_pieces<BSHP, C, Trace>(pos, ei, mobility, mobilityArea);
        score += evaluate_pieces<ROOK,   C, Trace>(pos, ei, mobility, mobilityArea);
        score += evaluate_pieces<QUEEN,  C, Trace>(pos, ei, mobility, mobilityArea);

        // Sum up all attacked squares
        ei.attackedBy[C][ALL_PIECES] =   ei.attackedBy[C][PAWN]   | ei.attackedBy[C][NIHT]
        | ei.attackedBy[C][BSHP] | ei.attackedBy[C][ROOK]
        | ei.attackedBy[C][QUEEN]  | ei.attackedBy[C][KING];
        if (Trace)
            Tracing::scores[C][MOBILITY] = apply_weight(mobility, Weights[Mobility]);

        return score;
    }

    // evaluate_king<>() assigns bonuses and penalties to a king of a given color
    template<Color C, bool Trace>
    Score evaluate_king(const Position &pos, const EvalInfo& ei, Value margins[]) {

        const Color _C = ~C;

        Bitboard undefended, b, b1, b2, safe;
        int32_t attackUnits;
        const Square ksq = pos.king_square(C);

        // King shelter and enemy pawns storm
        Score score = ei.pi->king_safety<C>(pos, ksq);

        // King safety. This is quite complicated, and is almost certainly far
        // from optimally tuned.
        if (   ei.kingAttackersCount[_C] >= 2
            && ei.kingAdjacentZoneAttacksCount[_C])
        {
            // Find the attacked squares around the king which has no defenders
            // apart from the king itself
            undefended = ei.attackedBy[_C][ALL_PIECES] & ei.attackedBy[C][KING];
            undefended &= ~(  ei.attackedBy[C][PAWN]   | ei.attackedBy[C][NIHT]
            | ei.attackedBy[C][BSHP] | ei.attackedBy[C][ROOK]
            | ei.attackedBy[C][QUEEN]);

            // Initialize the 'attackUnits' variable, which is used later on as an
            // index to the KingDanger[] array. The initial value is based on the
            // number and types of the enemy's attacking pieces, the number of
            // attacked and undefended squares around our king, the square of the
            // king, and the quality of the pawn shelter.
            attackUnits =  std::min(25, (ei.kingAttackersCount[_C] * ei.kingAttackersWeight[_C]) / 2)
                + 3 * (ei.kingAdjacentZoneAttacksCount[_C] + pop_count<Max15>(undefended))
                + KingExposed[rel_sq(C, ksq)]
            - mg_value(score) / 32;

            // Analyse enemy's safe queen contact checks. First find undefended
            // squares around the king attacked by enemy queen...
            b = undefended & ei.attackedBy[_C][QUEEN] & ~pos.pieces(_C);
            if (b)
            {
                // ...then remove squares not supported by another enemy piece
                b &= (  ei.attackedBy[_C][PAWN]   | ei.attackedBy[_C][NIHT]
                | ei.attackedBy[_C][BSHP] | ei.attackedBy[_C][ROOK]);
                if (b)
                    attackUnits +=  QueenContactCheck
                    * pop_count<Max15>(b)
                    * (_C == pos.active () ? 2 : 1);
            }

            // Analyse enemy's safe rook contact checks. First find undefended
            // squares around the king attacked by enemy rooks...
            b = undefended & ei.attackedBy[_C][ROOK] & ~pos.pieces(_C);

            // Consider only squares where the enemy rook gives check
            b &= PseudoAttacks[ROOK][ksq];

            if (b)
            {
                // ...then remove squares not supported by another enemy piece
                b &= (  ei.attackedBy[_C][PAWN]   | ei.attackedBy[_C][NIHT]
                | ei.attackedBy[_C][BSHP] | ei.attackedBy[_C][QUEEN]);
                if (b)
                    attackUnits +=  RookContactCheck
                    * pop_count<Max15>(b)
                    * (_C == pos.active () ? 2 : 1);
            }

            // Analyse enemy's safe distance checks for sliders and knights
            safe = ~(pos.pieces(_C) | ei.attackedBy[C][ALL_PIECES]);

            b1 = pos.attacks_from<ROOK>(ksq) & safe;
            b2 = pos.attacks_from<BSHP>(ksq) & safe;

            // Enemy queen safe checks
            b = (b1 | b2) & ei.attackedBy[_C][QUEEN];
            if (b)
                attackUnits += QueenCheck * pop_count<Max15>(b);

            // Enemy rooks safe checks
            b = b1 & ei.attackedBy[_C][ROOK];
            if (b)
                attackUnits += RookCheck * pop_count<Max15>(b);

            // Enemy bishops safe checks
            b = b2 & ei.attackedBy[_C][BSHP];
            if (b)
                attackUnits += BishopCheck * pop_count<Max15>(b);

            // Enemy knights safe checks
            b = pos.attacks_from<NIHT>(ksq) & ei.attackedBy[_C][NIHT] & safe;
            if (b)
                attackUnits += KnightCheck * pop_count<Max15>(b);

            // To index KingDanger[] attackUnits must be in [0, 99] range
            attackUnits = std::min(99, std::max(0, attackUnits));

            // Finally, extract the king danger score from the KingDanger[]
            // array and subtract the score from evaluation. Set also margins[]
            // value that will be used for pruning because this value can sometimes
            // be very big, and so capturing a single attacking piece can therefore
            // result in a score change far bigger than the value of the captured piece.
            score -= KingDanger[C == Search::RootColor][attackUnits];
            margins[C] += mg_value(KingDanger[C == Search::RootColor][attackUnits]);
        }

        if (Trace)
            Tracing::scores[C][KING] = score;

        return score;
    }


    // evaluate_passed_pawns<>() evaluates the passed pawns of the given color

    template<Color C, bool Trace>
    Score evaluate_passed_pawns(const Position &pos, const EvalInfo& ei)
    {

        const Color _C = ~C;

        Bitboard b, squaresToQueen, defendedSquares, unsafeSquares, supportingPawns;
        Score score = SCORE_ZERO;

        b = ei.pi->passed_pawns(C);

        while (b)
        {
            Square s = pop_lsb(&b);

            assert(pos.pawn_passed(C, s));

            int32_t r = int32_t(rel_rank(C, s) - R_2);
            int32_t rr = r * (r - 1);

            // Base bonus based on rank
            Value mbonus = Value(17 * rr);
            Value ebonus = Value(7 * (rr + r + 1));

            if (rr)
            {
                Square blockSq = s + pawn_push(C);

                // Adjust bonus based on kings proximity
                ebonus +=  Value(square_distance(pos.king_square(_C), blockSq) * 5 * rr)
                    - Value(square_distance(pos.king_square(C  ), blockSq) * 2 * rr);

                // If blockSq is not the queening square then consider also a second push
                if (rel_rank(C, blockSq) != RANK_8)
                    ebonus -= Value(square_distance(pos.king_square(C), blockSq + pawn_push(C)) * rr);

                // If the pawn is free to advance, increase bonus
                if (pos.empty(blockSq))
                {
                    squaresToQueen = forward_bb(C, s);

                    // If there is an enemy rook or queen attacking the pawn from behind,
                    // add all X-ray attacks by the rook or queen. Otherwise consider only
                    // the squares in the pawn's path attacked or occupied by the enemy.
                    if (    unlikely(forward_bb(_C, s) & pos.pieces(_C, ROOK, QUEEN))
                        && (forward_bb(_C, s) & pos.pieces(_C, ROOK, QUEEN) & pos.attacks_from<ROOK>(s)))
                        unsafeSquares = squaresToQueen;
                    else
                        unsafeSquares = squaresToQueen & (ei.attackedBy[_C][ALL_PIECES] | pos.pieces(_C));

                    if (    unlikely(forward_bb(_C, s) & pos.pieces(C, ROOK, QUEEN))
                        && (forward_bb(_C, s) & pos.pieces(C, ROOK, QUEEN) & pos.attacks_from<ROOK>(s)))
                        defendedSquares = squaresToQueen;
                    else
                        defendedSquares = squaresToQueen & ei.attackedBy[C][ALL_PIECES];

                    // If there aren't enemy attacks huge bonus, a bit smaller if at
                    // least block square is not attacked, otherwise smallest bonus.
                    int32_t k = !unsafeSquares ? 15 : !(unsafeSquares & blockSq) ? 9 : 3;

                    // Big bonus if the path to queen is fully defended, a bit less
                    // if at least block square is defended.
                    if (defendedSquares == squaresToQueen)
                        k += 6;

                    else if (defendedSquares & blockSq)
                        k += (unsafeSquares & defendedSquares) == unsafeSquares ? 4 : 2;

                    mbonus += Value(k * rr), ebonus += Value(k * rr);
                }
            } // rr != 0

            // Increase the bonus if the passed pawn is supported by a friendly pawn
            // on the same rank and a bit smaller if it's on the previous rank.
            supportingPawns = pos.pieces(C, PAWN) & adjacent_files_bb(_file (s));
            if (supportingPawns & mask_rank (s))
            {
                ebonus += Value(r * 20);
            }
            else if (supportingPawns & mask_rank (s - pawn_push(C)))
            {
                ebonus += Value(r * 12);
            }
            // Rook pawns are a special case: They are sometimes worse, and
            // sometimes better than other passed pawns. It is difficult to find
            // good rules for determining whether they are good or bad. For now,
            // we try the following: Increase the value for rook pawns if the
            // other side has no pieces apart from a knight, and decrease the
            // value if the other side has a rook or queen.
            if (_file (s) == FILE_A || _file (s) == FILE_H)
            {
                if (pos.non_pawn_material(_C) <= KnightValueMg)
                    ebonus += ebonus / 4;

                else if (pos.pieces(_C, ROOK, QUEEN))
                    ebonus -= ebonus / 4;
            }

            // Increase the bonus if we have more non-pawn pieces
            if (pos.piece_count(  C) - pos.piece_count<PAWN>(  C) >
                pos.piece_count(_C) - pos.piece_count<PAWN>(_C))
            {
                ebonus += ebonus / 4;
            }

            score += make_score(mbonus, ebonus);

        }

        if (Trace)
        {
            Tracing::scores[C][PASSED] = apply_weight(score, Weights[PassedPawns]);
        }

        // Add the scores to the middle game and endgame eval
        return apply_weight(score, Weights[PassedPawns]);
    }


    // evaluate_unstoppable_pawns() scores the most advanced among the passed and
    // candidate pawns. In case opponent has no pieces but pawns, this is somewhat
    // related to the possibility pawns are unstoppable.
    Score evaluate_unstoppable_pawns(const Position &pos, Color us, const EvalInfo& ei)
    {
        Bitboard b = 0; //ei.pi->passed_pawns(us) | ei.pi->candidate_pawns(us);

        if (!b || pos.non_pawn_material(~us))
        {
            return SCORE_ZERO;
        }
        return Unstoppable * int32_t (rel_rank(us, frontmost_rel_sq(us, b)));
    }


    // evaluate_space() computes the space evaluation for a given side. The
    // space evaluation is a simple bonus based on the number of safe squares
    // available for minor pieces on the central four files on ranks 2--4. Safe
    // squares one, two or three squares behind a friendly pawn are counted
    // twice. Finally, the space bonus is scaled by a weight taken from the
    // material hash table. The aim is to improve play on game opening.
    template<Color C>
    int32_t evaluate_space(const Position &pos, const EvalInfo& ei)
    {
        const Color _C = ~C;

        // Find the safe squares for our pieces inside the area defined by
        // SpaceMask[]. A square is unsafe if it is attacked by an enemy
        // pawn, or if it is undefended and attacked by an enemy piece.
        Bitboard safe =   SpaceMask[C]
        & ~pos.pieces(C, PAWN)
            & ~ei.attackedBy[_C][PAWN]
        & (ei.attackedBy[C][ALL_PIECES] | ~ei.attackedBy[_C][ALL_PIECES]);

        // Find all squares which are at most three squares behind some friendly pawn
        Bitboard behind = pos.pieces(C, PAWN);
        behind |= (C == WHITE ? behind >>  8 : behind <<  8);
        behind |= (C == WHITE ? behind >> 16 : behind << 16);

        // Since SpaceMask[C] is fully on our half of the board
        assert(unsigned(safe >> (C == WHITE ? 32 : 0)) == 0);

        // Count safe + (behind & safe) with a single pop_count
        return pop_count<FULL>((C == WHITE ? safe << 32 : safe >> 32) | (behind & safe));
    }


    // interpolate() interpolates between a middle game and an endgame score,
    // based on game phase. It also scales the return value by a ScaleFactor array.

    Value interpolate(const Score& v, Phase ph, ScaleFactor sf) {

        assert(mg_value(v) > -VALUE_INFINITE && mg_value(v) < VALUE_INFINITE);
        assert(eg_value(v) > -VALUE_INFINITE && eg_value(v) < VALUE_INFINITE);
        assert(ph >= PHASE_ENDGAME && ph <= PHASE_MIDGAME);

        int32_t e = (eg_value(v) * int32_t(sf)) / SCALE_FACTOR_NORMAL;
        int32_t r = (mg_value(v) * int32_t(ph) + e * int32_t(PHASE_MIDGAME - ph)) / PHASE_MIDGAME;
        return Value((r / GrainSize) * GrainSize); // Sign independent
    }

    // apply_weight() weights score v by score w trying to prevent overflow
    Score apply_weight(Score v, Score w) {
        return make_score((int32_t(mg_value(v)) * mg_value(w)) / 0x100,
            (int32_t(eg_value(v)) * eg_value(w)) / 0x100);
    }

    // weight_option() computes the value of an evaluation weight, by combining
    // two UCI-configurable weights (midgame and endgame) with an internal weight.

    Score weight_option(const std::string& mgOpt, const std::string& egOpt, Score internalWeight)
    {

        // Scale option value from 100 to 256
        //uint32_t mg = uint32_t (*(Options[mgOpt])) * 256 / 100;
        //uint32_t eg = uint32_t (*(Options[egOpt])) * 256 / 100;

        //return apply_weight(make_score(mg, eg), internalWeight);
        return SCORE_ZERO;
    }


    // Tracing functions definitions

    double to_cp(Value v) { return double(v) / double(VALUE_MG_PAWN); }

    void Tracing::add(int32_t idx, Score w_score, Score b_score)
    {
        scores[WHITE][idx] = w_score;
        scores[BLACK][idx] = b_score;
    }

    void Tracing::row(const char* name, int32_t idx)
    {
        Score w_score = scores[WHITE][idx];
        Score b_score = scores[BLACK][idx];
        switch (idx)
        {
        case PST: case IMBALANCE: case PAWN: case TOTAL:
            stream << std::setw(20) << name << " |   ---   --- |   ---   --- | "
                << std::setw(6)  << to_cp(mg_value(w_score)) << " "
                << std::setw(6)  << to_cp(eg_value(w_score)) << " \n";
            break;

        default:
            stream << std::setw(20) << name << " | " << std::noshowpos
                << std::setw(5)  << to_cp(mg_value(w_score)) << " "
                << std::setw(5)  << to_cp(eg_value(w_score)) << " | "
                << std::setw(5)  << to_cp(mg_value(b_score)) << " "
                << std::setw(5)  << to_cp(eg_value(b_score)) << " | "
                << std::showpos
                << std::setw(6)  << to_cp(mg_value(w_score - b_score)) << " "
                << std::setw(6)  << to_cp(eg_value(w_score - b_score)) << " \n";
        }
    }

    std::string Tracing::do_trace(const Position &pos) {

        stream.str("");
        stream << std::showpoint << std::showpos << std::fixed << std::setprecision(2);
        std::memset(scores, 0, 2 * (TOTAL + 1) * sizeof(Score));

        Value margin;
        do_evaluate<true>(pos, margin);

        std::string totals = stream.str();
        stream.str("");

        stream << std::setw(21) << "Eval term " << "|    White    |    Black    |     Total     \n"
            <<             "                     |   MG    EG  |   MG    EG  |   MG     EG   \n"
            <<             "---------------------+-------------+-------------+---------------\n";

        row("Material, PST, Tempo", PST);
        row("Material imbalance", IMBALANCE);
        row("Pawns", PAWN);
        row("Knights", NIHT);
        row("Bishops", BSHP);
        row("Rooks", ROOK);
        row("Queens", QUEN);
        row("Mobility", MOBILITY);
        row("King safety", KING);
        row("Threats", THREAT);
        row("Passed pawns", PASSED);
        row("Space", SPACE);

        stream <<             "---------------------+-------------+-------------+---------------\n";
        row("Total", TOTAL);
        stream << totals;

        return stream.str();
    }

}

namespace Evaluator {

    /// evaluate() is the main evaluation function. It always computes two
    /// values, an endgame score and a middle game score, and interpolates
    /// between them based on the remaining material.

    Value evaluate(const Position& pos, Value& margin)
    {
        return do_evaluate<false>(pos, margin);
    }


    /// trace() is like evaluate() but instead of a value returns a string suitable
    /// to be print on stdout with the detailed descriptions and values of each
    /// evaluation term. Used mainly for debugging.
    std::string trace(const Position& pos)
    {
        return Tracing::do_trace(pos);
    }


    /// initialize() computes evaluation weights from the corresponding UCI parameters
    /// and setup king tables.

    void initialize()
    {

        Weights[Mobility]       = weight_option("Mobility (Midgame)", "Mobility (Endgame)", WeightsInternal[Mobility]);
        Weights[PawnStructure]  = weight_option("Pawn Structure (Midgame)", "Pawn Structure (Endgame)", WeightsInternal[PawnStructure]);
        Weights[PassedPawns]    = weight_option("Passed Pawns (Midgame)", "Passed Pawns (Endgame)", WeightsInternal[PassedPawns]);
        Weights[Space]          = weight_option("Space", "Space", WeightsInternal[Space]);
        Weights[KingDangerUs]   = weight_option("Cowardice", "Cowardice", WeightsInternal[KingDangerUs]);
        Weights[KingDangerThem] = weight_option("Aggressiveness", "Aggressiveness", WeightsInternal[KingDangerThem]);

        const int32_t MaxSlope = 30;
        const int32_t Peak = 1280;

        for (int32_t t = 0, i = 1; i < 100; ++i)
        {
            t = std::min(Peak, std::min(int32_t(0.4 * i * i), t + MaxSlope));

            KingDanger[1][i] = apply_weight(make_score(t, 0), Weights[KingDangerUs]);
            KingDanger[0][i] = apply_weight(make_score(t, 0), Weights[KingDangerThem]);
        }
    }

} // namespace Eval


/*
static const int16_t _PieceSquareTable    [PT_NO][SQ_NO] =
{
//// PAWN
//0,    0,    0,    0,    0,    0,    0,    0,
//0,    0,    0,    0,    0,    0,    0,    0,
//0,    0,    0,    0,    0,    0,    0,    0,
//0,    0,    0,   75,   75,    0,    0,    0,
//0,    0,   15,   75,   75,   15,    0,    0,
//0,    0,    0,   40,   40,    0,    0,    0,
//0,    0,    0, -100, -100,    0,    0,    0,
//0,    0,    0,    0,    0,    0,    0,    0,

//// NIHT
//-150, -120, -120, -120, -120, -120, -120, -150,
//-120,   25,   25,   25,   25,   25,   25, -120,
//-120,   25,   50,   50,   50,   50,   25, -120,
//-120,   25,   50,  100,  100,   50,   25, -120,
//-120,   25,   50,  100,  100,   50,   25, -120,
//-120,   25,   50,   50,   50,   50,   25, -120,
//-120,   25,   25,   25,   25,   25,   25, -120,
//-150, -120, -120, -120, -120, -120, -120, -150,

//// BSHP
//-40,  -40,  -40,  -40,  -40,  -40,  -40,  -40,
//-40,   20,   20,   20,   20,   20,   20,  -40,
//-40,   20,   30,   30,   30,   30,   20,  -40,
//-40,   20,   30,   45,   45,   30,   20,  -40,
//-40,   20,   30,   45,   45,   30,   20,  -40,
//-40,   20,   30,   30,   30,   30,   20,  -40,
//-40,   20,   20,   20,   20,   20,   20,  -40,
//-40,  -40,  -40,  -40,  -40,  -40,  -40,  -40,

//// ROOK
//0,    0,   10,   15,   15,   10,    0,    0,
//0,    0,   10,   15,   15,   10,    0,    0,
//0,    0,   10,   15,   15,   10,    0,    0,
//0,    0,   10,   15,   15,   10,    0,    0,
//0,    0,   10,   15,   15,   10,    0,    0,
//0,    0,   10,   15,   15,   10,    0,    0,
//0,    0,   10,   15,   15,   10,    0,    0,
//0,    0,   10,   15,   15,   10,    0,    0,

//// QUEEN
//0,    0,    0,    0,    0,    0,    0,    0,
//0,    0,    0,    0,    0,    0,    0,    0,
//0,    0,   75,   75,   75,   75,    0,    0,
//0,    0,   75,  100,  100,   75,    0,    0,
//0,    0,   75,  100,  100,   75,    0,    0,
//0,    0,   75,   75,   75,   75,    0,    0,
//0,    0,    0,    0,    0,    0,    0,    0,
//0,    0,    0,    0,    0,    0,    0,    0,

//// KING
//-900, -900, -900, -900, -900, -900, -900, -900,
//-900, -900, -900, -900, -900, -900, -900, -900,
//-900, -900, -900, -900, -900, -900, -900, -900,
//-900, -900, -900, -900, -900, -900, -900, -900,
//-900, -900, -900, -900, -900, -900, -900, -900,
//-700, -700, -700, -700, -700, -700, -700, -700,
//-200, -200, -500, -500, -500, -500, -200, -200,
//+200, +300, +300, -300, -300, +100, +400, +200,

// ---

// PAWN
+ 0,    0,    0,    0,    0,    0,    0,    0,
+20,   26,   26,   28,   28,   26,   26,   20,
+12,   14,   16,   21,   21,   16,   14,   12,
+ 8,   10,   12,   18,   18,   12,   10,    8,
+ 4,    6,    8,   16,   16,    8,    6,    4,
+ 2,    2,    4,    6,    6,    4,    2,    2,
+ 0,    0,    0,   -4,   -4,    0,    0,    0,
+ 0,    0,    0,    0,    0,    0,    0,    0,

// NIHT
-40,  -10,  - 5,  - 5,  - 5,  - 5,  -10,  -40,
- 5,    5,    5,    5,    5,    5,    5,  - 5,
- 5,    5,   10,   15,   15,   10,    5,  - 5,
- 5,    5,   10,   15,   15,   10,    5,  - 5,
- 5,    5,   10,   15,   15,   10,    5,  - 5,
- 5,    5,    8,    8,    8,    8,    5,  - 5,
- 5,    0,    5,    5,    5,    5,    0,  - 5,
-50,  -20,  -10,  -10,  -10,  -10,  -20,  -50,

// BSHP
-40,  -20,  -15,  -15,  -15,  -15,  -20,  -40,
+ 0,    5,    5,    5,    5,    5,    5,    0,
+ 0,   10,   10,   18,   18,   10,   10,    0,
+ 0,   10,   10,   18,   18,   10,   10,    0,
+ 0,    5,   10,   18,   18,   10,    5,    0,
+ 0,    0,    5,    5,    5,    5,    0,    0,
+ 0,    5,    0,    0,    0,    0,    5,    0,
-50,  -20,  -10,  -20,  -20,  -10,  -20,  -50,

// ROOK
+10,   10,   10,   10,   10,   10,   10,   10,
+ 5,    5,    5,   10,   10,    5,    5,    5,
+ 0,    0,    5,   10,   10,    5,    0,    0,
+ 0,    0,    5,   10,   10,    5,    0,    0,
+ 0,    0,    5,   10,   10,    5,    0,    0,
+ 0,    0,    5,   10,   10,    5,    0,    0,
+ 0,    0,    5,   10,   10,    5,    0,    0,
+ 0,    0,    5,   10,   10,    5,    0,    0,

// QUEEN
+ 0,    0,    0,    0,    0,    0,    0,    0,
+ 0,    0,    0,    0,    0,    0,    0,    0,
+ 0,    0,   10,   10,   10,   10,    0,    0,
+ 0,    0,   10,   15,   15,   10,    0,    0,
+ 0,    0,   10,   15,   15,   10,    0,    0,
+ 0,    0,   10,   10,   10,   10,    0,    0,
+ 0,    0,    0,    0,    0,    0,    0,    0,
+ 0,    0,    0,    0,    0,    0,    0,    0,

// KING
+ 0,    0,    0,    0,    0,    0,    0,    0,
+ 0,    0,    0,    0,    0,    0,    0,    0,
+ 0,    0,    0,    0,    0,    0,    0,    0,
+ 0,    0,    0,    0,    0,    0,    0,    0,
+12,    8,    4,    0,    0,    4,    8,   12,
+16,   12,    8,    4,    4,    8,   12,   16,
+24,   20,   16,   12,   12,   16,   20,   24,
+24,   24,   24,   16,   16,    6,   32,   32,

};

Score pieceSquareTable(const PType t, const Square s)
{
return (Score) _PieceSquareTable[t][s];
}

static const uint16_t PieceWeight [PT_NO] =
{
100,    // PAWN
320,    // NIHT
325,    // BSHP
500,    // ROOK
975,    // QUEEN
16383,  // KING
};

static Value evaluate_material   (const Position &pos);
static Value evaluate_mobility   (const Position &pos);


namespace Evaluator {

Value evaluate (const Position &pos, Value &margin)
{
Value score = VALUE_DRAW;

score   = evaluate_material (pos);

if (VALUE_INFINITE == abs (int16_t (score))) return score;

score  += evaluate_mobility (pos);

//uint8_t pieces    [CLR_NO][PT_NO];
//uint8_t piecesDiff[PT_NO];
//uint8_t piecesSum [PT_NO];
//for (PType t = PAWN; t <= KING; ++t)
//{
//    for (Color c = WHITE; c <= BLACK; ++c)
//    {
//        pieces[c][t] = pos.piece_count (c, t);
//    }
//    piecesDiff[t] = pieces[WHITE][t] - pieces[BLACK][t];
//    piecesSum [t] = pieces[WHITE][t] + pieces[BLACK][t];
//}
//for (PType t = PAWN; t <= KING; ++t)
//{
//    //score += piecesDiff[t] / piecesSum [t];
//}

return score;
}

}

static Value evaluate_material   (const Position &pos)
{
const Color active = pos.active ();
const Color pasive =    ~active;

size_t kingDiff = pos.piece_count(active, KING) - pos.piece_count(pasive, KING);
if (kingDiff)
{
return (kingDiff > 0) ? VALUE_INFINITE : -VALUE_INFINITE;
}

size_t pieceValue   [CLR_NO]  = { 0, 0 };
for (PType t = PAWN; t <= KING; ++t)
{
pieceValue[active]   += PieceWeight[t] * pos.piece_count(active, t);
pieceValue[pasive]   += PieceWeight[t] * pos.piece_count(pasive, t);
}

return Value (pieceValue[active] - pieceValue[pasive]);
//return (VALUE_INFINITE * (double) (pieceValue[active] - pieceValue[pasive])) / (double) (pieceValue[active] + pieceValue[pasive]);
}

static Value evaluate_mobility   (const Position &pos)
{
const Color active = pos.active ();
const Color pasive =    ~active;

uint16_t mobilityValue    [CLR_NO]  = { 0, 0 };

Bitboard occ     = pos.pieces ();
Bitboard actives = pos.pieces (active);
Bitboard pasives = pos.pieces (pasive);

for (PType t = PAWN; t <= KING; ++t)
{
const Piece a_piece         = active | t;
const SquareList &a_orgs    = pos[a_piece];
for (SquareList::const_iterator itr_s = a_orgs.begin(); itr_s != a_orgs.end(); ++itr_s)
{
const Square s  = *itr_s;
const Bitboard moves = 0;//PieceMoves(a_piece, s, occ) & ~actives;
mobilityValue[active] += (PieceWeight[t]) * pop_count<FULL> (moves);
}

const Piece p_piece         = pasive | t;
const SquareList &p_orgs    = pos[p_piece];
for (SquareList::const_iterator itr_s = p_orgs.begin(); itr_s != p_orgs.end(); ++itr_s)
{
const Square s  = *itr_s;
const Bitboard moves = 0;//PieceMoves(p_piece, s, occ) & ~pasives;
mobilityValue[pasive] += (PieceWeight[t]) * pop_count<FULL> (moves);
}
}

return Value ((mobilityValue[active] - mobilityValue[pasive]) / 10);

//return (VALUE_INFINITE * (mobilityValue[active] - mobilityValue[pasive])) / (mobilityValue[active] + mobilityValue[pasive]) / 10;

}
*/
