#pragma once

#include "eee/core/Math.hpp"
#include "eee/core/Types.hpp"

#include <array>

namespace eee::core {

// Key identifiers are backend/platform independent. Printable keys use their
// ASCII uppercase value; everything else lives above 0x100.
enum class Key : u16 {
    Unknown = 0,
    Space = ' ',
    Backquote = '`',
    Escape = 0x100,
    Enter,
    Tab,
    Backspace,
    Left,
    Right,
    Up,
    Down,
    LeftShift,
    RightShift,
    F1,
    Count
};

constexpr usize kKeyCount = 512;

// A snapshot of input state. The platform layer fills it; only the logic layer
// consumes it. Nothing here knows about windows, graphics APIs or OS events.
struct InputState {
    std::array<bool, kKeyCount> down{};
    std::array<bool, kKeyCount> pressed{};
    std::array<bool, kKeyCount> released{};

    Vec2 mousePosition{};
    std::array<bool, 3> mouseDown{};
    std::array<bool, 3> mousePressed{};
    f32 scrollDelta = 0.0f;

    [[nodiscard]] bool isDown(Key key) const { return down[static_cast<usize>(key)]; }
    [[nodiscard]] bool wasPressed(Key key) const { return pressed[static_cast<usize>(key)]; }
    [[nodiscard]] bool wasReleased(Key key) const { return released[static_cast<usize>(key)]; }

    [[nodiscard]] bool isDown(char letter) const {
        return down[static_cast<u16>(letter >= 'a' && letter <= 'z' ? letter - 32 : letter)];
    }
    [[nodiscard]] bool wasPressed(char letter) const {
        return pressed[static_cast<u16>(letter >= 'a' && letter <= 'z' ? letter - 32 : letter)];
    }

    // Called by the platform layer when an OS event changes a key state.
    void setKey(Key key, bool isDown) {
        const auto index = static_cast<usize>(key);
        if (index >= kKeyCount || down[index] == isDown) {
            return;
        }
        down[index] = isDown;
        if (isDown) {
            pressed[index] = true;
        } else {
            released[index] = true;
        }
    }

    void setMouseButton(u32 button, bool isDown) {
        if (button >= mouseDown.size() || mouseDown[button] == isDown) {
            return;
        }
        mouseDown[button] = isDown;
        if (isDown) {
            mousePressed[button] = true;
        }
    }

    // Edge-triggered state is cleared at the end of every frame so that the
    // logic layer sees each press exactly once.
    void endFrame() {
        pressed.fill(false);
        released.fill(false);
        mousePressed.fill(false);
        scrollDelta = 0.0f;
    }
};

} // namespace eee::core
