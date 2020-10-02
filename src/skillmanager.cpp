#include "SkillManager.h"

#include "PRNG.h"
#include "Searcher.h"
#include "Thread.h"

SkillManager SkillMgr;

SkillManager::SkillManager() noexcept :
    level{ MaxLevel },
    bestMove{ MOVE_NONE } {
}

bool SkillManager::enabled() const noexcept {
    return level < MaxLevel;
}

bool SkillManager::canPick(Depth depth) const noexcept {
    return depth == 1 + level;
}

void SkillManager::setLevel(u16 lvl) noexcept {
    level = lvl;
}

void SkillManager::clear() noexcept {
    bestMove = MOVE_NONE;
}


/// SkillManager::pickBestMove() chooses best move among a set of RootMoves when playing with a strength handicap,
/// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
Move SkillManager::pickBestMove() noexcept {
    static PRNG prng(now()); // PRNG sequence should be non-deterministic.

    if (bestMove == MOVE_NONE) {
        auto const &rootMoves{ Threadpool.mainThread()->rootMoves };
        assert(!rootMoves.empty());

        // RootMoves are already sorted by value in descending order
        i32  weakness{ MAX_PLY - 8 * level };
        i32  deviance{ std::min(rootMoves[0].newValue - rootMoves[PVCount - 1].newValue, VALUE_MG_PAWN) };
        auto bestValue{ -VALUE_INFINITE };
        for (u16 i = 0; i < PVCount; ++i) {
            // First for each move score add two terms, both dependent on weakness.
            // One is deterministic with weakness, and one is random with weakness.
            auto value{ rootMoves[i].newValue
                      + (weakness * i32(rootMoves[0].newValue - rootMoves[i].newValue)
                       + deviance * i32(prng.rand<u32>() % weakness)) / VALUE_MG_PAWN };
            // Then choose the move with the highest value.
            if (bestValue <= value) {
                bestValue = value;
                bestMove = rootMoves[i][0];
            }
        }
    }
    return bestMove;
}
