#pragma once

#include <array>

#include "Endgame.h"
#include "Position.h"
#include "Type.h"
#include "Util.h"

namespace Material {

    const i32 PhaseResolution = 128;

    /// Material::Entry contains various information about a material configuration.
    struct Entry
    {
    public:
        Key   key;
        i32   phase;
        Score imbalance;
        std::array<Scale, CLR_NO> scale;

        const Endgames::EndgameBase<Value> *evaluationFunc;
        std::array<const Endgames::EndgameBase<Scale>*, CLR_NO> scalingFunc;

        void evaluate(const Position&);
    };

    typedef HashTable<Entry, 0x2000> Table;

    extern Entry* probe(const Position&);
}
