#include "RootMove.h"

#include <iterator>
#include <sstream>
#include <iostream>

#include "MoveGenerator.h"
#include "Notation.h"

using namespace std;

RootMove::RootMove(Move m)
    : std::list<Move>{1, m}
    , oldValue{-VALUE_INFINITE}
    , newValue{-VALUE_INFINITE}
    , selDepth{0}
    , tbRank{0}
    , tbValue{VALUE_ZERO}
    , bestCount{0}
{}

/// RootMove::operator string()
string RootMove::toString() const
{
    ostringstream oss;
    for (auto move : *this)
    {
        assert(MOVE_NONE != move);
        oss << " " << move;
    }
    return oss.str();
}

ostream& operator<<(ostream &os, const RootMove &rm) {
    os << rm.toString();
    return os;
}


void RootMoves::initialize(const Position &pos, const vector<Move> &searchMoves)
{
    assert(empty());
    for (const auto &vm : MoveList<GenType::LEGAL>(pos))
    {
        if (searchMoves.empty()
         || std::find(searchMoves.begin(), searchMoves.end(), vm) != searchMoves.end())
        {
            *this += vm;
            assert(back().tbRank == 0
                && back().tbValue == VALUE_ZERO);
        }
    }
}

i16 RootMoves::moveBestCount(u32 sIdx, u32 eIdx, Move move) const
{
    auto rmItr{ std::find(std::next(begin(), sIdx), std::next(begin(), eIdx), move) };
    return rmItr != std::next(begin(), eIdx) ?
            rmItr->bestCount : 0;
}

/// RootMoves::operator string()
string RootMoves::toString() const
{
    ostringstream oss;
    std::copy(begin(), end(), ostream_iterator<RootMove>(oss, "\n"));
    return oss.str();
}

ostream& operator<<(ostream &os, const RootMoves &rms) {
    os << rms.toString();
    return os;
}
