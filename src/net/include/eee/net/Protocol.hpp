#pragma once

#include "eee/core/NetSnapshot.hpp"
#include "eee/core/PlayerInput.hpp"
#include "eee/core/Types.hpp"

#include <span>
#include <variant>
#include <vector>

namespace eee::net {

inline constexpr core::u16 kProtocolVersion = 3;
inline constexpr core::usize kMaxFramePayload = 64 * 1024;

// Bounds for inventory replication in core::WorldSnapshot.
inline constexpr core::u16 kMaxSnapshotInventory = 24;
inline constexpr core::u16 kMaxSnapshotEquipment = 8;
inline constexpr core::u16 kMaxSnapshotString = 128;
inline constexpr core::u16 kMaxSnapshotMods = 16;
inline constexpr core::u16 kMaxSnapshotSounds = 64;

enum class MessageType : core::u16 {
    Hello = 1,
    Welcome = 2,
    LobbyState = 3,
    ClassChoice = 4,
    StartRun = 5,
    InputCommand = 6,
    InputAck = 7,
    Checksum = 8,
    Ping = 9,
    Pong = 10,
    Disconnect = 11,
    ServerTick = 12,
    Snapshot = 13,
    ActionRequest = 14,
    ResumeRequest = 15,
    Ready = 16,
};

struct Hello {
    core::u8 classId = 0;
    core::u64 nonce = 0;
    core::u64 authToken = 0;
};

struct Welcome {
    core::u8 playerId = 0;
    core::u32 sessionId = 0;
    core::u16 tickRate = 60;
    core::u64 resumeToken = 0;
};

struct LobbyState {
    core::u8 hostClass = 0;
    core::u8 clientClass = 0;
    bool hostReady = false;
    bool clientReady = false;
    core::u64 seed = 0;
};

struct Ready {
    bool ready = true;
};

struct ClassChoice {
    core::u8 classId = 0;
};

struct StartRun {
    core::u64 seed = 0;
    core::i32 zoneIndex = 0;
    core::u8 activePlayers = 2;
    core::u64 startTick = 0;
    core::u8 hostClass = 0;
    core::u8 clientClass = 0;
    bool authoritative = false;
};

struct InputCommand {
    core::u8 playerId = 0;
    core::u64 tick = 0;
    core::PlayerInput input{};
};

struct InputAck {
    core::u64 tick = 0;
};

struct Checksum {
    core::u64 tick = 0;
    core::u64 hash = 0;
};

struct Timestamp {
    core::u64 value = 0;
};

struct Disconnect {
    core::u8 reason = 0;
};

struct ServerTick {
    core::u64 tick = 0;
};

struct ResumeRequest {
    core::u32 sessionId = 0;
    core::u8 playerId = 0;
    core::u64 token = 0;
};

struct ActionRequest {
    core::u8 playerId = 0;
    core::u8 action = 0; // 0 = equip/use, 1 = drop
    core::u16 index = 0;
};

using Payload = std::variant<Hello, Welcome, LobbyState, ClassChoice, StartRun, InputCommand,
                             InputAck, Checksum, Timestamp, Disconnect, ServerTick,
                             core::WorldSnapshot, ActionRequest, ResumeRequest, Ready>;

struct Message {
    MessageType type = MessageType::Ping;
    Payload payload = Timestamp{};
};

// Encodes [type u16][version u16][payload] in little-endian order.
// The TCP transport adds the outer u32 length prefix.
[[nodiscard]] std::vector<core::u8> encode(const Message& message);

// Validates the version, type, payload bounds and trailing bytes.
[[nodiscard]] bool decode(std::span<const core::u8> bytes, Message& out);

[[nodiscard]] const char* messageTypeName(MessageType type);

} // namespace eee::net
