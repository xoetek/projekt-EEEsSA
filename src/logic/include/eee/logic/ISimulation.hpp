#pragma once

#include "eee/core/Input.hpp"
#include "eee/core/Types.hpp"
#include "eee/scene/SceneSnapshot.hpp"

#include <memory>

namespace eee::logic {

// The seam between the game and the host application. A simulation knows about
// raw input and produces a presentation snapshot. It has no access to a render
// device, a window, or a frame timing source beyond the dt it is given.
class ISimulation {
public:
    virtual ~ISimulation() = default;

    // Advance the simulation by `dt` seconds with the given input snapshot.
    // Must be deterministic for a given sequence of (dt, input) pairs.
    virtual void update(core::f32 dt, const core::InputState& input) = 0;

    // Produce an immutable description of what should be drawn this frame.
    [[nodiscard]] virtual scene::SceneSnapshot buildSnapshot() const = 0;
};

} // namespace eee::logic
