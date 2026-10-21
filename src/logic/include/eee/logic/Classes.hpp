#pragma once

#include "eee/core/Types.hpp"

namespace eee::logic {

enum class ClassId : core::u8 { Warrior, Archer, Mage, Count };
enum class SkillId : core::u8 {
    None,
    Whirlwind,
    LeapSlam,
    RainOfArrows,
    ShadowDash,
    FrostNova,
    Meteor,
    Count
};

struct SkillDef {
    const char* name;
    core::f32 manaCost;
    core::f32 cooldown;
    core::f32 damage;   // multiplier of weapon damage
    core::f32 radius;
    bool targeted;      // uses the cursor ground point
    bool dash;          // moves the player towards the target
    const char* description;
};

struct ClassDef {
    const char* name;
    const char* title;
    core::f32 health;
    core::f32 mana;
    core::f32 damage;
    core::f32 moveSpeed;
    SkillId primary;
    SkillId secondary;
    bool ranged;
    const char* description;
};

const ClassDef& classDef(ClassId id);
const SkillDef& skillDef(SkillId id);
core::usize classCount();

} // namespace eee::logic
