#pragma once

#include <array>

#include "Position.h"
#include "Type.h"

/// Zobrist class
struct Zobrist {

    // 2*6*64 + 16 + 8 + 1 = 793
    Array<Key, COLORS
             , PIECE_TYPES
             , SQUARES>       pieceSquareKey;
    Array<Key, CASTLE_RIGHTS> castleRightKey;
    Array<Key, FILES>         enpassantKey;
    Key                       colorKey;

    Zobrist() = default;
    Zobrist(Zobrist const&) = delete;
    Zobrist(Zobrist&&) = delete;
    Zobrist& operator=(Zobrist const&) = delete;
    Zobrist& operator=(Zobrist&&) = delete;

    Key computeMatlKey(Position const&) const;
    Key computePawnKey(Position const&) const;
    Key computePosiKey(Position const&) const;
};

namespace Zobrists {

    extern void initialize();
}

extern Zobrist RandZob;
extern Zobrist const PolyZob;
