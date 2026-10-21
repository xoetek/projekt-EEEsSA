#pragma once

#include "eee/core/Input.hpp"
#include "eee/core/NetSnapshot.hpp"
#include "eee/core/PlayerInput.hpp"
#include "eee/core/Sound.hpp"
#include "eee/core/Types.hpp"
#include "eee/logic/Classes.hpp"
#include "eee/logic/ISimulation.hpp"
#include "eee/logic/Items.hpp"
#include "eee/logic/Zones.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace eee::logic {

enum class UiMode : core::u8 {
    ClassSelect,
    Playing,
    Inventory,
    Paused,
    Dead,
    Victory
};

struct Loadout {
    std::vector<Item> inventory;
    std::array<Item, static_cast<core::usize>(ItemSlot::Count)> equipment{};
    ItemTotals totals{};
    core::i32 level = 1;
    core::i32 xp = 0;
    core::i32 xpToNext = 71;
    core::i32 gold = 0;
    core::i32 kills = 0;
    bool secondaryUnlocked = false;
};

struct CameraState {
    core::f32 yaw = 0.0f;
    core::f32 pitch = 0.36f;
    core::f32 distance = 6.2f;
    core::f32 smoothedDistance = 6.2f;
    core::Vec3 target{0.0f, 1.55f, 0.0f};
};

struct PlayerState {
    core::Vec3 position{0.0f, 0.0f, 4.0f};
    core::f32 yaw = 0.0f;
    ClassId classId = ClassId::Warrior;

    core::f32 health = 100.0f;
    core::f32 maxHealth = 100.0f;
    core::f32 mana = 55.0f;
    core::f32 maxMana = 55.0f;

    Loadout loadout{};
    CameraState camera{};
    core::Vec3 respawnPoint{0.0f, 0.0f, 4.0f};
    bool playerDead = false;

    core::f32 damage = 15.0f;
    core::f32 armour = 0.0f;
    core::f32 moveSpeed = 5.0f;
    core::f32 critChance = 5.0f;
    core::f32 lifeRegen = 0.8f;
    core::f32 manaRegen = 3.0f;

    core::f32 attackCooldown = 0.0f;
    core::f32 attackTimer = 0.0f;
    core::f32 qCooldown = 0.0f;
    core::f32 eCooldown = 0.0f;
    core::f32 walkTime = 0.0f;

    core::f32 jumpHeight = 0.0f;
    core::f32 verticalVelocity = 0.0f;
    bool grounded = true;
};

struct ItemComparison {
    core::f32 damage = 0.0f;
    core::f32 armour = 0.0f;
    core::f32 life = 0.0f;
    core::f32 mana = 0.0f;
    core::f32 moveSpeed = 0.0f;
    core::f32 critChance = 0.0f;
    bool equippable = false;
};

struct EnemyState {
    std::string name;
    core::Vec3 position{};
    core::Vec3 home{};
    core::f32 yaw = 0.0f;
    core::f32 health = 1.0f;
    core::f32 maxHealth = 1.0f;
    core::f32 damage = 1.0f;
    core::f32 speed = 1.0f;
    core::f32 scale = 1.0f;
    core::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    core::i32 level = 1;
    bool ranged = false;
    bool boss = false;
    bool alive = true;
    core::f32 attackCooldown = 0.0f;
    core::f32 fireCooldown = 0.0f;
    core::f32 slowTimer = 0.0f;
    core::f32 respawnTimer = 0.0f;
    core::f32 bob = 0.0f;
    core::u8 bossPattern = 0;
    core::f32 specialCooldown = 2.5f;
    core::f32 telegraphTimer = 0.0f;
    core::f32 chargeTimer = 0.0f;
    core::Vec3 chargeDirection{};
    bool charging = false;
    core::u8 pendingPattern = 0;
};

struct NpcState {
    core::Vec3 position{};
    core::f32 yaw = 0.0f;
    core::Vec4 robe{};
    core::Vec4 hat{};
    std::string name;
    std::string line;
};

struct LootDrop {
    Item item;
    core::Vec3 position{};
    core::f32 bob = 0.0f;
};

struct Projectile {
    core::PlayerId owner = core::PlayerId::P0;
    core::Vec3 position{};
    core::Vec3 velocity{};
    core::f32 life = 0.0f;
    core::f32 damage = 0.0f;
    core::f32 radius = 0.2f;
    core::f32 aoeRadius = 0.0f;
    core::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    bool fromPlayer = true;
};

