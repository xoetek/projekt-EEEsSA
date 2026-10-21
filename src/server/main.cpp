#include "eee/core/Log.hpp"
#include "eee/logic/RpgGame.hpp"
#include "eee/net/Protocol.hpp"
#include "eee/net/Auth.hpp"
#include "eee/net/RateLimiter.hpp"
#include "eee/net/TcpTransport.hpp"

#include <array>
#include <array>
#include <chrono>
#include <map>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>

namespace {

using eee::core::logf;
using eee::core::LogLevel;
using eee::net::ConnectionId;
using eee::net::Message;
using eee::net::MessageType;
using eee::net::TcpTransport;

std::optional<ConnectionId> acceptBlocking(TcpTransport& transport, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        auto connection = transport.accept();
        if (connection.has_value()) {
            return connection;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return std::nullopt;
}

std::optional<Message> waitForMessage(TcpTransport& transport, ConnectionId connection,
                                      int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        auto bytes = transport.receive(connection);
        if (bytes.has_value()) {
            Message message;
            if (eee::net::decode(*bytes, message)) {
                return message;
            }
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

bool send(TcpTransport& transport, ConnectionId connection, const Message& message) {
    return transport.send(connection, eee::net::encode(message));
}

void printUsage() {
    std::printf(
        "EEEsSA dedicated relay server\n"
        "\n"
        "Usage: eee_server [options]\n"
        "  --port=<n>       TCP listen port (default 27015)\n"
        "  --password=<pw>  Require this shared secret from clients\n"
        "  --once           Exit after one two-player session\n"
        "  --help           Show this message\n"
        "\n"
        "This is a relay, not an authoritative server. Clients must use the same\n"
        "build. Internet play requires a reachable public IP and TCP port forwarding.\n"
        "The protocol is plaintext and unauthenticated; do not expose it publicly\n"
        "without a firewall/VPN or an authentication layer.\n");
}

} // namespace

int main(int argc, char** argv) {
    int port = 27015;
    bool once = false;
    std::string password;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else if (arg == "--once") {
            once = true;
        } else if (arg.starts_with("--port=")) {
            port = std::atoi(arg.substr(7).c_str());
        } else if (arg.starts_with("--password=")) {
            password = arg.substr(11);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            return 2;
        }
    }
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "invalid port\n");
        return 2;
    }

    TcpTransport transport;
    if (!transport.listen(static_cast<eee::core::u16>(port))) {
        logf(LogLevel::Error, "server: failed to listen on port %d", port);
        return 1;
    }
    const eee::core::u64 expectedAuthToken = eee::net::hashSecret(password);
    logf(LogLevel::Info, "server: listening on TCP port %u%s", transport.localPort(),
         password.empty() ? "" : " (password required)");

