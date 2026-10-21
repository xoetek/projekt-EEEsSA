#include "TestHarness.hpp"

#include "eee/core/Input.hpp"
#include "eee/core/Math.hpp"
#include "eee/logic/RpgGame.hpp"
#include "eee/logic/World.hpp"

#include <cmath>

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

void testMath() {
    const Vec3 x{1.0f, 0.0f, 0.0f};
    const Vec3 y{0.0f, 1.0f, 0.0f};
    CHECK(eee::test::nearlyEqual(dot(x, y), 0.0f));
    const Vec3 z = cross(x, y);
    CHECK(eee::test::nearlyEqual(z.z, 1.0f));
    CHECK(eee::test::nearlyEqual(normalize(Vec3{3.0f, 4.0f, 0.0f}).length(), 1.0f));

    const Mat4 model = Mat4::trs3({1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f});
    const Vec3 point = transformPoint(model, {1.0f, 0.0f, 0.0f});
    CHECK(eee::test::nearlyEqual(point.x, 3.0f));
    CHECK(eee::test::nearlyEqual(point.y, 2.0f));
    CHECK(eee::test::nearlyEqual(point.z, 3.0f));

    const Mat4 view = Mat4::lookAt({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const Vec3 eye = transformPoint(view, {0.0f, 0.0f, 10.0f});
    CHECK(eee::test::nearlyEqual(eye.length(), 0.0f));
    const Vec3 target = transformPoint(view, {0.0f, 0.0f, 0.0f});
    CHECK(eee::test::nearlyEqual(target.z, -10.0f));

    const Mat4 projection = Mat4::perspective(1.0f, 16.0f / 9.0f, 1.0f, 100.0f);
    const Vec4 nearClip = multiply(projection, {0.0f, 0.0f, -1.0f, 1.0f});
    CHECK(eee::test::nearlyEqual(nearClip.z / nearClip.w, -1.0f));
    const Vec4 farClip = multiply(projection, {0.0f, 0.0f, -100.0f, 1.0f});
    CHECK(eee::test::nearlyEqual(farClip.z / farClip.w, 1.0f));

    const Mat4 ortho = Mat4::orthographic(0.0f, 800.0f, 0.0f, 600.0f, -1.0f, 1.0f);
    const Vec4 bottomLeft = multiply(ortho, {0.0f, 0.0f, 0.0f, 1.0f});
    CHECK(eee::test::nearlyEqual(bottomLeft.x, -1.0f));
    CHECK(eee::test::nearlyEqual(bottomLeft.y, -1.0f));
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
    CHECK(!input.isDown('W'));
    input.setKey(Key::Up, true);
    CHECK(input.isDown(Key::Up));
    CHECK(input.wasPressed(Key::Up));
    input.endFrame();
    CHECK(input.isDown(Key::Up));
    CHECK(!input.wasPressed(Key::Up));
    input.setKey(Key::Up, false);
    CHECK(input.wasReleased(Key::Up));
    input.endFrame();
    CHECK(!input.wasReleased(Key::Up));
}

void testSceneAndCamera() {
    RpgGame game;
    const scene::SceneSnapshot snapshot = game.buildSnapshot();

    CHECK(snapshot.meshes.size() >= 9700);
    CHECK(snapshot.meshes.size() <= 11000);
    CHECK(snapshot.overlay.empty());
    CHECK(snapshot.camera.fovYRadians > 0.5f);
    CHECK(snapshot.camera.farZ > 100.0f);

    const Vec3 targetOffset = snapshot.camera.target - game.player().position;
    CHECK(eee::test::nearlyEqual(targetOffset.y, 1.5f));
    const Vec3 eyeOffset = snapshot.camera.position - snapshot.camera.target;
    CHECK(eee::test::nearlyEqual(eyeOffset.length(), game.cameraDistance(), 1e-3f));

    // The third-person camera looks at the player: the player is in front of
    // the view (negative Z) and horizontally centred.
    const Mat4 view = Mat4::lookAt(snapshot.camera.position, snapshot.camera.target,
                                   snapshot.camera.up);
    const Vec3 playerInView = transformPoint(view, game.player().position);
    CHECK(eee::test::nearlyEqual(playerInView.x, 0.0f, 1e-4f));
    CHECK(playerInView.z <= -3.0f);

    for (eee::core::usize i = 0; i < snapshot.meshes.size(); i += 211) {
        for (float value : snapshot.meshes[i].transform.m) {
            CHECK(std::isfinite(value));
        }
        CHECK(snapshot.meshes[i].color.w > 0.0f);
    }
}

void testMovementAndBounds() {
    RpgGame game;
    const Vec3 start = game.player().position;
    InputState input;
    input.setKey(key('W'), true);
    for (int i = 0; i < 60; ++i) {
        game.update(1.0f / 60.0f, input);
    }
    CHECK((game.player().position - start).length() > 1.0f);
    CHECK(game.player().position.z < start.z);
    CHECK(game.player().yaw > -0.2f && game.player().yaw < 0.2f);

    // Sprint is faster than walking.
    RpgGame walker;
    RpgGame sprinter;
    InputState walkInput;
    walkInput.setKey(key('W'), true);
    InputState sprintInput;
    sprintInput.setKey(key('W'), true);
    sprintInput.setKey(Key::LeftShift, true);
    for (int i = 0; i < 60; ++i) {
        walker.update(1.0f / 60.0f, walkInput);
        sprinter.update(1.0f / 60.0f, sprintInput);
    }
    const Vec3 walkStart{0.0f, 0.0f, 2.0f};
    const Vec3 sprintStart{0.0f, 0.0f, 2.0f};
    CHECK((sprinter.player().position - sprintStart).length() >
          (walker.player().position - walkStart).length());
}

void testTimestepClamp() {
    RpgGame game;
    const Vec3 start = game.player().position;
    InputState input;
    input.setKey(key('W'), true);
    game.update(10.0f, input); // clamped to 0.1s -> ~0.5 units
    const float moved = (game.player().position - start).length();
    CHECK(moved > 0.3f);
    CHECK(moved < 0.8f);
}

void testCameraOrbitAndMouse() {
    RpgGame game;
    const core::f32 yawBefore = game.cameraYaw();
    InputState arrows;
    arrows.setKey(Key::Right, true);
    for (int i = 0; i < 30; ++i) {
        game.update(1.0f / 60.0f, arrows);
    }
    CHECK(std::fabs(game.cameraYaw() - yawBefore) > 0.2f);
    CHECK((game.player().position - Vec3{0.0f, 0.0f, 2.0f}).length() < 1e-4f);

    const core::f32 yawBeforeMouse = game.cameraYaw();
    InputState mouse;
    mouse.mouseDown[1] = true;
    mouse.mousePosition = {100.0f, 100.0f};
    game.update(1.0f / 60.0f, mouse);
    mouse.mousePosition = {160.0f, 100.0f};
    game.update(1.0f / 60.0f, mouse);
    CHECK(std::fabs(game.cameraYaw() - yawBeforeMouse) > 0.1f);
}

void testMenuToggleAndNavigation() {
    RpgGame game;

    InputState open;
    open.setKey(Key::Escape, true);
    game.update(1.0f / 60.0f, open);
    CHECK(game.paused());
    CHECK(game.menuActive());
    CHECK(game.menuOptionCount() == 3);
    CHECK(game.menuOption(0) == "RESUME");
    CHECK(game.menuOption(1) == "RESTART");
    CHECK(game.menuOption(2) == "QUIT");
    CHECK(game.menuSelection() == 0);

    // Movement is ignored while paused. (W is also menu navigation, so the
    // selection moves; that is fine, we reset it by reopening below.)
    const Vec3 pausedPosition = game.player().position;
    InputState move;
    move.setKey(key('W'), true);
    game.update(0.1f, move);
    CHECK((game.player().position - pausedPosition).length() < 1e-5f);

    // Resume, then reopen so the selection starts at RESUME again.
    InputState resume;
    resume.setKey(Key::Escape, true);
    game.update(0.1f, resume);
    CHECK(!game.paused());
    InputState reopenMenu;
    reopenMenu.setKey(Key::Escape, true);
    game.update(0.1f, reopenMenu);
    CHECK(game.paused());
    CHECK(game.menuSelection() == 0);

    InputState down;
    down.setKey(Key::Down, true);
    game.update(0.1f, down);
    CHECK(game.menuSelection() == 1);

    InputState restart;
    restart.setKey(Key::Enter, true);
    game.update(0.1f, restart);
    CHECK(!game.paused());
    CHECK(!game.menuActive());
    CHECK((game.player().position - Vec3{0.0f, 0.0f, 2.0f}).length() < 1e-4f);
    CHECK(game.player().level == 1);
    CHECK(game.menuSelection() == 0);

    InputState reopen;
    reopen.setKey(Key::Escape, true);
    game.update(0.1f, reopen);
    CHECK(game.paused());
    InputState close;
    close.setKey(Key::Escape, true);
    game.update(0.1f, close);
    CHECK(!game.paused());
}

void testQuitOption() {
    RpgGame game;
    CHECK(!game.quitRequested());

    InputState open;
    open.setKey(Key::Escape, true);
    game.update(0.1f, open);

    InputState down;
    down.setKey(Key::Down, true);
    game.update(0.1f, down);
    game.update(0.1f, down);

    InputState confirm;
    confirm.setKey(Key::Enter, true);
    game.update(0.1f, confirm);
    CHECK(game.quitRequested());
}

void placePlayerNextTo(RpgGame& game, const EnemyState& enemy) {
    game.player().position = {enemy.position.x, 0.0f, enemy.position.z + 1.5f};
    game.player().yaw = 0.0f;
}

void testCombatAndRewards() {
    RpgGame game;
    const EnemyState& enemy = game.enemies()[0];
    placePlayerNextTo(game, enemy);
    const core::f32 startingHealth = enemy.health;

    for (int strike = 0; strike < 4; ++strike) {
        game.player().attackCooldown = 0.0f;
        InputState attack;
        attack.setKey(Key::Space, true);
        game.update(0.016f, attack);
    }

    CHECK(game.enemies()[0].health < startingHealth);
    CHECK(!game.enemies()[0].alive);
    CHECK(game.player().kills == 1);
    CHECK(game.player().gold == 8);
    CHECK(game.player().xp == 25);
    CHECK(game.enemies()[0].respawnTimer > 0.0f);

    // Killing a second slime should level the player up.
    const EnemyState& second = game.enemies()[1];
    placePlayerNextTo(game, second);
    for (int strike = 0; strike < 4; ++strike) {
        game.player().attackCooldown = 0.0f;
        InputState attack;
        attack.setKey(Key::Space, true);
        game.update(0.016f, attack);
    }

    CHECK(game.player().kills == 2);
    CHECK(game.player().level == 2);
    CHECK(game.player().maxHealth > 100.0f);
    CHECK(game.player().xp == 10); // 50 XP - 40 required for level 2
    CHECK(game.player().health == game.player().maxHealth);
}

void testEnemyDamageAndDeathMenu() {
    RpgGame game;
    const EnemyState& enemy = game.enemies()[0];
    game.player().position = {enemy.position.x, 0.0f, enemy.position.z + 1.0f};
    const core::f32 startingHealth = game.player().health;

    for (int i = 0; i < 20; ++i) {
        game.update(0.1f, InputState{});
    }
    CHECK(game.player().health < startingHealth);

    game.player().health = 5.0f;
    for (int i = 0; i < 10 && !game.dead(); ++i) {
        game.update(0.1f, InputState{});
    }
    CHECK(game.dead());
    CHECK(game.menuActive());
    CHECK(game.menuOptionCount() == 2);
    CHECK(game.menuOption(0) == "RESTART");
    CHECK(game.menuOption(1) == "QUIT");

    InputState restart;
    restart.setKey(Key::Enter, true);
    game.update(0.1f, restart);
    CHECK(!game.dead());
    CHECK(!game.menuActive());
    CHECK(game.player().health > 0.0f);
}

void testDialogue() {
    RpgGame game;
    const NpcState& npc = game.npcs()[0];
    game.player().position = {npc.position.x, 0.0f, npc.position.z + 1.0f};

    InputState talk;
    talk.setKey(key('E'), true);
    game.update(1.0f / 60.0f, talk);
    CHECK(game.dialogueActive());
    CHECK(game.dialogueName() == "ELDER");
    CHECK(!game.dialogueText().empty());

    for (int i = 0; i < 300; ++i) {
        game.update(1.0f / 60.0f, InputState{});
    }
    CHECK(!game.dialogueActive());
}

void testDeterminism() {
    RpgGame first;
    RpgGame second;
    InputState input;
    input.setKey(key('W'), true);
    input.setKey(key('D'), true);

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

    const scene::SceneSnapshot a = first.buildSnapshot();
    const scene::SceneSnapshot b = second.buildSnapshot();
    CHECK(a.camera.position.x == b.camera.position.x);
    CHECK(a.camera.position.z == b.camera.position.z);
    CHECK(a.meshes.size() == b.meshes.size());
}

} // namespace

int main() {
    testMath();
    testWorld();
    testInputEdges();
    testSceneAndCamera();
    testMovementAndBounds();
    testTimestepClamp();
    testCameraOrbitAndMouse();
    testMenuToggleAndNavigation();
    testQuitOption();
    testCombatAndRewards();
    testEnemyDamageAndDeathMenu();
    testDialogue();
    testDeterminism();
    return eee::test::report("logic");
}
