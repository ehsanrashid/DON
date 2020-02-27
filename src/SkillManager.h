#pragma once

#include "Type.h"

// MaxLevel should be <= MAX_PLY/9
constexpr u16 MaxLevel = 25;

/// Skill Manager class is used to implement strength limit
class SkillManager {

private:

    u16  _level{ MaxLevel };
    Move _bestMove{ MOVE_NONE };

public:

    SkillManager() = default;
    SkillManager(SkillManager const&) = delete;
    SkillManager& operator=(SkillManager const&) = delete;

    bool enabled() const;
    bool canPick(Depth) const;

    void setLevel(u16);

    void clearBestMove();

    Move pickBestMove();
};

// Global Skill Manager
extern SkillManager SkillMgr;
