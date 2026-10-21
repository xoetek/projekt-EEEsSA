#include "eee/logic/RpgGame.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace eee::logic {
namespace {

constexpr core::f32 kPi = 3.14159265359f;
constexpr core::f32 kTwoPi = 6.28318530718f;
constexpr core::f32 kBounds = 40.0f;
constexpr core::f32 kPlayerSpeed = 5.0f;
constexpr core::f32 kEnemyAggro = 13.0f;
constexpr core::f32 kEnemySpeed = 2.0f;

constexpr std::string_view kPausedOptions[] = {"RESUME", "RESTART", "QUIT"};
constexpr std::string_view kDeadOptions[] = {"RESTART", "QUIT"};

core::Vec4 mix(core::Vec4 a, core::Vec4 b, core::f32 t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t};
}

core::f32 wrapAngle(core::f32 angle) {
    while (angle > kPi) {
        angle -= kTwoPi;
    }
    while (angle < -kPi) {
        angle += kTwoPi;
    }
    return angle;
}

} // namespace

RpgGame::RpgGame() {
    randomState_ = 0xC0FFEEu;
    buildStaticWorld();

    struct NpcDef {
        const char* name;
        const char* line;
        core::Vec4 robe;
        core::Vec4 hat;
        core::f32 angle;
    };
    const NpcDef defs[] = {
        {"ELDER", "THE FOREST GROWS DARK. BEWARE THE SLIMES.", {0.35f, 0.5f, 0.85f, 1.0f},
         {0.85f, 0.8f, 0.4f, 1.0f}, 0.8f},
        {"SMITH", "BRING ME SLIME CORES AND I WILL FORGE STEEL.", {0.75f, 0.3f, 0.25f, 1.0f},
         {0.3f, 0.3f, 0.35f, 1.0f}, 2.6f},
        {"HERBALIST", "REST BY THE FIRE. YOUR WOUNDS WILL HEAL.", {0.3f, 0.65f, 0.4f, 1.0f},
         {0.55f, 0.35f, 0.2f, 1.0f}, 4.8f},
    };

    for (const NpcDef& def : defs) {
        NpcState npc;
        const core::f32 radius = 9.5f;
        npc.position = {std::cos(def.angle) * radius, 0.0f, std::sin(def.angle) * radius};
        npc.yaw = std::atan2(-npc.position.x, -npc.position.z);
        npc.robe = def.robe;
        npc.hat = def.hat;
        npc.name = def.name;
        npc.line = def.line;
        npcs_.push_back(npc);
    }

    reset();
}

core::f32 RpgGame::randomFloat() {
    core::u32 x = randomState_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    randomState_ = x;
    return static_cast<core::f32>(x >> 8) * (1.0f / 16777216.0f);
}

void RpgGame::addBox(std::vector<scene::MeshInstance>& out, core::Vec3 center, core::Vec3 size,
                     core::Vec4 color, core::Vec3 euler) {
    scene::MeshInstance instance;
    instance.transform = core::Mat4::trs3(center, euler, size);
    instance.color = color;
    out.push_back(instance);
}

core::Vec3 RpgGame::rotateY(core::Vec3 value, core::f32 yaw) {
    const core::f32 c = std::cos(yaw);
    const core::f32 s = std::sin(yaw);
    return {value.x * c + value.z * s, value.y, -value.x * s + value.z * c};
}

