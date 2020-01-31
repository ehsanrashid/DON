#pragma once

#include <array>

#include "Position.h"
#include "Type.h"
#include "Util.h"

namespace Pawns {

    using namespace BitBoard;

    /// Pawns::Entry contains various information about a pawn structure.
    struct Entry
    {
    public:
        Key key;

        std::array<Score   , CLR_NO> scores;
        std::array<Bitboard, CLR_NO> attack_span;
        std::array<Bitboard, CLR_NO> passers;

        std::array<Square     , CLR_NO> king_sq;
        std::array<CastleRight, CLR_NO> castling_rights;
        std::array<Bitboard   , CLR_NO> king_path_attacks;
        std::array<Score      , CLR_NO> king_safety;

        i32 passed_count() const { return pop_count(passers[WHITE] | passers[BLACK]); }

        template<Color Own>
        Score evaluate_king_safety(const Position&, Bitboard);

        template<Color>
        void evaluate(const Position&);

    };

    typedef HashTable<Entry, 0x20000> Table;

    extern Entry* probe(const Position&);
}