struct Effect {
    core::Vec3 position{};
    core::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    core::f32 size = 1.0f;
    core::f32 life = 0.0f;
    core::f32 maxLife = 1.0f;
    bool ring = false;
};

class RpgGame final : public ISimulation {
public:
    RpgGame();

    // Movement constants shared by the authoritative tick and client-side
    // prediction so both integrate jumps identically.
    static constexpr core::f32 kJumpVelocity = 9.0f;
    static constexpr core::f32 kGravity = 30.0f;

    // Client-side prediction (A5a): advances one local player using the same
    // movement, collision and jump rules as updatePlaying. It never advances
    // enemies, projectiles, loot, cooldowns, RNG or other players.
    void predictLocalMovement(core::PlayerId id, const core::PlayerInput& input, core::f32 dt);

    // Remote snapshot interpolation (A5b): blends the non-local player, enemies,
    // projectiles and loot between two authoritative snapshots. The local
    // player, zone, portal, stats, inventory, time and RNG are left untouched.
    void applyRemoteInterpolation(core::PlayerId localPlayer,
                                  const core::WorldSnapshot& previous,
                                  const core::WorldSnapshot& current, core::f32 alpha);

    // Starts a run immediately (host shortcut / tests).
    void begin(ClassId classId) { startRun(classId); }

    void update(core::f32 dt, const core::InputState& input) override;
    void updateFrame(core::f32 dt, const core::InputFrame& frame);
    [[nodiscard]] scene::SceneSnapshot buildSnapshot() const override;
    [[nodiscard]] scene::SceneSnapshot buildSnapshot(core::PlayerId viewer) const;

    [[nodiscard]] PlayerState& player() { return players_[0]; }
    [[nodiscard]] const PlayerState& player() const { return players_[0]; }
    [[nodiscard]] PlayerState& player(core::PlayerId id) { return players_[static_cast<core::usize>(id)]; }
    [[nodiscard]] const PlayerState& player(core::PlayerId id) const {
        return players_[static_cast<core::usize>(id)];
    }
    [[nodiscard]] core::u8 activePlayers() const { return activePlayers_; }
    [[nodiscard]] core::PlayerId localViewerId() const { return localViewer_; }
    void setLocalViewerId(core::PlayerId id) { localViewer_ = id; }
    void setActivePlayers(core::u8 count) { activePlayers_ = core::clamp(count, core::u8{1}, core::u8{2}); }
    void beginCoop(ClassId p0Class, ClassId p1Class);
    void applyLocalCameraInput(core::f32 dt, const core::InputState& input, core::PlayerId id);
    [[nodiscard]] const std::vector<EnemyState>& enemies() const { return enemies_; }
    [[nodiscard]] std::vector<EnemyState>& enemiesMutable() { return enemies_; }
    [[nodiscard]] const std::vector<NpcState>& npcs() const { return npcs_; }
    [[nodiscard]] const std::vector<Projectile>& projectiles() const { return projectiles_; }
    [[nodiscard]] const std::vector<LootDrop>& loot() const { return loot_; }
    [[nodiscard]] const std::vector<Item>& inventory() const {
        return players_[static_cast<core::usize>(localViewer_)].loadout.inventory;
    }
    [[nodiscard]] const std::vector<Item>& inventory(core::PlayerId id) const {
        return players_[static_cast<core::usize>(id)].loadout.inventory;
    }
    [[nodiscard]] const Item& equipped(ItemSlot slot) const;
    [[nodiscard]] const Item& equipped(core::PlayerId id, ItemSlot slot) const;
    [[nodiscard]] const ItemTotals& totals() const {
        return players_[static_cast<core::usize>(localViewer_)].loadout.totals;
    }
    [[nodiscard]] const ItemTotals& totals(core::PlayerId id) const {
        return players_[static_cast<core::usize>(id)].loadout.totals;
    }
    [[nodiscard]] ItemComparison compareItem(const Item& item) const;
    [[nodiscard]] core::f32 damageFor(const ItemTotals& totals) const;
    [[nodiscard]] core::f32 computeDamage(const ItemTotals& totals, core::PlayerId id) const;
    [[nodiscard]] core::i32 inventorySelection() const { return inventorySelection_; }
    void setInventorySelection(core::i32 index);
    [[nodiscard]] core::usize inventoryCapacity() const { return 24; }
    [[nodiscard]] bool secondaryUnlocked() const {
        return players_[static_cast<core::usize>(localViewer_)].loadout.secondaryUnlocked;
    }
    [[nodiscard]] const core::Vec3& portalPosition() const { return portalPosition_; }

