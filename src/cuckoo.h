#pragma once

#include "type.h"

/// Cuckoo consists Zobrist hashes and corresponding valid reversible moves
/// Marcel van Kervink's cuckoo algorithm for fast detection of "upcoming repetition".
/// Description of the algorithm in the following paper:
/// https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf
struct Cuckoo final {

    Cuckoo(Piece, Square, Square) noexcept;
    Cuckoo() noexcept;

    bool empty() const noexcept;
    bool operator==(Cuckoo const&) const noexcept;
    bool operator!=(Cuckoo const&) const noexcept;

    Key key() const noexcept;

    Piece piece;
    Square sq1;
    Square sq2;
};


namespace Cuckoos {

    constexpr u16 CuckooSize{ 0x2000 };

    // Global Cuckoo table
    // Cuckoo tables with Zobrist hashes of valid reversible moves, and the moves themselves
    extern Cuckoo CuckooTable[CuckooSize];

    extern bool lookup(Key, Cuckoo&) noexcept;

    extern void initialize() noexcept;
}
