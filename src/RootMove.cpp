#include "RootMove.h"

#include <iterator>
#include <sstream>

#include "MoveGenerator.h"
#include "Notation.h"

using namespace std;

/// RootMove::operator string()
RootMove::operator string() const
{
    ostringstream oss;
    for (auto move : *this)
    {
        assert(MOVE_NONE != move);
        oss << " " << move;
    }
    return oss.str();
}


void RootMoves::initialize(const Position &pos, const vector<Move> &searchMoves)
{
    assert(empty());
    for (const auto &vm : MoveList<GenType::LEGAL>(pos))
    {
        if (   searchMoves.empty()
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
    auto rmItr = std::find(std::next(begin(), sIdx), std::next(begin(), eIdx), move);
    return rmItr != std::next(begin(), eIdx) ?
            rmItr->bestCount : 0;
}

/// RootMoves::operator string()
RootMoves::operator string() const
{
    ostringstream oss;
    std::copy(begin(), end(), ostream_iterator<RootMove>(oss, "\n"));
    return oss.str();
}
