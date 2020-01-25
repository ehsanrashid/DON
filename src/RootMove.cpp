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


void RootMoves::initialize(const Position &pos, const vector<Move> &search_moves)
{
    assert(empty());
    for (const auto &vm : MoveList<GenType::LEGAL>(pos))
    {
        if (   search_moves.empty()
            || std::find(search_moves.begin(), search_moves.end(), vm) != search_moves.end())
        {
            *this += vm;
            assert(back().tb_rank == 0
                && back().tb_value == VALUE_ZERO);
        }
    }
}

i16 RootMoves::move_best_count(u32 s_idx, u32 e_idx, Move move) const
{
    auto rm_itr = std::find(begin() + s_idx, begin() + e_idx, move);
    return rm_itr != begin() + e_idx ?
            rm_itr->best_count :
            0;
}

/// RootMoves::operator string()
RootMoves::operator string() const
{
    ostringstream oss;
    std::copy(begin(), end(), ostream_iterator<RootMove>(oss, "\n"));
    return oss.str();
}
