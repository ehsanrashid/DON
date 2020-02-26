#pragma once

#include <vector>

#include "Position.h"
#include "Type.h"

enum GenType : u08 {
    NATURAL,
    EVASION,
    CAPTURE,
    QUIET,
    CHECK,
    QUIET_CHECK,
    LEGAL,
};





template<GenType>
extern void generate(ValMoves&, Position const&);

template<GenType GT>//, PieceType PT>
class MoveList
    : public ValMoves {
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
    i16 moves;
    u64 any;
    u64 capture;
    u64 enpassant;
    u64 anyCheck;
    u64 dscCheck;
    u64 dblCheck;
    u64 castle;
    u64 promotion;
    u64 checkmate;
    //u64 stalemate;

    Perft();

    void operator+=(Perft const&);
    void operator-=(Perft const&);

    void classify(Position&, Move);
};

template<bool RootNode>
extern Perft perft(Position&, Depth, bool = false);
