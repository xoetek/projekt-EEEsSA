#include "TestHarness.hpp"

#include "eee/core/Input.hpp"
#include "eee/core/Math.hpp"
#include "eee/logic/Classes.hpp"
#include "eee/logic/Items.hpp"
#include "eee/logic/RpgGame.hpp"
#include "eee/logic/World.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using namespace eee::core;
using namespace eee::logic;

namespace core = eee::core;
namespace scene = eee::scene;

Key key(char code) { return static_cast<Key>(code); }

Vec4 multiply(const Mat4& m, const Vec4& v) {
    return {m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w,
            m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w,
            m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w,
            m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w};
}

void pressKey(RpgGame& game, Key code, float dt = 1.0f / 60.0f) {
    InputState input;
    input.setKey(code, true);
    game.update(dt, input);
}

void beginRun(RpgGame& game, ClassId classId = ClassId::Warrior) {
    const int index = static_cast<int>(classId);
    for (int i = 0; i < index; ++i) {
        pressKey(game, Key::Down);
    }
    pressKey(game, Key::Enter);
}

bool hasSound(const std::vector<SoundId>& sounds, SoundId id) {
    return std::find(sounds.begin(), sounds.end(), id) != sounds.end();
}

EnemyState* findBoss(RpgGame& game) {
    for (EnemyState& enemy : game.enemiesMutable()) {
        if (enemy.boss) {
            return &enemy;
        }
    }
    return nullptr;
}

void killBossAndEnterPortal(RpgGame& game) {
    EnemyState* boss = findBoss(game);
    CHECK(boss != nullptr);
    if (boss == nullptr) {
        return;
    }
    boss->health = 1.0f;
    boss->alive = true;
    game.player().position = {boss->position.x, 0.0f, boss->position.z + 1.5f};
    game.player().yaw = 0.0f;
    game.player().attackCooldown = 0.0f;

    InputState attack;
    attack.mouseDown[0] = true;
    game.update(0.016f, attack);
    CHECK(game.portalActive());

    game.player().position = {game.portalPosition().x, 0.0f, game.portalPosition().z + 0.5f};
    InputState interact;
    interact.mousePressed[1] = true;
    game.update(0.016f, interact);
}

void testMath() {
    const Vec3 x{1.0f, 0.0f, 0.0f};
    const Vec3 y{0.0f, 1.0f, 0.0f};
    CHECK(eee::test::nearlyEqual(dot(x, y), 0.0f));
    CHECK(eee::test::nearlyEqual(cross(x, y).z, 1.0f));
    CHECK(eee::test::nearlyEqual(normalize(Vec3{3.0f, 4.0f, 0.0f}).length(), 1.0f));

    const Mat4 model = Mat4::trs3({1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f});
    const Vec3 point = transformPoint(model, {1.0f, 0.0f, 0.0f});
    CHECK(eee::test::nearlyEqual(point.x, 3.0f));
    CHECK(eee::test::nearlyEqual(point.z, 3.0f));

    const Mat4 view = Mat4::lookAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    CHECK(eee::test::nearlyEqual(transformPoint(view, {0.0f, 0.0f, 10.0f}).length(), 0.0f));
    CHECK(eee::test::nearlyEqual(transformPoint(view, {0.0f, 0.0f, 0.0f}).z, -10.0f));

    const Mat4 projection = Mat4::perspective(1.0f, 16.0f / 9.0f, 1.0f, 100.0f);
    const Vec4 nearClip = multiply(projection, {0.0f, 0.0f, -1.0f, 1.0f});
    CHECK(eee::test::nearlyEqual(nearClip.z / nearClip.w, -1.0f));
    const Vec4 farClip = multiply(projection, {0.0f, 0.0f, -100.0f, 1.0f});
    CHECK(eee::test::nearlyEqual(farClip.z / farClip.w, 1.0f));
}

void testWorld() {
    World world;
    const Entity a = world.create();
    const Entity b = world.create();
    CHECK(a != kInvalidEntity);
    CHECK(world.alive(a));
    CHECK(world.entityCount() == 2);
    world.destroy(a);
    CHECK(!world.alive(a));
    CHECK(world.transform(a) == nullptr);
    const Entity c = world.create();
    CHECK(c == a);
    world.destroy(b);
    world.destroy(b);
    CHECK(world.entityCount() == 1);
}

void testInputEdges() {
    InputState input;
    input.setKey(Key::Up, true);
    CHECK(input.wasPressed(Key::Up));
    input.endFrame();
    CHECK(input.isDown(Key::Up));
    CHECK(!input.wasPressed(Key::Up));
    input.setKey(Key::Up, false);
    CHECK(input.wasReleased(Key::Up));
}

void testPlayerArrayIsolation() {
    RpgGame game;
    beginRun(game);
    CHECK(game.activePlayers() == 1);
    CHECK(game.localViewerId() == core::PlayerId::P0);
    game.setLocalViewerId(core::PlayerId::P1);
    CHECK(game.localViewerId() == core::PlayerId::P1);
    game.setLocalViewerId(core::PlayerId::P0);

    game.player(core::PlayerId::P1).loadout.level = 5;
    game.player(core::PlayerId::P1).loadout.gold = 99;
    CHECK(game.player(core::PlayerId::P0).loadout.level == 1);
    CHECK(game.player(core::PlayerId::P0).loadout.gold == 0);

    Item ring;
    ring.name = "P1 RING";
    ring.slot = ItemSlot::Ring;
    ring.itemLevel = 1;
    CHECK(game.addItem(core::PlayerId::P1, ring));
    CHECK(game.inventory(core::PlayerId::P1).size() == 1);
    CHECK(game.inventory(core::PlayerId::P0).empty());
    CHECK(game.equipped(core::PlayerId::P1, ItemSlot::Weapon).empty());
}

