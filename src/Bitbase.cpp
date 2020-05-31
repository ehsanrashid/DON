#include "Bitbase.h"

#include <cassert>
#include <bitset>

#include "BitBoard.h"

namespace BitBase {

    namespace {

        // There are 24 possible pawn squares: files A to D and ranks from 2 to 7
        // Positions with the pawn on files E to H will be mirrored before probing.
        constexpr u32 BaseSize{ 24 * 2 * 64 * 64 }; // wpSq * active * wkSq * bkSq

        std::bitset<BaseSize> KPKBitBase;

        // A KPK bitbase index is an integer in [0, BaseSize] range
        //
        // Information is mapped in a way that minimizes the number of iterations:
        //
        // bit 00-05: white king square (from SQ_A1 to SQ_H8)
        // bit 06-11: black king square (from SQ_A1 to SQ_H8)
        // bit    12: active (WHITE or BLACK)
        // bit 13-14: white pawn file [(from FILE_A to FILE_D) - FILE_A]
        // bit 15-17: white pawn rank [(from RANK_2 to RANK_7) - RANK_2]
        u32 index(Color active, Square wkSq, Square bkSq, Square wpSq) {
            assert(FILE_A <= sFile(wpSq) && sFile(wpSq) <= FILE_D);
            assert(RANK_2 <= sRank(wpSq) && sRank(wpSq) <= RANK_7);

            return (wkSq << 0)
                 + (bkSq << 6)
                 + (active << 12)
                 + ((sFile(wpSq) - FILE_A) << 13)
                 + ((sRank(wpSq) - RANK_2) << 15);
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
            Square wkSq;
            Square bkSq;
            Square wpSq;

            Result result;

            KPKPosition() = default;
            KPKPosition(u32);

            operator Result() const { return result; }

            Result classify(KPKPosition kpkArrBase[]);
        };

        KPKPosition::KPKPosition(u32 idx) {
            assert(idx < BaseSize);
            wkSq   = Square((idx >>  0) & 63);
            bkSq   = Square((idx >>  6) & 63);
            active =  Color((idx >> 12) & 1);
            wpSq   = makeSquare(File(((idx >> 13) & 3) + FILE_A),
                                Rank(((idx >> 15) & 7) + RANK_2));

            assert(isOk(wkSq)
                && isOk(bkSq)
                && isOk(active)
                && isOk(wpSq)
                && index(active, wkSq, bkSq, wpSq) == idx);

            // Check if two pieces are on the same square or if a king can be captured
            if (distance(wkSq, bkSq) <= 1
             || wkSq == wpSq
             || bkSq == wpSq
             || (active == WHITE
              && contains(pawnAttacksBB(WHITE, wpSq), bkSq))) {
                result = INVALID;
            }
            else
            // Immediate win if a pawn can be promoted without getting captured
            if (active == WHITE
             && sRank(wpSq) == RANK_7
             && wkSq != wpSq + NORTH
             && bkSq != wpSq + NORTH
             && (distance(bkSq, wpSq + NORTH) >= 2
              || distance(wkSq, wpSq + NORTH) <= 1)) {
                result = WIN;
            }
            else
            // Immediate draw if king captures undefended pawn or is a stalemate
            if (active == BLACK
             && ((distance(bkSq, wpSq) <= 1
               && distance(wkSq, wpSq) >= 2)
              || (  attacksBB<KING>(bkSq)
                & ~(attacksBB<KING>(wkSq)|pawnAttacksBB(WHITE, wpSq))) == 0)) {
                result = DRAW;
            }
            // Position will be classified later
            else {
                result = UNKNOWN;
            }
        }

        Result KPKPosition::classify(KPKPosition kpkArrBase[]) {
            // White to Move:
            // If one move leads to a position classified as WIN, the result of the current position is WIN.
            // If all moves lead to positions classified as DRAW, the result of the current position is DRAW
            // otherwise the current position is classified as UNKNOWN.
            //
            // Black to Move:
            // If one move leads to a position classified as DRAW, the result of the current position is DRAW.
            // If all moves lead to positions classified as WIN, the result of the current position is WIN
            // otherwise the current position is classified as UNKNOWN.

            Result const Good{ active == WHITE ? WIN : DRAW };
            Result const  Bad{ active == WHITE ? DRAW : WIN };

            Result r{ INVALID };

            if (active == WHITE) {
                Bitboard b{  attacksBB<KING>(wkSq)
                          & ~attacksBB<KING>(bkSq) };
                while (b != 0) {
                    r |= kpkArrBase[index(BLACK, popLSq(b), bkSq, wpSq)];
                }

                // Pawn Single push
                if (sRank(wpSq) <= RANK_6) {
                    r |= kpkArrBase[index(BLACK, wkSq, bkSq, wpSq + NORTH)];

                    // Pawn Double push
                    if (sRank(wpSq) == RANK_2
                     && wkSq != wpSq + NORTH    // Front is not own king
                     && bkSq != wpSq + NORTH) { // Front is not opp king
                        r |= kpkArrBase[index(BLACK, wkSq, bkSq, wpSq + NORTH + NORTH)];
                    }
                }
            }
            else { // if (active == BLACK)
                Bitboard b{  attacksBB<KING>(bkSq)
                          & ~attacksBB<KING>(wkSq) };
                while (b != 0) {
                    r |= kpkArrBase[index(WHITE, wkSq, popLSq(b), wpSq)];
                }
            }

            result = r & Good    ? Good :
                     r & UNKNOWN ? UNKNOWN : Bad;
            return result;
        }
    }

    void initialize() {

        KPKPosition kpkArrBase[BaseSize];
        // Initialize kpkArrBase with known WIN/DRAW positions
        for (u32 idx = 0; idx < BaseSize; ++idx) {
            kpkArrBase[idx] = { idx };
        }
        // Iterate through the positions until none of the unknown positions
        // can be changed to either WIN/DRAW (15 cycles needed).
        bool repeat{ true };
        while (repeat) {
            repeat = false;
            for (u32 idx = 0; idx < BaseSize; ++idx) {
                repeat |= kpkArrBase[idx] == UNKNOWN
                       && kpkArrBase[idx].classify(kpkArrBase) != UNKNOWN;
            }
        }
        // Fill the Bitbase from Arraybase
        for (u32 idx = 0; idx < BaseSize; ++idx) {
            if (kpkArrBase[idx] == WIN) {
                KPKBitBase.set(idx);
            }
        }

        assert(KPKBitBase.count() == 111282);
    }

    bool probe(bool stngActive, Square skSq, Square wkSq, Square spSq) {
        // skSq = White King
        // wkSq = Black King
        // spSq = White Pawn
        return KPKBitBase[index(stngActive ? WHITE : BLACK, skSq, wkSq, spSq)];
    }

}
