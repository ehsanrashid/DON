#pragma once

#include <array>

#include "Position.h"
#include "Type.h"

/// Zobrist
class Zobrist
{
public:
    // 2*6*64 + 16 + 8 + 1 = 793
    std::array<std::array<std::array<Key, SQ_NO>, NONE>, CLR_NO> piece_square;
    std::array<Key, CR_NO> castle_right;
    std::array<Key, F_NO> enpassant;
    Key color;

    Zobrist() = default;
    Zobrist(const Zobrist&) = delete;
    Zobrist& operator=(const Zobrist&) = delete;

    Key compute_matl_key(const Position&) const;
    Key compute_pawn_key(const Position&) const;
    Key compute_posi_key(const Position&) const;
    //Key compute_fen_key(const std::string&) const;
};

extern void zobrist_initialize();

extern Zobrist RandZob;
extern Zobrist const PolyZob;
