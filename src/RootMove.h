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
    : public std::list<Move> {

public:

    Value oldValue{ -VALUE_INFINITE }
        , newValue{ -VALUE_INFINITE };
    Depth selDepth{ DEPTH_ZERO };
    i16   tbRank{ 0 };
    Value tbValue{ VALUE_ZERO };
    i16   bestCount{ 0 };

    using std::list<Move>::list;

    explicit RootMove(Move = MOVE_NONE);

    bool operator< (RootMove const&) const;
    bool operator> (RootMove const&) const;
    //bool operator<=(RootMove const&) const;
    //bool operator>=(RootMove const&) const;
    //bool operator==(RootMove const&) const;
    //bool operator!=(RootMove const&) const;

    bool operator==(Move m) const { return front() == m; }
    bool operator!=(Move m) const { return front() != m; }

    void operator+=(Move m) { push_back(m); }
    //void operator-=(Move m) { erase(std::remove(begin(), end(), m), end()); }

    std::string toString() const;
};

extern std::ostream& operator<<(std::ostream&, RootMove const&);

class RootMoves
    : public std::vector<RootMove> {

public:

    using std::vector<RootMove>::vector;

    void operator+=(Move m) { emplace_back(m); }
    //void operator-=(Move m) { erase(std::remove(begin(), end(), m), end()); }

    void operator+=(RootMove const &rm) { push_back(rm); }
    //void operator-=(RootMove const &rm) { erase(std::remove(begin(), end(), rm), end()); }

    void initialize(Position const&);
    void initialize(Position const&, Moves const&);

    RootMoves::const_iterator find(u16, u16, Move) const;
    bool contains(u16, u16, Move) const;

    i16 moveBestCount(u16, u16, Move) const;

    void saveValues();
    void stableSort(i16, i16);

    std::string toString() const;
};

extern std::ostream& operator<<(std::ostream&, RootMoves const&);
