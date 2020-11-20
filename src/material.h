#pragma once

#include "endgame.h"
#include "position.h"
#include "type.h"

namespace Material {

    constexpr int32_t PhaseResolution{ 128 };

    /// Material::Entry contains information about Material configuration.
    struct Entry {

    public:

        void evaluate(Position const&);
        
        Scale scaleFunc(const Position& pos, Color c) const noexcept {
            Scale const scale { scalingFunc[c] != nullptr ?
                              (*scalingFunc[c])(pos) : SCALE_NONE };
            return scale != SCALE_NONE ? scale : scaleFactor[c];
        }

        bool  evalExists() const noexcept { return evaluatingFunc != nullptr; }
        Value evaluateFunc(const Position &pos) const noexcept { return (*evaluatingFunc)(pos); }

        Key     key;
        int32_t phase;
        Score   imbalance;

        Scale   scaleFactor[COLORS];
        EndgameBase<Value> const *evaluatingFunc;
        EndgameBase<Scale> const *scalingFunc[COLORS];
    };

    using Table = HashTable<Entry, 0x2000>;

    extern Entry* probe(Position const&) noexcept;
}
