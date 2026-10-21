#pragma once

#include "eee/core/NetSnapshot.hpp"
#include "eee/core/PlayerInput.hpp"
#include "eee/core/Types.hpp"
#include "eee/logic/RpgGame.hpp"
#include "eee/net/Protocol.hpp"
#include "eee/net/Transport.hpp"

#include <deque>

namespace eee::session {

// Thin authoritative client: sends inputs, applies server snapshots, never
// advances the simulation. Server ticks are authoritative.
//
// A5a adds purely local movement prediction: inputs are remembered until the
// server acknowledges them, replayed on top of the latest authoritative local
// player state, and reconciled smoothly against incoming snapshots.
//
// A5b adds visual interpolation for remote entities between the last two
// snapshots. The local player remains predicted; only other players, enemies,
// projectiles and loot are interpolated.
class AuthoritativeClient {
public:
    struct SentInput {
        core::u64 tick = 0;
        core::PlayerInput input{};
    };

    struct PredictedPlayer {
        core::Vec3 position{};
        core::f32 yaw = 0.0f;
        core::f32 jumpHeight = 0.0f;
        bool valid = false;
    };

    // Prediction errors above this distance snap instead of blending.
    static constexpr core::f32 kSnapThreshold = 1.5f;

    AuthoritativeClient(net::ITransport& transport, net::ConnectionId connection,
                        core::PlayerId localPlayer, core::u32 inputDelay = 3);

    void sendInput(const core::PlayerInput& input);
    void sendAction(core::u8 action, core::u16 index);
    core::u32 poll(logic::RpgGame& game);

    // Rebuilds the local player from the latest authoritative state, replays
    // unacknowledged inputs at a fixed 60 Hz and extrapolates the newest input
    // by the real frame dt. Only the local player's position/yaw/jump are
    // touched; health, mana, enemies and loot stay server-authoritative.
    void updatePrediction(logic::RpgGame& game, const core::PlayerInput& latestInput,
                          core::f32 dt);

    // Advances the render interpolation clock and applies visual interpolation
    // for remote entities between the last two authoritative snapshots. The
    // local player is left untouched so prediction remains authoritative for
    // movement. If only one snapshot has been received, the current state is
    // used without interpolation.
    void updateInterpolation(logic::RpgGame& game, core::f32 dt);

    [[nodiscard]] core::u64 serverTick() const { return serverTick_; }
    [[nodiscard]] core::u64 lastSnapshotTick() const { return lastSnapshotTick_; }
    [[nodiscard]] bool disconnected() const { return disconnected_; }
    [[nodiscard]] core::u32 snapshotsApplied() const { return snapshotsApplied_; }

    [[nodiscard]] const PredictedPlayer& predictedPlayer() const { return predictedPlayer_; }
    [[nodiscard]] bool hasAuthoritativeLocal() const { return hasAuthoritativeLocal_; }
    [[nodiscard]] core::usize pendingInputCount() const { return sentInputs_.size(); }
    // Distance between the previous prediction and the snapshot that replaced it.
    [[nodiscard]] core::f32 lastPredictionError() const { return lastPredictionError_; }
    [[nodiscard]] bool lastPredictionSnapped() const { return lastPredictionSnapped_; }

    [[nodiscard]] core::u64 currentSnapshotTick() const {
        return hasCurrentSnapshot_ ? currentSnapshot_.tick : 0;
    }
    [[nodiscard]] core::f32 interpolationAlpha() const { return interpolationAlpha_; }
    [[nodiscard]] bool hasPreviousSnapshot() const { return hasPreviousSnapshot_; }

private:
    void handleMessage(logic::RpgGame& game, const net::Message& message);

    net::ITransport& transport_;
    net::ConnectionId connection_;
    core::PlayerId localPlayer_;
    core::u32 inputDelay_;
    core::u64 serverTick_ = 0;
    core::u64 lastSentTick_ = 0;
    core::u64 lastSnapshotTick_ = 0;
    core::u32 snapshotsApplied_ = 0;
    bool disconnected_ = false;

    std::deque<SentInput> sentInputs_;
    core::PlayerNetState authoritativeLocal_{};
    PredictedPlayer predictedPlayer_{};
    core::Vec3 smoothingOffset_{};
    core::f32 lastPredictionError_ = 0.0f;
    bool hasAuthoritativeLocal_ = false;
    bool lastPredictionSnapped_ = false;

    core::WorldSnapshot previousSnapshot_{};
    core::WorldSnapshot currentSnapshot_{};
    bool hasPreviousSnapshot_ = false;
    bool hasCurrentSnapshot_ = false;
    core::f32 interpolationClock_ = 0.0f;
    core::f32 interpolationAlpha_ = 0.0f;
};

} // namespace eee::session
