#pragma once

#include "Table.h"
#include "Type.h"

namespace CucKoo {

    /// Cuckoo consists Zobrist hashes and corresponding valid reversible moves
    /// Marcel van Kervink's cuckoo algorithm for fast detection of "upcoming repetition".
    /// Description of the algorithm in the following paper:
    /// https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf
    struct Cuckoo {
        Key  key;
        Move move;

        Cuckoo() = default;
        Cuckoo(Key, Move);

        bool empty() const;
    };

    extern void initialize();
}

constexpr size_t CuckooSize = 0x2000;
// Hash functions for indexing the cuckoo tables

inline u16 H1(Key key) {
    return u16((key >> 0x00) & (CuckooSize - 1));
}
inline u16 H2(Key key) {
    return u16((key >> 0x10) & (CuckooSize - 1));
}

// Global Cuckoo table
// Cuckoo tables with Zobrist hashes of valid reversible moves, and the moves themselves
extern Array<CucKoo::Cuckoo, CuckooSize> Cuckoos;
