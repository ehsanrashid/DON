#include "BitBases.h"

#include <vector>

#include "BitBoard.h"
#include "BitScan.h"

namespace BitBases {

    using namespace std;
    using namespace BitBoard;

    namespace {

        // There are 24 possible pawn squares: the first 4 files and ranks from 2 to 7
        const u32 MAX_INDEX = 2*24*SQ_NO*SQ_NO; // stm * wp_sq * wk_sq * bk_sq = 196608

        enum Result
        {
            INVALID = 0,
            UNKNOWN = 1,
            DRAW    = 2,
            WIN     = 4,
            LOSE    = 8
        };
        inline Result& operator|= (Result &r1, Result r2) { return r1 = Result (r1 | r2); }

        // Each u32 stores results of 32 positions, one per bit
        u32 KPKBitbase[MAX_INDEX / 32];

        struct KPKPosition
        {

        private:

            template<Color C>
            Result classify (const vector<KPKPosition> &db);

            Color  _active;
            Square _bk_sq
                ,  _wk_sq
                ,  _p_sq;

            Result result;

        public:

            KPKPosition (u32 idx);

            operator Result () const { return result; }

            Result classify (const vector<KPKPosition>& db)
            {
                return (WHITE == _active) ? classify<WHITE> (db) : classify<BLACK> (db);
            }

        };

        inline KPKPosition::KPKPosition (u32 idx)
        {
            _wk_sq  = Square((idx >>  0) & 0x3F);
            _bk_sq  = Square((idx >>  6) & 0x3F);
            _active = Color ((idx >> 12) & 0x01);
            _p_sq   = File  ((idx >> 13) & 0x03) | Rank (i08 (R_7) - (idx >> 15));
            result  = UNKNOWN;

            // Check if two pieces are on the same square or if a king can be captured
            if (   (SquareDist[_wk_sq][_bk_sq] <= 1)
                || (_wk_sq == _p_sq)
                || (_bk_sq == _p_sq)
                || (WHITE == _active && (PawnAttacks[WHITE][_p_sq] & _bk_sq))
               )
            {
                result = INVALID;
            }
            else
            {
                if (WHITE == _active)
                {
                    // Immediate win if a pawn can be promoted without getting captured
                    if (   (_rank (_p_sq) == R_7)
                        && (_wk_sq != _p_sq + DEL_N)
                        && ((SquareDist[_bk_sq][_p_sq + DEL_N] > 1)
                         || (PieceAttacks[KING][_wk_sq] & (_p_sq + DEL_N))
                           )
                       )
                    {
                        result = WIN;
                    }
                }
                else
                {
                    // Immediate draw if is a stalemate or king captures undefended pawn
                    if (  !(PieceAttacks[KING][_bk_sq] & ~(PieceAttacks[KING][_wk_sq] | PawnAttacks[WHITE][_p_sq]))
                        || (PieceAttacks[KING][_bk_sq] &  ~PieceAttacks[KING][_wk_sq] & _p_sq)
                       )
                    {
                        result = DRAW;
                    }
                }
            }
        }

        // A KPK bitbase index is an integer in [0, MAX_INDEX] range
        //
        // Information is mapped in a way that minimizes the number of iterations:
        //
        // bit  0- 5: white king square (from SQ_A1 to SQ_H8)
        // bit  6-11: black king square (from SQ_A1 to SQ_H8)
        // bit    12: side to move (WHITE or BLACK)
        // bit 13-14: white pawn file (from F_A to F_D)
        // bit 15-17: white pawn R_7 - rank (from R_7 - R_7 to R_7 - R_2)
        inline u32 index (Color c, Square bk_sq, Square wk_sq, Square wp_sq)
        {
            return wk_sq + (bk_sq << 6) + (c << 12) + (_file (wp_sq) << 13) + ((i32 (R_7) - i32 (_rank (wp_sq))) << 15);
        }

        template<Color C>
        inline Result KPKPosition::classify (const vector<KPKPosition>& db)
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

            const Color C_ = (WHITE == C) ? BLACK : WHITE;

            Result r = INVALID;

            Bitboard b = PieceAttacks[KING][(WHITE == C) ? _wk_sq : _bk_sq];
            while (b != U64 (0))
            {
                r |= (WHITE == C) ?
                    db[index(C_, _bk_sq, pop_lsq (b), _p_sq)] :
                    db[index(C_, pop_lsq (b), _wk_sq, _p_sq)];
            }

            if ((WHITE == C) && (_rank (_p_sq) < R_7))
            {
                Square s = _p_sq + DEL_N;

                r |= db[index(BLACK, _bk_sq, _wk_sq, s)];               // Single push

                if (_rank (_p_sq) == R_2 && s != _wk_sq && s != _bk_sq)
                {
                    r |= db[index(BLACK, _bk_sq, _wk_sq, s + DEL_N)];   // Double push
                }
            }

            return result = (WHITE == C)
                ? (r & WIN  ? WIN  : r & UNKNOWN ? UNKNOWN : DRAW)
                : (r & DRAW ? DRAW : r & UNKNOWN ? UNKNOWN : WIN);
        }

    }

    void initialize ()
    {
        vector<KPKPosition> db;
        db.reserve (MAX_INDEX);

        u32 idx;
        // Initialize db with known win / draw positions
        for (idx = 0; idx < MAX_INDEX; ++idx)
        {
            db.push_back (KPKPosition (idx));
        }

        bool repeat;
        // Iterate through the positions until none of the unknown positions can be
        // changed to either wins or draws (15 cycles needed).
        do
        {
            repeat = false;
            for (idx = 0; idx < MAX_INDEX; ++idx)
            {
                repeat |= ((UNKNOWN == db[idx]) && (UNKNOWN != db[idx].classify (db)));
            }
        }
        while (repeat);

        // Map 32 results into one KPKBitbase[] entry
        for (idx = 0; idx < MAX_INDEX; ++idx)
        {
            if (WIN == db[idx])
            {
                KPKBitbase[idx / 32] |= 1 << (idx & 0x1F);
            }
        }
    }

    bool probe_kpk (Color c, Square wk_sq, Square wp_sq, Square bk_sq)
    {
        ASSERT (_file (wp_sq) <= F_D);

        u32 idx = index (c, bk_sq, wk_sq, wp_sq);
        return KPKBitbase[idx / 32] & (1 << (idx & 0x1F));
    }

}