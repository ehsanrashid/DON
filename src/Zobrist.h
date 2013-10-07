//#pragma once
#ifndef ZOBRIST_H_
#define ZOBRIST_H_

#include <string>
#include "Type.h"

class Position;

namespace Zobrist {

    extern const Key KEY_MATL; // = U64 (0xC1D58449E708A0AD);
    extern const Key KEY_PAWN; // = U64 (0x37FC40DA841E1692);
    extern const Key KEY_POSI; // = U64 (0x463B96181691FC9C);

    // 2*6*64 + 2*2 + 8 + 1
    //    768 +   4 + 8 + 1
    //                  781
    const uint16_t SIZE_RANDOM = 781;

    extern Key exclusion;

    // Zobrist Random numbers
    typedef union Zob
    {
    public:
        Key random[SIZE_RANDOM];

        struct _
        {
            Key ps_sq[CLR_NO][PT_NO][SQ_NO]; // [COLOR][PIECE][SQUARE]
            Key castle_right[CLR_NO][CS_NO]; // [COLOR][CASTLE SIDE]
            Key en_passant[F_NO];            // [ENPASSANT FILE]
            Key side_move;                   // COLOR

        } _;

    public:

        void initialize ();

    public:
        // Hash key of the material situation.
        Key key_matl (const Position &pos) const;
        // Hash key of the pawn structure.
        Key key_pawn (const Position &pos) const;
        // Hash key of the complete position.
        Key key_posi (const Position &pos) const;

        // Hash key of the FEN
        Key key_fen (const        char *fen, bool c960 = false) const;
        Key key_fen (const std::string &fen, bool c960 = false) const;

    } Zob;


    extern void initialize ();

}

extern const Zobrist::Zob ZobPoly;
extern       Zobrist::Zob ZobRand;
extern const Zobrist::Zob &ZobGlob;

#endif
