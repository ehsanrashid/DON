#pragma once

#include <vector>

#include "Position.h"
#include "Type.h"

enum GenType : u08
{
    NATURAL,
    EVASION,
    CAPTURE,
    QUIET,
    CHECK,
    QUIET_CHECK,
    LEGAL,
};

struct ValMove
{
public:
    Move move;
    i32  value;

    ValMove(Move m, i32 v)
        : move(m)
        , value(v)
    {}
    explicit ValMove(Move m = MOVE_NONE)
        : ValMove(m, 0)
    {}

    operator Move() const { return move; }
    void operator=(Move m) { move = m; }

    // Inhibit unwanted implicit conversions to Move
    // with an ambiguity that yields to a compile error.
    operator float() const = delete;
    operator double() const = delete;

    bool operator<(const ValMove &vm) const { return value < vm.value; }
    bool operator>(const ValMove &vm) const { return value > vm.value; }
    //bool operator<=(const ValMove &vm) const { return value <= vm.value; }
    //bool operator>=(const ValMove &vm) const { return value >= vm.value; }
};

class ValMoves
    : public std::vector<ValMove>
{
public:

    void operator+=(Move move) { emplace_back(move); }
    void operator-=(Move move) { erase(std::remove(begin(), end(), move), end()); }
};


template<GenType>
extern void generate(ValMoves&, const Position&);

extern void filterIllegal(ValMoves&, const Position&);

template<GenType GT, PieceType PT = NONE>
class MoveList
    : public ValMoves
{
public:

    MoveList() = delete;
    //MoveList(const MoveList&) = delete;

    explicit MoveList(const Position &pos)
    {
        generate<GT>(*this, pos);
        //if (NONE != PT)
        //{
        //    erase(std::remove_if(begin(), end(),
        //                        [&pos] (const ValMove &vm)
        //                        {
        //                            return PT != typeOf(pos[orgOf(vm)]);
        //                        }),
        //            end());
        //}
    }

    bool contains(Move move) const
    {
        return std::find(begin(), end(), move) != end();
    }
};

struct Perft
{
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

    Perft()
        : moves(0)
        , any(0)
        , capture(0)
        , enpassant(0)
        , anyCheck(0)
        , dscCheck(0)
        , dblCheck(0)
        , castle(0)
        , promotion(0)
        , checkmate(0)
        //, stalemate(0)
    {}

    void operator+=(const Perft &p)
    {
        any       += p.any;
        capture   += p.capture;
        enpassant += p.enpassant;
        anyCheck += p.anyCheck;
        dscCheck += p.dscCheck;
        dblCheck += p.dblCheck;
        castle    += p.castle;
        promotion += p.promotion;
        checkmate += p.checkmate;
        //stalemate += p.stalemate;
    }
    void operator-=(const Perft &p)
    {
        any       -= p.any;
        capture   -= p.capture;
        enpassant -= p.enpassant;
        anyCheck -= p.anyCheck;
        dscCheck -= p.dscCheck;
        dblCheck -= p.dblCheck;
        castle    -= p.castle;
        promotion -= p.promotion;
        checkmate -= p.checkmate;
        //stalemate -= p.stalemate;
    }

    void classify(Position&, Move);
};

template<bool RootNode>
extern Perft perft(Position&, Depth, bool = false);
