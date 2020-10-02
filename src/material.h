#pragma once

#include "endgame.h"
#include "position.h"
#include "type.h"

namespace Material {

    constexpr int32_t PhaseResolution{ 128 };

    /// Material::Entry contains information about Material configuration.
    struct Entry {

        void evaluate(Position const&);

        Key key;
        int32_t phase;
        Score imbalance;

        Scale scaleFactor[COLORS];
        EndgameBase<Value> const *evaluationFunc;
        EndgameBase<Scale> const *scalingFunc[COLORS];
    };

    using Table = HashTable<Entry, 0x2000>;

    extern Entry* probe(Position const&);
}
