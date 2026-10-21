#include "eee/logic/Items.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace eee::logic {
namespace {

struct BaseDef {
    const char* name;
    ItemSlot slot;
    ClassId weaponClass; // Count for non-weapons
    core::i32 damage;
    core::i32 armour;
};

constexpr BaseDef kBases[] = {
    // Warrior weapons
    {"IRON SWORD", ItemSlot::Weapon, ClassId::Warrior, 12, 0},
    {"WAR AXE", ItemSlot::Weapon, ClassId::Warrior, 17, 0},
    {"GREATSWORD", ItemSlot::Weapon, ClassId::Warrior, 23, 0},
    // Archer weapons
    {"SHORT BOW", ItemSlot::Weapon, ClassId::Archer, 10, 0},
    {"RECURVE BOW", ItemSlot::Weapon, ClassId::Archer, 15, 0},
    {"LONGBOW", ItemSlot::Weapon, ClassId::Archer, 21, 0},
    // Mage weapons
    {"APPRENTICE WAND", ItemSlot::Weapon, ClassId::Mage, 9, 0},
    {"OAK STAFF", ItemSlot::Weapon, ClassId::Mage, 14, 0},
    {"RUNED SCEPTER", ItemSlot::Weapon, ClassId::Mage, 19, 0},
    // Armour
    {"LEATHER VEST", ItemSlot::Armour, ClassId::Count, 0, 20},
    {"CHAIN MAIL", ItemSlot::Armour, ClassId::Count, 0, 38},
    {"PLATE ARMOUR", ItemSlot::Armour, ClassId::Count, 0, 58},
    {"LEATHER CAP", ItemSlot::Helmet, ClassId::Count, 0, 10},
    {"IRON HELM", ItemSlot::Helmet, ClassId::Count, 0, 22},
    {"LEATHER BOOTS", ItemSlot::Boots, ClassId::Count, 0, 8},
    {"GREAVES", ItemSlot::Boots, ClassId::Count, 0, 16},
    {"COPPER RING", ItemSlot::Ring, ClassId::Count, 0, 0},
    {"BONE AMULET", ItemSlot::Amulet, ClassId::Count, 0, 0},
    // Flasks
    {"CRIMSON FLASK", ItemSlot::Flask, ClassId::Count, 0, 0},
    {"AZURE FLASK", ItemSlot::Flask, ClassId::Count, 0, 0},
};

constexpr const char* kPrefixes[] = {"RUSTED", "GLEAMING", "GRIM",    "SABLE",   "RUNED",
                                     "WILD",   "BLOODED",  "STORM",   "HOLLOW",  "EMBER",
                                     "FROST",  "SHADOW",   "ANCIENT", "VICIOUS", "SACRED"};
constexpr const char* kSuffixes[] = {"OF FURY",   "OF THE FOX", "OF RUIN",   "OF THE VALE",
                                     "OF ASH",    "OF WINTER",  "OF THE ABYSS", "OF STORMS",
                                     "OF THE CROWN", "OF VENOM"};
constexpr const char* kUniqueWeapons[] = {"DOOMSINGER", "WIDOWMAKER", "STARFALL",
                                          "NIGHTREAPER"};
constexpr const char* kUniqueArmour[] = {"HEART OF THE VALE", "THE LAST WARDEN",
                                         "CROWN OF EMBERS", "SHROUD OF ECHOES"};
constexpr const char* kUniqueJewelry[] = {"BINDING OF THE SUNDERED", "OATH OF THE FALLEN",
                                          "TEAR OF THE FIRST DAWN"};

core::f32 randomFloat(core::u32& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<core::f32>(state >> 8) * (1.0f / 16777216.0f);
}


Modifier rollModifier(core::u32& state, core::i32 itemLevel, StatId stat) {
    const core::f32 r = randomFloat(state);
    Modifier mod;
    mod.stat = stat;
    char label[64] = {};

    switch (stat) {
    case StatId::FlatDamage:
        mod.value = 2.0f + static_cast<core::f32>(itemLevel) * 0.5f + r * 5.0f;
        std::snprintf(label, sizeof(label), "+%d DAMAGE", static_cast<int>(mod.value));
        break;
    case StatId::PercentDamage:
        mod.value = 5.0f + r * 18.0f;
        std::snprintf(label, sizeof(label), "+%d%% DAMAGE", static_cast<int>(mod.value));
        break;
    case StatId::Life:
        mod.value = 8.0f + static_cast<core::f32>(itemLevel) * 1.5f + r * 12.0f;
        std::snprintf(label, sizeof(label), "+%d LIFE", static_cast<int>(mod.value));
        break;
    case StatId::Mana:
        mod.value = 6.0f + static_cast<core::f32>(itemLevel) * 1.0f + r * 8.0f;
        std::snprintf(label, sizeof(label), "+%d MANA", static_cast<int>(mod.value));
        break;
    case StatId::Armour:
        mod.value = 5.0f + static_cast<core::f32>(itemLevel) * 1.5f + r * 10.0f;
        std::snprintf(label, sizeof(label), "+%d ARMOUR", static_cast<int>(mod.value));
        break;
    case StatId::MoveSpeed:
        mod.value = 2.0f + r * 6.0f;
        std::snprintf(label, sizeof(label), "+%d%% MOVE SPEED", static_cast<int>(mod.value));
        break;
    case StatId::CritChance:
        mod.value = 1.0f + r * 5.0f;
        std::snprintf(label, sizeof(label), "+%d%% CRIT CHANCE", static_cast<int>(mod.value));
        break;
    case StatId::LifeRegen:
        mod.value = 0.5f + r * 1.5f;
        std::snprintf(label, sizeof(label), "+%.1f LIFE REGEN", static_cast<double>(mod.value));
        break;
    case StatId::ManaRegen:
        mod.value = 0.5f + r * 1.5f;
        std::snprintf(label, sizeof(label), "+%.1f MANA REGEN", static_cast<double>(mod.value));
        break;
    case StatId::SkillDamage:
        mod.value = 5.0f + r * 25.0f;
        std::snprintf(label, sizeof(label), "+%d%% SKILL DAMAGE", static_cast<int>(mod.value));
        break;
    default:
        break;
    }
    mod.label = label;
    return mod;
}

StatId randomStat(core::u32& state) {
    constexpr std::array<StatId, 10> kStats = {
        StatId::FlatDamage, StatId::PercentDamage, StatId::Life,       StatId::Mana,
        StatId::Armour,     StatId::MoveSpeed,     StatId::CritChance, StatId::LifeRegen,
        StatId::ManaRegen,  StatId::SkillDamage,
    };
    return kStats[static_cast<core::usize>(randomFloat(state) * kStats.size()) % kStats.size()];
}

bool hasStat(const Item& item, StatId stat) {
    for (const Modifier& mod : item.mods) {
        if (mod.stat == stat) {
            return true;
        }
    }
    return false;
}

} // namespace

