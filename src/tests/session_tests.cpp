#include "TestHarness.hpp"

#include "eee/core/Input.hpp"
#include "eee/logic/RpgGame.hpp"
#include "eee/session/AuthoritativeClient.hpp"
#include "eee/session/LockstepSession.hpp"
#include "eee/session/SessionDriver.hpp"
#include "eee/net/TcpTransport.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <thread>

namespace {

bool gTcpUnavailable = false;

using namespace eee::core;
using namespace eee::logic;
namespace core = eee::core;

void beginRun(RpgGame& game) {
    InputState enter;
    enter.setKey(Key::Enter, true);
    game.update(1.0f / 60.0f, enter);
    CHECK(game.uiMode() == UiMode::Playing);
}

int countSounds(const std::vector<SoundId>& sounds, SoundId id) {
    return static_cast<int>(std::count(sounds.begin(), sounds.end(), id));
}

void testFixedTickRate() {
    RpgGame game;
    beginRun(game);
    eee::session::SessionDriver driver;

    for (int i = 0; i < 60; ++i) {
        driver.advance(game, InputState{}, 1.0f / 60.0f);
    }
    CHECK(driver.tickCount() == 60);

    // 30 render frames at 30 Hz = the same one second = 60 ticks.
    eee::session::SessionDriver second;
    for (int i = 0; i < 30; ++i) {
        second.advance(game, InputState{}, 1.0f / 30.0f);
    }
    CHECK(second.tickCount() == 60);
}

void testEdgesConsumedOnce() {
    RpgGame game;
    beginRun(game);
    (void)game.takeSoundEvents();
    eee::session::SessionDriver driver;

    InputState jump;
    jump.setKey(Key::Space, true);
    // 1/30 s = two fixed ticks in one render frame.
    driver.advance(game, jump, 1.0f / 30.0f);
    CHECK(driver.lastTickCount() == 2);
    CHECK(countSounds(game.takeSoundEvents(), SoundId::Jump) == 1);
    CHECK(!game.player().grounded);
}

void testRenderCadenceDeterminism() {
    RpgGame first;
    RpgGame second;
    beginRun(first);
    beginRun(second);
    eee::session::SessionDriver a;
    eee::session::SessionDriver b;

    InputState move;
    move.setKey(static_cast<Key>('W'), true);
    move.setKey(static_cast<Key>('D'), true);

    for (int i = 0; i < 30; ++i) {
        a.advance(first, move, 1.0f / 30.0f);
    }
    for (int i = 0; i < 120; ++i) {
        b.advance(second, move, 1.0f / 120.0f);
    }

    CHECK(a.tickCount() == b.tickCount());
    CHECK(first.player().position.x == second.player().position.x);
    CHECK(first.player().position.z == second.player().position.z);
    CHECK(first.player().yaw == second.player().yaw);
    CHECK(first.enemies().size() == second.enemies().size());
    for (eee::core::usize i = 0; i < first.enemies().size(); ++i) {
        CHECK(first.enemies()[i].position.x == second.enemies()[i].position.x);
        CHECK(first.enemies()[i].health == second.enemies()[i].health);
    }
}

void testMenuDoesNotTickSimulation() {
    RpgGame game;
    eee::session::SessionDriver driver;
    CHECK(game.uiMode() == UiMode::ClassSelect);
    driver.advance(game, InputState{}, 0.5f);
    CHECK(driver.tickCount() == 0);
    CHECK(game.uiMode() == UiMode::ClassSelect);
}

struct PeerPair {
    eee::net::TcpTransport server;
    eee::net::TcpTransport client;
    eee::net::ConnectionId serverConnection = 0;
    eee::net::ConnectionId clientConnection = 0;
};

bool setupPeers(PeerPair& peers) {
    if (!peers.server.listen(0)) {
        gTcpUnavailable = true;
        return false;
    }
    auto client = peers.client.connect("127.0.0.1", peers.server.localPort());
    if (!client.has_value()) return false;
    peers.clientConnection = *client;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        auto server = peers.server.accept();
        if (server.has_value()) {
            peers.serverConnection = *server;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

class DelayedTransport final : public eee::net::ITransport {
public:
    DelayedTransport(eee::net::ITransport& inner, int delayMs)
        : inner_(inner), delayMs_(delayMs) {}

    bool listen(core::u16) override { return false; }
    [[nodiscard]] core::u16 localPort() const override { return inner_.localPort(); }
    [[nodiscard]] std::optional<eee::net::ConnectionId> accept() override {
        return inner_.accept();
    }
    [[nodiscard]] std::optional<eee::net::ConnectionId> connect(std::string_view host,
                                                                 core::u16 port) override {
        return inner_.connect(host, port);
    }
    bool send(eee::net::ConnectionId connection, std::span<const core::u8> payload) override {
        pending_.push_back(Pending{std::chrono::steady_clock::now() +
                                       std::chrono::milliseconds(delayMs_),
                                   connection, std::vector<core::u8>(payload.begin(), payload.end())});
        return true;
    }
    [[nodiscard]] std::optional<std::vector<core::u8>> receive(
        eee::net::ConnectionId connection) override {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->due <= now) {
                inner_.send(it->connection, it->payload);
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
        return inner_.receive(connection);
    }
    void close(eee::net::ConnectionId connection) override { inner_.close(connection); }
    void closeAll() override { inner_.closeAll(); }
    [[nodiscard]] bool isConnected(eee::net::ConnectionId connection) const override {
        return inner_.isConnected(connection);
    }

private:
    struct Pending {
        std::chrono::steady_clock::time_point due;
        eee::net::ConnectionId connection;
        std::vector<core::u8> payload;
    };
    eee::net::ITransport& inner_;
    int delayMs_;
    std::deque<Pending> pending_;
};

class ScriptedTransport final : public eee::net::ITransport {
public:
    bool listen(core::u16) override { return false; }
    [[nodiscard]] core::u16 localPort() const override { return 0; }
    [[nodiscard]] std::optional<eee::net::ConnectionId> accept() override {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<eee::net::ConnectionId> connect(std::string_view,
                                                                 core::u16) override {
        return std::nullopt;
    }
    bool send(eee::net::ConnectionId, std::span<const core::u8>) override {
        ++sent_;
        return true;
    }
    [[nodiscard]] std::optional<std::vector<core::u8>> receive(
        eee::net::ConnectionId) override {
        if (inbox_.empty()) return std::nullopt;
        std::vector<core::u8> bytes = std::move(inbox_.front());
        inbox_.pop_front();
        return bytes;
    }
    void close(eee::net::ConnectionId) override {}
    void closeAll() override {}
    [[nodiscard]] bool isConnected(eee::net::ConnectionId) const override { return true; }

    void push(const eee::net::Message& message) { inbox_.push_back(eee::net::encode(message)); }
    [[nodiscard]] core::u32 sent() const { return sent_; }

private:
    std::deque<std::vector<core::u8>> inbox_;
    core::u32 sent_ = 0;
};

void testAuthoritativePredictionReconciliation() {
    RpgGame game;
    game.beginCoop(ClassId::Warrior, ClassId::Archer);
    game.setLocalViewerId(core::PlayerId::P1);

    ScriptedTransport transport;
    eee::session::AuthoritativeClient client(transport, 7, core::PlayerId::P1, 3);

    RpgGame source;
    source.beginCoop(ClassId::Warrior, ClassId::Archer);
    source.player(core::PlayerId::P1).position = {0.0f, 0.0f, 4.0f};
    source.player(core::PlayerId::P1).health = 88.0f;
    source.enemiesMutable()[0].position = {5.0f, 0.0f, 5.0f};

    auto sendSnapshot = [&](core::u64 tick) {
        eee::net::Message message;
        message.type = eee::net::MessageType::Snapshot;
        message.payload = source.captureNetworkSnapshot(tick);
        transport.push(message);
    };

    sendSnapshot(100);
    CHECK(client.poll(game) == 1);
    CHECK(client.hasAuthoritativeLocal());
    CHECK(game.player(core::PlayerId::P1).position.z == 4.0f);
    CHECK(game.player(core::PlayerId::P1).health == 88.0f);

    const core::Vec3 p0Before = game.player(core::PlayerId::P0).position;
    const core::Vec3 enemyBefore = game.enemies()[0].position;
    const core::f32 healthBefore = game.player(core::PlayerId::P1).health;

    core::PlayerInput forward;
    forward.moveZ = 1.0f;
    forward.cameraYaw = 0.0f;
    forward.cameraPitch = 0.36f;
    client.sendInput(forward);
    CHECK(client.pendingInputCount() == 1);
    client.updatePrediction(game, forward, 1.0f / 60.0f);

    CHECK(client.predictedPlayer().valid);
    CHECK(client.predictedPlayer().position.z < 4.0f);
    CHECK(game.player(core::PlayerId::P1).position.z < 4.0f);
    CHECK(game.player(core::PlayerId::P0).position.x == p0Before.x);
    CHECK(game.player(core::PlayerId::P0).position.z == p0Before.z);
    CHECK(game.enemies()[0].position.x == enemyBefore.x);
    CHECK(game.enemies()[0].position.z == enemyBefore.z);
    CHECK(game.player(core::PlayerId::P1).health == healthBefore);
    CHECK(client.snapshotsApplied() == 1);
    CHECK(transport.sent() >= 2); // input + checksum

    // A nearby authoritative correction is blended, not snapped.
    source.player(core::PlayerId::P1).position = {0.0f, 0.0f, 3.9f};
    sendSnapshot(104);
    CHECK(client.poll(game) == 1);
    CHECK(client.lastPredictionError() < eee::session::AuthoritativeClient::kSnapThreshold);
    CHECK(!client.lastPredictionSnapped());
    // Snapshot application is immediate; the acknowledged input is dropped.
    CHECK(game.player(core::PlayerId::P1).position.z == 3.9f);
    CHECK(client.pendingInputCount() == 0);

    client.updatePrediction(game, core::PlayerInput{}, 1.0f / 60.0f);
    CHECK(client.predictedPlayer().valid);
    CHECK(std::fabs(client.predictedPlayer().position.z - 3.9f) < 1.0f);
    for (int i = 0; i < 30; ++i) {
        client.updatePrediction(game, core::PlayerInput{}, 1.0f / 60.0f);
    }
    CHECK(std::fabs(client.predictedPlayer().position.z - 3.9f) < 0.01f);

    // A large correction snaps straight to the authoritative position.
    source.player(core::PlayerId::P1).position = {30.0f, 0.0f, 30.0f};
    sendSnapshot(110);
    CHECK(client.poll(game) == 1);
    CHECK(client.lastPredictionError() > eee::session::AuthoritativeClient::kSnapThreshold);
    CHECK(client.lastPredictionSnapped());
    CHECK(game.player(core::PlayerId::P1).position.x == 30.0f);
    CHECK(game.player(core::PlayerId::P1).position.z == 30.0f);

    client.updatePrediction(game, core::PlayerInput{}, 1.0f / 60.0f);
    CHECK((client.predictedPlayer().position - core::Vec3{30.0f, 0.0f, 30.0f}).length() < 0.01f);
}

void testAuthoritativeRemoteInterpolation() {
    RpgGame game;
    game.beginCoop(ClassId::Warrior, ClassId::Archer);
    game.setLocalViewerId(core::PlayerId::P1);

    ScriptedTransport transport;
    eee::session::AuthoritativeClient client(transport, 7, core::PlayerId::P1, 3);

    auto makeSnapshot = [](core::u64 tick, core::f32 remoteZ, core::f32 remoteYaw,
                           core::f32 remoteHealth, core::f32 enemyX, core::f32 enemyYaw,
                           core::f32 enemyHealth, core::f32 projectileX, core::f32 lootX) {
        core::WorldSnapshot snapshot;
        snapshot.tick = tick;

        core::PlayerNetState& remote = snapshot.players[0];
        remote.position = {0.0f, 0.0f, remoteZ};
        remote.yaw = remoteYaw;
        remote.health = remoteHealth;
        remote.maxHealth = 100.0f;
        remote.classId = static_cast<core::u8>(ClassId::Warrior);

        core::PlayerNetState& local = snapshot.players[1];
        local.position = {0.0f, 0.0f, 4.0f};
        local.yaw = 0.0f;
        local.health = 100.0f;
        local.maxHealth = 100.0f;
        local.mana = 55.0f;
        local.maxMana = 55.0f;
        local.moveSpeed = 5.0f;
        local.classId = static_cast<core::u8>(ClassId::Archer);

        core::EnemyNetState enemy;
        enemy.position = {enemyX, 0.0f, 0.0f};
        enemy.yaw = enemyYaw;
        enemy.health = enemyHealth;
        enemy.maxHealth = 100.0f;
        enemy.scale = 1.0f;
        enemy.color = {1.0f, 0.5f, 0.5f, 1.0f};
        enemy.alive = true;
        snapshot.enemies.push_back(enemy);

        core::ProjectileNetState projectile;
        projectile.position = {projectileX, 0.0f, 0.0f};
        snapshot.projectiles.push_back(projectile);

        core::LootNetState loot;
        loot.position = {lootX, 0.0f, 0.0f};
        loot.rarity = 1;
        snapshot.loot.push_back(loot);

        return snapshot;
    };

    auto pushSnapshot = [&](const core::WorldSnapshot& snapshot) {
        eee::net::Message message;
        message.type = eee::net::MessageType::Snapshot;
        message.payload = snapshot;
        transport.push(message);
    };

    CHECK(client.currentSnapshotTick() == 0);
    CHECK(!client.hasPreviousSnapshot());

    pushSnapshot(makeSnapshot(100, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0.0f));
    CHECK(client.poll(game) == 1);
    CHECK(client.currentSnapshotTick() == 100);
    CHECK(!client.hasPreviousSnapshot());
    client.updateInterpolation(game, 0.0f);
    CHECK(game.player(core::PlayerId::P0).position.z == 0.0f);

    pushSnapshot(makeSnapshot(101, 10.0f, 1.0f, 80.0f, 10.0f, 1.0f, 60.0f, 10.0f,
                              10.0f));
    CHECK(client.poll(game) == 1);
    CHECK(client.currentSnapshotTick() == 101);
    CHECK(client.hasPreviousSnapshot());
    CHECK(client.interpolationAlpha() == 0.0f);

    // Snapshot application is immediate; interpolation starts at the previous
    // snapshot and the local player is predicted separately.
    core::PlayerInput forward;
    forward.moveZ = 1.0f;
    forward.cameraYaw = 0.0f;
    forward.cameraPitch = 0.36f;
    client.sendInput(forward);
    client.updatePrediction(game, forward, 1.0f / 60.0f);
    const core::Vec3 predictedLocal = game.player(core::PlayerId::P1).position;
    CHECK(predictedLocal.z < 4.0f);

    client.updateInterpolation(game, 0.0f);
    CHECK(eee::test::nearlyEqual(client.interpolationAlpha(), 0.0f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P0).position.z, 0.0f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P0).yaw, 0.0f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P0).health, 100.0f));
    CHECK(eee::test::nearlyEqual(game.enemies()[0].position.x, 0.0f));
    CHECK(eee::test::nearlyEqual(game.enemies()[0].yaw, 0.0f));
    CHECK(eee::test::nearlyEqual(game.enemies()[0].health, 100.0f));
    CHECK(eee::test::nearlyEqual(game.projectiles()[0].position.x, 0.0f));
    CHECK(eee::test::nearlyEqual(game.loot()[0].position.x, 0.0f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P1).position.z,
                                  predictedLocal.z));

