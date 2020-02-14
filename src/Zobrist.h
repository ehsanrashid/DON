#pragma once

#include <array>

#include "Position.h"
#include "Type.h"

/// Zobrist
class Zobrist
{
public:
    // 2*6*64 + 16 + 8 + 1 = 793
    Array<Key, COLORS, PIECE_TYPES, SQUARES> pieceSquareKey;
    Array<Key, CASTLE_RIGHTS>                castleRightKey;
    Array<Key, FILES>                        enpassantKey;
    Key                                      colorKey;

    Zobrist() = default;
    Zobrist(const Zobrist&) = delete;
    Zobrist& operator=(const Zobrist&) = delete;

    Key computeMatlKey(const Position&) const;
    Key computePawnKey(const Position&) const;
    Key computePosiKey(const Position&) const;
    //Key computeFenKey(const std::string&) const;
};

namespace Zob {

    extern void initialize();

}

extern Zobrist RandZob;
extern Zobrist const PolyZob;
