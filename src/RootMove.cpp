#include "RootMove.h"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <iostream>

#include "MoveGenerator.h"
#include "Notation.h"

RootMove::RootMove(Move m) :
    std::list<Move>{ 1, m }
{}

bool RootMove::operator<(RootMove const &rm) const {
    return (rm.newValue < newValue)
        || (rm.newValue == newValue
         && rm.oldValue < oldValue);
}
bool RootMove::operator>(RootMove const &rm) const {
    return (rm.newValue > newValue)
        || (rm.newValue == newValue
         && rm.oldValue > oldValue);
}

//bool RootMove::operator==(RootMove const &rm) const {
//    return front() == rm.front();
//}
//bool RootMove::operator!=(RootMove const &rm) const {
//    return front() != rm.front();
//}

bool RootMove::operator==(Move m) const {
    return front() == m;
}

bool RootMove::operator!=(Move m) const {
    return front() != m;
}

void RootMove::operator+=(Move m) {
    emplace_back(m);
}
//void RootMove::operator-=(Move m) {
//    erase(std::remove(begin(), end(), m), end());
//}

/// RootMove::toString()
std::string RootMove::toString() const {
    std::ostringstream oss;
    std::copy(begin(), end(), std::ostream_iterator<Move>(oss, " "));
    return oss.str();
}

std::ostream& operator<<(std::ostream &os, RootMove const &rm) {
    os << rm.toString();
    return os;
}


void RootMoves::operator+=(Move m) {
    emplace_back(m);
}
//void RootMoves::operator-=(Move m) {
//    erase(std::remove(begin(), end(), m), end());
//}

void RootMoves::operator+=(RootMove const &rm) {
    emplace_back(rm);
}
//void RootMoves::operator-=(RootMove const &rm) {
//    erase(std::remove(begin(), end(), rm), end());
//}

void RootMoves::initialize(Position const &pos) {
    assert(empty());
    //clear();
    for (auto const &vm : MoveList<GenType::LEGAL>(pos)) {
        *this += vm;
        assert(back().tbRank == 0
            && back().tbValue == VALUE_ZERO);
    }
}

void RootMoves::initialize(Position const &pos, Moves const &filterMoves) {

    if (filterMoves.empty()) {
        initialize(pos);
        return;
    }

    assert(empty());
    //clear();
    for (auto const &vm : MoveList<GenType::LEGAL>(pos)) {
        if (filterMoves.contains(vm)) {
            *this += vm;
            assert(back().tbRank == 0
                && back().tbValue == VALUE_ZERO);
        }
    }
}

RootMoves::const_iterator RootMoves::find(Move m) const {
    return std::find(begin(), end(), m);
}
RootMoves::const_iterator RootMoves::find(u16 iBeg, u16 iEnd, Move m) const {
    return std::find(std::next(begin(), iBeg),
                     std::next(begin(), iEnd), m);
}

bool RootMoves::contains(Move m) const {
    return find(m) != end();
}
bool RootMoves::contains(u16 iBeg, u16 iEnd, Move m) const {
    return find(iBeg, iEnd, m) != std::next(begin(), iEnd);
}

u16 RootMoves::bestCount(Move m) const {
    return contains(m) ?
            find(m)->bestCount : 0;
}
u16 RootMoves::bestCount(u16 iBeg, u16 iEnd, Move m) const {
    return contains(iBeg, iEnd, m) ?
            find(iBeg, iEnd, m)->bestCount : 0;
}

RootMoves::iterator RootMoves::find(Move m) {
    return std::find(begin(), end(), m);
}
RootMoves::iterator RootMoves::find(u16 iBeg, u16 iEnd, Move m) {
    return std::find(std::next(begin(), iBeg),
        std::next(begin(), iEnd), m);
}

void RootMoves::stableSort() {
    std::stable_sort(begin(), end());
}
void RootMoves::stableSort(u16 iBeg, u16 iEnd) {
    std::stable_sort(std::next(begin(), iBeg), std::next(begin(), iEnd));
}

void RootMoves::saveValues() {

    for (auto &rm : *this) {
        rm.oldValue = rm.newValue;
    }
}

void RootMoves::bringToFront(Move m) {
    std::swap(front(), *std::find(begin(), end(), m));
}

/// RootMoves::toString()
std::string RootMoves::toString() const {
    std::ostringstream oss;
    std::copy(begin(), end(), std::ostream_iterator<RootMove>(oss, "\n"));
    return oss.str();
}

std::ostream& operator<<(std::ostream &os, RootMoves const &rms) {
    os << rms.toString();
    return os;
}
