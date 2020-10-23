#pragma once

#include "position.h"
#include "type.h"

enum GenType : uint8_t {
    NORMAL,
    EVASION,
    CAPTURE,
    QUIET,
    QUIET_CHECK,
    LEGAL,
};

template<GenType>
extern void generate(ValMoves&, Position const&) noexcept;

template<GenType GT>//, PieceType PT = NONE>
class MoveList :
    public ValMoves {

public:

    explicit MoveList(Position const &pos) noexcept {

        generate<GT>(*this, pos);
        //if (NONE != PT) {
        //    erase(std::remove_if(begin(), end(),
        //        [&](ValMove const &vm) {
        //            return PT != pType(pos.movedPiece(vm));
        //        }), end());
        //}
    }
    MoveList() = delete;
    MoveList(MoveList const&) = delete;
    MoveList(MoveList&&) = delete;

    MoveList& operator=(MoveList const&) = delete;
    MoveList& operator=(MoveList&&) = delete;
};

struct Perft {

    void classify(Position&, Move) noexcept;

    void operator+=(Perft const&) noexcept;
    void operator-=(Perft const&) noexcept;

    uint16_t num{ 0 };
    uint64_t any{ 0 };
    uint64_t capture{ 0 };
    uint64_t enpassant{ 0 };
    uint64_t anyCheck{ 0 };
    uint64_t dscCheck{ 0 };
    uint64_t dblCheck{ 0 }; // Only if Direct & Discovered Check or one Enpassant case Bishop & Rook
    uint64_t castle{ 0 };
    uint64_t promotion{ 0 };
    uint64_t checkmate{ 0 };
    //uint64_t stalemate{ 0 };
};

template<bool RootNode>
extern Perft perft(Position&, Depth, bool = false) noexcept;
