#pragma once

#include "Table.h"
#include "Type.h"

/// Cuckoo consists Zobrist hashes and corresponding valid reversible moves
/// Marcel van Kervink's cuckoo algorithm for fast detection of "upcoming repetition".
/// Description of the algorithm in the following paper:
/// https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf
struct Cuckoo {
    Key  key;
    Move move;

    bool empty() const;
};

namespace CucKoo {

    extern void initialize();
}

constexpr u16 CuckooSize = 0x2000;

// Hash function for indexing the cuckoo tables
constexpr u16 hash(u16 key) {
    return key & (CuckooSize - 1);
}

// Global Cuckoo table
// Cuckoo tables with Zobrist hashes of valid reversible moves, and the moves themselves
extern Array<Cuckoo, CuckooSize> Cuckoos;
