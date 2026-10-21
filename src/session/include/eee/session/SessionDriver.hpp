#pragma once

#include "eee/core/Input.hpp"
#include "eee/core/Types.hpp"
#include "eee/logic/RpgGame.hpp"

#include <array>

namespace eee::session {

// Fixed-timestep driver for single-player and (later) lockstep sessions.
//
// Menus/inventory/class select still update once per rendered frame because they
// are UI, not simulation. While playing, the world advances in fixed 60 Hz ticks.
// Edge-triggered input is buffered until a tick consumes it exactly once.
class SessionDriver {
public:
    explicit SessionDriver(core::f32 fixedDelta = 1.0f / 60.0f,
                           core::u32 maxCatchUpTicks = 5);

    // Advances the game. Returns the number of simulation ticks run this call.
    core::u32 advance(logic::RpgGame& game, const core::InputState& input,
                      core::f32 elapsedSeconds);

    void resetAccumulator();

    [[nodiscard]] core::u64 tickCount() const { return tickCount_; }
    [[nodiscard]] core::f32 fixedDelta() const { return fixedDelta_; }
    [[nodiscard]] core::u32 lastTickCount() const { return lastTickCount_; }

private:
    void clearPendingEdges();

    core::f32 fixedDelta_;
    core::f32 accumulator_ = 0.0f;
    core::u32 maxCatchUpTicks_;
    core::u32 lastTickCount_ = 0;
    core::u64 tickCount_ = 0;

    std::array<bool, core::kKeyCount> pendingPressed_{};
    std::array<bool, core::kKeyCount> pendingReleased_{};
    std::array<bool, 3> pendingMousePressed_{};
    core::f32 pendingScroll_ = 0.0f;
    core::Vec2 pendingMouseDelta_{};
};

} // namespace eee::session