void ItemTotals::add(const Item& item) {
    weaponDamage += item.baseDamage;
    armour += static_cast<core::f32>(item.baseArmour);
    for (const Modifier& mod : item.mods) {
        switch (mod.stat) {
        case StatId::FlatDamage: flatDamage += mod.value; break;
        case StatId::PercentDamage: percentDamage += mod.value; break;
        case StatId::Life: life += mod.value; break;
        case StatId::Mana: mana += mod.value; break;
        case StatId::Armour: armour += mod.value; break;
        case StatId::MoveSpeed: moveSpeed += mod.value; break;
        case StatId::CritChance: critChance += mod.value; break;
        case StatId::LifeRegen: lifeRegen += mod.value; break;
        case StatId::ManaRegen: manaRegen += mod.value; break;
        case StatId::SkillDamage: skillDamage += mod.value; break;
        default: break;
        }
    }
}

Item generateSkillGem(core::u32& rng, core::i32 itemLevel, ClassId classId) {
    Item item;
    item.id = 1;
    item.slot = ItemSlot::SkillGem;
    item.rarity = Rarity::Magic;
    item.itemLevel = itemLevel;
    item.gemSkill = classDef(classId).secondary;
    item.gemClass = classId;
    item.baseName = skillDef(item.gemSkill).name;
    item.name = item.baseName;
    item.mods.push_back(Modifier{StatId::SkillDamage, 10.0f + randomFloat(rng) * 20.0f,
                                 "+15% SKILL DAMAGE"});
    (void)rng;
    return item;
}

