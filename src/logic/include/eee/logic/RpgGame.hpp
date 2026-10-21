#pragma once

#include "eee/logic/ISimulation.hpp"

#include <string_view>
#include <vector>

namespace eee::logic {

struct PlayerState {
    core::Vec3 position{0.0f, 0.0f, 2.0f};
    core::f32 yaw = 0.0f; // 0 = facing -Z
    core::f32 health = 100.0f;
    core::f32 maxHealth = 100.0f;
    core::i32 level = 1;
    core::i32 xp = 0;
    core::i32 xpToNext = 40;
    core::i32 gold = 0;
    core::i32 kills = 0;
    core::f32 attackCooldown = 0.0f;
    core::f32 attackTimer = 0.0f;
    core::f32 walkTime = 0.0f;
};

struct EnemyState {
    core::Vec3 position{};
    core::Vec3 home{};
    core::f32 yaw = 0.0f;
    core::f32 health = 40.0f;
    core::f32 maxHealth = 40.0f;
    core::f32 attackCooldown = 0.0f;
    core::f32 respawnTimer = 0.0f;
    core::f32 bob = 0.0f;
    bool alive = true;
};

struct NpcState {
    core::Vec3 position{};
    core::f32 yaw = 0.0f;
    core::Vec4 robe{};
    core::Vec4 hat{};
    std::string_view name;
    std::string_view line;
};

// Third-person RPG demo: explore a low-poly village, fight slimes, talk to
// villagers, level up, and open a pause/death menu with Escape.
class RpgGame final : public ISimulation {
public:
    RpgGame();

    void update(core::f32 dt, const core::InputState& input) override;
    [[nodiscard]] scene::SceneSnapshot buildSnapshot() const override;

    [[nodiscard]] PlayerState& player() { return player_; }
    [[nodiscard]] const PlayerState& player() const { return player_; }
    [[nodiscard]] const std::vector<EnemyState>& enemies() const { return enemies_; }
    [[nodiscard]] const std::vector<NpcState>& npcs() const { return npcs_; }
    [[nodiscard]] core::u32 aliveEnemies() const;

    [[nodiscard]] bool paused() const { return paused_; }
    [[nodiscard]] bool dead() const { return dead_; }
    [[nodiscard]] bool menuActive() const { return paused_ || dead_; }
    [[nodiscard]] core::i32 menuSelection() const { return selection_; }
    [[nodiscard]] core::usize menuOptionCount() const;
    [[nodiscard]] std::string_view menuOption(core::usize index) const;
    [[nodiscard]] bool quitRequested() const { return quitRequested_; }

    [[nodiscard]] bool dialogueActive() const { return dialogueTimer_ > 0.0f; }
    [[nodiscard]] std::string_view dialogueName() const { return dialogueName_; }
    [[nodiscard]] std::string_view dialogueText() const { return dialogueText_; }

    [[nodiscard]] core::f32 cameraYaw() const { return cameraYaw_; }
    [[nodiscard]] core::f32 cameraPitch() const { return cameraPitch_; }
    [[nodiscard]] core::f32 cameraDistance() const { return cameraDistance_; }

private:
    void reset();
    void updatePlaying(core::f32 dt, const core::InputState& input);
    void updateMenu(const core::InputState& input);
    void updateEnemies(core::f32 dt);
    void tryAttack();
    void applyKillRewards();
    void checkLevelUp();
    void handleInteraction(const core::InputState& input);
    void buildStaticWorld();

    static void addBox(std::vector<scene::MeshInstance>& out, core::Vec3 center, core::Vec3 size,
                       core::Vec4 color, core::Vec3 euler = {});
    static core::Vec3 rotateY(core::Vec3 value, core::f32 yaw);

    void appendPlayer(scene::SceneSnapshot& snapshot) const;
    void appendEnemies(scene::SceneSnapshot& snapshot) const;
    void appendNpcs(scene::SceneSnapshot& snapshot) const;

    [[nodiscard]] core::f32 randomFloat();

    std::vector<scene::MeshInstance> staticMeshes_;
    std::vector<EnemyState> enemies_;
    std::vector<NpcState> npcs_;
    PlayerState player_{};

    core::u32 randomState_ = 0xC0FFEEu;
    core::f32 cameraYaw_ = 0.0f;
    core::f32 cameraPitch_ = 0.32f;
    core::f32 cameraDistance_ = 6.5f;

    core::Vec2 lastMouse_{};
    bool hasLastMouse_ = false;

    bool paused_ = false;
    bool dead_ = false;
    bool quitRequested_ = false;
    core::i32 selection_ = 0;

    core::f32 dialogueTimer_ = 0.0f;
    std::string_view dialogueName_{};
    std::string_view dialogueText_{};
    core::f32 time_ = 0.0f;
};

} // namespace eee::logic
