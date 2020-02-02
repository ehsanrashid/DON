#include "Evaluator.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

#include "BitBoard.h"
#include "Material.h"
#include "Notation.h"
#include "Option.h"
#include "Pawns.h"
#include "Thread.h"

using namespace std;
using namespace BitBoard;

namespace {

    enum Term : u08
    {
        // The first 6 entries are for PieceType
        MATERIAL = NONE,
        IMBALANCE,
        MOBILITY,
        THREAT,
        PASSER,
        SPACE,
        INITIATIVE,
        TOTAL,
    };

    array<array<Score, CLR_NO>, TOTAL + 1> Scores;

    void clear()
    {
        for (auto &s : Scores) { s.fill(SCORE_ZERO); }
    }

    void write(Term term, Color c, Score score)
    {
        Scores[term][c] = score;
    }
    void write(Term term, Score wscore, Score bscore = SCORE_ZERO)
    {
        write(term, WHITE, wscore);
        write(term, BLACK, bscore);
    }

    ostream& operator<<(ostream &os, Term term)
    {
        const auto &score = Scores[term];
        switch (term)
        {
        case Term::MATERIAL:
        case Term::IMBALANCE:
        case Term::INITIATIVE:
        case Term::TOTAL:
            os << " | ----- -----"
               << " | ----- -----";
            break;
        default:
            os << " | " << score[WHITE]
               << " | " << score[BLACK];
            break;
        }
        os << " | " << score[WHITE] - score[BLACK] << endl;
        return os;
    }


#define S(mg, eg) make_score(mg, eg)

    constexpr array<array<Score, 28>, 4> Mobility
    {{
        { // Knight
            S(-62,-81), S(-53,-56), S(-12,-30), S( -4,-14), S(  3,  8), S( 13, 15),
            S( 22, 23), S( 28, 27), S( 33, 33)
        },
        { // Bishop
            S(-48,-59), S(-20,-23), S( 16, -3), S( 26, 13), S( 38, 24), S( 51, 42),
            S( 55, 54), S( 63, 57), S( 63, 65), S( 68, 73), S( 81, 78), S( 81, 86),
            S( 91, 88), S( 98, 97)
        },
        { // Rook
            S(-58,-76), S(-27,-18), S(-15, 28), S(-10, 55), S( -5, 69), S( -2, 82),
            S(  9,112), S( 16,118), S( 30,132), S( 29,142), S( 32,155), S( 38,165),
            S( 46,166), S( 48,169), S( 58,171)
        },
        { // Queen
            S(-39,-36), S(-21,-15), S(  3,  8), S(  3, 18), S( 14, 34), S( 22, 54),
            S( 28, 61), S( 41, 73), S( 43, 79), S( 48, 92), S( 56, 94), S( 60,104),
            S( 60,113), S( 66,120), S( 67,123), S( 70,126), S( 71,133), S( 73,136),
            S( 79,140), S( 88,143), S( 88,148), S( 99,166), S(102,170), S(102,175),
            S(106,184), S(109,191), S(113,206), S(116,212)
        }
    }};

    constexpr array<Score, 2> RookOnFile
    {
        S(21, 4), S(47,25)
    };

    constexpr array<Score, NONE> MinorThreat
    {
        S( 6,32), S(59,41), S(79,56), S(90,119), S(79,161), S( 0, 0)
    };
    constexpr array<Score, NONE> MajorThreat
    {
        S( 3,44), S(38,71), S(38,61), S( 0, 38), S(51, 38), S( 0, 0)
    };

    constexpr array<Score, R_NO> PasserRank
    {
        S( 0, 0), S(10,28), S(17,33), S(15,41), S(62,72), S(168,177), S(276,260), S( 0, 0)
    };

    constexpr Score MinorBehindPawn =    S( 18,  3);
    constexpr Score MinorOutpost =       S( 30, 21);
    constexpr Score KnightReachablepost =S( 32, 10);
    constexpr Score MinorKingProtect =   S(  7,  8);
    constexpr Score BishopOnDiagonal =   S( 45,  0);
    constexpr Score BishopPawns =        S(  3,  7);
    constexpr Score BishopTrapped =      S( 50, 50);
    constexpr Score RookOnQueenFile =    S(  7,  6);
    constexpr Score RookTrapped =        S( 52, 10);
    constexpr Score QueenWeaken =        S( 49, 15);
    constexpr Score PawnLessFlank =      S( 17, 95);
    constexpr Score PasserFile =         S( 11,  8);
    constexpr Score KingFlankAttacks =   S(  8,  0);
    constexpr Score PieceRestricted =    S(  7,  7);
    constexpr Score PieceHanged =        S( 69, 36);
    constexpr Score PawnThreat =         S(173, 94);
    constexpr Score PawnPushThreat =     S( 48, 39);
    constexpr Score KingThreat =         S( 24, 89);
    constexpr Score KnightOnQueen =      S( 16, 12);
    constexpr Score SliderOnQueen =      S( 59, 18);

#undef S

