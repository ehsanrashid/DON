#include "BitBases.h"

#include <bitset>
#include <vector>

#include "BitBoard.h"

namespace BitBases {

    using namespace std;
    using namespace BitBoard;

    namespace {

        // There are 24 possible pawn squares: files A to D and ranks from 2 to 7
        // Positions with the pawn on files E to H will be mirrored before probing.
        constexpr u32 MAX_INDEX = 24*2*SQ_NO*SQ_NO; // p_sq*active*wk_sq*bk_sq = 2*24*64*64 = 196608

        std::bitset<MAX_INDEX> KPK_Bitbase;

        // A KPK bitbase index is an integer in [0, MAX_INDEX] range
        //
        // Information is mapped in a way that minimizes the number of iterations:
        //
        // bit 00-05: white king square (from SQ_A1 to SQ_H8)
        // bit 06-11: black king square (from SQ_A1 to SQ_H8)
        // bit    12: active (WHITE or BLACK)
        // bit 13-14: white pawn file (from F_A to F_D)
        // bit 15-17: white pawn R_7 - rank (from R_2 to R_7)
        u32 index(Color active, Square wk_sq, Square bk_sq, Square wp_sq) noexcept
        {
            assert(F_A <= _file(wp_sq) && _file(wp_sq) <= F_D);
            assert(R_2 <= _rank(wp_sq) && _rank(wp_sq) <= R_7);

            return (wk_sq << 0)
                 | (bk_sq << 6)
                 | (active << 12)
                 | ((_file(wp_sq)-F_A) << 13)
                 | ((_rank(wp_sq)-R_2) << 15);
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

        /// KPK_Position
        struct KPK_Position
        {
        private:
            Color                 active;
            Square                p_sq;
            array<Square, CLR_NO> k_sq;

            Result                result;

        public:

            KPK_Position() = default;

            explicit KPK_Position(u32 idx)
            {
                k_sq[WHITE] = Square((idx >>  0) & SQ_H8);
                k_sq[BLACK] = Square((idx >>  6) & SQ_H8);
                active      =  Color((idx >> 12) & BLACK);
                p_sq        =  (File((idx >> 13) & F_D)+F_A)
                             | (Rank((idx >> 15) & R_8)+R_2);

                // Check if two pieces are on the same square or if a king can be captured
                if (   1 >= dist(k_sq[WHITE], k_sq[BLACK])
                    || k_sq[WHITE] == p_sq
                    || k_sq[BLACK] == p_sq
                    || (   WHITE == active
                        && contains(PawnAttacks[WHITE][p_sq], k_sq[BLACK])))
                {
                    result = INVALID;
                }
                else
                // Immediate win if a pawn can be promoted without getting captured
                if (   WHITE == active
                    && R_7 == _rank(p_sq)
                    && k_sq[WHITE] != p_sq + DEL_N
                    && (   1 < dist(k_sq[BLACK], p_sq + DEL_N)
                        || contains(PieceAttacks[KING][k_sq[WHITE]], p_sq + DEL_N)))
                {
                    result = WIN;
                }
                else
                // Immediate draw if is a stalemate or king captures undefended pawn
                if (   BLACK == active
                    && (   0 == (    PieceAttacks[KING][k_sq[BLACK]]
                                 & ~(PieceAttacks[KING][k_sq[WHITE]] | PawnAttacks[WHITE][p_sq]))
                        || contains(   PieceAttacks[KING][k_sq[BLACK]]
                                    & ~PieceAttacks[KING][k_sq[WHITE]], p_sq)))
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

            Result classify(const vector<KPK_Position> &kpk_db)
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
                Bitboard b = PieceAttacks[KING][k_sq[active]];
                while (0 != b)
                {
                    auto ksq = pop_lsq(b);
                    r |= WHITE == active ?
                            kpk_db[index(BLACK, ksq, k_sq[BLACK], p_sq)] :
                            kpk_db[index(WHITE, k_sq[WHITE], ksq, p_sq)];
                }

                if (WHITE == active)
                {
                    // Single push
                    if (R_7 > _rank(p_sq))
                    {
                        auto push_sq = p_sq + DEL_N;
                        r |= kpk_db[index(BLACK, k_sq[WHITE], k_sq[BLACK], push_sq)];

                        // Double push
                        if (   R_2 == _rank(p_sq)
                            // Front is not own king
                            && k_sq[WHITE] != push_sq
                            // Front is not opp king
                            && k_sq[BLACK] != push_sq)
                        {
                            r |= kpk_db[index(BLACK, k_sq[WHITE], k_sq[BLACK], push_sq + DEL_N)];
                        }
                    }
                }

                return result = r & Good  ?
                                    Good  :
                                    r & UNKNOWN ?
                                        UNKNOWN :
                                        Bad;
            }

        };
    }

    /// BitBases::initialize()
    void initialize()
    {
        vector<KPK_Position> kpk_db(MAX_INDEX);
        // Initialize kpk_db with known win / draw positions
        for (u32 idx = 0; idx < MAX_INDEX; ++idx)
        {
            kpk_db[idx] = KPK_Position(idx);
        }
        // Iterate through the positions until none of the unknown positions can be
        // changed to either wins or draws (15 cycles needed).
        bool repeat = true;
        while (repeat)
        {
            repeat = false;
            for (u32 idx = 0; idx < MAX_INDEX; ++idx)
            {
                repeat |= UNKNOWN == kpk_db[idx]
                       && UNKNOWN != kpk_db[idx].classify(kpk_db);
            }
        }
        // Fill the bitbase with the decisive results
        for (u32 idx = 0; idx < MAX_INDEX; ++idx)
        {
            if (WIN == kpk_db[idx])
            {
                KPK_Bitbase.set(idx);
            }
        }
    }

    /// BitBases::probe()
    bool probe(Color active, Square wk_sq, Square wp_sq, Square bk_sq)
    {
        return KPK_Bitbase[index(active, wk_sq, bk_sq, wp_sq)];
    }

}
