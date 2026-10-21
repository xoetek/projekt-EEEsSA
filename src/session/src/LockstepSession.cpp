#include "eee/session/LockstepSession.hpp"

#include "eee/core/Log.hpp"
#include "eee/net/Protocol.hpp"

#include <algorithm>

namespace eee::session {
namespace {

core::usize playerIndex(core::u8 id) { return id < 2 ? id : 0; }

} // namespace

LockstepSession::LockstepSession(net::ITransport& transport, net::ConnectionId connection,
                                 LockstepConfig config)
    : transport_(transport), connection_(connection), config_(config) {
    config_.activePlayers = 2;
    config_.inputDelay = std::max(config_.inputDelay, core::u32{1});
    config_.fixedDelta = config_.fixedDelta > 0.0f ? config_.fixedDelta : 1.0f / 60.0f;
    config_.maxCatchUpTicks = std::max(config_.maxCatchUpTicks, core::u32{1});
    authoritative_ = config_.authoritative;
    if (config_.localPlayer != core::PlayerId::P0 && config_.localPlayer != core::PlayerId::P1) {
        config_.localPlayer = core::PlayerId::P0;
    }

    // Seed the first `inputDelay` ticks with neutral commands so both peers can
    // start immediately. Real sampled input begins at tick inputDelay.
    for (core::u32 t = 0; t < config_.inputDelay; ++t) {
        inputs_[0][t] = core::PlayerInput{};
        inputs_[1][t] = core::PlayerInput{};
    }
    lastScheduledTick_ = config_.inputDelay - 1;
}

core::u32 LockstepSession::advance(logic::RpgGame& game, const core::PlayerInput& localInput,
                                   core::f32 elapsedSeconds) {
    if (desynced_ || disconnected_) {
        return 0;
    }
    if (game.uiMode() != logic::UiMode::Playing) {
        return 0;
    }

    poll();
    sendLocalInput(localInput);

    if (authoritative_ && tickCount_ >= serverTick_) {
        waiting_ = true;
        return 0;
    }

    accumulator_ += std::max(elapsedSeconds, 0.0f);
    core::u32 ticks = 0;
    waiting_ = false;

    while (accumulator_ >= config_.fixedDelta && ticks < config_.maxCatchUpTicks &&
           (!authoritative_ || tickCount_ < serverTick_)) {
        if (!haveCommandsFor(tickCount_)) {
            waiting_ = true;
            break;
        }

        core::InputFrame frame;
        frame.activePlayers = 2;
        frame.players[0] = inputs_[0][tickCount_];
        frame.players[1] = inputs_[1][tickCount_];
        inputs_[0].erase(tickCount_);
        inputs_[1].erase(tickCount_);

        game.updateFrame(config_.fixedDelta, frame);
        ++tickCount_;
        ++ticks;
        accumulator_ -= config_.fixedDelta;

        if (tickCount_ % 30 == 0) {
            const core::u64 hash = game.checksum();
            lastChecksum_ = hash;
            localChecksums_[tickCount_] = hash;
            sendChecksum(tickCount_, hash);
            compareChecksums(tickCount_);
        }

        sendLocalInput(localInput);
        poll();

        if (desynced_ || disconnected_) {
            break;
        }
    }

    const core::f32 maxBacklog =
        config_.fixedDelta * static_cast<core::f32>(config_.maxCatchUpTicks);
    accumulator_ = std::min(accumulator_, maxBacklog);
    return ticks;
}

void LockstepSession::poll() {
    if (disconnected_) return;
    while (true) {
        auto bytes = transport_.receive(connection_);
        if (!bytes.has_value()) break;
        net::Message message;
        if (!net::decode(*bytes, message)) {
            core::logError("lockstep: malformed network message");
            disconnected_ = true;
            return;
        }
        handleMessage(message);
        if (disconnected_) return;
    }
}

void LockstepSession::sendLocalInput(const core::PlayerInput& input) {
    const core::u64 target = tickCount_ + config_.inputDelay;
    if (target <= lastScheduledTick_) return;

    const core::u8 localId = static_cast<core::u8>(config_.localPlayer);
    inputs_[playerIndex(localId)][target] = input;

    net::Message message;
    message.type = net::MessageType::InputCommand;
    message.payload = net::InputCommand{localId, target, input};
    if (!transport_.send(connection_, net::encode(message))) {
        disconnected_ = true;
    }
    lastScheduledTick_ = target;
}

void LockstepSession::sendChecksum(core::u64 tick, core::u64 hash) {
    net::Message message;
    message.type = net::MessageType::Checksum;
    message.payload = net::Checksum{tick, hash};
    if (!transport_.send(connection_, net::encode(message))) {
        disconnected_ = true;
    }
}

void LockstepSession::handleMessage(const net::Message& message) {
    switch (message.type) {
    case net::MessageType::InputCommand: {
        const auto& command = std::get<net::InputCommand>(message.payload);
        if (command.playerId >= 2 || command.tick < tickCount_) {
            return;
        }
        inputs_[playerIndex(command.playerId)][command.tick] = command.input;
        break;
    }
    case net::MessageType::Checksum: {
        const auto& checksum = std::get<net::Checksum>(message.payload);
        remoteChecksums_[checksum.tick] = checksum.hash;
        compareChecksums(checksum.tick);
        break;
    }
    case net::MessageType::ServerTick: {
        const auto& tick = std::get<net::ServerTick>(message.payload);
        if (tick.tick > serverTick_) {
            serverTick_ = tick.tick;
        }
        break;
    }
    case net::MessageType::Disconnect:
        disconnected_ = true;
        break;
    case net::MessageType::Snapshot:
    case net::MessageType::ActionRequest:
    case net::MessageType::ResumeRequest:
        break;
    case net::MessageType::Ping:
    case net::MessageType::Pong:
    case net::MessageType::Hello:
    case net::MessageType::Welcome:
    case net::MessageType::LobbyState:
    case net::MessageType::ClassChoice:
    case net::MessageType::StartRun:
    case net::MessageType::InputAck:
        break;
    }
}

void LockstepSession::compareChecksums(core::u64 tick) {
    if (desynced_) return;
    const auto local = localChecksums_.find(tick);
    const auto remote = remoteChecksums_.find(tick);
    if (local == localChecksums_.end() || remote == remoteChecksums_.end()) return;
    if (local->second != remote->second) {
        desynced_ = true;
        desyncTick_ = tick;
        core::logf(core::LogLevel::Error,
                   "lockstep: desync at tick %llu (local %llx remote %llx)",
                   static_cast<unsigned long long>(tick),
                   static_cast<unsigned long long>(local->second),
                   static_cast<unsigned long long>(remote->second));
    }
}

bool LockstepSession::haveCommandsFor(core::u64 tick) const {
    return inputs_[0].find(tick) != inputs_[0].end() &&
           inputs_[1].find(tick) != inputs_[1].end();
}

} // namespace eee::session