    // Adds an item directly (loot pickup, trading, tests). Returns false if full.
    bool addItem(Item item);
    bool addItem(core::PlayerId id, Item item);

    [[nodiscard]] UiMode uiMode() const { return mode_; }
    [[nodiscard]] bool paused() const { return mode_ == UiMode::Paused; }
    [[nodiscard]] bool dead() const { return mode_ == UiMode::Dead; }
    [[nodiscard]] bool victory() const { return mode_ == UiMode::Victory; }
    [[nodiscard]] bool inventoryOpen() const { return mode_ == UiMode::Inventory; }
    [[nodiscard]] bool classSelect() const { return mode_ == UiMode::ClassSelect; }
    [[nodiscard]] bool menuActive() const {
        return mode_ == UiMode::Paused || mode_ == UiMode::Dead || mode_ == UiMode::Victory ||
               mode_ == UiMode::ClassSelect;
    }

    [[nodiscard]] core::i32 menuSelection() const { return menuSelection_; }
    [[nodiscard]] core::usize menuOptionCount() const;
    [[nodiscard]] std::string_view menuOption(core::usize index) const;
    [[nodiscard]] bool quitRequested() const { return quitRequested_; }

    [[nodiscard]] core::i32 zoneIndex() const { return zoneIndex_; }
    [[nodiscard]] const ZoneDef& zone() const;
    [[nodiscard]] bool portalActive() const { return portalActive_; }
    [[nodiscard]] const EnemyState* boss() const;

    [[nodiscard]] bool dialogueActive() const { return dialogueTimer_ > 0.0f; }
    [[nodiscard]] std::string_view dialogueName() const { return dialogueName_; }
    [[nodiscard]] std::string_view dialogueText() const { return dialogueText_; }

    [[nodiscard]] std::string_view bannerTitle() const { return bannerTitle_; }
    [[nodiscard]] std::string_view bannerText() const { return bannerText_; }
    [[nodiscard]] core::f32 bannerTimer() const { return bannerTimer_; }
    [[nodiscard]] std::string_view message() const { return message_; }
    [[nodiscard]] core::f32 messageTimer() const { return messageTimer_; }
    [[nodiscard]] std::string_view interactionHint() const { return interactionHint_; }

    [[nodiscard]] core::f32 cameraYaw() const { return players_[0].camera.yaw; }
    [[nodiscard]] core::f32 cameraPitch() const { return players_[0].camera.pitch; }
    [[nodiscard]] core::f32 cameraDistance() const { return players_[0].camera.distance; }

    [[nodiscard]] std::vector<core::SoundId> takeSoundEvents();
    [[nodiscard]] core::u64 checksum() const;
    [[nodiscard]] core::WorldSnapshot captureNetworkSnapshot(core::u64 tick) const;
    void applyNetworkSnapshot(const core::WorldSnapshot& snapshot);
    // Authoritative respawn (P0 also clears the global death menu).
    void respawnPlayer(core::PlayerId id);
    // Server-side inventory action: 0 = equip/use, 1 = drop.
    bool applyInventoryAction(core::PlayerId id, core::u8 action, core::u16 index);
    [[nodiscard]] bool takeGraphicsRequest() {
        const bool requested = graphicsRequested_;
        graphicsRequested_ = false;
        return requested;
    }

    static core::i32 xpToNextFor(core::i32 level);

private:
    struct Obstacle {
        core::Vec3 position;
        core::f32 radius = 0.5f;
    };

    void startRun(ClassId classId);
    void restartRun();
    void changeClass();
    void loadZone(core::i32 index);
    void buildStaticWorld();
    void spawnEnemies();
    void spawnNpcs();

    void updatePlaying(core::f32 dt, const core::InputFrame& frame);
    void updateInventory(const core::InputState& input);
    void updateMenu(const core::InputState& input);
    void updateEnemies(core::f32 dt);
    void executeBossPattern(EnemyState& boss, std::vector<EnemyState>& summons,
                            core::PlayerId target);
    void updateProjectiles(core::f32 dt);
    void updateEffects(core::f32 dt);

    void updateLocalCamera(core::f32 dt, const core::InputState& input);
    void updateCameraFollow(core::f32 dt);
    void movePlayer(core::f32 dt, bool sprint, core::Vec3 move);
    void resolveObstacles(core::Vec3& position, core::f32 radius) const;
    void separateEnemies();

