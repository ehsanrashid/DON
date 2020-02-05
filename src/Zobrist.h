#pragma once

#include <array>

#include "Position.h"
#include "Type.h"

/// Zobrist
class Zobrist
{
public:
    // 2*6*64 + 16 + 8 + 1 = 793
    std::array<
        std::array<
            std::array<
                Key, SQ_NO>, NONE>, CLR_NO> pieceSquareKey;
    std::array<Key, CR_NO>                  castleRightKey;
    std::array<Key, F_NO>                   enpassantKey;
    Key                                     colorKey;

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
