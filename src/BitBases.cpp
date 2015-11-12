#include "BitBases.h"

#include "BitBoard.h"

namespace BitBases {

    using namespace std;
    using namespace BitBoard;

    namespace {

        // There are 24 possible pawn squares: the first 4 files and ranks from 2 to 7
        const u32 MAX_INDEX = 2*24*i08(SQ_NO)*i08(SQ_NO); // stm * p_sq * wk_sq * bk_sq = 196608
        
        // Each u32 entity stores results of 32 positions, one per bit
        u32 KPK_Bitbase[MAX_INDEX / 32];

        // A KPK bitbase index is an integer in [0, MAX_INDEX] range
        //
        // Information is mapped in a way that minimizes the number of iterations:
        //
        // bit  0- 5: white king square (from SQ_A1 to SQ_H8)
        // bit  6-11: black king square (from SQ_A1 to SQ_H8)
        // bit    12: side to move color (WHITE or BLACK)
        // bit 13-14: white pawn file (from F_A to F_D)
        // bit 15-17: white pawn R_7 - rank (from R_7 to R_2)
        u32 index (Color c, Square bk_sq, Square wk_sq, Square wp_sq)
        {
            return wk_sq | (bk_sq << 6) | (c << 12) | (_file (wp_sq) << 13) | ((R_7 - _rank (wp_sq)) << 15);
        }
        
        struct KPK_Position
        {
        public:
            
            enum Result
            {
                INVALID = 0,
                UNKNOWN = 1,
                DRAW    = 2,
                WIN     = 4,
                LOSE    = 8
            };
            
            friend Result& operator|= (Result &r1, Result r2) { return r1 = Result (r1|r2); }
            //friend Result& operator&= (Result &r1, Result r2) { return r1 = Result (r1&r2); }
            
        private:

            Color  _active;
            
            Square _k_sq[CLR_NO]
                ,  _p_sq;

            Result result;

            template<Color Own>
            Result classify (const vector<KPK_Position> &kpk_db)
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

                const auto Opp  = WHITE == Own ? BLACK : WHITE;
                const auto Good = WHITE == Own ? WIN   : DRAW;
                const auto Bad  = WHITE == Own ? DRAW  : WIN;

                Result r = INVALID;
                
                Bitboard b = PIECE_ATTACKS[KING][_k_sq[Own]];
                while (b != U64(0))
                {
                    r |= WHITE == Own ?
                            kpk_db[index (Opp, _k_sq[Opp], pop_lsq (b), _p_sq)] :
                            kpk_db[index (Opp, pop_lsq (b), _k_sq[Opp], _p_sq)];
                }

                if (WHITE == Own)
                {
                    // Single push
                    if (_rank (_p_sq) < R_7)
                    {
                        r |= kpk_db[index (Opp, _k_sq[Opp], _k_sq[Own], _p_sq + DEL_N)];
                    }
                    // Double push
                    if (   _rank (_p_sq) == R_2
                        && _p_sq + DEL_N != _k_sq[Own] // Front is not own king
                        && _p_sq + DEL_N != _k_sq[Opp] // Front is not opp king
                       )
                    {
                        r |= kpk_db[index (Opp, _k_sq[Opp], _k_sq[Own], _p_sq + DEL_N + DEL_N)];
                    }
                }

                return result = r & Good  ? Good  :
                                r & UNKNOWN ? UNKNOWN : Bad;
            }

        public:

            KPK_Position () = default;

            explicit KPK_Position (u32 idx)
            {
                _k_sq[WHITE] = Square(         (idx >>  0) & 0x3F);
                _k_sq[BLACK] = Square(         (idx >>  6) & 0x3F);
                _active      = Color (         (idx >> 12) & 0x01);
                _p_sq        = File  (         (idx >> 13) & 0x03)
                             | Rank  (R_7-Rank((idx >> 15) & 0x07));

                // Check if two pieces are on the same square or if a king can be captured
                if (   dist (_k_sq[WHITE], _k_sq[BLACK]) <= 1
                    || _k_sq[WHITE] == _p_sq
                    || _k_sq[BLACK] == _p_sq
                    || (WHITE == _active && (PAWN_ATTACKS[WHITE][_p_sq] & _k_sq[BLACK]) != U64 (0))
                   )
                {
                    result = INVALID;
                }
                else
                // Immediate win if a pawn can be promoted without getting captured
                if (   WHITE == _active
                    && _rank (_p_sq) == R_7
                    && _k_sq[WHITE] != _p_sq + DEL_N
                    && (   dist (_k_sq[BLACK], _p_sq + DEL_N) > 1
                        || (PIECE_ATTACKS[KING][_k_sq[WHITE]] & (_p_sq + DEL_N)) != U64(0)
                       )
                   )
                {
                    result = WIN;
                }
                else
                // Immediate draw if is a stalemate or king captures undefended pawn
                if (   BLACK == _active
                    && (   (PIECE_ATTACKS[KING][_k_sq[BLACK]] & ~(PIECE_ATTACKS[KING][_k_sq[WHITE]] | PAWN_ATTACKS[WHITE][_p_sq])) == U64 (0)
                        || ((PIECE_ATTACKS[KING][_k_sq[BLACK]] & ~PIECE_ATTACKS[KING][_k_sq[WHITE]]) & _p_sq) != U64 (0)
                       )
                   )
                {
                    result = DRAW;
                }
                else
                // Position will be classified later
                {
                    result  = UNKNOWN;
                }
            }

            operator Result () const { return result; }

            Result classify (const vector<KPK_Position>& kpk_db)
            {
                return WHITE == _active ? classify<WHITE> (kpk_db) : classify<BLACK> (kpk_db);
            }

        };

    }

    void initialize ()
    {
        vector<KPK_Position> kpk_db;
        kpk_db.reserve (MAX_INDEX);

        u32 idx;
        // Initialize db with known win / draw positions
        for (idx = 0; idx < MAX_INDEX; ++idx)
        {
            kpk_db.push_back (KPK_Position (idx));
        }

        bool repeat;
        // Iterate through the positions until none of the unknown positions can be
        // changed to either wins or draws (15 cycles needed).
        do
        {
            repeat = false;
            for (idx = 0; idx < MAX_INDEX; ++idx)
            {
                repeat |= KPK_Position::UNKNOWN == kpk_db[idx] && KPK_Position::UNKNOWN != kpk_db[idx].classify (kpk_db);
            }
        } while (repeat);

        // Map 32 results into one KPK_Bitbase[] entry
        for (idx = 0; idx < MAX_INDEX; ++idx)
        {
            if (KPK_Position::WIN == kpk_db[idx])
            {
                KPK_Bitbase[idx / 32] |= 1 << (idx & 0x1F);
            }
        }
    }

    bool probe (Color c, Square wk_sq, Square wp_sq, Square bk_sq)
    {
        assert (_file (wp_sq) <= F_D);

        u32 idx = index (c, bk_sq, wk_sq, wp_sq);
        return KPK_Bitbase[idx / 32] & (1 << (idx & 0x1F));
    }

}