#include "rootmove.h"

#include <algorithm>
#include <iterator>
#include <sstream>
#include <iostream>

#include "movegenerator.h"
#include "notation.h"

//bool RootMove::operator==(RootMove const &rm) const noexcept {
//    return front() == rm[0];
//}
//bool RootMove::operator!=(RootMove const &rm) const noexcept {
//    return front() != rm[0];
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
