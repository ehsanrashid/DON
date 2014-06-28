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

        u08    _castle_rights [CLR_NO];
        u08    _kp_min_dist   [CLR_NO];
        Score  _king_safety   [CLR_NO];

        template<Color C>
        Value _shelter_storm (const Position &pos, Square k_sq) const;

        template<Color C>
        Score _evaluate_king_safety (const Position &pos, Square k_sq);

    public:

        Key      pawn_key;
        Score    pawn_score;

        Square   king_sq        [CLR_NO];
        Bitboard pawn_attacks   [CLR_NO];

        Bitboard blocked_pawns  [CLR_NO];
        Bitboard passed_pawns   [CLR_NO];
        Bitboard candidate_pawns[CLR_NO];

        u08      semiopen_files [CLR_NO];
        u08      pawn_span      [CLR_NO];
        // Count of pawns on LIGHT and DARK squares
        u08      pawns_on_sqrs  [CLR_NO][CLR_NO]; // [color][light/dark squares]

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
            return pawns_on_sqrs[C][!!(BitBoard::Dark_bb & s)];
        }

        template<Color C>
        inline Score evaluate_king_safety (const Position &pos, Square k_sq)
        {
            if (king_sq[C] != k_sq || _castle_rights[C] != pos.can_castle (C))
            {
                king_sq       [C] = k_sq;
                _castle_rights[C] = pos.can_castle (C);
                _king_safety  [C] = _evaluate_king_safety<C> (pos, k_sq);
            }
            return _king_safety[C];
        }

        template<Color C>
        Score evaluate_unstoppable_pawns () const;

    };

    typedef HashTable<Entry, 0x4000> Table; // 16384

    extern void initialize ();

    extern Entry* probe (const Position &pos, Table &table);

}

#endif // _PAWNS_H_INC_
