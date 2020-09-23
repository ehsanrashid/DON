#include "Evaluator.h"

#include <cassert>
#include <cstdlib>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <vector>

#include "BitBoard.h"
#include "Helper.h"
#include "King.h"
#include "Material.h"
#include "Pawns.h"
#include "Position.h"
#include "Notation.h"
#include "Thread.h"
#include "UCI.h"
#include "incbin/incbin.h"

// Macro to embed the default NNUE file data in the engine binary (using incbin.h, by Dale Weiler).
// This macro invocation will declare the following three variables
//     const unsigned char        gEmbeddedNNUEData[];  // a pointer to the embedded data
//     const unsigned char *const gEmbeddedNNUEEnd;     // a marker to the end
//     const unsigned int         gEmbeddedNNUESize;    // the size of the embedded file
// Note that this does not work in Microsof Visual Studio.
#if !defined(_MSC_VER) && !defined(NNUE_EMBEDDING_OFF)
    INCBIN(EmbeddedNNUE, DefaultEvalFile);
#else
    const unsigned char        gEmbeddedNNUEData[1]{ 0x0 };
    const unsigned char *const gEmbeddedNNUEEnd    { &gEmbeddedNNUEData[1] };
    const unsigned int         gEmbeddedNNUESize   { 1 };
#endif


namespace Evaluator {

    bool useNNUE{ false };
    std::string loadedEvalFile{ "None" };

    // C++ way to prepare a buffer for a memory stream
    template<class T>
    class MemoryStreamBuffer :
        public std::basic_streambuf<T> {

    public:
        using std::basic_streambuf<T>::basic_streambuf;

        MemoryStreamBuffer(MemoryStreamBuffer const&) = delete;
        MemoryStreamBuffer(MemoryStreamBuffer&&) = delete;

        MemoryStreamBuffer(T *p, size_t n) {
            std::basic_streambuf<T>::setg(p, p, p + n);
            std::basic_streambuf<T>::setp(p, p + n);
        }
    };

    namespace NNUE {
        /// initialize() tries to load a nnue network at startup time, or when the engine
        /// receives a UCI command "setoption name EvalFile value nn-[a-z0-9]{12}.nnue"
        /// The name of the nnue network is always retrieved from the EvalFile option.
        /// We search the given network in three locations: internally (the default
        /// network may be embedded in the binary), in the active working directory and
        /// in the engine directory. Distro packagers may define the DEFAULT_NNUE_DIRECTORY
        /// variable to have the engine search in a special directory in their distro.
        void initialize() {

            useNNUE = Options["Use NNUE"];
            auto evalFile{ std::string(Options["Eval File"]) };

            if (useNNUE) {

                std::vector<std::string> directories{
                      "<internal>"
                    , ""
                    , CommandLine::binaryDirectory
            #if defined(DEFAULT_NNUE_DIRECTORY)
                    , STRINGIFY(DEFAULT_NNUE_DIRECTORY)
            #endif
                };

                for (auto &dir : directories) {
                    if (loadedEvalFile != evalFile) {

                        if (dir != "<internal>") {
                            std::ifstream ifstream{ dir + evalFile, std::ios::in | std::ios::binary };
                            if (NNUE::loadEvalFile(ifstream)) {
                                loadedEvalFile = evalFile;
                            }
                        }
                        else
                            if (evalFile == DefaultEvalFile) {
                                MemoryStreamBuffer buffer{ const_cast<char *>(reinterpret_cast<const char *>(gEmbeddedNNUEData)), size_t(gEmbeddedNNUESize) };
                                std::istream istream{ &buffer };
                                if (NNUE::loadEvalFile(istream)) {
                                    loadedEvalFile = evalFile;
                                }
                            }

                    }
                }
            }
        }

        void verify() {

            auto evalFile{ std::string(Options["Eval File"]) };
            if (useNNUE) {
                if (loadedEvalFile != evalFile) {
                    sync_cout << "info string ERROR: NNUE evaluation used, but the network file " << evalFile << " was not loaded successfully.\n"
                        << "info string ERROR: These network evaluation parameters must be available, and compatible with this version of the code.\n"
                        << "info string ERROR: The UCI option 'Eval File' might need to specify the full path, including the directory/folder name, to the file.\n"
                        << "info string ERROR: The default net can be downloaded from: https://tests.stockfishchess.org/api/nn/" << Options["Eval File"].defaultValue() << sync_endl;
                    std::exit(EXIT_FAILURE);
                }
                sync_cout << "info string NNUE evaluation using " << evalFile << " enabled." << sync_endl;
            }
            else {
                sync_cout << "info string classical evaluation enabled." << sync_endl;
            }
        }
    }

