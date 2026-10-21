#include "eee/logic/Classes.hpp"

namespace eee::logic {
namespace {

constexpr ClassDef kClasses[] = {
    {"WARRIOR", "IRONBOUND", 150.0f, 55.0f, 15.0f, 5.0f, SkillId::Whirlwind,
     SkillId::LeapSlam, false,
     "HEAVY ARMOUR AND BRUTAL SWEEPS. CLOSE THE GAP AND CLEAVE."},
    {"ARCHER", "WILDSHOT", 105.0f, 70.0f, 12.0f, 5.6f, SkillId::RainOfArrows,
     SkillId::ShadowDash, true,
     "FAST, FRAGILE AND DEADLY AT RANGE. DANCE OUT OF REACH."},
    {"MAGE", "EMBERSEER", 88.0f, 110.0f, 14.0f, 5.0f, SkillId::FrostNova,
     SkillId::Meteor, true,
     "GLASS AND FIRE. CONTROL SPACE WITH ELEMENTAL MAGIC."},
};

constexpr SkillDef kSkills[] = {
    {"NONE", 0.0f, 1.0f, 0.0f, 0.0f, false, false, ""},
    {"WHIRLWIND", 15.0f, 4.0f, 1.8f, 3.4f, false, false,
     "SPIN THROUGH NEARBY ENEMIES."},
    {"LEAP SLAM", 20.0f, 7.0f, 2.3f, 3.8f, true, true,
     "LEAP TO THE CURSOR AND CRUSH THE LANDING."},
    {"RAIN OF ARROWS", 18.0f, 5.0f, 1.7f, 3.8f, true, false,
     "CALL A VOLLEY DOWN ON THE CURSOR."},
    {"SHADOW DASH", 16.0f, 6.0f, 1.3f, 2.8f, true, true,
     "DASH THROUGH THE CURSOR AND CUT EVERYTHING NEARBY."},
    {"FROST NOVA", 20.0f, 6.0f, 1.4f, 4.2f, false, false,
     "FREEZE AND SLOW ENEMIES AROUND YOU."},
    {"METEOR", 30.0f, 9.0f, 2.9f, 4.5f, true, false,
     "DROP A BURNING STAR ON THE CURSOR."},
};

} // namespace

const ClassDef& classDef(ClassId id) {
    const auto index = static_cast<core::usize>(id);
    return kClasses[index < 3 ? index : 0];
}

const SkillDef& skillDef(SkillId id) {
    const auto index = static_cast<core::usize>(id);
    return kSkills[index < static_cast<core::usize>(SkillId::Count) ? index : 0];
}

core::usize classCount() { return 3; }

} // namespace eee::logic