void RpgGame::buildStaticWorld() {
    staticMeshes_.clear();
    staticMeshes_.reserve(12000);

    constexpr core::i32 kHalf = 48;
    for (core::i32 z = -kHalf; z <= kHalf; ++z) {
        for (core::i32 x = -kHalf; x <= kHalf; ++x) {
            const bool path = std::abs(x) <= 1 || std::abs(z) <= 1;
            const core::u32 hash =
                static_cast<core::u32>(x * 73856093) ^ static_cast<core::u32>(z * 19349663);
            const core::f32 noise = static_cast<core::f32>(hash & 1023u) / 1023.0f;

            const core::Vec4 grass = mix({0.16f, 0.38f, 0.19f, 1.0f}, {0.28f, 0.52f, 0.25f, 1.0f},
                                         noise);
            const core::Vec4 dirt = mix({0.42f, 0.33f, 0.22f, 1.0f}, {0.50f, 0.40f, 0.28f, 1.0f},
                                        noise);
            addBox(staticMeshes_, {static_cast<core::f32>(x), -0.1f, static_cast<core::f32>(z)},
                   {0.98f, 0.2f, 0.98f}, path ? dirt : grass);
        }
    }

    // Trees.
    core::i32 trees = 0;
    while (trees < 180) {
        const core::f32 x = randomFloat() * 92.0f - 46.0f;
        const core::f32 z = randomFloat() * 92.0f - 46.0f;
        if (std::abs(x) < 3.0f || std::abs(z) < 3.0f || x * x + z * z < 64.0f) {
            continue;
        }
        const core::f32 size = 0.8f + randomFloat() * 0.8f;
        addBox(staticMeshes_, {x, 1.0f, z}, {0.35f, 2.0f, 0.35f},
               {0.30f, 0.20f, 0.12f, 1.0f});
        addBox(staticMeshes_, {x, 2.5f + size * 0.2f, z},
               {1.4f + size, 1.6f + size, 1.4f + size},
               mix({0.12f, 0.40f, 0.16f, 1.0f}, {0.22f, 0.55f, 0.22f, 1.0f}, randomFloat()));
        ++trees;
    }

    // Rocks.
    for (core::i32 i = 0; i < 80; ++i) {
        const core::f32 x = randomFloat() * 92.0f - 46.0f;
        const core::f32 z = randomFloat() * 92.0f - 46.0f;
        if (std::abs(x) < 3.0f || std::abs(z) < 3.0f || x * x + z * z < 36.0f) {
            continue;
        }
        const core::f32 size = 0.4f + randomFloat() * 1.1f;
        const core::f32 shade = 0.35f + randomFloat() * 0.25f;
        addBox(staticMeshes_, {x, size * 0.5f, z}, {size, size, size},
               {shade, shade, shade * 1.05f, 1.0f},
               {0.0f, randomFloat() * kTwoPi, 0.0f});
    }

    // Huts arranged around the village square.
    for (core::i32 i = 0; i < 4; ++i) {
        const core::f32 angle = kPi * 0.25f + static_cast<core::f32>(i) * kPi * 0.5f;
        const core::f32 x = std::cos(angle) * 13.0f;
        const core::f32 z = std::sin(angle) * 13.0f;
        addBox(staticMeshes_, {x, 1.2f, z}, {3.0f, 2.4f, 3.0f},
               {0.62f, 0.52f, 0.38f, 1.0f}, {0.0f, -angle, 0.0f});
        addBox(staticMeshes_, {x, 2.65f, z}, {3.8f, 0.6f, 3.8f},
               {0.42f, 0.16f, 0.14f, 1.0f}, {0.0f, -angle, 0.0f});
    }

    // Campfire on the road, in front of the player spawn.
    addBox(staticMeshes_, {0.0f, 0.08f, -4.0f}, {1.0f, 0.16f, 0.16f},
           {0.30f, 0.20f, 0.12f, 1.0f}, {0.0f, 0.7f, 0.0f});
    addBox(staticMeshes_, {0.0f, 0.08f, -4.0f}, {1.0f, 0.16f, 0.16f},
           {0.30f, 0.20f, 0.12f, 1.0f}, {0.0f, -0.7f, 0.0f});
    addBox(staticMeshes_, {0.0f, 0.35f, -4.0f}, {0.35f, 0.55f, 0.35f},
           {1.0f, 0.45f, 0.10f, 1.0f});
}

void RpgGame::reset() {
    player_ = PlayerState{};

    enemies_.clear();
    for (core::i32 i = 0; i < 8; ++i) {
        const core::f32 angle = static_cast<core::f32>(i) * kTwoPi / 8.0f + 0.4f;
        const core::f32 radius = 17.0f + static_cast<core::f32>(i % 3) * 3.0f;
        EnemyState enemy;
        enemy.home = {std::cos(angle) * radius, 0.0f, std::sin(angle) * radius};
        enemy.position = enemy.home;
        enemy.yaw = std::atan2(-enemy.home.x, -enemy.home.z);
        enemy.health = enemy.maxHealth;
        enemy.bob = static_cast<core::f32>(i);
        enemies_.push_back(enemy);
    }

    cameraYaw_ = 0.0f;
    cameraPitch_ = 0.32f;
    cameraDistance_ = 6.5f;
    lastMouse_ = {};
    hasLastMouse_ = false;

    paused_ = false;
    dead_ = false;
    quitRequested_ = false;
    selection_ = 0;

    dialogueTimer_ = 0.0f;
    dialogueName_ = {};
    dialogueText_ = {};
    time_ = 0.0f;
}

