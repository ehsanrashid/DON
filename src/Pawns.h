#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _PAWNS_H_INC_
#define _PAWNS_H_INC_

#include "Type.h"
#include "BitBoard.h"
#include "Position.h"

namespace Pawns {

    // Pawns::Entry contains various information about a pawn structure.
    // A lookup to the pawn hash table (performed by calling the probe function)
    // returns a pointer to an Entry object.
    struct Entry
    {

    private:
        
        Score  _king_safety   [CLR_NO];

        template<Color C>
        Score do_king_safety (const Position &pos, Square k_sq);

    public:

        Key     _pawn_key;
        Score   _pawn_score;

        Bitboard _pawn_attacks   [CLR_NO];
        Bitboard _passed_pawns   [CLR_NO];
        Bitboard _candidate_pawns[CLR_NO];
        
        Square _king_sq       [CLR_NO];
        u08    _kp_min_dist   [CLR_NO];
        u08    _castle_rights [CLR_NO];
        u08    _semiopen_files[CLR_NO];
        // Count of pawns on LIGHT and DARK squares
        u08    _pawn_count_sq [CLR_NO][CLR_NO]; // [color][light/dark squares]       

        inline Score    pawn_score      () const { return _pawn_score; }

        template<Color C>
        inline Bitboard pawn_attacks    () const { return _pawn_attacks[C]; }

        template<Color C>
        inline Bitboard passed_pawns    () const { return _passed_pawns[C]; }
        
        template<Color C>
        inline Bitboard candidate_pawns () const { return _candidate_pawns[C]; }

        template<Color C>
        inline i32  pawns_on_same_color_squares (Square s) const
        {
            return _pawn_count_sq[C][!!(BitBoard::DARK_bb & s)];
        }

        template<Color C>
        inline u08  semiopen_file (File f) const
        {
            return _semiopen_files[C] & (1 << f);
        }

        template<Color C>
        inline u08  semiopen_side (File f, bool left) const
        {
            return _semiopen_files[C] & (left ? ((1 << f) - 1) : ~((1 << (f+1)) - 1));
        }

        template<Color C>
        inline Score king_safety (const Position &pos, Square k_sq)
        {
            return (_king_sq[C] == k_sq && _castle_rights[C] == pos.can_castle (C))
                ? _king_safety[C] : (_king_safety[C] = do_king_safety<C> (pos, k_sq));
        }

        template<Color C>
        Value shelter_storm (const Position &pos, Square k_sq);

    };

    typedef HashTable<Entry, 0x4000> Table; // 16384

    extern void initialize ();

    extern Entry* probe (const Position &pos, Table &table);

}

#endif // _PAWNS_H_INC_
