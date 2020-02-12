#pragma once

#include <list>
#include <vector>

#include "Position.h"
#include "Types.h"

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
    Value oldValue
        , newValue;
    Depth selDepth;
    i16   tbRank;
    Value tbValue;
    i16   bestCount;

    explicit RootMove(Move m = MOVE_NONE)
        : std::list<Move>(1, m)
        , oldValue(-VALUE_INFINITE)
        , newValue(-VALUE_INFINITE)
        , selDepth(0)
        , tbRank(0)
        , tbValue(VALUE_ZERO)
        , bestCount(0)
    {}
    RootMove& operator=(const RootMove&) = default;

    bool operator< (const RootMove &rm) const { return newValue != rm.newValue ? newValue > rm.newValue : oldValue > rm.oldValue; }
    bool operator> (const RootMove &rm) const { return newValue != rm.newValue ? newValue < rm.newValue : oldValue < rm.oldValue; }
    //bool operator<=(const RootMove &rm) const { return newValue != rm.newValue ? newValue >= rm.newValue : oldValue >= rm.oldValue; }
    //bool operator>=(const RootMove &rm) const { return newValue != rm.newValue ? newValue <= rm.newValue : oldValue <= rm.oldValue; }
    //bool operator==(const RootMove &rm) const { return front() == rm.front(); }
    //bool operator!=(const RootMove &rm) const { return front() != rm.front(); }

    bool operator==(Move m) const { return front() == m; }
    bool operator!=(Move m) const { return front() != m; }

    void operator+=(Move m) { push_back(m); }
    //void operator-=(Move m) { erase(std::remove(begin(), end(), m), end()); }

    explicit operator std::string() const;
};

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, const RootMove &rm)
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

    i16 moveBestCount(u32, u32, Move) const;

    explicit operator std::string() const;
};

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, const RootMoves &rms)
{
    os << std::string(rms);
    return os;
}