    client.updateInterpolation(game, 0.5f / 60.0f);
    CHECK(eee::test::nearlyEqual(client.interpolationAlpha(), 0.5f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P0).position.z, 5.0f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P0).yaw, 0.5f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P0).health, 90.0f));
    CHECK(eee::test::nearlyEqual(game.enemies()[0].position.x, 5.0f));
    CHECK(eee::test::nearlyEqual(game.enemies()[0].yaw, 0.5f));
    CHECK(eee::test::nearlyEqual(game.enemies()[0].health, 80.0f));
    CHECK(eee::test::nearlyEqual(game.projectiles()[0].position.x, 5.0f));
    CHECK(eee::test::nearlyEqual(game.loot()[0].position.x, 5.0f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P1).position.z,
                                  predictedLocal.z));

    client.updateInterpolation(game, 1.0f / 60.0f);
    CHECK(eee::test::nearlyEqual(client.interpolationAlpha(), 1.0f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P0).position.z, 10.0f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P0).yaw, 1.0f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P0).health, 80.0f));
    CHECK(eee::test::nearlyEqual(game.enemies()[0].position.x, 10.0f));
    CHECK(eee::test::nearlyEqual(game.enemies()[0].yaw, 1.0f));
    CHECK(eee::test::nearlyEqual(game.enemies()[0].health, 60.0f));
    CHECK(eee::test::nearlyEqual(game.projectiles()[0].position.x, 10.0f));
    CHECK(eee::test::nearlyEqual(game.loot()[0].position.x, 10.0f));
    CHECK(eee::test::nearlyEqual(game.player(core::PlayerId::P1).position.z,
                                  predictedLocal.z));
}

