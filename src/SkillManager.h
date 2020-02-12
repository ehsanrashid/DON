#pragma once

#include "Types.h"

// MaxLevel should be <= MaxDepth/9
constexpr i16 MaxLevel = 25;

/// Skill Manager class is used to implement strength limit
class SkillManager
{
private:

public:

    i16  level;
    Move bestMove;

    SkillManager();

    SkillManager(const SkillManager&) = delete;
    SkillManager& operator=(const SkillManager&) = delete;

    bool enabled() const { return level < MaxLevel; }

    void pickBestMove();
};
