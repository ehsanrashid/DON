#pragma once

#include <array>

#include "Position.h"
#include "Types.h"

/// Zobrist
class Zobrist
{
public:
    // 2*6*64 + 16 + 8 + 1 = 793
    Table<Key, COLORS, NONE, SQUARES>   pieceSquareKey;
    Table<Key, CASTLE_RIGHTS>           castleRightKey;
    Table<Key, FILES>                   enpassantKey;
    Key                                 colorKey;

    Zobrist() = default;
    Zobrist(const Zobrist&) = delete;
    Zobrist& operator=(const Zobrist&) = delete;

    Key computeMatlKey(const Position&) const;
    Key computePawnKey(const Position&) const;
    Key computePosiKey(const Position&) const;
    //Key computeFenKey(const std::string&) const;
};

extern void initializeZobrist();

extern Zobrist RandZob;
extern Zobrist const PolyZob;
