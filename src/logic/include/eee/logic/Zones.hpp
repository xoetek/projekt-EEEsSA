#pragma once

#include "eee/core/Math.hpp"

namespace eee::logic {

struct EnemyTypeDef {
    const char* name;
    core::Vec4 color;
    core::f32 health;
    core::f32 damage;
    core::f32 speed;
    core::f32 scale;
    bool ranged;
};

struct ZoneDef {
    const char* name;
    const char* subtitle;
    const char* story;
    core::i32 minLevel;
    core::i32 maxLevel;

    core::Vec4 sky;
    core::Vec4 grassA;
    core::Vec4 grassB;
    core::Vec4 dirtA;
    core::Vec4 dirtB;
    core::Vec4 trunk;
    core::Vec4 foliageA;
    core::Vec4 foliageB;
    core::Vec4 rock;
    core::Vec4 hutBase;
    core::Vec4 hutRoof;
    core::Vec4 lightDirection;
    core::f32 ambient;

    EnemyTypeDef enemies[2];

    const char* bossName;
    const char* bossIntro;
    const char* bossOutro;
    core::Vec4 bossColor;
    core::f32 bossScale;
    core::f32 bossHealthMult;
    core::f32 bossDamageMult;

    const char* npcName;
    const char* npcLine;
};

[[nodiscard]] const ZoneDef& zoneDef(core::i32 index);
[[nodiscard]] core::i32 zoneCount();

} // namespace eee::logic