void testAuthoritativeSnapshotApplication() {
    RpgGame source;
    RpgGame target;
    source.beginCoop(ClassId::Warrior, ClassId::Archer);
    target.beginCoop(ClassId::Warrior, ClassId::Archer);

    source.player(core::PlayerId::P0).position = {3.0f, 0.0f, -4.0f};
    source.player(core::PlayerId::P0).health = 77.0f;
    source.player(core::PlayerId::P1).position = {-2.0f, 0.0f, 6.0f};
    source.player(core::PlayerId::P1).loadout.gold = 42;
    source.enemiesMutable()[0].position = {8.0f, 0.0f, 9.0f};
    source.enemiesMutable()[0].health = 13.0f;

    Item sword;
    sword.name = "REPLICATED BLADE";
    sword.baseName = "IRON SWORD";
    sword.slot = ItemSlot::Weapon;
    sword.rarity = Rarity::Rare;
    sword.itemLevel = 1;
    sword.baseDamage = 33;
    sword.mods.push_back(Modifier{StatId::FlatDamage, 7.5f, "+7 DAMAGE"});
    CHECK(source.addItem(core::PlayerId::P0, sword));
    CHECK(source.applyInventoryAction(core::PlayerId::P0, 0, 0));
    CHECK(source.equipped(core::PlayerId::P0, ItemSlot::Weapon).name == "REPLICATED BLADE");

    Item ring;
    ring.name = "REPLICATED RING";
    ring.baseName = "COPPER RING";
    ring.slot = ItemSlot::Ring;
    ring.rarity = Rarity::Magic;
    ring.itemLevel = 1;
    ring.mods.push_back(Modifier{StatId::Life, 12.0f, "+12 LIFE"});
    CHECK(source.addItem(core::PlayerId::P0, ring));

    const core::WorldSnapshot snapshot = source.captureNetworkSnapshot(10);
    target.applyNetworkSnapshot(snapshot);

    CHECK(target.player(core::PlayerId::P0).position.x == 3.0f);
    CHECK(target.player(core::PlayerId::P0).position.z == -4.0f);
    CHECK(target.player(core::PlayerId::P0).health == 77.0f);
    CHECK(target.player(core::PlayerId::P1).position.x == -2.0f);
    CHECK(target.player(core::PlayerId::P1).loadout.gold == 42);
    CHECK(target.enemies()[0].position.x == 8.0f);
    CHECK(target.enemies()[0].health == 13.0f);
    CHECK(target.checksum() == source.checksum());

    CHECK(target.inventory(core::PlayerId::P0).size() == 1);
    const Item& targetRing = target.inventory(core::PlayerId::P0)[0];
    CHECK(targetRing.name == "REPLICATED RING");
    CHECK(targetRing.baseName == "COPPER RING");
    CHECK(targetRing.slot == ItemSlot::Ring);
    CHECK(targetRing.rarity == Rarity::Magic);
    CHECK(targetRing.mods.size() == 1);
    CHECK(targetRing.mods[0].stat == StatId::Life);
    CHECK(targetRing.mods[0].value == 12.0f);
    CHECK(targetRing.mods[0].label == "+12 LIFE");

    const Item& targetSword = target.equipped(core::PlayerId::P0, ItemSlot::Weapon);
    CHECK(!targetSword.empty());
    CHECK(targetSword.name == "REPLICATED BLADE");
    CHECK(targetSword.baseName == "IRON SWORD");
    CHECK(targetSword.slot == ItemSlot::Weapon);
    CHECK(targetSword.rarity == Rarity::Rare);
    CHECK(targetSword.itemLevel == 1);
    CHECK(targetSword.baseDamage == 33);
    CHECK(targetSword.mods.size() == 1);
    CHECK(targetSword.mods[0].stat == StatId::FlatDamage);
    CHECK(targetSword.mods[0].value == 7.5f);
    CHECK(targetSword.mods[0].label == "+7 DAMAGE");
    CHECK(target.equipped(core::PlayerId::P0, ItemSlot::Armour).empty());
}

void testServerInventoryActions() {
    RpgGame game;
    game.beginCoop(ClassId::Warrior, ClassId::Archer);
    Item sword;
    sword.name = "SERVER BLADE";
    sword.slot = ItemSlot::Weapon;
    sword.itemLevel = 1;
    sword.baseDamage = 50;
    CHECK(game.addItem(core::PlayerId::P0, sword));
    CHECK(game.applyInventoryAction(core::PlayerId::P0, 0, 0));
    CHECK(game.equipped(core::PlayerId::P0, ItemSlot::Weapon).name == "SERVER BLADE");

    Item ring;
    ring.name = "SERVER RING";
    ring.slot = ItemSlot::Ring;
    ring.itemLevel = 1;
    CHECK(game.addItem(core::PlayerId::P0, ring));
    const core::usize lootBefore = game.loot().size();
    CHECK(game.applyInventoryAction(core::PlayerId::P0, 1, 0));
    CHECK(game.loot().size() == lootBefore + 1);
}

