#pragma once

#include <array>

#include "Position.h"
#include "Type.h"

namespace Pawns {

    /// Pawns::Entry contains various information about a pawn structure.
    struct Entry
    {
    public:
        Key key;

        Array<Score   , COLORS> score;

        Array<Bitboard, COLORS> attackSpan;
        Array<Bitboard, COLORS> passers;

        Array<Square  , COLORS> kingSq;
        Array<Bitboard, COLORS> kingPath;
        Array<Score   , COLORS> kingSafety;
        Array<Score   , COLORS> kingDist;

        i32 passedCount() const { return popCount(passers[WHITE] | passers[BLACK]); }

        template<Color Own>
        Score evaluateKingSafety(const Position&, Bitboard);

        template<Color>
        void evaluate(const Position&);

    };

    extern Entry* probe(const Position&);
}

using PawnHashTable = HashTable<Pawns::Entry, 0x20000>;
