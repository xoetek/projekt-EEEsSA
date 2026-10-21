#include "eee/net/Protocol.hpp"

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace eee::net {
namespace {

class Writer {
public:
    void u8(core::u8 value) { data_.push_back(value); }
    void u16(core::u16 value) {
        data_.push_back(static_cast<core::u8>(value & 0xFF));
        data_.push_back(static_cast<core::u8>((value >> 8) & 0xFF));
    }
    void u32(core::u32 value) {
        for (int i = 0; i < 4; ++i) {
            data_.push_back(static_cast<core::u8>((value >> (8 * i)) & 0xFF));
        }
    }
    void u64(core::u64 value) {
        for (int i = 0; i < 8; ++i) {
            data_.push_back(static_cast<core::u8>((value >> (8 * i)) & 0xFF));
        }
    }
    void i32(core::i32 value) { u32(static_cast<core::u32>(value)); }
    void f32(core::f32 value) {
        core::u32 bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }
    void string(const std::string& value) {
        const core::usize length =
            std::min(value.size(), static_cast<core::usize>(kMaxSnapshotString));
        u16(static_cast<core::u16>(length));
        data_.insert(data_.end(), value.begin(), value.begin() + static_cast<std::ptrdiff_t>(length));
    }
    void item(const core::ItemNetState& item) {
        string(item.name);
        string(item.baseName);
        u8(item.slot);
        u8(item.rarity);
        i32(item.itemLevel);
        i32(item.baseDamage);
        i32(item.baseArmour);
        u8(item.manaFlask ? 1 : 0);
        f32(item.flaskPower);
        u8(item.gemSkill);
        u8(item.gemClass);
        const core::usize modCount =
            std::min(item.mods.size(), static_cast<core::usize>(kMaxSnapshotMods));
        u16(static_cast<core::u16>(modCount));
        for (core::usize i = 0; i < modCount; ++i) {
            const core::ModifierNetState& mod = item.mods[i];
            u8(mod.stat);
            f32(mod.value);
            string(mod.label);
        }
    }
    void playerInput(const core::PlayerInput& input) {
        f32(input.moveX);
        f32(input.moveZ);
        f32(input.aimYaw);
        f32(input.cameraYaw);
        f32(input.cameraPitch);
        core::u16 flags = 0;
        flags |= input.jump ? 1u : 0u;
        flags |= input.attack ? 2u : 0u;
        flags |= input.interact ? 4u : 0u;
        flags |= input.skillQ ? 8u : 0u;
        flags |= input.skillE ? 16u : 0u;
        flags |= input.sprint ? 32u : 0u;
        u16(flags);
    }
    [[nodiscard]] std::vector<core::u8> take() { return std::move(data_); }

private:
    std::vector<core::u8> data_;
};

class Reader {
public:
    explicit Reader(std::span<const core::u8> data) : data_(data) {}
    [[nodiscard]] bool u8(core::u8& value) {
        if (remaining() < 1) return false;
        value = data_[offset_++];
        return true;
    }
    [[nodiscard]] bool u16(core::u16& value) {
        if (remaining() < 2) return false;
        value = static_cast<core::u16>(data_[offset_]) |
                static_cast<core::u16>(data_[offset_ + 1]) << 8;
        offset_ += 2;
        return true;
    }
    [[nodiscard]] bool u32(core::u32& value) {
        if (remaining() < 4) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) value |= static_cast<core::u32>(data_[offset_ + i]) << (8 * i);
        offset_ += 4;
        return true;
    }
    [[nodiscard]] bool u64(core::u64& value) {
        if (remaining() < 8) return false;
        value = 0;
        for (int i = 0; i < 8; ++i) value |= static_cast<core::u64>(data_[offset_ + i]) << (8 * i);
        offset_ += 8;
        return true;
    }
    [[nodiscard]] bool i32(core::i32& value) {
        core::u32 bits = 0;
        if (!u32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }
    [[nodiscard]] bool f32(core::f32& value) {
        core::u32 bits = 0;
        if (!u32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }
    [[nodiscard]] bool string(std::string& value) {
        core::u16 length = 0;
        if (!u16(length) || length > kMaxSnapshotString || remaining() < length) return false;
        value.assign(reinterpret_cast<const char*>(data_.data() + offset_), length);
        offset_ += length;
        return true;
    }
    [[nodiscard]] bool item(core::ItemNetState& item) {
        core::u8 manaFlask = 0;
        if (!string(item.name) || !string(item.baseName) || !u8(item.slot) ||
            !u8(item.rarity) || !i32(item.itemLevel) || !i32(item.baseDamage) ||
            !i32(item.baseArmour) || !u8(manaFlask) || !f32(item.flaskPower) ||
            !u8(item.gemSkill) || !u8(item.gemClass)) {
            return false;
        }
        item.manaFlask = manaFlask != 0;

        core::u16 modCount = 0;
        if (!u16(modCount) || modCount > kMaxSnapshotMods) return false;
        item.mods.resize(modCount);
        for (core::ModifierNetState& mod : item.mods) {
            if (!u8(mod.stat) || !f32(mod.value) || !string(mod.label)) return false;
        }
        return true;
    }
    [[nodiscard]] bool playerInput(core::PlayerInput& input) {
        if (!f32(input.moveX) || !f32(input.moveZ) || !f32(input.aimYaw) ||
            !f32(input.cameraYaw) || !f32(input.cameraPitch)) return false;
        core::u16 flags = 0;
        if (!u16(flags)) return false;
        input.jump = (flags & 1u) != 0;
        input.attack = (flags & 2u) != 0;
        input.interact = (flags & 4u) != 0;
        input.skillQ = (flags & 8u) != 0;
        input.skillE = (flags & 16u) != 0;
        input.sprint = (flags & 32u) != 0;
        return true;
    }
    [[nodiscard]] core::usize remaining() const { return data_.size() - offset_; }

private:
    std::span<const core::u8> data_;
    core::usize offset_ = 0;
};

} // namespace

std::vector<core::u8> encode(const Message& message) {
    Writer writer;
    writer.u16(static_cast<core::u16>(message.type));
    writer.u16(kProtocolVersion);

    std::visit(
        [&](const auto& payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, Hello>) {
                writer.u8(payload.classId);
                writer.u64(payload.nonce);
            } else if constexpr (std::is_same_v<T, Welcome>) {
                writer.u8(payload.playerId);
                writer.u32(payload.sessionId);
                writer.u16(payload.tickRate);
            } else if constexpr (std::is_same_v<T, LobbyState>) {
                writer.u8(payload.hostClass);
                writer.u8(payload.clientReady ? 1 : 0);
                writer.u64(payload.seed);
            } else if constexpr (std::is_same_v<T, ClassChoice>) {
                writer.u8(payload.classId);
            } else if constexpr (std::is_same_v<T, StartRun>) {
                writer.u64(payload.seed);
                writer.i32(payload.zoneIndex);
                writer.u8(payload.activePlayers);
                writer.u64(payload.startTick);
                writer.u8(payload.hostClass);
                writer.u8(payload.clientClass);
                writer.u8(payload.authoritative ? 1 : 0);
            } else if constexpr (std::is_same_v<T, InputCommand>) {
                writer.u8(payload.playerId);
                writer.u64(payload.tick);
                writer.playerInput(payload.input);
            } else if constexpr (std::is_same_v<T, InputAck>) {
                writer.u64(payload.tick);
            } else if constexpr (std::is_same_v<T, Checksum>) {
                writer.u64(payload.tick);
                writer.u64(payload.hash);
            } else if constexpr (std::is_same_v<T, Timestamp>) {
                writer.u64(payload.value);
            } else if constexpr (std::is_same_v<T, Disconnect>) {
                writer.u8(payload.reason);
            } else if constexpr (std::is_same_v<T, ServerTick>) {
                writer.u64(payload.tick);
            } else if constexpr (std::is_same_v<T, ActionRequest>) {
                writer.u8(payload.playerId);
                writer.u8(payload.action);
                writer.u16(payload.index);
            } else if constexpr (std::is_same_v<T, core::WorldSnapshot>) {
                writer.u64(payload.tick);
                writer.i32(payload.zoneIndex);
                writer.u8(payload.portalActive ? 1 : 0);
                for (const core::PlayerNetState& player : payload.players) {
                    writer.f32(player.position.x);
                    writer.f32(player.position.y);
                    writer.f32(player.position.z);
                    writer.f32(player.yaw);
                    writer.f32(player.health);
                    writer.f32(player.maxHealth);
                    writer.f32(player.mana);
                    writer.f32(player.maxMana);
                    writer.f32(player.jumpHeight);
                    writer.f32(player.attackCooldown);
                    writer.f32(player.qCooldown);
                    writer.f32(player.eCooldown);
                    writer.f32(player.damage);
                    writer.f32(player.armour);
                    writer.f32(player.moveSpeed);
                    writer.f32(player.critChance);
                    writer.u8(player.secondaryUnlocked ? 1 : 0);
                    writer.i32(player.level);
                    writer.i32(player.xp);
                    writer.i32(player.gold);
                    writer.i32(player.kills);
                    writer.u8(player.classId);
                    writer.u8(player.dead ? 1 : 0);
                    const core::usize inventoryCount =
                        std::min(player.inventory.size(),
                                 static_cast<core::usize>(kMaxSnapshotInventory));
                    writer.u16(static_cast<core::u16>(inventoryCount));
                    for (core::usize i = 0; i < inventoryCount; ++i) {
                        writer.item(player.inventory[i]);
                    }
                    for (const core::ItemNetState& item : player.equipment) {
                        writer.item(item);
                    }
                }
                writer.u16(static_cast<core::u16>(payload.enemies.size()));
                for (const core::EnemyNetState& enemy : payload.enemies) {
                    writer.f32(enemy.position.x);
                    writer.f32(enemy.position.y);
                    writer.f32(enemy.position.z);
                    writer.f32(enemy.yaw);
                    writer.f32(enemy.health);
                    writer.f32(enemy.maxHealth);
                    writer.f32(enemy.scale);
                    writer.f32(enemy.color.x);
                    writer.f32(enemy.color.y);
                    writer.f32(enemy.color.z);
                    writer.f32(enemy.color.w);
                    writer.u8(enemy.alive ? 1 : 0);
                    writer.u8(enemy.boss ? 1 : 0);
                    writer.u8(enemy.ranged ? 1 : 0);
                }
                writer.u16(static_cast<core::u16>(payload.projectiles.size()));
                for (const core::ProjectileNetState& projectile : payload.projectiles) {
                    writer.f32(projectile.position.x);
                    writer.f32(projectile.position.y);
                    writer.f32(projectile.position.z);
                    writer.f32(projectile.color.x);
                    writer.f32(projectile.color.y);
                    writer.f32(projectile.color.z);
                    writer.f32(projectile.color.w);
                    writer.f32(projectile.radius);
                    writer.u8(projectile.fromPlayer ? 1 : 0);
                }
                writer.u16(static_cast<core::u16>(payload.loot.size()));
                for (const core::LootNetState& loot : payload.loot) {
                    writer.f32(loot.position.x);
                    writer.f32(loot.position.y);
                    writer.f32(loot.position.z);
                    writer.u8(loot.rarity);
                }
            }
        },
        message.payload);

    return writer.take();
}

bool decode(std::span<const core::u8> bytes, Message& out) {
    Reader reader(bytes);
    core::u16 type = 0;
    core::u16 version = 0;
    if (!reader.u16(type) || !reader.u16(version) || version != kProtocolVersion) {
        return false;
    }

    Message message;
    message.type = static_cast<MessageType>(type);

    switch (message.type) {
    case MessageType::Hello: {
        Hello payload;
        if (!reader.u8(payload.classId) || !reader.u64(payload.nonce)) return false;
        message.payload = payload;
        break;
    }
    case MessageType::Welcome: {
        Welcome payload;
        if (!reader.u8(payload.playerId) || !reader.u32(payload.sessionId) ||
            !reader.u16(payload.tickRate)) return false;
        message.payload = payload;
        break;
    }
    case MessageType::LobbyState: {
        LobbyState payload;
        core::u8 ready = 0;
        if (!reader.u8(payload.hostClass) || !reader.u8(ready) || !reader.u64(payload.seed)) {
            return false;
        }
        payload.clientReady = ready != 0;
        message.payload = payload;
        break;
    }
    case MessageType::ClassChoice: {
        ClassChoice payload;
        if (!reader.u8(payload.classId)) return false;
        message.payload = payload;
        break;
    }
    case MessageType::StartRun: {
        StartRun payload;
        if (!reader.u64(payload.seed) || !reader.i32(payload.zoneIndex) ||
            !reader.u8(payload.activePlayers) || !reader.u64(payload.startTick) ||
            !reader.u8(payload.hostClass) || !reader.u8(payload.clientClass)) {
            return false;
        }
        core::u8 authoritative = 0;
        if (!reader.u8(authoritative)) return false;
        payload.authoritative = authoritative != 0;
        message.payload = payload;
        break;
    }
    case MessageType::InputCommand: {
        InputCommand payload;
        if (!reader.u8(payload.playerId) || !reader.u64(payload.tick) ||
            !reader.playerInput(payload.input)) {
            return false;
        }
        message.payload = payload;
        break;
    }
    case MessageType::InputAck: {
        InputAck payload;
        if (!reader.u64(payload.tick)) return false;
        message.payload = payload;
        break;
    }
    case MessageType::Checksum: {
        Checksum payload;
        if (!reader.u64(payload.tick) || !reader.u64(payload.hash)) return false;
        message.payload = payload;
        break;
    }
    case MessageType::Ping:
    case MessageType::Pong: {
        Timestamp payload;
        if (!reader.u64(payload.value)) return false;
        message.payload = payload;
        break;
    }
    case MessageType::Disconnect: {
        Disconnect payload;
        if (!reader.u8(payload.reason)) return false;
        message.payload = payload;
        break;
    }
    case MessageType::ServerTick: {
        ServerTick payload;
        if (!reader.u64(payload.tick)) return false;
        message.payload = payload;
        break;
    }
    case MessageType::ActionRequest: {
        ActionRequest payload;
        if (!reader.u8(payload.playerId) || !reader.u8(payload.action) ||
            !reader.u16(payload.index)) {
            return false;
        }
        message.payload = payload;
        break;
    }
    case MessageType::Snapshot: {
        core::WorldSnapshot snapshot;
        core::u8 portal = 0;
        if (!reader.u64(snapshot.tick) || !reader.i32(snapshot.zoneIndex) ||
            !reader.u8(portal)) {
            return false;
        }
        snapshot.portalActive = portal != 0;
        for (core::PlayerNetState& player : snapshot.players) {
            core::u8 secondary = 0;
            core::u8 dead = 0;
            if (!reader.f32(player.position.x) || !reader.f32(player.position.y) ||
                !reader.f32(player.position.z) || !reader.f32(player.yaw) ||
                !reader.f32(player.health) || !reader.f32(player.maxHealth) ||
                !reader.f32(player.mana) || !reader.f32(player.maxMana) ||
                !reader.f32(player.jumpHeight) || !reader.f32(player.attackCooldown) ||
                !reader.f32(player.qCooldown) || !reader.f32(player.eCooldown) ||
                !reader.f32(player.damage) || !reader.f32(player.armour) ||
                !reader.f32(player.moveSpeed) || !reader.f32(player.critChance) ||
                !reader.u8(secondary) || !reader.i32(player.level) || !reader.i32(player.xp) ||
                !reader.i32(player.gold) || !reader.i32(player.kills) ||
                !reader.u8(player.classId) || !reader.u8(dead)) {
                return false;
            }
            player.secondaryUnlocked = secondary != 0;
            player.dead = dead != 0;

            core::u16 inventoryCount = 0;
            if (!reader.u16(inventoryCount) || inventoryCount > kMaxSnapshotInventory) {
                return false;
            }
            player.inventory.resize(inventoryCount);
            for (core::ItemNetState& item : player.inventory) {
                if (!reader.item(item)) return false;
            }
            for (core::ItemNetState& item : player.equipment) {
                if (!reader.item(item)) return false;
            }
        }
        core::u16 enemyCount = 0;
        if (!reader.u16(enemyCount) || enemyCount > 512) return false;
        snapshot.enemies.resize(enemyCount);
        for (core::EnemyNetState& enemy : snapshot.enemies) {
            core::u8 alive = 0;
            core::u8 boss = 0;
            core::u8 ranged = 0;
            if (!reader.f32(enemy.position.x) || !reader.f32(enemy.position.y) ||
                !reader.f32(enemy.position.z) || !reader.f32(enemy.yaw) ||
                !reader.f32(enemy.health) || !reader.f32(enemy.maxHealth) ||
                !reader.f32(enemy.scale) || !reader.f32(enemy.color.x) ||
                !reader.f32(enemy.color.y) || !reader.f32(enemy.color.z) ||
                !reader.f32(enemy.color.w) || !reader.u8(alive) || !reader.u8(boss) ||
                !reader.u8(ranged)) {
                return false;
            }
            enemy.alive = alive != 0;
            enemy.boss = boss != 0;
            enemy.ranged = ranged != 0;
        }
        core::u16 projectileCount = 0;
        if (!reader.u16(projectileCount) || projectileCount > 1024) return false;
        snapshot.projectiles.resize(projectileCount);
        for (core::ProjectileNetState& projectile : snapshot.projectiles) {
            core::u8 fromPlayer = 0;
            if (!reader.f32(projectile.position.x) || !reader.f32(projectile.position.y) ||
                !reader.f32(projectile.position.z) || !reader.f32(projectile.color.x) ||
                !reader.f32(projectile.color.y) || !reader.f32(projectile.color.z) ||
                !reader.f32(projectile.color.w) || !reader.f32(projectile.radius) ||
                !reader.u8(fromPlayer)) {
                return false;
            }
            projectile.fromPlayer = fromPlayer != 0;
        }
        core::u16 lootCount = 0;
        if (!reader.u16(lootCount) || lootCount > 512) return false;
        snapshot.loot.resize(lootCount);
        for (core::LootNetState& loot : snapshot.loot) {
            if (!reader.f32(loot.position.x) || !reader.f32(loot.position.y) ||
                !reader.f32(loot.position.z) || !reader.u8(loot.rarity)) {
                return false;
            }
        }
        message.payload = std::move(snapshot);
        break;
    }
    default:
        return false;
    }

    if (reader.remaining() != 0) {
        return false;
    }
    out = std::move(message);
    return true;
}

const char* messageTypeName(MessageType type) {
    switch (type) {
    case MessageType::Hello: return "Hello";
    case MessageType::Welcome: return "Welcome";
    case MessageType::LobbyState: return "LobbyState";
    case MessageType::ClassChoice: return "ClassChoice";
    case MessageType::StartRun: return "StartRun";
    case MessageType::InputCommand: return "InputCommand";
    case MessageType::InputAck: return "InputAck";
    case MessageType::Checksum: return "Checksum";
    case MessageType::Ping: return "Ping";
    case MessageType::Pong: return "Pong";
    case MessageType::Disconnect: return "Disconnect";
    case MessageType::ServerTick: return "ServerTick";
    case MessageType::Snapshot: return "Snapshot";
    case MessageType::ActionRequest: return "ActionRequest";
    }
    return "Unknown";
}

} // namespace eee::net