void testLocalInventoryAccessors() {
    RpgGame game;
    game.beginCoop(ClassId::Warrior, ClassId::Archer);
    game.setLocalViewerId(core::PlayerId::P1);
    Item sword;
    sword.name = "LOCAL BLADE";
    sword.slot = ItemSlot::Weapon;
    sword.itemLevel = 1;
    sword.baseDamage = 25;
    CHECK(game.addItem(core::PlayerId::P1, sword));
    CHECK(game.inventory().size() == 1);
    CHECK(game.inventory(core::PlayerId::P0).empty());
    game.setInventorySelection(7);
    CHECK(game.inventorySelection() == 0);
    game.setInventorySelection(-3);
    CHECK(game.inventorySelection() == 0);
    CHECK(game.compareItem(sword).equippable);
    game.setLocalViewerId(core::PlayerId::P0);
    CHECK(game.inventory().empty());
}

void testTwoPlayerSimulation() {
    RpgGame game;
    beginRun(game);
    game.setActivePlayers(2);
    CHECK(game.activePlayers() == 2);
    game.player(core::PlayerId::P0).maxHealth = 1.0e6f;
    game.player(core::PlayerId::P0).health = 1.0e6f;
    game.player(core::PlayerId::P1).maxHealth = 1.0e6f;
    game.player(core::PlayerId::P1).health = 1.0e6f;

    const Vec3 p0Start = game.player(core::PlayerId::P0).position;
    const Vec3 p1Start = game.player(core::PlayerId::P1).position;

    core::InputFrame frame;
    frame.activePlayers = 2;
    frame.players[1].moveX = 1.0f; // camera yaw 0 -> +X
    for (int i = 0; i < 60; ++i) {
        game.updateFrame(1.0f / 60.0f, frame);
    }
    CHECK(game.player(core::PlayerId::P1).position.x > p1Start.x + 1.0f);
    CHECK((game.player(core::PlayerId::P0).position - p0Start).length() < 0.25f);

    const scene::SceneSnapshot p0View = game.buildSnapshot(core::PlayerId::P0);
    const scene::SceneSnapshot p1View = game.buildSnapshot(core::PlayerId::P1);
    const Vec3 p0TargetOffset =
        p0View.camera.target - game.player(core::PlayerId::P0).position;
    const Vec3 p1TargetOffset =
        p1View.camera.target - game.player(core::PlayerId::P1).position;
    CHECK(eee::test::nearlyEqual(p0TargetOffset.y, 1.55f, 0.2f));
    CHECK(eee::test::nearlyEqual(p1TargetOffset.y, 1.55f, 0.2f));
    CHECK((p0View.camera.position - p1View.camera.position).length() > 0.5f);
}

void testTwoPlayerCombatAndTargeting() {
    RpgGame game;
    beginRun(game);
    game.setActivePlayers(2);
    PlayerState& p0 = game.player(core::PlayerId::P0);
    PlayerState& p1 = game.player(core::PlayerId::P1);
    p0.position = {30.0f, 0.0f, 30.0f};
    p0.maxHealth = 1.0e6f;
    p0.health = 1.0e6f;
    p1.position = {0.0f, 0.0f, 0.0f};
    p1.yaw = 0.0f;
    p1.maxHealth = 100.0f;
    p1.health = 100.0f;

    eee::core::usize enemyIndex = 0;
    for (eee::core::usize i = 0; i < game.enemies().size(); ++i) {
        if (!game.enemies()[i].ranged && !game.enemies()[i].boss) {
            enemyIndex = i;
            break;
        }
    }
    EnemyState& enemy = game.enemiesMutable()[enemyIndex];
    enemy.ranged = false;
    enemy.position = {5.0f, 0.0f, 5.0f};
    enemy.home = enemy.position;
    enemy.health = 1000.0f;
    p1.position = {5.0f, 0.0f, 6.5f};
    const core::f32 p0Health = p0.health;

    core::InputFrame frame;
    frame.activePlayers = 2;
    frame.players[1].cameraYaw = 0.0f;
    frame.players[1].cameraPitch = 0.36f;
    frame.players[1].attack = true;
    for (int i = 0; i < 90; ++i) {
        game.updateFrame(1.0f / 60.0f, frame);
        p1.attackCooldown = 0.0f;
    }

    CHECK(game.enemies()[enemyIndex].health < 1000.0f); // P1 damaged the enemy
    CHECK(p1.health < 100.0f);                 // enemy targeted the nearest player (P1)
    CHECK(eee::test::nearlyEqual(p0.health, p0Health, 1.0f));

    // Killing blows credit the attacking player.
    game.enemiesMutable()[enemyIndex].health = 1.0f;
    for (int i = 0; i < 5; ++i) {
        p1.attackCooldown = 0.0f;
        game.updateFrame(1.0f / 60.0f, frame);
    }
    CHECK(!game.enemies()[enemyIndex].alive);
    CHECK(p1.loadout.kills == 1);
    CHECK(p0.loadout.kills == 0);
    CHECK(p1.loadout.xp > 0);
    CHECK(p0.loadout.xp == 0);
}

