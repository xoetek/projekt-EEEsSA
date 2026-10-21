#pragma once

#include "eee/core/Math.hpp"
#include "eee/core/Types.hpp"

#include <array>

namespace eee::core {

enum class PlayerId : u8 { P0 = 0, P1 = 1, Count = 2 };

// Simulation-facing input for one player. Platform input is mapped into this;
// networked co-op will exchange this fixed-size value instead of raw events.
struct PlayerInput {
    f32 moveX = 0.0f;
    f32 moveZ = 0.0f;
    f32 aimYaw = 0.0f;
    f32 cameraYaw = 0.0f;
    f32 cameraPitch = 0.0f;
    bool jump = false;
    bool attack = false;
    bool interact = false;
    bool skillQ = false;
    bool skillE = false;
    bool sprint = false;
};

struct InputFrame {
    std::array<PlayerInput, 2> players{};
    u8 activePlayers = 1;
};

} // namespace eee::core
