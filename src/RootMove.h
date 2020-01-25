#pragma once

#include <list>
#include <vector>

#include "Position.h"
#include "Type.h"

/// The root of the tree is a PV node.
/// At a PV node all the children have to be investigated.
/// The best move found at a PV node leads to a successor PV node,
/// while all the other investigated children are CUT nodes
/// At a CUT node the child causing a beta cut-off is an ALL node
/// In a perfectly ordered tree only one child of a CUT node has to be explored
/// At an ALL node all the children have to be explored. The successors of an ALL node are CUT nodes
/// NonPV nodes = CUT nodes + ALL nodes
///
/// RootMove class is used for moves at the root of the tree.
/// RootMove stores:
///  - New/Old values
///  - SelDepth
///  - PV (really a refutation table in the case of moves which fail low)
/// Value is normally set at -VALUE_INFINITE for all non-pv moves.
class RootMove
    : public std::list<Move>
{
public:
    Value old_value
        , new_value;

    i16   best_count;
    Depth sel_depth;
    i16   tb_rank;
    Value tb_value;

    explicit RootMove(Move m = MOVE_NONE)
        : std::list<Move>(1, m)
        , old_value(-VALUE_INFINITE)
        , new_value(-VALUE_INFINITE)
        , best_count(0)
        , sel_depth(0)
        , tb_rank(0)
        , tb_value(VALUE_ZERO)
    {}
    RootMove& operator=(const RootMove&) = default;

    bool operator< (const RootMove &rm) const { return new_value != rm.new_value ? new_value > rm.new_value : old_value > rm.old_value; }
    bool operator> (const RootMove &rm) const { return new_value != rm.new_value ? new_value < rm.new_value : old_value < rm.old_value; }
    //bool operator<=(const RootMove &rm) const { return new_value != rm.new_value ? new_value >= rm.new_value : old_value >= rm.old_value; }
    //bool operator>=(const RootMove &rm) const { return new_value != rm.new_value ? new_value <= rm.new_value : old_value <= rm.old_value; }
    //bool operator==(const RootMove &rm) const { return front() == rm.front(); }
    //bool operator!=(const RootMove &rm) const { return front() != rm.front(); }

    bool operator==(Move m) const { return front() == m; }
    bool operator!=(Move m) const { return front() != m; }

    void operator+=(Move m) { push_back(m); }
    //void operator-=(Move m) { erase(std::remove(begin(), end(), m), end()); }

    explicit operator std::string() const;
};

template<typename CharT, typename Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits> &os, const RootMove &rm)
{
    os << std::string(rm);
    return os;
}


class RootMoves
    : public std::vector<RootMove>
{
public:
    RootMoves() = default;
    RootMoves(const RootMoves&) = default;
    RootMoves& operator=(const RootMoves&) = default;

    void operator+=(Move m) { emplace_back(m); }
    //void operator-=(Move m) { erase(std::remove(begin(), end(), m), end()); }

    void operator+=(const RootMove &rm) { push_back(rm); }
    //void operator-=(const RootMove &rm) { erase(std::remove(begin(), end(), rm), end()); }

    void initialize(const Position&, const std::vector<Move>&);

    i16 move_best_count(u32, u32, Move) const;

    explicit operator std::string() const;
};

template<typename CharT, typename Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<<(std::basic_ostream<CharT, Traits> &os, const RootMoves &rms)
{
    os << std::string(rms);
    return os;
}
