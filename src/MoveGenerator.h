//#pragma once
#ifndef MOVEGENERATOR_H_
#define MOVEGENERATOR_H_

#include "Move.h"

class Position;

namespace MoveGenerator {

    // Type of Generators
    typedef enum GType : uint8_t
    {
        // PSEUDO-LEGAL MOVES
        RELAX,       // Normal moves.
        EVASION,     // Save king in check
        CAPTURE,     // Change material balance where an enemy piece is captured.
        QUIET,       // Do not change material, thus no captures nor promotions.
        CHECK,       // Only checks the enemy King.
        QUIET_CHECK, // Do not change material and only checks the enemy King.
        //DESPERADO,   // Where pieces seem determined to give itself up to bring up stalemate if it is captured.
        
        // ------------------------

        LEGAL,       // Legal moves

    } GType;


    template<GType G>
    extern MoveList generate (const Position &pos);

}

#endif
