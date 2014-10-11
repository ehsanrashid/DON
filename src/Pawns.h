#ifndef _PAWNS_H_INC_
#define _PAWNS_H_INC_

#include "Type.h"
#include "Position.h"

namespace Pawns {

    // Pawns::Entry contains various information about a pawn structure.
    // A lookup to the pawn hash table (performed by calling the probe function)
    // returns a pointer to an Entry object.
    struct Entry
    {
    private:
        template<Color C>
        Value pawn_shelter_storm (const Position &pos, Square k_sq) const;

    public:

        Key      pawn_key;
        Score    pawn_score;

        Bitboard pawns_attacks [CLR_NO];

        //Bitboard blocked_pawns [CLR_NO];
        Bitboard passed_pawns  [CLR_NO];

        u08      semiopen_files[CLR_NO];
        u08      pawn_span     [CLR_NO];
        // Count of pawns on LIGHT and DARK squares
        u08      pawns_on_sqrs [CLR_NO][CLR_NO]; // [color][light/dark squares]

        Square   king_sq       [CLR_NO];
        Value    shelter_storm [CLR_NO][3];
        u08      kp_dist       [CLR_NO];

        template<Color C>
        inline u08 semiopen_file (File f) const
        {
            return semiopen_files[C] & (1 << f);
        }

        template<Color C>
        inline u08 semiopen_side (File f, bool left) const
        {
            return semiopen_files[C] & (left ? ((1 << f) - 1) : ~((1 << (f+1)) - 1));
        }

        template<Color C>
        inline i32 pawns_on_squarecolor (Square s) const
        {
            return pawns_on_sqrs[C][!(BitBoard::LIHT_bb & BitBoard::SQUARE_bb[s])];
        }
        template<Color C>
        inline i32 pawns_on_center () const
        {
            return pawns_on_sqrs[C][WHITE] + pawns_on_sqrs[C][BLACK];
        }


        template<Color C>
        Score evaluate_unstoppable_pawns () const;

        template<Color C>
        inline void evaluate_king_pawn_safety (const Position &pos)
        {
            Square k_sq = pos.king_sq (C);
            if (king_sq[C] != k_sq)
            {
                king_sq[C] = k_sq;

                Rank kr = rel_rank (C, k_sq);
                shelter_storm[C][CS_K ] = kr == R_1 ? pawn_shelter_storm<C> (pos, rel_sq (C, SQ_G1)) : VALUE_ZERO;
                shelter_storm[C][CS_Q ] = kr == R_1 ? pawn_shelter_storm<C> (pos, rel_sq (C, SQ_C1)) : VALUE_ZERO;
                shelter_storm[C][CS_NO] = kr <= R_4 ? pawn_shelter_storm<C> (pos, k_sq) : VALUE_ZERO;

                kp_dist[C] = 0;
                Bitboard pawns = pos.pieces<PAWN> (C);
                if (pawns != U64(0))
                {
                    while ((BitBoard::DIST_RINGS_bb[k_sq][kp_dist[C]++] & pawns) == U64(0)) {}
                }
            }
        }

    };

    typedef HashTable<Entry, 0x4000> Table; // 16384

    extern Entry* probe (const Position &pos, Table &table);
    
}

#endif // _PAWNS_H_INC_
