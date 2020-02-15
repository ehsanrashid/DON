#pragma once

#include <array>

#include "Endgame.h"
#include "Position.h"
#include "Type.h"

namespace Material
{

    const i32 PhaseResolution = 128;

    /// Material::Entry contains various information about a material configuration.
    struct Entry
    {
    public:
        Key   key;
        i32   phase;
        Score imbalance;
        Array<Scale, COLORS> scale;

        const EndgameBase<Value> *evaluationFunc;
        Array<const EndgameBase<Scale>*, COLORS> scalingFunc;

        void evaluate(const Position&);
    };

    using Table = HashTable<Entry, 0x2000>;


    extern Entry* probe(const Position&);
}
