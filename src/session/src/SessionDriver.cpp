#include "eee/session/SessionDriver.hpp"

#include <algorithm>

namespace eee::session {

SessionDriver::SessionDriver(core::f32 fixedDelta, core::u32 maxCatchUpTicks)
    : fixedDelta_(fixedDelta > 0.0f ? fixedDelta : 1.0f / 60.0f),
      maxCatchUpTicks_(std::max(maxCatchUpTicks, core::u32{1})) {}

void SessionDriver::clearPendingEdges() {
    pendingPressed_.fill(false);
    pendingReleased_.fill(false);
    pendingMousePressed_.fill(false);
    pendingScroll_ = 0.0f;
    pendingMouseDelta_ = {};
}

void SessionDriver::resetAccumulator() {
    accumulator_ = 0.0f;
    lastTickCount_ = 0;
}

core::u32 SessionDriver::advance(logic::RpgGame& game, const core::InputState& input,
                                 core::f32 elapsedSeconds) {
    // UI modes are not simulated on the fixed clock.
    if (game.uiMode() != logic::UiMode::Playing) {
        accumulator_ = 0.0f;
        lastTickCount_ = 0;
        clearPendingEdges();
        game.update(std::clamp(elapsedSeconds, 0.0f, 0.1f), input);
        return 0;
    }

    // Buffer edge-triggered input and relative mouse motion until a tick runs.
    for (core::usize i = 0; i < core::kKeyCount; ++i) {
        pendingPressed_[i] = pendingPressed_[i] || input.pressed[i];
        pendingReleased_[i] = pendingReleased_[i] || input.released[i];
    }
    for (core::usize i = 0; i < 3; ++i) {
        pendingMousePressed_[i] = pendingMousePressed_[i] || input.mousePressed[i];
    }
    pendingScroll_ += input.scrollDelta;
    pendingMouseDelta_ += input.mouseDelta;

    accumulator_ += std::max(elapsedSeconds, 0.0f);
    core::u32 ticks = 0;

    while (accumulator_ >= fixedDelta_ && ticks < maxCatchUpTicks_) {
        core::InputState step = input;
        step.pressed = pendingPressed_;
        step.released = pendingReleased_;
        step.mousePressed = pendingMousePressed_;
        step.scrollDelta = pendingScroll_;
        step.mouseDelta = pendingMouseDelta_;
        clearPendingEdges();

        game.update(fixedDelta_, step);
        accumulator_ -= fixedDelta_;
        ++ticks;
        ++tickCount_;

        if (game.uiMode() != logic::UiMode::Playing) {
            // A menu/transition consumed this tick; drop the backlog.
            accumulator_ = 0.0f;
            clearPendingEdges();
            break;
        }
    }

    // Prevent a spiral of death after a long stall.
    const core::f32 maxBacklog = fixedDelta_ * static_cast<core::f32>(maxCatchUpTicks_);
    accumulator_ = std::min(accumulator_, maxBacklog);

    lastTickCount_ = ticks;
    return ticks;
}

} // namespace eee::session
