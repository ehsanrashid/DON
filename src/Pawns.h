#pragma once

#include "Position.h"
#include "Type.h"

namespace Pawns {

    /// Pawns::Entry contains information about Pawn structure.
    struct Entry {

        Key      key;

        i32      complexity;
        bool     pawnNotBothFlank;

        Score    score[COLORS];

        Bitboard sglAttacks[COLORS];
        Bitboard dblAttacks[COLORS];
        Bitboard attacksSpan[COLORS];
        Bitboard passPawns[COLORS];

        i32 passedCount() const;

        template<Color>
        void evaluate(Position const&);

    };

    using Table = HashTable<Entry>;

    extern Entry* probe(Position const&);
}
