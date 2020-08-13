#include "Evaluator.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "BitBoard.h"
#include "Helper.h"
#include "King.h"
#include "Material.h"
#include "Pawns.h"
#include "Position.h"
#include "Notation.h"
#include "Thread.h"
#include "UCI.h"

namespace Evaluator {

    bool useNNUE = false;
    std::string prevEvalFile = "None";

    void initializeNNUE() {

        useNNUE = Options["Use NNUE"];
        auto evalFile{ std::string(Options["Eval File"]) };
        if (useNNUE
         && prevEvalFile != evalFile) {
            if (Evaluator::NNUE::loadEvalFile(evalFile)) {
                prevEvalFile = evalFile;
            }
        }
    }

    void verifyNNUE() {

        auto evalFile{ std::string(Options["Eval File"]) };
        if (useNNUE
         && prevEvalFile != evalFile) {
            std::cerr << "NNUE evaluation used, but the network file " << evalFile << " was not loaded successfully. "
                      << "These network evaluation parameters must be available, and compatible with this version of the code. "
                      << "The UCI option EvalFile might need to specify the full path, including the directory/folder name, to the file. "
                      << "The default net can be downloaded from: https://tests.stockfishchess.org/api/nn/" << Options["Eval File"].defaultValue() << std::endl;
            std::exit(EXIT_FAILURE);
        }

        if (useNNUE) {
            sync_cout << "info string NNUE evaluation using " << evalFile << " enabled." << sync_endl;
        }
        else {
            sync_cout << "info string classical evaluation enabled." << sync_endl;
        }
    }


    namespace {

        enum Term : u08 { MATERIAL = 8, IMBALANCE, MOBILITY, THREAT, PASSER, SPACE, SCALING, TOTAL, TERMS = 16 };

        class Tracer {

        private:
            static Score Scores[TERMS][COLORS];

        public:
            static void clear() {
                std::fill_n(*Scores, TERMS*COLORS, SCORE_ZERO);
            }

            static void write(Term t, Color c, Score s) {
                Scores[t][c] = s;
            }
            static void write(Term t, Score sW, Score sB = SCORE_ZERO) {
                write(t, WHITE, sW);
                write(t, BLACK, sB);
            }

            friend std::ostream& operator<<(std::ostream &, Term);


        };

        Score Tracer::Scores[TERMS][COLORS];

        std::ostream& operator<<(std::ostream &os, Term t) {
            if (t == MATERIAL
             || t == IMBALANCE
             || t == SCALING
             || t == TOTAL) {
                os << " | ----- -----" << " | ----- -----";
            }
            else {
                os << " | " << Tracer::Scores[t][WHITE] << " | " << Tracer::Scores[t][BLACK];
            }
            os << " | " << Tracer::Scores[t][WHITE] - Tracer::Scores[t][BLACK] << " |\n";
            return os;
        }

        constexpr Bitboard CenterBB{ (FileBB[FILE_D]|FileBB[FILE_E]) & (RankBB[RANK_4]|RankBB[RANK_5]) };

        constexpr Bitboard LowRankBB[COLORS]
        {
            RankBB[RANK_2]|RankBB[RANK_3],
            RankBB[RANK_7]|RankBB[RANK_6]
        };

        constexpr Bitboard CampBB[COLORS]
        {
            RankBB[RANK_1]|RankBB[RANK_2]|RankBB[RANK_3]|RankBB[RANK_4]|RankBB[RANK_5],
            RankBB[RANK_8]|RankBB[RANK_7]|RankBB[RANK_6]|RankBB[RANK_5]|RankBB[RANK_4]
        };

        constexpr Bitboard OutpostBB[COLORS]
        {
            RankBB[RANK_4]|RankBB[RANK_5]|RankBB[RANK_6],
            RankBB[RANK_5]|RankBB[RANK_4]|RankBB[RANK_3]
        };

        constexpr Bitboard KingFlankBB[FILES]
        {
            SlotFileBB[CS_QUEN] ^ FileBB[FILE_D],
            SlotFileBB[CS_QUEN],
            SlotFileBB[CS_QUEN],
            SlotFileBB[CS_CENTRE],
            SlotFileBB[CS_CENTRE],
            SlotFileBB[CS_KING],
            SlotFileBB[CS_KING],
            SlotFileBB[CS_KING] ^ FileBB[FILE_E]
        };


    #define S(mg, eg) makeScore(mg, eg)

        constexpr Score Mobility[PIECE_TYPES][28]
        {
            {}, {},
            { // Knight
                S(-62,-81), S(-53,-56), S(-12,-31), S( -4,-16), S(  3,  5), S( 13, 11),
                S( 22, 17), S( 28, 20), S( 33, 25)
            },
            { // Bishop
                S(-48,-59), S(-20,-23), S( 16, -3), S( 26, 13), S( 38, 24), S( 51, 42),
                S( 55, 54), S( 63, 57), S( 63, 65), S( 68, 73), S( 81, 78), S( 81, 86),
                S( 91, 88), S( 98, 97)
            },
            { // Rook
                S(-60,-78), S(-20,-17), S(  2, 23), S(  3, 39), S(  3, 70), S( 11, 99),
                S( 22,103), S( 31,121), S( 40,134), S( 40,139), S( 41,158), S( 48,164),
                S( 57,168), S( 57,169), S( 62,172)
            },
            { // Queen
                S(-30,-48), S(-12,-30), S( -8, -7), S( -9, 19), S( 20, 40), S( 23, 55),
                S( 23, 59), S( 35, 75), S( 38, 78), S( 53, 96), S( 64, 96), S( 65,100),
                S( 65,121), S( 66,127), S( 67,131), S( 67,133), S( 72,136), S( 72,141),
                S( 77,147), S( 79,150), S( 93,151), S(108,168), S(108,168), S(108,171),
                S(110,182), S(114,182), S(114,192), S(116,219)
            },
            {}
        };

