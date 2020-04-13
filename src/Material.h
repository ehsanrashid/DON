#pragma once

#include "Endgame.h"
#include "Position.h"
#include "Type.h"

namespace Material {

    constexpr i32 PhaseResolution{ 128 };

    /// Material::Entry contains information about Material configuration.
    struct Entry {

        Key   key;
        i32   phase;
        Score imbalance;

        Scale scaleFactor[COLORS];
        EndgameBase<Value> const *evaluationFunc;
        EndgameBase<Scale> const *scalingFunc[COLORS];

        void evaluate(Position const&);

    };

    using Table = HashTable<Entry>;

    extern Entry* probe(Position const&);
}
