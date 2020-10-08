#pragma once

#include "type.h"
#include "zobrist.h"

/// Cuckoo consists Zobrist hashes and corresponding valid reversible moves
/// Marcel van Kervink's cuckoo algorithm for fast detection of "upcoming repetition".
/// Description of the algorithm in the following paper:
/// https://marcelk.net/2013-04-06/paper/upcoming-rep-v2.pdf
struct Cuckoo final {

    Cuckoo(Piece p, Square s1, Square s2) noexcept :
        piece { p },
        sq1{ s1 },
        sq2{ s2 } {
    }
    Cuckoo() noexcept :
        Cuckoo{ NO_PIECE, SQ_NONE, SQ_NONE } {
    }

    bool empty() const noexcept {
        return piece == NO_PIECE
            || sq1 == SQ_NONE
            || sq2 == SQ_NONE;
    }

    bool operator==(Cuckoo const &ck) const noexcept {
        return piece == ck.piece
            && sq1 == ck.sq1
            && sq2 == ck.sq2;
    }
    bool operator!=(Cuckoo const &ck) const noexcept {
        return piece != ck.piece
            || sq1 != ck.sq1
            || sq2 != ck.sq2;
    }

    Key key() const noexcept {
        return empty() ?
            0 : RandZob.side
              ^ RandZob.psq[piece][sq1]
              ^ RandZob.psq[piece][sq2];
    }

    Piece piece;
    Square sq1;
    Square sq2;
};


namespace Cuckoos {

    constexpr uint16_t CuckooSize{ 0x2000 };

    // Global Cuckoo table
    // Cuckoo tables with Zobrist hashes of valid reversible moves, and the moves themselves
    extern Cuckoo CuckooTable[CuckooSize];

    extern bool lookup(Key, Cuckoo&) noexcept;

    extern void initialize();
}