        constexpr Score RookOnFile[2]
        {
            S(19, 7), S(48,29)
        };

        constexpr Score MinorThreat[PIECE_TYPES]
        {
            S( 0, 0), S( 5,32), S(57,41), S(77,56), S(88,119), S(79,161), S( 0, 0)
        };
        constexpr Score MajorThreat[PIECE_TYPES]
        {
            S( 0, 0), S( 3,46), S(37,68), S(42,60), S( 0, 38), S(58, 41), S( 0, 0)
        };

        constexpr Score PasserRank[RANKS]
        {
            S( 0, 0), S(10,28), S(17,33), S(15,41), S(62,72), S(168,177), S(276,260), S( 0, 0)
        };

        constexpr Score MinorBehindPawn   { S( 18,  3) };
        constexpr Score KnightOutpost     { S( 56, 36) };
        constexpr Score KnightReachOutpost{ S( 31, 22) };
        constexpr Score KnightBadOutpost  { S( -7, 36) };
        constexpr Score KnightKingProtect { S(  8,  9) };
        constexpr Score BishopOutpost     { S( 30, 23) };
        constexpr Score BishopKingProtect { S(  6,  9) };
        constexpr Score BishopOnDiagonal  { S( 45,  0) };
        constexpr Score BishopPawnsBlocked{ S(  3,  7) };
        constexpr Score BishopPawnsXRayed { S(  4,  5) };
        constexpr Score BishopOnKingRing  { S( 24,  0) };
        constexpr Score BishopTrapped     { S( 50, 50) };
        constexpr Score RookOnQueenFile   { S(  6, 11) };
        constexpr Score RookOnKingRing    { S( 16,  0) };
        constexpr Score RookTrapped       { S( 55, 13) };
        constexpr Score QueenAttacked     { S( 56, 15) };
        constexpr Score PawnLessFlank     { S( 17, 95) };
        constexpr Score PasserFile        { S( 11,  8) };
        constexpr Score KingFlankAttacks  { S(  8,  0) };
        constexpr Score PieceRestricted   { S(  7,  7) };
        constexpr Score PieceHanged       { S( 69, 36) };
        constexpr Score QueenProtected    { S( 14,  0) };
        constexpr Score PawnThreat        { S(173, 94) };
        constexpr Score PawnPushThreat    { S( 48, 39) };
        constexpr Score KingThreat        { S( 24, 89) };
        constexpr Score KnightOnQueen     { S( 16, 11) };
        constexpr Score SliderOnQueen     { S( 60, 18) };

    #undef S

        // Threshold for lazy and space evaluation
        constexpr Value LazyThreshold1{ Value( 1400) };
        constexpr Value LazyThreshold2{ Value( 1300) };
        constexpr Value SpaceThreshold{ Value(12222) };
        constexpr Value NNUEThreshold {   Value(575) };

        constexpr i32 SafeCheckWeight[PIECE_TYPES][2]
        {
            {0, 0}, {0, 0}, {792, 1283}, {645, 967}, {1084, 1897}, {772, 1119}, {0, 0}
        };
        constexpr i32 KingAttackerWeight[PIECE_TYPES]
        {
            0, 0,  81,  52,   44,  10, 0
        };

        // Evaluator class contains various evaluation functions.
        template<bool Trace>
        class Evaluation {
        private:
            Position const &pos;

            King    ::Entry *kingEntry{ nullptr };
            Material::Entry *matlEntry{ nullptr };
            Pawns   ::Entry *pawnEntry{ nullptr };

            // Contains all squares attacked by the color and piece type.
            Bitboard fulAttacks[COLORS];
            // Contains all squares attacked by the color and piece type with pinned removed.
            Bitboard sqlAttacks[COLORS][PIECE_TYPES];
            // Contains all squares attacked by more than one pieces of a color, possibly via x-ray or by one pawn and one piece.
            Bitboard dblAttacks[COLORS];
            // Contains all squares from which queen can be attacked
            Bitboard queenAttacked[COLORS][3];

            Bitboard mobArea[COLORS];
            Score mobility[COLORS];

            // The squares adjacent to the king plus some other very near squares, depending on king position.
            Bitboard kingRing[COLORS];
            // Number of pieces of the color, which attack a square in the kingRing of the enemy king.
            i32 kingAttackersCount[COLORS];
            // Sum of the "weight" of the pieces of the color which attack a square in the kingRing of the enemy king.
            // The weights of the individual piece types are given by the KingAttackerWeight[piece-type]
            i32 kingAttackersWeight[COLORS];
            // Number of attacks by the color to squares directly adjacent to the enemy king.
            // Pieces which attack more than one square are counted multiple times.
            // For instance, if there is a white knight on g5 and black's king is on g8, this white knight adds 2 to kingAttacksCount[WHITE]
            i32 kingAttacksCount[COLORS];

            bool canCastle[COLORS];

            template<Color> void initialize();
            template<Color, PieceType> Score pieces();
            template<Color> Score king() const;
            template<Color> Score threats() const;
            template<Color> Score passers() const;
            template<Color> Score space() const;

        public:

            Evaluation() = delete;
            Evaluation(Evaluation const&) = delete;
            Evaluation& operator=(Evaluation const&) = delete;

            Evaluation(Position const&);

            Value value();
        };

        template<bool Trace>
        Evaluation<Trace>::Evaluation(Position const &p) :
            pos{ p }
        {}

