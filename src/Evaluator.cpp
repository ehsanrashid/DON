#include "Evaluator.h"

#include <iomanip>
#include <sstream>

#include "Pawns.h"
#include "Material.h"
#include "MoveGenerator.h"
#include "Thread.h"

namespace Evaluator {

    using namespace std;
    using namespace BitBoard;
    using namespace MoveGen;

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
            // pin_attacked_by[Color][NONE] contains all squares attacked by the given color with pinned removed.
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
            // The weights of the individual piece types are given by the KING_ATTACK[PieceT]
            u32 king_ring_attackers_weight[CLR_NO];

            // king_zone_attacks_count[Color] is the number of attacks by
            // the given color to squares directly adjacent to the enemy king.
            // Pieces which attack more than one square are counted multiple times.
            // e.g, if Black's King is on g8 and there's a White knight on g5,
            // this knight adds 2 to king_zone_attacks_count[WHITE].
            u08 king_zone_attacks_count[CLR_NO];

        };

        template<bool Trace>
        // evaluate<>()
        Value evaluate (const Position &pos);

        namespace Tracer {

            // Used for tracing
            enum TermT : u08
            {
                MATERIAL = 6, IMBALANCE, MOBILITY, THREAT, PASSER, SPACE, TOTAL, TERM_NO
            };

            Score Scores[CLR_NO][TERM_NO];

            void write (TermT term, Color c, Score score)
            {
                Scores[c][term] = score;
            }
            void write (TermT term, Score wscore, Score bscore = SCORE_ZERO)
            {
                Scores[WHITE][term] = wscore;
                Scores[BLACK][term] = bscore;
            }

            ostream& operator<< (ostream &os, TermT term)
            {
                double cp[CLR_NO][2] =
                {
                    { value_to_cp (mg_value (Scores[WHITE][term])), value_to_cp (eg_value (Scores[WHITE][term])) },
                    { value_to_cp (mg_value (Scores[BLACK][term])), value_to_cp (eg_value (Scores[BLACK][term])) }
                };

                os << showpos;

                if (term == MATERIAL || term == IMBALANCE || term == TermT(PAWN) || term == TOTAL)
                {
                    os << " | ----- ----- | ----- ----- | ";
                }
                else
                {
                    os << " | " << setw(5) << cp[WHITE][MG] << " " << setw(5) << cp[WHITE][EG]
                       << " | " << setw(5) << cp[BLACK][MG] << " " << setw(5) << cp[BLACK][EG] << " | ";
                }
                os << setw(5) << cp[WHITE][MG] - cp[BLACK][MG] << " "
                   << setw(5) << cp[WHITE][EG] - cp[BLACK][EG] << " \n";

                return os;

            }

            string trace (const Position &pos)
            {
                for (auto c = WHITE; c <= BLACK; ++c)
                {
                    fill (begin (Scores[c]), end (Scores[c]), SCORE_ZERO);
                }

                auto value = evaluate<true> (pos);
                value = WHITE == pos.active () ? +value : -value; // White's point of view

                stringstream ss;

                ss  << showpoint << showpos << setprecision (2) << fixed
                    << "         Entity |    White    |    Black    |     Total    \n"
                    << "                |   MG    EG  |   MG    EG  |   MG    EG   \n"
                    << "----------------+-------------+-------------+--------------\n"
                    << "       Material" << TermT(MATERIAL)
                    << "      Imbalance" << TermT(IMBALANCE)
                    << "          Pawns" << TermT(PAWN)
                    << "        Knights" << TermT(NIHT)
                    << "         Bishop" << TermT(BSHP)
                    << "          Rooks" << TermT(ROOK)
                    << "         Queens" << TermT(QUEN)
                    << "       Mobility" << TermT(MOBILITY)
                    << "    King safety" << TermT(KING)
                    << "        Threats" << TermT(THREAT)
                    << "   Passed pawns" << TermT(PASSER)
                    << "          Space" << TermT(SPACE)
                    << "----------------+-------------+-------------+--------------\n"
                    << "          Total" << TermT(TOTAL);
                ss  << "\n"
                    << "Evaluation: " << value_to_cp (value) << " (white side)\n";

                return ss.str ();
            }

        }

        enum { PIECE_MOBILITY, PAWN_STRUCTURE, PASSED_PAWN, SPACE_ACTIVITY, KING_SAFETY };

        struct Weight { i32 mg, eg; };
        
        Score operator* (Score score, const Weight &weight)
        {
            return mk_score (
                mg_value (score) * weight.mg / 0x100,
                eg_value (score) * weight.eg / 0x100);
        }

        // Evaluation weights, indexed by the corresponding evaluation term
        const Weight WEIGHTS[5]
        {
            {289,344}, // Piece Mobility
            {233,201}, // Pawn Structure
            {221,273}, // Passed Pawns
            { 46,  0}, // Space Activity
            {322,  0}  // King Safety
        };

    #define S(mg, eg) mk_score (mg, eg)

        // MOBILITY_BONUS[PieceT][Attacks] contains bonuses for mobility,
        const Score MOBILITY_BONUS[NONE][28] =
        {
            {},
            { // Knights
                S(-68,-49), S(-46,-33), S(-3,-12), S( 5, -4), S( 9, 11), S(15, 16),
                S( 23, 27), S( 33, 28), S(37, 29)
            },
            { // Bishops
                S(-49,-44), S(-23,-16), S(16,  1), S(29, 16), S(40, 25), S(51, 34),
                S( 55, 43), S( 61, 49), S(64, 51), S(68, 52), S(73, 55), S(75, 60),
                S( 80, 65), S( 86, 66)
            },
            { // Rooks
                S(-50,-57), S(-28,-22), S(-11, 7), S(-1, 29), S( 0, 39), S( 1, 46),
                S( 10, 66), S( 16, 79), S(22, 86), S(23,103), S(30,109), S(33,111),
                S( 37,115), S( 38,119), S(48,124)
            },
            { // Queens
                S(-43,-30), S(-27,-15), S( 1, -5), S( 2, -3), S(14, 10), S(18, 24),
                S( 20, 27), S( 33, 37), S(33, 38), S(34, 43), S(40, 46), S(43, 56),
                S( 46, 61), S( 52, 63), S(52, 63), S(57, 65), S(60, 70), S(61, 74),
                S( 67, 80), S( 76, 82), S(77, 88), S(82, 94), S(86, 95), S(90, 96),
                S( 94, 99), S( 96,100), S(99,111), S(99,112)
            },
            {}
        };

        // OUTPOST[supported by pawn]
        const Score KNIGHT_OUTPOST[2] = { S(28, 7), S(42,11) };
        const Score BISHOP_OUTPOST[2] = { S(12, 3), S(18, 5) };

        // THREATEN_BY_PAWN[PieceT] contains bonuses according to which piece type is attacked by pawn.
        const Score THREATEN_BY_PAWN[NONE] =
        {
            S(0, 0), S(107, 138), S(84, 122), S(114, 203), S(121, 217), S(0, 0)
        };
        

        enum { STRONG, WEAK };
        enum { MINOR, MAJOR };

        // THREATEN_BY_PIECE[defended/weak][minor/major attacking][attacked PieceType] contains
        // bonuses according to which piece type attacks which one.
        const Score THREATEN_BY_PIECE[2][2][NONE] =
        {
            {
                { S( 0, 0), S(19, 37), S(24, 37), S(44, 97), S(35,106), S(0, 0) },  // Strong - Minor
                { S( 0, 0), S( 9, 14), S( 9, 14), S( 7, 14), S(24, 48), S(0, 0) }   // Strong - Major
            },
            {
                { S( 0,32), S(33, 41), S(31, 50), S(41,100), S(35,104), S(0, 0) },  // Weak - Minor
                { S( 0,27), S(26, 57), S(26, 57), S(0 , 43), S(23, 51), S(0, 0) }   // Weak - Major
            }
        };
        
        const Score THREATEN_BY_KING[] =
        {
            S( 2, 58), S( 6,125)
        };

        const Score THREATEN_BY_HANG_PAWN   = S(40, 60);

        const Score BISHOP_PAWNS            = S( 8,12); // Penalty for bishop with more pawns on same color
        const Score BISHOP_TRAPPED          = S(50,50); // Penalty for bishop trapped with pawns (Chess960)

        const Score MINOR_BEHIND_PAWN       = S(16, 0); // Bonus for minor behind a pawn

        const Score ROOK_ON_OPENFILE        = S(43,21); // Bonus for rook on open file
        const Score ROOK_ON_SEMIOPENFILE    = S(19,10); // Bonus for rook on semi-open file
        const Score ROOK_ON_PAWNS           = S( 7,27); // Bonus for rook on pawns
        const Score ROOK_TRAPPED            = S(92, 0); // Penalty for rook trapped
        
        const Score PIECE_HANGED            = S(31,26); // Bonus for each enemy hanged piece       
        
        const Score PAWN_SAFEPUSH           = S( 5, 5);
        const Score PAWN_SAFEATTACK         = S(20,20);

    #undef S

    #define V(v) Value(v)
        // PAWN_PASSED[Phase][Rank] contains bonuses for passed pawns according to the rank of the pawn.
        const Value PAWN_PASSED[PHASE_NO][R_NO] =
        {
            { V(0), V( 1), V(34), V(90), V(214), V(328), V(0), V(0) },
            { V(7), V(14), V(37), V(63), V(134), V(189), V(0), V(0) }
        };
    #undef V

        // The SPACE_MASK[Color] contains the area of the board which is considered
        // by the space evaluation. In the middle game, each side is given a bonus
        // based on how many squares inside this area are safe and available for
        // friendly minor pieces.
        const Bitboard SPACE_MASK[CLR_NO] =
        {
            ((FC_bb|FD_bb|FE_bb|FF_bb) & (R2_bb|R3_bb|R4_bb)),
            ((FC_bb|FD_bb|FE_bb|FF_bb) & (R7_bb|R6_bb|R5_bb))
        };

        // King danger constants and variables. The king danger scores are taken
        // from the KING_DANGER[]. Various little "meta-bonuses" measuring
        // the strength of the enemy attack are added up into an integer, which
        // is used as an index to KING_DANGER[].
        const i32 MAX_ATTACK_UNITS = 400;
        // KING_DANGER[attack_units] contains the king danger weighted score
        // indexed by a calculated integer number.
        Score KING_DANGER[MAX_ATTACK_UNITS];

        // KING_ATTACK[PieceT] contains king attack weights by piece type
        const i32   KING_ATTACK[NONE] = { 1, 14, 10,  8,  2,  0 };

        // Bonuses for safe checks
        const i32    SAFE_CHECK[NONE] = { 0, 14,  6, 37, 50,  0 };

        // Bonuses for contact safe checks
        const i32 CONTACT_CHECK[NONE] = { 0,  0, 18, 71, 89,  0 };

        //  --- init evaluation info --->
        template<Color Own>
        // init_evaluation<>() initializes king bitboards for given color adding
        // pawn attacks. To be done at the beginning of the evaluation.
        void init_evaluation (const Position &pos, EvalInfo &ei)
        {
            const auto Opp  = WHITE == Own ? BLACK : WHITE;
            const auto Push = WHITE == Own ? DEL_N: DEL_S;

            auto fk_sq = pos.square<KING> (Own);
            auto ek_sq = pos.square<KING> (Opp);

            auto pinneds = ei.pinneds[Own] = pos.pinneds (Own);
            ei.ful_attacked_by[Own][NONE] |= ei.ful_attacked_by[Own][PAWN] = ei.pi->pawns_attacks[Own];
            
            auto pinned_pawns = pinneds & pos.pieces (Own, PAWN);
            if (pinned_pawns != U64(0))
            {
                auto free_pawns    = pos.pieces (Own, PAWN) & ~pinned_pawns;
                auto pawns_attacks = shift_bb<WHITE == Own ? DEL_NE : DEL_SW> (free_pawns)
                                   | shift_bb<WHITE == Own ? DEL_NW : DEL_SE> (free_pawns);
                while (pinned_pawns != U64(0))
                {
                    auto s = pop_lsq (pinned_pawns);
                    pawns_attacks |= PAWN_ATTACKS[Own][s] & RAYLINE_bb[fk_sq][s];
                }
                ei.pin_attacked_by[Own][NONE] |= ei.pin_attacked_by[Own][PAWN] = pawns_attacks;
            }
            else
            {
                ei.pin_attacked_by[Own][NONE] |= ei.pin_attacked_by[Own][PAWN] = ei.pi->pawns_attacks[Own];
            }

            auto king_attacks             = PIECE_ATTACKS[KING][ek_sq];
            ei.ful_attacked_by[Opp][KING] = king_attacks;
            ei.pin_attacked_by[Opp][KING] = king_attacks;
            
            ei.king_ring_attackers_count [Own] = 0;
            ei.king_ring_attackers_weight[Own] = 0;
            ei.king_zone_attacks_count   [Own] = 0;
            ei.king_ring                 [Opp] = U64(0);

            // Init king safety tables only if going to use them
            // Do not evaluate king safety when you are close to the endgame so the weight of king safety is small
            //if (ei.mi->game_phase > PHASE_KINGSAFETY)
            if (pos.non_pawn_material (Own) >= VALUE_MG_QUEN)
            {
                auto ekr = rel_rank (Opp, ek_sq);
                ei.king_ring[Opp] = king_attacks|(DIST_RINGS_bb[ek_sq][1] &
                                                        (ekr < R_5 ? (PAWN_PASS_SPAN[Opp][ek_sq]) :
                                                         ekr < R_7 ? (PAWN_PASS_SPAN[Opp][ek_sq]|PAWN_PASS_SPAN[Own][ek_sq]) :
                                                                     (PAWN_PASS_SPAN[Own][ek_sq])));

                if ((king_attacks & ei.pin_attacked_by[Own][PAWN]) != U64(0))
                {
                    auto attackers = pos.pieces (Own, PAWN) & (king_attacks|(DIST_RINGS_bb[ek_sq][1] & (rank_bb (ek_sq-Push)|rank_bb (ek_sq))));
                    ei.king_ring_attackers_count [Own] = attackers != U64(0) ? u08(pop_count<MAX15> (attackers)) : 0;
                    ei.king_ring_attackers_weight[Own] = ei.king_ring_attackers_count [Own]*KING_ATTACK[PAWN];
                }
            }
        }

        template<Color Own, PieceT PT, bool Trace>
        // evaluate_pieces<>() assigns bonuses and penalties to the pieces of a given color except PAWN
        Score evaluate_pieces (const Position &pos, EvalInfo &ei, Bitboard mobility_area, Score &mobility)
        {
            const auto Opp  = WHITE == Own ? BLACK : WHITE;
            const auto Push = WHITE == Own ? DEL_N : DEL_S;

            auto score = SCORE_ZERO;

            ei.ful_attacked_by[Own][PT] = U64(0);
            ei.pin_attacked_by[Own][PT] = U64(0);
            
            const auto *pl = pos.squares<PT> (Own);
            Square s;
            while ((s = *pl++) != SQ_NO)
            {
                // Find attacked squares, including x-ray attacks for bishops and rooks
                auto attacks =
                    BSHP == PT ? attacks_bb<BSHP> (s, (pos.pieces () ^ pos.pieces (Own, QUEN, BSHP)) | ei.pinneds[Own]) :
                    ROOK == PT ? attacks_bb<ROOK> (s, (pos.pieces () ^ pos.pieces (Own, QUEN, ROOK)) | ei.pinneds[Own]) :
                    QUEN == PT ? attacks_bb<BSHP> (s, (pos.pieces () ^ pos.pieces (Own, QUEN)) | ei.pinneds[Own])
                               | attacks_bb<ROOK> (s, (pos.pieces () ^ pos.pieces (Own, QUEN)) | ei.pinneds[Own]) :
                    /*NIHT == PT*/PIECE_ATTACKS[PT][s];

                ei.ful_attacked_by[Own][NONE] |= ei.ful_attacked_by[Own][PT] |= attacks;
                
                if ((ei.pinneds[Own] & s) != U64(0))
                {
                    attacks &= RAYLINE_bb[pos.square<KING> (Own)][s];
                }
                ei.pin_attacked_by[Own][NONE] |= ei.pin_attacked_by[Own][PT] |= attacks;

                if ((ei.king_ring[Opp] & attacks) != U64(0))
                {
                    ei.king_ring_attackers_count [Own]++;
                    ei.king_ring_attackers_weight[Own] += KING_ATTACK[PT];
                    auto zone_attacks = ei.ful_attacked_by[Opp][KING] & attacks;
                    if (zone_attacks != U64(0)) ei.king_zone_attacks_count[Own] += u08(pop_count<MAX15> (zone_attacks));
                }

                /*
                if (ROOK == PT)
                {
                    attacks &= ~(  ei.pin_attacked_by[Opp][NIHT]
                                 | ei.pin_attacked_by[Opp][BSHP]
                                );
                             //|  (  ei.pin_attacked_by[Own][PAWN]
                             //    | ei.pin_attacked_by[Own][NIHT]
                             //    | ei.pin_attacked_by[Own][BSHP]
                             //   );
                }
                else
                */
                if (QUEN == PT)
                {
                    attacks &= ~(  ei.pin_attacked_by[Opp][NIHT]
                                 | ei.pin_attacked_by[Opp][BSHP]
                                 | ei.pin_attacked_by[Opp][ROOK]
                                );
                             //|  (  ei.pin_attacked_by[Own][NIHT]
                             //    | ei.pin_attacked_by[Own][BSHP]
                             //    | ei.pin_attacked_by[Own][ROOK]
                             //   );
                }

                i32 mob = pop_count<QUEN == PT ? FULL : MAX15> (attacks & mobility_area);
                mobility += MOBILITY_BONUS[PT][mob];

                // Special extra evaluation for pieces
                
                if (NIHT == PT || BSHP == PT)
                {
                    // Minors (bishop or knight) behind a pawn
                    if (   rel_rank (Own, s) < R_5
                        && (pos.pieces (PAWN) & (s+Push)) != U64(0)
                       )
                    {
                        score += MINOR_BEHIND_PAWN;
                    }

                    if (NIHT == PT)
                    {
                        // Outpost for knight
                        if (   rel_rank (Own, s) >= R_4
                            //&& rel_rank (Own, s) <= R_6
                            && (pos.pieces (Opp, PAWN) & PAWN_ATTACK_SPAN[Own][s]) == U64(0)
                           )
                        {
                            score += KNIGHT_OUTPOST[(ei.pin_attacked_by[Own][PAWN] & s) != U64(0)];
                        }
                    }
                    else
                    if (BSHP == PT)
                    {
                        score -= BISHOP_PAWNS * ei.pi->pawns_on_squarecolor (Own, s);

                        // Outpost for bishop
                        if (   rel_rank (Own, s) >= R_4
                            //&& rel_rank (Own, s) <= R_6
                            && (pos.pieces (Opp, PAWN) & PAWN_ATTACK_SPAN[Own][s]) == U64(0)
                           )
                        {
                            score += BISHOP_OUTPOST[(ei.pin_attacked_by[Own][PAWN] & s) != U64(0)];
                        }

                        if (s == rel_sq (Own, SQ_A8) || s == rel_sq (Own, SQ_H8))
                        {
                            auto del = (F_A == _file (s) ? DEL_E : DEL_W)-Push;
                            if (pos[s + del] == (Own|PAWN))
                            {
                                score -= BISHOP_TRAPPED;
                            }
                        }

                        // An important Chess960 pattern: A cornered bishop blocked by own pawn diagonally in front
                        // of it is a very serious problem, especially when that pawn is also blocked.
                        // Bishop on a1/h1 (a8/h8 for black) which is trapped by own pawn on b2/g2 (b7/g7 for black).
                        if (pos.chess960 ())
                        {
                            if (s == rel_sq (Own, SQ_A1) || s == rel_sq (Own, SQ_H1))
                            {
                                auto del = (F_A == _file (s) ? DEL_E : DEL_W)+Push;
                                if (pos[s+del] == (Own|PAWN))
                                {
                                    score -= BISHOP_TRAPPED *
                                            (  !pos.empty (s+del+Push) ? 4 :
                                                pos[s+del+del] == (Own|PAWN) ? 2 : 1);
                                }
                            }
                        }
                    }
                }
                else
                if (ROOK == PT)
                {
                    if (R_4 < rel_rank (Own, s))
                    {
                        // Rook piece attacking enemy pawns on the same rank/file
                        auto rook_on_pawns = pos.pieces (Opp, PAWN) & PIECE_ATTACKS[ROOK][s];
                        if (rook_on_pawns != U64(0)) score += ROOK_ON_PAWNS * pop_count<MAX15> (rook_on_pawns);
                    }

                    // Rook on a open or semi-open file
                    if (ei.pi->file_semiopen (Own, _file (s)))
                    {
                        score += ei.pi->file_semiopen (Opp, _file (s)) ? ROOK_ON_OPENFILE : ROOK_ON_SEMIOPENFILE;
                    }

                    if (mob <= 3 && !ei.pi->file_semiopen (Own, _file (s)))
                    {
                        auto kf = _file (pos.square<KING> (Own));
                        auto kr = rel_rank (Own, pos.square<KING> (Own));
                        // Rooks trapped by own king, more if the king has lost its castling capability.
                        if (   (kf < F_E) == (_file (s) < kf)
                            && (kr == R_1 || kr == rel_rank (Own, s))
                            && !ei.pi->side_semiopen (Own, kf, _file (s) < kf)
                           )
                        {
                            score -= (ROOK_TRAPPED - mk_score (22 * mob, 0)) * (1 + !pos.can_castle (Own));
                        }
                    }
                }
            }

            if (Trace)
            {
                Tracer::write (Tracer::TermT(PT), Own, score);
            }

            return score;
        }
        //  --- init evaluation info <---

        template<Color Own, bool Trace>
        // evaluate_king<>() assigns bonuses and penalties to a king of a given color
        Score evaluate_king (const Position &pos, const EvalInfo &ei)
        {
            const auto Opp = WHITE == Own ? BLACK : WHITE;

            auto fk_sq = pos.square<KING> (Own);

            // King Safety: friend pawn shelter and enemy pawns storm
            ei.pi->evaluate_king_safety<Own> (pos);

            auto value = VALUE_ZERO;

            // If can castle use the value after the castle if is bigger
            if (rel_rank (Own, fk_sq) == R_1 && pos.can_castle (Own))
            {
                value = ei.pi->king_safety[Own][CS_NO];

                if (    pos.can_castle (Castling<Own, CS_K>::Right)
                    && !pos.castle_impeded (Castling<Own, CS_K>::Right)
                    && (pos.king_path (Castling<Own, CS_K>::Right) & ei.ful_attacked_by[Opp][NONE]) == U64(0)
                   )
                {
                    value = max (value, ei.pi->king_safety[Own][CS_K]);
                }
                if (    pos.can_castle (Castling<Own, CS_Q>::Right)
                    && !pos.castle_impeded (Castling<Own, CS_Q>::Right)
                    && (pos.king_path (Castling<Own, CS_Q>::Right) & ei.ful_attacked_by[Opp][NONE]) == U64(0)
                   )
                {
                    value = max (value, ei.pi->king_safety[Own][CS_Q]);
                }
            }
            else
            if (rel_rank (Own, fk_sq) <= R_4)
            {
                value = ei.pi->king_safety[Own][CS_NO];
            }

            auto score = mk_score (value, -0x10 * ei.pi->king_pawn_dist[Own]);

            // Main king safety evaluation
            if (ei.king_ring_attackers_count[Opp] != 0)
            {
                // Attacked squares around the king which has no defenders
                // apart from the king itself
                auto undefended =
                      ei.ful_attacked_by[Own][KING] // King-zone
                    & ei.ful_attacked_by[Opp][NONE]
                    & ~(  ei.pin_attacked_by[Own][PAWN]
                        | ei.pin_attacked_by[Own][NIHT]
                        | ei.pin_attacked_by[Own][BSHP]
                        | ei.pin_attacked_by[Own][ROOK]
                        | ei.pin_attacked_by[Own][QUEN]
                       );

                // Initialize the 'attack_units' variable, which is used later on as an
                // index to the KING_DANGER[] array. The initial value is based on the
                // number and types of the enemy's attacking pieces, the number of
                // attacked and undefended squares around our king, and the quality of
                // the pawn shelter (current 'mg score' value).
                i32 attack_units =
                    + min ((ei.king_ring_attackers_count[Opp]*ei.king_ring_attackers_weight[Opp])/2, 74U)   // King-ring attacks
                    +  8 * (ei.king_zone_attacks_count[Opp])                                                // King-zone attacks
                    + 25 * (undefended != U64(0) ? pop_count<MAX15> (undefended) : 0)                       // King-zone undefended pieces
                    + 11 * (ei.pinneds[Own] != U64(0) ? pop_count<MAX15> (ei.pinneds[Own]) : 0)             // King pinned piece
                    - 60 * (pos.count<QUEN>(Opp) == 0)
                    - i32(value) / 8;

                auto occ = pos.pieces ();

                // Undefended squares around king not occupied by enemy's
                undefended &= ~pos.pieces (Opp);
                if (undefended != U64(0))
                {
                    Bitboard undefended_attacked;
                    if (pos.count<QUEN> (Opp) > 0)
                    {
                        // Analyze enemy's safe queen contact checks.
                        
                        // Undefended squares around the king attacked by enemy queen...
                        undefended_attacked = undefended & ei.pin_attacked_by[Opp][QUEN];
                        
                        auto unsafe = ei.ful_attacked_by[Opp][PAWN]
                                    | ei.ful_attacked_by[Opp][NIHT]
                                    | ei.ful_attacked_by[Opp][BSHP]
                                    | ei.ful_attacked_by[Opp][ROOK];

                        while (undefended_attacked != U64(0))
                        {
                            auto sq = pop_lsq (undefended_attacked);
                            if (   (unsafe & sq) != U64(0)
                                || (  pos.count<QUEN> (Opp) > 1
                                    && more_than_one (pos.pieces (Opp, QUEN) & (PIECE_ATTACKS[BSHP][sq]|PIECE_ATTACKS[ROOK][sq]))
                                    && more_than_one (pos.pieces (Opp, QUEN) & (attacks_bb<BSHP> (sq, occ ^ pos.pieces (Opp, QUEN))|attacks_bb<ROOK> (sq, occ ^ pos.pieces (Opp, QUEN))))
                                   )
                               )
                            {
                                attack_units += CONTACT_CHECK[QUEN];
                            }
                        }
                    }
                    if (pos.count<ROOK> (Opp) > 0)
                    {
                        // Analyze enemy's safe rook contact checks.
                        
                        // Undefended squares around the king attacked by enemy rooks...
                        undefended_attacked = undefended & ei.pin_attacked_by[Opp][ROOK];
                        // Consider only squares where the enemy rook gives check
                        undefended_attacked &= PIECE_ATTACKS[ROOK][fk_sq];
                        
                        auto unsafe = ei.ful_attacked_by[Opp][PAWN]
                                    | ei.ful_attacked_by[Opp][NIHT]
                                    | ei.ful_attacked_by[Opp][BSHP];

                        while (undefended_attacked != U64(0))
                        {
                            auto sq = pop_lsq (undefended_attacked);
                            if (   (unsafe & sq) != U64(0)
                                || (   pos.count<ROOK> (Opp) > 1
                                    && more_than_one (pos.pieces (Opp, ROOK) & PIECE_ATTACKS[ROOK][sq])
                                    && more_than_one (pos.pieces (Opp, ROOK) & attacks_bb<ROOK> (sq, occ ^ pos.pieces (Opp, ROOK)))
                                   )
                               )
                            {
                                attack_units += CONTACT_CHECK[ROOK];
                            }
                        }
                    }
                    if (pos.count<BSHP> (Opp) > 0)
                    {
                        // Analyze enemy's safe bishop contact checks.
                        
                        // Undefended squares around the king attacked by enemy bishop...
                        undefended_attacked = undefended & ei.pin_attacked_by[Opp][BSHP];
                        // Consider only squares where the enemy bishop gives check
                        undefended_attacked &= PIECE_ATTACKS[BSHP][fk_sq];
                        
                        auto unsafe = ei.ful_attacked_by[Opp][PAWN]
                                    | ei.ful_attacked_by[Opp][NIHT]
                                    | ei.ful_attacked_by[Opp][ROOK];

                        while (undefended_attacked != U64(0))
                        {
                            auto sq = pop_lsq (undefended_attacked);
                            auto bishops = U64(0);
                            if (   (unsafe & sq) != U64(0)
                                || (   pos.count<BSHP> (Opp) > 1
                                    && (bishops = pos.pieces (Opp, BSHP) & ((DARK_bb & sq) != U64(0) ? DARK_bb : LIHT_bb)) != U64(0)
                                    && more_than_one (bishops)
                                    && more_than_one (bishops & PIECE_ATTACKS[BSHP][sq])
                                    && more_than_one (bishops & attacks_bb<BSHP> (sq, occ ^ pos.pieces (Opp, BSHP)))
                                   )
                               )
                            {
                                attack_units += CONTACT_CHECK[BSHP];
                            }
                        }
                    }

                    // knight can't give contact check but safe distance check
                }

                // Analyse the enemies safe distance checks for sliders and knights
                auto safe_area = ~(pos.pieces (Opp) | ei.pin_attacked_by[Own][NONE]);
                auto rook_check = attacks_bb<ROOK> (fk_sq, occ) & safe_area;
                auto bshp_check = attacks_bb<BSHP> (fk_sq, occ) & safe_area;

                // Enemies safe-checks
                Bitboard safe_check;
                // Queens safe-checks
                safe_check = (rook_check | bshp_check) & ei.pin_attacked_by[Opp][QUEN];
                if (safe_check != U64(0)) attack_units += SAFE_CHECK[QUEN] * pop_count<MAX15> (safe_check);
                // Rooks safe-checks
                safe_check = rook_check & ei.pin_attacked_by[Opp][ROOK];
                if (safe_check != U64(0)) attack_units += SAFE_CHECK[ROOK] * pop_count<MAX15> (safe_check);
                // Bishops safe-checks
                safe_check = bshp_check & ei.pin_attacked_by[Opp][BSHP];
                if (safe_check != U64(0)) attack_units += SAFE_CHECK[BSHP] * pop_count<MAX15> (safe_check);
                // Knights safe-checks
                safe_check = PIECE_ATTACKS[NIHT][fk_sq] & safe_area & ei.pin_attacked_by[Opp][NIHT];
                if (safe_check != U64(0)) attack_units += SAFE_CHECK[NIHT] * pop_count<MAX15> (safe_check);

                // Finally, extract the king danger score from the KING_DANGER[] array
                // attack_units must be in [0, MAX_ATTACK_UNITS-1] range
                // and subtract the score from evaluation.
                score -= KING_DANGER[min (max (attack_units, 0), MAX_ATTACK_UNITS-1)];
            }

            if (Trace)
            {
                Tracer::write (Tracer::TermT(KING), Own, score);
            }

            return score;
        }

        template<Color Own, bool Trace>
        // evaluate_threats<>() assigns bonuses according to the type of attacking piece
        // and the type of attacked one.
        Score evaluate_threats (const Position &pos, const EvalInfo &ei)
        {
            const auto Opp  = WHITE == Own ? BLACK : WHITE;
            const auto Push = WHITE == Own ? DEL_N  : DEL_S;
            const auto LCap = WHITE == Own ? DEL_NW : DEL_SE;
            const auto RCap = WHITE == Own ? DEL_NE : DEL_SW;

            auto score = SCORE_ZERO;

            Bitboard b;

            auto opp_pieces = pos.pieces (Opp);

            // Non-pawn enemies attacked by a pawn
            auto pawn_threats =
                  (opp_pieces ^ pos.pieces (Opp, PAWN))
                &  ei.pin_attacked_by[Own][PAWN];
            
            if (pawn_threats != U64(0))
            {
                // Safe Pawns
                b = pos.pieces (Own, PAWN) & (ei.pin_attacked_by[Own][NONE] | ~ei.pin_attacked_by[Opp][NONE]);
                // Safe Pawn threats
                b = (shift_bb<RCap>(b) | shift_bb<LCap>(b)) & pawn_threats;
                if ((pawn_threats ^ b) != U64(0)) score += THREATEN_BY_HANG_PAWN;

                while (b != U64(0)) score += THREATEN_BY_PAWN[ptype (pos[pop_lsq (b)])];
            }
            
            // Non-pawn enemies defended by a pawn and attacked by any piece
            auto strong_pieces = 
                  (opp_pieces ^ pos.pieces (Opp, PAWN))
                &  ei.pin_attacked_by[Opp][PAWN]
                &  ei.pin_attacked_by[Own][NONE];
            
            if (strong_pieces != U64(0))
            {
                // Strong enemies attacked by minor pieces
                b = strong_pieces & (ei.pin_attacked_by[Own][NIHT] | ei.pin_attacked_by[Own][BSHP]);
                while (b != U64(0)) score += THREATEN_BY_PIECE[STRONG][MINOR][ptype (pos[pop_lsq (b)])];
                // Strong enemies attacked by rooks
                b = strong_pieces & (ei.pin_attacked_by[Own][ROOK]);
                while (b != U64(0)) score += THREATEN_BY_PIECE[STRONG][MAJOR][ptype (pos[pop_lsq (b)])];
                //// Strong enemies attacked by queens
                //b = strong_pieces & (ei.pin_attacked_by[Own][QUEN]);
                //while (b != U64(0)) score += THREATEN_BY_PIECE[STRONG][MAJOR][ptype (pos[pop_lsq (b)])]/2;
            }
            
            // Enemies not defended by pawn and attacked by any piece
            auto weak_pieces =
                   opp_pieces
                & ~ei.pin_attacked_by[Opp][PAWN]
                &  ei.pin_attacked_by[Own][NONE];

            // Add a bonus according to the kind of attacking pieces
            if (weak_pieces != U64(0))
            {
                // Weak enemies attacked by minor pieces
                b = weak_pieces & (ei.pin_attacked_by[Own][NIHT] | ei.pin_attacked_by[Own][BSHP]);
                while (b != U64(0)) score += THREATEN_BY_PIECE[WEAK][MINOR][ptype (pos[pop_lsq (b)])];
                // Weak enemies attacked by major pieces
                b = weak_pieces & (ei.pin_attacked_by[Own][ROOK] | ei.pin_attacked_by[Own][QUEN]);
                while (b != U64(0)) score += THREATEN_BY_PIECE[WEAK][MAJOR][ptype (pos[pop_lsq (b)])];

                b = weak_pieces & ei.ful_attacked_by[Own][KING];
                if (b != U64(0)) score += THREATEN_BY_KING[more_than_one (b) ? 1 : 0]; 
                
                // Hanged enemies
                b = weak_pieces & ~ei.pin_attacked_by[Opp][NONE];
                if (b != U64(0)) score += PIECE_HANGED * pop_count<MAX15> (b);
            }

            b = pos.pieces (Own, PAWN);// & ~(WHITE == Own ? R7_bb  : R2_bb);
            b = shift_bb<Push> (b | (shift_bb<Push> (b & (WHITE == Own ? R2_bb  : R7_bb)) & ~pos.pieces ()));
            // Safe pawn pushes
            b &= ~pos.pieces ()
              &  ~ei.pin_attacked_by[Opp][PAWN]
              &  (ei.pin_attacked_by[Own][NONE] | ~ei.pin_attacked_by[Opp][NONE]);
            if (b != U64(0)) score += PAWN_SAFEPUSH * pop_count<FULL> (b);
            
            // Safe pawn pushes attacks an enemy piece
            b =  (shift_bb<LCap> (b) | shift_bb<RCap> (b))
              &   pos.pieces (Opp)
              &  ~ei.pin_attacked_by[Own][PAWN];
            if (b != U64(0)) score += PAWN_SAFEATTACK * pop_count<MAX15> (b);

            if (Trace)
            {
                Tracer::write (Tracer::THREAT, Own, score);
            }

            return score;
        }

        template<Color Own, bool Trace>
        // evaluate_passed_pawns<>() evaluates the passed pawns of the given color
        Score evaluate_passed_pawns (const Position &pos, const EvalInfo &ei)
        {
            const auto Opp  = WHITE == Own ? BLACK : WHITE;
            const auto Push = WHITE == Own ? DEL_N : DEL_S;

            auto score = SCORE_ZERO;

            auto nonpawn_diff = pos.count<NONPAWN> (Own) - pos.count<NONPAWN> (Opp);

            auto passed_pawns = ei.pi->passed_pawns[Own];
            while (passed_pawns != U64(0))
            {
                auto s = pop_lsq (passed_pawns);
                assert (pos.passed_pawn (Own, s));
                
                i32  r = rel_rank (Own, s) - R_2;
                i32 rr = r * (r - 1);

                // Base bonus depends on rank
                auto mg_value = PAWN_PASSED[MG][r];
                auto eg_value = PAWN_PASSED[EG][r];

                if (rr != 0)
                {
                    auto block_sq = s+Push;

                    // Adjust bonus based on kings proximity
                    eg_value += 
                        + 5*rr*dist (pos.square<KING> (Opp), block_sq)
                        - 2*rr*dist (pos.square<KING> (Own), block_sq);
                    // If block square is not the queening square then consider also a second push
                    if (rel_rank (Own, block_sq) != R_8)
                    {
                        eg_value -= 1*rr*dist (pos.square<KING> (Own), block_sq+Push);
                    }

                    bool pinned = (ei.pinneds[Own] & s) != U64(0);
                    if (pinned)
                    {
                        // Only one real pinner exist other are fake pinner
                        auto pawn_pinners = pos.pieces (Opp) & RAYLINE_bb[pos.square<KING> (Own)][s] &
                            (  (attacks_bb<ROOK> (s, pos.pieces ()) & pos.pieces (ROOK, QUEN))
                             | (attacks_bb<BSHP> (s, pos.pieces ()) & pos.pieces (BSHP, QUEN))
                            );
                        pinned = (BETWEEN_bb[pos.square<KING> (Own)][scan_lsq (pawn_pinners)] & block_sq) == U64(0);
                    }

                    // If the pawn is free to advance
                    if (!pinned && pos.empty (block_sq))
                    {
                        // Squares to queen
                        auto front_squares = FRONT_SQRS_bb[Own][s];
                        auto behind_majors = FRONT_SQRS_bb[Opp][s] & pos.pieces (ROOK, QUEN) & attacks_bb<ROOK> (s, pos.pieces ());
                        auto unsafe_squares = front_squares
                            ,  safe_squares = front_squares;
                        // If there is an enemy rook or queen attacking the pawn from behind,
                        // add all X-ray attacks by the rook or queen. Otherwise consider only
                        // the squares in the pawn's path attacked or occupied by the enemy.
                        if ((behind_majors & pos.pieces (Opp)) == U64(0))
                        {
                            unsafe_squares &= ei.pin_attacked_by[Opp][NONE] | pos.pieces (Opp);
                        }

                        if ((behind_majors & pos.pieces (Own)) == U64(0))
                        {
                              safe_squares &= ei.pin_attacked_by[Own][NONE];
                        }

                        // Give a big bonus if there aren't enemy attacks, otherwise
                        // a smaller bonus if block square is not attacked.
                        i32 k = unsafe_squares != U64(0) ?
                                    (unsafe_squares & block_sq) != U64(0) ?
                                        0 : 9 : 15;

                        if (safe_squares != U64(0))
                        {
                            // Give a big bonus if the path to queen is fully defended,
                            // a smaller bonus if at least block square is defended.
                            k += safe_squares == front_squares ?
                                    6 : (safe_squares & block_sq) != U64(0) ?
                                        4 : 0;

                            // If the block square is defended by a pawn add more small bonus.
                            if ((ei.pin_attacked_by[Own][PAWN] & block_sq) != U64(0)) k += 1;
                        }

                        mg_value += k*rr;
                        eg_value += k*rr;
                    }
                    else
                    // If the pawn is blocked by own pieces
                    if ((pos.pieces (Own) & block_sq) != U64(0))
                    {
                        mg_value += 3*rr + 2*r + 3;
                        eg_value += 1*rr + 2*r + 0;
                    }
                }

                // If non-pawn pieces difference
                if (nonpawn_diff != 0)
                {
                    eg_value += nonpawn_diff * eg_value / 4;
                }

                score += mk_score (mg_value, eg_value);
            }
            
            score = score * WEIGHTS[PASSED_PAWN];

            if (Trace)
            {
                Tracer::write (Tracer::PASSER, Own, score);
            }

            return score;
        }

        template<Color Own, bool Trace>
        // evaluate_space_activity<>() computes the space evaluation for a given side. The
        // space evaluation is a simple bonus based on the number of safe squares
        // available for minor pieces on the central four files on ranks 2--4. Safe
        // squares one, two or three squares behind a friendly pawn are counted twice.
        // Finally, the space bonus is multiplied by a weight.
        // The aim is to improve play on game opening.
        Score evaluate_space_activity (const Position &pos, const EvalInfo &ei)
        {
            const auto Opp = WHITE == Own ? BLACK : WHITE;

            // Find the safe squares for our pieces inside the area defined by SPACE_MASK[].
            // A square is unsafe:
            // if it is attacked by an enemy pawn or
            // if it is undefended and attacked by an enemy piece.
            auto safe_space =
                  SPACE_MASK[Own]
                & ~pos.pieces (Own, PAWN)
                & ~ei.pin_attacked_by[Opp][PAWN]
                & (ei.pin_attacked_by[Own][NONE]|~ei.pin_attacked_by[Opp][NONE]);

            // Since SPACE_MASK[Own] is fully on our half of the board
            assert (u32(safe_space >> (WHITE == Own ? 32 : 0)) == 0);

            // Find all squares which are at most three squares behind some friendly pawn
            auto behind = pos.pieces (Own, PAWN);
            behind |= shift_bb<WHITE == Own ? DEL_S  : DEL_N > (behind);
            behind |= shift_bb<WHITE == Own ? DEL_SS : DEL_NN> (behind);

            // Count safe_space + (behind & safe_space) with a single pop_count
            i32 bonus = pop_count<FULL> ((WHITE == Own ? safe_space << 32 : safe_space >> 32) | (behind & safe_space));
            i32 weight = pos.count<NIHT> () + pos.count<BSHP> ();

            auto score = mk_score (bonus * weight * weight, 0) * WEIGHTS[SPACE_ACTIVITY];
            
            if (Trace)
            {
                Tracer::write (Tracer::SPACE, Own, score);
            }

            return score;
        }

        template<bool Trace>
        // evaluate<>()
        Value evaluate (const Position &pos)
        {
            assert (pos.checkers () == U64(0));

            EvalInfo ei;
            // Probe the material hash table
            ei.mi  = Material::probe (pos);

            // If have a specialized evaluation function for the current material configuration
            if (ei.mi->specialized_eval_exists ())
            {
                return ei.mi->evaluate (pos);
            }

            // Score is computed from the point of view of white.
            // Initialize score by reading the incrementally updated scores included
            // in the position object (material + piece square tables) and adding Tempo bonus. 
            auto score  = pos.psq_score ();
            
            score += ei.mi->imbalance;

            // Probe the pawn hash table
            ei.pi  = Pawns::probe (pos);
            score += ei.pi->pawn_score * WEIGHTS[PAWN_STRUCTURE];

            for (auto c = WHITE; c <= BLACK; ++c)
            {
                ei.ful_attacked_by[c][NONE] = U64(0);
                ei.pin_attacked_by[c][NONE] = U64(0);
            }

            // Initialize attack and king safety bitboards
            init_evaluation<WHITE> (pos, ei);
            init_evaluation<BLACK> (pos, ei);

            for (auto c = WHITE; c <= BLACK; ++c)
            {
                ei.ful_attacked_by[c][NONE] |= ei.ful_attacked_by[c][KING];
                ei.pin_attacked_by[c][NONE] |= ei.pin_attacked_by[c][KING];
            }

            // Evaluate pieces and mobility
            Score mobility[CLR_NO] = { SCORE_ZERO, SCORE_ZERO };
            // Pawns which can't move forward or on Rank 2-3
            const Bitboard fixed_pawns[CLR_NO] =
            {
                pos.pieces (WHITE, PAWN) & (shift_bb<DEL_S> (pos.pieces ()) | R2_bb | R3_bb),
                pos.pieces (BLACK, PAWN) & (shift_bb<DEL_N> (pos.pieces ()) | R7_bb | R6_bb)
            };
            // Do not include in mobility squares protected by enemy pawns or occupied by friend fixed pawns or king
            const Bitboard mobility_area[CLR_NO] =
            {
                ~(ei.pin_attacked_by[BLACK][PAWN] | fixed_pawns[WHITE] | pos.pieces (WHITE, KING)),
                ~(ei.pin_attacked_by[WHITE][PAWN] | fixed_pawns[BLACK] | pos.pieces (BLACK, KING))
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
            score += (mobility[WHITE] - mobility[BLACK]) * WEIGHTS[PIECE_MOBILITY];

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
            score += 
                + evaluate_passed_pawns<WHITE, Trace> (pos, ei)
                - evaluate_passed_pawns<BLACK, Trace> (pos, ei);

            const Value npm[CLR_NO] = 
            {
                pos.non_pawn_material (WHITE),
                pos.non_pawn_material (BLACK)
            };

            // Evaluate space for both sides, only during opening
            if (npm[WHITE] + npm[BLACK] >= VALUE_SPACE)
            {
                score += 
                    + evaluate_space_activity<WHITE, Trace> (pos, ei)
                    - evaluate_space_activity<BLACK, Trace> (pos, ei);
            }
            // If both sides have only pawns, score for potential unstoppable pawns
            if (npm[WHITE] == VALUE_ZERO && npm[BLACK] == VALUE_ZERO)
            {
                score +=
                    + ei.pi->evaluate_unstoppable_pawns<WHITE> ();
                    - ei.pi->evaluate_unstoppable_pawns<BLACK> ();
            }

            // In case of tracing add each evaluation contributions for both white and black
            if (Trace)
            {
                Tracer::write (Tracer::TermT(PAWN), ei.pi->pawn_score);
                Tracer::write (Tracer::MATERIAL   , pos.psq_score ());
                Tracer::write (Tracer::IMBALANCE  , ei.mi->imbalance);
                Tracer::write (Tracer::MOBILITY
                    , mobility[WHITE] * WEIGHTS[PIECE_MOBILITY]
                    , mobility[BLACK] * WEIGHTS[PIECE_MOBILITY]);

                Tracer::write (Tracer::TOTAL      , score);
            }

            // --------------------------------------------------

            auto mg = mg_value (score);
            auto eg = eg_value (score);
            assert (-VALUE_INFINITE < mg && mg < +VALUE_INFINITE);
            assert (-VALUE_INFINITE < eg && eg < +VALUE_INFINITE);

            auto strong_side = eg >= VALUE_DRAW ? WHITE : BLACK;
            // Scale winning side if position is more drawish than it appears
            auto scale_factor = ei.mi->scale_factor (pos, strong_side);

            auto game_phase = ei.mi->game_phase;
            assert (PHASE_ENDGAME <= game_phase && game_phase <= PHASE_MIDGAME);

            // If don't already have an unusual scale factor, check for opposite
            // colored bishop endgames, and use a lower scale for those.
            if (   game_phase < PHASE_MIDGAME
                && (scale_factor == SCALE_FACTOR_NORMAL || scale_factor == SCALE_FACTOR_ONEPAWN)
               )
            {
                if (pos.opposite_bishops ())
                {
                    // Endgame with opposite-colored bishops and no other pieces (ignoring pawns)
                    // is almost a draw, in case of KBP vs KB is even more a draw.
                    if (npm[WHITE] == VALUE_MG_BSHP && npm[BLACK] == VALUE_MG_BSHP)
                    {
                        i32 pawn_diff = abs (pos.count<PAWN> (WHITE) - pos.count<PAWN> (BLACK));
                        scale_factor = pawn_diff == 0 ? ScaleFactor(4) : ScaleFactor(8 * pawn_diff);
                    }
                    // Endgame with opposite-colored bishops, but also other pieces. Still
                    // a bit drawish, but not as drawish as with only the two bishops. 
                    else
                    {
                        scale_factor = ScaleFactor(i32(scale_factor) * i32(SCALE_FACTOR_BISHOPS)/i32(SCALE_FACTOR_NORMAL));
                    }
                }
                // Endings where weaker side can place his king in front of the strong side pawns are drawish.
                else
                if (    abs (eg) <= VALUE_EG_BSHP
                    &&  ei.pi->pawn_span[strong_side] <= 1
                    && !pos.passed_pawn (~strong_side, pos.square<KING> (~strong_side))
                   )
                {
                    scale_factor = ei.pi->pawn_span[strong_side] != 0 ? ScaleFactor(56) : ScaleFactor(38);
                }
            }

            // Interpolates between a middle game and a (scaled by 'scale_factor') endgame score, based on game phase.
            auto value = Value((mg*i32(game_phase) + eg*i32(PHASE_MIDGAME - game_phase)*i32(scale_factor)/i32(SCALE_FACTOR_NORMAL))/i32(PHASE_MIDGAME));

            return (WHITE == pos.active () ? +value : -value) + TEMPO;
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

    // initialize() init evaluation weights
    void initialize ()
    {
        i32 mg = 0;
        for (i32 i = 0; i < MAX_ATTACK_UNITS; ++i)
        {
            //                       MAX_SLOPE - PEAK_VALUE
            mg = min (min (i*i*27, mg + 8700), 1280000);
            KING_DANGER[i] = mk_score (mg/1000, 0) * WEIGHTS[KING_SAFETY];
        }
    }

}
