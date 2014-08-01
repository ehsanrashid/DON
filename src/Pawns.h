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

    public:

        Key      pawn_key;
        Score    pawn_score;

        Bitboard pawns          [CLR_NO];
        Bitboard pawns_attacks  [CLR_NO];

        //Bitboard blocked_pawns  [CLR_NO];
        Bitboard passed_pawns   [CLR_NO];
        Bitboard candidate_pawns[CLR_NO];

        u08      semiopen_files [CLR_NO];
        u08      pawn_span      [CLR_NO];
        // Count of pawns on LIGHT and DARK squares
        u08      pawns_on_sqrs  [CLR_NO][CLR_NO]; // [color][light/dark squares]

        Square   king_sq        [CLR_NO];
        Value    shelter_storm  [CLR_NO][3];
        u08      min_kp_dist    [CLR_NO];

        template<Color C>
        inline u08  semiopen_file (File f) const
        {
            return semiopen_files[C] & (1 << f);
        }

        template<Color C>
        inline u08  semiopen_side (File f, bool left) const
        {
            return semiopen_files[C] & (left ? ((1 << f) - 1) : ~((1 << (f+1)) - 1));
        }

        template<Color C>
        inline i32 pawns_on_squares (Square s) const
        {
            return pawns_on_sqrs[C][!(BitBoard::Liht_bb & BitBoard::Square_bb[s])];
        }

        template<Color C>
        Score evaluate_unstoppable_pawns () const;

        template<Color C>
        Value pawn_shelter_storm (Square k_sq);

        template<Color C>
        inline void evaluate_king_pawn_safety (const Position &pos)
        {
            Square k_sq = pos.king_sq (C);
            if (king_sq[C] != k_sq)
            {
                king_sq[C] = k_sq;

                Rank kr = rel_rank (C, k_sq);
                if (kr <= R_4)
                {
                    if (kr == R_1 && pos.can_castle (C))
                    {
                        shelter_storm[C][CS_K ] = pos.can_castle (Castling<C, CS_K>::Right) ? pawn_shelter_storm<C> (rel_sq (C, SQ_G1)) : VALUE_ZERO; 
                        shelter_storm[C][CS_Q ] = pos.can_castle (Castling<C, CS_Q>::Right) ? pawn_shelter_storm<C> (rel_sq (C, SQ_C1)) : VALUE_ZERO; 
                    }
                    else
                    {
                        shelter_storm[C][CS_K ] = VALUE_ZERO; 
                        shelter_storm[C][CS_Q ] = VALUE_ZERO; 
                    }
                    shelter_storm[C][CS_NO] = pawn_shelter_storm<C> (k_sq);
                }

                min_kp_dist[C] = 0;
                if (pawns[C])
                {
                    while (!(BitBoard::DistanceRings[k_sq][min_kp_dist[C]++] & pawns[C])) {}
                }
            }
        }

    };

    typedef HashTable<Entry, 0x4000> Table; // 16384

    extern void initialize ();

    extern Entry* probe (const Position &pos, Table &table);

}

#endif // _PAWNS_H_INC_
