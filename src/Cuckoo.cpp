#include "Cuckoo.h"

#include "Bitboard.h"
#include "Zobrist.h"


Array<Cuckoo, CuckooSize> Cuckoos;


Cuckoo::Cuckoo(Key k, Move m)
    : key{k}
    , move{m}
{}

bool Cuckoo::empty() const
{
    return 0 == key
        || MOVE_NONE == move;
}

namespace Cuckooo
{

    void initialize()
    {
        // Prepare the Cuckoo tables
        Cuckoos.fill({0, MOVE_NONE});
        u16 count = 0;
        for (Color c : { WHITE, BLACK })
        {
            for (PieceType pt = NIHT; pt <= KING; ++pt)
            {
                for (Square org = SQ_A1; org <= SQ_H8; ++org)
                {
                    for (Square dst = Square(org + 1); dst <= SQ_H8; ++dst)
                    {
                        if (contains(PieceAttacks[pt][org], dst))
                        {
                            Cuckoo cuckoo{RandZob.pieceSquareKey[c][pt][org]
                                        ^ RandZob.pieceSquareKey[c][pt][dst]
                                        ^ RandZob.colorKey,
                                          makeMove<NORMAL>(org, dst)};

                            u16 i = H1(cuckoo.key);
                            while (true)
                            {
                                std::swap(Cuckoos[i], cuckoo);
                                // Arrived at empty slot ?
                                if (cuckoo.empty())
                                {
                                    break;
                                }
                                // Push victim to alternative slot
                                i = i == H1(cuckoo.key) ?
                                        H2(cuckoo.key) :
                                        H1(cuckoo.key);
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
