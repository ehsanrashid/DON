#include "Cuckoo.h"

#include "Bitboard.h"
#include "Zobrist.h"


Cuckoo::Cuckoo(Piece p, Square s1, Square s2) :
    piece{ p },
    sq1{ s1 },
    sq2{ s2 }
{}

Cuckoo::Cuckoo() :
    Cuckoo{ NO_PIECE, SQ_NONE, SQ_NONE }
{}

bool Cuckoo::empty() const {
    return piece == NO_PIECE
        || sq1 == SQ_NONE
        || sq2 == SQ_NONE;
}

bool Cuckoo::operator==(Cuckoo const &ck) const {
    return piece == ck.piece
        && sq1 == ck.sq1
        && sq2 == ck.sq2;
}
bool Cuckoo::operator!=(Cuckoo const &ck) const {
    return piece != ck.piece
        || sq1 != ck.sq1
        || sq2 != ck.sq2;
}

Key Cuckoo::key() const {
    return empty() ? 0 :
           RandZob.side
         ^ RandZob.psq[piece][sq1]
         ^ RandZob.psq[piece][sq2];
}

namespace Cuckoos {

    Cuckoo CuckooTable[CuckooSize];


    u16 nextHash(Key key, u16 h) {
        return hash<0>(key) == h ?
                hash<1>(key) :
                hash<0>(key);
    }

    void place(Cuckoo &cuckoo) {

        u16 h{ hash<0>(cuckoo.key()) };
        while (true) { // max 20 iteration

            std::swap(CuckooTable[h], cuckoo);
            // Arrived at empty slot ?
            if (cuckoo.empty()) {
                return;
            }
            // Push victim to alternative slot
            h = nextHash(cuckoo.key(), h);
        }
    }

    bool lookup(Key key, Cuckoo &cuckoo) {
        return ((cuckoo = CuckooTable[hash<0>(key)]).key() == key)
            || ((cuckoo = CuckooTable[hash<1>(key)]).key() == key);
    }

    void initialize() {

        std::vector<Cuckoo> cuckoos;
        for (Piece p : Pieces) {
            // Pawn moves are not reversible
            if (pType(p) == PAWN) {
                continue;
            }

            for (Square s1 = SQ_A1; s1 <= SQ_H8 + WEST; ++s1) {
                for (Square s2 = s1 + EAST; s2 <= SQ_H8; ++s2) {
                    if (contains(PieceAttacksBB[pType(p)][s1], s2)) {
                        cuckoos.emplace_back(p, s1, s2);
                    }
                }
            }
        }
        assert(cuckoos.size() == 3668); // 2*(168+280+448+728+210) = 7336 / 2

        // Prepare the Cuckoo table
        std::fill_n(CuckooTable, CuckooSize, Cuckoo{ NO_PIECE, SQ_NONE, SQ_NONE });
        for (auto cuckoo : cuckoos) {
            place(cuckoo);
        }
    }
}
