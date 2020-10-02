#pragma once

#include "position.h"
#include "type.h"

namespace Pawns {

    /// Pawns::Entry contains information about Pawn structure.
    struct Entry {

        int32_t blockedCount() const noexcept;
        int32_t passedCount() const noexcept;

        template<Color>
        void evaluate(Position const&);

        Key key;

        int32_t complexity;
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