core::u32 RpgGame::aliveEnemies() const {
    core::u32 count = 0;
    for (const EnemyState& enemy : enemies_) {
        count += enemy.alive ? 1 : 0;
    }
    return count;
}

core::usize RpgGame::menuOptionCount() const { return dead_ ? 2u : 3u; }

std::string_view RpgGame::menuOption(core::usize index) const {
    if (dead_) {
        return index < 2 ? kDeadOptions[index] : std::string_view{};
    }
    return index < 3 ? kPausedOptions[index] : std::string_view{};
}

void RpgGame::update(core::f32 dt, const core::InputState& input) {
    dt = std::clamp(dt, 0.0f, 0.1f);

    if (input.wasPressed(core::Key::Escape) && !dead_) {
        paused_ = !paused_;
        selection_ = 0;
        hasLastMouse_ = false;
        return;
    }

    if (menuActive()) {
        updateMenu(input);
        return;
    }

    updatePlaying(dt, input);
}

void RpgGame::updateMenu(const core::InputState& input) {
    const core::usize count = menuOptionCount();
    if (count == 0) {
        return;
    }

    if (input.wasPressed(core::Key::Up) || input.wasPressed('W')) {
        selection_ = (selection_ + static_cast<core::i32>(count) - 1) %
                     static_cast<core::i32>(count);
    }
    if (input.wasPressed(core::Key::Down) || input.wasPressed('S')) {
        selection_ = (selection_ + 1) % static_cast<core::i32>(count);
    }

    if (input.wasPressed(core::Key::Enter) || input.wasPressed(core::Key::Space)) {
        if (dead_) {
            if (selection_ == 0) {
                reset();
            } else {
                quitRequested_ = true;
            }
        } else {
            if (selection_ == 0) {
                paused_ = false;
            } else if (selection_ == 1) {
                reset();
            } else {
                quitRequested_ = true;
            }
        }
    }
}

void RpgGame::updatePlaying(core::f32 dt, const core::InputState& input) {
    time_ += dt;
    player_.attackCooldown = std::max(0.0f, player_.attackCooldown - dt);
    player_.attackTimer = std::max(0.0f, player_.attackTimer - dt);
    dialogueTimer_ = std::max(0.0f, dialogueTimer_ - dt);

    // Mouse look (right-button drag).
    if (input.mouseDown[1]) {
        if (hasLastMouse_) {
            const core::f32 dx = input.mousePosition.x - lastMouse_.x;
            const core::f32 dy = input.mousePosition.y - lastMouse_.y;
            cameraYaw_ -= dx * 0.006f;
            cameraPitch_ -= dy * 0.004f;
            cameraPitch_ = core::clamp(cameraPitch_, 0.08f, 1.15f);
        }
        lastMouse_ = input.mousePosition;
        hasLastMouse_ = true;
    } else {
        hasLastMouse_ = false;
    }

    // Keyboard camera orbit.
    if (input.isDown(core::Key::Left)) {
        cameraYaw_ += 1.6f * dt;
    }
    if (input.isDown(core::Key::Right)) {
        cameraYaw_ -= 1.6f * dt;
    }
    if (input.isDown(core::Key::Up)) {
        cameraPitch_ += 1.1f * dt;
    }
    if (input.isDown(core::Key::Down)) {
        cameraPitch_ -= 1.1f * dt;
    }
    cameraPitch_ = core::clamp(cameraPitch_, 0.08f, 1.15f);
    if (input.scrollDelta != 0.0f) {
        cameraDistance_ = core::clamp(cameraDistance_ - input.scrollDelta * 0.01f, 3.2f, 11.0f);
    }

    // Movement relative to the camera yaw.
    const core::Vec3 forward{-std::sin(cameraYaw_), 0.0f, -std::cos(cameraYaw_)};
    const core::Vec3 right{std::cos(cameraYaw_), 0.0f, -std::sin(cameraYaw_)};
    core::Vec3 move{};
    if (input.isDown('W')) {
        move += forward;
    }
    if (input.isDown('S')) {
        move -= forward;
    }
    if (input.isDown('D')) {
        move += right;
    }
    if (input.isDown('A')) {
        move -= right;
    }

    const bool moving = move.lengthSquared() > 1e-5f;
    if (moving) {
        move = normalize(move);
        const bool sprint = input.isDown(core::Key::LeftShift) ||
                            input.isDown(core::Key::RightShift);
        const core::f32 speed = kPlayerSpeed * (sprint ? 1.55f : 1.0f);
        player_.position += move * (speed * dt);
        player_.position.x = core::clamp(player_.position.x, -kBounds, kBounds);
        player_.position.z = core::clamp(player_.position.z, -kBounds, kBounds);

        const core::f32 targetYaw = std::atan2(-move.x, -move.z);
        const core::f32 difference = wrapAngle(targetYaw - player_.yaw);
        player_.yaw += difference * std::min(1.0f, 12.0f * dt);
        player_.walkTime += dt * (speed / kPlayerSpeed);
    } else {
        player_.walkTime = 0.0f;
    }

    if ((input.wasPressed(core::Key::Space) || input.mousePressed[0]) &&
        player_.attackCooldown <= 0.0f) {
        tryAttack();
    }

    handleInteraction(input);
    updateEnemies(dt);

    player_.health = std::min(player_.maxHealth, player_.health + 0.8f * dt);
    if (player_.health <= 0.0f) {
        player_.health = 0.0f;
        dead_ = true;
        paused_ = false;
        selection_ = 0;
        dialogueTimer_ = 0.0f;
        hasLastMouse_ = false;
    }
}

