#include "RootMove.h"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <iostream>

#include "MoveGenerator.h"
#include "Notation.h"

RootMove::RootMove(Move m)
    : std::list<Move>{ 1, m }
    , oldValue{ -VALUE_INFINITE }
    , newValue{ -VALUE_INFINITE }
    , selDepth{ DEPTH_ZERO }
    , tbRank{ 0 }
    , tbValue{ VALUE_ZERO }
    , bestCount{ 0 }
{}

/// RootMove::toString()
std::string RootMove::toString() const {
    std::ostringstream oss;
    for (auto move : *this) {
        assert(MOVE_NONE != move);
        oss << " " << move;
    }
    return oss.str();
}

std::ostream& operator<<(std::ostream &os, RootMove const &rm) {
    os << rm.toString();
    return os;
}


i16 RootMoves::moveBestCount(u32 sIdx, u32 eIdx, Move move) const {
    auto rmItr{ std::find(std::next(begin(), sIdx), std::next(begin(), eIdx), move) };
    return rmItr != std::next(begin(), eIdx) ?
            rmItr->bestCount : 0;
}

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
        if (std::find(filterMoves.begin(), filterMoves.end(), vm) != filterMoves.end()) {
            *this += vm;
            assert(back().tbRank == 0
                && back().tbValue == VALUE_ZERO);
        }
    }
}

void RootMoves::saveValues() {

    for (auto &rm : *this) {
        rm.oldValue = rm.newValue;
    }
}

void RootMoves::stableSort(i16 pvBeg, i16 pvEnd) {
    std::stable_sort(std::next(begin(), pvBeg),
                     std::next(begin(), pvEnd));
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
