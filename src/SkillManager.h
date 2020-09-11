#pragma once

#include "Type.h"

// MaxLevel should be <= MAX_PLY/9
constexpr u16 MaxLevel{ 25 };

/// Skill Manager class is used to implement strength limit
class SkillManager {

public:
    SkillManager() = default;
    SkillManager(SkillManager const&) = delete;
    SkillManager(SkillManager&&) = delete;
    SkillManager& operator=(SkillManager const&) = delete;
    SkillManager& operator=(SkillManager&&) = delete;

    bool enabled() const noexcept;
    bool canPick(Depth) const noexcept;

    void setLevel(u16) noexcept;

    void clear() noexcept;

    Move pickBestMove() noexcept;

private:
    u16  level{ MaxLevel };
    Move bestMove{ MOVE_NONE };
};

// Global Skill Manager
extern SkillManager SkillMgr;
