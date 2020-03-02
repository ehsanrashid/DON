#include "Cuckoo.h"

#include "Bitboard.h"
#include "Zobrist.h"

Array<Cuckoo, CuckooSize> Cuckoos;

bool Cuckoo::empty() const {
    return 0 == key
        || MOVE_NONE == move;
}

namespace CucKoo {

    void initialize() {
        u16 count = 0;
        // Prepare the Cuckoo tables
        Cuckoos.fill({ 0, MOVE_NONE });
        for (Color c : { WHITE, BLACK }) {
            for (PieceType pt = NIHT; pt <= KING; ++pt) {
                for (Square org = SQ_A1; org <= SQ_H8; ++org) {
                    for (Square dst = Square(org + 1); dst <= SQ_H8; ++dst) {

                        if (!contains(PieceAttacks[pt][org], dst)) {
                            continue;
                        }

                        Cuckoo cuckoo{ RandZob.pieceSquareKey[c][pt][org]
                                     ^ RandZob.pieceSquareKey[c][pt][dst]
                                     ^ RandZob.colorKey,
                                       makeMove<NORMAL>(org, dst) };

                        u16 h = hash(cuckoo.key >> 0x00);
                        while (true) {
                            std::swap(Cuckoos[h], cuckoo);
                            // Arrived at empty slot ?
                            if (cuckoo.empty()) {
                                break;
                            }
                            // Push victim to alternative slot
                            h = h == hash(cuckoo.key >> 0x00) ?
                                hash(cuckoo.key >> 0x10) :
                                hash(cuckoo.key >> 0x00);
                        }
                        ++count;
                    }
                }
            }
        }
        assert(3668 == count);
    }
}
