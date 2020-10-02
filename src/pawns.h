#pragma once

#include "position.h"
#include "type.h"

namespace Pawns {

    /// Pawns::Entry contains information about Pawn structure.
    struct Entry {

        i32 blockedCount() const noexcept;
        i32 passedCount() const noexcept;

        template<Color>
        void evaluate(Position const&);

        Key key;

        i32 complexity;
        bool pawnOnBothFlank;

        Score score[COLORS];

        Bitboard sglAttacks[COLORS];
        Bitboard dblAttacks[COLORS];
        Bitboard attacksSpan[COLORS];
        Bitboard passeds[COLORS];
        Bitboard blockeds;
    };

    using Table = HashTable<Entry, 0x20000>;

    extern Entry* probe(Position const&);
}
