#include "rootmove.h"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <iostream>

#include "movegenerator.h"
#include "notation.h"

RootMove::RootMove(Move m) :
    std::vector<Move>{ 1, m } {
}

bool RootMove::operator<(RootMove const &rm) const noexcept {
    return (rm.newValue < newValue)
        || (rm.newValue == newValue
         && rm.oldValue < oldValue);
}
bool RootMove::operator>(RootMove const &rm) const noexcept {
    return (rm.newValue > newValue)
        || (rm.newValue == newValue
         && rm.oldValue > oldValue);
}

//bool RootMove::operator==(RootMove const &rm) const noexcept {
//    return front() == rm[0];
//}
//bool RootMove::operator!=(RootMove const &rm) const noexcept {
//    return front() != rm[0];
//}

bool RootMove::operator==(Move m) const noexcept {
    return front() == m;
}
bool RootMove::operator!=(Move m) const noexcept {
    return front() != m;
}

void RootMove::operator+=(Move m) {
    push_back(m);
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

std::ostream& operator<<(std::ostream &ostream, RootMove const &rm) {
    ostream << rm.toString();
    return ostream;
}

RootMoves::RootMoves(Position const &pos) :
    std::vector<RootMove>() {

    for (auto const &vm : MoveList<LEGAL>(pos)) {
        *this += vm;
    }
}

RootMoves::RootMoves(Position const &pos, Moves const &filterMoves) :
    std::vector<RootMove>() {

    for (auto const &vm : MoveList<LEGAL>(pos)) {
        if (filterMoves.empty()
         || filterMoves.contains(vm)) {
            *this += vm;
        }
    }
}

void RootMoves::operator+=(Move m) {
    emplace_back(m);
}
//void RootMoves::operator-=(Move m) {
//    erase(std::remove(begin(), end(), m), end());
//}

void RootMoves::operator+=(RootMove const &rm) {
    push_back(rm);
}
//void RootMoves::operator-=(RootMove const &rm) {
//    erase(std::remove(begin(), end(), rm), end());
//}

RootMoves::const_iterator RootMoves::find(Move m) const {
    return std::find(begin(), end(), m);
}
RootMoves::const_iterator RootMoves::find(uint16_t iBeg, uint16_t iEnd, Move m) const {
    return std::find(begin() + iBeg, begin() + iEnd, m);
}

bool RootMoves::contains(Move m) const {
    return find(m) != end();
}
bool RootMoves::contains(uint16_t iBeg, uint16_t iEnd, Move m) const {
    return find(iBeg, iEnd, m) != (begin() + iEnd);
}

RootMoves::iterator RootMoves::find(Move m) {
    return std::find(begin(), end(), m);
}
RootMoves::iterator RootMoves::find(uint16_t iBeg, uint16_t iEnd, Move m) {
    return std::find(begin() + iBeg, begin() + iEnd, m);
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

std::ostream& operator<<(std::ostream &ostream, RootMoves const &rms) {
    ostream << rms.toString();
    return ostream;
}