void testTwoPlayerDeterminism() {
    RpgGame first;
    RpgGame second;
    beginRun(first);
    beginRun(second);
    first.setActivePlayers(2);
    second.setActivePlayers(2);

    core::InputFrame frame;
    frame.activePlayers = 2;
    frame.players[0].moveZ = 0.5f;
    frame.players[1].moveX = 0.75f;
    frame.players[0].cameraYaw = 0.2f;
    frame.players[1].cameraYaw = -0.2f;
    for (int i = 0; i < 180; ++i) {
        first.updateFrame(1.0f / 120.0f, frame);
        second.updateFrame(1.0f / 120.0f, frame);
    }
    for (core::u8 p = 0; p < 2; ++p) {
        const PlayerState& a = first.player(static_cast<core::PlayerId>(p));
        const PlayerState& b = second.player(static_cast<core::PlayerId>(p));
        CHECK(a.position.x == b.position.x);
        CHECK(a.position.z == b.position.z);
        CHECK(a.camera.yaw == b.camera.yaw);
    }
    CHECK(first.enemies().size() == second.enemies().size());
    for (eee::core::usize i = 0; i < first.enemies().size(); ++i) {
        CHECK(first.enemies()[i].position.x == second.enemies()[i].position.x);
        CHECK(first.enemies()[i].health == second.enemies()[i].health);
    }
}

void testClassSelectionAndScene() {
    RpgGame game;
    CHECK(game.uiMode() == UiMode::ClassSelect);
    CHECK(game.menuOptionCount() == 3);
    CHECK(game.menuOption(0) == "WARRIOR");
    CHECK(game.menuOption(1) == "ARCHER");
    CHECK(game.menuOption(2) == "MAGE");
    CHECK(game.buildSnapshot().meshes.size() > 12000); // zone backdrop

    beginRun(game, ClassId::Archer);
    CHECK(game.uiMode() == UiMode::Playing);
    CHECK(game.player().classId == ClassId::Archer);
    CHECK(classDef(game.player().classId).ranged);
    CHECK(game.player().loadout.level == 1);
    CHECK(game.zoneIndex() == 0);
    CHECK(game.zone().minLevel == 1);

    const scene::SceneSnapshot snapshot = game.buildSnapshot();
    CHECK(snapshot.meshes.size() > 12000);
    CHECK(snapshot.meshes.size() < 20000);
    CHECK(snapshot.overlay.empty());
    CHECK(snapshot.camera.fovYRadians > 0.5f);

    const Vec3 targetOffset = snapshot.camera.target - game.player().position;
    CHECK(eee::test::nearlyEqual(targetOffset.y, 1.5f, 0.2f));
    const Vec3 eyeOffset = snapshot.camera.position - snapshot.camera.target;
    CHECK(eyeOffset.length() > 3.0f);
}

void testMovementJumpAndCamera() {
    RpgGame game;
    beginRun(game);

    const Vec3 start = game.player().position;
    InputState move;
    move.setKey(key('W'), true);
    for (int i = 0; i < 60; ++i) {
        game.update(1.0f / 60.0f, move);
    }
    CHECK((game.player().position - start).length() > 1.0f);
    CHECK(game.player().position.z < start.z);

    // Jump.
    CHECK(game.player().grounded);
    pressKey(game, Key::Space);
    CHECK(!game.player().grounded);
    CHECK(game.player().jumpHeight > 0.0f);
    for (int i = 0; i < 120; ++i) {
        game.update(1.0f / 60.0f, InputState{});
    }
    CHECK(game.player().grounded);
    CHECK(eee::test::nearlyEqual(game.player().jumpHeight, 0.0f));

    // Mouse-look: relative motion rotates the camera.
    const core::f32 yawBefore = game.cameraYaw();
    InputState look;
    look.mouseDelta = {400.0f, 0.0f};
    game.update(1.0f / 60.0f, look);
    CHECK(std::fabs(game.cameraYaw() - yawBefore) > 0.5f);
}

void testLocalMovementPrediction() {
    RpgGame game;
    game.beginCoop(ClassId::Warrior, ClassId::Archer);
    game.player(core::PlayerId::P1).position = {0.0f, 0.0f, 4.0f};
    game.player(core::PlayerId::P1).yaw = 0.0f;
    game.player(core::PlayerId::P1).grounded = true;
    const Vec3 p0Before = game.player(core::PlayerId::P0).position;
    const Vec3 enemyBefore = game.enemies()[0].position;
    const core::f32 healthBefore = game.player(core::PlayerId::P1).health;
    const core::f32 manaBefore = game.player(core::PlayerId::P1).mana;
    const core::usize enemyCount = game.enemies().size();
    const core::usize lootCount = game.loot().size();
    (void)game.takeSoundEvents();

    core::PlayerInput forward;
    forward.moveZ = 1.0f;
    forward.cameraYaw = 0.0f;
    forward.cameraPitch = 0.36f;
    game.predictLocalMovement(core::PlayerId::P1, forward, 1.0f / 60.0f);

    CHECK(game.player(core::PlayerId::P1).position.z < 4.0f);
    CHECK(std::fabs(game.player(core::PlayerId::P1).position.x) < 1e-4f);
    CHECK(game.player(core::PlayerId::P0).position.x == p0Before.x);
    CHECK(game.player(core::PlayerId::P0).position.z == p0Before.z);
    CHECK(game.enemies().size() == enemyCount);
    CHECK(game.loot().size() == lootCount);
    CHECK(game.enemies()[0].position.x == enemyBefore.x);
    CHECK(game.enemies()[0].position.z == enemyBefore.z);
    CHECK(game.player(core::PlayerId::P1).health == healthBefore);
    CHECK(game.player(core::PlayerId::P1).mana == manaBefore);
    CHECK(game.takeSoundEvents().empty());

    // Jump prediction mirrors the authoritative integrator.
    core::PlayerInput jump;
    jump.jump = true;
    jump.cameraYaw = 0.0f;
    jump.cameraPitch = 0.36f;
    game.predictLocalMovement(core::PlayerId::P1, jump, 1.0f / 60.0f);
    CHECK(!game.player(core::PlayerId::P1).grounded);
    CHECK(game.player(core::PlayerId::P1).jumpHeight > 0.0f);
    for (int i = 0; i < 120; ++i) {
        game.predictLocalMovement(core::PlayerId::P1, core::PlayerInput{}, 1.0f / 60.0f);
    }
    CHECK(game.player(core::PlayerId::P1).grounded);
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P1).jumpHeight, 0.0f));
}