        /// initialize() computes pawn and king attacks also mobility and the king ring
        template<bool Trace> template<Color Own>
        void Evaluation<Trace>::initialize() {
            constexpr auto Opp{ ~Own };

            const auto kSq{ pos.square(Own|KING) };

            sqlAttacks[Own][PAWN] = pawnEntry->sglAttacks[Own];
            sqlAttacks[Own][KING] = attacksBB<KING>(kSq);

            fulAttacks[Own] =
            sqlAttacks[Own][NONE] = sqlAttacks[Own][PAWN]
                                  | sqlAttacks[Own][KING];

            dblAttacks[Own] = pawnEntry->dblAttacks[Own]
                            | (sqlAttacks[Own][PAWN]
                             & sqlAttacks[Own][KING]);

            // Mobility area: Exclude followings
            mobArea[Own] = ~(// Squares protected by enemy pawns
                             pawnEntry->sglAttacks[Opp]
                             // Squares occupied by friend Queen and King
                           | pos.pieces(Own, QUEN, KING)
                             // Squares occupied by friend King blockers
                           | pos.kingBlockers(Own)
                             // Squares occupied by block pawns (pawns on ranks 2-3/blocked)
                           | (pos.pieces(Own, PAWN)
                            & (LowRankBB[Own]
                             | pawnSglPushBB<Opp>(pos.pieces()))));

            mobility[Own] = SCORE_ZERO;

            // King safety tables
            auto sq{ makeSquare(clamp(sFile(kSq), FILE_B, FILE_G),
                                clamp(sRank(kSq), RANK_2, RANK_7)) };
            kingRing[Own] = attacksBB<KING>(sq) | sq;

            kingAttackersCount [Opp] = popCount(kingRing[Own]
                                              & pawnEntry->sglAttacks[Opp]);
            kingAttackersWeight[Opp] = 0;
            kingAttacksCount   [Opp] = 0;

            // Remove from kingRing the squares defended by two pawns
            kingRing[Own] &= ~pawnEntry->dblAttacks[Own];
            canCastle[Own] = pos.canCastle(Own)
                          && ((pos.canCastle(Own, CS_KING)
                            && pos.castleExpeded(Own, CS_KING))
                           || (pos.canCastle(Own, CS_QUEN)
                            && pos.castleExpeded(Own, CS_QUEN)));
        }

