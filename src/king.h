#pragma once

#include <algorithm>
#include <cassert>

#include "pawns.h"
#include "position.h"
#include "type.h"

namespace King {

    /// King::Entry contains information about King & Pawn structure.
    struct Entry {

    public:

        template<Color>
        Score evaluateSafety(Position const&, Bitboard) noexcept;

        Key     key;
        Pawns::Entry *pawnEntry;

        int8_t  outflanking;
        bool    infiltration;

        Square  square[COLORS];
        bool    castleSide[COLORS][CASTLE_SIDES];
        Score   safety[COLORS];

    private:

        template<Color>
        Score evaluateBonusOn(Position const&, Square) noexcept;
    };

    using Table = HashTable<Entry, 0x10000>;

    extern Entry* probe(Position const&, Pawns::Entry*);

}
