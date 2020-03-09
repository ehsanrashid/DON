#include "Cuckoo.h"

#include "Bitboard.h"
#include "Zobrist.h"

Array<Cuckoo, CuckooSize> Cuckoos;

namespace CucKoo {

    void initialize() {

        u16 count = 0;
        // Prepare the Cuckoo tables
        Cuckoos.fill({ 0, MOVE_NONE });
        for (Color c : { WHITE, BLACK }) {
            for (PieceType pt = NIHT; pt <= KING; ++pt) {
                for (Square org = SQ_A1; org <= SQ_H8; ++org) {
                    for (Square dst = Square(org + 1); dst <= SQ_H8; ++dst) {

                        if (contains(PieceAttackBB[pt][org], dst)) {

                            Cuckoo cuckoo{ RandZob.colorKey
                                         ^ RandZob.pieceSquareKey[c][pt][org]
                                         ^ RandZob.pieceSquareKey[c][pt][dst],
                                           makeMove<NORMAL>(org, dst) };

                            u16 h = hash<0>(cuckoo.key);
                            while (true) {

                                std::swap(Cuckoos[h], cuckoo);
                                // Arrived at empty slot ?
                                if (MOVE_NONE == cuckoo.move) {
                                    break;
                                }
                                // Push victim to alternative slot
                                h = h == hash<0>(cuckoo.key) ?
                                    hash<1>(cuckoo.key) :
                                    hash<0>(cuckoo.key);
                            }
                            ++count;
                        }
                    }
                }
            }
        }
        assert(3668 == count);
    }
}
