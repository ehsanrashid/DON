#include "bitbase.h"

#include <cassert>
#include <bitset>

#include "bitboard.h"

namespace Bitbases {

    namespace {

        // There are 24 possible pawn squares: files A to D and ranks from 2 to 7
        // Positions with the pawn on files E to H will be mirrored before probing.
        constexpr uint32_t BaseSize{ 24 * 2 * 64 * 64 }; // wpSq * active * wkSq * bkSq

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
        uint32_t index(Color active, Square wkSq, Square bkSq, Square wpSq) noexcept {
            assert(FILE_A <= sFile(wpSq) && sFile(wpSq) <= FILE_D);
            assert(RANK_2 <= sRank(wpSq) && sRank(wpSq) <= RANK_7);

            return (wkSq << 0)
                 | (bkSq << 6)
                 | (active << 12)
                 | (((sFile(wpSq) - FILE_A) & 3) << 13)
                 | (((sRank(wpSq) - RANK_2) & 7) << 15);
        }

        enum Result : uint8_t {
            INVALID = 0,
            UNKNOWN = 1 << 0,
            DRAW    = 1 << 1,
            WIN     = 1 << 2,
            LOSE    = 1 << 3,
        };

        Result& operator|=(Result &r1, Result r2) noexcept { return r1 = Result(r1 | r2); }
        //Result& operator&=(Result &r1, Result r2) noexcept { return r1 = Result(r1 & r2); }

        /// KPKPosition
        struct KPKPosition {

            KPKPosition() = default;
            explicit KPKPosition(uint32_t);

            operator Result() const noexcept { return result; }

            Result classify(KPKPosition kpkArr[]) noexcept;

            Color  active;
            Square wkSq;
            Square bkSq;
            Square wpSq;

            Result result;
        };

        KPKPosition::KPKPosition(uint32_t idx) {
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

            // Invalid if two pieces are on the same square or if a king can be captured
            if (distance(wkSq, bkSq) <= 1
             || wkSq == wpSq
             || bkSq == wpSq
             || (active == WHITE
              && contains(pawnAttacksBB(WHITE, wpSq), bkSq))) {
                result = INVALID;
            } else
            // Win if a pawn can be promoted without getting captured
            if (active == WHITE
             && sRank(wpSq) == RANK_7
             && wkSq != wpSq + NORTH
             && bkSq != wpSq + NORTH
             && (distance(bkSq, wpSq + NORTH) >= 2
              || distance(wkSq, wpSq + NORTH) == 1)) {
                result = WIN;
            } else
            // Draw if king captures undefended pawn or is a stalemate
            if (active == BLACK
             && ((distance(bkSq, wpSq) == 1
               && distance(wkSq, wpSq) >= 2)
              || (  attacksBB(KING, bkSq)
                & ~(attacksBB(KING, wkSq)|pawnAttacksBB(WHITE, wpSq))) == 0)) {
                result = DRAW;
            } else {
            // Position will be classified later
                result = UNKNOWN;
            }
        }

        Result KPKPosition::classify(KPKPosition kpkArr[]) noexcept {
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
                Bitboard b{  attacksBB(KING, wkSq)
                          & ~attacksBB(KING, bkSq) };
                while (b != 0) {
                    r |= kpkArr[index(BLACK, popLSq(b), bkSq, wpSq)];
                }

                // Pawn Single push
                if (sRank(wpSq) <= RANK_6) {
                    r |= kpkArr[index(BLACK, wkSq, bkSq, wpSq + NORTH)];

                    // Pawn Double push
                    if (sRank(wpSq) == RANK_2
                     && wkSq != wpSq + NORTH    // Front is not own king
                     && bkSq != wpSq + NORTH) { // Front is not opp king
                        r |= kpkArr[index(BLACK, wkSq, bkSq, wpSq + NORTH + NORTH)];
                    }
                }
            } else {
                // if (active == BLACK)
                Bitboard b{  attacksBB(KING, bkSq)
                          & ~attacksBB(KING, wkSq) };
                while (b != 0) {
                    r |= kpkArr[index(WHITE, wkSq, popLSq(b), wpSq)];
                }
            }

            result = r & Good    ? Good :
                     r & UNKNOWN ? UNKNOWN : Bad;
            return result;
        }
    }

    void initialize() {

        KPKPosition kpkArr[BaseSize];
        // Initialize kpkArr with known WIN/DRAW positions
        for (uint32_t idx = 0; idx < BaseSize; ++idx) {
            kpkArr[idx] = KPKPosition{ idx };
        }
        // Iterate through the positions until none of the unknown positions
        // can be changed to either WIN/DRAW (15 cycles needed).
        bool repeat{ true };
        while (repeat) {
            repeat = false;
            for (uint32_t idx = 0; idx < BaseSize; ++idx) {
                repeat |= kpkArr[idx] == UNKNOWN
                       && kpkArr[idx].classify(kpkArr) != UNKNOWN;
            }
        }
        // Fill the Bitbase from kpkArr
        for (uint32_t idx = 0; idx < BaseSize; ++idx) {
            if (kpkArr[idx] == WIN) {
                KPKBitBase.set(idx);
            }
        }

        assert(KPKBitBase.count() == 111282);
    }

    bool probe(bool stngActive, Square skSq, Square wkSq, Square spSq) noexcept {
        // skSq = White King
        // wkSq = Black King
        // spSq = White Pawn
        return KPKBitBase[index(stngActive ? WHITE : BLACK, skSq, wkSq, spSq)];
    }

}
