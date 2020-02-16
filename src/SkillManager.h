#pragma once

#include "Type.h"

// MaxLevel should be <= MaxDepth/9
constexpr i16 MaxLevel = 25;

/// Skill Manager class is used to implement strength limit
class SkillManager
{
private:

    i16  level;

    Move bestMove{MOVE_NONE};

public:

    SkillManager();
    SkillManager(const SkillManager&) = delete;
    SkillManager& operator=(const SkillManager&) = delete;

    bool enabled() const;
    bool canPick(Depth) const;

    void setLevel(i16);

    void clearBestMove();

    Move pickBestMove();
};