void testCameraCollision() {
    RpgGame game;
    beginRun(game);
    game.player().maxHealth = 1.0e6f;
    game.player().health = 1.0e6f;
    game.player().armour = 1.0e6f;

    const Vec3 hut{13.0f * std::cos(0.785398f), 0.0f, 13.0f * std::sin(0.785398f)};
    game.player().position = {hut.x, 0.0f, hut.z - 4.0f};
    for (int i = 0; i < 90; ++i) {
        game.update(1.0f / 60.0f, InputState{});
    }

    const scene::SceneSnapshot snapshot = game.buildSnapshot();
    const Vec3 eye = snapshot.camera.position;
    const Vec3 eye2D{eye.x, 0.0f, eye.z};
    const Vec3 hut2D{hut.x, 0.0f, hut.z};
    CHECK((eye2D - hut2D).length() >= 2.2f);
    CHECK((eye - snapshot.camera.target).length() < game.cameraDistance());
    CHECK(eye.y >= 0.3f);
}

void testTimestepClampAndBounds() {
    RpgGame game;
    beginRun(game);
    const Vec3 start = game.player().position;
    InputState move;
    move.setKey(key('W'), true);
    game.update(10.0f, move); // clamped to 0.1s
    const float moved = (game.player().position - start).length();
    CHECK(moved > 0.3f);
    CHECK(moved < 0.9f);

    // Avoid dying to roaming enemies during the long bounds walk.
    game.player().maxHealth = 1.0e6f;
    game.player().health = 1.0e6f;
    InputState forward;
    forward.setKey(key('W'), true);
    for (int i = 0; i < 2000; ++i) {
        game.update(0.05f, forward);
    }
    CHECK(game.player().position.z >= -48.01f);
    CHECK(game.player().position.z <= 48.01f);
}

void testCollisionAgainstHut() {
    RpgGame game;
    beginRun(game);
    // The four huts sit at radius 13 on the diagonals; this one is NE.
    const Vec3 hut{13.0f * std::cos(0.785398f), 0.0f, 13.0f * std::sin(0.785398f)};
    game.player().position = {hut.x, 0.0f, hut.z + 3.2f};
    game.player().yaw = 0.0f;
    game.player().armour = 1.0e6f;

    InputState forward;
    forward.setKey(key('W'), true);
    for (int i = 0; i < 120; ++i) {
        game.update(1.0f / 60.0f, forward);
    }
    const Vec3 delta = game.player().position - hut;
    CHECK(std::sqrt(delta.x * delta.x + delta.z * delta.z) >= 2.5f);
}

void testMenuAndRestart() {
    RpgGame game;
    beginRun(game);
    const Vec3 start = game.player().position;

    pressKey(game, Key::Escape);
    CHECK(game.paused());
    CHECK(game.menuOptionCount() == 5);
    CHECK(game.menuOption(0) == "RESUME");
    CHECK(game.menuOption(1) == "GRAPHICS");
    CHECK(game.menuOption(2) == "RESTART");
    CHECK(game.menuOption(3) == "CHANGE CLASS");
    CHECK(game.menuOption(4) == "QUIT");

    InputState move;
    move.setKey(key('W'), true);
    const Vec3 paused = game.player().position;
    for (int i = 0; i < 30; ++i) {
        game.update(0.1f, move);
    }
    CHECK((game.player().position - paused).length() < 1e-5f);

    pressKey(game, Key::Escape); // resume
    CHECK(!game.paused());

    pressKey(game, Key::Escape); // pause again
    pressKey(game, Key::Down);   // GRAPHICS
    pressKey(game, Key::Enter);
    CHECK(game.takeGraphicsRequest());
    CHECK(game.paused());
    CHECK(!game.takeGraphicsRequest());
    pressKey(game, Key::Escape); // resume
    CHECK(!game.paused());

    pressKey(game, Key::Escape);
    pressKey(game, Key::Down);
    pressKey(game, Key::Down); // RESTART
    pressKey(game, Key::Enter);
    CHECK(!game.paused());
    CHECK(game.uiMode() == UiMode::Playing);
    CHECK((game.player().position - start).length() < 1e-4f);
    CHECK(game.player().loadout.level == 1);
    CHECK(game.inventory().empty());
}

