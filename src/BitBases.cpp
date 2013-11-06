#include "BitBases.h"
#include "BitBoard.h"
#include "BitScan.h"
#include "Square.h"
#include <vector>

namespace BitBases {

    namespace {

        // The possible pawns squares are 24, the first 4 files and ranks from 2 to 7
        const uint32_t MAX_INDEX = 2*24*64*64; // stm * p_sq * wk_sq * bk_sq = 196608

        // Each uint32_t stores results of 32 positions, one per bit
        uint32_t KPKBitbase[MAX_INDEX / 32];

        // A KPK bitbase index is an integer in [0, MAX_INDEX] range
        //
        // Information is mapped in a way that minimizes number of iterations:
        //
        // bit  0- 5: white king square (from SQ_A1 to SQ_H8)
        // bit  6-11: black king square (from SQ_A1 to SQ_H8)
        // bit    12: side to move (WHITE or BLACK)
        // bit 13-14: white pawn file (from F_A to F_D)
        // bit 15-17: white pawn R_7 - rank (from R_7 - R_7 to R_7 - R_2)
        uint32_t index(Color c, Square bk_sq, Square wk_sq, Square p_sq)
        {
            return wk_sq + (bk_sq << 6) + (c << 12) + (_file(p_sq) << 13) + ((R_7 - _rank (p_sq)) << 15);
        }

        enum Result
        {
            INVALID = 0,
            UNKNOWN = 1,
            DRAW    = 2,
            WIN     = 4
        };

        inline Result& operator|= (Result &r1, Result r2) { return r1 = Result (r1 | r2); }

        struct KPKPosition
        {

        private:

            template<Color C>
            Result classify (const std::vector<KPKPosition>& db);

            Color c;
            Square bk_sq, wk_sq, p_sq;
            Result result;

        public:

            KPKPosition (uint32_t idx);

            operator Result() const { return result; }

            Result classify (const std::vector<KPKPosition>& db)
            {
                return (c == WHITE) ? classify<WHITE> (db) : classify<BLACK> (db);
            }

        };

        KPKPosition::KPKPosition (uint32_t idx)
        {

            wk_sq = Square((idx >>  0) & 0x3F);
            bk_sq = Square((idx >>  6) & 0x3F);
            c     = Color ((idx >> 12) & 0x01);
            p_sq  = File  ((idx >> 13) & 0x03) | Rank (uint8_t (R_7) - (idx >> 15));
            result  = UNKNOWN;

            //// Check if two pieces are on the same square or if a king can be captured
            //if (   square_distance(wk_sq, bk_sq) <= 1 || wk_sq == p_sq || bk_sq == p_sq
            //    || (us == WHITE && (StepAttacksBB[PAWN][p_sq] & bk_sq)))
            //{
            //    result = INVALID;
            //}
            //else if (us == WHITE)
            //{
            //    // Immediate win if pawn can be promoted without getting captured
            //    if (   rank_of(p_sq) == RANK_7
            //        && wk_sq != p_sq + DELTA_N
            //        && (   square_distance(bk_sq, p_sq + DELTA_N) > 1
            //        ||(StepAttacksBB[KING][wk_sq] & (p_sq + DELTA_N))))
            //        result = WIN;
            //}
            //// Immediate draw if is stalemate or king captures undefended pawn
            //else if (  !(StepAttacksBB[KING][bk_sq] & ~(StepAttacksBB[KING][wk_sq] | StepAttacksBB[PAWN][p_sq]))
            //    || (StepAttacksBB[KING][bk_sq] & p_sq & ~StepAttacksBB[KING][wk_sq]))
            //{
            //    result = DRAW;
            //}
        }

        template<Color C>
        Result KPKPosition::classify (const std::vector<KPKPosition>& db)
        {

            // White to Move:
            // If one move leads to a position classified as WIN, the result of the current position is WIN.
            // If all moves lead to positions classified as DRAW, the current position is classified DRAW
            // otherwise the current position is classified as UNKNOWN.
            //
            // Black to Move:
            // If one move leads to a position classified as DRAW, the result of the current position is DRAW.
            // If all moves lead to positions classified as WIN, the current position is classified WIN
            // otherwise the current position is classified as UNKNOWN.

            const Color C_ = ((WHITE == C) ? BLACK : WHITE);

            Result r = INVALID;

            Bitboard b = BitBoard::attacks_bb<KING> ((WHITE == C) ? wk_sq : bk_sq);
            while (b)
            {
                r |= (WHITE == C) ?
                    db[index(C_, bk_sq, pop_lsq (b), p_sq)] :
                    db[index(C_, pop_lsq (b), wk_sq, p_sq)];
            }

            if ((WHITE == C) && (_rank (p_sq) < R_7))
            {
                Square s = p_sq + DEL_N;

                r |= db[index(BLACK, bk_sq, wk_sq, s)]; // Single push

                if (_rank (p_sq) == R_2 && s != wk_sq && s != bk_sq)
                {
                    r |= db[index(BLACK, bk_sq, wk_sq, s + DEL_N)]; // Double push
                }
            }

            return result = (WHITE == C) ?
                (r & WIN  ? WIN  : r & UNKNOWN ? UNKNOWN : DRAW) :
                (r & DRAW ? DRAW : r & UNKNOWN ? UNKNOWN : WIN);
        }

    }

    void init_kpk ()
    {
        std::vector<KPKPosition> db;
        db.reserve (MAX_INDEX);

        uint32_t idx;
        // Initialize db with known win / draw positions
        for (idx = 0; idx < MAX_INDEX; ++idx)
        {
            db.emplace_back (KPKPosition (idx));
        }
        
        bool repeat = true;
        // Iterate through the positions until no more of the unknown positions can be
        // changed to either wins or draws (15 cycles needed).
        while (repeat)
        {
            for (idx = 0; idx < MAX_INDEX; ++idx)
            {
                repeat |= ((UNKNOWN == db[idx]) && (UNKNOWN != db[idx].classify (db)));
            }
        }

        // Map 32 results into one KPKBitbase[] entry
        for (idx = 0; idx < MAX_INDEX; ++idx)
        {
            if (WIN == db[idx])
            {
                KPKBitbase[idx / 32] |= 1 << (idx & 0x1F);
            }
        }
    }

    bool probe_kpk (Color c, Square wk_sq, Square p_sq, Square bk_sq)
    {


        return false;
    }

}