void RpgGame::updateEnemies(core::f32 dt) {
    for (EnemyState& enemy : enemies_) {
        if (!enemy.alive) {
            enemy.respawnTimer -= dt;
            if (enemy.respawnTimer <= 0.0f) {
                enemy.alive = true;
                enemy.health = enemy.maxHealth;
                enemy.position = enemy.home;
                enemy.attackCooldown = 0.0f;
            }
            continue;
        }

        enemy.bob += dt * 5.0f;
        enemy.attackCooldown = std::max(0.0f, enemy.attackCooldown - dt);

        core::Vec3 toPlayer = player_.position - enemy.position;
        toPlayer.y = 0.0f;
        const core::f32 distance = toPlayer.length();
        if (distance > kEnemyAggro || distance < 1e-4f) {
            continue;
        }

        enemy.yaw = std::atan2(-toPlayer.x, -toPlayer.z);
        if (distance > 1.9f) {
            const core::Vec3 direction = toPlayer * (1.0f / distance);
            enemy.position += direction * (kEnemySpeed * dt);
            enemy.position.x = core::clamp(enemy.position.x, -kBounds, kBounds);
            enemy.position.z = core::clamp(enemy.position.z, -kBounds, kBounds);
        } else if (enemy.attackCooldown <= 0.0f) {
            player_.health -= 7.0f;
            enemy.attackCooldown = 1.4f;
        }
    }
}

void RpgGame::tryAttack() {
    player_.attackTimer = 0.28f;
    player_.attackCooldown = 0.5f;

    const core::Vec3 forward{-std::sin(player_.yaw), 0.0f, -std::cos(player_.yaw)};
    const core::f32 damage = 14.0f + static_cast<core::f32>(player_.level) * 4.0f;

    for (EnemyState& enemy : enemies_) {
        if (!enemy.alive) {
            continue;
        }
        core::Vec3 toEnemy = enemy.position - player_.position;
        toEnemy.y = 0.0f;
        const core::f32 distance = toEnemy.length();
        if (distance > 2.9f) {
            continue;
        }
        if (distance > 0.05f) {
            const core::Vec3 direction = toEnemy * (1.0f / distance);
            if (dot(direction, forward) < 0.1f) {
                continue;
            }
        }

        enemy.health -= damage;
        if (enemy.health <= 0.0f) {
            enemy.health = 0.0f;
            enemy.alive = false;
            enemy.respawnTimer = 6.0f;
            applyKillRewards();
        }
    }
}