void testChangeClassAndQuit() {
    RpgGame game;
    beginRun(game);
    pressKey(game, Key::Escape);
    for (int i = 0; i < 3; ++i) {
        pressKey(game, Key::Down); // CHANGE CLASS
    }
    pressKey(game, Key::Enter);
    CHECK(game.uiMode() == UiMode::ClassSelect);

    beginRun(game, ClassId::Mage);
    CHECK(game.player().classId == ClassId::Mage);
    pressKey(game, Key::Escape);
    for (int i = 0; i < 4; ++i) {
        pressKey(game, Key::Down);
    }
    pressKey(game, Key::Enter); // QUIT
    CHECK(game.quitRequested());
}

void testItemGeneration() {
    core::u32 rng = 0x1234567u;
    core::i32 weapons = 0;
    for (int i = 0; i < 240; ++i) {
        const Item item = generateItem(rng, 1 + i % 60, static_cast<ClassId>(i % 3));
        CHECK(!item.name.empty());
        CHECK(item.itemLevel >= 1);
        switch (item.rarity) {
        case Rarity::Normal: CHECK(item.mods.empty()); break;
        case Rarity::Magic: CHECK(item.mods.size() >= 1 && item.mods.size() <= 2); break;
        case Rarity::Rare: CHECK(item.mods.size() >= 3 && item.mods.size() <= 4); break;
        case Rarity::Unique: CHECK(item.mods.size() == 4); break;
        }
        for (const Modifier& mod : item.mods) {
            CHECK(!mod.label.empty());
        }
        if (item.slot == ItemSlot::Weapon) {
            CHECK(item.baseDamage > 0);
            ++weapons;
        }
    }
    CHECK(weapons > 20);

    const Item gem = generateSkillGem(rng, 10, ClassId::Warrior);
    CHECK(gem.slot == ItemSlot::SkillGem);
    CHECK(gem.gemClass == ClassId::Warrior);
    CHECK(gem.gemSkill == SkillId::LeapSlam);
    CHECK(gem.name == "LEAP SLAM");
}

void testItemTotals() {
    Item sword;
    sword.id = 1;
    sword.name = "TEST BLADE";
    sword.slot = ItemSlot::Weapon;
    sword.baseDamage = 40;
    sword.mods.push_back(Modifier{StatId::FlatDamage, 10.0f, "+10 DAMAGE"});
    sword.mods.push_back(Modifier{StatId::PercentDamage, 20.0f, "+20% DAMAGE"});
    sword.mods.push_back(Modifier{StatId::Life, 30.0f, "+30 LIFE"});

    ItemTotals totals;
    totals.add(sword);
    CHECK(totals.weaponDamage == 40);
    CHECK(eee::test::nearlyEqual(totals.flatDamage, 10.0f));
    CHECK(eee::test::nearlyEqual(totals.percentDamage, 20.0f));
    CHECK(eee::test::nearlyEqual(totals.life, 30.0f));
}

void testInventoryEquipAndFlask() {
    RpgGame game;
    beginRun(game);

    Item sword;
    sword.name = "TEST BLADE";
    sword.baseName = "IRON SWORD";
    sword.slot = ItemSlot::Weapon;
    sword.rarity = Rarity::Rare;
    sword.itemLevel = 1;
    sword.baseDamage = 60;
    sword.mods.push_back(Modifier{StatId::FlatDamage, 12.0f, "+12 DAMAGE"});
    CHECK(game.addItem(sword));

    const core::f32 damageBefore = game.player().damage;
    pressKey(game, key('I'));
    CHECK(game.inventoryOpen());
    pressKey(game, Key::Enter);
    CHECK(!game.equipped(ItemSlot::Weapon).empty());
    CHECK(game.equipped(ItemSlot::Weapon).name == "TEST BLADE");
    CHECK(game.player().damage > damageBefore);
    CHECK(game.inventory().empty());

    // Flasks heal.
    Item flask;
    flask.name = "CRIMSON FLASK";
    flask.baseName = "CRIMSON FLASK";
    flask.slot = ItemSlot::Flask;
    flask.itemLevel = 1;
    flask.flaskPower = 0.45f;
    CHECK(game.addItem(flask));
    game.player().health = 10.0f;
    pressKey(game, Key::Enter);
    CHECK(game.player().health > 10.0f);
    CHECK(game.inventory().empty());
    pressKey(game, key('I'));
    CHECK(!game.inventoryOpen());
}

void testItemComparisonAndDrop() {
    RpgGame game;
    beginRun(game);

    Item weak;
    weak.name = "WEAK BLADE";
    weak.slot = ItemSlot::Weapon;
    weak.itemLevel = 1;
    weak.baseDamage = 10;
    CHECK(game.addItem(weak));
    pressKey(game, key('I'));
    pressKey(game, Key::Enter); // equip
    pressKey(game, key('I'));

    Item strong;
    strong.name = "STRONG BLADE";
    strong.slot = ItemSlot::Weapon;
    strong.itemLevel = 1;
    strong.baseDamage = 120;
    strong.mods.push_back(Modifier{StatId::FlatDamage, 40.0f, "+40 DAMAGE"});
    const ItemComparison comparison = game.compareItem(strong);
    CHECK(comparison.equippable);
    CHECK(comparison.damage > 0.0f);

    CHECK(game.addItem(strong));
    pressKey(game, key('I'));
    pressKey(game, Key::Enter); // equip stronger weapon
    const core::f32 upgradedDamage = game.player().damage;
    pressKey(game, key('I')); // close inventory

    Item spare;
    spare.name = "SPARE RING";
    spare.slot = ItemSlot::Ring;
    spare.itemLevel = 1;
    CHECK(game.addItem(spare));
    pressKey(game, key('I'));
    const core::usize lootBefore = game.loot().size();
    const core::usize inventoryBefore = game.inventory().size();
    pressKey(game, key('X')); // drop the selected item
    CHECK(game.inventory().size() == inventoryBefore - 1);
    CHECK(game.loot().size() == lootBefore + 1);

    // A high-level weapon is not equippable yet.
    Item locked = strong;
    locked.name = "LOCKED BLADE";
    locked.itemLevel = 60;
    CHECK(!game.compareItem(locked).equippable);
    CHECK(game.player().damage == upgradedDamage);
    pressKey(game, key('I'));
}

