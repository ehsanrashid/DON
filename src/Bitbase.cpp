#include "Bitbase.h"

#include <cassert>
#include <bitset>

#include "BitBoard.h"

namespace BitBase {

    namespace {

        using std::bitset;

        // There are 24 possible pawn squares: files A to D and ranks from 2 to 7
        // Positions with the pawn on files E to H will be mirrored before probing.
        constexpr u32 MaxIndex{ 24 * 2 * 64 * 64 }; // pSq * active * wkSq * bkSq

        bitset<MaxIndex> KPKBitbase;

        // A KPK bitbase index is an integer in [0, MaxIndex] range
        //
        // Information is mapped in a way that minimizes the number of iterations:
        //
        // bit 00-05: white king square (from SQ_A1 to SQ_H8)
        // bit 06-11: black king square (from SQ_A1 to SQ_H8)
        // bit    12: active (WHITE or BLACK)
        // bit 13-14: white pawn file [(from FILE_A to FILE_D) - FILE_A]
        // bit 15-17: white pawn rank [(from RANK_2 to RANK_7) - RANK_2]
        u32 index(Color active, Square wkSq, Square bkSq, Square wpSq) {
            assert(FILE_A <= SFile[wpSq] && SFile[wpSq] <= FILE_D);
            assert(RANK_2 <= SRank[wpSq] && SRank[wpSq] <= RANK_7);

            return (wkSq << 0)
                 | (bkSq << 6)
                 | (active << 12)
                 | ((SFile[wpSq] - FILE_A) << 13)
                 | ((SRank[wpSq] - RANK_2) << 15);
        }

        enum Result : u08 {
            INVALID = 0,
            UNKNOWN = 1 << 0,
            DRAW    = 1 << 1,
            WIN     = 1 << 2,
            LOSE    = 1 << 3,
        };

        Result& operator|=(Result &r1, Result r2) { return r1 = Result(r1 | r2); }
        //Result& operator&=(Result &r1, Result r2) { return r1 = Result(r1 & r2); }

        /// KPKPosition
        struct KPKPosition {

            Color  active;
            Square pSq;
            Array<Square, COLORS> kSq;

            Result result;

            KPKPosition() = default;
            explicit KPKPosition(u32);

            operator Result() const {
                return result;
            }

            Result classify(Array<KPKPosition, MaxIndex> const&);
        };

        KPKPosition::KPKPosition(u32 idx) {
            kSq[WHITE] = Square((idx >>  0) & i32(SQ_H8));
            kSq[BLACK] = Square((idx >>  6) & i32(SQ_H8));
            active     =  Color((idx >> 12) & BLACK);
            pSq        = makeSquare(File(((idx >> 13) & 0x03) + FILE_A),
                                    Rank(((idx >> 15) & 0x07) + RANK_2));

            assert(isOk(kSq[WHITE])
                && isOk(kSq[BLACK])
                && isOk(active)
                && isOk(pSq)
                && index(active, kSq[WHITE], kSq[BLACK], pSq) == idx);

            // Check if two pieces are on the same square or if a king can be captured
            if (1 >= distance(kSq[WHITE], kSq[BLACK])
             || kSq[WHITE] == pSq
             || kSq[BLACK] == pSq
             || (WHITE == active
              && contains(PawnAttackBB[WHITE][pSq], kSq[BLACK]))) {
                result = INVALID;
            }
            else
            // Immediate win if a pawn can be promoted without getting captured
            if (WHITE == active
             && RANK_7 == SRank[pSq]
             && kSq[WHITE] != pSq + NORTH
             && (1 < distance(kSq[BLACK], pSq + NORTH)
              || contains(PieceAttackBB[KING][kSq[WHITE]], pSq + NORTH))) {
                result = WIN;
            }
            else
            // Immediate draw if is a stalemate or king captures undefended pawn
            if (BLACK == active
             && (0 == (  PieceAttackBB[KING][kSq[BLACK]]
                     & ~(PieceAttackBB[KING][kSq[WHITE]]
                       | PawnAttackBB[WHITE][pSq]))
              || contains( PieceAttackBB[KING][kSq[BLACK]]
                        & ~PieceAttackBB[KING][kSq[WHITE]], pSq))) {
                result = DRAW;
            }
            // Position will be classified later
            else {
                result = UNKNOWN;
            }
        }

        Result KPKPosition::classify(Array<KPKPosition, MaxIndex> const &kpkDB) {
            // White to Move:
            // If one move leads to a position classified as WIN, the result of the current position is WIN.
            // If all moves lead to positions classified as DRAW, the result of the current position is DRAW
            // otherwise the current position is classified as UNKNOWN.
            //
            // Black to Move:
            // If one move leads to a position classified as DRAW, the result of the current position is DRAW.
            // If all moves lead to positions classified as WIN, the result of the current position is WIN
            // otherwise the current position is classified as UNKNOWN.

            auto const Good{ WHITE == active ? WIN : DRAW };
            auto const  Bad{ WHITE == active ? DRAW : WIN };

            auto r{ INVALID };
            Bitboard b{ PieceAttackBB[KING][kSq[active]] };
            while (0 != b) {
                auto ksq{ popLSq(b) };
                r |= WHITE == active ?
                        kpkDB[index(BLACK, ksq, kSq[BLACK], pSq)] :
                        kpkDB[index(WHITE, kSq[WHITE], ksq, pSq)];
            }

            if (WHITE == active) {
                // Single push
                if (RANK_7 > SRank[pSq]) {
                    auto pushSq{ pSq + NORTH };
                    r |= kpkDB[index(BLACK, kSq[WHITE], kSq[BLACK], pushSq)];

                    // Double push
                    if (RANK_2 == SRank[pSq]
                        // Front is not own king
                     && kSq[WHITE] != pushSq
                        // Front is not opp king
                     && kSq[BLACK] != pushSq) {
                        r |= kpkDB[index(BLACK, kSq[WHITE], kSq[BLACK], pushSq + NORTH)];
                    }
                }
            }

            result = r & Good ? Good :
                        r & UNKNOWN ? UNKNOWN : Bad;
            return result;
        }
    }

    void initialize() {
        Array<KPKPosition, MaxIndex> kpkDB;
        // Initialize kpkDB with known win / draw positions
        for (u32 idx = 0; idx < MaxIndex; ++idx) {
            kpkDB[idx] = KPKPosition(idx);
        }
        // Iterate through the positions until none of the unknown positions can be
        // changed to either wins or draws (15 cycles needed).
        bool repeat{ true };
        while (repeat) {
            repeat = false;
            for (u32 idx = 0; idx < MaxIndex; ++idx) {
                repeat |= UNKNOWN == kpkDB[idx]
                       && UNKNOWN != kpkDB[idx].classify(kpkDB);
            }
        }
        // Fill the bitbase with the decisive results
        for (u32 idx = 0; idx < MaxIndex; ++idx) {
            if (WIN == kpkDB[idx]) {
                KPKBitbase.set(idx);
            }
        }
    }

    bool probe(Color active, Square wkSq, Square wpSq, Square bkSq) {
        // wkSq = White King
        // wpSq = White Pawn
        // bkSq = Black King
        return KPKBitbase[index(active, wkSq, bkSq, wpSq)];
    }

}
