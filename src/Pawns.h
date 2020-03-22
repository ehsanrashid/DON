#pragma once

#include <array>

#include "Position.h"
#include "Type.h"

namespace Pawns {

    /// Pawns::Entry contains various information about a pawn structure.
    struct Entry {

        Key key;

        Array<Score, COLORS> score;

        Array<Bitboard, COLORS> sglAttacks;
        Array<Bitboard, COLORS> dblAttacks;
        Array<Bitboard, COLORS> attacksSpan;
        Array<Bitboard, COLORS> passPawns;

        Array<Square, COLORS> kingSq;
        Array<u08   , COLORS> castleSide;

        Array<Score, COLORS> kingSafety;
        Array<Score, COLORS> kingDist;

        i32 passedCount() const;

        template<Color Own>
        Score evaluateKingSafety(Position const&, Bitboard);

        template<Color>
        void evaluate(Position const&);

    };

    using Table = HashTable<Entry>;

    extern Entry* probe(Position const&);
}
