#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _PAWNS_H_INC_
#define _PAWNS_H_INC_

#include "Type.h"
#include "BitBoard.h"
#include "Position.h"

namespace Pawns {

    // Pawns::Entry contains various information about a pawn structure. Currently,
    // it only includes a middle game and end game pawn structure evaluation, and a
    // bitboard of passed pawns. We may want to add further information in the future.
    // A lookup to the pawn hash table (performed by calling the probe function)
    // returns a pointer to an Entry object.
    struct Entry
    {
        Key     _pawn_key;
        Score   _pawn_score;

        Bitboard _pawn_attacks   [CLR_NO];
        Bitboard _passed_pawns   [CLR_NO];
        Bitboard _candidate_pawns[CLR_NO];
        
        Square _king_sq       [CLR_NO];
        // Count of pawns on LIGHT and DARK squares
        u08   _pawn_count_sq  [CLR_NO][CLR_NO];
        u08   _kp_min_dist    [CLR_NO];
        u08   _castle_rights  [CLR_NO];
        u08   _semiopen_files [CLR_NO];
        Score _king_safety    [CLR_NO];

        inline Score    pawn_score()             const { return _pawn_score; }
        inline Bitboard pawn_attacks   (Color c) const { return _pawn_attacks[c]; }
        inline Bitboard passed_pawns   (Color c) const { return _passed_pawns[c]; }
        inline Bitboard candidate_pawns(Color c) const { return _candidate_pawns[c]; }
        inline i32  pawns_on_same_color_squares (Color c, Square s) const
        {
            return _pawn_count_sq[c][bool (BitBoard::DARK_bb & BitBoard::Square_bb[s])];
        }
        inline u08  semiopen        (Color c, File f) const
        {
            return _semiopen_files[c] & (1 << f);
        }
        inline u08  semiopen_on_side(Color c, File f, bool left) const
        {
            return _semiopen_files[c] & (left ? ((1 << f) - 1) : ~((1 << (f+1)) - 1));
        }

        template<Color C>
        inline Score king_safety (const Position &pos, Square k_sq)
        {
            return (_king_sq[C] == k_sq && _castle_rights[C] == pos.can_castle (C))
                ? _king_safety[C] : update_safety<C> (pos, k_sq);
        }

        template<Color C>
        Score update_safety (const Position &pos, Square k_sq);

        template<Color C>
        Value shelter_storm (const Position &pos, Square k_sq);

    };


    typedef HashTable<Entry, 16384> Table;

    extern void initialize ();

    extern Entry* probe (const Position &pos, Table &table);

}

#endif // _PAWNS_H_INC_
