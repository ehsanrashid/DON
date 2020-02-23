#pragma once

#include <array>

#include "Position.h"
#include "Type.h"

namespace Pawns {

    /// Pawns::Entry contains various information about a pawn structure.
    struct Entry {
        Key key;

        Array<Score, COLORS> score;

        Array<Bitboard, COLORS> attackSpan;
        Array<Bitboard, COLORS> passers;

        Array<Square, COLORS> kingSq{ SQ_NONE, SQ_NONE };
        Array<Bitboard, COLORS> kingPath;
        Array<Score, COLORS> kingSafety;
        Array<Score, COLORS> kingDist;

        i32 passedCount() const;

        template<Color Own>
        Score evaluateKingSafety(Position const&, Bitboard);

        template<Color>
        void evaluate(Position const&);

    };

    using Table = HashTable<Entry, 0x20000>;

    extern Entry* probe(Position const&);
}
