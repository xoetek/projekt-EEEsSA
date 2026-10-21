#include "eee/session/AuthoritativeClient.hpp"

#include <algorithm>
#include <cmath>

namespace eee::session {
namespace {

constexpr core::f32 kPredictionStep = 1.0f / 60.0f;
constexpr core::f32 kMaxPredictionFrame = 0.1f;
constexpr core::f32 kSmoothingDecay = 10.0f;
constexpr core::usize kMaxReplayedInputs = 128;
constexpr core::usize kMaxSentInputs = 256;
constexpr core::f32 kInterpolationStep = 1.0f / 60.0f;

} // namespace

AuthoritativeClient::AuthoritativeClient(net::ITransport& transport,
                                         net::ConnectionId connection,
                                         core::PlayerId localPlayer,
                                         core::u32 inputDelay)
    : transport_(transport),
      connection_(connection),
      localPlayer_(localPlayer),
      inputDelay_(std::max(inputDelay, core::u32{1})) {}

void AuthoritativeClient::sendInput(const core::PlayerInput& input) {
    if (disconnected_) return;

    // Fill the server's input pipeline, not just one future tick. The server
    // may already be at tick N (for example after the neutral warm-up) and will
    // need N, N+1, ... as soon as it simulates. Sending only N+delay left it
    // stalled forever.
    core::u64 first = std::max(std::max(nextInputTick_, serverTick_),
                               static_cast<core::u64>(inputDelay_));
    const core::u64 last = serverTick_ + inputDelay_;
    if (last < first) {
        return;
    }

    for (core::u64 tick = first; tick <= last; ++tick) {
        net::Message message;
        message.type = net::MessageType::InputCommand;
        message.payload =
            net::InputCommand{static_cast<core::u8>(localPlayer_), tick, input};
        if (!transport_.send(connection_, net::encode(message))) {
            disconnected_ = true;
            return;
        }
        if (sentInputs_.size() >= kMaxSentInputs) {
            sentInputs_.pop_front();
        }
        sentInputs_.push_back(SentInput{tick, input});
    }
    nextInputTick_ = last + 1;
}

void AuthoritativeClient::sendAction(core::u8 action, core::u16 index) {
    if (disconnected_) return;
    net::Message message;
    message.type = net::MessageType::ActionRequest;
    message.payload = net::ActionRequest{static_cast<core::u8>(localPlayer_), action, index};
    if (!transport_.send(connection_, net::encode(message))) {
        disconnected_ = true;
    }
}

core::u32 AuthoritativeClient::poll(logic::RpgGame& game) {
    if (disconnected_) return 0;
    if (!transport_.isConnected(connection_)) {
        disconnected_ = true;
        return 0;
    }
    core::u32 applied = 0;
    while (true) {
        auto bytes = transport_.receive(connection_);
        if (!bytes.has_value()) break;
        net::Message message;
        if (!net::decode(*bytes, message)) {
            disconnected_ = true;
            return applied;
        }
        handleMessage(game, message);
        if (message.type == net::MessageType::Snapshot) {
            ++applied;
        }
        if (disconnected_) break;
    }
    if (!transport_.isConnected(connection_)) {
        disconnected_ = true;
    }
    return applied;
}

void AuthoritativeClient::updatePrediction(logic::RpgGame& game,
                                           const core::PlayerInput& /*latestInput*/,
                                           core::f32 dt) {
    if (!hasAuthoritativeLocal_ || disconnected_) {
        return;
    }

    dt = core::clamp(dt, 0.0f, kMaxPredictionFrame);

    // Every frame restarts from the server position, so prediction never
    // accumulates drift and an authoritative correction is applied immediately.
    logic::PlayerState& local = game.player(localPlayer_);
    local.position = authoritativeLocal_.position;
    local.yaw = authoritativeLocal_.yaw;
    local.jumpHeight = authoritativeLocal_.jumpHeight;
    local.grounded = local.jumpHeight <= 1e-4f;
    local.verticalVelocity =
        local.grounded
            ? 0.0f
            : std::sqrt(2.0f * logic::RpgGame::kGravity * local.jumpHeight);
    // Start walk animation from the authoritative phase, then advance it for
    // the replayed ticks. Resetting it to zero every frame made the legs and
    // arms snap back to their rest pose.
    local.walkTime = authoritativeLocal_.walkTime;

    // Replay inputs the server has not processed yet at the fixed tick rate.
    // The newest input is already in this list when it was sent; do not apply
    // it a second time for the render frame (that would double movement speed).
    core::usize replayed = 0;
    for (const SentInput& sent : sentInputs_) {
        if (replayed >= kMaxReplayedInputs) break;
        game.predictLocalMovement(localPlayer_, sent.input, kPredictionStep);
        ++replayed;
    }

    // `base` is the simulation position, without the render-only smoothing
    // offset. Keep it separate: feeding the smoothed position back into the
    // next prediction double-counted the correction and caused visible jitter.
    const core::Vec3 base = local.position;
    predictedPlayer_.position = base;
    predictedPlayer_.yaw = local.yaw;
    predictedPlayer_.jumpHeight = local.jumpHeight;
    predictedPlayer_.valid = true;

    if (hasCorrectionAnchor_) {
        smoothingOffset_ = correctionAnchor_ - base;
        hasCorrectionAnchor_ = false;
        if (smoothingOffset_.lengthSquared() >
            kSnapThreshold * kSnapThreshold) {
            smoothingOffset_ = {};
        }
    }
    // Blend away the correction from the last snapshot instead of popping.
    if (smoothingOffset_.lengthSquared() > 1e-8f) {
        const core::f32 decay = std::exp(-kSmoothingDecay * dt);
        smoothingOffset_ = smoothingOffset_ * decay;
        if (smoothingOffset_.lengthSquared() < 1e-6f) {
            smoothingOffset_ = {};
        }
    }
    local.position = base + smoothingOffset_;
}

void AuthoritativeClient::updateInterpolation(logic::RpgGame& game, core::f32 dt) {
    if (!hasCurrentSnapshot_ || disconnected_) {
        return;
    }

    // One snapshot interval per alpha unit. New snapshots reset the clock, so
    // visual remote entities trail the authoritative state by up to one tick.
    interpolationClock_ += core::clamp(dt, 0.0f, kMaxPredictionFrame);
    interpolationAlpha_ =
        core::clamp(interpolationClock_ / kInterpolationStep, 0.0f, 1.0f);

    if (hasPreviousSnapshot_) {
        game.applyRemoteInterpolation(localPlayer_, previousSnapshot_, currentSnapshot_,
                                      interpolationAlpha_);
    } else {
        // First snapshot: show the authoritative state immediately.
        game.applyRemoteInterpolation(localPlayer_, currentSnapshot_, currentSnapshot_, 0.0f);
    }
}

void AuthoritativeClient::handleMessage(logic::RpgGame& game, const net::Message& message) {
    switch (message.type) {
    case net::MessageType::Snapshot: {
        const auto& snapshot = std::get<core::WorldSnapshot>(message.payload);

        // Keep the last two snapshots for render interpolation. The latest
        // snapshot is still applied immediately below for authoritative checks.
        if (hasCurrentSnapshot_) {
            previousSnapshot_ = currentSnapshot_;
            hasPreviousSnapshot_ = true;
        }
        currentSnapshot_ = snapshot;
        hasCurrentSnapshot_ = true;
        interpolationClock_ = 0.0f;
        interpolationAlpha_ = 0.0f;

        const core::usize localIndex = static_cast<core::usize>(localPlayer_);
        const core::PlayerNetState& authoritative = snapshot.players[localIndex];

        // Reconcile the previous prediction with the new authoritative state.
        if (predictedPlayer_.valid) {
            const core::Vec3 correction = predictedPlayer_.position - authoritative.position;
            lastPredictionError_ = correction.length();
            if (lastPredictionError_ > kSnapThreshold) {
                smoothingOffset_ = {};
                hasCorrectionAnchor_ = false;
                lastPredictionSnapped_ = true;
            } else {
                // Remember the last rendered position. updatePrediction() will
                // measure the new predicted base and derive the visual offset
                // from it, so the correction is not applied twice.
                correctionAnchor_ = predictedPlayer_.position + smoothingOffset_;
                hasCorrectionAnchor_ = true;
                lastPredictionSnapped_ = false;
            }
        } else {
            smoothingOffset_ = {};
            hasCorrectionAnchor_ = false;
            lastPredictionError_ = 0.0f;
            lastPredictionSnapped_ = false;
        }

        game.applyNetworkSnapshot(snapshot);
        serverTick_ = std::max(serverTick_, snapshot.tick);
        lastSnapshotTick_ = snapshot.tick;
        ++snapshotsApplied_;

        // Inputs at or before this tick are already reflected in the snapshot.
        while (!sentInputs_.empty() && sentInputs_.front().tick <= snapshot.tick) {
            sentInputs_.pop_front();
        }

        authoritativeLocal_ = authoritative;
        hasAuthoritativeLocal_ = true;
        predictedPlayer_.position = authoritative.position;
        predictedPlayer_.yaw = authoritative.yaw;
        predictedPlayer_.jumpHeight = authoritative.jumpHeight;
        predictedPlayer_.valid = true;

        // Validate client-side application against the authoritative hash.
        net::Message checksum;
        checksum.type = net::MessageType::Checksum;
        checksum.payload = net::Checksum{snapshot.tick, game.checksum()};
        if (!transport_.send(connection_, net::encode(checksum))) {
            disconnected_ = true;
        }
        break;
    }
    case net::MessageType::ServerTick: {
        const auto& tick = std::get<net::ServerTick>(message.payload);
        serverTick_ = std::max(serverTick_, tick.tick);
        break;
    }
    case net::MessageType::Disconnect:
        disconnected_ = true;
        break;
    default:
        break;
    }
}

} // namespace eee::session