        /// pieces() evaluates the pieces of the color and type
        template<bool Trace> template<Color Own, PieceType PT>
        Score Evaluation<Trace>::pieces() {
            static_assert (NIHT <= PT && PT <= QUEN, "PT incorrect");
            constexpr auto Opp{ ~Own };

            auto kSq{ pos.square(Own|KING) };
            Bitboard kingBlockers{ pos.kingBlockers(Own) };

            sqlAttacks[Own][PT] = 0;

            Score score{ SCORE_ZERO };

            Square const *ps{ pos.squares(Own | PT) };
            if (PT == QUEN
             && *ps != SQ_NONE) {
                std::fill_n(queenAttacked[Own], 3, 0);
            }
            Square s;
            while ((s = *ps++) != SQ_NONE) {
                assert(pos[s] == (Own|PT));

                Bitboard action{
                    contains(kingBlockers, s) ?
                        lineBB(kSq, s) : BoardBB };

                // Find attacked squares, including x-ray attacks for Bishops, Rooks and Queens
                Bitboard attacks{
                    PT == NIHT ? attacksBB<NIHT>(s) & action :
                    PT == BSHP ? attacksBB<BSHP>(s, pos.pieces() ^ ((pos.pieces(Own, QUEN, BSHP) & ~kingBlockers) | pos.pieces(Opp, QUEN))) & action :
                    PT == ROOK ? attacksBB<ROOK>(s, pos.pieces() ^ ((pos.pieces(Own, QUEN, ROOK) & ~kingBlockers) | pos.pieces(Opp, QUEN))) & action :
                                 attacksBB<QUEN>(s, pos.pieces() ^ ((pos.pieces(Own, QUEN)       & ~kingBlockers))) & action };

                auto mob{ popCount(attacks & mobArea[Own]) };
                assert(0 <= mob && mob <= 27);

                // Bonus for piece mobility
                mobility[Own] += Mobility[PT][mob];

                Bitboard b;
                // Special evaluation for pieces
                if (PT == NIHT
                 || PT == BSHP) {

                    dblAttacks[Own] |= sqlAttacks[Own][NONE] & attacks;

                    // Bonus for minor shielded by pawn
                    score += MinorBehindPawn * (relativeRank(Own, s) <= RANK_6
                                             && contains(pos.pieces(PAWN), s + PawnPush[Own]));

                    // Bonus if the piece is on an outpost square or can reach one
                    // Reduced bonus for knights (BadOutpost) if few relevant targets
                    b =  OutpostBB[Own]
                      &  (sqlAttacks[Own][PAWN]
                        | pawnSglPushBB<Opp>(pos.pieces(PAWN)))
                      & ~pawnEntry->attacksSpan[Opp];

                    if (PT == NIHT) {
                        
                        // Bonus for knight outpost squares
                        Bitboard targets{ pos.pieces(Opp) & ~pos.pieces(PAWN) };
                        if (// On a side outpost
                            contains(b & ~SlotFileBB[CS_CENTRE], s)
                            // No relevant attacks
                         && (attacks & targets) == 0
                         && !moreThanOne(targets
                                       & (contains(SlotFileBB[CS_QUEN], s) ? SlotFileBB[CS_QUEN] : SlotFileBB[CS_KING]))) {
                            score += KnightBadOutpost;
                        }
                        else
                        if (contains(b, s)) {
                            score += KnightOutpost;
                        }
                        else
                        if ((b
                           & attacks
                           & ~pos.pieces(Own)) != 0) {
                            score += KnightReachOutpost;
                        }
                        // Penalty for knight distance from the friend king
                        score -= KnightKingProtect * distance(kSq, s);
                    }
                    else
                    if (PT == BSHP) {
                        // Bonus for bishop outpost squares
                        if (contains(b, s)) {
                            score += BishopOutpost;
                        }
                        // Penalty for bishop distance from the friend king
                        score -= BishopKingProtect * distance(kSq, s);

                        // Penalty for pawns on the same color square as the bishop,
                        // less when the bishop is protected by pawn
                        // more when the center files are blocked with pawns.
                        score -= BishopPawnsBlocked
                               * popCount(pos.pawnsOnSqColor(Own, sColor(s)))
                               * (!contains(sqlAttacks[Own][PAWN], s)
                                + popCount(pos.pieces(Own, PAWN)
                                         & SlotFileBB[CS_CENTRE]
                                         & pawnSglPushBB<Opp>(pos.pieces())));
                        // Penalty for all enemy pawns x-rayed
                        score -= BishopPawnsXRayed
                               * popCount(pos.pieces(Opp, PAWN)
                                        & attacksBB<BSHP>(s));
                        // Bonus for bishop on a long diagonal which can "see" both center squares
                        score += BishopOnDiagonal
                               * moreThanOne(attacksBB<BSHP>(s, pos.pieces(PAWN))
                                           & CenterBB);

                        // An important Chess960 pattern: A cornered bishop blocked by a friend pawn diagonally in front of it.
                        // It is a very serious problem, especially when that pawn is also blocked.
                        // Bishop (white or black) on a1/h1 or a8/h8 which is trapped by own pawn on b2/g2 or b7/g7.
                        if (mob <= 1
                         && Options["UCI_Chess960"]
                         && (relativeSq(Own, s) == SQ_A1
                          || relativeSq(Own, s) == SQ_H1)) {
                            auto Push{ PawnPush[Own] };
                            auto del{ Push + sign(FILE_E - sFile(s)) * EAST };
                            if (contains(pos.pieces(Own, PAWN), s + del)) {
                                score -= BishopTrapped
                                        * (!contains(pos.pieces(), s + del + Push) ?
                                                !contains(pos.pieces(Own, PAWN), s + del + del) ?
                                                    1 : 2 : 4);
                            }
                        }
                    }
                }
                else
                if (PT == ROOK) {

                    dblAttacks[Own] |= sqlAttacks[Own][NONE] & attacks;

                    // Bonus for rook on the same file as a queen
                    if ((pos.pieces(QUEN) & fileBB(s)) != 0) {
                        score += RookOnQueenFile;
                    }
                    // Bonus for rook when on an open or semi-open file
                    if (pos.semiopenFileOn(Own, s)) {
                        score += RookOnFile[pos.semiopenFileOn(Opp, s)];
                    }
                    else
                    // Penalty for rook when trapped by the king, even more if the king can't castle
                    if (mob <= 3
                     && relativeRank(Own, s) < RANK_4
                     && (pos.pieces(Own, PAWN) & frontSquaresBB(Own, s)) != 0) {
                        auto kF{ sFile(kSq) };
                        if (((kF < FILE_E) && (sFile(s) < kF))
                         || ((kF > FILE_D) && (sFile(s) > kF))) {
                            score -= RookTrapped * (1 + !pos.canCastle(Own));
                        }
                    }
                }
                else
                if (PT == QUEN) {

                    b =  pos.pieces(Own)
                      & ~kingBlockers;
                    dblAttacks[Own] |= sqlAttacks[Own][NONE]
                                     & ( attacks
                                      | (attacksBB<BSHP>(s, pos.pieces() ^ (pos.pieces(BSHP) & b & attacksBB<BSHP>(s))) & action)
                                      | (attacksBB<ROOK>(s, pos.pieces() ^ (pos.pieces(ROOK) & b & attacksBB<ROOK>(s))) & action));

                    queenAttacked[Own][0] |= attacksBB<NIHT>(s);
                    queenAttacked[Own][1] |= attacksBB<BSHP>(s, pos.pieces());
                    queenAttacked[Own][2] |= attacksBB<ROOK>(s, pos.pieces());

                    // Penalty for pin or discover attack on the queen
                    // Queen attackers
                    b =  pos.pieces(Opp, BSHP, ROOK)
                      & ~pos.kingBlockers(Opp);
                    if ((pos.sliderBlockersAt(s, b, b, b)
                      & ~(  pos.kingBlockers(Opp)
                        | ( pos.pieces(Opp, PAWN)
                         &  fileBB(s)
                         & ~pawnSglAttackBB<Own>(pos.pieces(Own))))) != 0) {
                        score -= QueenAttacked;
                    }
                }

                sqlAttacks[Own][PT]   |= attacks;
                sqlAttacks[Own][NONE] |= attacks;
                if (canCastle[Opp]) {
                fulAttacks[Own]       |= attacksBB<PT>(s, pos.pieces());
                }

                if ((attacks & kingRing[Opp]) != 0) {
                    kingAttackersCount [Own]++;
                    kingAttackersWeight[Own] += KingAttackerWeight[PT];
                    kingAttacksCount   [Own] += popCount(attacks
                                                       & sqlAttacks[Opp][KING]);
                }
                else {
                    if (PT == BSHP
                     && (attacksBB<BSHP>(s, pos.pieces(PAWN)) & kingRing[Opp]) != 0) {
                        score += BishopOnKingRing;
                    }
                    else
                    if (PT == ROOK
                     && (sFile(s) & kingRing[Opp]) != 0) {
                        score += RookOnKingRing;
                    }
                }
            }

            if (Trace) {
                Tracer::write(Term(PT), Own, score);
            }

            return score;
        }