    namespace {

        enum Term : u08 { MATERIAL = 8, IMBALANCE, MOBILITY, THREAT, PASSER, SPACE, SCALING, TOTAL, TERMS = 16 };

        class Tracer {

        public:
            static void clear() {
                //std::fill(&Scores[0][0], &Scores[0][0] + sizeof (Scores) / sizeof (Scores[0][0]), SCORE_ZERO);
                std::fill_n(&Scores[0][0], sizeof (Scores) / sizeof (Scores[0][0]), SCORE_ZERO);
            }

            static void write(Term t, Color c, Score s) noexcept {
                Scores[t][c] = s;
            }
            static void write(Term t, Score sW, Score sB = SCORE_ZERO) noexcept {
                write(t, WHITE, sW);
                write(t, BLACK, sB);
            }

        private:
            static Score Scores[TERMS][COLORS];

            friend std::ostream& operator<<(std::ostream&, Term);
        };

        Score Tracer::Scores[TERMS][COLORS];

        std::ostream& operator<<(std::ostream &ostream, Term t) {
            if (t == MATERIAL
             || t == IMBALANCE
             || t == SCALING
             || t == TOTAL) {
                ostream << " | ------ ------" << " | ------ ------";
            }
            else {
                ostream << " | " << Tracer::Scores[t][WHITE] << " | " << Tracer::Scores[t][BLACK];
            }
            ostream << " | " << Tracer::Scores[t][WHITE] - Tracer::Scores[t][BLACK] << " |\n";
            return ostream;
        }

        constexpr Bitboard CenterBB{ (FileBB[FILE_D]|FileBB[FILE_E]) & (RankBB[RANK_4]|RankBB[RANK_5]) };

        constexpr Bitboard LowRankBB[COLORS]{
            RankBB[RANK_2]|RankBB[RANK_3],
            RankBB[RANK_7]|RankBB[RANK_6]
        };

        constexpr Bitboard CampBB[COLORS]{
            RankBB[RANK_1]|RankBB[RANK_2]|RankBB[RANK_3]|RankBB[RANK_4]|RankBB[RANK_5],
            RankBB[RANK_8]|RankBB[RANK_7]|RankBB[RANK_6]|RankBB[RANK_5]|RankBB[RANK_4]
        };

        constexpr Bitboard OutpostBB[COLORS]{
            RankBB[RANK_4]|RankBB[RANK_5]|RankBB[RANK_6],
            RankBB[RANK_5]|RankBB[RANK_4]|RankBB[RANK_3]
        };

