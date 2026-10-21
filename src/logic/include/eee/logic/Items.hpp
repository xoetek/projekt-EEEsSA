#pragma once

#include "eee/core/Math.hpp"
#include "eee/core/Types.hpp"
#include "eee/logic/Classes.hpp"

#include <string>
#include <vector>

namespace eee::logic {

enum class ItemSlot : core::u8 {
    Weapon,
    Armour,
    Helmet,
    Boots,
    Ring,
    Amulet,
    Flask,
    SkillGem,
    Count
};

enum class Rarity : core::u8 { Normal, Magic, Rare, Unique };

enum class StatId : core::u8 {
    FlatDamage,
    PercentDamage,
    Life,
    Mana,
    Armour,
    MoveSpeed,
    CritChance,
    LifeRegen,
    ManaRegen,
    SkillDamage,
    Count
};

struct Modifier {
    StatId stat = StatId::FlatDamage;
    core::f32 value = 0.0f;
    std::string label;
};

struct Item {
    core::u32 id = 0;
    std::string name;
    std::string baseName;
    ItemSlot slot = ItemSlot::Weapon;
    Rarity rarity = Rarity::Normal;
    core::i32 itemLevel = 1;
    core::i32 baseDamage = 0;
    core::i32 baseArmour = 0;
    bool manaFlask = false;
    core::f32 flaskPower = 0.0f;
    SkillId gemSkill = SkillId::None;
    ClassId gemClass = ClassId::Count;
    std::vector<Modifier> mods;

    [[nodiscard]] bool empty() const { return id == 0; }
};

struct ItemTotals {
    core::f32 flatDamage = 0.0f;
    core::f32 percentDamage = 0.0f;
    core::f32 life = 0.0f;
    core::f32 mana = 0.0f;
    core::f32 armour = 0.0f;
    core::f32 moveSpeed = 0.0f;
    core::f32 critChance = 0.0f;
    core::f32 lifeRegen = 0.0f;
    core::f32 manaRegen = 0.0f;
    core::f32 skillDamage = 0.0f;
    core::i32 weaponDamage = 0;

    void add(const Item& item);
};

[[nodiscard]] Item generateItem(core::u32& rng, core::i32 itemLevel, ClassId classId);
[[nodiscard]] Item generateSkillGem(core::u32& rng, core::i32 itemLevel, ClassId classId);
[[nodiscard]] const char* slotName(ItemSlot slot);
[[nodiscard]] const char* rarityName(Rarity rarity);

} // namespace eee::logic
