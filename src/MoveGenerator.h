#pragma once

#include <vector>

#include "Position.h"
#include "Type.h"

enum GenType : u08 {
    NATURAL,
    EVASION,
    CAPTURE,
    QUIET,
    QUIET_CHECK,
    LEGAL,
};

template<GenType>
extern void generate(ValMoves&, Position const&);

template<GenType GT>//, PieceType PT = NONE>
class MoveList :
    public ValMoves {

public:

    MoveList() = delete;
    //MoveList(MoveList const&) = delete;
    MoveList& operator=(MoveList const&) = delete;

    explicit MoveList(Position const &pos) {

        generate<GT>(*this, pos);
        //if (NONE != PT) {
        //    erase(
        //        std::remove_if(
        //            begin(), end(),
        //            [&](ValMove const &vm) {
        //                return PT != pType(pos[orgSq(vm)]);
        //            }),
        //        end());
        //}
    }

};

struct Perft {

    i16 moves{ 0 };
    u64 any{ 0 };
    u64 capture{ 0 };
    u64 enpassant{ 0 };
    u64 anyCheck{ 0 };
    u64 dscCheck{ 0 };
    u64 dblCheck{ 0 };
    u64 castle{ 0 };
    u64 promotion{ 0 };
    u64 checkmate{ 0 };
    //u64 stalemate{ 0 };

    void operator+=(Perft const&);
    void operator-=(Perft const&);

    void classify(Position&, Move);
};

template<bool RootNode>
extern Perft perft(Position&, Depth, bool = false);
