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

    explicit RootMove(Move m) :
        std::vector<Move>{ 1, m } {
    }

    bool operator<(RootMove const &rm) const noexcept {
        return (rm.newValue < newValue)
            || (rm.newValue == newValue
             && rm.oldValue < oldValue);
    }
    bool operator>(RootMove const &rm) const noexcept {
        return (rm.newValue > newValue)
            || (rm.newValue == newValue
             && rm.oldValue > oldValue);
    }

    //bool operator==(RootMove const&) const noexcept;
    //bool operator!=(RootMove const&) const noexcept;

    bool operator==(Move m) const noexcept {
        return front() == m;
    }
    bool operator!=(Move m) const noexcept {
        return front() != m;
    }

    void operator+=(Move m) {
        push_back(m);
    }
    //void operator-=(Move m) {
    //    erase(std::remove(begin(), end(), m), end());
    //}

    std::string toString() const;

    Value oldValue{ -VALUE_INFINITE }
        , newValue{ -VALUE_INFINITE };
    Depth selDepth{ DEPTH_ZERO };
    int16_t tbRank{ 0 };
    Value tbValue{ VALUE_ZERO };

};

extern std::ostream& operator<<(std::ostream&, RootMove const&);

class RootMoves :
    public std::vector<RootMove> {

public:

    //using std::vector<RootMove>::vector;

    RootMoves() = default;
    RootMoves(Position const&);
    RootMoves(Position const&, Moves const&);

    void operator+=(Move m) {
        emplace_back(m);
    }
    //void operator-=(Move m) {
    //    erase(std::remove(begin(), end(), m), end());
    //}

    void operator+=(RootMove const &rm) {
        push_back(rm);
    }
    //void operator-=(RootMove const &rm) {
    //    erase(std::remove(begin(), end(), rm), end());
    //}

    const_iterator find(Move m) const {
        return std::find(begin(), end(), m);
    }
    const_iterator find(uint16_t iBeg, uint16_t iEnd, Move m) const {
        return std::find(begin() + iBeg, begin() + iEnd, m);
    }

    bool contains(Move m) const {
        return find(m) != end();
    }
    bool contains(uint16_t iBeg, uint16_t iEnd, Move m) const {
        return find(iBeg, iEnd, m) != (begin() + iEnd);
    }

    iterator find(Move m) {
        return std::find(begin(), end(), m);
    }
    iterator find(uint16_t iBeg, uint16_t iEnd, Move m) {
        return std::find(begin() + iBeg, begin() + iEnd, m);
    }

    void stableSort() {
        std::stable_sort(begin(), end());
    }
    void stableSort(uint16_t iBeg, uint16_t iEnd) {
        std::stable_sort(begin() + iBeg, begin() + iEnd);
    }
    template<class Pred>
    void stableSort(Pred pred) {
        std::stable_sort(begin(), end(), pred);
    }

    void saveValues() {
        for (auto &rm : *this) {
            rm.oldValue = rm.newValue;
        }
    }
    void bringToFront(Move m) {
        std::swap(front(), *std::find(begin(), end(), m));
    }

    std::string toString() const;
};

extern std::ostream& operator<<(std::ostream&, RootMoves const&);