    // Threshold for lazy and space evaluation
    constexpr Value LazyThreshold = Value(1400);
    constexpr Value SpaceThreshold = Value(12222);

    constexpr array<i32, NONE> SafeCheckWeight
    {
        0, 790, 635, 1080, 780, 0
    };

    constexpr array<i32, NONE> KingAttackerWeight
    {
        0, 81, 52, 44, 10, 0
    };

    // Evaluator class contains various evaluation functions.
    template<bool Trace>
    class Evaluator
    {
    private:

        const Position &pos;

        Pawns::Entry *pe;
        Material::Entry *me;

        // Contains all squares attacked by the color and piece type.
        array<Bitboard              , CLR_NO> ful_attacks;
        
        // Contains all squares attacked by the color and piece type with pinned removed.
        array<array<Bitboard, PT_NO>, CLR_NO> sgl_attacks;
        
        // Contains all squares attacked by more than one pieces of a color, possibly via x-ray or by one pawn and one piece.
        array<Bitboard              , CLR_NO> pawn_dbl_attacks;
        array<Bitboard              , CLR_NO> dbl_attacks;

        // Contains all squares from which queen can be attacked
        array<array<Bitboard, 3>    , CLR_NO> queen_attacked;


        array<Bitboard              , CLR_NO> mob_area;
        array<Score                 , CLR_NO> mobility;

        // The squares adjacent to the king plus some other very near squares, depending on king position.
        array<Bitboard              , CLR_NO> king_ring;
        // Number of pieces of the color, which attack a square in the king_ring of the enemy king.
        array<i32                   , CLR_NO> king_attackers_count;
        // Sum of the "weight" of the pieces of the color which attack a square in the king_ring of the enemy king.
        // The weights of the individual piece types are given by the KingAttackerWeight[piece-type]
        array<i32                   , CLR_NO> king_attackers_weight;
        // Number of attacks by the color to squares directly adjacent to the enemy king.
        // Pieces which attack more than one square are counted multiple times.
        // For instance, if there is a white knight on g5 and black's king is on g8, this white knight adds 2 to king_attacks_count[WHITE]
        array<i32                   , CLR_NO> king_attacks_count;

        template<Color> void init_attacks();
        template<Color> void init_mobility();
        template<Color, PieceType> Score pieces();
        template<Color> Score king() const;
        template<Color> Score threats() const;
        template<Color> Score passers() const;
        template<Color> Score space() const;

        Score initiative(Score) const;
        Scale scale(Value) const;

    public:
        Evaluator() = delete;
        Evaluator(const Evaluator&) = delete;
        Evaluator& operator=(const Evaluator&) = delete;

        Evaluator(const Position &p)
            : pos(p)
        {}

        Value value();
    };

    /// init_attacks() computes pawn and king attacks.
    template<bool Trace> template<Color Own>
    void Evaluator<Trace>::init_attacks()
    {
        Bitboard pawns = pos.pieces(Own, PAWN);
     
        sgl_attacks[Own].fill(0);
        sgl_attacks[Own][PAWN] =  pawn_sgl_attacks_bb(Own, pawns & ~pos.si->king_blockers[Own])
                               | (pawn_sgl_attacks_bb(Own, pawns &  pos.si->king_blockers[Own]) & PieceAttacks[BSHP][k_sq]);
        sgl_attacks[Own][KING] = PieceAttacks[KING][pos.square(Own|KING)];
        sgl_attacks[Own][NONE] = sgl_attacks[Own][PAWN]
                               | sgl_attacks[Own][KING];
        //
        ful_attacks[Own] = pawn_sgl_attacks_bb(Own, pawns)
                         | sgl_attacks[Own][KING];
        //
        pawn_dbl_attacks[Own] = pawn_dbl_attacks_bb(Own, pawns)
                              & sgl_attacks[Own][PAWN];
        dbl_attacks[Own] = pawn_dbl_attacks[Own]
                         | (  sgl_attacks[Own][PAWN]
                            & sgl_attacks[Own][KING]);
        //
        queen_attacked[Own].fill(0);
    }
    /// init_mobility() computes mobility and king-ring.
    template<bool Trace> template<Color Own>
    void Evaluator<Trace>::init_mobility()
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

        // Mobility area: Exclude followings
        mob_area[Own] = ~(  // Squares protected by enemy pawns
                            sgl_attacks[Opp][PAWN]
                            // Squares occupied by friend Queen and King
                          | pos.pieces(Own, QUEN, KING)
                            // Squares occupied by friend King blockers
                          | pos.si->king_blockers[Own]
                            // Squares occupied by block pawns (pawns on ranks 2-3/blocked)
                          | (  pos.pieces(Own, PAWN)
                             & (  LowRanks_bb[Own]
                                | pawn_sgl_pushes_bb(Opp, pos.pieces()))));
        mobility[Own] = SCORE_ZERO;

