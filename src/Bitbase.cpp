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
                 | (bkSq << 6)
                 | (active << 12)
                 | ((sFile(wpSq) - FILE_A) << 13)
                 | ((sRank(wpSq) - RANK_2) << 15);
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

            Result classify(Array<KPKPosition, BaseSize> const&);
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
            if (2 > distance(wkSq, bkSq)
             || wkSq == wpSq
             || bkSq == wpSq
             || (WHITE == active
              && contains(PawnAttackBB[WHITE][wpSq], bkSq))) {
                result = INVALID;
            }
            else
            // Immediate win if a pawn can be promoted without getting captured
            if (WHITE == active
             && RANK_7 == sRank(wpSq)
             && wkSq != wpSq + NORTH
             && bkSq != wpSq + NORTH
             && (1 < distance(bkSq, wpSq + NORTH)
              || 2 > distance(wkSq, wpSq + NORTH))) {
                result = WIN;
            }
            else
            // Immediate draw if king captures undefended pawn or is a stalemate
            if (BLACK == active
             && ((2 > distance(bkSq, wpSq)
               && 1 < distance(wkSq, wpSq))
              || 0 == (   PieceAttackBB[KING][bkSq]
                      & ~(PieceAttackBB[KING][wkSq]|PawnAttackBB[WHITE][wpSq])))) {
                result = DRAW;
            }
            // Position will be classified later
            else {
                result = UNKNOWN;
            }
        }

        Result KPKPosition::classify(Array<KPKPosition, BaseSize> const &kpkArrBase) {
            // White to Move:
            // If one move leads to a position classified as WIN, the result of the current position is WIN.
            // If all moves lead to positions classified as DRAW, the result of the current position is DRAW
            // otherwise the current position is classified as UNKNOWN.
            //
            // Black to Move:
            // If one move leads to a position classified as DRAW, the result of the current position is DRAW.
            // If all moves lead to positions classified as WIN, the result of the current position is WIN
            // otherwise the current position is classified as UNKNOWN.

            Result const Good{ WHITE == active ? WIN : DRAW };
            Result const  Bad{ WHITE == active ? DRAW : WIN };

            Result r{ INVALID };

            switch (active) {

            case WHITE: {
                Bitboard b{  PieceAttackBB[KING][wkSq]
                          & ~PieceAttackBB[KING][bkSq] };
                while (0 != b) {
                    r |= kpkArrBase[index(BLACK, popLSq(b), bkSq, wpSq)];
                }

                // Pawn Single push
                if (RANK_6 >= sRank(wpSq)) {
                    r |= kpkArrBase[index(BLACK, wkSq, bkSq, wpSq + NORTH)];

                    // Pawn Double push
                    if (RANK_2 == sRank(wpSq)
                     && wkSq != wpSq + NORTH    // Front is not own king
                     && bkSq != wpSq + NORTH) { // Front is not opp king
                        r |= kpkArrBase[index(BLACK, wkSq, bkSq, wpSq + NORTH + NORTH)];
                    }
                }
            }
                break;
            case BLACK: {
                Bitboard b{  PieceAttackBB[KING][bkSq]
                          & ~PieceAttackBB[KING][wkSq] };
                while (0 != b) {
                    r |= kpkArrBase[index(WHITE, wkSq, popLSq(b), wpSq)];
                }
            }
                break;
            default: break;
            }

            result = r & Good ? Good :
                        r & UNKNOWN ? UNKNOWN : Bad;
            return result;
        }
    }

    void initialize() {

        Array<KPKPosition, BaseSize> kpkArrBase;
        // Initialize kpkArrBase with known WIN/DRAW positions
        for (u32 idx = 0; idx < kpkArrBase.size(); ++idx) {
            kpkArrBase[idx] = { idx };
        }
        // Iterate through the positions until none of the unknown positions
        // can be changed to either WIN/DRAW (15 cycles needed).
        bool repeat{ true };
        while (repeat) {
            repeat = false;
            for (u32 idx = 0; idx < kpkArrBase.size(); ++idx) {
                repeat |= UNKNOWN == kpkArrBase[idx]
                       && UNKNOWN != kpkArrBase[idx].classify(kpkArrBase);
            }
        }

        // Fill the Bitbase from Arraybase
        for (u32 idx = 0; idx < kpkArrBase.size(); ++idx) {
            if (WIN == kpkArrBase[idx]) {
                KPKBitBase.set(idx);
            }
        }

        assert(111282 == KPKBitBase.count());
    }

    bool probe(bool stngActive, Square skSq, Square wkSq, Square spSq) {
        // skSq = White King
        // wkSq = Black King
        // spSq = White Pawn
        return KPKBitBase[index(stngActive ? WHITE : BLACK, skSq, wkSq, spSq)];
    }

}