        constexpr Bitboard KingFlankBB[FILES]{
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

        constexpr Score Mobility[4][28]{
            {   S(-62,-81), S(-53,-56), S(-12,-31), S( -4,-16), S(  3,  5), S( 13, 11),
                S( 22, 17), S( 28, 20), S( 33, 25)
            },
            {   S(-48,-59), S(-20,-23), S( 16, -3), S( 26, 13), S( 38, 24), S( 51, 42),
                S( 55, 54), S( 63, 57), S( 63, 65), S( 68, 73), S( 81, 78), S( 81, 86),
                S( 91, 88), S( 98, 97)
            },
            {   S(-60,-78), S(-20,-17), S(  2, 23), S(  3, 39), S(  3, 70), S( 11, 99),
                S( 22,103), S( 31,121), S( 40,134), S( 40,139), S( 41,158), S( 48,164),
                S( 57,168), S( 57,169), S( 62,172)
            },
            {   S(-30,-48), S(-12,-30), S( -8, -7), S( -9, 19), S( 20, 40), S( 23, 55),
                S( 23, 59), S( 35, 75), S( 38, 78), S( 53, 96), S( 64, 96), S( 65,100),
                S( 65,121), S( 66,127), S( 67,131), S( 67,133), S( 72,136), S( 72,141),
                S( 77,147), S( 79,150), S( 93,151), S(108,168), S(108,168), S(108,171),
                S(110,182), S(114,182), S(114,192), S(116,219)
            },
        };

        constexpr Score RookOnFile[2]{
            S(19, 7), S(48,29)
        };

        constexpr Score MinorThreat[PIECE_TYPES]{
            S( 0, 0), S( 5,32), S(55,41), S(77,56), S(89,119), S(79,162), S( 0, 0)
        };
        constexpr Score MajorThreat[PIECE_TYPES]{
            S( 0, 0), S( 3,44), S(37,68), S(42,60), S( 0, 39), S(58, 43), S( 0, 0)
        };

        constexpr Score PasserRank[RANKS]{
            S( 0, 0), S( 9,28), S(15,31), S(17,39), S(64,70), S(171,177), S(277,260), S( 0, 0)
        };

        constexpr Score MinorBehindPawn   { S( 18,  3) };
        constexpr Score KnightOutpost     { S( 56, 34) };
        constexpr Score KnightReachOutpost{ S( 31, 22) };
        constexpr Score KnightBadOutpost  { S( -7, 36) };
        constexpr Score KnightKingProtect { S(  8,  9) };
        constexpr Score BishopOutpost     { S( 31, 23) };
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
        constexpr Value NNUEThreshold1{   Value(550) };
        constexpr Value NNUEThreshold2{   Value(150) };

        constexpr i32 SafeCheckWeight[4][2]{
            {792, 1283}, {645, 967}, {1084, 1897}, {772, 1119}
        };
        constexpr i32 KingAttackerWeight[4]{
            81, 52, 44, 10
        };

        // Evaluator class contains various evaluation functions.
        template<bool Trace>
        class Evaluation {

        public:
            Evaluation() = delete;
            Evaluation(Evaluation const&) = delete;
            Evaluation& operator=(Evaluation const&) = delete;

            Evaluation(Position const&) noexcept;

            Value value();

        private:
            template<Color> void initialize();
            template<Color, PieceType> Score pieces();
            template<Color> Score king() const;
            template<Color> Score threats() const;
            template<Color> Score passers() const;
            template<Color> Score space() const;

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
            Score   mobility[COLORS];

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
        };

        /// initialize() computes pawn and king attacks also mobility and the king ring
        template<bool Trace> template<Color Own>
        void Evaluation<Trace>::initialize() {
            constexpr auto Opp{ ~Own };

            auto const kSq{ pos.square(Own|KING) };

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
            auto const sq{
                makeSquare(std::clamp(sFile(kSq), FILE_B, FILE_G),
                           std::clamp(sRank(kSq), RANK_2, RANK_7)) };
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

            auto const kSq{ pos.square(Own|KING) };
            Bitboard const kingBlockers{ pos.kingBlockers(Own) };

            sqlAttacks[Own][PT] = 0;

            Score score{ SCORE_ZERO };

            Square const *ps{ pos.squares(Own|PT) };
            if (PT == QUEN
             && *ps != SQ_NONE) {
                std::fill_n(queenAttacked[Own], 3, 0);
            }
            Square s;
            while ((s = *ps++) != SQ_NONE) {
                assert(pos[s] == (Own|PT));

                Bitboard const action{
                    contains(kingBlockers, s) ? lineBB(kSq, s) : BoardBB };

                // Find attacked squares, including x-ray attacks for Bishops, Rooks and Queens
                Bitboard attacks{
                    PT == NIHT ? attacksBB<NIHT>(s) & action :
                    PT == BSHP ? attacksBB<BSHP>(s, pos.pieces() ^ ((pos.pieces(Own, QUEN, BSHP) & ~kingBlockers) | pos.pieces(Opp, QUEN))) & action :
                    PT == ROOK ? attacksBB<ROOK>(s, pos.pieces() ^ ((pos.pieces(Own, QUEN, ROOK) & ~kingBlockers) | pos.pieces(Opp, QUEN))) & action :
                                 attacksBB<QUEN>(s, pos.pieces() ^ ((pos.pieces(Own, QUEN)       & ~kingBlockers))) & action };

                auto const mob{ popCount(attacks & mobArea[Own]) };
                assert(0 <= mob && mob <= 27);

                // Bonus for piece mobility
                mobility[Own] += Mobility[PT - 2][mob];

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
                        Bitboard const targets{ pos.pieces(Opp) & ~pos.pieces(PAWN) };
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

                            auto const Push{ PawnPush[Own] };
                            auto const del{ Push + sign(FILE_E - sFile(s)) * EAST };
                            if (contains(pos.pieces(Own, PAWN), s + del)) {
                                score -= BishopTrapped
                                       * (contains(pos.pieces(), s + del + Push)         ? 4 :
                                          contains(pos.pieces(Own, PAWN), s + del + del) ? 2 : 1);
                            }
                        }
                    }
                }
                else
                if (PT == ROOK) {

                    dblAttacks[Own] |= sqlAttacks[Own][NONE] & attacks;

                    // Bonus for rook on the same file as a queen
                    if ((fileBB(s) & pos.pieces(QUEN)) != 0) {
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
                        auto const kF{ sFile(kSq) };
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
                    kingAttackersWeight[Own] += KingAttackerWeight[PT - 2];
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

            auto const kSq{ pos.square(Own|KING) };

            // Main king safety evaluation
            i32 kingDanger{ 0 };

            // Attacked squares defended at most once by friend queen or king
            Bitboard const weakArea{
                 sqlAttacks[Opp][NONE]
              & ~dblAttacks[Own]
              & (~sqlAttacks[Own][NONE]
               |  sqlAttacks[Own][QUEN]
               |  sqlAttacks[Own][KING]) };

            // Safe squares where enemy's safe checks are possible on next move
            Bitboard const safeArea{
                 ~pos.pieces(Opp)
              & (~sqlAttacks[Own][NONE]
               | (weakArea & dblAttacks[Opp])) };

            Bitboard unsafeCheck{ 0 };

            Bitboard const mocc{ pos.pieces() ^ pos.pieces(Own, QUEN) };
            Bitboard const rookPins{ attacksBB<ROOK>(kSq, mocc) };
            Bitboard const bshpPins{ attacksBB<BSHP>(kSq, mocc) };

            // Enemy rooks checks
            Bitboard const rookSafeChecks{
                rookPins
              & sqlAttacks[Opp][ROOK]
              & safeArea };

            if (rookSafeChecks != 0) {
                kingDanger += SafeCheckWeight[ROOK - 2][moreThanOne(rookSafeChecks)];
            }
            else {
                unsafeCheck |= rookPins
                             & sqlAttacks[Opp][ROOK];
            }

            // Enemy queens checks
            Bitboard const quenSafeChecks{
                (rookPins | bshpPins)
              & sqlAttacks[Opp][QUEN]
              & safeArea
              & ~sqlAttacks[Own][QUEN]
              & ~rookSafeChecks };

            if (quenSafeChecks != 0) {
                kingDanger += SafeCheckWeight[QUEN - 2][moreThanOne(quenSafeChecks)];
            }

            // Enemy bishops checks
            Bitboard bshpSafeChecks{
                bshpPins
              & sqlAttacks[Opp][BSHP]
              & safeArea
              & ~quenSafeChecks };

            if (bshpSafeChecks != 0) {
                kingDanger += SafeCheckWeight[BSHP - 2][moreThanOne(bshpSafeChecks)];
            }
            else {
                unsafeCheck |= bshpPins
                             & sqlAttacks[Opp][BSHP];
            }

            // Enemy knights checks
            Bitboard nihtSafeChecks{
                attacksBB<NIHT>(kSq)
              & sqlAttacks[Opp][NIHT]
              & safeArea };

            if (nihtSafeChecks != 0) {
                kingDanger += SafeCheckWeight[NIHT - 2][moreThanOne(nihtSafeChecks)];
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
            i32 const kingFlankAttack{
                popCount(b)                      // Squares attacked by enemy in friend king flank
              + popCount(b & dblAttacks[Opp]) }; // Squares attacked by enemy twice in friend king flank.
            // Friend king flank defense count
            b =  KingFlankBB[sFile(kSq)]
              &  CampBB[Own]
              &  sqlAttacks[Own][NONE];
            i32 const kingFlankDefense{
                popCount(b) };                   // Squares attacked by friend in friend king flank

            // King Safety:
            Score score{ kingEntry->evaluateSafety<Own>(pos, fulAttacks[Opp]) };

            kingDanger +=   1 * kingAttackersCount[Opp] * kingAttackersWeight[Opp]
                        +  69 * kingAttacksCount[Opp]
                        + 185 * popCount(kingRing[Own] & weakArea)
                        + 148 * popCount(unsafeCheck)
                        +  98 * popCount(pos.kingBlockers(Own))
                        +   3 * i32(nSqr(kingFlankAttack)) / 8
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
                score -= makeScore(u32(nSqr(kingDanger) / 0x1000), kingDanger / 0x10);
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
            Bitboard const defendedArea{
                sqlAttacks[Opp][PAWN]
              | ( dblAttacks[Opp]
               & ~dblAttacks[Own]) };
            // Enemy non-pawns
            Bitboard const nonPawnsEnemies{
                pos.pieces(Opp)
             & ~pos.pieces(PAWN) };
            // Enemy not defended and under attacked by any friend piece
            Bitboard const attackedUndefendedEnemies{
                pos.pieces(Opp)
             & ~defendedArea
             &  sqlAttacks[Own][NONE] };
            // Non-pawn enemies, defended by enemies
            Bitboard const defendedNonPawnsEnemies{
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
              | (sqlAttacks[Own][NONE] & ~dblAttacks[Opp])
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
                bool const queenImbalance{ pos.count(Own|QUEN) < pos.count(Opp|QUEN) };

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

            auto const kingProximity{
                [&](Color c, Square s) {
                    return std::min(distance(pos.square(c|KING), s), 5);
                }
            };

            Bitboard pass{ pawnEntry->passeds[Own] };
            Bitboard const blockedPass{
                pass
              & pawnSglPushBB<Opp>(pos.pieces(Opp, PAWN)) };
            if (blockedPass != 0) {
                // Can we lever the blocker of a blocked passer?
                Bitboard const helpers{
                    pawnSglPushBB<Own>(pos.pieces(Own, PAWN))
                  & ~pos.pieces(Opp)
                  & (~dblAttacks[Opp] | sqlAttacks[Own][NONE]) };
                // Remove blocked otherwise
                pass &= ~blockedPass
                      | shift<WEST>(helpers)
                      | shift<EAST>(helpers);
            }

            Score score{ SCORE_ZERO };

            while (pass != 0) {
                auto const s{ popLSq(pass) };
                assert((pos.pieces(Opp, PAWN) & frontSquaresBB(Own, s + Push)) == 0);

                i32 const r{ relativeRank(Own, s) };
                // Base bonus depending on rank.
                Score bonus{ PasserRank[r] };

                auto const pushSq{ s + Push };
                if (r > RANK_3) {
                    i32 const w{ 5 * r - 13 };

                    // Adjust bonus based on the king's proximity
                    bonus += makeScore(0,  kingProximity(Opp, pushSq) * w * 19 / 4
                                         + kingProximity(Own, pushSq) * w * -2);
                    // If pushSq is not the queening square then consider also a second push.
                    if (r < RANK_7) {
                        bonus += makeScore(0, kingProximity(Own, pushSq + Push) * w * -1);
                    }

                    // If the pawn is free to advance.
                    if (pos.empty(pushSq)) {
                        Bitboard const behindMajors{ frontSquaresBB(Opp, s) & pos.pieces(ROOK, QUEN) };

                        Bitboard attackedSquares{ pawnPassSpan(Own, s) };
                        if ((behindMajors & pos.pieces(Opp)) == 0) {
                            attackedSquares &= sqlAttacks[Opp][NONE];
                        }

                        i32 const k{
                                // Bonus according to attacked squares
                              + 15 * ((attackedSquares) == 0)
                              + 11 * ((attackedSquares & frontSquaresBB(Own, s)) == 0)
                              +  9 * !contains(attackedSquares, pushSq)
                                // Bonus according to defended squares
                              +  5 * ((behindMajors & pos.pieces(Own)) != 0
                                   || contains(sqlAttacks[Own][NONE], pushSq)) };

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
        /// It is based on the number of safe squares on the four central files on ranks 2 to 4.
        /// Completely safe squares behind a friendly pawn are counted twice.
        /// Finally, the space bonus is multiplied by a weight which decreases according to occupancy.
        template<bool Trace> template<Color Own>
        Score Evaluation<Trace>::space() const {
            constexpr auto Opp{ ~Own };

            // Safe squares for friend pieces inside the area defined by SpaceMask.
            Bitboard const safeSpace{
                 SlotFileBB[CS_CENTRE]
              &  PawnSideBB[Own]
              & ~pos.pieces(Own, PAWN)
              & ~sqlAttacks[Opp][PAWN] };
            // Find all squares which are at most three squares behind some friend pawn
            Bitboard behind{ pos.pieces(Own, PAWN) };
            behind |= pawnSglPushBB<Opp>(behind);
            behind |= pawnDblPushBB<Opp>(behind);

            i32 const bonus{
                popCount(safeSpace)
              + popCount(  behind
                        &  safeSpace
                        & ~sqlAttacks[Opp][NONE]) };
            i32 const weight{
                pos.count(Own)
              + std::min(pawnEntry->blockedCount(), 9)
              - 3 };

            Score const score{ makeScore(bonus * weight * weight / 16, 0) };

            if (Trace) {
                Tracer::write(SPACE, Own, score);
            }

            return score;
        }


        template<bool Trace>
        Evaluation<Trace>::Evaluation(Position const &p) noexcept :
            pos{ p }
        {}

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
            Score score{
                pos.psqScore()
              + matlEntry->imbalance
              + (pawnEntry->score[WHITE] - pawnEntry->score[BLACK])
              + pos.thread()->contempt };

            // Early exit if score is high
            auto const lazySkip{
                [&](Value lazyThreshold) {
                    return std::abs(mgValue(score) + egValue(score)) / 2 > lazyThreshold + pos.nonPawnMaterial() / 64;
                }
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
            // Winnable:
            // Derive single value from mg and eg parts of score
            auto const wkSq{ pos.square(W_KING) };
            auto const bkSq{ pos.square(B_KING) };

            i32 const outflanking{
                distance<File>(wkSq, bkSq)
              - distance<Rank>(wkSq, bkSq) };

            // Compute the initiative bonus for the attacking side
            i32 const complexity{
                 1 * pawnEntry->complexity
              +  9 * outflanking
                // King infiltration
              + 24 * (sRank(wkSq) > RANK_4
                   || sRank(bkSq) < RANK_5)
              + 51 * (pos.nonPawnMaterial() == VALUE_ZERO)
                // Pawn on both flanks
              + 21 * (pawnEntry->pawnOnBothFlank)
                // Almost Unwinnable
              - 43 * (outflanking < 0
                  && !pawnEntry->pawnOnBothFlank)
              - 110 };

            auto mg{ mgValue(score) };
            auto eg{ egValue(score) };

            // Now apply the bonus: note that we find the attacking side by extracting the
            // sign of the midgame or endgame values, and that we carefully cap the bonus
            // so that the midgame and endgame scores do not change sign after the bonus.
            auto mv{ sign(mg) * std::clamp(complexity + 50, -std::abs(mg), 0) };
            auto ev{ sign(eg) * std::max(complexity, -std::abs(eg)) };

            mg += mv;
            eg += ev;

            // Compute the scale factor for the winning side
            auto const strongSide{ eg > VALUE_ZERO ? WHITE : BLACK };

            Scale scale{ matlEntry->scalingFunc[strongSide] != nullptr ?
                        (*matlEntry->scalingFunc[strongSide])(pos) : SCALE_NONE };
            if (scale == SCALE_NONE) {
                scale = matlEntry->scaleFactor[strongSide];
            }
            // If scale factor is not already specific, scale down via general heuristics
            if (scale == SCALE_NORMAL) {
                if (pos.bishopOpposed()) {
                    scale = Scale(pos.nonPawnMaterial() == 2 * VALUE_MG_BSHP ?
                                    18 + 4 * popCount(pawnEntry->passeds[strongSide]) :
                                    22 + 3 * pos.count());
                }
                else
                if (pos.nonPawnMaterial(WHITE) == VALUE_MG_ROOK
                 && pos.nonPawnMaterial(BLACK) == VALUE_MG_ROOK
                 && pos.count( strongSide|PAWN)
                  - pos.count(~strongSide|PAWN) <= 1
                 && bool(pos.pieces(strongSide, PAWN) & SlotFileBB[CS_KING])
                 != bool(pos.pieces(strongSide, PAWN) & SlotFileBB[CS_QUEN])
                 && (sqlAttacks[~strongSide][KING] & pos.pieces(~strongSide, PAWN)) != 0) {
                    scale = Scale(36);
                }
                else
                if (pos.count(QUEN) == 1) {
                    auto const queenColor{ pColor(pos[scanLSq(pos.pieces(QUEN))]) };
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
            v = (pos.activeSide() == WHITE ? +v : -v);
            v += VALUE_TEMPO;

            // Write remaining evaluation terms
            if (Trace) {
                Tracer::write(Term(PAWN), pawnEntry->score[WHITE], pawnEntry->score[BLACK]);
                Tracer::write(MATERIAL  , pos.psqScore());
                Tracer::write(IMBALANCE , matlEntry->imbalance);
                Tracer::write(MOBILITY  , mobility[WHITE], mobility[BLACK]);                
                Tracer::write(SCALING   , makeScore(mv, ev - eg + eg * scale / SCALE_NORMAL));
                Tracer::write(TOTAL     , makeScore(mg,         + eg * scale / SCALE_NORMAL));
            }

            return v;
        }
    }

    /// evaluate() returns a static evaluation of the position from the point of view of the side to move.
    Value evaluate(Position const &pos) {
        assert(pos.checkers() == 0);

        Value v;

        if (useNNUE) {
            // scale and shift NNUE for compatibility with search and classical evaluation
            auto adjustedNNUE = [&pos]() { return NNUE::evaluate(pos) * 5 / 4 + VALUE_TEMPO; };

            // if there is PSQ imbalance use classical eval, with small probability if it is small
            Value psq = Value(std::abs(egValue(pos.psqScore())));
            int   r50 = pos.clockPly() + 16;
            bool  psqLarge = psq * 16 > (NNUEThreshold1 + pos.nonPawnMaterial() / 64) * r50;
            bool  classical = psqLarge || (psq > VALUE_MG_PAWN / 4 && !(pos.thread()->nodes & 0xB));

            if (classical) {
                v = Evaluation<false>(pos).value();

                // if the classical eval is small and imbalance large, use NNUE nevertheless.
                if (psqLarge
                 && abs(v) * 16 < NNUEThreshold2 * r50) {
                    v = adjustedNNUE();
                }
            }
            else {
                v = adjustedNNUE();
            }
        }
        else {
            v = Evaluation<false>(pos).value();
        }

        // Damp down the evaluation linearly when shuffling
        v = v * (100 - pos.clockPly()) / 100;

        // Guarantee evaluation does not hit the tablebase range
        return std::clamp(v, -VALUE_MATE_2_MAX_PLY + 1, +VALUE_MATE_2_MAX_PLY - 1);
    }

    /// trace() returns a string (suitable for outputting to stdout for debugging)
    /// that contains the detailed descriptions and values of each evaluation term.
    std::string trace(Position const &pos) {
        if (pos.checkers() != 0) {
            return "Evaluation: none (in check)\n";
        }

        std::ostringstream oss{};
        oss << std::showpos << std::showpoint << std::fixed << std::setprecision(2);

        // Set dynamic contempt to zero
        auto const contempt{ pos.thread()->contempt };
        pos.thread()->contempt = SCORE_ZERO;

        Value value;

        value = Evaluation<true>(pos).value();

        oss << "      Eval Term |      White    |      Black    |      Total    |\n"
            << "                |    MG     EG  |    MG     EG  |    MG    EG   |\n"
            << "----------------+---------------+---------------+---------------|\n"
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
            << "----------------+---------------+---------------+---------------\n"
            << "          Total" << Term(TOTAL);
        // Trace scores are from White's point of view
        value = (pos.activeSide() == WHITE ? +value : -value);
        oss << "\nClassical Evaluation: " << toCP(value) / 100 << " (white side)\n";

        if (useNNUE) {
            value = NNUE::evaluate(pos);
            // Trace scores are from White's point of view
            value = (pos.activeSide() == WHITE ? +value : -value);
            oss << "\nNNUE Evaluation     : " << toCP(value) / 100 << " (white side)\n";
        }

        value = evaluate(pos);

        // Trace scores are from White's point of view
        value = (pos.activeSide() == WHITE ? +value : -value);
        oss << "\nFinal Evaluation    : " << toCP(value) / 100 << " (white side)\n";

        // Set dynamic contempt back
        pos.thread()->contempt = contempt;

        return oss.str();
    }
}