    void basicAttack();
    void useSkill(SkillId skill, bool secondary);
    void castArea(core::Vec3 center, core::f32 radius, core::f32 damageMultiplier);
    void fireProjectile(core::Vec3 from, core::Vec3 direction, core::f32 speed, core::f32 damage,
                        bool fromPlayer, core::f32 aoeRadius, core::Vec4 color,
                        core::PlayerId owner = core::PlayerId::P0);
    void hurtEnemy(EnemyState& enemy, core::f32 amount);
    void hurtPlayer(core::PlayerId id, core::f32 amount);
    void killEnemy(EnemyState& enemy);

    void handleInteract();
    void pickupLoot();
    void useInventoryItem();
    void dropSelectedItem();
    void restAtFire();
    void respawn();
    void equipItem(Item& item);
    void recomputeStats();
    void gainXp(core::i32 amount);
    void checkLevelUp();
    void addLoot(Item item, core::Vec3 position);
    void spawnEffect(core::Vec3 position, core::f32 size, core::Vec4 color, bool ring);
    void setMessage(std::string text);
    void emit(core::SoundId sound);

    PlayerState& active() { return players_[static_cast<core::usize>(activeId_)]; }
    [[nodiscard]] const PlayerState& active() const {
        return players_[static_cast<core::usize>(activeId_)];
    }

    [[nodiscard]] core::Vec3 aimPoint() const;
    [[nodiscard]] core::Vec3 forward() const;
    [[nodiscard]] core::Vec3 cameraForward() const;
    [[nodiscard]] core::f32 resolveCameraDistance(core::Vec3 target, core::Vec3 offset,
                                                  core::f32 desired) const;
    [[nodiscard]] const Item* selectedItem() const;
    [[nodiscard]] Item* selectedItem();

    static void addBox(std::vector<scene::MeshInstance>& out, core::Vec3 center, core::Vec3 size,
                       core::Vec4 color, core::Vec3 euler = {});
    static core::Vec3 rotateY(core::Vec3 value, core::f32 yaw);
    static core::f32 wrapAngle(core::f32 angle);
    [[nodiscard]] core::f32 randomFloat();

    void appendPlayer(scene::SceneSnapshot& snapshot, const PlayerState& player) const;
    void appendEnemies(scene::SceneSnapshot& snapshot) const;
    void appendNpcs(scene::SceneSnapshot& snapshot) const;
    void appendLoot(scene::SceneSnapshot& snapshot) const;
    void appendProjectiles(scene::SceneSnapshot& snapshot) const;
    void appendEffects(scene::SceneSnapshot& snapshot) const;
    void appendPortal(scene::SceneSnapshot& snapshot) const;
    void appendCampfire(scene::SceneSnapshot& snapshot) const;

    std::vector<scene::MeshInstance> staticMeshes_;
    std::vector<Obstacle> obstacles_;
    std::vector<EnemyState> enemies_;
    std::vector<NpcState> npcs_;
    std::vector<LootDrop> loot_;
    std::vector<Projectile> projectiles_;
    std::vector<Effect> effects_;
    std::array<PlayerState, 2> players_{};
    PlayerState& player_ = players_[0];
    core::PlayerId localViewer_ = core::PlayerId::P0;
    core::PlayerId activeId_ = core::PlayerId::P0;
    core::u8 activePlayers_ = 1;
    UiMode mode_ = UiMode::ClassSelect;

    core::i32 zoneIndex_ = 0;
    bool portalActive_ = false;
    bool bossIntroShown_ = false;

    core::Vec3 portalPosition_{0.0f, 0.0f, -22.0f};
    core::Vec3 bossSpawn_{0.0f, 0.0f, -16.0f};
    core::Vec3 firePosition_{0.0f, 0.0f, 0.0f};
    bool graphicsRequested_ = false;

    core::i32 menuSelection_ = 0;
    core::i32 inventorySelection_ = 0;
    bool quitRequested_ = false;

    core::f32 dialogueTimer_ = 0.0f;
    std::string dialogueName_;
    std::string dialogueText_;

    std::string bannerTitle_;
    std::string bannerText_;
    core::f32 bannerTimer_ = 0.0f;
    std::string message_;
    core::f32 messageTimer_ = 0.0f;
    std::string interactionHint_;

    std::vector<core::SoundId> sounds_;

    core::u32 randomState_ = 0xC0FFEEu;
    core::u32 itemId_ = 0;
    core::f32 time_ = 0.0f;
};

} // namespace eee::logic