    eee::core::u32 sessionCounter = 0;
    while (true) {
        auto first = acceptBlocking(transport, 24 * 60 * 60 * 1000);
        if (!first.has_value()) {
            continue;
        }
        logf(LogLevel::Info, "server: first client connected");
        auto second = acceptBlocking(transport, 120000);
        if (!second.has_value()) {
            logf(LogLevel::Warning, "server: second client did not arrive");
            transport.close(*first);
            if (once) return 1;
            continue;
        }
        logf(LogLevel::Info, "server: second client connected");

        std::array<ConnectionId, 2> connections{*first, *second};
        const eee::core::u32 sessionId = ++sessionCounter;
        std::array<eee::core::u64, 2> resumeTokens{};
        for (int i = 0; i < 2; ++i) {
            resumeTokens[i] = 0x9E3779B97F4A7C15ull ^
                              (static_cast<eee::core::u64>(sessionId) << 17) ^
                              static_cast<eee::core::u64>(i + 1);
        }

        std::array<eee::core::u8, 2> classes{0, 0};
        bool handshakeOk = true;
        for (int i = 0; i < 2; ++i) {
            // Authenticate before sending the Welcome/resume token.
            auto helloMessage = waitForMessage(transport, connections[i], 15000);
            if (!helloMessage.has_value() || helloMessage->type != MessageType::Hello) {
                handshakeOk = false;
                break;
            }
            const auto& hello = std::get<eee::net::Hello>(helloMessage->payload);
            if (!password.empty() &&
                !eee::net::constantTimeEqual(hello.authToken, expectedAuthToken)) {
                logf(LogLevel::Warning, "server: P%d authentication failed", i);
                handshakeOk = false;
                break;
            }

            Message welcome;
            welcome.type = MessageType::Welcome;
            welcome.payload = eee::net::Welcome{static_cast<eee::core::u8>(i), sessionId, 60,
                                                     resumeTokens[i]};
            if (!send(transport, connections[i], welcome)) {
                handshakeOk = false;
                break;
            }

            bool gotClass = false;
            for (int attempt = 0; attempt < 2 && !gotClass; ++attempt) {
                auto incoming = waitForMessage(transport, connections[i], 15000);
                if (!incoming.has_value()) break;
                if (incoming->type == MessageType::ClassChoice) {
                    classes[i] = std::get<eee::net::ClassChoice>(incoming->payload).classId;
                    gotClass = true;
                }
            }
            if (!gotClass) {
                handshakeOk = false;
                break;
            }
        }
        if (!handshakeOk) {
            logf(LogLevel::Warning, "server: lobby handshake/authentication failed");
            for (ConnectionId id : connections) {
                Message bye;
                bye.type = MessageType::Disconnect;
                bye.payload = eee::net::Disconnect{3};
                send(transport, id, bye);
                transport.close(id);
            }
            if (once) return 1;
            continue;
        }
        if (!handshakeOk) {
            logf(LogLevel::Warning, "server: lobby handshake failed");
            for (ConnectionId id : connections) transport.close(id);
            if (once) return 1;
            continue;
        }

        Message start;
        start.type = MessageType::StartRun;
        start.payload = eee::net::StartRun{0xC0FFEEull, 0, 2, 0, classes[0], classes[1], true};
        for (ConnectionId id : connections) {
            if (!send(transport, id, start)) {
                handshakeOk = false;
            }
        }
        if (!handshakeOk) {
            for (ConnectionId id : connections) transport.close(id);
            if (once) return 1;
            continue;
        }

        logf(LogLevel::Info, "server: session %u starting (P0 class=%u, P1 class=%u)",
             sessionId, static_cast<unsigned>(classes[0]), static_cast<unsigned>(classes[1]));

        eee::logic::RpgGame authoritative;
        authoritative.beginCoop(static_cast<eee::logic::ClassId>(classes[0]),
                                static_cast<eee::logic::ClassId>(classes[1]));
        authoritative.setActivePlayers(2);

        std::array<std::map<eee::core::u64, eee::core::PlayerInput>, 2> pendingInputs;
        std::array<std::map<eee::core::u64, eee::core::u64>, 2> clientChecksums;
        std::map<eee::core::u64, eee::core::u64> serverChecksums;
        eee::core::u64 authoritativeTick = 0;
        eee::core::u64 snapshotsSent = 0;
        bool authoritativeOk = true;
        bool mismatchLogged = false;
        std::array<bool, 2> active{true, true};
        std::array<bool, 2> grace{false, false};
        std::array<std::chrono::steady_clock::time_point, 2> graceDeadline{};
        struct ClientGuard {
            eee::net::TokenBucket messages{300.0, 120.0};
            eee::net::TokenBucket actions{20.0, 10.0};
            eee::core::u32 invalidTicks = 0;
        };
        std::array<ClientGuard, 2> guards{};
        const auto sessionStart = std::chrono::steady_clock::now();

        auto compareChecksum = [&](int client, eee::core::u64 tick, eee::core::u64 hash) {
            const auto server = serverChecksums.find(tick);
            if (server == serverChecksums.end()) return;
            if (server->second != hash) {
                authoritativeOk = false;
                if (!mismatchLogged) {
                    mismatchLogged = true;
                    logf(LogLevel::Error,
                         "authoritative: checksum mismatch at tick %llu from P%d "
                         "(server %llx client %llx)",
                         static_cast<unsigned long long>(tick), client,
                         static_cast<unsigned long long>(server->second),
                         static_cast<unsigned long long>(hash));
                }
            }
        };

        auto trySimulateAuthoritative = [&]() -> bool {
            if (authoritative.uiMode() != eee::logic::UiMode::Playing) {
                return false;
            }
            const bool neutralWarmup = authoritativeTick < 3;
            bool ready = true;
            for (int p = 0; p < 2; ++p) {
                if (active[p] && !neutralWarmup &&
                    pendingInputs[p].find(authoritativeTick) == pendingInputs[p].end()) {
                    ready = false;
                }
                if (!active[p] && !grace[p]) {
                    ready = false;
                }
            }
            if (!ready) {
                return false;
            }

            eee::core::InputFrame frame;
            frame.activePlayers = 2;
            for (int p = 0; p < 2; ++p) {
                auto it = pendingInputs[p].find(authoritativeTick);
                if (it != pendingInputs[p].end()) {
                    frame.players[p] = it->second;
                    pendingInputs[p].erase(it);
                }
            }
            authoritative.updateFrame(1.0f / 60.0f, frame);
            ++authoritativeTick;

            Message tickMessage;
            tickMessage.type = MessageType::ServerTick;
            tickMessage.payload = eee::net::ServerTick{authoritativeTick};
            for (int p = 0; p < 2; ++p) {
                if (active[p]) {
                    send(transport, connections[p], tickMessage);
                }
            }

            eee::core::WorldSnapshot snapshot =
                authoritative.captureNetworkSnapshot(authoritativeTick);
            Message snapshotMessage;
            snapshotMessage.type = MessageType::Snapshot;
            snapshotMessage.payload = std::move(snapshot);
            for (int p = 0; p < 2; ++p) {
                if (active[p] && send(transport, connections[p], snapshotMessage)) {
                    ++snapshotsSent;
                }
            }

            if (authoritativeTick % 30 == 0) {
                const eee::core::u64 hash = authoritative.checksum();
                serverChecksums[authoritativeTick] = hash;
                for (int p = 0; p < 2; ++p) {
                    auto it = clientChecksums[p].find(authoritativeTick);
                    if (it != clientChecksums[p].end()) {
                        compareChecksum(p, authoritativeTick, it->second);
                    }
                }
            }
            return true;
        };

        auto lastSimulation = std::chrono::steady_clock::now();
        eee::core::f32 simulationAccumulator = 0.0f;

        auto markDisconnected = [&](int p) {
            if (!active[p]) return;
            active[p] = false;
            grace[p] = true;
            graceDeadline[p] = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            pendingInputs[p].clear();
            clientChecksums[p].clear();
            guards[p] = ClientGuard{};
            logf(LogLevel::Warning, "server: P%d disconnected; holding slot for 20s", p);
        };

        while (active[0] || active[1] || grace[0] || grace[1]) {
            // Accept reconnect attempts while the session is live.
            while (true) {
                auto incoming = transport.accept();
                if (!incoming.has_value()) break;

                auto request = waitForMessage(transport, *incoming, 1500);
                bool resumed = false;
                if (request.has_value() && request->type == MessageType::ResumeRequest) {
                    const auto& resume = std::get<eee::net::ResumeRequest>(request->payload);
                    const int slot = resume.playerId;
                    if (resume.sessionId == sessionId && slot >= 0 && slot < 2 &&
                        !active[slot] && resume.token == resumeTokens[slot]) {
                        connections[slot] = *incoming;
                        active[slot] = true;
                        grace[slot] = false;
                        pendingInputs[slot].clear();
                        clientChecksums[slot].clear();
                        guards[slot] = ClientGuard{};

                        Message welcome;
                        welcome.type = MessageType::Welcome;
                        welcome.payload = eee::net::Welcome{static_cast<eee::core::u8>(slot),
                                                           sessionId, 60,
                                                           resumeTokens[slot]};
                        send(transport, connections[slot], welcome);

                        Message snapshot;
                        snapshot.type = MessageType::Snapshot;
                        snapshot.payload =
                            authoritative.captureNetworkSnapshot(authoritativeTick);
                        send(transport, connections[slot], snapshot);
                        resumed = true;
                        logf(LogLevel::Info, "server: P%d reconnected at tick %llu", slot,
                             static_cast<unsigned long long>(authoritativeTick));
                    }
                }
                if (!resumed) {
                    transport.close(*incoming);
                }
            }

            const double messageSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - sessionStart)
                    .count();

            for (int i = 0; i < 2; ++i) {
                if (!active[i]) continue;
                while (true) {
                    auto bytes = transport.receive(connections[i]);
                    if (!bytes.has_value()) break;
                    if (!guards[i].messages.consume(messageSeconds)) {
                        logf(LogLevel::Warning, "server: P%d exceeded message rate", i);
                        markDisconnected(i);
                        break;
                    }
                    Message message;
                    if (!eee::net::decode(*bytes, message)) {
                        markDisconnected(i);
                        break;
                    }
                    if (message.type == MessageType::Disconnect) {
                        markDisconnected(i);
                        break;
                    }
                    if (message.type == MessageType::InputCommand) {
                        const auto& command = std::get<eee::net::InputCommand>(message.payload);
                        const bool validPlayer = command.playerId == i;
                        const bool tickInWindow =
                            command.tick >= authoritativeTick &&
                            command.tick <= authoritativeTick + 30;
                        if (validPlayer && tickInWindow) {
                            pendingInputs[i][command.tick] = command.input;
                        } else {
                            ++guards[i].invalidTicks;
                            if (guards[i].invalidTicks > 20) {
                                logf(LogLevel::Warning,
                                     "server: P%d sent invalid input commands", i);
                                markDisconnected(i);
                                break;
                            }
                        }
                    } else if (message.type == MessageType::Checksum) {
                        const auto& checksum = std::get<eee::net::Checksum>(message.payload);
                        clientChecksums[static_cast<size_t>(i)][checksum.tick] = checksum.hash;
                        compareChecksum(i, checksum.tick, checksum.hash);
                    } else if (message.type == MessageType::ActionRequest) {
                        const auto& action = std::get<eee::net::ActionRequest>(message.payload);
                        if (action.playerId == i && guards[i].actions.consume(messageSeconds)) {
                            const auto playerId =
                                static_cast<eee::core::PlayerId>(action.playerId);
                            if (action.action == 2) {
                                if (authoritative.player(playerId).playerDead) {
                                    authoritative.respawnPlayer(playerId);
                                }
                            } else {
                                authoritative.applyInventoryAction(playerId, action.action,
                                                                    action.index);
                            }
                        }
                    }

                    if (message.type == MessageType::InputCommand ||
                        message.type == MessageType::Checksum ||
                        message.type == MessageType::Ping ||
                        message.type == MessageType::Pong) {
                        const int other = 1 - i;
                        if (active[other]) {
                            transport.send(connections[other], *bytes);
                        }
                    }
                }
                if (active[i] && !transport.isConnected(connections[i])) {
                    markDisconnected(i);
                }
            }

            const auto now = std::chrono::steady_clock::now();
            for (int p = 0; p < 2; ++p) {
                if (!active[p] && grace[p] && now >= graceDeadline[p]) {
                    logf(LogLevel::Warning, "server: P%d reconnect grace expired", p);
                    grace[p] = false;
                    for (int q = 0; q < 2; ++q) {
                        if (active[q]) {
                            Message bye;
                            bye.type = MessageType::Disconnect;
                            bye.payload = eee::net::Disconnect{2};
                            send(transport, connections[q], bye);
                            active[q] = false;
                        }
                    }
                }
            }

            const auto simNow = std::chrono::steady_clock::now();
            const float elapsed =
                std::chrono::duration<float>(simNow - lastSimulation).count();
            lastSimulation = simNow;
            simulationAccumulator += std::min(elapsed, 0.25f);
            int steps = 0;
            while (simulationAccumulator >= 1.0f / 60.0f && steps < 5) {
                if (!trySimulateAuthoritative()) {
                    break;
                }
                simulationAccumulator -= 1.0f / 60.0f;
                ++steps;
            }
            simulationAccumulator =
                std::min(simulationAccumulator, 5.0f * (1.0f / 60.0f));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        logf(authoritativeOk ? LogLevel::Info : LogLevel::Error,
             "authoritative: session %u ended at tick %llu, checksums %s", sessionId,
             static_cast<unsigned long long>(authoritativeTick),
             authoritativeOk ? "validated" : "MISMATCH");
        logf(LogLevel::Info, "authoritative: sent %llu snapshots",
             static_cast<unsigned long long>(snapshotsSent));
        for (ConnectionId id : connections) transport.close(id);
        if (once) {
            return authoritativeOk ? 0 : 1;
        }
    }
}
