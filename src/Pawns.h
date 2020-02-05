#pragma once

#include <array>

#include "Position.h"
#include "Type.h"
#include "Util.h"

namespace Pawns {

    /// Pawns::Entry contains various information about a pawn structure.
    struct Entry
    {
    public:
        Key key;

        std::array<Score   , CLR_NO> score;

        std::array<Bitboard, CLR_NO> attackSpan;
        std::array<Bitboard, CLR_NO> passers;

        std::array<Square  , CLR_NO> kingSq;
        std::array<Bitboard, CLR_NO> kingPath;
        std::array<Score   , CLR_NO> kingSafety;
        std::array<Score   , CLR_NO> kingDist;

        i32 passedCount() const { return BitBoard::popCount(passers[WHITE] | passers[BLACK]); }

        template<Color Own>
        Score evaluateKingSafety(const Position&, Bitboard);

        template<Color>
        void evaluate(const Position&);

    };

    typedef HashTable<Entry, 0x20000> Table;

    extern Entry* probe(const Position&);
}
