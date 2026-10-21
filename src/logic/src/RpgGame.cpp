#include "eee/logic/RpgGame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace eee::logic {
namespace {

constexpr core::f32 kPi = 3.14159265359f;
constexpr core::f32 kTwoPi = 6.28318530718f;
constexpr core::f32 kBounds = 48.0f;
constexpr core::f32 kPlayerRadius = 0.45f;
constexpr core::f32 kFovY = 1.0471975512f;
constexpr core::i32 kMaxLevel = 80;
constexpr core::usize kInventorySize = 24;

constexpr std::string_view kPausedOptions[] = {"RESUME", "GRAPHICS", "RESTART", "CHANGE CLASS", "QUIT"};
constexpr std::string_view kDeadOptions[] = {"RESPAWN AT FIRE", "CHANGE CLASS", "QUIT"};
constexpr std::string_view kVictoryOptions[] = {"NEW JOURNEY", "CHANGE CLASS", "QUIT"};

core::Vec4 mix(core::Vec4 a, core::Vec4 b, core::f32 t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t};
}

core::Vec3 lerpVec3(core::Vec3 a, core::Vec3 b, core::f32 t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

core::f32 lerpFloat(core::f32 a, core::f32 b, core::f32 t) { return a + (b - a) * t; }

core::Vec4 rarityColor(Rarity rarity) {
    switch (rarity) {
    case Rarity::Normal: return {0.85f, 0.85f, 0.85f, 1.0f};
    case Rarity::Magic: return {0.30f, 0.55f, 1.00f, 1.0f};
    case Rarity::Rare: return {1.00f, 0.82f, 0.25f, 1.0f};
    case Rarity::Unique: return {1.00f, 0.45f, 0.15f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

core::usize itemSlotIndex(ItemSlot slot) { return static_cast<core::usize>(slot); }

} // namespace

RpgGame::RpgGame() {
    mode_ = UiMode::ClassSelect;
    loadZone(0);
    sounds_.clear();
}

core::f32 RpgGame::randomFloat() {
    core::u32 x = randomState_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    randomState_ = x;
    return static_cast<core::f32>(x >> 8) * (1.0f / 16777216.0f);
}

core::f32 RpgGame::wrapAngle(core::f32 angle) {
    while (angle > kPi) {
        angle -= kTwoPi;
    }
    while (angle < -kPi) {
        angle += kTwoPi;
    }
    return angle;
}

core::i32 RpgGame::xpToNextFor(core::i32 level) {
    return 40 + level * 30 + level * level;
}

const ZoneDef& RpgGame::zone() const { return zoneDef(zoneIndex_); }

const Item& RpgGame::equipped(ItemSlot slot) const {
    return equipped(localViewer_, slot);
}

const Item& RpgGame::equipped(core::PlayerId id, ItemSlot slot) const {
    return players_[static_cast<core::usize>(id)].loadout.equipment[itemSlotIndex(slot)];
}

const EnemyState* RpgGame::boss() const {
    for (const EnemyState& enemy : enemies_) {
        if (enemy.boss) {
            return &enemy;
        }
    }
    return nullptr;
}

std::vector<core::SoundId> RpgGame::takeSoundEvents() {
    std::vector<core::SoundId> events;
    events.swap(sounds_);
    return events;
}

void RpgGame::emit(core::SoundId sound) { sounds_.push_back(sound); }

void RpgGame::setMessage(std::string text) {
    message_ = std::move(text);
    messageTimer_ = 3.0f;
}

void RpgGame::startRun(ClassId classId) {
    players_[0] = PlayerState{};
    players_[1] = PlayerState{};
    activePlayers_ = 1;
    localViewer_ = core::PlayerId::P0;
    activeId_ = core::PlayerId::P0;
    active().classId = classId;
    active().loadout.inventory.clear();
    active().loadout.equipment.fill(Item{});
    active().loadout.totals = ItemTotals{};
    active().loadout.secondaryUnlocked = false;
    itemId_ = 0;
    randomState_ = 0xC0FFEEu;
    time_ = 0.0f;
    active().loadout.level = 1;
    active().loadout.xp = 0;
    active().loadout.xpToNext = xpToNextFor(1);
    recomputeStats();
    active().health = active().maxHealth;
    active().mana = active().maxMana;
    mode_ = UiMode::Playing;
    menuSelection_ = 0;
    inventorySelection_ = 0;
    quitRequested_ = false;
    message_.clear();
    messageTimer_ = 0.0f;
    loadZone(0);
    emit(core::SoundId::MenuSelect);
}

void RpgGame::beginCoop(ClassId p0Class, ClassId p1Class) {
    players_[0] = PlayerState{};
    players_[1] = PlayerState{};
    activePlayers_ = 2;
    localViewer_ = core::PlayerId::P0;
    activeId_ = core::PlayerId::P0;
    players_[0].classId = p0Class;
    players_[1].classId = p1Class;
    itemId_ = 0;
    randomState_ = 0xC0FFEEu;
    time_ = 0.0f;

    for (core::u8 i = 0; i < 2; ++i) {
        activeId_ = static_cast<core::PlayerId>(i);
        active().loadout.level = 1;
        active().loadout.xp = 0;
        active().loadout.xpToNext = xpToNextFor(1);
        recomputeStats();
        active().health = active().maxHealth;
        active().mana = active().maxMana;
    }
    activeId_ = core::PlayerId::P0;
    mode_ = UiMode::Playing;
    menuSelection_ = 0;
    inventorySelection_ = 0;
    quitRequested_ = false;
    message_.clear();
    messageTimer_ = 0.0f;
    loadZone(0);
    emit(core::SoundId::MenuSelect);
}

void RpgGame::applyLocalCameraInput(core::f32 dt, const core::InputState& input,
                                    core::PlayerId id) {
    activeId_ = id;
    updateLocalCamera(dt, input);
    updateCameraFollow(dt);
    activeId_ = localViewer_;
}

void RpgGame::restartRun() {
    const ClassId classId = active().classId;
    startRun(classId);
}

void RpgGame::changeClass() {
    mode_ = UiMode::ClassSelect;
    menuSelection_ = 0;
    dialogueTimer_ = 0.0f;
    bannerTimer_ = 0.0f;
    emit(core::SoundId::MenuSelect);
}

void RpgGame::loadZone(core::i32 index) {
    zoneIndex_ = std::clamp(index, 0, zoneCount() - 1);
    const ZoneDef& def = zone();

    buildStaticWorld();
    spawnNpcs();
    spawnEnemies();

    loot_.clear();
    projectiles_.clear();
    effects_.clear();
    dialogueTimer_ = 0.0f;
    interactionHint_.clear();

    for (core::u8 i = 0; i < 2; ++i) {
        PlayerState& player = players_[i];
        player.position = {0.0f, 0.0f, 4.0f};
        player.respawnPoint = player.position;
        player.yaw = 0.0f;
        player.jumpHeight = 0.0f;
        player.verticalVelocity = 0.0f;
        player.grounded = true;
        player.playerDead = false;
        player.camera.yaw = 0.0f;
        player.camera.pitch = 0.36f;
        player.camera.distance = 6.2f;
        player.camera.smoothedDistance = 6.2f;
        player.camera.target = player.position + core::Vec3{0.0f, 1.55f, 0.0f};
    }
    activeId_ = core::PlayerId::P0;

    portalActive_ = false;
    bossIntroShown_ = false;

    bannerTitle_ = def.name;
    bannerText_ = def.story;
    bannerTimer_ = 8.0f;

    emit(core::SoundId::ZoneEnter);
}

void RpgGame::buildStaticWorld() {
    staticMeshes_.clear();
    obstacles_.clear();
    staticMeshes_.reserve(11000);
    const ZoneDef& def = zone();

    // Invariant: the same zone index produces the same static world on the
    // authoritative server and on every client. Static props are presentation
    // and collision data, so derive their RNG from the zone index instead of
    // consuming the simulation RNG. A client that applies a snapshot for a new
    // zone must not depend on its own simulation history to rebuild collision.
    constexpr core::u32 kStaticWorldSeed = 0x9E3779B9u;
    constexpr core::u32 kStaticWorldZoneStep = 2654435761u;
    const core::u32 savedRandomState = randomState_;
    randomState_ =
        kStaticWorldSeed ^ (static_cast<core::u32>(zoneIndex_) * kStaticWorldZoneStep);

    constexpr core::i32 kHalf = 56;
    for (core::i32 z = -kHalf; z <= kHalf; ++z) {
        for (core::i32 x = -kHalf; x <= kHalf; ++x) {
            const bool path = std::abs(x) <= 1 || std::abs(z) <= 1;
            const core::u32 hash =
                static_cast<core::u32>(x * 73856093) ^ static_cast<core::u32>(z * 19349663);
            const core::f32 noise = static_cast<core::f32>(hash & 1023u) / 1023.0f;
            const core::Vec4 grass = mix(def.grassA, def.grassB, noise);
            const core::Vec4 dirt = mix(def.dirtA, def.dirtB, noise);
            addBox(staticMeshes_, {static_cast<core::f32>(x), -0.1f, static_cast<core::f32>(z)},
                   {0.98f, 0.2f, 0.98f}, path ? dirt : grass);
        }
    }

    auto blocked = [&](core::f32 x, core::f32 z, core::f32 clearance) {
        if (x * x + (z - 4.0f) * (z - 4.0f) < 6.0f * 6.0f) {
            return true;
        }
        if (x * x + (z + 16.0f) * (z + 16.0f) < (7.0f + clearance) * (7.0f + clearance)) {
            return true;
        }
        if (x * x + (z + 22.0f) * (z + 22.0f) < (4.0f + clearance) * (4.0f + clearance)) {
            return true;
        }
        return false;
    };

    core::i32 trees = 0;
    while (trees < 180) {
        const core::f32 x = randomFloat() * 92.0f - 46.0f;
        const core::f32 z = randomFloat() * 92.0f - 46.0f;
        if (std::abs(x) < 3.0f || std::abs(z) < 3.0f || blocked(x, z, 0.0f)) {
            continue;
        }
        const core::f32 size = 0.8f + randomFloat() * 0.8f;
        addBox(staticMeshes_, {x, 1.0f, z}, {0.35f, 2.0f, 0.35f}, def.trunk);
        addBox(staticMeshes_, {x, 2.5f + size * 0.2f, z},
               {1.4f + size, 1.6f + size, 1.4f + size},
               mix(def.foliageA, def.foliageB, randomFloat()));
        obstacles_.push_back({{x, 0.0f, z}, 0.55f});
        ++trees;
    }

    for (core::i32 i = 0; i < 100; ++i) {
        const core::f32 x = randomFloat() * 92.0f - 46.0f;
        const core::f32 z = randomFloat() * 92.0f - 46.0f;
        if (std::abs(x) < 3.0f || std::abs(z) < 3.0f || blocked(x, z, 0.0f)) {
            continue;
        }
        const core::f32 size = 0.4f + randomFloat() * 1.1f;
        const core::f32 shade = 0.65f + randomFloat() * 0.35f;
        addBox(staticMeshes_, {x, size * 0.5f, z}, {size, size, size},
               {def.rock.x * shade, def.rock.y * shade, def.rock.z * shade, 1.0f},
               {0.0f, randomFloat() * kTwoPi, 0.0f});
        obstacles_.push_back({{x, 0.0f, z}, size * 0.55f});
    }

    for (core::i32 i = 0; i < 4; ++i) {
        const core::f32 angle = kPi * 0.25f + static_cast<core::f32>(i) * kPi * 0.5f;
        const core::f32 x = std::cos(angle) * 13.0f;
        const core::f32 z = std::sin(angle) * 13.0f;
        addBox(staticMeshes_, {x, 1.2f, z}, {3.0f, 2.4f, 3.0f}, def.hutBase,
               {0.0f, -angle, 0.0f});
        addBox(staticMeshes_, {x, 2.65f, z}, {3.8f, 0.6f, 3.8f}, def.hutRoof,
               {0.0f, -angle, 0.0f});
        // Door and window.
        addBox(staticMeshes_, {x - std::cos(angle) * 1.52f, 0.75f, z - std::sin(angle) * 1.52f},
               {0.7f, 1.4f, 0.12f}, {0.12f, 0.08f, 0.06f, 1.0f}, {0.0f, -angle, 0.0f});
        addBox(staticMeshes_, {x - std::cos(angle) * 1.52f, 1.75f, z - std::sin(angle) * 1.52f},
               {0.7f, 0.5f, 0.12f}, {0.85f, 0.65f, 0.25f, 1.0f}, {0.0f, -angle, 0.0f});
        obstacles_.push_back({{x, 0.0f, z}, 2.2f});
    }

    // Campfire / checkpoint.
    firePosition_ = {0.0f, 0.0f, 0.0f};
    for (core::i32 i = 0; i < 6; ++i) {
        const core::f32 angle = static_cast<core::f32>(i) / 6.0f * kTwoPi;
        addBox(staticMeshes_, {std::cos(angle) * 0.75f, 0.12f, std::sin(angle) * 0.75f},
               {0.28f, 0.24f, 0.28f}, def.rock, {0.0f, angle, 0.0f});
    }
    addBox(staticMeshes_, {0.0f, 0.10f, 0.0f}, {1.00f, 0.18f, 0.18f},
           {0.28f, 0.18f, 0.10f, 1.0f}, {0.0f, 0.7f, 0.0f});
    addBox(staticMeshes_, {0.0f, 0.10f, 0.0f}, {1.00f, 0.18f, 0.18f},
           {0.28f, 0.18f, 0.10f, 1.0f}, {0.0f, -0.7f, 0.0f});
    obstacles_.push_back({{0.0f, 0.0f, 0.0f}, 0.55f});

    // Village well and fence ring.
    for (core::i32 i = 0; i < 8; ++i) {
        const core::f32 angle = static_cast<core::f32>(i) / 8.0f * kTwoPi;
        addBox(staticMeshes_, {std::cos(angle) * 1.1f, 0.35f, -7.0f + std::sin(angle) * 1.1f},
               {0.55f, 0.7f, 0.55f}, def.rock, {0.0f, angle, 0.0f});
    }
    addBox(staticMeshes_, {0.0f, 0.75f, -7.0f}, {1.7f, 0.08f, 1.7f},
           {0.20f, 0.45f, 0.65f, 1.0f});
    obstacles_.push_back({{0.0f, 0.0f, -7.0f}, 1.0f});
    for (core::i32 i = 0; i < 18; ++i) {
        const core::f32 angle = static_cast<core::f32>(i) / 18.0f * kTwoPi;
        const core::f32 x = std::cos(angle) * 17.0f;
        const core::f32 z = std::sin(angle) * 17.0f;
        addBox(staticMeshes_, {x, 0.55f, z}, {0.20f, 1.1f, 0.20f}, def.trunk);
        addBox(staticMeshes_, {std::cos(angle + 0.18f) * 17.0f, 0.85f,
                               std::sin(angle + 0.18f) * 17.0f},
               {0.10f, 0.10f, 1.4f}, def.trunk, {0.0f, -angle, 0.0f});
        obstacles_.push_back({{x, 0.0f, z}, 0.22f});
    }

    // Biome-specific landmarks.
    if (zoneIndex_ == 0) {
        for (core::i32 i = 0; i < 50; ++i) {
            const core::f32 x = randomFloat() * 90.0f - 45.0f;
            const core::f32 z = randomFloat() * 90.0f - 45.0f;
            if (blocked(x, z, 0.0f)) {
                continue;
            }
            addBox(staticMeshes_, {x, 0.12f, z}, {0.18f, 0.24f, 0.18f},
                   mix({0.85f, 0.35f, 0.45f, 1.0f}, {0.95f, 0.85f, 0.35f, 1.0f}, randomFloat()));
        }
    } else if (zoneIndex_ == 1) {
        for (core::i32 i = 0; i < 24; ++i) {
            const core::f32 x = randomFloat() * 96.0f - 48.0f;
            const core::f32 z = randomFloat() * 96.0f - 48.0f;
            if (blocked(x, z, 0.0f)) {
                continue;
            }
            addBox(staticMeshes_, {x, 1.3f, z}, {0.35f, 2.6f, 0.35f}, {0.18f, 0.12f, 0.09f, 1.0f},
                   {0.0f, randomFloat() * kTwoPi, 0.0f});
            addBox(staticMeshes_, {x + 0.5f, 2.0f, z}, {0.12f, 0.12f, 1.3f},
                   {0.18f, 0.12f, 0.09f, 1.0f}, {0.3f, randomFloat(), 0.4f});
            obstacles_.push_back({{x, 0.0f, z}, 0.4f});
        }
    } else if (zoneIndex_ == 2) {
        for (core::i32 i = 0; i < 55; ++i) {
            const core::f32 x = randomFloat() * 96.0f - 48.0f;
            const core::f32 z = randomFloat() * 96.0f - 48.0f;
            if (blocked(x, z, 0.0f)) {
                continue;
            }
            const core::f32 height = 2.0f + randomFloat() * 5.0f;
            addBox(staticMeshes_, {x, height * 0.5f, z}, {0.4f, height, 0.4f},
                   mix({0.45f, 0.75f, 0.95f, 1.0f}, {0.80f, 0.95f, 1.0f, 1.0f}, randomFloat()),
                   {0.05f, randomFloat(), 0.05f});
            obstacles_.push_back({{x, 0.0f, z}, 0.35f});
        }
    } else if (zoneIndex_ == 3) {
        for (core::i32 i = 0; i < 36; ++i) {
            const core::f32 x = randomFloat() * 96.0f - 48.0f;
            const core::f32 z = randomFloat() * 96.0f - 48.0f;
            if (blocked(x, z, 0.0f)) {
                continue;
            }
            const core::f32 height = 2.5f + randomFloat() * 4.0f;
            addBox(staticMeshes_, {x, height * 0.5f, z}, {0.8f, height, 0.8f},
                   {0.20f, 0.34f, 0.32f, 1.0f}, {0.0f, randomFloat() * 0.4f, 0.0f});
            addBox(staticMeshes_, {x, height + 0.25f, z}, {1.2f, 0.5f, 1.2f},
                   {0.16f, 0.28f, 0.26f, 1.0f}, {0.0f, randomFloat(), 0.0f});
            obstacles_.push_back({{x, 0.0f, z}, 0.7f});
        }
    } else if (zoneIndex_ == 4) {
        for (core::i32 i = 0; i < 30; ++i) {
            const core::f32 x = randomFloat() * 96.0f - 48.0f;
            const core::f32 z = randomFloat() * 96.0f - 48.0f;
            if (blocked(x, z, 0.0f)) {
                continue;
            }
            const core::f32 height = 3.0f + randomFloat() * 3.0f;
            addBox(staticMeshes_, {x, height * 0.5f, z}, {0.7f, height, 0.7f},
                   {0.24f, 0.12f, 0.38f, 1.0f}, {0.0f, 0.785f, 0.0f});
            addBox(staticMeshes_, {x, height + 0.35f, z}, {0.36f, 0.7f, 0.36f},
                   {0.65f, 0.30f, 0.85f, 1.0f}, {0.0f, 1.57f, 0.0f});
            obstacles_.push_back({{x, 0.0f, z}, 0.5f});
        }
    } else {
        for (core::i32 i = 0; i < 72; ++i) {
            const core::f32 x = randomFloat() * 96.0f - 48.0f;
            const core::f32 z = randomFloat() * 96.0f - 48.0f;
            if (blocked(x, z, 0.0f)) {
                continue;
            }
            addBox(staticMeshes_, {x, 0.03f, z}, {0.9f, 0.03f, 0.9f},
                   mix({0.55f, 0.10f, 0.05f, 1.0f}, {1.0f, 0.35f, 0.05f, 1.0f}, randomFloat()));
        }
        for (core::i32 i = 0; i < 26; ++i) {
            const core::f32 x = randomFloat() * 96.0f - 48.0f;
            const core::f32 z = randomFloat() * 96.0f - 48.0f;
            if (blocked(x, z, 0.0f)) {
                continue;
            }
            const core::f32 height = 4.0f + randomFloat() * 4.0f;
            addBox(staticMeshes_, {x, height * 0.5f, z}, {1.1f, height, 1.1f},
                   {0.07f, 0.06f, 0.08f, 1.0f}, {0.0f, 0.785f, 0.0f});
            addBox(staticMeshes_, {x, height + 0.3f, z}, {0.7f, 0.6f, 0.7f},
                   {0.85f, 0.12f, 0.10f, 1.0f}, {0.0f, 1.57f, 0.0f});
            obstacles_.push_back({{x, 0.0f, z}, 0.9f});
        }
    }

    randomState_ = savedRandomState;
}

void RpgGame::spawnNpcs() {
    npcs_.clear();
    const ZoneDef& def = zone();
    NpcState npc;
    npc.position = {5.0f, 0.0f, 1.5f};
    npc.yaw = std::atan2(-npc.position.x, -npc.position.z);
    npc.robe = {0.35f, 0.5f, 0.8f, 1.0f};
    npc.hat = {0.8f, 0.75f, 0.4f, 1.0f};
    npc.name = def.npcName;
    npc.line = def.npcLine;
    npcs_.push_back(npc);
    obstacles_.push_back({{npc.position.x, 0.0f, npc.position.z}, 0.6f});
}

void RpgGame::spawnEnemies() {
    enemies_.clear();
    const ZoneDef& def = zone();
    const core::i32 range = std::max(1, def.maxLevel - def.minLevel + 1);
    const core::i32 count = 14 + zoneIndex_ * 2;

    for (core::i32 i = 0; i < count; ++i) {
        const EnemyTypeDef& type = def.enemies[i % 3 == 0 ? 1 : 0];
        EnemyState enemy;
        enemy.name = type.name;
        enemy.color = type.color;
        enemy.scale = type.scale;
        enemy.ranged = type.ranged;
        enemy.level = def.minLevel + (i % range);
        enemy.maxHealth = type.health * (1.0f + static_cast<core::f32>(enemy.level) * 0.22f);
        enemy.health = enemy.maxHealth;
        enemy.damage = type.damage * (1.0f + static_cast<core::f32>(enemy.level) * 0.10f);
        enemy.speed = type.speed;
        enemy.bob = static_cast<core::f32>(i) * 0.7f;
        enemy.fireCooldown = 0.8f + randomFloat();

        const core::f32 angle = randomFloat() * kTwoPi;
        const core::f32 radius = 9.0f + randomFloat() * 26.0f;
        enemy.home = {std::cos(angle) * radius, 0.0f, std::sin(angle) * radius};
        enemy.position = enemy.home;
        enemy.yaw = std::atan2(-enemy.position.x, -enemy.position.z);
        resolveObstacles(enemy.position, enemy.scale * 0.5f);
        enemies_.push_back(enemy);
    }

    EnemyState boss;
    boss.name = def.bossName;
    boss.color = def.bossColor;
    boss.scale = def.bossScale * 2.0f;
    boss.level = def.maxLevel;
    boss.maxHealth = (60.0f + static_cast<core::f32>(def.maxLevel) * 10.0f) *
                     def.bossHealthMult;
    boss.health = boss.maxHealth;
    boss.damage = def.enemies[0].damage *
                  (1.0f + static_cast<core::f32>(def.maxLevel) * 0.12f) * def.bossDamageMult;
    boss.speed = 2.2f;
    boss.boss = true;
    boss.ranged = false;
    boss.bob = 7.0f;
    boss.bossPattern = static_cast<core::u8>(zoneIndex_);
    boss.specialCooldown = 2.4f;
    boss.home = bossSpawn_;
    boss.position = bossSpawn_;
    boss.yaw = 0.0f;
    enemies_.push_back(boss);
}

bool RpgGame::addItem(Item item) { return addItem(core::PlayerId::P0, std::move(item)); }

bool RpgGame::addItem(core::PlayerId id, Item item) {
    Loadout& loadout = players_[static_cast<core::usize>(id)].loadout;
    if (loadout.inventory.size() >= kInventorySize) {
        return false;
    }
    item.id = ++itemId_;
    loadout.inventory.push_back(std::move(item));
    return true;
}

core::f32 RpgGame::computeDamage(const ItemTotals& totals, core::PlayerId id) const {
    const PlayerState& player = players_[static_cast<core::usize>(id)];
    const ClassDef& cls = classDef(player.classId);
    const core::f32 levelScale = static_cast<core::f32>(player.loadout.level - 1);
    return (cls.damage + static_cast<core::f32>(totals.weaponDamage) * 2.2f +
            totals.flatDamage * 1.6f + levelScale * 1.3f) *
           (1.0f + totals.percentDamage / 100.0f);
}

core::f32 RpgGame::damageFor(const ItemTotals& totals) const {
    return computeDamage(totals, localViewer_);
}

void RpgGame::setInventorySelection(core::i32 index) {
    const core::i32 count = static_cast<core::i32>(inventory().size());
    inventorySelection_ = count <= 0 ? 0 : core::clamp(index, 0, count - 1);
}

ItemComparison RpgGame::compareItem(const Item& item) const {
    ItemComparison result;
    if (item.slot == ItemSlot::SkillGem || item.slot == ItemSlot::Flask) {
        return result;
    }
    const PlayerState& local = players_[static_cast<core::usize>(localViewer_)];
    result.equippable = item.itemLevel <= local.loadout.level;

    ItemTotals candidate;
    for (core::usize i = 0; i < local.loadout.equipment.size(); ++i) {
        if (static_cast<ItemSlot>(i) == item.slot) {
            continue;
        }
        if (!local.loadout.equipment[i].empty()) {
            candidate.add(local.loadout.equipment[i]);
        }
    }
    candidate.add(item);

    const ClassDef& cls = classDef(local.classId);
    result.damage = computeDamage(candidate, localViewer_) -
                    computeDamage(local.loadout.totals, localViewer_);
    result.armour = (candidate.armour - local.loadout.totals.armour);
    result.life = (candidate.life - local.loadout.totals.life);
    result.mana = (candidate.mana - local.loadout.totals.mana);
    result.moveSpeed = cls.moveSpeed * (1.0f + candidate.moveSpeed / 100.0f) -
                       cls.moveSpeed * (1.0f + local.loadout.totals.moveSpeed / 100.0f);
    result.critChance = candidate.critChance - local.loadout.totals.critChance;
    return result;
}

void RpgGame::recomputeStats() {
    const ClassDef& cls = classDef(active().classId);
    ItemTotals totals;
    for (const Item& item : active().loadout.equipment) {
        if (!item.empty()) {
            totals.add(item);
        }
    }
    active().loadout.totals = totals;

    const core::f32 levelScale = static_cast<core::f32>(active().loadout.level - 1);
    active().maxHealth = cls.health + levelScale * 12.0f + totals.life;
    active().maxMana = cls.mana + levelScale * 5.0f + totals.mana;
    active().damage = computeDamage(totals, activeId_);
    active().armour = totals.armour;
    active().moveSpeed = cls.moveSpeed * (1.0f + totals.moveSpeed / 100.0f);
    active().critChance = 5.0f + totals.critChance;
    active().lifeRegen = 0.8f + totals.lifeRegen;
    active().manaRegen = 3.0f + totals.manaRegen;
    active().health = std::min(active().health, active().maxHealth);
    active().mana = std::min(active().mana, active().maxMana);
    active().loadout.xpToNext = xpToNextFor(active().loadout.level);
}

void RpgGame::update(core::f32 dt, const core::InputState& input) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    bannerTimer_ = std::max(0.0f, bannerTimer_ - dt);
    messageTimer_ = std::max(0.0f, messageTimer_ - dt);
    dialogueTimer_ = std::max(0.0f, dialogueTimer_ - dt);

    if (mode_ != UiMode::Playing && mode_ != UiMode::Inventory) {
        updateMenu(input);
        return;
    }

    if (input.wasPressed(core::Key::Escape)) {
        if (mode_ == UiMode::Playing) {
            mode_ = UiMode::Paused;
            menuSelection_ = 0;
            emit(core::SoundId::MenuSelect);
        } else {
            mode_ = UiMode::Playing;
            emit(core::SoundId::MenuSelect);
        }
        return;
    }

    if (mode_ == UiMode::Inventory) {
        updateInventory(input);
        return;
    }

    if (input.wasPressed('I')) {
        mode_ = UiMode::Inventory;
        inventorySelection_ = 0;
        emit(core::SoundId::MenuSelect);
        return;
    }

    activeId_ = core::PlayerId::P0;
    updateLocalCamera(dt, input);

    core::InputFrame frame;
    frame.activePlayers = activePlayers_;
    core::PlayerInput& local = frame.players[0];
    local.moveX = (input.isDown('D') ? 1.0f : 0.0f) - (input.isDown('A') ? 1.0f : 0.0f);
    local.moveZ = (input.isDown('W') ? 1.0f : 0.0f) - (input.isDown('S') ? 1.0f : 0.0f);
    local.cameraYaw = active().camera.yaw;
    local.cameraPitch = active().camera.pitch;
    local.aimYaw = active().camera.yaw;
    local.jump = input.wasPressed(core::Key::Space);
    local.attack = input.mouseDown[0];
    local.interact = input.mousePressed[1];
    local.skillQ = input.wasPressed('Q');
    local.skillE = input.wasPressed('E');
    local.sprint = input.isDown(core::Key::LeftShift) ||
                   input.isDown(core::Key::RightShift);
    updatePlaying(dt, frame);
}

void RpgGame::updateFrame(core::f32 dt, const core::InputFrame& frame) {
    updatePlaying(dt, frame);
}

void RpgGame::updateMenu(const core::InputState& input) {
    if (mode_ == UiMode::Paused && input.wasPressed(core::Key::Escape)) {
        mode_ = UiMode::Playing;
        emit(core::SoundId::MenuSelect);
        return;
    }

    const core::usize count = menuOptionCount();
    if (count == 0) {
        return;
    }

    if (input.wasPressed(core::Key::Up) || input.wasPressed('W')) {
        menuSelection_ = (menuSelection_ + static_cast<core::i32>(count) - 1) %
                         static_cast<core::i32>(count);
        emit(core::SoundId::MenuMove);
    }
    if (input.wasPressed(core::Key::Down) || input.wasPressed('S')) {
        menuSelection_ = (menuSelection_ + 1) % static_cast<core::i32>(count);
        emit(core::SoundId::MenuMove);
    }

    if (input.wasPressed(core::Key::Enter) || input.wasPressed(core::Key::Space)) {
        if (mode_ == UiMode::ClassSelect) {
            startRun(static_cast<ClassId>(menuSelection_));
            return;
        }
        if (mode_ == UiMode::Paused) {
            if (menuSelection_ == 0) {
                mode_ = UiMode::Playing;
            } else if (menuSelection_ == 1) {
                graphicsRequested_ = true;
            } else if (menuSelection_ == 2) {
                restartRun();
            } else if (menuSelection_ == 3) {
                changeClass();
            } else {
                quitRequested_ = true;
            }
        } else if (mode_ == UiMode::Dead) {
            if (menuSelection_ == 0) {
                respawn();
            } else if (menuSelection_ == 1) {
                changeClass();
            } else {
                quitRequested_ = true;
            }
        } else if (mode_ == UiMode::Victory) {
            if (menuSelection_ == 0) {
                restartRun();
            } else if (menuSelection_ == 1) {
                changeClass();
            } else {
                quitRequested_ = true;
            }
        }
        emit(core::SoundId::MenuSelect);
    }
}

void RpgGame::updateInventory(const core::InputState& input) {
    if (input.wasPressed('I') || input.wasPressed(core::Key::Escape)) {
        mode_ = UiMode::Playing;
        emit(core::SoundId::MenuSelect);
        return;
    }

    const core::i32 count = static_cast<core::i32>(active().loadout.inventory.size());
    if (count > 0) {
        if (input.wasPressed(core::Key::Left) || input.wasPressed('A')) {
            inventorySelection_ = (inventorySelection_ + count - 1) % count;
            emit(core::SoundId::MenuMove);
        }
        if (input.wasPressed(core::Key::Right) || input.wasPressed('D')) {
            inventorySelection_ = (inventorySelection_ + 1) % count;
            emit(core::SoundId::MenuMove);
        }
        if (input.wasPressed(core::Key::Up) || input.wasPressed('W')) {
            inventorySelection_ = (inventorySelection_ - 8 + count) % count;
            emit(core::SoundId::MenuMove);
        }
        if (input.wasPressed(core::Key::Down) || input.wasPressed('S')) {
            inventorySelection_ = (inventorySelection_ + 8) % count;
            emit(core::SoundId::MenuMove);
        }
        if (input.wasPressed(core::Key::Enter) || input.wasPressed(core::Key::Space)) {
            useInventoryItem();
        }
        if (input.wasPressed('X')) {
            dropSelectedItem();
        }
    }
}

core::Vec3 RpgGame::forward() const {
    return {-std::sin(active().yaw), 0.0f, -std::cos(active().yaw)};
}

void RpgGame::updateLocalCamera(core::f32 dt, const core::InputState& input) {
    active().camera.yaw -= input.mouseDelta.x * 0.0026f;
    active().camera.pitch -= input.mouseDelta.y * 0.0020f;
    if (input.isDown(core::Key::Left)) {
        active().camera.yaw += 1.7f * dt;
    }
    if (input.isDown(core::Key::Right)) {
        active().camera.yaw -= 1.7f * dt;
    }
    if (input.isDown(core::Key::Up)) {
        active().camera.pitch += 1.1f * dt;
    }
    if (input.isDown(core::Key::Down)) {
        active().camera.pitch -= 1.1f * dt;
    }
    active().camera.pitch = core::clamp(active().camera.pitch, -0.28f, 1.18f);
    if (input.scrollDelta != 0.0f) {
        active().camera.distance =
            core::clamp(active().camera.distance - input.scrollDelta * 0.01f, 2.6f, 10.0f);
    }
}

void RpgGame::updateCameraFollow(core::f32 dt) {
    const core::Vec3 right{std::cos(active().camera.yaw), 0.0f,
                           -std::sin(active().camera.yaw)};
    core::Vec3 desiredTarget =
        active().position + core::Vec3{0.0f, 1.55f + active().jumpHeight * 0.65f, 0.0f} +
        right * 0.38f;
    resolveObstacles(desiredTarget, 0.15f);
    active().camera.target = desiredTarget;

    const core::Vec3 offset{std::sin(active().camera.yaw) * std::cos(active().camera.pitch),
                            std::sin(active().camera.pitch),
                            std::cos(active().camera.yaw) * std::cos(active().camera.pitch)};
    const core::f32 allowed =
        resolveCameraDistance(active().camera.target, offset, active().camera.distance);
    if (allowed < active().camera.smoothedDistance) {
        active().camera.smoothedDistance = allowed;
    } else {
        active().camera.smoothedDistance +=
            (allowed - active().camera.smoothedDistance) * std::min(1.0f, 3.5f * dt);
    }
}

core::Vec3 RpgGame::cameraForward() const {
    return normalize(core::Vec3{-std::sin(active().camera.yaw), 0.0f, -std::cos(active().camera.yaw)});
}

core::f32 RpgGame::resolveCameraDistance(core::Vec3 target, core::Vec3 offset,
                                         core::f32 desired) const {
    core::f32 allowed = desired;

    const core::Vec2 start{target.x, target.z};
    const core::Vec2 delta{offset.x * desired, offset.z * desired};
    const core::f32 a = delta.lengthSquared();
    if (a > 1e-6f) {
        for (const Obstacle& obstacle : obstacles_) {
            const core::f32 radius = obstacle.radius + 0.30f;
            const core::Vec2 center{obstacle.position.x, obstacle.position.z};
            const core::Vec2 f{start.x - center.x, start.y - center.y};
            const core::f32 b = 2.0f * (f.x * delta.x + f.y * delta.y);
            const core::f32 c = f.lengthSquared() - radius * radius;
            const core::f32 discriminant = b * b - 4.0f * a * c;
            if (discriminant < 0.0f) {
                continue;
            }
            const core::f32 t = (-b - std::sqrt(discriminant)) / (2.0f * a);
            if (t >= 0.0f && t <= 1.0f) {
                allowed = std::min(allowed, desired * t - 0.08f);
            }
        }
    }

    const core::f32 endY = target.y + offset.y * desired;
    if (endY < 0.35f && offset.y < -1e-4f) {
        const core::f32 t = (0.35f - target.y) / (offset.y * desired);
        allowed = std::min(allowed, desired * core::clamp(t, 0.0f, 1.0f) - 0.05f);
    }

    return core::clamp(allowed, 0.05f, desired);
}

void RpgGame::updatePlaying(core::f32 dt, const core::InputFrame& frame) {
    time_ += dt;
    activePlayers_ = core::clamp(frame.activePlayers, core::u8{1}, core::u8{2});

    for (core::u8 i = 0; i < activePlayers_; ++i) {
        activeId_ = static_cast<core::PlayerId>(i);
        if (active().playerDead) {
            continue;
        }
        const core::PlayerInput& input = frame.players[i];

        active().attackCooldown = std::max(0.0f, active().attackCooldown - dt);
        active().attackTimer = std::max(0.0f, active().attackTimer - dt);
        active().qCooldown = std::max(0.0f, active().qCooldown - dt);
        active().eCooldown = std::max(0.0f, active().eCooldown - dt);

        active().camera.yaw = input.cameraYaw;
        active().camera.pitch = core::clamp(input.cameraPitch, -0.28f, 1.18f);
        updateCameraFollow(dt);

        const core::Vec3 cameraForward{-std::sin(active().camera.yaw), 0.0f,
                                       -std::cos(active().camera.yaw)};
        const core::Vec3 cameraRight{std::cos(active().camera.yaw), 0.0f,
                                     -std::sin(active().camera.yaw)};
        const core::Vec3 move =
            cameraForward * input.moveZ + cameraRight * input.moveX;
        movePlayer(dt, input.sprint, move);

        if (input.jump && active().grounded) {
            active().verticalVelocity = kJumpVelocity;
            active().grounded = false;
            emit(core::SoundId::Jump);
        }
        if (!active().grounded) {
            active().verticalVelocity -= kGravity * dt;
            active().jumpHeight += active().verticalVelocity * dt;
            if (active().jumpHeight <= 0.0f) {
                active().jumpHeight = 0.0f;
                active().verticalVelocity = 0.0f;
                active().grounded = true;
            }
        }

        if (input.attack && active().attackCooldown <= 0.0f) {
            basicAttack();
        }
        if (input.skillQ) {
            useSkill(classDef(active().classId).primary, false);
        }
        if (input.skillE) {
            useSkill(classDef(active().classId).secondary, true);
        }
        if (input.interact) {
            handleInteract();
        }
    }

    updateEnemies(dt);
    updateProjectiles(dt);
    updateEffects(dt);

    for (core::u8 i = 0; i < activePlayers_; ++i) {
        activeId_ = static_cast<core::PlayerId>(i);
        if (!active().playerDead) {
            active().health = std::min(active().maxHealth,
                                       active().health + active().lifeRegen * dt);
            active().mana = std::min(active().maxMana,
                                     active().mana + active().manaRegen * dt);
        }
        if (active().health <= 0.0f) {
            active().health = 0.0f;
            active().playerDead = true;
            if (i == 0 || activePlayers_ == 1) {
                mode_ = UiMode::Dead;
                menuSelection_ = 0;
                dialogueTimer_ = 0.0f;
            }
            emit(core::SoundId::PlayerHurt);
        }
    }

    activeId_ = core::PlayerId::P0;

    if (!bossIntroShown_) {
        const EnemyState* bossEnemy = boss();
        if (bossEnemy != nullptr && bossEnemy->alive) {
            const core::Vec3 delta = bossEnemy->position - active().position;
            if (delta.x * delta.x + delta.z * delta.z < 18.0f * 18.0f) {
                bossIntroShown_ = true;
                bannerTitle_ = zone().bossName;
                bannerText_ = zone().bossIntro;
                bannerTimer_ = 6.0f;
                emit(core::SoundId::BossRoar);
            }
        }
    }

    interactionHint_.clear();
    core::f32 best = 3.5f;
    for (const LootDrop& drop : loot_) {
        const core::f32 dx = drop.position.x - active().position.x;
        const core::f32 dz = drop.position.z - active().position.z;
        const core::f32 distance = std::sqrt(dx * dx + dz * dz);
        if (distance < best) {
            best = distance;
            interactionHint_ = "RMB PICK UP " + drop.item.name;
        }
    }
    for (const NpcState& npc : npcs_) {
        const core::f32 dx = npc.position.x - active().position.x;
        const core::f32 dz = npc.position.z - active().position.z;
        const core::f32 distance = std::sqrt(dx * dx + dz * dz);
        if (distance < best) {
            best = distance;
            interactionHint_ = "RMB TALK TO " + npc.name;
        }
    }
    if (portalActive_) {
        const core::f32 dx = portalPosition_.x - active().position.x;
        const core::f32 dz = portalPosition_.z - active().position.z;
        const core::f32 distance = std::sqrt(dx * dx + dz * dz);
        if (distance < best) {
            best = distance;
            interactionHint_ = "RMB ENTER THE PORTAL";
        }
    }
    {
        const core::f32 dx = firePosition_.x - active().position.x;
        const core::f32 dz = firePosition_.z - active().position.z;
        const core::f32 distance = std::sqrt(dx * dx + dz * dz);
        if (distance < best && distance < 2.8f) {
            interactionHint_ = "RMB REST AT FIRE";
        }
    }
}

void RpgGame::predictLocalMovement(core::PlayerId id, const core::PlayerInput& input,
                                   core::f32 dt) {
    if (dt <= 0.0f) {
        return;
    }
    dt = std::min(dt, 0.1f);

    const core::PlayerId previous = activeId_;
    activeId_ = id;
    if (!active().playerDead) {
        active().camera.yaw = input.cameraYaw;
        active().camera.pitch = core::clamp(input.cameraPitch, -0.28f, 1.18f);

        const core::Vec3 forward{-std::sin(active().camera.yaw), 0.0f,
                                 -std::cos(active().camera.yaw)};
        const core::Vec3 right{std::cos(active().camera.yaw), 0.0f,
                               -std::sin(active().camera.yaw)};
        movePlayer(dt, input.sprint, forward * input.moveZ + right * input.moveX);

        if (input.jump && active().grounded) {
            active().verticalVelocity = kJumpVelocity;
            active().grounded = false;
        }
        if (!active().grounded) {
            active().verticalVelocity -= kGravity * dt;
            active().jumpHeight += active().verticalVelocity * dt;
            if (active().jumpHeight <= 0.0f) {
                active().jumpHeight = 0.0f;
                active().verticalVelocity = 0.0f;
                active().grounded = true;
            }
        }
    }
    activeId_ = previous;
}

void RpgGame::movePlayer(core::f32 dt, bool sprint, core::Vec3 move) {
    const bool moving = move.lengthSquared() > 1e-5f;
    if (moving) {
        move = normalize(move);
        const core::f32 speed = active().moveSpeed * (sprint ? 1.45f : 1.0f);
        active().position += move * (speed * dt);
        resolveObstacles(active().position, kPlayerRadius);
        active().walkTime += dt * (speed / 5.0f);
    } else {
        active().walkTime = 0.0f;
    }

    if (moving) {
        const core::f32 targetYaw = std::atan2(-move.x, -move.z);
        active().yaw += wrapAngle(targetYaw - active().yaw) * std::min(1.0f, 14.0f * dt);
    }
}

core::Vec3 RpgGame::aimPoint() const {
    return active().position + cameraForward() * 8.0f;
}

void RpgGame::resolveObstacles(core::Vec3& position, core::f32 radius) const {
    position.x = core::clamp(position.x, -kBounds, kBounds);
    position.z = core::clamp(position.z, -kBounds, kBounds);

    for (int pass = 0; pass < 2; ++pass) {
        for (const Obstacle& obstacle : obstacles_) {
            const core::f32 dx = position.x - obstacle.position.x;
            const core::f32 dz = position.z - obstacle.position.z;
            const core::f32 minDistance = radius + obstacle.radius;
            const core::f32 distanceSq = dx * dx + dz * dz;
            if (distanceSq >= minDistance * minDistance) {
                continue;
            }
            const core::f32 distance = std::sqrt(distanceSq);
            if (distance < 1e-4f) {
                position.x += minDistance;
            } else {
                const core::f32 push = (minDistance - distance) / distance;
                position.x += dx * push;
                position.z += dz * push;
            }
        }
    }
}

void RpgGame::updateEnemies(core::f32 dt) {
    std::vector<EnemyState> summons;
    for (EnemyState& enemy : enemies_) {
        if (!enemy.alive) {
            if (!enemy.boss) {
                enemy.respawnTimer -= dt;
                if (enemy.respawnTimer <= 0.0f) {
                    enemy.alive = true;
                    enemy.health = enemy.maxHealth;
                    enemy.position = enemy.home;
                    enemy.attackCooldown = 0.0f;
                    enemy.fireCooldown = 1.0f;
                }
            }
            continue;
        }

        enemy.bob += dt * 5.0f;
        enemy.attackCooldown = std::max(0.0f, enemy.attackCooldown - dt);
        enemy.fireCooldown = std::max(0.0f, enemy.fireCooldown - dt);
        enemy.slowTimer = std::max(0.0f, enemy.slowTimer - dt);

        core::PlayerId targetId = core::PlayerId::P0;
        core::f32 distance = 1.0e9f;
        for (core::u8 p = 0; p < activePlayers_; ++p) {
            const PlayerState& candidate = players_[p];
            if (candidate.playerDead) {
                continue;
            }
            const core::f32 dx = candidate.position.x - enemy.position.x;
            const core::f32 dz = candidate.position.z - enemy.position.z;
            const core::f32 d = std::sqrt(dx * dx + dz * dz);
            if (d < distance) {
                distance = d;
                targetId = static_cast<core::PlayerId>(p);
            }
        }
        if (distance > 1.0e8f) {
            continue;
        }
        activeId_ = targetId;
        const core::Vec3 toPlayer = active().position - enemy.position;
        const core::f32 aggro = enemy.boss ? 30.0f : 16.0f;
        if (distance > aggro || distance < 1e-4f) {
            continue;
        }

        enemy.yaw = std::atan2(-toPlayer.x, -toPlayer.z);
        const core::f32 slowFactor = enemy.slowTimer > 0.0f ? 0.5f : 1.0f;
        const core::Vec3 direction{toPlayer.x / distance, 0.0f, toPlayer.z / distance};

        if (enemy.boss) {
            if (enemy.charging) {
                enemy.position += enemy.chargeDirection * (17.0f * dt);
                enemy.chargeTimer -= dt;
                if (distance < 2.4f && enemy.attackCooldown <= 0.0f) {
                    hurtPlayer(targetId, enemy.damage * 1.3f);
                    enemy.attackCooldown = 1.0f;
                }
                if (enemy.chargeTimer <= 0.0f) {
                    enemy.charging = false;
                }
                resolveObstacles(enemy.position, enemy.scale * 0.5f);
                continue;
            }
            if (enemy.telegraphTimer > 0.0f) {
                enemy.telegraphTimer -= dt;
                if (enemy.telegraphTimer <= 0.0f) {
                    executeBossPattern(enemy, summons, targetId);
                }
                continue;
            }
            enemy.specialCooldown -= dt;
            if (enemy.specialCooldown <= 0.0f && distance < 26.0f) {
                enemy.pendingPattern = enemy.bossPattern;
                enemy.telegraphTimer = 0.7f;
                enemy.specialCooldown = 4.6f;
                spawnEffect(enemy.position, 3.4f, core::Vec4{1.0f, 0.22f, 0.15f, 1.0f}, true);
                emit(core::SoundId::BossRoar);
            }
            if (distance > 3.4f) {
                enemy.position += direction * (enemy.speed * slowFactor * dt);
            } else if (enemy.attackCooldown <= 0.0f) {
                hurtPlayer(targetId, enemy.damage);
                enemy.attackCooldown = 1.6f;
            }
        } else if (enemy.ranged) {
            if (distance > 12.0f) {
                enemy.position += direction * (enemy.speed * slowFactor * dt);
            } else if (distance < 6.0f) {
                enemy.position -= direction * (enemy.speed * slowFactor * dt);
            }
            if (enemy.fireCooldown <= 0.0f && distance < 16.0f) {
                fireProjectile(enemy.position + core::Vec3{0.0f, 0.9f, 0.0f}, direction, 9.5f,
                               enemy.damage, false, 0.0f, enemy.color);
                enemy.fireCooldown = 2.4f;
            }
        } else {
            if (distance > 2.0f) {
                enemy.position += direction * (enemy.speed * slowFactor * dt);
            } else if (enemy.attackCooldown <= 0.0f) {
                hurtPlayer(targetId, enemy.damage);
                enemy.attackCooldown = 1.4f;
            }
        }

        resolveObstacles(enemy.position, enemy.scale * 0.5f);
    }

    if (!summons.empty()) {
        enemies_.insert(enemies_.end(), summons.begin(), summons.end());
    }
    separateEnemies();
}

void RpgGame::executeBossPattern(EnemyState& boss, std::vector<EnemyState>& summons,
                                 core::PlayerId target) {
    activeId_ = target;
    const core::Vec3 toPlayer = active().position - boss.position;
    core::Vec3 direction = toPlayer;
    direction.y = 0.0f;
    if (direction.lengthSquared() < 1e-4f) {
        direction = cameraForward();
    }
    direction = normalize(direction);
    const core::f32 angle = std::atan2(direction.z, direction.x);
    const core::Vec3 origin = boss.position + core::Vec3{0.0f, 1.5f, 0.0f};

    auto ring = [&](core::i32 count, core::f32 offset, core::f32 speed) {
        for (core::i32 i = 0; i < count; ++i) {
            const core::f32 a = offset + static_cast<core::f32>(i) / count * kTwoPi;
            fireProjectile(origin, {std::cos(a), 0.0f, std::sin(a)}, speed,
                           boss.damage * 0.55f, false, 0.0f, boss.color);
        }
    };

    switch (boss.pendingPattern % 6) {
    case 0: // Verdant: root ring + adds
        ring(12, 0.0f, 9.5f);
        for (core::i32 i = 0; i < 2; ++i) {
            EnemyState minion;
            const EnemyTypeDef& type = zone().enemies[0];
            minion.name = type.name;
            minion.color = type.color;
            minion.scale = type.scale * 0.8f;
            minion.ranged = false;
            minion.level = std::max(1, boss.level - 3);
            minion.maxHealth = type.health * (1.0f + minion.level * 0.22f);
            minion.health = minion.maxHealth;
            minion.damage = type.damage * (1.0f + minion.level * 0.10f);
            minion.speed = type.speed;
            const core::f32 a = static_cast<core::f32>(i) * kPi + 0.5f;
            minion.position = boss.position + core::Vec3{std::cos(a) * 3.0f, 0.0f,
                                                         std::sin(a) * 3.0f};
            minion.home = minion.position;
            minion.bob = static_cast<core::f32>(i);
            summons.push_back(minion);
        }
        break;
    case 1: // Ashen: telegraphed charge
        boss.charging = true;
        boss.chargeTimer = 0.45f;
        boss.chargeDirection = direction;
        break;
    case 2: // Frozen: wide projectile fan
        for (core::i32 i = -4; i <= 4; ++i) {
            const core::f32 a = angle + static_cast<core::f32>(i) * 0.16f;
            fireProjectile(origin, {std::cos(a), 0.0f, std::sin(a)}, 10.5f,
                           boss.damage * 0.6f, false, 0.0f, boss.color);
        }
        break;
    case 3: // Drowned: three expanding rings
        ring(10, 0.0f, 8.0f);
        ring(10, 0.4f, 9.5f);
        ring(10, 0.8f, 11.0f);
        break;
    case 4: { // Umbral: blink beside the player, then burst
        const core::Vec3 destination =
            active().position - direction * 3.5f + core::Vec3{0.0f, 0.0f, 0.0f};
        boss.position = destination;
        resolveObstacles(boss.position, boss.scale * 0.5f);
        spawnEffect(boss.position, 2.0f, core::Vec4{0.7f, 0.3f, 1.0f, 1.0f}, true);
        ring(10, 0.2f, 10.0f);
        break;
    }
    case 5: // Citadel: meteor rain around the player
        for (core::i32 i = 0; i < 8; ++i) {
            const core::f32 a = static_cast<core::f32>(i) / 8.0f * kTwoPi;
            const core::f32 radius = 1.5f + static_cast<core::f32>(i % 3) * 1.8f;
            const core::Vec3 spawn{active().position.x + std::cos(a) * radius, 9.5f,
                                   active().position.z + std::sin(a) * radius};
            fireProjectile(spawn, {0.0f, -1.0f, 0.0f}, 13.0f, boss.damage * 0.55f, false, 0.0f,
                           boss.color);
        }
        break;
    }
    spawnEffect(boss.position, 2.6f, boss.color, true);
}

void RpgGame::separateEnemies() {
    for (core::usize i = 0; i < enemies_.size(); ++i) {
        if (!enemies_[i].alive) {
            continue;
        }
        for (core::usize j = i + 1; j < enemies_.size(); ++j) {
            if (!enemies_[j].alive) {
                continue;
            }
            const core::f32 minDistance = (enemies_[i].scale + enemies_[j].scale) * 0.45f;
            core::Vec3 delta = enemies_[j].position - enemies_[i].position;
            delta.y = 0.0f;
            const core::f32 distanceSq = delta.lengthSquared();
            if (distanceSq >= minDistance * minDistance || distanceSq < 1e-6f) {
                continue;
            }
            const core::f32 distance = std::sqrt(distanceSq);
            const core::f32 push = (minDistance - distance) * 0.5f / distance;
            enemies_[i].position -= delta * push;
            enemies_[j].position += delta * push;
        }
    }

    // Keep enemies from occupying any player's space.
    for (core::u8 p = 0; p < activePlayers_; ++p) {
        PlayerState& player = players_[p];
        for (const EnemyState& enemy : enemies_) {
            if (!enemy.alive) {
                continue;
            }
            core::Vec3 delta = player.position - enemy.position;
            delta.y = 0.0f;
            const core::f32 minDistance = kPlayerRadius + enemy.scale * 0.5f;
            const core::f32 distanceSq = delta.lengthSquared();
            if (distanceSq >= minDistance * minDistance || distanceSq < 1e-6f) {
                continue;
            }
            const core::f32 distance = std::sqrt(distanceSq);
            player.position += delta * ((minDistance - distance) / distance);
            resolveObstacles(player.position, kPlayerRadius);
        }
    }
}

void RpgGame::basicAttack() {
    const bool ranged = classDef(active().classId).ranged;
    active().attackCooldown = ranged ? 0.50f : 0.45f;
    active().attackTimer = 0.25f;

    const core::Vec3 aim = cameraForward();
    active().yaw = std::atan2(-aim.x, -aim.z);

    if (ranged) {
        core::Vec3 direction = aim;
        const bool mage = active().classId == ClassId::Mage;
        fireProjectile(active().position + direction * 0.7f + core::Vec3{0.0f, 1.1f, 0.0f},
                       direction, mage ? 18.0f : 28.0f, active().damage, true,
                       mage ? 1.6f : 0.0f,
                       mage ? core::Vec4{1.0f, 0.45f, 0.15f, 1.0f}
                            : core::Vec4{0.9f, 0.9f, 0.7f, 1.0f});
        emit(mage ? core::SoundId::RangedAttack : core::SoundId::RangedAttack);
    } else {
        const core::Vec3 facing = forward();
        for (EnemyState& enemy : enemies_) {
            if (!enemy.alive) {
                continue;
            }
            const core::Vec3 delta = enemy.position - active().position;
            const core::f32 distance =
                std::sqrt(delta.x * delta.x + delta.z * delta.z);
            if (distance > 2.9f) {
                continue;
            }
            if (distance > 0.05f) {
                const core::Vec3 dir{delta.x / distance, 0.0f, delta.z / distance};
                if (dot(dir, facing) < 0.1f) {
                    continue;
                }
            }
            core::f32 damage = active().damage;
            if (randomFloat() * 100.0f < active().critChance) {
                damage *= 2.0f;
            }
            hurtEnemy(enemy, damage);
        }
        emit(core::SoundId::Attack);
    }
}

void RpgGame::useSkill(SkillId skill, bool secondary) {
    if (secondary && !active().loadout.secondaryUnlocked) {
        setMessage("SKILL LOCKED - HUNT FOR A SKILL GEM");
        emit(core::SoundId::Error);
        return;
    }
    const SkillDef& def = skillDef(skill);
    core::f32& cooldown = secondary ? active().eCooldown : active().qCooldown;
    if (cooldown > 0.0f) {
        return;
    }
    if (active().mana < def.manaCost) {
        setMessage("NOT ENOUGH MANA");
        emit(core::SoundId::Error);
        return;
    }

    cooldown = def.cooldown;
    active().mana -= def.manaCost;
    emit(core::SoundId::Skill);

    core::Vec3 offset = cameraForward();
    active().yaw = std::atan2(-offset.x, -offset.z);
    const core::f32 targetDistance = 8.0f;

    core::Vec3 center = def.targeted ? active().position + offset * std::min(targetDistance, 12.0f)
                                     : active().position;
    if (def.dash) {
        const core::f32 dashDistance = std::min(targetDistance, 8.0f);
        active().position += offset * dashDistance;
        resolveObstacles(active().position, kPlayerRadius);
        center = active().position;
    }

    castArea(center, def.radius, def.damage);
    spawnEffect(center, def.radius, core::Vec4{0.6f, 0.8f, 1.0f, 1.0f}, true);

    if (skill == SkillId::FrostNova) {
        for (EnemyState& enemy : enemies_) {
            if (!enemy.alive) {
                continue;
            }
            const core::f32 dx = enemy.position.x - center.x;
            const core::f32 dz = enemy.position.z - center.z;
            if (dx * dx + dz * dz <= def.radius * def.radius) {
                enemy.slowTimer = 3.0f;
            }
        }
    }
    if (skill == SkillId::LeapSlam || skill == SkillId::ShadowDash) {
        spawnEffect(active().position, 1.2f, core::Vec4{0.9f, 0.8f, 0.5f, 1.0f}, false);
    }
}

void RpgGame::castArea(core::Vec3 center, core::f32 radius, core::f32 damageMultiplier) {
    const core::f32 skillScale = 1.0f + active().loadout.totals.skillDamage / 100.0f;
    bool hit = false;
    for (EnemyState& enemy : enemies_) {
        if (!enemy.alive) {
            continue;
        }
        const core::f32 dx = enemy.position.x - center.x;
        const core::f32 dz = enemy.position.z - center.z;
        const core::f32 reach = radius + enemy.scale * 0.5f;
        if (dx * dx + dz * dz > reach * reach) {
            continue;
        }
        core::f32 damage = active().damage * damageMultiplier * skillScale;
        if (randomFloat() * 100.0f < active().critChance) {
            damage *= 2.0f;
        }
        hurtEnemy(enemy, damage);
        hit = true;
    }
    if (hit) {
        emit(core::SoundId::Hit);
    }
}

void RpgGame::fireProjectile(core::Vec3 from, core::Vec3 direction, core::f32 speed,
                             core::f32 damage, bool fromPlayer, core::f32 aoeRadius,
                             core::Vec4 color, core::PlayerId owner) {
    Projectile projectile;
    projectile.owner = owner;
    projectile.position = from;
    projectile.velocity = normalize(direction) * speed;
    projectile.life = 2.4f;
    projectile.damage = damage;
    projectile.radius = fromPlayer ? 0.22f : 0.30f;
    projectile.aoeRadius = aoeRadius;
    projectile.color = color;
    projectile.fromPlayer = fromPlayer;
    projectiles_.push_back(projectile);
}

void RpgGame::hurtEnemy(EnemyState& enemy, core::f32 amount) {
    if (!enemy.alive) {
        return;
    }
    enemy.health -= amount;
    spawnEffect(enemy.position + core::Vec3{0.0f, 0.8f, 0.0f}, 0.5f,
                core::Vec4{1.0f, 0.8f, 0.4f, 1.0f}, false);
    emit(core::SoundId::Hit);
    if (enemy.health <= 0.0f) {
        killEnemy(enemy);
    }
}

void RpgGame::killEnemy(EnemyState& enemy) {
    enemy.health = 0.0f;
    enemy.alive = false;
    emit(core::SoundId::EnemyDeath);
    spawnEffect(enemy.position, enemy.scale * 1.6f, enemy.color, true);

    const core::i32 gold = enemy.boss ? 80 + enemy.level * 3 : 3 + enemy.level / 2;
    active().loadout.gold += gold;
    ++active().loadout.kills;
    gainXp(enemy.boss ? xpToNextFor(active().loadout.level) * 3
                      : static_cast<core::i32>(static_cast<core::f32>(xpToNextFor(active().loadout.level)) *
                                               0.7f));

    if (enemy.boss) {
        portalActive_ = true;
        bannerTitle_ = "BOSS DEFEATED";
        bannerText_ = zone().bossOutro;
        bannerTimer_ = 7.0f;
        emit(core::SoundId::Victory);
        addLoot(generateItem(randomState_, enemy.level + 2, active().classId),
                enemy.position + core::Vec3{0.6f, 0.0f, 0.0f});
        addLoot(generateItem(randomState_, enemy.level + 2, active().classId),
                enemy.position + core::Vec3{-0.6f, 0.0f, 0.2f});
        if (!active().loadout.secondaryUnlocked) {
            addLoot(generateSkillGem(randomState_, enemy.level, active().classId),
                    enemy.position + core::Vec3{0.0f, 0.0f, 0.8f});
        }
    } else {
        enemy.respawnTimer = 10.0f;
        if (randomFloat() < 0.45f) {
            addLoot(generateItem(randomState_, enemy.level, active().classId), enemy.position);
        }
        if (!active().loadout.secondaryUnlocked && randomFloat() < 0.10f) {
            addLoot(generateSkillGem(randomState_, enemy.level, active().classId),
                    enemy.position + core::Vec3{0.3f, 0.0f, 0.0f});
        }
    }
}

void RpgGame::hurtPlayer(core::PlayerId id, core::f32 amount) {
    PlayerState& target = players_[static_cast<core::usize>(id)];
    const core::f32 reduced = amount * (100.0f / (100.0f + target.armour));
    target.health -= std::max(1.0f, reduced);
    spawnEffect(target.position + core::Vec3{0.0f, 1.0f, 0.0f}, 0.6f,
                core::Vec4{0.9f, 0.2f, 0.2f, 1.0f}, false);
    emit(core::SoundId::PlayerHurt);
}

void RpgGame::updateProjectiles(core::f32 dt) {
    for (core::usize i = 0; i < projectiles_.size();) {
        Projectile& projectile = projectiles_[i];
        projectile.life -= dt;
        projectile.position += projectile.velocity * dt;

        bool remove = projectile.life <= 0.0f;
        if (!remove) {
            for (const Obstacle& obstacle : obstacles_) {
                const core::f32 dx = projectile.position.x - obstacle.position.x;
                const core::f32 dz = projectile.position.z - obstacle.position.z;
                const core::f32 reach = obstacle.radius + projectile.radius;
                if (dx * dx + dz * dz < reach * reach) {
                    remove = true;
                    break;
                }
            }
        }
        if (!remove && (std::abs(projectile.position.x) > 48.0f ||
                        std::abs(projectile.position.z) > 48.0f)) {
            remove = true;
        }

        if (!remove && projectile.fromPlayer) {
            for (EnemyState& enemy : enemies_) {
                if (!enemy.alive) {
                    continue;
                }
                const core::f32 dx = enemy.position.x - projectile.position.x;
                const core::f32 dz = enemy.position.z - projectile.position.z;
                const core::f32 dy = projectile.position.y - (enemy.position.y + enemy.scale * 0.5f);
                const core::f32 reach = projectile.radius + enemy.scale * 0.65f;
                if (dx * dx + dz * dz < reach * reach && std::abs(dy) < 1.6f) {
                    activeId_ = projectile.owner;
                    hurtEnemy(enemy, projectile.damage);
                    if (projectile.aoeRadius > 0.0f) {
                        castArea(projectile.position, projectile.aoeRadius, 0.55f);
                        spawnEffect(projectile.position, projectile.aoeRadius,
                                    core::Vec4{1.0f, 0.5f, 0.2f, 1.0f}, true);
                    }
                    remove = true;
                    break;
                }
            }
        } else if (!remove) {
            for (core::u8 p = 0; p < activePlayers_; ++p) {
                const PlayerState& target = players_[p];
                if (target.playerDead) {
                    continue;
                }
                const core::f32 dx = target.position.x - projectile.position.x;
                const core::f32 dz = target.position.z - projectile.position.z;
                const core::f32 dy = projectile.position.y - (target.position.y + 1.0f);
                if (target.jumpHeight < 1.2f && dx * dx + dz * dz < 0.55f * 0.55f &&
                    std::abs(dy) < 1.2f) {
                    hurtPlayer(static_cast<core::PlayerId>(p), projectile.damage);
                    remove = true;
                    break;
                }
            }
        }

        if (remove) {
            projectiles_.erase(projectiles_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void RpgGame::updateEffects(core::f32 dt) {
    for (core::usize i = 0; i < effects_.size();) {
        effects_[i].life -= dt;
        if (effects_[i].life <= 0.0f) {
            effects_.erase(effects_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void RpgGame::handleInteract() {
    // Loot first.
    core::f32 best = 3.5f;
    std::ptrdiff_t bestIndex = -1;
    for (core::usize i = 0; i < loot_.size(); ++i) {
        const core::f32 dx = loot_[i].position.x - active().position.x;
        const core::f32 dz = loot_[i].position.z - active().position.z;
        const core::f32 distance = std::sqrt(dx * dx + dz * dz);
        if (distance < best) {
            best = distance;
            bestIndex = static_cast<std::ptrdiff_t>(i);
        }
    }

    if (bestIndex >= 0) {
        if (active().loadout.inventory.size() >= kInventorySize) {
            setMessage("INVENTORY FULL");
            emit(core::SoundId::Error);
            return;
        }
        Item item = loot_[static_cast<core::usize>(bestIndex)].item;
        item.id = ++itemId_;
        active().loadout.inventory.push_back(item);
        setMessage("PICKED UP " + item.name);
        emit(core::SoundId::Pickup);
        loot_.erase(loot_.begin() + bestIndex);
        return;
    }

    for (const NpcState& npc : npcs_) {
        const core::f32 dx = npc.position.x - active().position.x;
        const core::f32 dz = npc.position.z - active().position.z;
        if (dx * dx + dz * dz < 3.5f * 3.5f) {
            dialogueName_ = npc.name;
            dialogueText_ = npc.line;
            dialogueTimer_ = 6.0f;
            emit(core::SoundId::MenuSelect);
            return;
        }
    }

    if (portalActive_) {
        const core::f32 dx = portalPosition_.x - active().position.x;
        const core::f32 dz = portalPosition_.z - active().position.z;
        if (dx * dx + dz * dz < 3.5f * 3.5f) {
            if (zoneIndex_ + 1 < zoneCount()) {
                emit(core::SoundId::Portal);
                loadZone(zoneIndex_ + 1);
            } else {
                mode_ = UiMode::Victory;
                menuSelection_ = 0;
                bannerTitle_ = "THE OATH IS KEPT";
                bannerText_ = "THE SUNDERED CROWN FALLS. THE REALM WILL REMEMBER YOUR NAME.";
                emit(core::SoundId::Victory);
            }
            return;
        }
    }

    {
        const core::f32 dx = firePosition_.x - active().position.x;
        const core::f32 dz = firePosition_.z - active().position.z;
        if (dx * dx + dz * dz < 2.8f * 2.8f) {
            restAtFire();
            return;
        }
    }

    setMessage("NOTHING TO INTERACT WITH");
    emit(core::SoundId::Error);
}

const Item* RpgGame::selectedItem() const {
    if (active().loadout.inventory.empty()) {
        return nullptr;
    }
    const core::usize index =
        static_cast<core::usize>(inventorySelection_ % static_cast<core::i32>(active().loadout.inventory.size()));
    return &active().loadout.inventory[index];
}

Item* RpgGame::selectedItem() {
    if (active().loadout.inventory.empty()) {
        return nullptr;
    }
    const core::usize index =
        static_cast<core::usize>(inventorySelection_ % static_cast<core::i32>(active().loadout.inventory.size()));
    return &active().loadout.inventory[index];
}

void RpgGame::useInventoryItem() {
    Item* item = selectedItem();
    if (item == nullptr) {
        return;
    }

    if (item->slot == ItemSlot::Flask) {
        if (item->manaFlask) {
            active().mana = std::min(active().maxMana, active().mana + active().maxMana * item->flaskPower);
            setMessage("MANA RESTORED");
        } else {
            active().health =
                std::min(active().maxHealth, active().health + active().maxHealth * item->flaskPower);
            setMessage("HEALTH RESTORED");
        }
        emit(core::SoundId::Heal);
        if (inventorySelection_ > 0) {
            --inventorySelection_;
        }
        active().loadout.inventory.erase(active().loadout.inventory.begin() + inventorySelection_);
        return;
    }

    if (item->slot == ItemSlot::SkillGem) {
        if (item->gemClass == active().classId && !active().loadout.secondaryUnlocked) {
            active().loadout.secondaryUnlocked = true;
            setMessage("SKILL UNLOCKED: " + std::string(skillDef(item->gemSkill).name));
            emit(core::SoundId::LevelUp);
            active().loadout.inventory.erase(active().loadout.inventory.begin() + inventorySelection_);
            if (inventorySelection_ > 0) {
                --inventorySelection_;
            }
        } else {
            active().loadout.gold += 50;
            setMessage("GEM SOLD FOR 50 GOLD");
            emit(core::SoundId::Pickup);
            active().loadout.inventory.erase(active().loadout.inventory.begin() + inventorySelection_);
            if (inventorySelection_ > 0) {
                --inventorySelection_;
            }
        }
        return;
    }

    if (item->itemLevel > active().loadout.level) {
        setMessage("REQUIRES LEVEL " + std::to_string(item->itemLevel));
        emit(core::SoundId::Error);
        return;
    }

    Item copy = *item;
    const core::usize slotIndex = itemSlotIndex(copy.slot);
    Item previous = active().loadout.equipment[slotIndex];
    active().loadout.equipment[slotIndex] = copy;
    active().loadout.inventory.erase(active().loadout.inventory.begin() + inventorySelection_);
    if (!previous.empty()) {
        previous.id = ++itemId_;
        active().loadout.inventory.push_back(previous);
    }
    if (inventorySelection_ >= static_cast<core::i32>(active().loadout.inventory.size())) {
        inventorySelection_ = std::max(0, static_cast<core::i32>(active().loadout.inventory.size()) - 1);
    }
    recomputeStats();
    setMessage("EQUIPPED " + copy.name);
    emit(core::SoundId::MenuSelect);
}

void RpgGame::respawnPlayer(core::PlayerId id) {
    PlayerState& player = players_[static_cast<core::usize>(id)];
    player.position = player.respawnPoint;
    player.health = player.maxHealth;
    player.mana = player.maxMana;
    player.jumpHeight = 0.0f;
    player.verticalVelocity = 0.0f;
    player.grounded = true;
    player.playerDead = false;
    if (id == core::PlayerId::P0 || activePlayers_ == 1) {
        mode_ = UiMode::Playing;
        menuSelection_ = 0;
    }
    setMessage(id == core::PlayerId::P0 ? "YOU WAKE BY THE FIRE" : "ALLY RETURNS TO THE FIRE");
    emit(core::SoundId::Heal);
}

void RpgGame::respawn() {
    activeId_ = localViewer_;
    respawnPlayer(localViewer_);
}

void RpgGame::restAtFire() {
    active().health = active().maxHealth;
    active().mana = active().maxMana;
    active().respawnPoint = active().position;
    setMessage("RESTED BY THE FIRE - CHECKPOINT SET");
    spawnEffect(firePosition_, 2.2f, core::Vec4{1.0f, 0.55f, 0.15f, 1.0f}, true);
    emit(core::SoundId::Heal);
}

void RpgGame::dropSelectedItem() {
    Item* item = selectedItem();
    if (item == nullptr) {
        return;
    }
    const Item copy = *item;
    active().loadout.inventory.erase(active().loadout.inventory.begin() + inventorySelection_);
    if (inventorySelection_ >= static_cast<core::i32>(active().loadout.inventory.size())) {
        inventorySelection_ = std::max(0, static_cast<core::i32>(active().loadout.inventory.size()) - 1);
    }
    addLoot(copy, active().position + forward() * 0.9f);
    setMessage("DROPPED " + copy.name);
    emit(core::SoundId::Pickup);
}

void RpgGame::addLoot(Item item, core::Vec3 position) {
    if (loot_.size() >= 40) {
        loot_.erase(loot_.begin());
    }
    item.id = ++itemId_;
    LootDrop drop;
    drop.item = std::move(item);
    drop.position = position;
    drop.bob = randomFloat() * 6.0f;
    loot_.push_back(std::move(drop));
}

void RpgGame::gainXp(core::i32 amount) {
    if (active().loadout.level >= kMaxLevel) {
        active().loadout.xp = 0;
        return;
    }
    active().loadout.xp += amount;
    checkLevelUp();
}

void RpgGame::checkLevelUp() {
    while (active().loadout.level < kMaxLevel && active().loadout.xp >= active().loadout.xpToNext) {
        active().loadout.xp -= active().loadout.xpToNext;
        ++active().loadout.level;
        active().loadout.xpToNext = xpToNextFor(active().loadout.level);
        recomputeStats();
        active().health = active().maxHealth;
        active().mana = active().maxMana;
        setMessage("LEVEL " + std::to_string(active().loadout.level));
        emit(core::SoundId::LevelUp);
    }
    if (active().loadout.level >= kMaxLevel) {
        active().loadout.xp = 0;
    }
}

void RpgGame::spawnEffect(core::Vec3 position, core::f32 size, core::Vec4 color, bool ring) {
    Effect effect;
    effect.position = position;
    effect.size = size;
    effect.color = color;
    effect.ring = ring;
    effect.maxLife = ring ? 0.45f : 0.3f;
    effect.life = effect.maxLife;
    effects_.push_back(effect);
}

core::Vec3 RpgGame::rotateY(core::Vec3 value, core::f32 yaw) {
    const core::f32 c = std::cos(yaw);
    const core::f32 s = std::sin(yaw);
    return {value.x * c + value.z * s, value.y, -value.x * s + value.z * c};
}

void RpgGame::addBox(std::vector<scene::MeshInstance>& out, core::Vec3 center, core::Vec3 size,
                     core::Vec4 color, core::Vec3 euler) {
    scene::MeshInstance instance;
    instance.transform = core::Mat4::trs3(center, euler, size);
    instance.color = color;
    out.push_back(instance);
}

void RpgGame::appendPlayer(scene::SceneSnapshot& snapshot, const PlayerState& player) const {
    const core::Vec3 base = player.position + core::Vec3{0.0f, player.jumpHeight, 0.0f};
    const core::f32 yaw = player.yaw;
    const bool moving = player.walkTime > 0.0f;
    const core::f32 stride = moving ? std::sin(player.walkTime * 10.0f) * 0.55f : 0.0f;
    const core::f32 bob = moving ? std::fabs(std::sin(player.walkTime * 10.0f)) * 0.04f : 0.0f;
    const core::f32 swing =
        player.attackTimer > 0.0f
            ? std::sin((1.0f - player.attackTimer / 0.25f) * kPi) * 1.8f
            : 0.0f;

    const core::Vec4 primary{0.20f, 0.35f, 0.75f, 1.0f};
    const core::Vec4 accent = player.classId == ClassId::Archer
                                  ? core::Vec4{0.25f, 0.55f, 0.30f, 1.0f}
                                  : player.classId == ClassId::Mage
                                        ? core::Vec4{0.55f, 0.30f, 0.75f, 1.0f}
                                        : primary;
    const core::Vec4 skin{0.90f, 0.72f, 0.55f, 1.0f};
    const core::Vec4 pants{0.20f, 0.18f, 0.24f, 1.0f};
    const core::Vec4 steel{0.80f, 0.82f, 0.88f, 1.0f};

    auto place = [&](core::Vec3 local, core::Vec3 size, core::Vec4 color, core::f32 tilt) {
        const core::Vec3 world = base + rotateY(local, yaw);
        addBox(snapshot.meshes, world, size, color, {tilt, yaw, 0.0f});
    };

    // Ground shadow.
    addBox(snapshot.meshes, {player.position.x, 0.02f, player.position.z}, {0.9f, 0.02f, 0.9f},
           {0.05f, 0.08f, 0.05f, 1.0f});

    place({-0.18f, 0.35f, 0.0f}, {0.24f, 0.7f, 0.28f}, pants, stride);
    place({0.18f, 0.35f, 0.0f}, {0.24f, 0.7f, 0.28f}, pants, -stride);
    place({0.0f, 1.02f + bob, 0.0f}, {0.72f, 0.95f, 0.46f}, accent, 0.0f);
    place({0.0f, 1.70f + bob, 0.0f}, {0.44f, 0.44f, 0.44f}, skin, 0.0f);
    place({0.0f, 1.95f + bob, 0.0f}, {0.50f, 0.12f, 0.50f}, accent, 0.0f);
    place({-0.48f, 1.10f + bob, 0.0f}, {0.18f, 0.62f, 0.18f}, accent, -stride * 0.8f);
    place({0.48f, 1.10f + bob, 0.0f}, {0.18f, 0.62f, 0.18f}, accent, stride * 0.8f);

    if (player.classId == ClassId::Warrior) {
        place({0.58f, 1.02f + bob, -0.34f}, {0.08f, 0.9f, 0.08f}, steel, swing);
        place({0.58f, 1.02f + bob, -0.34f}, {0.28f, 0.08f, 0.12f}, accent, swing);
    } else if (player.classId == ClassId::Archer) {
        place({0.55f, 1.15f + bob, -0.15f}, {0.06f, 0.9f, 0.06f}, steel, 0.4f);
        place({0.55f, 1.15f + bob, -0.30f}, {0.28f, 0.05f, 0.05f}, accent, -0.4f);
    } else {
        place({0.58f, 1.10f + bob, -0.30f}, {0.07f, 0.8f, 0.07f}, core::Vec4{0.55f, 0.35f, 0.85f, 1.0f},
              swing);
        addBox(snapshot.meshes, base + rotateY({0.58f, 1.55f + bob, -0.30f}, yaw),
               {0.18f, 0.18f, 0.18f}, core::Vec4{0.45f, 0.85f, 1.0f, 1.0f});
    }
}

void RpgGame::appendEnemies(scene::SceneSnapshot& snapshot) const {
    for (const EnemyState& enemy : enemies_) {
        if (!enemy.alive) {
            continue;
        }
        const core::f32 bob = std::sin(enemy.bob) * 0.08f;
        addBox(snapshot.meshes, {enemy.position.x, 0.02f, enemy.position.z},
               {enemy.scale * 0.95f, 0.02f, enemy.scale * 0.95f}, {0.04f, 0.05f, 0.04f, 1.0f});
        const core::Vec3 body{enemy.position.x, enemy.scale * 0.55f + bob, enemy.position.z};
        addBox(snapshot.meshes, body, {enemy.scale, enemy.scale, enemy.scale}, enemy.color,
               {0.0f, enemy.yaw, 0.0f});

        for (const core::f32 side : {-1.0f, 1.0f}) {
            const core::Vec3 local{side * enemy.scale * 0.22f, enemy.scale * 0.15f,
                                   -enemy.scale * 0.46f};
            addBox(snapshot.meshes, body + rotateY(local, enemy.yaw),
                   {0.14f * enemy.scale, 0.14f * enemy.scale, 0.14f * enemy.scale},
                   {0.05f, 0.05f, 0.06f, 1.0f}, {0.0f, enemy.yaw, 0.0f});
        }

        if (enemy.boss) {
            // Crown of shards.
            for (core::i32 i = -2; i <= 2; ++i) {
                const core::Vec3 local{static_cast<core::f32>(i) * 0.28f * enemy.scale,
                                       enemy.scale * 1.15f, 0.0f};
                addBox(snapshot.meshes, body + rotateY(local, enemy.yaw),
                       {0.12f * enemy.scale, 0.5f * enemy.scale, 0.12f * enemy.scale},
                       core::Vec4{1.0f, 0.85f, 0.30f, 1.0f}, {0.0f, enemy.yaw, 0.0f});
            }
        } else {
            const core::f32 width = 1.3f;
            const core::f32 fraction =
                core::clamp(enemy.health / std::max(1.0f, enemy.maxHealth), 0.0f, 1.0f);
            addBox(snapshot.meshes, {enemy.position.x, enemy.scale + 0.55f, enemy.position.z},
                   {width, 0.08f, 0.08f}, {0.10f, 0.05f, 0.05f, 1.0f});
            const core::f32 fill = width * fraction;
            addBox(snapshot.meshes,
                   {enemy.position.x - (width - fill) * 0.5f, enemy.scale + 0.55f,
                    enemy.position.z},
                   {fill, 0.10f, 0.10f}, {0.30f, 0.85f, 0.35f, 1.0f});
        }
    }
}

void RpgGame::appendNpcs(scene::SceneSnapshot& snapshot) const {
    for (core::usize i = 0; i < npcs_.size(); ++i) {
        const NpcState& npc = npcs_[i];
        const core::f32 bob = std::sin(time_ * 2.0f + static_cast<core::f32>(i)) * 0.03f;
        core::f32 yaw = npc.yaw;
        const core::Vec3 toPlayer = active().position - npc.position;
        if (toPlayer.lengthSquared() < 64.0f) {
            yaw = std::atan2(-toPlayer.x, -toPlayer.z);
        }
        addBox(snapshot.meshes, {npc.position.x, 0.02f, npc.position.z}, {0.85f, 0.02f, 0.65f},
               {0.04f, 0.05f, 0.04f, 1.0f});
        addBox(snapshot.meshes, {npc.position.x, 0.62f + bob, npc.position.z},
               {0.8f, 1.25f, 0.55f}, npc.robe, {0.0f, yaw, 0.0f});
        addBox(snapshot.meshes, {npc.position.x, 1.5f + bob, npc.position.z},
               {0.4f, 0.4f, 0.4f}, {0.88f, 0.72f, 0.56f, 1.0f}, {0.0f, yaw, 0.0f});
        addBox(snapshot.meshes, {npc.position.x, 1.75f + bob, npc.position.z},
               {0.56f, 0.14f, 0.56f}, npc.hat, {0.0f, yaw, 0.0f});
    }
}

void RpgGame::appendLoot(scene::SceneSnapshot& snapshot) const {
    for (const LootDrop& drop : loot_) {
        const core::f32 bob = std::sin(drop.bob + time_ * 3.0f) * 0.12f;
        addBox(snapshot.meshes, drop.position + core::Vec3{0.0f, 0.35f + bob, 0.0f},
               {0.35f, 0.35f, 0.35f}, rarityColor(drop.item.rarity),
               {0.0f, time_ * 1.5f, 0.0f});
        addBox(snapshot.meshes, drop.position + core::Vec3{0.0f, 0.12f, 0.0f},
               {0.5f, 0.04f, 0.5f}, {0.2f, 0.2f, 0.25f, 1.0f});
    }
}

void RpgGame::appendProjectiles(scene::SceneSnapshot& snapshot) const {
    for (const Projectile& projectile : projectiles_) {
        addBox(snapshot.meshes, projectile.position,
               {projectile.radius * 2.0f, projectile.radius * 2.0f, projectile.radius * 2.0f},
               projectile.color);
    }
}

void RpgGame::appendEffects(scene::SceneSnapshot& snapshot) const {
    for (const Effect& effect : effects_) {
        const core::f32 progress = 1.0f - effect.life / effect.maxLife;
        if (effect.ring) {
            const core::i32 segments = 14;
            const core::f32 radius = effect.size * (0.3f + progress * 0.7f);
            for (core::i32 i = 0; i < segments; ++i) {
                const core::f32 angle = static_cast<core::f32>(i) / segments * kTwoPi;
                addBox(snapshot.meshes,
                       effect.position + core::Vec3{std::cos(angle) * radius, 0.15f,
                                                    std::sin(angle) * radius},
                       {0.22f, 0.22f, 0.22f}, effect.color);
            }
        } else {
            const core::f32 scale = effect.size * (1.0f - progress * 0.7f);
            addBox(snapshot.meshes, effect.position, {scale, scale, scale}, effect.color);
        }
    }
}

void RpgGame::appendCampfire(scene::SceneSnapshot& snapshot) const {
    const core::f32 flicker = 0.12f * std::sin(time_ * 14.0f);
    addBox(snapshot.meshes, firePosition_ + core::Vec3{0.0f, 0.40f, 0.0f},
           {0.42f + flicker, 0.65f + flicker, 0.42f + flicker},
           {1.0f, 0.42f, 0.08f, 1.0f}, {0.0f, time_ * 2.0f, 0.0f});
    addBox(snapshot.meshes, firePosition_ + core::Vec3{0.0f, 0.55f, 0.0f},
           {0.22f, 0.55f, 0.22f}, {1.0f, 0.85f, 0.30f, 1.0f},
           {0.0f, -time_ * 3.0f, 0.0f});
    addBox(snapshot.meshes, firePosition_ + core::Vec3{0.0f, 0.90f, 0.0f},
           {0.12f, 0.28f, 0.12f}, {1.0f, 0.95f, 0.65f, 1.0f});
}

void RpgGame::appendPortal(scene::SceneSnapshot& snapshot) const {
    if (!portalActive_) {
        return;
    }
    for (core::i32 i = 0; i < 10; ++i) {
        const core::f32 angle = static_cast<core::f32>(i) / 10.0f * kTwoPi + time_ * 0.8f;
        addBox(snapshot.meshes,
               {portalPosition_.x + std::cos(angle) * 1.6f, 1.2f,
                portalPosition_.z + std::sin(angle) * 1.6f},
               {0.25f, 2.4f, 0.25f}, {0.65f, 0.35f, 1.0f, 1.0f});
    }
    addBox(snapshot.meshes, portalPosition_ + core::Vec3{0.0f, 1.2f, 0.0f},
           {0.8f, 2.0f, 0.8f}, {0.35f, 0.15f, 0.6f, 1.0f});
}

scene::SceneSnapshot RpgGame::buildSnapshot() const { return buildSnapshot(localViewer_); }

scene::SceneSnapshot RpgGame::buildSnapshot(core::PlayerId viewer) const {
    const PlayerState& viewerPlayer = players_[static_cast<core::usize>(viewer)];
    const ZoneDef& def = zone();
    scene::SceneSnapshot snapshot;
    snapshot.clearColor = def.sky;
    snapshot.lightDirection = {def.lightDirection.x, def.lightDirection.y, def.lightDirection.z};
    snapshot.ambient = def.ambient;

    const core::Vec3 offset{std::sin(viewerPlayer.camera.yaw) * std::cos(viewerPlayer.camera.pitch),
                            std::sin(viewerPlayer.camera.pitch),
                            std::cos(viewerPlayer.camera.yaw) * std::cos(viewerPlayer.camera.pitch)};
    const core::Vec3 target = viewerPlayer.camera.target;
    snapshot.camera.position = target + offset * viewerPlayer.camera.smoothedDistance;
    snapshot.camera.target = target;
    snapshot.camera.up = {0.0f, 1.0f, 0.0f};
    snapshot.camera.fovYRadians = kFovY;
    snapshot.camera.nearZ = 0.05f;
    snapshot.camera.farZ = 250.0f;

    snapshot.meshes.reserve(staticMeshes_.size() + 96);
    snapshot.meshes.insert(snapshot.meshes.end(), staticMeshes_.begin(), staticMeshes_.end());
    for (core::u8 i = 0; i < activePlayers_; ++i) {
        appendPlayer(snapshot, players_[i]);
    }
    appendEnemies(snapshot);
    appendNpcs(snapshot);
    appendLoot(snapshot);
    appendProjectiles(snapshot);
    appendEffects(snapshot);
    appendPortal(snapshot);
    appendCampfire(snapshot);

    return snapshot;
}

core::usize RpgGame::menuOptionCount() const {
    switch (mode_) {
    case UiMode::ClassSelect: return classCount();
    case UiMode::Paused: return 5;
    case UiMode::Dead: return 3;
    case UiMode::Victory: return 3;
    default: return 0;
    }
}

std::string_view RpgGame::menuOption(core::usize index) const {
    switch (mode_) {
    case UiMode::ClassSelect:
        return index < classCount() ? std::string_view(classDef(static_cast<ClassId>(index)).name)
                                    : std::string_view{};
    case UiMode::Paused:
        return index < 5 ? kPausedOptions[index] : std::string_view{};
    case UiMode::Dead:
        return index < 3 ? kDeadOptions[index] : std::string_view{};
    case UiMode::Victory:
        return index < 3 ? kVictoryOptions[index] : std::string_view{};
    default:
        return {};
    }
}

core::u64 RpgGame::checksum() const {
    core::u64 hash = 1469598103934665603ull;
    auto mixByte = [&](core::u8 value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    auto mixU64 = [&](core::u64 value) {
        for (int i = 0; i < 8; ++i) mixByte(static_cast<core::u8>((value >> (8 * i)) & 0xFF));
    };
    auto mixF32 = [&](core::f32 value) {
        core::u32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        mixU64(bits);
    };
    auto mixVec3 = [&](core::Vec3 value) {
        mixF32(value.x);
        mixF32(value.y);
        mixF32(value.z);
    };

    // Only replicated state: a client that applied a server snapshot must hash
    // identically to the authoritative server.
    mixU64(zoneIndex_);
    mixU64(activePlayers_);
    mixByte(portalActive_ ? 1 : 0);
    for (core::u8 i = 0; i < activePlayers_; ++i) {
        const PlayerState& player = players_[i];
        mixVec3(player.position);
        mixF32(player.yaw);
        mixF32(player.health);
        mixF32(player.maxHealth);
        mixF32(player.mana);
        mixF32(player.maxMana);
        mixF32(player.jumpHeight);
        mixU64(static_cast<core::u64>(player.loadout.level));
        mixU64(static_cast<core::u64>(player.loadout.xp));
        mixU64(static_cast<core::u64>(player.loadout.gold));
        mixU64(static_cast<core::u64>(player.loadout.kills));
        mixByte(static_cast<core::u8>(player.classId));
        mixByte(player.playerDead ? 1 : 0);
    }
    for (const EnemyState& enemy : enemies_) {
        mixVec3(enemy.position);
        mixF32(enemy.yaw);
        mixF32(enemy.health);
        mixF32(enemy.maxHealth);
        mixByte(enemy.alive ? 1 : 0);
    }
    mixU64(loot_.size());
    for (const LootDrop& drop : loot_) {
        mixVec3(drop.position);
        mixByte(static_cast<core::u8>(drop.item.rarity));
    }
    mixU64(projectiles_.size());
    for (const Projectile& projectile : projectiles_) mixVec3(projectile.position);
    return hash;
}

namespace {

core::ItemNetState itemToNet(const Item& item) {
    core::ItemNetState out;
    out.name = item.name;
    out.baseName = item.baseName;
    out.slot = static_cast<core::u8>(item.slot);
    out.rarity = static_cast<core::u8>(item.rarity);
    out.itemLevel = item.itemLevel;
    out.baseDamage = item.baseDamage;
    out.baseArmour = item.baseArmour;
    out.manaFlask = item.manaFlask;
    out.flaskPower = item.flaskPower;
    out.gemSkill = static_cast<core::u8>(item.gemSkill);
    out.gemClass = static_cast<core::u8>(item.gemClass);
    out.mods.reserve(item.mods.size());
    for (const Modifier& mod : item.mods) {
        out.mods.push_back(core::ModifierNetState{static_cast<core::u8>(mod.stat), mod.value,
                                                  mod.label});
    }
    return out;
}

Item itemFromNet(const core::ItemNetState& source, core::u32 id) {
    Item item;
    if (source.name.empty()) {
        return item;
    }
    item.id = id;
    item.name = source.name;
    item.baseName = source.baseName;
    item.slot = static_cast<ItemSlot>(source.slot);
    item.rarity = static_cast<Rarity>(source.rarity);
    item.itemLevel = source.itemLevel;
    item.baseDamage = source.baseDamage;
    item.baseArmour = source.baseArmour;
    item.manaFlask = source.manaFlask;
    item.flaskPower = source.flaskPower;
    item.gemSkill = static_cast<SkillId>(source.gemSkill);
    item.gemClass = static_cast<ClassId>(source.gemClass);
    item.mods.reserve(source.mods.size());
    for (const core::ModifierNetState& mod : source.mods) {
        item.mods.push_back(Modifier{static_cast<StatId>(mod.stat), mod.value, mod.label});
    }
    return item;
}

} // namespace

core::WorldSnapshot RpgGame::captureNetworkSnapshot(core::u64 tick) const {
    core::WorldSnapshot snapshot;
    snapshot.tick = tick;
    snapshot.zoneIndex = zoneIndex_;
    snapshot.portalActive = portalActive_;
    for (core::u8 i = 0; i < 2; ++i) {
        const PlayerState& source = players_[i];
        core::PlayerNetState& target = snapshot.players[i];
        target.position = source.position;
        target.yaw = source.yaw;
        target.health = source.health;
        target.maxHealth = source.maxHealth;
        target.mana = source.mana;
        target.maxMana = source.maxMana;
        target.jumpHeight = source.jumpHeight;
        target.attackCooldown = source.attackCooldown;
        target.qCooldown = source.qCooldown;
        target.eCooldown = source.eCooldown;
        target.damage = source.damage;
        target.armour = source.armour;
        target.moveSpeed = source.moveSpeed;
        target.critChance = source.critChance;
        target.secondaryUnlocked = source.loadout.secondaryUnlocked;
        target.level = source.loadout.level;
        target.xp = source.loadout.xp;
        target.gold = source.loadout.gold;
        target.kills = source.loadout.kills;
        target.classId = static_cast<core::u8>(source.classId);
        target.dead = source.playerDead;

        target.inventory.clear();
        target.inventory.reserve(source.loadout.inventory.size());
        for (const Item& item : source.loadout.inventory) {
            target.inventory.push_back(itemToNet(item));
        }
        for (core::usize slot = 0; slot < target.equipment.size(); ++slot) {
            target.equipment[slot] = itemToNet(source.loadout.equipment[slot]);
        }
    }
    for (const EnemyState& enemy : enemies_) {
        core::EnemyNetState out;
        out.position = enemy.position;
        out.yaw = enemy.yaw;
        out.health = enemy.health;
        out.maxHealth = enemy.maxHealth;
        out.scale = enemy.scale;
        out.color = enemy.color;
        out.alive = enemy.alive;
        out.boss = enemy.boss;
        out.ranged = enemy.ranged;
        snapshot.enemies.push_back(out);
    }
    for (const Projectile& projectile : projectiles_) {
        core::ProjectileNetState out;
        out.position = projectile.position;
        out.color = projectile.color;
        out.radius = projectile.radius;
        out.fromPlayer = projectile.fromPlayer;
        snapshot.projectiles.push_back(out);
    }
    for (const LootDrop& drop : loot_) {
        core::LootNetState out;
        out.position = drop.position;
        out.rarity = static_cast<core::u8>(drop.item.rarity);
        snapshot.loot.push_back(out);
    }
    return snapshot;
}

void RpgGame::applyRemoteInterpolation(core::PlayerId localPlayer,
                                       const core::WorldSnapshot& previous,
                                       const core::WorldSnapshot& current,
                                       core::f32 alpha) {
    alpha = core::clamp(alpha, 0.0f, 1.0f);
    const core::usize localIndex = static_cast<core::usize>(localPlayer);

    // Other players: positions, yaw and health are visual-only. The local
    // player stays untouched so client prediction owns its movement.
    for (core::usize i = 0; i < players_.size(); ++i) {
        if (i == localIndex) continue;
        const core::PlayerNetState& from = previous.players[i];
        const core::PlayerNetState& to = current.players[i];
        PlayerState& target = players_[i];
        target.position = lerpVec3(from.position, to.position, alpha);
        target.yaw = lerpFloat(from.yaw, to.yaw, alpha);
        target.health = lerpFloat(from.health, to.health, alpha);
    }

    // Enemies: blend by index only while both snapshots have the same roster;
    // otherwise the current authoritative state wins. Appearance and status
    // flags always come from the current snapshot.
    const bool interpolateEnemies = previous.enemies.size() == current.enemies.size();
    const core::usize enemyCount = std::min(enemies_.size(), current.enemies.size());
    for (core::usize i = 0; i < enemyCount; ++i) {
        const core::EnemyNetState& to = current.enemies[i];
        EnemyState& target = enemies_[i];
        if (interpolateEnemies) {
            const core::EnemyNetState& from = previous.enemies[i];
            target.position = lerpVec3(from.position, to.position, alpha);
            target.yaw = lerpFloat(from.yaw, to.yaw, alpha);
            target.health = lerpFloat(from.health, to.health, alpha);
        } else {
            target.position = to.position;
            target.yaw = to.yaw;
            target.health = to.health;
        }
        target.scale = to.scale;
        target.color = to.color;
        target.alive = to.alive;
        target.boss = to.boss;
        target.ranged = to.ranged;
    }

    // Projectiles and loot follow the same index-matched interpolation rule.
    const bool interpolateProjectiles =
        previous.projectiles.size() == current.projectiles.size();
    const core::usize projectileCount =
        std::min(projectiles_.size(), current.projectiles.size());
    for (core::usize i = 0; i < projectileCount; ++i) {
        const core::ProjectileNetState& to = current.projectiles[i];
        projectiles_[i].position =
            interpolateProjectiles
                ? lerpVec3(previous.projectiles[i].position, to.position, alpha)
                : to.position;
    }

    const bool interpolateLoot = previous.loot.size() == current.loot.size();
    const core::usize lootCount = std::min(loot_.size(), current.loot.size());
    for (core::usize i = 0; i < lootCount; ++i) {
        const core::LootNetState& to = current.loot[i];
        loot_[i].position = interpolateLoot
                                ? lerpVec3(previous.loot[i].position, to.position, alpha)
                                : to.position;
    }
}

bool RpgGame::applyInventoryAction(core::PlayerId id, core::u8 action, core::u16 index) {
    auto& inv = players_[static_cast<core::usize>(id)].loadout.inventory;
    if (index >= inv.size()) return false;

    const core::PlayerId previousActive = activeId_;
    const core::i32 previousSelection = inventorySelection_;
    activeId_ = id;
    inventorySelection_ = static_cast<core::i32>(index);
    bool ok = true;
    if (action == 0) {
        useInventoryItem();
    } else if (action == 1) {
        dropSelectedItem();
    } else {
        ok = false;
    }
    activeId_ = previousActive;
    inventorySelection_ = previousSelection;
    return ok;
}

void RpgGame::applyNetworkSnapshot(const core::WorldSnapshot& snapshot) {
    if (snapshot.zoneIndex != zoneIndex_) {
        loadZone(snapshot.zoneIndex);
    }
    activePlayers_ = 2;
    time_ = static_cast<core::f32>(snapshot.tick) / 60.0f;
    portalActive_ = snapshot.portalActive;

    for (core::u8 i = 0; i < 2; ++i) {
        PlayerState& target = players_[i];
        const core::PlayerNetState& source = snapshot.players[i];
        target.position = source.position;
        target.yaw = source.yaw;
        target.health = source.health;
        target.maxHealth = source.maxHealth;
        target.mana = source.mana;
        target.maxMana = source.maxMana;
        target.jumpHeight = source.jumpHeight;
        target.attackCooldown = source.attackCooldown;
        target.qCooldown = source.qCooldown;
        target.eCooldown = source.eCooldown;
        target.damage = source.damage;
        target.armour = source.armour;
        target.moveSpeed = source.moveSpeed;
        target.critChance = source.critChance;
        target.loadout.secondaryUnlocked = source.secondaryUnlocked;
        target.classId = static_cast<ClassId>(source.classId);
        target.loadout.level = source.level;
        target.loadout.xp = source.xp;
        target.loadout.gold = source.gold;
        target.loadout.kills = source.kills;
        target.playerDead = source.dead;
        target.respawnPoint = target.position;

        target.loadout.inventory.clear();
        target.loadout.inventory.reserve(source.inventory.size());
        for (const core::ItemNetState& item : source.inventory) {
            if (item.name.empty()) continue;
            target.loadout.inventory.push_back(itemFromNet(item, ++itemId_));
        }
        for (core::usize slot = 0; slot < target.loadout.equipment.size(); ++slot) {
            const core::ItemNetState& item = source.equipment[slot];
            target.loadout.equipment[slot] =
                item.name.empty() ? Item{} : itemFromNet(item, ++itemId_);
        }

        ItemTotals totals;
        for (const Item& item : target.loadout.equipment) {
            if (!item.empty()) {
                totals.add(item);
            }
        }
        target.loadout.totals = totals;
    }

    const core::i32 inventoryCount =
        static_cast<core::i32>(active().loadout.inventory.size());
    if (inventoryCount <= 0) {
        inventorySelection_ = 0;
    } else if (inventorySelection_ >= inventoryCount) {
        inventorySelection_ = inventoryCount - 1;
    }

    enemies_.clear();
    enemies_.reserve(snapshot.enemies.size());
    for (const core::EnemyNetState& source : snapshot.enemies) {
        EnemyState enemy;
        enemy.position = source.position;
        enemy.home = source.position;
        enemy.yaw = source.yaw;
        enemy.health = source.health;
        enemy.maxHealth = source.maxHealth;
        enemy.scale = source.scale;
        enemy.color = source.color;
        enemy.alive = source.alive;
        enemy.boss = source.boss;
        enemy.ranged = source.ranged;
        enemies_.push_back(enemy);
    }

    projectiles_.clear();
    projectiles_.reserve(snapshot.projectiles.size());
    for (const core::ProjectileNetState& source : snapshot.projectiles) {
        Projectile projectile;
        projectile.position = source.position;
        projectile.color = source.color;
        projectile.radius = source.radius;
        projectile.fromPlayer = source.fromPlayer;
        projectiles_.push_back(projectile);
    }

    loot_.clear();
    loot_.reserve(snapshot.loot.size());
    for (const core::LootNetState& source : snapshot.loot) {
        LootDrop drop;
        drop.position = source.position;
        drop.item.rarity = static_cast<Rarity>(source.rarity);
        drop.item.name = "LOOT";
        loot_.push_back(drop);
    }
    effects_.clear();
    interactionHint_.clear();
    messageTimer_ = 0.0f;
    message_.clear();
}

} // namespace eee::logic


