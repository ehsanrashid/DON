#pragma once

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

template<GenType>
extern void generate(ValMoves&, const Position&);

extern void filter_illegal(ValMoves&, const Position&);

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
        //                            return PT != ptype(pos[org_sq(vm)]);
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
    u64 any_check;
    u64 dsc_check;
    u64 dbl_check;
    u64 castle;
    u64 promotion;
    u64 checkmate;
    //u64 stalemate;

    Perft()
        : moves(0)
        , any(0)
        , capture(0)
        , enpassant(0)
        , any_check(0)
        , dsc_check(0)
        , dbl_check(0)
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
        any_check += p.any_check;
        dsc_check += p.dsc_check;
        dbl_check += p.dbl_check;
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
        any_check -= p.any_check;
        dsc_check -= p.dsc_check;
        dbl_check -= p.dbl_check;
        castle    -= p.castle;
        promotion -= p.promotion;
        checkmate -= p.checkmate;
        //stalemate -= p.stalemate;
    }

    void classify(Position&, Move);
};

template<bool RootNode>
extern Perft perft(Position&, Depth, bool = false);