        /// king() evaluates the king of the color
        template<bool Trace> template<Color Own>
        Score Evaluation<Trace>::king() const {
            constexpr auto Opp{ ~Own };

            auto kSq{ pos.square(Own|KING) };

            // Main king safety evaluation
            i32 kingDanger{ 0 };

            // Attacked squares defended at most once by friend queen or king
            Bitboard weakArea{
                 sqlAttacks[Opp][NONE]
              & ~dblAttacks[Own]
              & (~sqlAttacks[Own][NONE]
               |  sqlAttacks[Own][QUEN]
               |  sqlAttacks[Own][KING]) };

            // Safe squares where enemy's safe checks are possible on next move
            Bitboard safeArea{
                 ~pos.pieces(Opp)
              & (~sqlAttacks[Own][NONE]
               | (weakArea
                & dblAttacks[Opp])) };

            Bitboard unsafeCheck{ 0 };

            Bitboard rookPins{ attacksBB<ROOK>(kSq, pos.pieces() ^ pos.pieces(Own, QUEN)) };
            Bitboard bshpPins{ attacksBB<BSHP>(kSq, pos.pieces() ^ pos.pieces(Own, QUEN)) };

            // Enemy rooks checks
            Bitboard rookSafeChecks{  rookPins
                                   &  sqlAttacks[Opp][ROOK]
                                   &  safeArea};
            if (rookSafeChecks != 0) {
                kingDanger += SafeCheckWeight[ROOK][moreThanOne(rookSafeChecks)];
            }
            else {
                unsafeCheck |= rookPins
                             & sqlAttacks[Opp][ROOK];
            }

            // Enemy queens checks
            Bitboard quenSafeChecks{ (rookPins | bshpPins)
                                   &  sqlAttacks[Opp][QUEN]
                                   &  safeArea
                                   & ~sqlAttacks[Own][QUEN]
                                   & ~rookSafeChecks };
            if (quenSafeChecks != 0) {
                kingDanger += SafeCheckWeight[QUEN][moreThanOne(quenSafeChecks)];
            }

            // Enemy bishops checks
            Bitboard bshpSafeChecks{  bshpPins
                                   &  sqlAttacks[Opp][BSHP]
                                   &  safeArea
                                   & ~quenSafeChecks };
            if (bshpSafeChecks != 0) {
                kingDanger += SafeCheckWeight[BSHP][moreThanOne(bshpSafeChecks)];
            }
            else {
                unsafeCheck |= bshpPins
                             & sqlAttacks[Opp][BSHP];
            }

            // Enemy knights checks
            Bitboard nihtSafeChecks{  attacksBB<NIHT>(kSq)
                                   &  sqlAttacks[Opp][NIHT]
                                   &  safeArea };
            if (nihtSafeChecks != 0) {
                kingDanger += SafeCheckWeight[NIHT][moreThanOne(nihtSafeChecks)];
            }
            else {
                unsafeCheck |= attacksBB<NIHT>(kSq)
                             & sqlAttacks[Opp][NIHT];
            }

            Bitboard b;

            b =  KingFlankBB[sFile(kSq)]
              &  CampBB[Own]
              &  sqlAttacks[Opp][NONE];
            // Friend king flank attack count
            i32 kingFlankAttack = popCount(b)                     // Squares attacked by enemy in friend king flank
                          + popCount(b & dblAttacks[Opp]);  // Squares attacked by enemy twice in friend king flank.
            // Friend king flank defense count
            b =  KingFlankBB[sFile(kSq)]
              &  CampBB[Own]
              &  sqlAttacks[Own][NONE];
            i32 kingFlankDefense = popCount(b);

            // King Safety:
            Score score{ kingEntry->evaluateSafety<Own>(pos, fulAttacks[Opp]) };

            kingDanger +=   1 * (kingAttackersCount[Opp] * kingAttackersWeight[Opp])
                        +  69 * kingAttacksCount[Opp]
                        + 185 * popCount(kingRing[Own] & weakArea)
                        + 148 * popCount(unsafeCheck)
                        +  98 * popCount(pos.kingBlockers(Own))
                        +   3 * nSqr(kingFlankAttack) / 8
                        // Enemy queen is gone
                        - 873 * (pos.pieces(Opp, QUEN) == 0)
                        // Friend knight is near by to defend king
                        - 100 * (( sqlAttacks[Own][NIHT]
                                & (sqlAttacks[Own][KING] | kSq)) != 0)
                        // Mobility
                        -   1 * (mgValue(mobility[Own] - mobility[Opp]))
                        -   4 * kingFlankDefense
                        // Pawn Safety quality
                        -   3 * mgValue(score) / 4
                        +  37;

            // transform the king danger into a score
            if (kingDanger > 100) {
                score -= makeScore(nSqr(kingDanger) / 0x1000, kingDanger / 0x10);
            }

            // Penalty for king on a pawn less flank
            score -= PawnLessFlank * ((pos.pieces(PAWN) & KingFlankBB[sFile(kSq)]) == 0);

            // King tropism: Penalty for slow motion attacks moving towards friend king zone
            score -= KingFlankAttacks * kingFlankAttack;

            if (Trace) {
                Tracer::write(Term(KING), Own, score);
            }

            return score;
        }

