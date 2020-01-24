#include "BitBases.h"

#include <array>
#include <vector>

#include "BitBoard.h"

namespace BitBases {

    using namespace std;
    using namespace BitBoard;

    namespace {

        // There are 24 possible pawn squares: files A to D and ranks from 2 to 7
        constexpr u32 MaxIndex = 2*24*SQ_NO*SQ_NO; // 2*24*64*64 = 196608

        // Each u32 entity stores results of 32 positions, one per bit
        array<u32, MaxIndex / 32> KPK_Bitbase;

        // A KPK bit-base index is an integer in [0, MaxIndex] range
        //
        // Information is mapped in a way that minimizes the number of iterations:
        //
        // bit  0- 5: white king square(from SQ_A1 to SQ_H8)
        // bit  6-11: black king square(from SQ_A1 to SQ_H8)
        // bit    12: color(WHITE or BLACK)
        // bit 13-14: white pawn file(from F_A to F_D)
        // bit 15-17: white pawn R_7 - rank(from R_7 to R_2)
        u32 index(Color c, Square wk_sq, Square bk_sq, Square wp_sq)
        {
            return (wk_sq << 0)
                 | (bk_sq << 6)
                 | (c << 12)
                 | (_file(wp_sq) << 13)
                 | ((R_7 - _rank(wp_sq)) << 15);
        }

        enum Result : u08
        {
            NONE    = 0,
            UNKNOWN = 1 << 0,
            DRAW    = 1 << 1,
            WIN     = 1 << 2,
            LOSE    = 1 << 3,
        };

        Result& operator|=(Result &r1, Result r2) { return r1 = Result(r1|r2); }
        //Result& operator&=(Result &r1, Result r2) { return r1 = Result(r1&r2); }

        struct KPK_Position
        {
        private:
            Color                 active;
            array<Square, CLR_NO> k_sq;
            Square                p_sq;

            template<Color Own>
            Result classify(const vector<KPK_Position> &kpk_pos)
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

                constexpr auto  Opp = WHITE == Own ? BLACK : WHITE;
                constexpr auto Good = WHITE == Own ? Result::WIN : Result::DRAW;
                constexpr auto  Bad = WHITE == Own ? Result::DRAW : Result::WIN;

                Result r = Result::NONE;
                Bitboard b = PieceAttacks[KING][k_sq[Own]];
                while (0 != b)
                {
                    auto ksq = pop_lsq(b);
                    r |= WHITE == Own ?
                            kpk_pos[index(Opp, ksq, k_sq[Opp], p_sq)].result :
                            kpk_pos[index(Opp, k_sq[Opp], ksq, p_sq)].result;
                }

                if (WHITE == Own)
                {
                    // Single push
                    if (R_7 > _rank(p_sq))
                    {
                        r |= kpk_pos[index(Opp, k_sq[Own], k_sq[Opp], p_sq + DEL_N)].result;
                    }
                    // Double push
                    if (   R_2 == _rank(p_sq)
                        // Front is not own king
                        && k_sq[Own] != p_sq + DEL_N
                        // Front is not opp king
                        && k_sq[Opp] != p_sq + DEL_N)
                    {
                        r |= kpk_pos[index(Opp, k_sq[Own], k_sq[Opp], p_sq + DEL_N + DEL_N)].result;
                    }
                }

                return result = r & Good  ?
                                    Good  :
                                    r & Result::UNKNOWN ?
                                        Result::UNKNOWN :
                                        Bad;
            }

        public:

            Result result;

            KPK_Position() = default;
            explicit KPK_Position(u32 idx)
            {
                k_sq[WHITE] = Square(        (idx >>  0) & i08(SQ_H8));
                k_sq[BLACK] = Square(        (idx >>  6) & i08(SQ_H8));
                active      = Color(         (idx >> 12) & i08(BLACK));
                p_sq        = File(          (idx >> 13) & 3)
                            | Rank(i08(R_7)-((idx >> 15) & i08(R_8)));

                // Check if two pieces are on the same square or if a king can be captured
                if (   1 >= dist(k_sq[WHITE], k_sq[BLACK])
                    || k_sq[WHITE] == p_sq
                    || k_sq[BLACK] == p_sq
                    || (   WHITE == active
                        && contains(PawnAttacks[WHITE][p_sq], k_sq[BLACK])))
                {
                    result = Result::NONE;
                }
                else
                // Immediate win if a pawn can be promoted without getting captured
                if (   WHITE == active
                    && _rank(p_sq) == R_7
                    && k_sq[WHITE] != (p_sq + DEL_N)
                    && (   1 < dist(k_sq[BLACK], p_sq + DEL_N)
                        || contains(PieceAttacks[KING][k_sq[WHITE]], p_sq + DEL_N)))
                {
                    result = Result::WIN;
                }
                else
                // Immediate draw if is a stalemate or king captures undefended pawn
                if (   BLACK == active
                    && (   0 == (PieceAttacks[KING][k_sq[BLACK]] & ~(PieceAttacks[KING][k_sq[WHITE]] | PawnAttacks[WHITE][p_sq]))
                        || contains(PieceAttacks[KING][k_sq[BLACK]] & ~PieceAttacks[KING][k_sq[WHITE]], p_sq)))
                {
                    result = Result::DRAW;
                }
                else
                // Position will be classified later
                {
                    result = Result::UNKNOWN;
                }
            }

            Result classify(const vector<KPK_Position> &kpk_pos)
            {
                return WHITE == active ?
                        classify<WHITE>(kpk_pos) :
                        classify<BLACK>(kpk_pos);
            }
        };
    }

    void initialize()
    {
        vector<KPK_Position> kpk_pos;
        kpk_pos.reserve(MaxIndex);
        // Initialize kpk_pos with known win / draw positions
        for (u32 idx = 0; idx < MaxIndex; ++idx)
        {
            kpk_pos.emplace_back(idx);
        }

        bool repeat;
        // Iterate through the positions until none of the unknown positions can be
        // changed to either wins or draws (15 cycles needed).
        do
        {
            repeat = false;
            for (u32 idx = 0; idx < MaxIndex; ++idx)
            {
                repeat |= (   Result::UNKNOWN == kpk_pos[idx].result
                           && Result::UNKNOWN != kpk_pos[idx].classify(kpk_pos));
            }
        } while (repeat);

        // Map 32 results into one KPK_Bitbase[] entry
        for (u32 idx = 0; idx < MaxIndex; ++idx)
        {
            if (Result::WIN == kpk_pos[idx].result)
            {
                KPK_Bitbase[idx / 32] |= 1 << (idx & 0x1F);
            }
        }
    }

    bool probe(Color c, Square wk_sq, Square wp_sq, Square bk_sq)
    {
        assert(_file(wp_sq) <= F_D);

        auto idx = index(c, wk_sq, bk_sq, wp_sq);
        return KPK_Bitbase[idx / 32] & (1 << (idx & 0x1F));
    }

}
