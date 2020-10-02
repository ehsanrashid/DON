#pragma once

#include <vector>

#include "position.h"
#include "type.h"

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
class RootMove :
    public std::vector<Move> {

public:
    //using std::vector<Move>::vector;

    explicit RootMove(Move = MOVE_NONE);

    bool operator< (RootMove const&) const noexcept;
    bool operator> (RootMove const&) const noexcept;
    //bool operator==(RootMove const&) const noexcept;
    //bool operator!=(RootMove const&) const noexcept;

    bool operator==(Move) const noexcept;
    bool operator!=(Move) const noexcept;

    void operator+=(Move);
    //void operator-=(Move);

    std::string toString() const;

    Value oldValue{ -VALUE_INFINITE }
        , newValue{ -VALUE_INFINITE };
    Depth selDepth{ DEPTH_ZERO };
    i16   tbRank  { 0 };
    Value tbValue { VALUE_ZERO };
};

extern std::ostream& operator<<(std::ostream&, RootMove const&);

class RootMoves :
    public std::vector<RootMove> {

public:
    using std::vector<RootMove>::vector;

    RootMoves() = default;
    RootMoves(Position const&);
    RootMoves(Position const&, Moves const&);

    void operator+=(Move);
    //void operator-=(Move);

    void operator+=(RootMove const&);
    //void operator-=(RootMove const&);

    const_iterator find(Move) const;
    const_iterator find(u16, u16, Move) const;

    bool contains(Move) const;
    bool contains(u16, u16, Move) const;

    iterator find(Move);
    iterator find(u16, u16, Move);

    void stableSort() {
        std::stable_sort(begin(), end());
    }
    void stableSort(u16 iBeg, u16 iEnd) {
        std::stable_sort(begin() + iBeg, begin() + iEnd);
    }
    template<class Pr>
    void stableSort(Pr pred) {
        std::stable_sort(begin(), end(), pred);
    }

    void saveValues();
    void bringToFront(Move);

    std::string toString() const;
};

extern std::ostream& operator<<(std::ostream&, RootMoves const&);