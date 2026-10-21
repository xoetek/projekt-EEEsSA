#include "eee/core/Log.hpp"
#include "eee/logic/RpgGame.hpp"
#include "eee/net/Protocol.hpp"
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
        "  --port=<n>   TCP listen port (default 27015)\n"
        "  --once       Exit after one two-player session\n"
        "  --help       Show this message\n"
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
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else if (arg == "--once") {
            once = true;
        } else if (arg.starts_with("--port=")) {
            port = std::atoi(arg.substr(7).c_str());
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
    logf(LogLevel::Info, "server: listening on TCP port %u", transport.localPort());

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

        const std::array<ConnectionId, 2> connections{*first, *second};
        const eee::core::u32 sessionId = ++sessionCounter;

        std::array<eee::core::u8, 2> classes{0, 0};
        bool handshakeOk = true;
        for (int i = 0; i < 2; ++i) {
            Message welcome;
            welcome.type = MessageType::Welcome;
            welcome.payload = eee::net::Welcome{static_cast<eee::core::u8>(i), sessionId, 60};
            if (!send(transport, connections[i], welcome)) {
                handshakeOk = false;
                break;
            }
            auto choice = waitForMessage(transport, connections[i], 15000);
            if (!choice.has_value() || choice->type != MessageType::ClassChoice) {
                handshakeOk = false;
                break;
            }
            classes[i] = std::get<eee::net::ClassChoice>(choice->payload).classId;
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

        auto simulateAuthoritative = [&]() {
            while (true) {
                const bool neutralWarmup = authoritativeTick < 3;
                bool ready = neutralWarmup;
                if (!ready) {
                    ready = pendingInputs[0].find(authoritativeTick) != pendingInputs[0].end() &&
                            pendingInputs[1].find(authoritativeTick) != pendingInputs[1].end();
                }
                if (!ready) break;

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
                for (ConnectionId id : connections) {
                    send(transport, id, tickMessage);
                }

                eee::core::WorldSnapshot snapshot =
                    authoritative.captureNetworkSnapshot(authoritativeTick);
                Message snapshotMessage;
                snapshotMessage.type = MessageType::Snapshot;
                snapshotMessage.payload = std::move(snapshot);
                for (ConnectionId id : connections) {
                    if (send(transport, id, snapshotMessage)) {
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
            }
        };

        simulateAuthoritative();

        std::array<bool, 2> active{true, true};
        while (active[0] || active[1]) {
            for (int i = 0; i < 2; ++i) {
                if (!active[i]) continue;
                while (true) {
                    auto bytes = transport.receive(connections[i]);
                    if (!bytes.has_value()) break;
                    Message message;
                    if (!eee::net::decode(*bytes, message)) {
                        active[i] = false;
                        break;
                    }
                    const int other = 1 - i;
                    if (message.type == MessageType::Disconnect) {
                        active[i] = false;
                        break;
                    }
                    if (message.type == MessageType::InputCommand) {
                        const auto& command = std::get<eee::net::InputCommand>(message.payload);
                        if (command.playerId < 2 && command.tick >= authoritativeTick) {
                            pendingInputs[command.playerId][command.tick] = command.input;
                        }
                    } else if (message.type == MessageType::Checksum) {
                        const auto& checksum = std::get<eee::net::Checksum>(message.payload);
                        clientChecksums[static_cast<size_t>(i)][checksum.tick] = checksum.hash;
                        compareChecksum(i, checksum.tick, checksum.hash);
                    } else if (message.type == MessageType::ActionRequest) {
                        const auto& action = std::get<eee::net::ActionRequest>(message.payload);
                        if (action.playerId == i) {
                            authoritative.applyInventoryAction(
                                static_cast<eee::core::PlayerId>(action.playerId), action.action,
                                action.index);
                        }
                    }

                    if (message.type == MessageType::InputCommand ||
                        message.type == MessageType::Checksum ||
                        message.type == MessageType::Ping ||
                        message.type == MessageType::Pong) {
                        if (active[other]) {
                            transport.send(connections[other], *bytes);
                        }
                        if (message.type == MessageType::InputCommand ||
                            message.type == MessageType::Checksum) {
                            simulateAuthoritative();
                        }
                    }
                }
                if (active[i] && !transport.isConnected(connections[i])) {
                    active[i] = false;
                }
            }

            if ((active[0] && !active[1]) || (active[1] && !active[0])) {
                const int remaining = active[0] ? 0 : 1;
                Message bye;
                bye.type = MessageType::Disconnect;
                bye.payload = eee::net::Disconnect{1};
                send(transport, connections[remaining], bye);
                active[remaining] = false;
            }
            simulateAuthoritative();
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
