#include "SkillManager.h"

#include "PRNG.h"
#include "Thread.h"

SkillManager::SkillManager()
    : level{MaxLevel}
{}

bool SkillManager::enabled() const
{
    return level < MaxLevel;
}

bool SkillManager::canPick(Depth depth) const
{
    return depth == 1 + level;
}


void SkillManager::setLevel(i16 lvl)
{
    level = lvl;
}

void SkillManager::clearBestMove()
{
    bestMove = MOVE_NONE;
}


/// SkillManager::pickBestMove() chooses best move among a set of RootMoves when playing with a strength handicap,
/// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
Move SkillManager::pickBestMove()
{
    static PRNG prng{u64(now())}; // PRNG sequence should be non-deterministic.

    const auto &rootMoves{Threadpool.mainThread()->rootMoves};
    assert(!rootMoves.empty());

    if (MOVE_NONE == bestMove)
    {
        // RootMoves are already sorted by value in descending order
        i32  weakness{MaxDepth - 8 * level};
        i32  deviance{std::min(rootMoves[0].newValue - rootMoves[Threadpool.pvCount - 1].newValue, VALUE_MG_PAWN)};
        auto bestValue{-VALUE_INFINITE};
        for (u32 i = 0; i < Threadpool.pvCount; ++i)
        {
            // First for each move score add two terms, both dependent on weakness.
            // One is deterministic with weakness, and one is random with weakness.
            auto value{rootMoves[i].newValue
                     + (weakness * i32(rootMoves[0].newValue - rootMoves[i].newValue)
                      + deviance * i32(prng.rand<u32>() % weakness)) / VALUE_MG_PAWN};
            // Then choose the move with the highest value.
            if (bestValue <= value)
            {
                bestValue = value;
                bestMove = rootMoves[i].front();
            }
        }
    }
    return bestMove;
}
