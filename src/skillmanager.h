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

    void clear() noexcept;

    Move pickBestMove() noexcept;

private:

    uint16_t level;
    Move bestMove;
};

// Global Skill Manager
extern SkillManager SkillMgr;