        /// threats() evaluates the threats of the color
        template<bool Trace> template<Color Own>
        Score Evaluation<Trace>::threats() const {
            constexpr auto Opp{ ~Own };
            constexpr Bitboard Rank3{ RankBB[relativeRank(Own, RANK_3)] };

            Score score{ SCORE_ZERO };

            // Squares defended by the opponent,
            // - defended the square with a pawn
            // - defended the square twice and not attacked twice.
            Bitboard defendedArea{
                sqlAttacks[Opp][PAWN]
              | ( dblAttacks[Opp]
               & ~dblAttacks[Own]) };
            // Enemy non-pawns
            Bitboard nonPawnsEnemies{
                pos.pieces(Opp)
             & ~pos.pieces(PAWN) };
            // Enemy not defended and under attacked by any friend piece
            Bitboard attackedUndefendedEnemies{
                pos.pieces(Opp)
             & ~defendedArea
             &  sqlAttacks[Own][NONE] };
            // Non-pawn enemies, defended by enemies
            Bitboard defendedNonPawnsEnemies{
                nonPawnsEnemies
              & defendedArea };

            Bitboard b;

            if ((attackedUndefendedEnemies
               | defendedNonPawnsEnemies) != 0) {
                // Bonus according to the type of attacking pieces

                // Enemies attacked by minors
                b =  (attackedUndefendedEnemies
                    | defendedNonPawnsEnemies)
                  &  (sqlAttacks[Own][NIHT]
                    | sqlAttacks[Own][BSHP]);
                while (b != 0) {
                    score += MinorThreat[pType(pos[popLSq(b)])];
                }

                if (attackedUndefendedEnemies != 0) {
                    // Enemies attacked by majors
                    b =  attackedUndefendedEnemies
                      &  sqlAttacks[Own][ROOK];
                    while (b != 0) {
                        score += MajorThreat[pType(pos[popLSq(b)])];
                    }

                    // Enemies attacked by king
                    b =  attackedUndefendedEnemies
                      &  sqlAttacks[Own][KING];
                    if (b != 0) {
                        score += KingThreat;
                    }

                    // Enemies attacked are hanging
                    b =  attackedUndefendedEnemies
                      &  (~sqlAttacks[Opp][NONE]
                        | (nonPawnsEnemies
                         & dblAttacks[Own]));
                    score += PieceHanged * popCount(b);

                    // Additional bonus if weak piece is only protected by a queen
                    b =  attackedUndefendedEnemies
                      &  sqlAttacks[Opp][QUEN];
                    score += QueenProtected * popCount(b);
                }
            }

            // Bonus for restricting their piece moves
            b = ~defendedArea
              &  sqlAttacks[Opp][NONE]
              &  sqlAttacks[Own][NONE];
            score += PieceRestricted * popCount(b);

            // Defended or Unattacked squares
            Bitboard safeArea{
                ~sqlAttacks[Opp][NONE]
              | (sqlAttacks[Own][NONE]
               & ~dblAttacks[Opp])
              | dblAttacks[Own] };

            // Safe friend pawns
            b =  safeArea
              &  pos.pieces(Own, PAWN);
            // Safe friend pawns attacks on non-pawn enemies
            b =  nonPawnsEnemies
              &  pawnSglAttackBB<Own>(b);
            score += PawnThreat * popCount(b);

            // Friend pawns who can push on the next move
            b =  pos.pieces(Own, PAWN)
              & ~pos.kingBlockers(Own);
            // Friend pawns push (squares where friend pawns can push on the next move)
            b =  pawnSglPushBB<Own>(b)
              & ~pos.pieces();
            b |= pawnSglPushBB<Own>(b & Rank3)
              & ~pos.pieces();
            // Friend pawns push safe (only the squares which are relatively safe)
            b &= safeArea
              & ~sqlAttacks[Opp][PAWN];
            // Friend pawns push safe attacks an enemies
            b =  nonPawnsEnemies
              &  pawnSglAttackBB<Own>(b);
            score += PawnPushThreat * popCount(b);

            // Bonus for threats on the next moves against enemy queens
            if (pos.pieces(Opp, QUEN) != 0) {
                bool queenImbalance{ pos.count(Own|QUEN) < pos.count(Opp|QUEN) };

                safeArea =  mobArea[Own]
                         & ~pos.pieces(Own, PAWN)
                         & ~defendedArea;

                b = safeArea
                  & (sqlAttacks[Own][NIHT] & queenAttacked[Opp][0]);
                score += KnightOnQueen * popCount(b) * (1 + queenImbalance);

                b = safeArea
                  & ((sqlAttacks[Own][BSHP] & queenAttacked[Opp][1])
                   | (sqlAttacks[Own][ROOK] & queenAttacked[Opp][2]))
                  & dblAttacks[Own];
                score += SliderOnQueen * popCount(b) * (1 + queenImbalance);
            }

            if (Trace) {
                Tracer::write(THREAT, Own, score);
            }

            return score;
        }

        /// passers() evaluates the passed pawns of the color
        template<bool Trace> template<Color Own>
        Score Evaluation<Trace>::passers() const {
            constexpr auto Opp{ ~Own };
            constexpr auto Push{ PawnPush[Own] };

            auto kingProximity{ [&](Color c, Square s) {
                return std::min(distance(pos.square(c|KING), s), 5);
            } };

            Bitboard pass{ pawnEntry->passeds[Own] };
            Bitboard blockedPass{ pass
                                & pawnSglPushBB<Opp>(pos.pieces(Opp, PAWN)) };
            if (blockedPass != 0) {
                // Can we lever the blocker of a blocked passer?
                Bitboard helpers{  pawnSglPushBB<Own>(pos.pieces(Own, PAWN))
                                & ~pos.pieces(Opp)
                                & (~dblAttacks[Opp]
                                 |  sqlAttacks[Own][NONE]) };
                // Remove blocked otherwise
                pass &= ~blockedPass
                      | shift<WEST>(helpers)
                      | shift<EAST>(helpers);
            }

            Score score{ SCORE_ZERO };

            while (pass != 0) {
                auto s{ popLSq(pass) };
                assert((pos.pieces(Opp, PAWN) & frontSquaresBB(Own, s + Push)) == 0);

                i32 r{ relativeRank(Own, s) };
                // Base bonus depending on rank.
                Score bonus{ PasserRank[r] };

                auto pushSq{ s + Push };
                if (r > RANK_3) {
                    i32 w{ 5 * r - 13 };

                    // Adjust bonus based on the king's proximity
                    bonus += makeScore(0, i32(+4.75*w*kingProximity(Opp, pushSq)
                                              -2.00*w*kingProximity(Own, pushSq)));
                    // If pushSq is not the queening square then consider also a second push.
                    if (r < RANK_7) {
                        bonus += makeScore(0, -1*w*kingProximity(Own, pushSq + Push));
                    }

                    // If the pawn is free to advance.
                    if (pos.empty(pushSq)) {
                        Bitboard behindMajors{ frontSquaresBB(Opp, s)
                                             & pos.pieces(ROOK, QUEN) };

                        Bitboard attackedSquares{ pawnPassSpan(Own, s) };
                        if ((behindMajors & pos.pieces(Opp)) == 0) {
                            attackedSquares &= sqlAttacks[Opp][NONE];
                        }

                        i32 k = // Bonus according to attacked squares
                              + 15 * ((attackedSquares) == 0)
                              + 11 * ((attackedSquares & frontSquaresBB(Own, s)) == 0)
                              +  9 * !contains(attackedSquares, pushSq)
                                // Bonus according to defended squares
                              +  5 * ((behindMajors & pos.pieces(Own)) != 0
                                   || contains(sqlAttacks[Own][NONE], pushSq));

                        bonus += makeScore(k*w, k*w);
                    }
                }
                // Pass bonus = Rank bonus + File bonus
                score += bonus
                       - PasserFile * edgeDistance(sFile(s));
            }

            if (Trace) {
                Tracer::write(PASSER, Own, score);
            }

            return score;
        }

