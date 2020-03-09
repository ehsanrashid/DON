#pragma once

#include "Type.h"

/// Cuckoo consists Zobrist hashes and corresponding valid reversible moves
/// Marcel van Kervink's cuckoo algorithm for fast detection of "upcoming repetition".
/// Description of the algorithm in the following paper:
/// https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf
struct Cuckoo {

    Key  key;
    Move move;
};

namespace CucKoo {

    extern void initialize();
}

constexpr u16 CuckooSize = 0x2000;

// Hash function for indexing the cuckoo tables
template<u08 F>
constexpr u16 hash(Key key) {
    //assert(0 <= F && F <= 3);
    return u16(key >> (0x10 * F)) & (CuckooSize - 1);
}

// Global Cuckoo table
// Cuckoo tables with Zobrist hashes of valid reversible moves, and the moves themselves
extern Array<Cuckoo, CuckooSize> Cuckoos;
