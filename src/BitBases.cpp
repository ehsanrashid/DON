#include "BitBases.h"

#include <cassert>
#include <bitset>
#include <vector>

#include "BitBoard.h"

namespace BitBases {

    using namespace std;
    using namespace BitBoard;

    namespace {

        // There are 24 possible pawn squares: files A to D and ranks from 2 to 7
        // Positions with the pawn on files E to H will be mirrored before probing.
        constexpr u32 MaxIndex = 24*2*64*64; // pSq * active * wkSq * bkSq

        bitset<MaxIndex> KPKBitbase;

        // A KPK bitbase index is an integer in [0, MaxIndex] range
        //
        // Information is mapped in a way that minimizes the number of iterations:
        //
        // bit 00-05: white king square (from SQ_A1 to SQ_H8)
        // bit 06-11: black king square (from SQ_A1 to SQ_H8)
        // bit    12: active (WHITE or BLACK)
        // bit 13-14: white pawn file (from F_A to F_D)
        // bit 15-17: white pawn R_7 - rank (from R_2 to R_7)
        u32 index(Color active, Square wkSq, Square bkSq, Square wpSq)
        {
            assert(F_A <= sFile(wpSq) && sFile(wpSq) <= F_D);
            assert(R_2 <= sRank(wpSq) && sRank(wpSq) <= R_7);

            return (wkSq << 0)
                 | (bkSq << 6)
                 | (active << 12)
                 | ((sFile(wpSq)-F_A) << 13)
                 | ((sRank(wpSq)-R_2) << 15);
        }

        enum Result : u08
        {
            INVALID = 0,
            UNKNOWN = 1 << 0,
            DRAW    = 1 << 1,
            WIN     = 1 << 2,
            LOSE    = 1 << 3,
        };

        constexpr Result operator|(Result r1, Result r2) { return Result(i32(r1) | i32(r2)); }
        //constexpr Result operator&(Result r1, Result r2) { return Result(i32(r1) & i32(r2)); }

        Result& operator|=(Result &r1, Result r2) { return r1 = r1 | r2; }
        //Result& operator&=(Result &r1, Result r2) { return r1 = r1 & r2; }

        /// KPKPosition
        struct KPKPosition
        {
        private:
            Color                 active;
            Square                pSq;
            array<Square, CLR_NO> kSq;

            Result                result;

        public:

            KPKPosition() = default;

            explicit KPKPosition(u32 idx)
            {
                kSq[WHITE] = Square((idx >>  0) & i32(SQ_H8));
                kSq[BLACK] = Square((idx >>  6) & i32(SQ_H8));
                active     =  Color((idx >> 12) & i32(BLACK));
                pSq        = makeSquare(File((idx >> 13) & 0x03)+F_A,
                                        Rank((idx >> 15) & 0x07)+R_2);

                // Check if two pieces are on the same square or if a king can be captured
                if (   1 >= dist(kSq[WHITE], kSq[BLACK])
                    || kSq[WHITE] == pSq
                    || kSq[BLACK] == pSq
                    || (   WHITE == active
                        && contains(PawnAttacks[WHITE][pSq], kSq[BLACK])))
                {
                    result = INVALID;
                }
                else
                // Immediate win if a pawn can be promoted without getting captured
                if (   WHITE == active
                    && R_7 == sRank(pSq)
                    && kSq[WHITE] != pSq + DEL_N
                    && (   1 < dist(kSq[BLACK], pSq + DEL_N)
                        || contains(PieceAttacks[KING][kSq[WHITE]], pSq + DEL_N)))
                {
                    result = WIN;
                }
                else
                // Immediate draw if is a stalemate or king captures undefended pawn
                if (   BLACK == active
                    && (   0 == (    PieceAttacks[KING][kSq[BLACK]]
                                 & ~(PieceAttacks[KING][kSq[WHITE]] | PawnAttacks[WHITE][pSq]))
                        || contains(   PieceAttacks[KING][kSq[BLACK]]
                                    & ~PieceAttacks[KING][kSq[WHITE]], pSq)))
                {
                    result = DRAW;
                }
                else
                // Position will be classified later
                {
                    result = UNKNOWN;
                }
            }

            operator Result() const { return result; }

            Result classify(const vector<KPKPosition> &kpkDB)
            {
                // White to Move:
                // If one move leads to a position classified as WIN, the result of the current position is WIN.
                // If all moves lead to positions classified as DRAW, the result of the current position is DRAW
                // otherwise the current position is classified as UNKNOWN.
                //
                // Black to Move:
                // If one move leads to a position classified as DRAW, the result of the current position is DRAW.
                // If all moves lead to positions classified as WIN, the result of the current position is WIN
                // otherwise the current position is classified as UNKNOWN.

                const auto Good = WHITE == active ? WIN : DRAW;
                const auto  Bad = WHITE == active ? DRAW : WIN;

                Result r = INVALID;
                Bitboard b = PieceAttacks[KING][kSq[active]];
                while (0 != b)
                {
                    auto ksq = popLSq(b);
                    r |= WHITE == active ?
                            kpkDB[index(BLACK, ksq, kSq[BLACK], pSq)] :
                            kpkDB[index(WHITE, kSq[WHITE], ksq, pSq)];
                }

                if (WHITE == active)
                {
                    // Single push
                    if (R_7 > sRank(pSq))
                    {
                        auto pushSq = pSq + DEL_N;
                        r |= kpkDB[index(BLACK, kSq[WHITE], kSq[BLACK], pushSq)];

                        // Double push
                        if (   R_2 == sRank(pSq)
                            // Front is not own king
                            && kSq[WHITE] != pushSq
                            // Front is not opp king
                            && kSq[BLACK] != pushSq)
                        {
                            r |= kpkDB[index(BLACK, kSq[WHITE], kSq[BLACK], pushSq + DEL_N)];
                        }
                    }
                }

                result = r & Good ?
                             Good :
                             r & UNKNOWN ?
                                 UNKNOWN :
                                 Bad;
                return result;
            }

        };
    }

    /// BitBases::initialize()
    void initialize()
    {
        vector<KPKPosition> kpkDB(MaxIndex);
        // Initialize kpkDB with known win / draw positions
        for (u32 idx = 0; idx < MaxIndex; ++idx)
        {
            kpkDB[idx] = KPKPosition(idx);
        }
        // Iterate through the positions until none of the unknown positions can be
        // changed to either wins or draws (15 cycles needed).
        bool repeat = true;
        while (repeat)
        {
            repeat = false;
            for (u32 idx = 0; idx < MaxIndex; ++idx)
            {
                repeat |= UNKNOWN == kpkDB[idx]
                       && UNKNOWN != kpkDB[idx].classify(kpkDB);
            }
        }
        // Fill the bitbase with the decisive results
        for (u32 idx = 0; idx < MaxIndex; ++idx)
        {
            if (WIN == kpkDB[idx])
            {
                KPKBitbase.set(idx);
            }
        }
    }

    /// BitBases::probe()
    bool probe(Color active, Square wkSq, Square wpSq, Square bkSq)
    {
        return KPKBitbase[index(active, wkSq, bkSq, wpSq)];
    }

}