        /// Evaluation::space() computes a space evaluation for a given side,
        /// aiming to improve game play in the opening.
        /// It is based on the number of safe squares on the 4 central files on ranks 2 to 4.
        /// Completely safe squares behind a friendly pawn are counted twice.
        /// Finally, the space bonus is multiplied by a weight which decreases according to occupancy.
        template<bool Trace> template<Color Own>
        Score Evaluation<Trace>::space() const {
            constexpr auto Opp{ ~Own };

            // Safe squares for friend pieces inside the area defined by SpaceMask.
            Bitboard safeSpace{  SlotFileBB[CS_CENTRE]
                              &  PawnSideBB[Own]
                              & ~pos.pieces(Own, PAWN)
                              & ~sqlAttacks[Opp][PAWN] };
            // Find all squares which are at most three squares behind some friend pawn
            Bitboard behind{ pos.pieces(Own, PAWN) };
            behind |= pawnSglPushBB<Opp>(behind);
            behind |= pawnDblPushBB<Opp>(behind);

            i32 bonus{ popCount(safeSpace)
                     + popCount( behind
                              &  safeSpace
                              & ~sqlAttacks[Opp][NONE]) };
            i32 weight{ pos.count(Own)
                      + std::min(pawnEntry->blockedCount(), 9)
                      - 3 };
            Score score{ makeScore(bonus * weight * weight / 16, 0) };

            if (Trace) {
                Tracer::write(SPACE, Own, score);
            }

            return score;
        }

        /// value() computes the various parts of the evaluation and
        /// returns the value of the position from the point of view of the side to move.
        template<bool Trace>
        Value Evaluation<Trace>::value() {
            assert(pos.checkers() == 0);

            // Probe the material hash table
            matlEntry = Material::probe(pos);
            // If have a specialized evaluation function for the material configuration
            if (matlEntry->evaluationFunc != nullptr) {
                return (*matlEntry->evaluationFunc)(pos);
            }

            // Probe the pawn hash table
            pawnEntry = Pawns::probe(pos);

            // Score is computed internally from the white point of view.
            // Initialize by
            // - incrementally updated scores (material + piece square tables)
            // - material imbalance
            // - pawn score
            // - dynamic contempt
            Score score{ pos.psqScore()
                       + matlEntry->imbalance
                       + (pawnEntry->score[WHITE] - pawnEntry->score[BLACK])
                       + pos.thread()->contempt };

            // Early exit if score is high
            auto lazySkip = [&](Value lazyThreshold) {
                return std::abs(mgValue(score) + egValue(score)) / 2 > lazyThreshold + pos.nonPawnMaterial() / 64;
            };

            if (lazySkip(LazyThreshold1)) {
                goto returnValue;
            }

            // Probe the king hash table
            kingEntry = King::probe(pos, pawnEntry);

            if (Trace) {
                Tracer::clear();
            }

            initialize<WHITE>();
            initialize<BLACK>();

            // Pieces should be evaluated first (also populate attack information)
            // Note that the order of evaluation of the terms is left unspecified
            score += pieces<WHITE, NIHT>() - pieces<BLACK, NIHT>()
                   + pieces<WHITE, BSHP>() - pieces<BLACK, BSHP>()
                   + pieces<WHITE, ROOK>() - pieces<BLACK, ROOK>()
                   + pieces<WHITE, QUEN>() - pieces<BLACK, QUEN>();

            assert((sqlAttacks[WHITE][NONE] & dblAttacks[WHITE]) == dblAttacks[WHITE]);
            assert((sqlAttacks[BLACK][NONE] & dblAttacks[BLACK]) == dblAttacks[BLACK]);
            // More complex interactions that require fully populated attack information
            score += mobility[WHITE]   - mobility[BLACK]
                   + king    <WHITE>() - king    <BLACK>()
                   + passers <WHITE>() - passers <BLACK>();

            if (lazySkip(LazyThreshold2)) {
                goto returnValue;
            }

            score += threats <WHITE>() - threats <BLACK>();
            // Skip if, for example, both queens or 6 minor pieces have been exchanged
            if (pos.nonPawnMaterial() >= SpaceThreshold) {
            score += space   <WHITE>() - space   <BLACK>();
            }

        returnValue:
            // Derive single value from mg and eg parts of score
            auto wkSq{ pos.square(W_KING) };
            auto bkSq{ pos.square(B_KING) };

            i32 outflanking{ fileDistance(wkSq, bkSq)
                           - rankDistance(wkSq, bkSq) };

            // Compute the initiative bonus for the attacking side
            i32 complexity =  1 * pawnEntry->complexity
                           +  9 * outflanking
                           // King infiltration
                           + 24 * (sRank(wkSq) > RANK_4
                                || sRank(bkSq) < RANK_5)
                           + 51 * (pos.nonPawnMaterial() == VALUE_ZERO)
                           // Pawn not on both flanks
                           - 21 * (pawnEntry->pawnNotBothFlank)
                           // Almost Unwinnable
                           - 43 * (pawnEntry->pawnNotBothFlank
                                && outflanking < 0)
                           - 89;

            auto mg{ mgValue(score) };
            auto eg{ egValue(score) };

            // Now apply the bonus: note that we find the attacking side by extracting the
            // sign of the midgame or endgame values, and that we carefully cap the bonus
            // so that the midgame and endgame scores do not change sign after the bonus.
            auto mv{ sign(mg) * clamp(complexity + 50, -std::abs(mg), 0) };
            auto ev{ sign(eg) * std::max(complexity, -std::abs(eg)) };

            mg += mv;
            eg += ev;

            // Compute the scale factor for the winning side
            auto strongSide{ eg > VALUE_ZERO ? WHITE : BLACK };

            Scale scale{ matlEntry->scalingFunc[strongSide] != nullptr ?
                        (*matlEntry->scalingFunc[strongSide])(pos) : SCALE_NONE };
            if (scale == SCALE_NONE) {
                scale = matlEntry->scaleFactor[strongSide];
            }
            // If scaleFactor is not already specific, scaleFactor down the endgame via general heuristics
            if (scale == SCALE_NORMAL) {
                if (pos.bishopOpposed()) {
                    scale = Scale(pos.nonPawnMaterial() == 2 * VALUE_MG_BSHP ?
                                    18 + 4 * popCount(pawnEntry->passeds[strongSide]) :
                                    22 + 3 * pos.count());
                }
                else
                if (pos.nonPawnMaterial(WHITE) == VALUE_MG_ROOK
                 && pos.nonPawnMaterial(BLACK) == VALUE_MG_ROOK
                 && (pos.count( strongSide|PAWN)
                   - pos.count(~strongSide|PAWN)) <= 1
                 && (bool(pos.pieces(strongSide, PAWN) & SlotFileBB[CS_KING])
                  != bool(pos.pieces(strongSide, PAWN) & SlotFileBB[CS_QUEN]))
                 && (attacksBB<KING>(pos.square(~strongSide|KING))
                   & pos.pieces(~strongSide, PAWN)) != 0) {
                    scale = Scale(36);
                }
                else
                if (pos.count(QUEN) == 1) {
                    auto queenColor{ pColor(pos[scanLSq(pos.pieces(QUEN))]) };
                    scale = Scale(37 + 3 * (pos.count(~queenColor|NIHT) + pos.count(~queenColor|BSHP)));
                }
                else {
                    scale = std::min(Scale(36 + 7 * pos.count(strongSide|PAWN)), SCALE_NORMAL);
                }
            }

            Value v;
            // Interpolates between midgame and scaled endgame values (scaled by 'scaleFactor(egValue(score))').
            v = mg * (matlEntry->phase)
              + eg * (Material::PhaseResolution - matlEntry->phase) * scale / SCALE_NORMAL;
            v /= Material::PhaseResolution;
            assert(-VALUE_INFINITE < v && v < +VALUE_INFINITE);

            // Evaluation grain
            v = (v / 16) * 16;
            // Active side's point of view
            v = (pos.activeSide() == WHITE ? +v : -v) + VALUE_TEMPO;

            // Damp down the evaluation linearly when shuffling
            v = v * (100 - pos.clockPly()) / 100;

            // Write remaining evaluation terms
            if (Trace) {
                Tracer::write(Term(PAWN), pawnEntry->score[WHITE], pawnEntry->score[BLACK]);
                Tracer::write(MATERIAL  , pos.psqScore());
                Tracer::write(IMBALANCE , matlEntry->imbalance);
                Tracer::write(MOBILITY  , mobility[WHITE], mobility[BLACK]);                
                Tracer::write(SCALING   , makeScore(mv, ev + eg * (scale / SCALE_NORMAL - 1)));
                Tracer::write(TOTAL     , makeScore(mg, eg * scale / SCALE_NORMAL));
            }

            return v;
        }
    }