void RpgGame::applyKillRewards() {
    player_.gold += 8;
    ++player_.kills;
    player_.xp += 25;
    checkLevelUp();
}

void RpgGame::checkLevelUp() {
    while (player_.xp >= player_.xpToNext) {
        player_.xp -= player_.xpToNext;
        ++player_.level;
        player_.maxHealth += 15.0f;
        player_.health = player_.maxHealth;
        player_.xpToNext = 40 + (player_.level - 1) * 30;
    }
}

void RpgGame::handleInteraction(const core::InputState& input) {
    if (!input.wasPressed('E')) {
        return;
    }

    const NpcState* closest = nullptr;
    core::f32 closestDistance = 3.5f;
    for (const NpcState& npc : npcs_) {
        const core::f32 dx = npc.position.x - player_.position.x;
        const core::f32 dz = npc.position.z - player_.position.z;
        const core::f32 distance = std::sqrt(dx * dx + dz * dz);
        if (distance < closestDistance) {
            closestDistance = distance;
            closest = &npc;
        }
    }

    if (closest != nullptr) {
        dialogueTimer_ = 4.0f;
        dialogueName_ = closest->name;
        dialogueText_ = closest->line;
    }
}

void RpgGame::appendPlayer(scene::SceneSnapshot& snapshot) const {
    const core::Vec3 base = player_.position;
    const core::f32 yaw = player_.yaw;
    const bool moving = player_.walkTime > 0.0f;
    const core::f32 stride =
        moving ? std::sin(player_.walkTime * 10.0f) * 0.55f : 0.0f;
    const core::f32 bob = moving ? std::fabs(std::sin(player_.walkTime * 10.0f)) * 0.04f : 0.0f;
    const core::f32 attackSwing =
        player_.attackTimer > 0.0f
            ? std::sin((1.0f - player_.attackTimer / 0.28f) * kPi) * 1.8f
            : 0.0f;

    const core::Vec4 tunic{0.20f, 0.35f, 0.75f, 1.0f};
    const core::Vec4 skin{0.90f, 0.72f, 0.55f, 1.0f};
    const core::Vec4 pants{0.20f, 0.18f, 0.24f, 1.0f};
    const core::Vec4 hat{0.32f, 0.22f, 0.14f, 1.0f};
    const core::Vec4 steel{0.80f, 0.82f, 0.88f, 1.0f};

    auto place = [&](core::Vec3 local, core::Vec3 size, core::Vec4 color, core::f32 tilt) {
        const core::Vec3 world = base + rotateY(local, yaw);
        addBox(snapshot.meshes, world, size, color, {tilt, yaw, 0.0f});
    };

    // Legs and body.
    place({-0.18f, 0.35f, 0.0f}, {0.24f, 0.7f, 0.28f}, pants, stride);
    place({0.18f, 0.35f, 0.0f}, {0.24f, 0.7f, 0.28f}, pants, -stride);
    place({0.0f, 1.02f + bob, 0.0f}, {0.72f, 0.95f, 0.46f}, tunic, 0.0f);
    place({0.0f, 1.70f + bob, 0.0f}, {0.44f, 0.44f, 0.44f}, skin, 0.0f);
    place({0.0f, 1.95f + bob, 0.0f}, {0.50f, 0.12f, 0.50f}, hat, 0.0f);
    place({-0.48f, 1.10f + bob, 0.0f}, {0.18f, 0.62f, 0.18f}, tunic, -stride * 0.8f);
    place({0.48f, 1.10f + bob, 0.0f}, {0.18f, 0.62f, 0.18f}, tunic, stride * 0.8f);
    // Sword in the right hand.
    place({0.58f, 1.02f + bob, -0.34f}, {0.08f, 0.9f, 0.08f}, steel, attackSwing);
    place({0.58f, 1.02f + bob, -0.34f}, {0.28f, 0.08f, 0.12f}, hat, attackSwing);
}

