#include "TestHarness.hpp"

#include "eee/net/Protocol.hpp"
#include "eee/net/TcpTransport.hpp"

#include <chrono>
#include <optional>
#include <thread>

namespace {

using namespace eee::net;
bool gTcpUnavailable = false;
namespace core = eee::core;

template <typename T>
const T& payload(const Message& message) {
    return std::get<T>(message.payload);
}

void testProtocolRoundTrips() {
    {
        Message message{MessageType::Hello, Hello{2, 0x1122334455667788ull}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(decoded.type == MessageType::Hello);
        CHECK(payload<Hello>(decoded).classId == 2);
        CHECK(payload<Hello>(decoded).nonce == 0x1122334455667788ull);
    }
    {
        Message message{MessageType::Welcome, Welcome{1, 42, 60}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(payload<Welcome>(decoded).playerId == 1);
        CHECK(payload<Welcome>(decoded).sessionId == 42);
        CHECK(payload<Welcome>(decoded).tickRate == 60);
    }
    {
        Message message{MessageType::LobbyState, LobbyState{1, true, 0xABCDEF}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(payload<LobbyState>(decoded).clientReady);
        CHECK(payload<LobbyState>(decoded).seed == 0xABCDEF);
    }
    {
        Message message{MessageType::ClassChoice, ClassChoice{2}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(payload<ClassChoice>(decoded).classId == 2);
    }
    {
        Message message{MessageType::StartRun, StartRun{7, 3, 2, 100, 1, 2, true}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(payload<StartRun>(decoded).seed == 7);
        CHECK(payload<StartRun>(decoded).zoneIndex == 3);
        CHECK(payload<StartRun>(decoded).activePlayers == 2);
        CHECK(payload<StartRun>(decoded).startTick == 100);
        CHECK(payload<StartRun>(decoded).hostClass == 1);
        CHECK(payload<StartRun>(decoded).clientClass == 2);
        CHECK(payload<StartRun>(decoded).authoritative);
    }
    {
        core::PlayerInput input;
        input.moveX = 0.5f;
        input.moveZ = -1.0f;
        input.cameraYaw = 1.25f;
        input.cameraPitch = 0.3f;
        input.jump = true;
        input.attack = true;
        input.skillQ = true;
        input.sprint = true;
        Message message{MessageType::InputCommand, InputCommand{1, 999, input}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        const InputCommand& command = payload<InputCommand>(decoded);
        CHECK(command.playerId == 1);
        CHECK(command.tick == 999);
        CHECK(command.input.moveX == 0.5f);
        CHECK(command.input.moveZ == -1.0f);
        CHECK(command.input.cameraYaw == 1.25f);
        CHECK(command.input.jump);
        CHECK(command.input.attack);
        CHECK(command.input.skillQ);
        CHECK(command.input.sprint);
        CHECK(!command.input.interact);
        CHECK(!command.input.skillE);
    }
    {
        Message message{MessageType::InputAck, InputAck{17}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(payload<InputAck>(decoded).tick == 17);
    }
    {
        Message message{MessageType::Checksum, Checksum{33, 0xDEADBEEF}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(payload<Checksum>(decoded).tick == 33);
        CHECK(payload<Checksum>(decoded).hash == 0xDEADBEEF);
    }
    {
        Message message{MessageType::Ping, Timestamp{12345}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(decoded.type == MessageType::Ping);
        CHECK(payload<Timestamp>(decoded).value == 12345);
    }
    {
        Message message{MessageType::Pong, Timestamp{54321}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(decoded.type == MessageType::Pong);
        CHECK(payload<Timestamp>(decoded).value == 54321);
    }
    {
        Message message{MessageType::Disconnect, Disconnect{4}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(payload<Disconnect>(decoded).reason == 4);
    }
    {
        core::WorldSnapshot snapshot;
        snapshot.tick = 55;
        snapshot.zoneIndex = 2;
        snapshot.portalActive = true;
        snapshot.players[0].position = {1, 2, 3};
        snapshot.players[0].health = 42;
        snapshot.players[1].classId = 2;
        snapshot.players[1].dead = true;
        core::EnemyNetState enemy;
        enemy.position = {4, 0, 5};
        enemy.health = 12;
        enemy.boss = true;
        snapshot.enemies.push_back(enemy);
        core::ProjectileNetState projectile;
        projectile.position = {6, 1, 7};
        projectile.fromPlayer = true;
        snapshot.projectiles.push_back(projectile);
        core::LootNetState loot;
        loot.position = {8, 0, 9};
        loot.rarity = 3;
        snapshot.loot.push_back(loot);

        core::ItemNetState sword;
        sword.name = "RUNED BLADE";
        sword.baseName = "IRON SWORD";
        sword.slot = 0;
        sword.rarity = 2;
        sword.itemLevel = 9;
        sword.baseDamage = 44;
        sword.mods.push_back(core::ModifierNetState{0, 12.5f, "+12 DAMAGE"});
        sword.mods.push_back(core::ModifierNetState{2, 30.0f, "+30 LIFE"});
        snapshot.players[0].inventory.push_back(sword);

        core::ItemNetState flask;
        flask.name = "AZURE FLASK";
        flask.baseName = "AZURE FLASK";
        flask.slot = 6;
        flask.rarity = 1;
        flask.itemLevel = 2;
        flask.manaFlask = true;
        flask.flaskPower = 0.45f;
        snapshot.players[0].inventory.push_back(flask);

        core::ItemNetState equipped = sword;
        equipped.name = "EQUIPPED BLADE";
        equipped.rarity = 3;
        snapshot.players[0].equipment[0] = equipped;

        core::ItemNetState gem;
        gem.name = "SKILL GEM";
        gem.baseName = "RAIN OF ARROWS";
        gem.slot = 7;
        gem.rarity = 1;
        gem.itemLevel = 4;
        gem.gemSkill = 3;
        gem.gemClass = 1;
        gem.mods.push_back(core::ModifierNetState{9, 15.0f, "+15% SKILL DAMAGE"});
        snapshot.players[0].equipment[7] = gem;

        Message message{MessageType::Snapshot, snapshot};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(decoded.type == MessageType::Snapshot);
        const auto& out = std::get<core::WorldSnapshot>(decoded.payload);
        CHECK(out.tick == 55);
        CHECK(out.zoneIndex == 2);
        CHECK(out.portalActive);
        CHECK(out.players[0].position.x == 1);
        CHECK(out.players[0].health == 42);
        CHECK(out.players[1].dead);
        CHECK(out.enemies.size() == 1 && out.enemies[0].boss);
        CHECK(out.projectiles.size() == 1);
        CHECK(out.loot.size() == 1 && out.loot[0].rarity == 3);

        CHECK(out.players[0].inventory.size() == 2);
        const core::ItemNetState& outSword = out.players[0].inventory[0];
        CHECK(outSword.name == "RUNED BLADE");
        CHECK(outSword.baseName == "IRON SWORD");
        CHECK(outSword.slot == 0);
        CHECK(outSword.rarity == 2);
        CHECK(outSword.itemLevel == 9);
        CHECK(outSword.baseDamage == 44);
        CHECK(outSword.baseArmour == 0);
        CHECK(!outSword.manaFlask);
        CHECK(outSword.mods.size() == 2);
        CHECK(outSword.mods[0].stat == 0);
        CHECK(outSword.mods[0].value == 12.5f);
        CHECK(outSword.mods[0].label == "+12 DAMAGE");
        CHECK(outSword.mods[1].stat == 2);
        CHECK(outSword.mods[1].value == 30.0f);
        CHECK(outSword.mods[1].label == "+30 LIFE");

        const core::ItemNetState& outFlask = out.players[0].inventory[1];
        CHECK(outFlask.name == "AZURE FLASK");
        CHECK(outFlask.slot == 6);
        CHECK(outFlask.manaFlask);
        CHECK(outFlask.flaskPower == 0.45f);

        const core::ItemNetState& outEquipped = out.players[0].equipment[0];
        CHECK(outEquipped.name == "EQUIPPED BLADE");
        CHECK(outEquipped.rarity == 3);
        CHECK(outEquipped.mods.size() == 2);
        CHECK(outEquipped.mods[0].label == "+12 DAMAGE");

        const core::ItemNetState& outGem = out.players[0].equipment[7];
        CHECK(outGem.name == "SKILL GEM");
        CHECK(outGem.slot == 7);
        CHECK(outGem.gemSkill == 3);
        CHECK(outGem.gemClass == 1);
        CHECK(outGem.mods.size() == 1);
        CHECK(outGem.mods[0].stat == 9);
        CHECK(outGem.mods[0].value == 15.0f);
        CHECK(outGem.mods[0].label == "+15% SKILL DAMAGE");
        CHECK(out.players[0].equipment[1].name.empty());
        CHECK(out.players[1].inventory.empty());
    }
    {
        Message message{MessageType::ActionRequest, ActionRequest{1, 1, 7}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(decoded.type == MessageType::ActionRequest);
        CHECK(payload<ActionRequest>(decoded).playerId == 1);
        CHECK(payload<ActionRequest>(decoded).action == 1);
        CHECK(payload<ActionRequest>(decoded).index == 7);
    }
    {
        Message message{MessageType::ServerTick, ServerTick{321}};
        Message decoded;
        CHECK(decode(encode(message), decoded));
        CHECK(decoded.type == MessageType::ServerTick);
        CHECK(payload<ServerTick>(decoded).tick == 321);
    }
}

void testMalformedFrames() {
    Message message{MessageType::Hello, Hello{1, 55}};
    const std::vector<core::u8> good = encode(message);
    Message decoded;

    std::vector<core::u8> truncated = good;
    truncated.pop_back();
    CHECK(!decode(truncated, decoded));

    std::vector<core::u8> badVersion = good;
    badVersion[2] = 99;
    CHECK(!decode(badVersion, decoded));

    std::vector<core::u8> badType = good;
    badType[0] = 200;
    badType[1] = 0;
    CHECK(!decode(badType, decoded));

    std::vector<core::u8> trailing = good;
    trailing.push_back(0);
    CHECK(!decode(trailing, decoded));

    CHECK(!decode(std::span<const core::u8>{}, decoded));
}

std::optional<Message> receiveMessage(TcpTransport& transport, ConnectionId connection,
                                      int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        auto bytes = transport.receive(connection);
        if (bytes.has_value()) {
            Message message;
            if (!decode(*bytes, message)) {
                return std::nullopt;
            }
            return message;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

std::optional<ConnectionId> acceptWithTimeout(TcpTransport& transport,
                                              int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        auto connection = transport.accept();
        if (connection.has_value()) {
            return connection;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

void testTcpLoopback() {
    TcpTransport server;
    if (!server.listen(0)) {
        gTcpUnavailable = true;
        std::printf("net: TCP bind unavailable in this environment; skipping loopback\n");
        return;
    }
    CHECK(server.localPort() > 0);

    TcpTransport client;
    auto clientConnection = client.connect("127.0.0.1", server.localPort());
    CHECK(clientConnection.has_value());
    if (!clientConnection.has_value()) {
        return;
    }
    auto serverConnection = acceptWithTimeout(server);
    CHECK(serverConnection.has_value());
    if (!serverConnection.has_value()) {
        return;
    }

    const Message hello{MessageType::Hello, Hello{2, 0xFEEDFACE}};
    CHECK(client.send(*clientConnection, encode(hello)));
    auto received = receiveMessage(server, *serverConnection);
    CHECK(received.has_value());
    if (received.has_value()) {
        CHECK(received->type == MessageType::Hello);
        CHECK(payload<Hello>(*received).nonce == 0xFEEDFACE);
    }

    const Message welcome{MessageType::Welcome, Welcome{1, 7, 60}};
    const Message ping{MessageType::Ping, Timestamp{123}};
    CHECK(server.send(*serverConnection, encode(welcome)));
    CHECK(server.send(*serverConnection, encode(ping)));

    auto first = receiveMessage(client, *clientConnection);
    auto second = receiveMessage(client, *clientConnection);
    CHECK(first.has_value() && first->type == MessageType::Welcome);
    CHECK(second.has_value() && second->type == MessageType::Ping);
    if (second.has_value()) {
        CHECK(payload<Timestamp>(*second).value == 123);
    }

    client.close(*clientConnection);
    CHECK(!client.isConnected(*clientConnection));
    server.closeAll();
    client.closeAll();
}

} // namespace

int main() {
    testProtocolRoundTrips();
    testMalformedFrames();
    testTcpLoopback();
    if (gTcpUnavailable) {
        const int status = eee::test::report("net (TCP skipped)");
        return status == 0 ? 77 : status; // CTest SKIP_RETURN_CODE
    }
    return eee::test::report("net");
}