    /// evaluate() returns a static evaluation of the position from the point of view of the side to move.
    Value evaluate(Position const &pos) {
        if (useNNUE) {
            return NNUE::evaluate(pos);
            Value v = egValue(pos.psqScore());
            // Take NNUE eval only on balanced positions
            if (abs(v) < NNUEThreshold) {
                return NNUE::evaluate(pos) + VALUE_TEMPO;
            }
        }
        return Evaluation<false>(pos).value();
    }

    /// trace() returns a string (suitable for outputting to stdout for debugging)
    /// that contains the detailed descriptions and values of each evaluation term.
    std::string trace(Position const &pos) {
        if (pos.checkers() != 0) {
            return "Evaluation: none (in check)\n";
        }

        std::ostringstream oss;

        Value value;

        if (useNNUE) {
            value = NNUE::evaluate(pos);
        }
        else {
            // Reset any dynamic contempt
            auto contempt{ pos.thread()->contempt };
            pos.thread()->contempt = SCORE_ZERO;
            value = Evaluation<true>(pos).value();
            pos.thread()->contempt = contempt;

            oss << "      Eval Term |    White    |    Black    |    Total     \n"
                << "                |   MG    EG  |   MG    EG  |   MG    EG   \n"
                << "----------------+-------------+-------------+--------------\n"
                << "       Material" << Term(MATERIAL)
                << "      Imbalance" << Term(IMBALANCE)
                << "           Pawn" << Term(PAWN)
                << "         Knight" << Term(NIHT)
                << "         Bishop" << Term(BSHP)
                << "           Rook" << Term(ROOK)
                << "          Queen" << Term(QUEN)
                << "       Mobility" << Term(MOBILITY)
                << "           King" << Term(KING)
                << "         Threat" << Term(THREAT)
                << "         Passer" << Term(PASSER)
                << "          Space" << Term(SPACE)
                << "        Scaling" << Term(SCALING)
                << "----------------+-------------+-------------+--------------\n"
                << "          Total" << Term(TOTAL);
        }
        
        // Trace scores are from White's point of view
        value = (pos.activeSide() == WHITE ? +value : -value);
        oss << std::showpos << std::showpoint << std::fixed << std::setprecision(2)
            << "\nEvaluation: " << toCP(value) / 100 << " (white side)\n";

        return oss.str();
    }
}
