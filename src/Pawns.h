#pragma once

#include <array>

#include "Position.h"
#include "Types.h"

namespace Pawns {

    /// Pawns::Entry contains various information about a pawn structure.
    struct Entry
    {
    public:
        Key key;

        Table<Score   , COLORS> score;

        Table<Bitboard, COLORS> attackSpan;
        Table<Bitboard, COLORS> passers;

        Table<Square  , COLORS> kingSq;
        Table<Bitboard, COLORS> kingPath;
        Table<Score   , COLORS> kingSafety;
        Table<Score   , COLORS> kingDist;

        i32 passedCount() const { return BitBoard::popCount(passers[WHITE] | passers[BLACK]); }

        template<Color Own>
        Score evaluateKingSafety(const Position&, Bitboard);

        template<Color>
        void evaluate(const Position&);

    };

    extern Entry* probe(const Position&);
}

using PawnHashTable = HashTable<Pawns::Entry, 0x20000>;
