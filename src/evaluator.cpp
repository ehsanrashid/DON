#include "evaluator.h"

#include <cassert>
#include <cstdlib>
#include <algorithm>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "bitboard.h"
#include "king.h"
#include "material.h"
#include "pawns.h"
#include "position.h"
#include "notation.h"
#include "thread.h"
#include "uci.h"
#include "incbin/incbin.h"
#include "helper/commandline.h"
#include "helper/memorystreambuffer.h"

// Macro to embed the default NNUE file data in the engine binary (using incbin.h, by Dale Weiler).
// This macro invocation will declare the following three variables
//     const unsigned char        gEmbeddedNNUEData[];  // a pointer to the embedded data
//     const unsigned char *const gEmbeddedNNUEEnd;     // a marker to the end
//     const unsigned int         gEmbeddedNNUESize;    // the size of the embedded file
// Note that this does not work in Microsoft Visual Studio.
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

    namespace NNUE {

        /// initialize() tries to load a NNUE network at startup time, or when the engine
        /// receives a UCI command "setoption name EvalFile value nn-[a-z0-9]{12}.nnue"
        /// The name of the NNUE network is always retrieved from the EvalFile option.
        /// We search the given network in three locations: internally (the default
        /// network may be embedded in the binary), in the active working directory and
        /// in the engine directory. Distro packagers may define the DEFAULT_NNUE_DIRECTORY
        /// variable to have the engine search in a special directory in their distro.
        void initialize() noexcept {

            useNNUE = Options["Use NNUE"];
            auto evalFile{ std::string(Options["Eval File"]) };
            if (evalFile == loadedEvalFile) return;

            if (useNNUE) {

                // "<internal>" embedded eval file
                if (evalFile == DefaultEvalFile) {
                    MemoryStreamBuffer buffer{ const_cast<char*>(reinterpret_cast<char const*>(gEmbeddedNNUEData)), size_t(gEmbeddedNNUESize) };
                    std::istream istream{ &buffer };
                    if (NNUE::loadEvalFile(istream)) {
                        loadedEvalFile = evalFile;
                        return;
                    }
                }

                std::vector<std::string> directories{
                    "",
                    CommandLine::binaryDirectory
                #if defined(DEFAULT_NNUE_DIRECTORY)
                    , STRINGIFY(DEFAULT_NNUE_DIRECTORY)
                #endif
                };
                for (auto const &dir : directories) {

                    std::ifstream ifstream{ dir + evalFile, std::ios::in|std::ios::binary };
                    if (NNUE::loadEvalFile(ifstream)) {
                        loadedEvalFile = evalFile;
                        return;
                    }
                }

            }
        }

        void verify() noexcept {

            auto evalFile{ std::string(Options["Eval File"]) };
            if (useNNUE) {
                if (evalFile != loadedEvalFile) {
                    sync_cout
                        << "info string ERROR: NNUE evaluation used, but the network file " << evalFile << " was not loaded successfully.\n"
                        << "info string ERROR: These network evaluation parameters must be available, and compatible with this version of the code.\n"
                        << "info string ERROR: The UCI option 'Eval File' might need to specify the full path, including the directory/folder name, to the file.\n"
                        << "info string ERROR: The default net can be downloaded from: https://tests.stockfishchess.org/api/nn/" << Options["Eval File"].defaultValue() << sync_endl;
                    //system("pause");
                    std::exit(EXIT_FAILURE);
                }
                sync_cout << "info string NNUE evaluation using " << evalFile << " enabled." << sync_endl;
            } else {
                sync_cout << "info string classical evaluation enabled." << sync_endl;
            }
        }
    }

    namespace {

        enum Term : uint8_t { MATERIAL = 8, IMBALANCE, MOBILITY, THREAT, PASSER, SPACE, SCALING, TOTAL, TERMS = 16 };

        namespace Tracer {

            Score Scores[TERMS][COLORS];

            void clear() {
                //std::fill(&Scores[0][0], &Scores[0][0] + sizeof(Scores) / sizeof(Scores[0][0]), SCORE_ZERO);
                std::fill_n(&Scores[0][0], sizeof(Scores) / sizeof(Scores[0][0]), SCORE_ZERO);
            }

            void write(Term t, Color c, Score s) noexcept {
                Scores[t][c] = s;
            }
            void write(Term t, Score wS, Score bS = SCORE_ZERO) noexcept {
                write(t, WHITE, wS);
                write(t, BLACK, bS);
            }

            std::string toString(Term t) {

                std::ostringstream oss;
                if (t == MATERIAL
                 || t == IMBALANCE
                 || t == SCALING
                 || t == TOTAL) {
                    oss << " | ------ ------" << " | ------ ------";
                } else {
                    oss << " | " << Scores[t][WHITE] << " | " << Scores[t][BLACK];
                }
                oss << " | " << Scores[t][WHITE] - Scores[t][BLACK] << " |\n";

                return oss.str();
            }
        }

        std::ostream& operator<<(std::ostream &ostream, Term t) {
            ostream << Tracer::toString(t);
            return ostream;
        }

        constexpr Bitboard DiagonalBB{ U64(0x8142241818244281) }; // A1..H8 | H1..A8
        constexpr Bitboard CenterBB{ (fileBB(FILE_D)|fileBB(FILE_E)) & (rankBB(RANK_4)|rankBB(RANK_5)) };

        constexpr Bitboard LowRankBB[COLORS]{
            rankBB(RANK_2)|rankBB(RANK_3),
            rankBB(RANK_7)|rankBB(RANK_6)
        };

        constexpr Bitboard CampBB[COLORS]{
            rankBB(RANK_1)|rankBB(RANK_2)|rankBB(RANK_3)|rankBB(RANK_4)|rankBB(RANK_5),
            rankBB(RANK_8)|rankBB(RANK_7)|rankBB(RANK_6)|rankBB(RANK_5)|rankBB(RANK_4)
        };

        constexpr Bitboard OutpostRankBB[COLORS]{
            rankBB(RANK_4)|rankBB(RANK_5)|rankBB(RANK_6),
            rankBB(RANK_5)|rankBB(RANK_4)|rankBB(RANK_3)
        };

        constexpr Bitboard KingFlankBB[FILES]{
            slotFileBB(CS_QUEN) ^ fileBB(FILE_D),
            slotFileBB(CS_QUEN),
            slotFileBB(CS_QUEN),
            slotFileBB(CS_CENTRE),
            slotFileBB(CS_CENTRE),
            slotFileBB(CS_KING),
            slotFileBB(CS_KING),
            slotFileBB(CS_KING) ^ fileBB(FILE_E)
        };
        // Center file and pawn side
        constexpr Bitboard SpaceMaskBB[COLORS]{
            slotFileBB(CS_CENTRE) & (rankBB(RANK_2)|rankBB(RANK_3)|rankBB(RANK_4)),
            slotFileBB(CS_CENTRE) & (rankBB(RANK_7)|rankBB(RANK_6)|rankBB(RANK_5))
        };


        constexpr Value BishopTrapped = Value(50);

        auto constexpr S = makeScore;

        constexpr Score Mobility[PIECE_TYPES_EX][28]{
            {},
            {},
            {   S(-62,-79), S(-53,-57), S(-12,-31), S( -3,-17), S(  3,  7), S( 12, 13), // Knight
                S( 21, 16), S( 28, 21), S( 37, 26) },
            {   S(-47,-59), S(-20,-25), S( 14, -8), S( 29, 12), S( 39, 21), S( 53, 40), // Bishop
                S( 53, 56), S( 60, 58), S( 62, 65), S( 69, 72), S( 78, 78), S( 83, 87),
                S( 91, 88), S( 96, 98) },
            {   S(-60,-82), S(-24,-15), S(  0, 17) ,S(  3, 43), S(  4, 72), S( 14,100), // Rook
                S( 20,102), S( 30,122), S( 41,133), S( 41,139), S( 41,153), S( 45,160),
                S( 57,165), S( 58,170), S( 67,175) },
            {   S(-29,-49), S(-16,-29), S( -8, -8), S( -8, 17), S( 18, 39), S( 25, 54), // Queen
                S( 23, 59), S( 37, 73), S( 41, 76), S( 54, 95), S( 65, 95) ,S( 68,101),
                S( 69,124), S( 70,128), S( 70,132), S( 70,133) ,S( 71,136), S( 72,140),
                S( 74,147), S( 76,149), S( 90,153), S(104,169), S(105,171), S(106,171),
                S(112,178), S(114,185), S(114,187), S(119,221) },
        };

        // ThreatByMinor[attacked PieceType] contains bonuses for minor according to which piece type attacks which one
        constexpr Score MinorThreat[PIECE_TYPES_EX]{
            S(0, 0), S( 5, 32), S(55, 41), S(77, 56), S(89, 119), S(79,162)
        };
        // ThreatByMajor[attacked PieceType] contains bonuses for rook according to which piece type attacks which one
        constexpr Score MajorThreat[PIECE_TYPES_EX]{
            S(0, 0), S( 3, 44), S(37, 68), S(42, 60), S( 0, 39), S(58, 43)
        };
        // PasserRank[Rank] contains a bonus according to the rank of a passed pawn
        constexpr Score PasserRank[RANKS]{
            S(0, 0), S( 7, 27), S(16, 32), S(17, 40), S(64, 71), S(170,174), S(278,262), S(0, 0)
        };

        constexpr Score BishopPawns[FILES / 2]{
            S(3, 8), S(3, 9), S(2, 8), S(3, 8)
        };

        constexpr Score MinorBehindPawn   { S( 18,  3) };
        constexpr Score KnightBadOutpost  { S(  1, 10) };
        constexpr Score KnightOutpost     { S( 57, 38) };
        constexpr Score KnightReachOutpost{ S( 31, 22) };
        constexpr Score KnightKingProtect { S(  8,  9) };
        constexpr Score BishopOutpost     { S( 31, 24) };
        constexpr Score BishopKingProtect { S(  6,  9) };
        constexpr Score BishopOnDiagonal  { S( 45,  0) };
        constexpr Score BishopPawnsXRayed { S(  4,  5) };
        constexpr Score BishopOnKingRing  { S( 24,  0) };
        constexpr Score RookOnSemiopenFile{ S( 19,  6) };
        constexpr Score RookOnFullopenFile{ S( 28, 20) };
        constexpr Score RookOnClosedFile  { S( 10,  5) };
        constexpr Score RookOnKingRing    { S( 16,  0) };
        constexpr Score RookTrapped       { S( 55, 13) };
        constexpr Score QueenAttacked     { S( 56, 15) };
        constexpr Score PawnLessFlank     { S( 17, 95) };
        constexpr Score PasserFile        { S( 11,  8) };
        constexpr Score KingFlankAttacks  { S(  8,  0) };
        constexpr Score PieceRestricted   { S(  7,  7) };
        constexpr Score PieceHanged       { S( 69, 36) };
        constexpr Score QueenProtection   { S( 14,  0) };
        constexpr Score PawnThreat        { S(173, 94) };
        constexpr Score PawnPushThreat    { S( 48, 39) };
        constexpr Score KingThreat        { S( 24, 89) };
        constexpr Score KnightOnQueen     { S( 16, 11) };
        constexpr Score SliderOnQueen     { S( 60, 18) };


        // Threshold for lazy and space evaluation
        constexpr Value LazyThreshold1{ Value( 1565) };
        constexpr Value LazyThreshold2{ Value( 1102) };
        constexpr Value SpaceThreshold{ Value(11551) };
        constexpr Value NNUEThreshold1{   Value(682) };
        constexpr Value NNUEThreshold2{   Value(176) };

        constexpr int32_t SafeCheckWeight[PIECE_TYPES_EX][2]{
            {0,0}, {0,0}, {803, 1292}, {639, 974}, {1087, 1878}, {759, 1132}
        };
        constexpr int32_t KingAttackerWeight[PIECE_TYPES_EX]{
            0, 0, 81, 52, 44, 10
        };

        // Evaluator class contains various evaluation functions.
        template<bool Trace>
        class Evaluation {

        public:

            explicit Evaluation(Position const &p) noexcept :
                pos{ p } {
            }
            Evaluation() = delete;
            Evaluation(Evaluation const&) = delete;
            Evaluation(Evaluation&&) = delete;

            Evaluation& operator=(Evaluation const&) = delete;
            Evaluation& operator=(Evaluation&&) = delete;

            Value value();

        private:

            template<Color> void initialize();
            template<Color, PieceType> Score pieces();
            template<Color> Score king() const;
            template<Color> Score threats() const;
            template<Color> Score passers() const;
            template<Color> Score space() const;

            Position const &pos;

            Material::Entry *matlEntry{ nullptr };
            Pawns   ::Entry *pawnEntry{ nullptr };
            King    ::Entry *kingEntry{ nullptr };

            // Contains all squares attacked by the color and piece type.
            Bitboard attackedFull[COLORS];
            // Contains all squares attacked by the color and piece type with pinned removed.
            Bitboard attackedBy[COLORS][PIECE_TYPES];
            // Contains all squares attacked by more than one pieces of a color, possibly via x-ray or by one pawn and one piece.
            Bitboard attackedBy2[COLORS];
            // Contains all squares from which queen can be attacked by Knight, Bishop & Rook
            Bitboard queenAttacked[COLORS][3];

            Bitboard mobArea[COLORS];
            Score   mobility[COLORS];

            // The squares adjacent to the king plus some other very near squares, depending on king position.
            Bitboard kingRing[COLORS];
            // Number of pieces of the color, which attack a square in the kingRing of the enemy king.
            int32_t kingAttackersCount[COLORS];
            // Sum of the "weight" of the pieces of the color which attack a square in the kingRing of the enemy king.
            // The weights of the individual piece types are given by the KingAttackerWeight[piece-type]
            int32_t kingAttackersWeight[COLORS];
            // Number of attacks by the color to squares directly adjacent to the enemy king.
            // Pieces which attack more than one square are counted multiple times.
            // For instance, if there is a white knight on g5 and black's king is on g8, this white knight adds 2 to kingAttacksCount[WHITE]
            int32_t kingAttacksCount[COLORS];

            bool canCastle[COLORS];
        };

        /// initialize() computes pawn and king attacks also mobility and the king ring
        template<bool Trace> template<Color Own>
        void Evaluation<Trace>::initialize() {
            constexpr auto Opp{ ~Own };

            auto const kSq{ pos.square(Own|KING) };

            attackedBy[Own][PAWN] = pawnEntry->sglAttacks[Own];
            attackedBy[Own][KING] = attacksBB(KING, kSq);

            attackedFull[Own] =
            attackedBy[Own][NONE] = attackedBy[Own][PAWN]
                                  | attackedBy[Own][KING];

            attackedBy2[Own] = pawnEntry->dblAttacks[Own]
                             | (attackedBy[Own][PAWN]
                              & attackedBy[Own][KING]);

            std::fill_n(queenAttacked[Own], 3, 0);

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
            kingRing[Own] = attacksBB(KING, sq) | sq;

            kingAttackersCount [Opp] = popCount(kingRing[Own]
                                              & pawnEntry->sglAttacks[Opp]);
            kingAttackersWeight[Opp] = 0;
            kingAttacksCount   [Opp] = 0;

            // Remove from kingRing the squares defended by two pawns
            kingRing[Own] &= ~pawnEntry->dblAttacks[Own];
            canCastle[Own] = pos.canCastle(Own)
                          && ((pos.canCastle(Own, CS_KING) && pos.castleExpeded(Own, CS_KING))
                           || (pos.canCastle(Own, CS_QUEN) && pos.castleExpeded(Own, CS_QUEN)));
        }

        /// pieces() evaluates the pieces of the color and type
        template<bool Trace> template<Color Own, PieceType PT>
        Score Evaluation<Trace>::pieces() {
            static_assert(NIHT <= PT && PT <= QUEN, "PT incorrect");
            constexpr auto Opp{ ~Own };

            attackedBy[Own][PT] = 0;

            Score score{ SCORE_ZERO };

            Bitboard bb{ pos.pieces(Own, PT) };
            while (bb != 0) {
                auto const s{ popLSq(bb) };
                assert(pos.pieceOn(s) == (Own|PT));

                // Find attacked squares, including x-ray attacks for Bishops, Rooks and Queens
                Bitboard attacks{
                    PT == NIHT ? attacksBB(NIHT, s) :
                    PT == BSHP ? attacksBB<BSHP>(s, pos.pieces() ^ ((pos.pieces(Own, QUEN, BSHP) & ~pos.kingBlockers(Own)) | pos.pieces(Opp, QUEN))) :
                    PT == ROOK ? attacksBB<ROOK>(s, pos.pieces() ^ ((pos.pieces(Own, QUEN, ROOK) & ~pos.kingBlockers(Own)) | pos.pieces(Opp, QUEN))) :
                                 attacksBB<QUEN>(s, pos.pieces() ^ ((pos.pieces(Own, QUEN)       & ~pos.kingBlockers(Own)))) };
                assert(popCount(attacks) <= 27);

                if (pos.isKingBlockersOn(Own, s)) {
                    attacks &= lineBB(pos.square(Own|KING), s);
                }

                if ((kingRing[Opp] & attacks) != 0) {
                    kingAttackersCount [Own]++;
                    kingAttackersWeight[Own] += KingAttackerWeight[PT];
                    kingAttacksCount   [Own] += popCount(attacks & attackedBy[Opp][KING]);
                } else
                if (PT == BSHP
                 && (kingRing[Opp]
                   & attacksBB<BSHP>(s, pos.pieces(PAWN))) != 0) {
                    score += BishopOnKingRing;
                } else
                if (PT == ROOK
                 && (kingRing[Opp]
                   & fileBB(s)
                   /*& attacksBB<ROOK>(s, pos.pieces(Own, PAWN))*/) != 0) {
                    score += RookOnKingRing;
                }

                auto const mob{ popCount(attacks & mobArea[Own]) };

                // Bonus for piece mobility
                mobility[Own] += Mobility[PT][mob];

                Bitboard b;
                // Special evaluation for pieces
                if (PT == NIHT
                 || PT == BSHP) {

                    attackedBy2[Own] |= attackedBy[Own][NONE] & attacks;

                    Bitboard const sglPullPawns{ pawnSglPushBB<Opp>(pos.pieces(PAWN)) };
                    // Bonus for a knight or bishop shielded by pawn
                    if (contains(sglPullPawns, s)) {
                        score += MinorBehindPawn;
                    }

                    // Bonus if the piece is on an outpost square or can reach one
                    b =  OutpostRankBB[Own]
                      &  (sglPullPawns
                        | attackedBy[Own][PAWN])
                      & ~pawnEntry->attacksSpan[Opp];

                    if (PT == NIHT) {

                        // Bonus for knight outpost squares
                        if (contains(b, s)) {
                            
                            // Bonus for knights if few relevant targets
                            Bitboard const targets{ pos.pieces(Opp) & ~pos.pieces(PAWN) };
                            if (// On a side outpost
                                !contains(slotFileBB(CS_CENTRE), s)
                                // No relevant attacks
                             && (attacks & targets) == 0
                             && !moreThanOne(targets & (contains(slotFileBB(CS_QUEN), s) ? slotFileBB(CS_QUEN) : slotFileBB(CS_KING)))) {
                                score += KnightBadOutpost * popCount(pos.pieces(PAWN) & (contains(slotFileBB(CS_QUEN), s) ? slotFileBB(CS_QUEN) : slotFileBB(CS_KING)));
                            } else {
                                score += KnightOutpost;
                            }
                        } else {
                            if ((b & attacks & ~pos.pieces(Own)) != 0) {
                                score += KnightReachOutpost;
                            }
                        }

                        // Penalty for knight distance from the friend king
                        score -= KnightKingProtect * distance(pos.square(Own|KING), s);
                    } else
                    if (PT == BSHP) {

                        // Bonus for bishop outpost squares
                        if (contains(b, s)) {
                            score += BishopOutpost;
                        }

                        // Penalty for bishop distance from the friend king
                        score -= BishopKingProtect * distance(pos.square(Own|KING), s);

                        // Penalty for pawns on the same color square as the bishop,
                        // less when the bishop is protected by pawn
                        // more when the center files are blocked with pawns.
                        Bitboard const blockedPawns{ pos.pieces(Own, PAWN) & pawnSglPushBB<Opp>(pos.pieces()) };
                        score -= BishopPawns[edgeDistance(sFile(s))]
                               * popCount(pos.pawnsOnColor(Own, s))
                               * (popCount(blockedPawns & slotFileBB(CS_CENTRE))
                                + !contains(attackedBy[Own][PAWN], s));
                        // Penalty for all enemy pawns x-rayed
                        score -= BishopPawnsXRayed
                               * popCount(pos.pieces(Opp, PAWN) & attacksBB(BSHP, s));
                        // Bonus for bishop on a long diagonal which can "see" both center squares
                        if (contains(DiagonalBB, s)
                         && moreThanOne(attacksBB<BSHP>(s, pos.pieces(PAWN)) & CenterBB)) {
                            score += BishopOnDiagonal;
                        }

                        // An important Chess960 pattern: A cornered bishop blocked by a friend pawn diagonally in front of it.
                        // It is a very serious problem, especially when that pawn is also blocked.
                        // Bishop (white or black) on a1/h1 or a8/h8 which is trapped by own pawn on b2/g2 or b7/g7.
                        if (mob <= 1
                         && Options["UCI_Chess960"]
                         && (relativeSq(Own, s) == SQ_A1
                          || relativeSq(Own, s) == SQ_H1)) {

                            auto const del{ PawnPush[Own] + sign(FILE_E - sFile(s)) * EAST };
                            if (pos.pieceOn(s + del) == (Own|PAWN)) {
                                score -= pos.emptyOn(s + del + PawnPush[Own]) ?
                                            3 * makeScore(BishopTrapped, BishopTrapped) :
                                            4 * makeScore(BishopTrapped, BishopTrapped);
                            }
                        }
                    }
                } else
                if (PT == ROOK) {

                    attackedBy2[Own] |= attackedBy[Own][NONE] & attacks;

                    // Bonus for rook when on an open or semi-open file
                    if (pos.semiopenFileOn(Own, s)) {
                        score += RookOnSemiopenFile;
                        if (pos.semiopenFileOn(Opp, s)) {
                            score += RookOnFullopenFile;
                        }
                    } else {
                        // If our pawn on this file is blocked, increase penalty
                        if ((pos.pieces(Own, PAWN)
                           & fileBB(s)
                           & pawnSglPushBB<Opp>(pos.pieces())) != 0) {
                            score -= RookOnClosedFile;
                        }

                        if (mob <= 3) {
                            auto const kf{ sFile(pos.square(Own|KING)) };
                            // Penalty for rook when trapped by the king, even more if the king can't castle
                            if ((kf < FILE_E) == (sFile(s) < kf)) {
                                score -= RookTrapped;
                                if (!pos.canCastle(Own)
                                 && (pos.pieces(Own, PAWN) & frontSquaresBB(Own, s)) != 0) {
                                    score -= RookTrapped;
                                }
                            }
                        }
                    }
                } else
                if (PT == QUEN) {

                    b =  pos.pieces(Own)
                      & ~pos.kingBlockers(Own);
                    Bitboard xray{ attacksBB<BSHP>(s, pos.pieces() ^ (pos.pieces(BSHP) & b & attacksBB(BSHP, s)))
                                 | attacksBB<ROOK>(s, pos.pieces() ^ (pos.pieces(ROOK) & b & attacksBB(ROOK, s))) };
                    if (pos.isKingBlockersOn(Own, s)) {
                        xray &= lineBB(pos.square(Own|KING), s);
                    }
                    attackedBy2[Own] |= attackedBy[Own][NONE]
                                      & (attacks | xray);

                    queenAttacked[Own][0] |= attacksBB(NIHT, s);
                    queenAttacked[Own][1] |= attacksBB<BSHP>(s, pos.pieces());
                    queenAttacked[Own][2] |= attacksBB<ROOK>(s, pos.pieces());

                    // Penalty for pin or discover attack on the queen
                    // Queen attackers
                    b =  pos.pieces(Opp, BSHP, ROOK)
                      & ~pos.kingBlockers(Opp);
                    if ((pos.sliderBlockersOn(s, b, b, b)
                      & ~(  pos.kingBlockers(Opp)
                        | ( pos.pieces(Opp, PAWN)
                         &  fileBB(s)
                         & ~pawnSglAttackBB<Own>(pos.pieces(Own))))) != 0) {
                        score -= QueenAttacked;
                    }
                }

                attackedBy[Own][PT]   |= attacks;
                attackedBy[Own][NONE] |= attacks;
                if (canCastle[Opp]) {
                attackedFull[Own]       |= attacksBB<PT>(s, pos.pieces());
                }
            }

            if constexpr (Trace) {
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
            int32_t kingDanger{ 0 };

            // Attacked squares defended at most once by friend queen or king
            Bitboard const weakArea{
                 attackedBy[Opp][NONE]
              & ~attackedBy2[Own]
              & (~attackedBy[Own][NONE]
               |  attackedBy[Own][QUEN]
               |  attackedBy[Own][KING]) };

            // Safe squares where enemy's safe checks are possible on next move
            Bitboard const safeArea{
                 ~pos.pieces(Opp)
              & (~attackedBy[Own][NONE]
               | (weakArea & attackedBy2[Opp])) };

            Bitboard unsafeCheck{ 0 };

            Bitboard const mocc{ pos.pieces() ^ pos.pieces(Own, QUEN) };
            Bitboard const rookPins{ attacksBB<ROOK>(kSq, mocc) };
            Bitboard const bshpPins{ attacksBB<BSHP>(kSq, mocc) };

            // Enemy rooks checks
            Bitboard const rookSafeChecks{
                rookPins
              & attackedBy[Opp][ROOK]
              & safeArea };

            if (rookSafeChecks != 0) {
                kingDanger += SafeCheckWeight[ROOK][moreThanOne(rookSafeChecks)];
            } else {
                unsafeCheck |= rookPins
                             & attackedBy[Opp][ROOK];
            }

            // Enemy queens checks
            Bitboard const quenSafeChecks{
                (rookPins | bshpPins)
              & attackedBy[Opp][QUEN]
              & safeArea
              & ~attackedBy[Own][QUEN]
              & ~rookSafeChecks };

            if (quenSafeChecks != 0) {
                kingDanger += SafeCheckWeight[QUEN][moreThanOne(quenSafeChecks)];
            }

            // Enemy bishops checks
            Bitboard const bshpSafeChecks{
                bshpPins
              & attackedBy[Opp][BSHP]
              & safeArea
              & ~quenSafeChecks };

            if (bshpSafeChecks != 0) {
                kingDanger += SafeCheckWeight[BSHP][moreThanOne(bshpSafeChecks)];
            } else {
                unsafeCheck |= bshpPins
                             & attackedBy[Opp][BSHP];
            }

            // Enemy knights checks
            Bitboard const nihtSafeChecks{
                attacksBB(NIHT, kSq)
              & attackedBy[Opp][NIHT]
              & safeArea };

            if (nihtSafeChecks != 0) {
                kingDanger += SafeCheckWeight[NIHT][moreThanOne(nihtSafeChecks)];
            } else {
                unsafeCheck |= attacksBB(NIHT, kSq)
                             & attackedBy[Opp][NIHT];
            }

            Bitboard b;

            b =  KingFlankBB[sFile(kSq)]
              &  CampBB[Own]
              &  attackedBy[Opp][NONE];
            // Friend king flank attack count
            int32_t const kingFlankAttack{
                popCount(b)                         // Squares attacked by enemy in friend king flank
              + popCount(b & attackedBy2[Opp]) };   // Squares attacked by enemy twice in friend king flank.
            // Friend king flank defense count
            b =  KingFlankBB[sFile(kSq)]
              &  CampBB[Own]
              &  attackedBy[Own][NONE];
            int32_t const kingFlankDefense{
                popCount(b) };                      // Squares attacked by friend in friend king flank

            // King Safety:
            Score score{ kingEntry->evaluateSafety<Own>(pos, attackedFull[Opp] /*& rankBB(relativeRank(Own, RANK_1))*/) };

            kingDanger +=   1 * kingAttackersCount[Opp] * kingAttackersWeight[Opp]  // (~10.0 Elo)
                        + 183 * popCount(kingRing[Own] & weakArea)                  // (~15.0 Elo)
                        + 148 * popCount(unsafeCheck)                               // (~ 4.0 Elo)
                        +  98 * popCount(pos.kingBlockers(Own))                     // (~ 2.0 Elo)
                        +  69 * kingAttacksCount[Opp]                               // (~ 0.5 Elo)
                        +   3 * (kingFlankAttack*kingFlankAttack) / 8               // (~ 0.5 Elo)
                        // Enemy queen is gone
                        - 873 * (pos.pieces(Opp, QUEN) == 0)                        // (~24.0 Elo)
                        // Friend knight is near by to defend king
                        - 100 * (( attackedBy[Own][NIHT]
                                & (attackedBy[Own][KING] | kSq)) != 0)              // (~ 5.0 Elo)
                        // Mobility
                        -   1 * (mgValue(mobility[Own] - mobility[Opp]))            // (~ 0.5 Elo)
                        -   4 * kingFlankDefense                                    // (~ 5.0 Elo)
                        // Pawn Safety quality
                        -   3 * mgValue(score) / 4                                  // (~ 8.0 Elo)
                        +  37;                                                      // (~ 0.5 Elo)

            // transform the king danger into a score
            if (kingDanger > 100) {
                score -= makeScore((kingDanger*kingDanger) / 0x1000, kingDanger / 0x10);
            }

            // Penalty for king on a pawn less flank
            if ((pos.pieces(PAWN) & KingFlankBB[sFile(kSq)]) == 0) {
                score -= PawnLessFlank;
            }

            // King tropism: Penalty for slow motion attacks moving towards friend king zone
            score -= KingFlankAttacks * kingFlankAttack;

            if constexpr (Trace) {
                Tracer::write(Term(KING), Own, score);
            }

            return score;
        }

        /// threats() evaluates the threats of the color
        template<bool Trace> template<Color Own>
        Score Evaluation<Trace>::threats() const {
            constexpr auto Opp{ ~Own };

            Score score{ SCORE_ZERO };

            // Squares defended by the opponent,
            // - defended the square with a pawn
            // - defended the square twice and not attacked twice.
            Bitboard const defendedArea{
                attackedBy[Opp][PAWN]
              | ( attackedBy2[Opp]
               & ~attackedBy2[Own]) };
            // Enemy non-pawns
            Bitboard const nonPawnsEnemies{
                pos.pieces(Opp)
             & ~pos.pieces(PAWN) };
            // Enemy not defended and under attacked by any friend piece
            Bitboard const attackedUndefendedEnemies{
                pos.pieces(Opp)
             & ~defendedArea
             &  attackedBy[Own][NONE] };
            // Non-pawn enemies, defended by enemies
            Bitboard const defendedNonPawnsEnemies{
                nonPawnsEnemies
             &  defendedArea };

            Bitboard b;

            if ((attackedUndefendedEnemies
               | defendedNonPawnsEnemies) != 0) {
                // Bonus according to the type of attacking pieces

                // Enemies attacked by minors
                b =  (attackedUndefendedEnemies
                    | defendedNonPawnsEnemies)
                  &  (attackedBy[Own][NIHT]
                    | attackedBy[Own][BSHP]);
                while (b != 0) {
                    score += MinorThreat[pType(pos.pieceOn(popLSq(b)))];
                }

                if (attackedUndefendedEnemies != 0) {
                    // Enemies attacked by majors
                    b =  attackedUndefendedEnemies
                      &  attackedBy[Own][ROOK];
                    while (b != 0) {
                        score += MajorThreat[pType(pos.pieceOn(popLSq(b)))];
                    }

                    // Enemies attacked by king
                    b =  attackedUndefendedEnemies
                      &  attackedBy[Own][KING];
                    if (b != 0) {
                        score += KingThreat;
                    }

                    // Enemies attacked are hanging
                    b =  attackedUndefendedEnemies
                      &  (~attackedBy[Opp][NONE]
                        | (nonPawnsEnemies
                         & attackedBy2[Own]));
                    score += PieceHanged * popCount(b);

                    // Additional bonus if weak piece is only protected by a queen
                    b =  attackedUndefendedEnemies
                      &  attackedBy[Opp][QUEN];
                    score += QueenProtection * popCount(b);
                }
            }

            // Bonus for restricting their piece moves
            b = ~defendedArea
              &  attackedBy[Opp][NONE]
              &  attackedBy[Own][NONE];
            score += PieceRestricted * popCount(b);

            // Defended or Unattacked squares
            Bitboard safeArea{
                ~attackedBy[Opp][NONE]
              |  attackedBy[Own][NONE] };

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
            b |= pawnSglPushBB<Own>(b & rankBB(relativeRank(Own, RANK_3)))
              & ~pos.pieces();
            // Friend pawns push safe (only the squares which are relatively safe)
            b &= safeArea
              & ~attackedBy[Opp][PAWN];
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
                  & ( attackedBy[Own][NIHT] & queenAttacked[Opp][0]);
                score += KnightOnQueen * popCount(b) * (1 + queenImbalance);

                b = safeArea
                  & ((attackedBy[Own][BSHP] & queenAttacked[Opp][1])
                   | (attackedBy[Own][ROOK] & queenAttacked[Opp][2]))
                  & attackedBy2[Own];
                score += SliderOnQueen * popCount(b) * (1 + queenImbalance);
            }

            if constexpr (Trace) {
                Tracer::write(THREAT, Own, score);
            }

            return score;
        }

        /// passers() evaluates the passed pawns of the color
        template<bool Trace> template<Color Own>
        Score Evaluation<Trace>::passers() const {
            constexpr auto Opp{ ~Own };

            auto const kingProximity{
                [&](Color c, Square s) {
                    return std::min(distance(pos.square(c|KING), s), 5);
                }
            };

            Score score{ SCORE_ZERO };

            Bitboard pass{ pawnEntry->passeds[Own] };
            Bitboard const blockedPass{
                pass
              & pawnSglPushBB<Opp>(pos.pieces(Opp, PAWN)) };
            if (blockedPass != 0) {
                // Can we lever the blocker of a blocked passer?
                Bitboard const helpers{
                    pawnSglPushBB<Own>(pos.pieces(Own, PAWN))
                  & ~pos.pieces(Opp)
                  & (~attackedBy2[Opp] | attackedBy[Own][NONE]) };
                // Remove blocked otherwise
                pass &= ~blockedPass
                      | shift<WEST>(helpers)
                      | shift<EAST>(helpers);
            }
            while (pass != 0) {
                auto const s{ popLSq(pass) };
                assert((pos.pieces(Opp, PAWN) & frontSquaresBB(Own, s + PawnPush[Own])) == 0);

                int32_t const r{ relativeRank(Own, s) };

                Score bonus{ PasserRank[r] - PasserFile * edgeDistance(sFile(s)) };

                auto const pushSq{ s + PawnPush[Own] };
                if (r > RANK_3) {
                    int32_t const w{ 5 * r - 13 };

                    // Adjust bonus based on the king's proximity
                    bonus += makeScore(0,  kingProximity(Opp, pushSq) * w * 19 / 4
                                         + kingProximity(Own, pushSq) * w * -2);
                    // If pushSq is not the queening square then consider also a second push.
                    if (r < RANK_7) {
                        bonus += makeScore(0, kingProximity(Own, pushSq + PawnPush[Own]) * w * -1);
                    }

                    // If the pawn is free to advance.
                    if (pos.emptyOn(pushSq)) {
                        Bitboard const behindMajors{ frontSquaresBB(Opp, s) & pos.pieces(ROOK, QUEN) };

                        Bitboard attackedSquares{
                              pawnPassSpan(Own, s)
                            & (attackedBy[Opp][NONE] | pos.pieces(Opp)) };
                        if ((behindMajors & pos.pieces(Opp)) != 0) {
                            attackedSquares |= frontSquaresBB(Own, s);
                        }

                        // If there are no enemy pieces or attacks on passed pawn span, assign a big bonus.
                        // Or if there is some, but they are all attacked by our pawns, assign a bit smaller bonus.
                        // Otherwise assign a smaller bonus if the path to queen is not attacked
                        // and even smaller bonus if it is attacked but block square is not.
                        int32_t k{
                              (attackedSquares) == 0                          ? 36 :
                              (attackedSquares & ~attackedBy[Own][PAWN]) == 0 ? 30 :
                              (attackedSquares & frontSquaresBB(Own, s)) == 0 ? 17 :
                              !contains(attackedSquares, pushSq)              ?  7 : 0 };

                        // Larger bonus if the block square is defended
                        if ((behindMajors & pos.pieces(Own)) != 0
                         || contains(attackedBy[Own][NONE], pushSq)) {
                            k += 5;
                        }

                        bonus += makeScore(k*w, k*w);
                    }
                }

                score += bonus;
            }

            if constexpr (Trace) {
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
                 SpaceMaskBB[Own]
              & ~pos.pieces(Own, PAWN)
              & ~attackedBy[Opp][PAWN] };
            // Find all squares which are at most three squares behind some friend pawn
            Bitboard behind{ pos.pieces(Own, PAWN) };
            behind |= pawnSglPushBB<Opp>(behind);
            behind |= pawnDblPushBB<Opp>(behind);

            int32_t const bonus{
                popCount(safeSpace)
              + popCount(  behind
                        &  safeSpace
                        & ~attackedBy[Opp][NONE]) };
            int32_t const weight{
                pos.count(Own)
              + std::min(pawnEntry->blockedCount(), 9)
              - 3 };

            Score const score{ makeScore(bonus * weight * weight / 16, 0) };

            if constexpr (Trace) {
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
            // If have a specialized evaluating function for the material configuration
            if (matlEntry->evalExists()) {
                return matlEntry->evaluateFunc(pos);
            }

            // Probe the pawn hash table
            pawnEntry = Pawns::probe(pos);
            // Probe the king hash table
            kingEntry = King::probe(pos, pawnEntry);

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
                goto makeValue;
            }

            if constexpr (Trace) {
                Tracer::clear();
            }

            initialize<WHITE>(), initialize<BLACK>();

            // Pieces should be evaluated first (also populate attack information)
            // Note that the order of evaluation of the terms is left unspecified
            score += pieces<WHITE, NIHT>() - pieces<BLACK, NIHT>()
                   + pieces<WHITE, BSHP>() - pieces<BLACK, BSHP>()
                   + pieces<WHITE, ROOK>() - pieces<BLACK, ROOK>();
            score += pieces<WHITE, QUEN>() - pieces<BLACK, QUEN>();
            score += mobility[WHITE] - mobility[BLACK];
            // More complex interactions that require fully populated attack information
            score += king    <WHITE>() - king    <BLACK>()
                   + passers <WHITE>() - passers <BLACK>();

            if (lazySkip(LazyThreshold2)) {
                goto makeValue;
            }

            score += threats <WHITE>() - threats <BLACK>();
            // Skip if, for example, both queens or 6 minor pieces have been exchanged
            if (pos.nonPawnMaterial() >= SpaceThreshold) {
            score += space   <WHITE>() - space   <BLACK>();
            }

        makeValue:
            // Winnable:
            // Derive single value from mg and eg parts of score

            // Compute the initiative bonus for the attacking side
            int32_t const complexity{
                 1 * pawnEntry->complexity
              +  9 * kingEntry->outflanking
                // King infiltration
              + 24 * kingEntry->infiltration
              + 51 * (pos.nonPawnMaterial() == VALUE_ZERO)
                // Pawn on both flanks
              + 21 * (pawnEntry->pawnsOnBothFlank)
                // Almost Unwinnable
              - 43 * (kingEntry->outflanking < 0
                  && !pawnEntry->pawnsOnBothFlank)
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

            int32_t scale{ matlEntry->scaleFunc(pos, strongSide) };
            // If scale factor is not already specific, scale down via general heuristics
            if (scale == SCALE_NORMAL) {

                if (pos.bishopOpposed()) {
                    scale = pos.nonPawnMaterial() == 2 * VALUE_MG_BSHP ?
                                18 + 4 * popCount(pawnEntry->passeds[strongSide]) :
                                22 + 3 * pos.count(strongSide);
                } else
                if (pos.nonPawnMaterial(WHITE) == VALUE_MG_ROOK
                 && pos.nonPawnMaterial(BLACK) == VALUE_MG_ROOK
                 && pos.count( strongSide|PAWN)
                  - pos.count(~strongSide|PAWN) <= 1
                 && bool(pos.pieces(strongSide, PAWN) & slotFileBB(CS_KING))
                 != bool(pos.pieces(strongSide, PAWN) & slotFileBB(CS_QUEN))
                 && (attackedBy[~strongSide][KING] & pos.pieces(~strongSide, PAWN)) != 0) {
                    scale = 36;
                } else
                if (pos.count(QUEN) == 1) {
                    // Opposite queen color
                    auto const queenColor{ ~pColor(pos.pieceOn(scanLSq(pos.pieces(QUEN)))) };
                    scale = 37 + 3 * (pos.count(queenColor|NIHT) + pos.count(queenColor|BSHP));
                } else {
                    scale = std::min(36 + 7 * pos.count(strongSide|PAWN), scale) - 4 * !pawnEntry->pawnsOnBothFlank;
                }

                scale -= 4 * !pawnEntry->pawnsOnBothFlank;
            }

            Value v;
            // Interpolates between midgame and scaled endgame values (scaled by 'scaleFactor(egValue(score))').
            v = mg * (matlEntry->phase)
              + eg * (Material::PhaseResolution - matlEntry->phase) * Scale(scale) / SCALE_NORMAL;
            v /= Material::PhaseResolution;
            assert(-VALUE_INFINITE < v && v < +VALUE_INFINITE);

            // Evaluation grain
            v = (v / 16) * 16;
            // Active side's point of view
            v = (pos.activeSide() == WHITE ? +v : -v) + VALUE_TEMPO;

            // Write remaining evaluation terms
            if constexpr (Trace) {
                Tracer::write(Term(PAWN), pawnEntry->score[WHITE], pawnEntry->score[BLACK]);
                Tracer::write(MATERIAL  , pos.psqScore());
                Tracer::write(IMBALANCE , matlEntry->imbalance);
                Tracer::write(MOBILITY  , mobility[WHITE], mobility[BLACK]);                
                Tracer::write(SCALING   , makeScore(mv, ev - eg + eg * Scale(scale) / SCALE_NORMAL));
                Tracer::write(TOTAL     , makeScore(mg,         + eg * Scale(scale) / SCALE_NORMAL));
            }

            return v;
        }


        // specifically correct for cornered bishops to fix FRC with NNUE.
        Value fixFRC(Position const &pos) {

            constexpr Bitboard Corners = 1ULL << SQ_A1
                                       | 1ULL << SQ_H1
                                       | 1ULL << SQ_A8
                                       | 1ULL << SQ_H8;

            if ((pos.pieces(BSHP) & Corners) == 0) {
                return VALUE_ZERO;
            }

            Value correction = VALUE_ZERO;

            if (pos.pieceOn(SQ_A1) == W_BSHP
             && pos.pieceOn(SQ_B2) == W_PAWN) {
                correction -= pos.emptyOn(SQ_B3) ?
                                3 * BishopTrapped :
                                4 * BishopTrapped;
            }
            if (pos.pieceOn(SQ_H1) == W_BSHP
             && pos.pieceOn(SQ_G2) == W_PAWN) {
                correction -= pos.emptyOn(SQ_G3) ?
                                3 * BishopTrapped :
                                4 * BishopTrapped;
            }
            if (pos.pieceOn(SQ_A8) == B_BSHP
             && pos.pieceOn(SQ_B7) == B_PAWN) {
                correction -= pos.emptyOn(SQ_B6) ?
                                3 * BishopTrapped :
                                4 * BishopTrapped;
            }
            if (pos.pieceOn(SQ_H8) == B_BSHP
             && pos.pieceOn(SQ_G7) == B_PAWN) {
                correction -= pos.emptyOn(SQ_G6) ?
                                3 * BishopTrapped :
                                4 * BishopTrapped;
            }

            return pos.activeSide() == WHITE ?
                    +correction : -correction;
        }

    }

    /// evaluate() returns a static evaluation of the position from the point of view of the side to move.
    Value evaluate(Position const &pos) {
        assert(pos.checkers() == 0);

        Value v;

        if (useNNUE) {

            auto const npm{ pos.nonPawnMaterial() };
            // Scale and shift NNUE for compatibility with search and classical evaluation
            auto const nnueAdjEvaluate = [&]() {
                int32_t const mat{ npm + 4 * VALUE_MG_PAWN * pos.count(PAWN) };
                int32_t const scale{ 580 + mat / 32 - 4 * pos.clockPly() };
                Value nnueValue = NNUE::evaluate(pos) * scale / 1024 + VALUE_TEMPO;

                if (Options["UCI_Chess960"]) {
                    nnueValue += fixFRC(pos);
                }
                return nnueValue;
            };

            // If there is PSQ imbalance use classical eval, with small probability if it is small
            Value   const psq{ Value(std::abs(egValue(pos.psqScore()))) };
            int32_t const r50{ 16 + pos.clockPly() };
            bool    const psqLarge{ psq * 16 > (NNUEThreshold1 + npm / 64) * r50 };
            bool    const lowEndgames{
                npm < 2 * VALUE_MG_ROOK
             && pos.count(PAWN) < 2 };

            bool    const classical{
                psqLarge
             || lowEndgames
             || (psq > VALUE_MG_PAWN / 4
              && (pos.thread()->nodes & 0xB) == 0) };

            if (classical) {
                v = Evaluation<false>(pos).value();

                // If the classical eval is small and imbalance large, use NNUE nevertheless.
                // For the case of opposite colored bishops, switch to NNUE eval with
                // small probability if the classical eval is less than the threshold.
                if ( psqLarge
                 && !lowEndgames
                 && (std::abs(v) * 16 < (NNUEThreshold2) * r50
                  || (pos.bishopOpposed()
                   && std::abs(v) * 16 < (NNUEThreshold1 + npm / 64) * r50
                   && (pos.thread()->nodes & 0xB) == 0))) {
                    v = nnueAdjEvaluate();
                }
            } else {
                v = nnueAdjEvaluate();
            }
        } else {
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

        std::ostringstream oss;
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