void RpgGame::appendEnemies(scene::SceneSnapshot& snapshot) const {
    for (const EnemyState& enemy : enemies_) {
        if (!enemy.alive) {
            continue;
        }

        const core::f32 bob = std::sin(enemy.bob) * 0.08f;
        const core::f32 fraction =
            enemy.maxHealth > 0.0f ? core::clamp(enemy.health / enemy.maxHealth, 0.0f, 1.0f)
                                   : 0.0f;
        const core::Vec4 color =
            mix({0.85f, 0.25f, 0.25f, 1.0f}, {0.28f, 0.62f, 0.28f, 1.0f}, fraction);

        const core::Vec3 body{enemy.position.x, 0.55f + bob, enemy.position.z};
        addBox(snapshot.meshes, body, {0.95f, 0.95f, 0.95f}, color, {0.0f, enemy.yaw, 0.0f});

        // Eyes on the forward side (local -Z).
        for (const core::f32 side : {-1.0f, 1.0f}) {
            const core::Vec3 local{side * 0.22f, 0.15f, -0.46f};
            addBox(snapshot.meshes, body + rotateY(local, enemy.yaw), {0.14f, 0.14f, 0.14f},
                   {0.05f, 0.05f, 0.06f, 1.0f}, {0.0f, enemy.yaw, 0.0f});
        }

        // Floating health bar.
        const core::f32 width = 1.2f;
        addBox(snapshot.meshes, {enemy.position.x, 1.45f, enemy.position.z},
               {width, 0.08f, 0.08f}, {0.10f, 0.05f, 0.05f, 1.0f});
        const core::f32 fill = width * fraction;
        addBox(snapshot.meshes,
               {enemy.position.x - (width - fill) * 0.5f, 1.45f, enemy.position.z},
               {fill, 0.10f, 0.10f}, {0.30f, 0.85f, 0.35f, 1.0f});
    }
}

void RpgGame::appendNpcs(scene::SceneSnapshot& snapshot) const {
    for (core::usize i = 0; i < npcs_.size(); ++i) {
        const NpcState& npc = npcs_[i];
        const core::f32 bob = std::sin(time_ * 2.0f + static_cast<core::f32>(i)) * 0.03f;

        core::f32 yaw = npc.yaw;
        const core::Vec3 toPlayer = player_.position - npc.position;
        if (toPlayer.lengthSquared() < 64.0f) {
            yaw = std::atan2(-toPlayer.x, -toPlayer.z);
        }

        const core::Vec3 base{npc.position.x, 0.0f, npc.position.z};
        addBox(snapshot.meshes, {base.x, 0.62f + bob, base.z}, {0.8f, 1.25f, 0.55f}, npc.robe,
               {0.0f, yaw, 0.0f});
        addBox(snapshot.meshes, {base.x, 1.5f + bob, base.z}, {0.4f, 0.4f, 0.4f},
               {0.88f, 0.72f, 0.56f, 1.0f}, {0.0f, yaw, 0.0f});
        addBox(snapshot.meshes, {base.x, 1.75f + bob, base.z}, {0.56f, 0.14f, 0.56f}, npc.hat,
               {0.0f, yaw, 0.0f});
    }
}

scene::SceneSnapshot RpgGame::buildSnapshot() const {
    scene::SceneSnapshot snapshot;
    snapshot.clearColor = {0.45f, 0.62f, 0.86f, 1.0f};
    snapshot.lightDirection = {-0.45f, -1.0f, -0.30f};
    snapshot.ambient = 0.30f;

    const core::Vec3 target = player_.position + core::Vec3{0.0f, 1.5f, 0.0f};
    const core::Vec3 offset{std::sin(cameraYaw_) * std::cos(cameraPitch_),
                            std::sin(cameraPitch_),
                            std::cos(cameraYaw_) * std::cos(cameraPitch_)};
    snapshot.camera.position = target + offset * cameraDistance_;
    snapshot.camera.target = target;
    snapshot.camera.up = {0.0f, 1.0f, 0.0f};
    snapshot.camera.fovYRadians = 1.0471975512f;
    snapshot.camera.nearZ = 0.1f;
    snapshot.camera.farZ = 250.0f;

    snapshot.meshes.reserve(staticMeshes_.size() + 64);
    snapshot.meshes.insert(snapshot.meshes.end(), staticMeshes_.begin(), staticMeshes_.end());
    appendPlayer(snapshot);
    appendEnemies(snapshot);
    appendNpcs(snapshot);

    return snapshot;
}

} // namespace eee::logic
