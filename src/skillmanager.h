#pragma once

#include "type.h"

// MaxLevel should be <= MAX_PLY/9
constexpr u16 MaxLevel{ 25 };

/// Skill Manager class is used to implement strength limit
class SkillManager final {

public:

    SkillManager() noexcept;
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

    u16  level;
    Move bestMove;
};

// Global Skill Manager
extern SkillManager SkillMgr;