void testLockstepLoopback() {
    PeerPair peers;
    if (!setupPeers(peers)) return;

    RpgGame first;
    RpgGame second;
    beginRun(first);
    beginRun(second);
    first.setActivePlayers(2);
    second.setActivePlayers(2);

    eee::session::LockstepConfig configA;
    configA.localPlayer = core::PlayerId::P0;
    eee::session::LockstepConfig configB;
    configB.localPlayer = core::PlayerId::P1;
    eee::session::LockstepSession a(peers.server, peers.serverConnection, configA);
    eee::session::LockstepSession b(peers.client, peers.clientConnection, configB);

    core::PlayerInput inputA;
    inputA.moveZ = 1.0f;
    inputA.cameraPitch = 0.36f;
    core::PlayerInput inputB;
    inputB.moveX = 1.0f;
    inputB.cameraPitch = 0.36f;

    for (int i = 0; i < 4000; ++i) {
        a.advance(first, inputA, 1.0f / 60.0f);
        b.advance(second, inputB, 1.0f / 60.0f);
        if (a.desynced() || b.desynced() || a.disconnected() || b.disconnected()) break;
        if (a.tickCount() >= 600 && b.tickCount() >= 600) break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }

    CHECK(!a.desynced());
    CHECK(!b.desynced());
    CHECK(!a.disconnected());
    CHECK(!b.disconnected());
    CHECK(a.tickCount() >= 600);
    CHECK(b.tickCount() >= 600);
    CHECK(first.player(core::PlayerId::P0).position.x ==
          second.player(core::PlayerId::P0).position.x);
    CHECK(first.player(core::PlayerId::P0).position.z ==
          second.player(core::PlayerId::P0).position.z);
    CHECK(a.lastChecksum() == b.lastChecksum());

    a.poll();
    peers.client.closeAll();
    peers.server.closeAll();
}