Item generateItem(core::u32& rng, core::i32 itemLevel, ClassId classId) {
    Item item;
    item.id = 1;
    item.itemLevel = itemLevel;

    // Pick a base: 45% weapons (class-appropriate), 45% armour/jewellery,
    // 10% flasks.
    const core::f32 roll = randomFloat(rng);
    std::vector<const BaseDef*> candidates;
    if (roll < 0.45f) {
        for (const BaseDef& base : kBases) {
            if (base.slot == ItemSlot::Weapon && base.weaponClass == classId) {
                candidates.push_back(&base);
            }
        }
    } else if (roll < 0.90f) {
        for (const BaseDef& base : kBases) {
            if (base.slot == ItemSlot::Armour || base.slot == ItemSlot::Helmet ||
                base.slot == ItemSlot::Boots || base.slot == ItemSlot::Ring ||
                base.slot == ItemSlot::Amulet) {
                candidates.push_back(&base);
            }
        }
    } else {
        for (const BaseDef& base : kBases) {
            if (base.slot == ItemSlot::Flask) {
                candidates.push_back(&base);
            }
        }
    }

    const BaseDef& base = *candidates[static_cast<core::usize>(randomFloat(rng) * candidates.size()) %
                                    candidates.size()];
    item.baseName = base.name;
    item.slot = base.slot;
    item.baseDamage = base.damage > 0
                          ? base.damage + static_cast<core::i32>(static_cast<core::f32>(itemLevel) *
                                                                 0.8f)
                          : 0;
    item.baseArmour = base.armour > 0
                          ? base.armour + static_cast<core::i32>(static_cast<core::f32>(itemLevel) *
                                                                 1.1f)
                          : 0;
    if (item.slot == ItemSlot::Flask) {
        item.manaFlask = item.baseName.find("AZURE") != std::string::npos;
        item.flaskPower = 0.45f;
    }

    const core::f32 rarityRoll = randomFloat(rng);
    item.rarity = rarityRoll < 0.45f ? Rarity::Normal
                 : rarityRoll < 0.80f ? Rarity::Magic
                 : rarityRoll < 0.98f ? Rarity::Rare
                                      : Rarity::Unique;

    core::i32 modCount = 0;
    switch (item.rarity) {
    case Rarity::Normal: modCount = 0; break;
    case Rarity::Magic: modCount = 1 + (randomFloat(rng) < 0.5f ? 1 : 0); break;
    case Rarity::Rare: modCount = 3 + (randomFloat(rng) < 0.5f ? 1 : 0); break;
    case Rarity::Unique: modCount = 4; break;
    }

    for (core::i32 i = 0; i < modCount; ++i) {
        StatId stat = randomStat(rng);
        for (core::i32 attempt = 0; attempt < 6 && hasStat(item, stat); ++attempt) {
            stat = randomStat(rng);
        }
        item.mods.push_back(rollModifier(rng, itemLevel, stat));
    }

    if (item.rarity == Rarity::Unique) {
        const auto index = static_cast<core::usize>(randomFloat(rng) * 3.0f);
        const char* unique = nullptr;
        if (item.slot == ItemSlot::Weapon) {
            unique = kUniqueWeapons[static_cast<core::usize>(randomFloat(rng) * 4.0f) % 4];
        } else if (item.slot == ItemSlot::Armour || item.slot == ItemSlot::Helmet ||
                   item.slot == ItemSlot::Boots) {
            unique = kUniqueArmour[static_cast<core::usize>(randomFloat(rng) * 4.0f) % 4];
        } else {
            unique = kUniqueJewelry[static_cast<core::usize>(randomFloat(rng) * 3.0f) % 3];
        }
        item.name = unique;
        item.baseName = unique;
        (void)index;
    } else if (item.rarity == Rarity::Rare) {
        item.name = std::string(kPrefixes[static_cast<core::usize>(randomFloat(rng) * 15.0f) % 15]) +
                    " " + base.name + " " +
                    kSuffixes[static_cast<core::usize>(randomFloat(rng) * 10.0f) % 10];
    } else if (item.rarity == Rarity::Magic) {
        if (randomFloat(rng) < 0.5f) {
            item.name = std::string(kPrefixes[static_cast<core::usize>(randomFloat(rng) * 15.0f) % 15]) +
                        " " + base.name;
        } else {
            item.name = std::string(base.name) + " " +
                        kSuffixes[static_cast<core::usize>(randomFloat(rng) * 10.0f) % 10];
        }
    } else {
        item.name = base.name;
    }

    return item;
}

const char* slotName(ItemSlot slot) {
    switch (slot) {
    case ItemSlot::Weapon: return "WEAPON";
    case ItemSlot::Armour: return "ARMOUR";
    case ItemSlot::Helmet: return "HELMET";
    case ItemSlot::Boots: return "BOOTS";
    case ItemSlot::Ring: return "RING";
    case ItemSlot::Amulet: return "AMULET";
    case ItemSlot::Flask: return "FLASK";
    case ItemSlot::SkillGem: return "SKILL GEM";
    default: return "UNKNOWN";
    }
}

const char* rarityName(Rarity rarity) {
    switch (rarity) {
    case Rarity::Normal: return "NORMAL";
    case Rarity::Magic: return "MAGIC";
    case Rarity::Rare: return "RARE";
    case Rarity::Unique: return "UNIQUE";
    }
    return "";
}

} // namespace eee::logic