void testItemLevelRequirement() {
    RpgGame game;
    beginRun(game);

    Item item;
    item.name = "HIGH LEVEL BLADE";
    item.slot = ItemSlot::Weapon;
    item.itemLevel = 50;
    item.baseDamage = 100;
    CHECK(game.addItem(item));
    pressKey(game, key('I'));
    pressKey(game, Key::Enter);
    CHECK(game.equipped(ItemSlot::Weapon).empty());
    CHECK(game.inventory().size() == 1);
    CHECK(game.message().find("REQUIRES") != std::string_view::npos);
}

void testSkillGemAndSkills() {
    RpgGame game;
    beginRun(game);
    const ClassDef& cls = classDef(game.player().classId);

    CHECK(!game.secondaryUnlocked());
    const core::f32 manaBefore = game.player().mana;
    pressKey(game, key('Q'));
    CHECK(game.player().qCooldown > 0.0f);
    CHECK(game.player().mana < manaBefore);
    CHECK(hasSound(game.takeSoundEvents(), SoundId::Skill));

    pressKey(game, key('E'));
    CHECK(!game.secondaryUnlocked());
    CHECK(hasSound(game.takeSoundEvents(), SoundId::Error));

    core::u32 gemRng = 0xABCDEFu;
    const Item gem = generateSkillGem(gemRng, game.player().loadout.level, game.player().classId);
    CHECK(game.addItem(gem));
    pressKey(game, key('I'));
    pressKey(game, Key::Enter);
    CHECK(game.secondaryUnlocked());
    CHECK(game.inventory().empty());
    pressKey(game, key('I'));

    const core::f32 manaBeforeE = game.player().mana;
    pressKey(game, key('E'));
    CHECK(game.player().eCooldown > 0.0f);
    CHECK(game.player().mana < manaBeforeE);
    (void)cls;
}

void testCombatAndLoot() {
    RpgGame game;
    beginRun(game);

    EnemyState& enemy = game.enemiesMutable()[0];
    enemy.position = {0.0f, 0.0f, -2.0f};
    enemy.home = enemy.position;
    game.player().position = {0.0f, 0.0f, 0.0f};
    game.player().yaw = 0.0f;
    const core::f32 healthBefore = enemy.health;

    bool died = false;
    for (int i = 0; i < 8 && !died; ++i) {
        game.player().attackCooldown = 0.0f;
        InputState attack;
        attack.mouseDown[0] = true;
        game.update(0.016f, attack);
        died = !game.enemiesMutable()[0].alive;
    }
    CHECK(game.enemiesMutable()[0].health < healthBefore);
    CHECK(died);
    CHECK(game.player().loadout.kills >= 1);
    CHECK(game.player().loadout.xp > 0);
    CHECK(game.player().loadout.gold > 0);
    CHECK(hasSound(game.takeSoundEvents(), SoundId::EnemyDeath));

    // Bosses always drop loot, so use one to test pickup deterministically.
    EnemyState* boss = findBoss(game);
    CHECK(boss != nullptr);
    if (boss == nullptr) {
        return;
    }
    boss->health = 1.0f;
    game.player().position = {boss->position.x, 0.0f, boss->position.z + 1.5f};
    game.player().yaw = 0.0f;
    game.player().attackCooldown = 0.0f;
    InputState bossAttack;
    bossAttack.mouseDown[0] = true;
    game.update(0.016f, bossAttack);
    CHECK(!game.loot().empty());

    // Pick up the nearest drop with right click.
    game.player().position = game.loot()[0].position;
    const core::usize inventoryBefore = game.inventory().size();
    InputState interact;
    interact.mousePressed[1] = true;
    game.update(1.0f / 60.0f, interact);
    CHECK(game.inventory().size() == inventoryBefore + 1);
    CHECK(hasSound(game.takeSoundEvents(), SoundId::Pickup));
}

void testZoneProgressionAndVictory() {
    RpgGame game;
    beginRun(game);

    for (int zone = 0; zone < 5; ++zone) {
        CHECK(game.zoneIndex() == zone);
        killBossAndEnterPortal(game);
        CHECK(game.zoneIndex() == zone + 1);
        CHECK(game.uiMode() == UiMode::Playing);
    }

    CHECK(game.zoneIndex() == 5);
    killBossAndEnterPortal(game);
    CHECK(game.uiMode() == UiMode::Victory);
    CHECK(game.victory());
    CHECK(game.menuOptionCount() == 3);
    CHECK(game.menuOption(0) == "NEW JOURNEY");
}

