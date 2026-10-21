#pragma once

#include "eee/core/Math.hpp"
#include "eee/core/Types.hpp"

#include <array>
#include <string>
#include <vector>

namespace eee::core {

struct ModifierNetState {
    u8 stat = 0;
    f32 value = 0.0f;
    std::string label;
};

struct ItemNetState {
    std::string name;
    std::string baseName;
    u8 slot = 0;
    u8 rarity = 0;
    i32 itemLevel = 0;
    i32 baseDamage = 0;
    i32 baseArmour = 0;
    bool manaFlask = false;
    f32 flaskPower = 0.0f;
    u8 gemSkill = 0;
    u8 gemClass = 0;
    std::vector<ModifierNetState> mods;
};

struct PlayerNetState {
    Vec3 position{};
    f32 yaw = 0.0f;
    f32 health = 0.0f;
    f32 maxHealth = 0.0f;
    f32 mana = 0.0f;
    f32 maxMana = 0.0f;
    f32 jumpHeight = 0.0f;
    f32 attackCooldown = 0.0f;
    f32 qCooldown = 0.0f;
    f32 eCooldown = 0.0f;
    f32 damage = 0.0f;
    f32 armour = 0.0f;
    f32 moveSpeed = 0.0f;
    f32 critChance = 0.0f;
    bool secondaryUnlocked = false;
    i32 level = 1;
    i32 xp = 0;
    i32 gold = 0;
    i32 kills = 0;
    u8 classId = 0;
    bool dead = false;
    std::vector<ItemNetState> inventory;
    std::array<ItemNetState, 8> equipment{};
};

struct EnemyNetState {
    Vec3 position{};
    f32 yaw = 0.0f;
    f32 health = 0.0f;
    f32 maxHealth = 0.0f;
    f32 scale = 1.0f;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    bool alive = true;
    bool boss = false;
    bool ranged = false;
};

struct ProjectileNetState {
    Vec3 position{};
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    f32 radius = 0.2f;
    bool fromPlayer = true;
};

struct LootNetState {
    Vec3 position{};
    u8 rarity = 0;
};

// Server-owned world state replicated to clients, including player inventories and
// equipment (A4b).
struct WorldSnapshot {
    u64 tick = 0;
    i32 zoneIndex = 0;
    bool portalActive = false;
    std::array<PlayerNetState, 2> players{};
    std::vector<EnemyNetState> enemies;
    std::vector<ProjectileNetState> projectiles;
    std::vector<LootNetState> loot;
};

} // namespace eee::core
