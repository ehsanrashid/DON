#include "cuckoo.h"

#include "bitboard.h"

namespace Cuckoos {

    Cuckoo CuckooTable[CuckooSize];

    // Hash function for indexing the Cuckoo table
    template<uint8_t F>
    constexpr uint16_t hash(Key key) {
        //assert(0 <= F && F <= 3);
        return (key >> (0x10 * F)) & (CuckooSize - 1);
    }

    constexpr uint16_t nextHash(Key key, uint16_t h) noexcept {
        return hash<0>(key) == h ?
                hash<1>(key) : hash<0>(key);
    }

    void place(Cuckoo &cuckoo) noexcept {

        uint16_t h{ hash<0>(cuckoo.key()) };
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

    bool lookup(Key key, Cuckoo &cuckoo) noexcept {
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
                    if (contains(attacksBB(pType(p), s1, 0), s2)) {
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
