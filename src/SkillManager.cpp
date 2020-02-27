#include "SkillManager.h"

#include "PRNG.h"
#include "Searcher.h"
#include "Thread.h"

SkillManager SkillMgr;


bool SkillManager::enabled() const {
    return _level < MaxLevel;
}

bool SkillManager::canPick(Depth depth) const {
    return depth == 1 + _level;
}


void SkillManager::setLevel(u16 lvl) {
    _level = lvl;
}

void SkillManager::clearBestMove() {
    _bestMove = MOVE_NONE;
}


/// SkillManager::pickBestMove() chooses best move among a set of RootMoves when playing with a strength handicap,
/// using a statistical rule dependent on 'level'. Idea by Heinz van Saanen.
Move SkillManager::pickBestMove() {
    static PRNG prng{ u64(now()) }; // PRNG sequence should be non-deterministic.

    if (MOVE_NONE == _bestMove) {
        auto const &rootMoves{ Threadpool.mainThread()->rootMoves };
        assert(!rootMoves.empty());

        // RootMoves are already sorted by value in descending order
        i32  weakness{ MAX_PLY - 8 * _level };
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
                _bestMove = rootMoves[i].front();
            }
        }
    }
    return _bestMove;
}
