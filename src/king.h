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

        template<Color>
        void initialize() noexcept;

        Key key;

        Pawns::Entry *pawnEntry;

        int8_t  outflanking;
        bool    infiltration;

        Square  kingSq[COLORS];
        bool    castleSide[COLORS][CASTLE_SIDES];
        Score   pawnSafety[COLORS];
        Score   pawnDist[COLORS];

    private:

        template<Color>
        Score evaluateSafetyOn(Position const&, Square) noexcept;
    };

    using Table = HashTable<Entry, 0x40000>;

    extern Entry* probe(Position const&, Pawns::Entry*);

}
