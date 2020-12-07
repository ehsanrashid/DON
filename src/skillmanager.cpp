#include "skillmanager.h"

#include "searcher.h"
#include "thread.h"
#include "helper/prng.h"

SkillManager SkillMgr;

constexpr SkillManager::SkillManager() noexcept :
    bestMove{ MOVE_NONE },
    level{ MaxLevel } {
}

bool SkillManager::enabled() const noexcept {
    return level < MaxLevel;
}

bool SkillManager::canPick(Depth depth) const noexcept {
    return depth == 1 + level;
}

void SkillManager::setLevel(uint16_t lvl) noexcept {
    level = lvl;
}

/// SkillManager::pickBestMove() chooses best move among a set of RootMoves when playing with a strength handicap,
/// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
Move SkillManager::pickBestMove() noexcept {
    static PRNG prng(now()); // PRNG sequence should be non-deterministic.

    auto const &rootMoves{ Threadpool.mainThread()->rootMoves };
    assert(!rootMoves.empty());

    // RootMoves are already sorted by value in descending order
    int32_t const weakness{ MAX_PLY / 2 - 2 * level };
    int32_t const deviance{ std::min(rootMoves[0].newValue - rootMoves[Threadpool.pvCount - 1].newValue, VALUE_MG_PAWN) };

    auto bestValue{ -VALUE_INFINITE };
    for (uint16_t i = 0; i < Threadpool.pvCount; ++i) {
        // First for each move score add two terms, both dependent on weakness.
        // One is deterministic with weakness, and one is random with weakness.
        auto const value{
            rootMoves[i].newValue
          + (weakness * int32_t(rootMoves[0].newValue - rootMoves[i].newValue)
          +  deviance * int32_t(prng.rand<uint32_t>() % weakness)) / VALUE_MG_PAWN };
        // Then choose the move with the highest value.
        if (bestValue <= value) {
            bestValue = value;
            bestMove = rootMoves[i][0];
        }
    }
    return bestMove;
}