        auto k_sq = pos.square(Own|KING);
        // King safety tables
        auto sq = clamp(_file(k_sq), F_B, F_G)
                | clamp(_rank(k_sq), R_2, R_7);
        king_ring[Own] = PieceAttacks[KING][sq] | sq;

        king_attackers_count [Opp] = pop_count(  king_ring[Own]
                                               & sgl_attacks[Opp][PAWN]);
        king_attackers_weight[Opp] = 0;
        king_attacks_count   [Opp] = 0;

        // Remove from king_ring the squares defended by two pawns
        king_ring[Own] &= ~pawn_dbl_attacks[Own];
    }

    /// pieces() evaluates the pieces of the color and type.
    template<bool Trace> template<Color Own, PieceType PT>
    Score Evaluator<Trace>::pieces()
    {
        static_assert (NIHT <= PT && PT <= QUEN, "PT incorrect");

        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

        Score score = SCORE_ZERO;

        for (auto s : pos.squares[Own|PT])
        {
            assert((Own|PT) == pos[s]);
            // Find attacked squares, including x-ray attacks for Bishops, Rooks and Queens
            Bitboard attacks = pos.xattacks_from<PT>(s, Own);

            ful_attacks[Own] |= attacks;

            if (contains(pos.si->king_blockers[Own], s))
            {
                attacks &= line_bb(pos.square(Own|KING), s);
            }

            switch (PT)
            {
            case BSHP:
            {
                Bitboard att =  attacks
                             &  pos.pieces(Own)
                             & ~pos.si->king_blockers[Own];
                dbl_attacks[Own] |= sgl_attacks[Own][NONE]
                                  & (  attacks
                                     | (  pawn_sgl_attacks_bb(Own, att & pos.pieces(PAWN) & front_rank_bb(Own, s))
                                        & PieceAttacks[BSHP][s]));
            }
                break;
            case QUEN:
            {
                Bitboard att =  attacks
                             &  pos.pieces(Own)
                             & ~pos.si->king_blockers[Own];
                dbl_attacks[Own] |= sgl_attacks[Own][NONE]
                                  & (  attacks
                                     | (  pawn_sgl_attacks_bb(Own, att & pos.pieces(PAWN) & front_rank_bb(Own, s))
                                        & PieceAttacks[BSHP][s])
                                     | attacks_bb<BSHP>(s, pos.pieces() ^ (att & pos.pieces(BSHP) & PieceAttacks[BSHP][s]))
                                     | attacks_bb<ROOK>(s, pos.pieces() ^ (att & pos.pieces(ROOK) & PieceAttacks[ROOK][s])));
            }
                break;
            default:
                dbl_attacks[Own] |= sgl_attacks[Own][NONE]
                                  & attacks;
                break;
            }

            sgl_attacks[Own][PT]   |= attacks;
            sgl_attacks[Own][NONE] |= attacks;

            if ((  attacks
                 & king_ring[Opp]) != 0)
            {
                king_attackers_count [Own]++;
                king_attackers_weight[Own] += KingAttackerWeight[PT];
                king_attacks_count   [Own] += pop_count(  attacks
                                                        & sgl_attacks[Opp][KING]);
            }

            auto mob = pop_count(attacks & mob_area[Own]);
            assert(0 <= mob && mob <= 27);

            // Bonus for piece mobility
            mobility[Own] += Mobility[PT - NIHT][mob];

            Bitboard b;
            // Special extra evaluation for pieces
            switch (PT)
            {
            case NIHT:
            case BSHP:
            {
                // Bonus for minor behind a pawn
                score += MinorBehindPawn * contains(pawn_sgl_pushes_bb(Opp, pos.pieces(PAWN)), s);

                // Penalty for distance from the friend king
                score -= MinorKingProtect * dist(s, pos.square(Own|KING));

                b = Outposts_bb[Own]
                  & ~pe->attack_span[Opp]
                  & sgl_attacks[Own][PAWN];

                if (NIHT == PT)
                {
                    // Bonus for knight outpost squares
                    if (contains(b, s))
                    {
                        score += MinorOutpost * 2;
                    }
                    else
                    if (0 != (b & attacks & ~pos.pieces(Own)))
                    {
                        score += KnightReachablepost;
                    }
                }
                else
                if (BSHP == PT)
                {
                    // Bonus for bishop outpost squares
                    if (contains(b, s))
                    {
                        score += MinorOutpost * 1;
                    }

                    // Penalty for pawns on the same color square as the bishop,
                    // more when the center files are blocked with pawns.
                    b = pos.pieces(Own, PAWN)
                      & Side_bb[CS_NO]
                      & pawn_sgl_pushes_bb(Opp, pos.pieces());
                    score -= BishopPawns
                           * (1 + pop_count(b))
                           * pop_count(pos.color_pawns(Own, color(s)));

                    // Bonus for bishop on a long diagonal which can "see" both center squares
                    score += BishopOnDiagonal * more_than_one(attacks_bb<BSHP>(s, pos.pieces(PAWN)) & Center_bb);

                    if (bool(Options["UCI_Chess960"]))
                    {
                        // An important Chess960 pattern: A cornered bishop blocked by a friend pawn diagonally in front of it.
                        // It is a very serious problem, especially when that pawn is also blocked.
                        // Bishop (white or black) on a1/h1 or a8/h8 which is trapped by own pawn on b2/g2 or b7/g7.
                        if (   1 >= mob
                            && contains(FA_bb|FH_bb, s)
                            && R_1 == rel_rank(Own, s))
                        {
                            auto del = pawn_push(Own) + (DEL_W + DEL_EE * i32(F_A == _file(s)));
                            if (contains(pos.pieces(Own, PAWN), s + del))
                            {
                                score -= BishopTrapped
                                       * (!contains(pos.pieces(), s + del + pawn_push(Own)) ?
                                              !contains(pos.pieces(Own, PAWN), s + del + del) ?
                                                  1 : 2 : 4);
                            }
                        }
                    }
                }
            }
                break;
            case ROOK:
            {
                // Bonus for rook on the same file as a queen
                if (file_bb(s) & pos.pieces(QUEN))
                {
                    score += RookOnQueenFile;
                }

                // Bonus for rook when on an open or semi-open file
                if (pos.semiopenfile_on(Own, s))
                {
                    score += RookOnFile[pos.semiopenfile_on(Opp, s)];
                }
                else
                // Penalty for rook when trapped by the king, even more if the king can't castle
                if (   3 >= mob
                    && R_5 > rel_rank(Own, s))
                {
                    auto kf = _file(pos.square(Own|KING));
                    if ((kf < F_E) == (_file(s) < kf))
                    {
                        score -= RookTrapped * (1 + (CR_NONE == pos.si->castle_right(Own)));
                    }
                }
            }
                break;
            case QUEN:
            {
                queen_attacked[Own][0] |= pos.attacks_from(NIHT, s);
                queen_attacked[Own][1] |= pos.attacks_from(BSHP, s);
                queen_attacked[Own][2] |= pos.attacks_from(ROOK, s);

                // Penalty for pin or discover attack on the queen
                b = 0; // Queen attackers
                if ((  pos.slider_blockers(s, Opp, pos.pieces(Opp, QUEN), b, b)
                     & ~pos.si->king_blockers[Opp]
                     & ~(  pos.pieces(Opp, PAWN)
                         & file_bb(s)
                         & ~pawn_sgl_attacks_bb(Own, pos.pieces(Own)))) != 0)
                {
                    score -= QueenWeaken;
                }
            }
                break;
            default: assert(false);
                break;
            }
        }

        if (Trace)
        {
            write(Term (PT), Own, score);
        }

        return score;
    }

    /// king() evaluates the king of the color.
    template<bool Trace> template<Color Own>
    Score Evaluator<Trace>::king() const
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

        auto k_sq = pos.square(Own|KING);

        // Main king safety evaluation
        i32 king_danger = 0;

        // Attacked squares defended at most once by friend queen or king
        Bitboard weak_area =  sgl_attacks[Opp][NONE]
                           & ~dbl_attacks[Own]
                           & (  ~sgl_attacks[Own][NONE]
                              |  sgl_attacks[Own][QUEN]
                              |  sgl_attacks[Own][KING]);

        // Safe squares where enemy's safe checks are possible on next move
        Bitboard safe_area = ~pos.pieces(Opp)
                           & (  ~sgl_attacks[Own][NONE]
                              | (  weak_area
                                 & dbl_attacks[Opp]));

        Bitboard unsafe_check = 0;

        Bitboard bshp_pins = attacks_bb<BSHP>(k_sq, pos.pieces() ^ pos.pieces(Own, QUEN));
        Bitboard rook_pins = attacks_bb<ROOK>(k_sq, pos.pieces() ^ pos.pieces(Own, QUEN));

        // Enemy rooks checks
        Bitboard rook_safe_checks =  rook_pins
                                  &  sgl_attacks[Opp][ROOK]
                                  &  safe_area;
        if (0 != rook_safe_checks)
        {
            king_danger += SafeCheckWeight[ROOK];
        }
        else
        {
            unsafe_check |= rook_pins
                          & sgl_attacks[Opp][ROOK];
        }

        // Enemy queens checks
        Bitboard quen_safe_checks = (bshp_pins | rook_pins)
                                  &  sgl_attacks[Opp][QUEN]
                                  &  safe_area
                                  & ~sgl_attacks[Own][QUEN]
                                  & ~rook_safe_checks;
        if (0 != quen_safe_checks)
        {
            king_danger += SafeCheckWeight[QUEN];
        }

        // Enemy bishops checks
        Bitboard bshp_safe_checks =  bshp_pins
                                  &  sgl_attacks[Opp][BSHP]
                                  &  safe_area
                                  & ~quen_safe_checks;
        if (0!= bshp_safe_checks)
        {
            king_danger += SafeCheckWeight[BSHP];
        }
        else
        {
            unsafe_check |= bshp_pins
                          & sgl_attacks[Opp][BSHP];
        }

        // Enemy knights checks
        Bitboard niht_checks = PieceAttacks[NIHT][k_sq]
                             & sgl_attacks[Opp][NIHT];
        if (0 != (niht_checks & safe_area))
        {
            king_danger += SafeCheckWeight[NIHT];
        }
        else
        {
            unsafe_check |= niht_checks;
        }

        Bitboard b;

        b = KingFlank_bb[_file(k_sq)]
          & Camp_bb[Own]
          & sgl_attacks[Opp][NONE];
        // Friend king flank attack count
        i32 kf_attacks = pop_count(b)                    // Squares attacked by enemy in friend king flank
                       + pop_count(b & dbl_attacks[Opp]);// Squares attacked by enemy twice in friend king flank.
        // Friend king flank defense count
        b = KingFlank_bb[_file(k_sq)]
          & Camp_bb[Own]
          & sgl_attacks[Own][NONE];
        i32 kf_defense = pop_count(b);

        // King Safety:
        Score score = pe->evaluate_king_safety<Own>(pos, ful_attacks[Opp]);

        king_danger +=   1 * king_attackers_count[Opp]*king_attackers_weight[Opp]
                     +  69 * king_attacks_count[Opp]
                     + 185 * pop_count(king_ring[Own] & weak_area)
                     + 148 * pop_count(unsafe_check)
                     +  98 * pop_count(pos.si->king_blockers[Own])
                     +   3 * kf_attacks * kf_attacks / 8
                        // Enemy queen is gone
                     - 873 * (0 == pos.pieces(Opp, QUEN))
                        // Friend knight is near by to defend king
                     - 100 * (0 != (   sgl_attacks[Own][NIHT]
                                    & (sgl_attacks[Own][KING] | k_sq)))
                        // Mobility
                     -   1 * i32(mg_value(mobility[Own] - mobility[Opp]))
                     -   4 * kf_defense
                        // Pawn Safety quality
                     -   3 * i32(mg_value(score)) / 4
                     +  37;

        // Transform the king danger into a score
        if (100 < king_danger)
        {
            score -= make_score(king_danger*king_danger / 0x1000, king_danger / 0x10);
        }

        // Penalty for king on a pawn less flank
        score -= PawnLessFlank * (0 == (pos.pieces(PAWN) & KingFlank_bb[_file(k_sq)]));

        // King tropism: Penalty for slow motion attacks moving towards friend king zone
        score -= KingFlankAttacks * kf_attacks;

        if (Trace)
        {
            write(Term (KING), Own, score);
        }

        return score;
    }

    /// threats() evaluates the threats of the color.
    template<bool Trace> template<Color Own>
    Score Evaluator<Trace>::threats() const
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

        Score score = SCORE_ZERO;

        // Enemy non-pawns
        Bitboard nonpawns_enemies =  pos.pieces(Opp)
                                  & ~pos.pieces(PAWN);
        // Squares defended by the opponent,
        // - attack the square with a pawn
        // - attack the square twice and not defended twice.
        Bitboard defended_area = sgl_attacks[Opp][PAWN]
                               | (   dbl_attacks[Opp]
                                  & ~dbl_attacks[Own]);
        // Enemy not defended and under attacked by any friend piece
        Bitboard attacked_undefended_enemies =  pos.pieces(Opp)
                                             & ~defended_area
                                             &  sgl_attacks[Own][NONE];
        // Non-pawn enemies, defended by enemies
        Bitboard defended_nonpawns_enemies = nonpawns_enemies
                                           & defended_area;

        Bitboard b;

        if (0 != (  attacked_undefended_enemies
                  | defended_nonpawns_enemies))
        {
            // Bonus according to the type of attacking pieces

            // Enemies attacked by minors
            b = (  attacked_undefended_enemies
                 | defended_nonpawns_enemies)
              & (  sgl_attacks[Own][NIHT]
                 | sgl_attacks[Own][BSHP]);
            while (0 != b)
            {
                score += MinorThreat[ptype(pos[pop_lsq(b)])];
            }

            if (0 != attacked_undefended_enemies)
            {
                // Enemies attacked by majors
                b =  attacked_undefended_enemies
                  &  sgl_attacks[Own][ROOK];
                while (0 != b)
                {
                    score += MajorThreat[ptype(pos[pop_lsq(b)])];
                }

                // Enemies attacked by king
                b =  attacked_undefended_enemies
                  &  sgl_attacks[Own][KING];
                if (0 != b)
                {
                    score += KingThreat;
                }

                // Enemies attacked are hanging
                b = attacked_undefended_enemies
                  & (  ~sgl_attacks[Opp][NONE]
                     | (  nonpawns_enemies
                        & dbl_attacks[Own]));
                score += PieceHanged * pop_count(b);
            }
        }

        // Bonus for restricting their piece moves
        b = ~defended_area
          &  sgl_attacks[Opp][NONE]
          &  sgl_attacks[Own][NONE];
        score += PieceRestricted * pop_count(b);

        Bitboard safe_area;

        // Defended or Unattacked squares
        safe_area =  sgl_attacks[Own][NONE]
                  | ~sgl_attacks[Opp][NONE];
        // Safe friend pawns
        b =  safe_area
          &  pos.pieces(Own, PAWN);
        // Safe friend pawns attacks on non-pawn enemies
        b =  nonpawns_enemies
          &  pawn_sgl_attacks_bb(Own, b)
          &  sgl_attacks[Own][PAWN];
        score += PawnThreat * pop_count(b);

        // Friend pawns who can push on the next move
        b =  pos.pieces(Own, PAWN)
          & ~pos.si->king_blockers[Own];
        // Friend pawns push (squares where friend pawns can push on the next move)
        b =  pawn_sgl_pushes_bb(Own, b)
          & ~pos.pieces();
        b |= pawn_sgl_pushes_bb(Own, b & rank_bb(rel_rank(Own, R_3)))
          & ~pos.pieces();
        // Friend pawns push safe (only the squares which are relatively safe)
        b &= safe_area
          & ~sgl_attacks[Opp][PAWN];
        // Friend pawns push safe attacks an enemies
        b =  nonpawns_enemies
          &  pawn_sgl_attacks_bb(Own, b);
        score += PawnPushThreat * pop_count(b);

        // Bonus for threats on the next moves against enemy queens
        if (0 != pos.pieces(Opp, QUEN))
        {
            safe_area =  mob_area[Own]
                      & ~defended_area;
            b = safe_area
              & (sgl_attacks[Own][NIHT] & queen_attacked[Opp][0]);
            score += KnightOnQueen * pop_count(b);

            b = safe_area
              & (  (sgl_attacks[Own][BSHP] & queen_attacked[Opp][1])
                 | (sgl_attacks[Own][ROOK] & queen_attacked[Opp][2]))
              & dbl_attacks[Own];
            score += SliderOnQueen * pop_count(b);
        }

        if (Trace)
        {
            write(Term::THREAT, Own, score);
        }

        return score;
    }

    /// passers() evaluates the passed pawns of the color.
    template<bool Trace> template<Color Own>
    Score Evaluator<Trace>::passers() const
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;

        auto king_proximity = [&](Color c, Square s) { return std::min(dist(pos.square(c|KING), s), 5); };

        Score score = SCORE_ZERO;

        Bitboard psr = pe->passers[Own];
        while (0 != psr)
        {
            auto s = pop_lsq(psr);
            assert((  pawn_sgl_pushes_bb(Own, front_squares_bb(Own, s))
                    & pos.pieces(Opp, PAWN)) == 0);

            i32 r = rel_rank(Own, s);
            // Base bonus depending on rank.
            Score bonus = PasserRank[r];

            auto push_sq = s + pawn_push(Own);

            if (R_3 < r)
            {
                i32 w = 5*r - 13;

                // Adjust bonus based on the king's proximity
                bonus += make_score(0, i32(+4.75*w*king_proximity(Opp, push_sq)
                                           -2.00*w*king_proximity(Own, push_sq)));
                // If block square is not the queening square then consider also a second push.
                if (R_7 != r)
                {
                    bonus += make_score(0, -1*w*king_proximity(Own, push_sq + pawn_push(Own)));
                }

                // If the pawn is free to advance.
                if (pos.empty(push_sq))
                {
                    Bitboard attacked_squares = pawn_pass_span(Own, s);

                    Bitboard behind_major = front_squares_bb(Opp, s)
                                          & pos.pieces(ROOK, QUEN);
                    if (0 == (pos.pieces(Opp) & behind_major))
                    {
                        attacked_squares &= sgl_attacks[Opp][NONE];
                    }

                    // Bonus according to attacked squares.
                    i32 k = 0 == attacked_squares                              ? 35 :
                            0 == (attacked_squares & front_squares_bb(Own, s)) ? 20 :
                            !contains(attacked_squares, push_sq)               ?  9 : 0;

                    // Bonus according to defended squares.
                    if (   0 != (pos.pieces(Own) & behind_major)
                        || contains(sgl_attacks[Own][NONE], push_sq))
                    {
                        k += 5;
                    }

                    bonus += make_score(k*w, k*w);
                }
            }

            // Scale down bonus for candidate passers
            // - have a pawn in front of it.
            // - need more than one pawn push to become passer.
            if (   contains(pos.pieces(PAWN), push_sq)
                || !pos.pawn_passed_at(Own, push_sq))
            {
                bonus /= 2;
            }

            score += bonus
                   - PasserFile * map_file(_file(s));
        }

        if (Trace)
        {
            write(Term::PASSER, Own, score);
        }

        return score;
    }

    /// space() evaluates the space of the color.
    /// The space evaluation is a simple bonus based on the number of safe squares
    /// available for minor pieces on the central four files on ranks 2-4
    /// Safe squares one, two or three squares behind a friend pawn are counted twice
    /// The aim is to improve play on opening
    template<bool Trace> template<Color Own>
    Score Evaluator<Trace>::space() const
    {
        constexpr auto Opp = WHITE == Own ? BLACK : WHITE;
        // Space Threshold
        if (pos.non_pawn_material() < SpaceThreshold)
        {
            return SCORE_ZERO;
        }

        // Find all squares which are at most three squares behind some friend pawn
        Bitboard behind = pos.pieces(Own, PAWN);
        behind |= pawn_sgl_pushes_bb(Opp, behind);
        behind |= pawn_dbl_pushes_bb(Opp, behind);

        // Safe squares for friend pieces inside the area defined by SpaceMask.
        Bitboard safe_space = Region_bb[Own]
                            & Side_bb[CS_NO]
                            & ~pos.pieces(Own, PAWN)
                            & ~sgl_attacks[Opp][PAWN];

        i32 bonus = pop_count(safe_space)
                  + pop_count(  behind
                              & safe_space
                              & ~sgl_attacks[Opp][NONE]);
        i32 weight = pos.count(Own) - 1;
        Score score = make_score(bonus * weight * weight / 16, 0);

        if (Trace)
        {
            write(Term::SPACE, Own, score);
        }

        return score;
    }

    /// initiative() evaluates the initiative correction value for the position
    /// i.e. second order bonus/malus based on the known attacking/defending status of the players
    template<bool Trace>
    Score Evaluator<Trace>::initiative(Score s) const
    {
        i32 outflanking = dist<File>(pos.square(WHITE|KING), pos.square(BLACK|KING))
                        - dist<Rank>(pos.square(WHITE|KING), pos.square(BLACK|KING));
        // Compute the initiative bonus for the attacking side
        i32 complexity = 11 * pos.count(PAWN)
                       +  9 * pe->passed_count()
                       +  9 * outflanking
                       + 51 * (VALUE_ZERO == pos.non_pawn_material())
                       - 95;

        // Pawn on both flanks
        if (   0 != (pos.pieces(PAWN) & Side_bb[CS_KING])
            && 0 != (pos.pieces(PAWN) & Side_bb[CS_QUEN]))
        {
            complexity += 21;
        }
        else
        // Almost Unwinnable
        if (   0 > outflanking
            && 0 == pe->passed_count())
        {
            complexity -= 43;
        }

        auto mg = mg_value(s);
        auto eg = eg_value(s);
        // Now apply the bonus: note that we find the attacking side by extracting the
        // sign of the midgame or endgame values, and that we carefully cap the bonus
        // so that the midgame and endgame scores do not change sign after the bonus.
        Score score = make_score(sign(mg) * ::clamp(complexity + 50, -abs(mg), 0),
                                 sign(eg) * std::max(complexity    , -abs(eg)));

        if (Trace)
        {
            write(Term::INITIATIVE, score);
        }

        return score;
    }

    /// scale() evaluates the scale for the position
    template<bool Trace>
    Scale Evaluator<Trace>::scale(Value eg) const
    {
        auto color = eg >= VALUE_ZERO ? WHITE : BLACK;

        auto scl = nullptr != me->scale_func[color] ?
                    (*me->scale_func[color])(pos) :
                    SCALE_NONE;
        if (SCALE_NONE == scl)
        {
            scl = me->scale[color];
        }
        assert(SCALE_NONE != scl);

        // If don't already have an unusual scale, check for certain types of endgames.
        if (SCALE_NORMAL == scl)
        {
            bool bishop_oppose = 1 == pos.count(WHITE|BSHP)
                              && 1 == pos.count(BLACK|BSHP)
                              && opposite_colors(pos.square(WHITE|BSHP), pos.square(BLACK|BSHP));
            scl = bishop_oppose
               && pos.non_pawn_material() == 2 * VALUE_MG_BSHP ?
                    // Endings with opposite-colored bishops and no other pieces is almost a draw
                    Scale(22) :
                    std::min(Scale(36 + (7 - 5 * (bishop_oppose)) * pos.count(color|PAWN)), SCALE_NORMAL);

            // Scale down endgame factor when shuffling
            scl = std::max(Scale(scl - std::max(pos.si->clock_ply / 4 - 3, 0)), SCALE_DRAW);
        }
        return scl;
    }

    /// value() computes the various parts of the evaluation and
    /// returns the value of the position from the point of view of the side to move.
    template<bool Trace>
    Value Evaluator<Trace>::value()
    {
        assert(0 == pos.si->checkers);

        // Probe the material hash table
        me = Material::probe(pos);
        // If have a specialized evaluation function for the material configuration
        if (nullptr != me->value_func)
        {
            return (*me->value_func)(pos);
        }

        // Probe the pawn hash table
        pe = Pawns::probe(pos);

        // Score is computed internally from the white point of view, initialize by
        // - incrementally updated scores (material + piece square tables).
        // - material imbalance.
        // - pawn score
        Score score = pos.psq
                    + me->imbalance
                    + (pe->scores[WHITE] - pe->scores[BLACK])
                    + pos.thread->contempt;

        // Lazy Threshold
        // Early exit if score is high
        Value v = (mg_value(score) + eg_value(score)) / 2;
        if (abs(v) > LazyThreshold + pos.non_pawn_material() / 64)
        {
            return WHITE == pos.active ? +v : -v;
        }

        if (Trace)
        {
            clear();
        }

        init_attacks <WHITE>(), init_attacks <BLACK>();
        init_mobility<WHITE>(), init_mobility<BLACK>();

        // Pieces should be evaluated first (populate attack information)
        score += pieces<WHITE, NIHT>() - pieces<BLACK, NIHT>();
        score += pieces<WHITE, BSHP>() - pieces<BLACK, BSHP>();
        score += pieces<WHITE, ROOK>() - pieces<BLACK, ROOK>();
        score += pieces<WHITE, QUEN>() - pieces<BLACK, QUEN>();

        assert((sgl_attacks[WHITE][NONE] & dbl_attacks[WHITE]) == dbl_attacks[WHITE]);
        assert((sgl_attacks[BLACK][NONE] & dbl_attacks[BLACK]) == dbl_attacks[BLACK]);

        score += mobility[WHITE]  - mobility[BLACK]
               + king   <WHITE>() - king   <BLACK>()
               + threats<WHITE>() - threats<BLACK>()
               + passers<WHITE>() - passers<BLACK>()
               + space  <WHITE>() - space  <BLACK>();

        score += initiative(score);

        assert(-VALUE_INFINITE < mg_value(score) && mg_value(score) < +VALUE_INFINITE);
        assert(-VALUE_INFINITE < eg_value(score) && eg_value(score) < +VALUE_INFINITE);
        assert(0 <= me->phase && me->phase <= Material::PhaseResolution);

        // Interpolates between midgame and scaled endgame values.
        v = mg_value(score) * (me->phase)
          + eg_value(score) * (Material::PhaseResolution - me->phase) * scale(eg_value(score)) / SCALE_NORMAL;
        v /= Material::PhaseResolution;

        if (Trace)
        {
            // Write remaining evaluation terms
            write(Term (PAWN), pe->scores[WHITE], pe->scores[BLACK]);
            write(Term::MATERIAL, pos.psq);
            write(Term::IMBALANCE, me->imbalance);
            write(Term::MOBILITY, mobility[WHITE], mobility[BLACK]);
            write(Term::TOTAL, score);
        }

        // Active side's point of view
        return (WHITE == pos.active ? +v : -v) + Tempo;
    }
}

