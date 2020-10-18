#pragma once

#include "type.h"

// MaxLevel should be <= MAX_PLY / 12
constexpr uint16_t MaxLevel{ 20 };

/// Skill Manager class is used to implement strength limit
class SkillManager final {

public:

    constexpr SkillManager() noexcept;
    SkillManager(SkillManager const&) = delete;
    SkillManager(SkillManager&&) = delete;

    SkillManager& operator=(SkillManager const&) = delete;
    SkillManager& operator=(SkillManager&&) = delete;

    bool enabled() const noexcept;
    bool canPick(Depth) const noexcept;

    void setLevel(uint16_t) noexcept;

    Move pickBestMove() noexcept;

    Move bestMove;

private:

    uint16_t level;
};

// Global Skill Manager
extern SkillManager SkillMgr;