void testLockstepLatency() {
    PeerPair peers;
    if (!setupPeers(peers)) return;

    RpgGame first;
    RpgGame second;
    beginRun(first);
    beginRun(second);
    first.setActivePlayers(2);
    second.setActivePlayers(2);

    DelayedTransport serverDelay(peers.server, 12);
    DelayedTransport clientDelay(peers.client, 12);
    eee::session::LockstepConfig configA;
    configA.localPlayer = core::PlayerId::P0;
    eee::session::LockstepConfig configB;
    configB.localPlayer = core::PlayerId::P1;
    eee::session::LockstepSession a(serverDelay, peers.serverConnection, configA);
    eee::session::LockstepSession b(clientDelay, peers.clientConnection, configB);

    core::PlayerInput inputA;
    inputA.moveZ = 1.0f;
    inputA.cameraPitch = 0.36f;
    core::PlayerInput inputB;
    inputB.moveX = 1.0f;
    inputB.cameraPitch = 0.36f;

    for (int i = 0; i < 8000; ++i) {
        a.advance(first, inputA, 1.0f / 60.0f);
        b.advance(second, inputB, 1.0f / 60.0f);
        if (a.desynced() || b.desynced()) break;
        if (a.tickCount() >= 120 && b.tickCount() >= 120) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CHECK(!a.desynced());
    CHECK(!b.desynced());
    CHECK(a.tickCount() >= 120);
    CHECK(b.tickCount() >= 120);
    CHECK(first.player(core::PlayerId::P0).position.z ==
          second.player(core::PlayerId::P0).position.z);
}

void testLockstepDesyncDetection() {
    PeerPair peers;
    if (!setupPeers(peers)) return;

    RpgGame first;
    RpgGame second;
    beginRun(first);
    beginRun(second);
    first.setActivePlayers(2);
    second.setActivePlayers(2);

    eee::session::LockstepConfig configA;
    configA.localPlayer = core::PlayerId::P0;
    eee::session::LockstepConfig configB;
    configB.localPlayer = core::PlayerId::P1;
    eee::session::LockstepSession a(peers.server, peers.serverConnection, configA);
    eee::session::LockstepSession b(peers.client, peers.clientConnection, configB);

    core::PlayerInput idle;
    for (int i = 0; i < 2000; ++i) {
        a.advance(first, idle, 1.0f / 60.0f);
        b.advance(second, idle, 1.0f / 60.0f);
        if (a.tickCount() >= 60 && b.tickCount() >= 60) break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    first.player(core::PlayerId::P0).health -= 50.0f;

    for (int i = 0; i < 3000; ++i) {
        a.advance(first, idle, 1.0f / 60.0f);
        b.advance(second, idle, 1.0f / 60.0f);
        if (a.desynced() || b.desynced()) break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    CHECK(a.desynced() || b.desynced());
}

void testCatchUpCap() {
    RpgGame game;
    beginRun(game);
    eee::session::SessionDriver driver(1.0f / 60.0f, 5);
    const core::u64 before = driver.tickCount();
    driver.advance(game, InputState{}, 1.0f); // one second backlog
    CHECK(driver.tickCount() - before == 5);
}

} // namespace

int main() {
    testFixedTickRate();
    testEdgesConsumedOnce();
    testRenderCadenceDeterminism();
    testLockstepLoopback();
    testLockstepLatency();
    testAuthoritativePredictionReconciliation();
    testAuthoritativeRemoteInterpolation();
    testLockstepDesyncDetection();
    testMenuDoesNotTickSimulation();
    testCatchUpCap();
    if (gTcpUnavailable) {
        std::printf("session: TCP bind unavailable in this environment; skipping TCP tests\n");
        const int status = eee::test::report("session (TCP skipped)");
        return status == 0 ? 77 : status; // CTest SKIP_RETURN_CODE
    }
    return eee::test::report("session");
}