/// evaluate() returns a static evaluation of the position from the point of view of the side to move.
Value evaluate(const Position &pos)
{
    return Evaluator<false>(pos).value();
}

/// trace() returns a string(suitable for outputting to stdout) that contains
/// the detailed descriptions and values of each evaluation term.
string trace(const Position &pos)
{
    if (0 != pos.si->checkers)
    {
        return "Total evaluation: none (in check)";
    }

    pos.thread->contempt = SCORE_ZERO; // Reset any dynamic contempt
    auto value = Evaluator<true>(pos).value();
    // Trace scores are from White's point of view
    value = WHITE == pos.active ? +value : -value;

    ostringstream oss;

    oss << setprecision(2) << fixed
        << "      Eval Term |    White    |    Black    |    Total     \n"
        << "                |   MG    EG  |   MG    EG  |   MG    EG   \n"
        << "----------------+-------------+-------------+--------------\n"
        << "       Material" << Term::MATERIAL
        << "      Imbalance" << Term::IMBALANCE
        << "           Pawn" << Term (PAWN)
        << "         Knight" << Term (NIHT)
        << "         Bishop" << Term (BSHP)
        << "           Rook" << Term (ROOK)
        << "          Queen" << Term (QUEN)
        << "       Mobility" << Term::MOBILITY
        << "           King" << Term (KING)
        << "         Threat" << Term::THREAT
        << "         Passer" << Term::PASSER
        << "          Space" << Term::SPACE
        << "     Initiative" << Term::INITIATIVE
        << "----------------+-------------+-------------+--------------\n"
        << "          Total" << Term::TOTAL
        << endl
        << showpos << showpoint
        << "Evaluation: " << value_to_cp(value) / 100.0 << " (white side)\n"
        << noshowpoint << noshowpos;

    return oss.str();
}
