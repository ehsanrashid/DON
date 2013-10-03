//#pragma once
#ifndef PAWN_STRUCTURE_H_
#define PAWN_STRUCTURE_H_

#include "Type.h"

namespace PawnStructure {


    template<Color C> extern Bitboard pawns_attacks (Bitboard pawns);

    template<Color C> extern Bitboard pawns_pushable_sgl (Bitboard pawns, Bitboard occ);

    template<Color C> extern Bitboard pawns_pushable_dbl (Bitboard pawns, Bitboard occ);

    template<Color C> extern Bitboard pawns_defended (Bitboard pawns);

    template<Color C> extern Bitboard pawns_defending (Bitboard pawns);

    template<Color C> extern Bitboard pawns_defended_defending (Bitboard pawns);

    template<Color C> extern Bitboard pawns_defending_not_defended (Bitboard pawns);

    template<Color C> extern Bitboard pawns_defended_not_defending (Bitboard pawns);

    // Pawns able to capture the enemy pieces
    template<Color C> extern Bitboard pawns_attacking (Bitboard pawns, Bitboard pieces);

    template<Color C> extern Bitboard pawns_rammed (Bitboard wpawns, Bitboard bpawns);


}

#endif
