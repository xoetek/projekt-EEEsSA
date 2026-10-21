#pragma once

#include "eee/core/PlayerInput.hpp"
#include "eee/core/Types.hpp"
#include "eee/logic/RpgGame.hpp"
#include "eee/net/Protocol.hpp"
#include "eee/net/Transport.hpp"

#include <array>
#include <map>

namespace eee::session {

struct LockstepConfig {
    core::PlayerId localPlayer = core::PlayerId::P0;
    core::u8 activePlayers = 2;
    core::u32 inputDelay = 3;
    core::f32 fixedDelta = 1.0f / 60.0f;
    core::u32 maxCatchUpTicks = 5;
    bool authoritative = false;
};

// Symmetric deterministic lockstep peer.
//
// Both peers run the same RpgGame at a fixed 60 Hz. Each sends its own
// PlayerInput as an InputCommand for tick `current + inputDelay`; a tick only
// runs when both players' commands are present. Checksums are exchanged every
// 30 ticks and a mismatch freezes the session.
class LockstepSession {
public:
    LockstepSession(net::ITransport& transport, net::ConnectionId connection,
                    LockstepConfig config = {});

    // Polls the socket, sends the local input, and advances as many fixed ticks
    // as possible. Returns the number of ticks simulated by this call.
    core::u32 advance(logic::RpgGame& game, const core::PlayerInput& localInput,
                      core::f32 elapsedSeconds);

    void poll();

    [[nodiscard]] core::u64 tickCount() const { return tickCount_; }
    [[nodiscard]] bool desynced() const { return desynced_; }
    [[nodiscard]] core::u64 desyncTick() const { return desyncTick_; }
    [[nodiscard]] bool disconnected() const { return disconnected_; }
    [[nodiscard]] bool waitingForInput() const { return waiting_; }
    [[nodiscard]] core::u64 lastChecksum() const { return lastChecksum_; }

private:
    void sendLocalInput(const core::PlayerInput& input);
    void sendChecksum(core::u64 tick, core::u64 hash);
    void handleMessage(const net::Message& message);
    void compareChecksums(core::u64 tick);
    [[nodiscard]] bool haveCommandsFor(core::u64 tick) const;

    net::ITransport& transport_;
    net::ConnectionId connection_;
    LockstepConfig config_;

    std::array<std::map<core::u64, core::PlayerInput>, 2> inputs_;
    std::map<core::u64, core::u64> localChecksums_;
    std::map<core::u64, core::u64> remoteChecksums_;

    core::f32 accumulator_ = 0.0f;
    core::u64 tickCount_ = 0;
    core::u64 lastScheduledTick_ = 0;
    core::u64 serverTick_ = 0;
    core::u64 desyncTick_ = 0;
    core::u64 lastChecksum_ = 0;

    bool desynced_ = false;
    bool disconnected_ = false;
    bool waiting_ = false;
    bool authoritative_ = false;
};

} // namespace eee::session
