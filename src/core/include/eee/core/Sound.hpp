#pragma once

#include "eee/core/Types.hpp"

namespace eee::core {

enum class SoundId : u8 {
    Attack,
    RangedAttack,
    Skill,
    Hit,
    EnemyDeath,
    PlayerHurt,
    Pickup,
    LevelUp,
    MenuMove,
    MenuSelect,
    ZoneEnter,
    BossRoar,
    Portal,
    Jump,
    Heal,
    Error,
    Victory,
    Count
};

} // namespace eee::core