void testBossPattern() {
    RpgGame game;
    beginRun(game);
    EnemyState* boss = findBoss(game);
    CHECK(boss != nullptr);
    if (boss == nullptr) {
        return;
    }
    game.player().maxHealth = 1.0e6f;
    game.player().health = 1.0e6f;
    game.player().position = {boss->position.x, 0.0f, boss->position.z + 5.0f};
    boss->specialCooldown = 0.0f;
    const core::usize before = game.enemies().size();
    game.update(0.05f, InputState{});
    CHECK(boss->telegraphTimer > 0.0f);
    for (int i = 0; i < 10; ++i) {
        game.update(0.1f, InputState{});
    }
    // Verdant boss pattern summons two adds.
    CHECK(game.enemies().size() >= before + 2);
}

void testBossIntroAndBanner() {
    RpgGame game;
    beginRun(game);
    EnemyState* boss = findBoss(game);
    CHECK(boss != nullptr);
    game.player().position = boss->position + Vec3{0.0f, 0.0f, 6.0f};
    game.update(1.0f / 60.0f, InputState{});
    CHECK(game.bannerTimer() > 0.0f);
    CHECK(game.bannerTitle() == game.zone().bossName);
}

void testDeathAndDialogue() {
    RpgGame game;
    beginRun(game);

    const NpcState& npc = game.npcs()[0];
    game.player().position = {npc.position.x, 0.0f, npc.position.z + 1.0f};
    InputState talk;
    talk.mousePressed[1] = true;
    game.update(1.0f / 60.0f, talk);
    CHECK(game.dialogueActive());
    CHECK(game.dialogueName() == game.zone().npcName);

    // Rest at the fire restores resources and sets a checkpoint.
    game.player().position = {0.0f, 0.0f, 1.0f};
    game.player().health = 5.0f;
    game.player().mana = 0.0f;
    InputState rest;
    rest.mousePressed[1] = true;
    game.update(1.0f / 60.0f, rest);
    CHECK(game.player().health == game.player().maxHealth);
    CHECK(game.player().mana == game.player().maxMana);
    CHECK(game.message().find("CHECKPOINT") != std::string_view::npos);

    game.player().health = -1.0f;
    game.update(1.0f / 60.0f, InputState{});
    CHECK(game.dead());
    CHECK(game.menuOptionCount() == 3);
    CHECK(game.menuOption(0) == "RESPAWN AT FIRE");
    CHECK(game.menuOption(1) == "CHANGE CLASS");
    CHECK(game.menuOption(2) == "QUIT");

    pressKey(game, Key::Enter);
    CHECK(!game.dead());
    CHECK(game.player().health > 0.0f);
    CHECK((game.player().position - Vec3{0.0f, 0.0f, 1.0f}).length() < 1e-4f);
}

void testLevelCap() {
    RpgGame game;
    beginRun(game);
    game.player().loadout.level = 79;
    game.player().loadout.xpToNext = RpgGame::xpToNextFor(79);
    game.player().loadout.xp = game.player().loadout.xpToNext - 1;

    EnemyState* boss = findBoss(game);
    CHECK(boss != nullptr);
    boss->health = 1.0f;
    game.player().position = {boss->position.x, 0.0f, boss->position.z + 1.5f};
    game.player().yaw = 0.0f;
    game.player().attackCooldown = 0.0f;

    InputState attack;
    attack.mouseDown[0] = true;
    game.update(0.016f, attack);
    CHECK(game.player().loadout.level == 80);
    CHECK(game.player().loadout.xp == 0);
    CHECK(RpgGame::xpToNextFor(80) > RpgGame::xpToNextFor(79));
}

void testDeterminism() {
    RpgGame first;
    RpgGame second;
    beginRun(first);
    beginRun(second);

    InputState input;
    input.setKey(key('W'), true);
    input.setKey(key('D'), true);
    input.viewportSize = {1000.0f, 600.0f};
    input.mousePosition = {500.0f, 300.0f};
    for (int i = 0; i < 240; ++i) {
        first.update(1.0f / 120.0f, input);
        second.update(1.0f / 120.0f, input);
    }

    CHECK(first.player().position.x == second.player().position.x);
    CHECK(first.player().position.z == second.player().position.z);
    CHECK(first.player().yaw == second.player().yaw);
    CHECK(first.enemies().size() == second.enemies().size());
    for (eee::core::usize i = 0; i < first.enemies().size(); ++i) {
        CHECK(first.enemies()[i].position.x == second.enemies()[i].position.x);
        CHECK(first.enemies()[i].health == second.enemies()[i].health);
    }
}

} // namespace

int main() {
    testMath();
    testWorld();
    testInputEdges();
    testPlayerArrayIsolation();
    testAuthoritativeSnapshotApplication();
    testServerInventoryActions();
    testLocalInventoryAccessors();
    testTwoPlayerSimulation();
    testTwoPlayerCombatAndTargeting();
    testTwoPlayerDeterminism();
    testClassSelectionAndScene();
    testMovementJumpAndCamera();
    testLocalMovementPrediction();
    testCameraCollision();
    testTimestepClampAndBounds();
    testCollisionAgainstHut();
    testMenuAndRestart();
    testChangeClassAndQuit();
    testItemGeneration();
    testItemTotals();
    testInventoryEquipAndFlask();
    testItemComparisonAndDrop();
    testItemLevelRequirement();
    testSkillGemAndSkills();
    testCombatAndLoot();
    testZoneProgressionAndVictory();
    testBossPattern();
    testBossIntroAndBanner();
    testDeathAndDialogue();
    testLevelCap();
    testDeterminism();
    return eee::test::report("logic");
}
