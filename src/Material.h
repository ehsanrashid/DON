#pragma once

#include <array>

#include "Endgame.h"
#include "Position.h"
#include "Type.h"

namespace Material {

    const i32 PhaseResolution = 128;

    /// Material::Entry contains various information about a material configuration.
    struct Entry
    {
    public:
        Key   key;
        i32   phase;
        Score imbalance;
        Array<Scale, COLORS> scale;

        const Endgames::EndgameBase<Value> *evaluationFunc;
        Array<const Endgames::EndgameBase<Scale>*, COLORS> scalingFunc;

        void evaluate(const Position&);
    };

    extern Entry* probe(const Position&);
}

using MatlHashTable = HashTable<Material::Entry, 0x2000>;
