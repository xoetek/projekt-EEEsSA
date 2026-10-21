#pragma once

#include "eee/core/Input.hpp"
#include "eee/core/Types.hpp"

#include <memory>

namespace eee::platform {

// Opaque native handles. Backends reinterpret these; the rest of the engine
// never has to know about Cocoa, Win32, X11, Wayland...
struct NativeWindow {
    void* window = nullptr;
    void* view = nullptr;
};

struct WindowDesc {
    const char* title = "EEEsSA";
    core::i32 width = 1280;
    core::i32 height = 720;
    bool headless = false;
};

// Platform window + input pump. Deliberately graphics-API agnostic: a window is
// a native surface plus input events, not a GL/Metal context.
class IWindow {
public:
    virtual ~IWindow() = default;

    // Pumps OS events into `input`. Returns false once the window is closing.
    virtual bool pollEvents(core::InputState& input) = 0;

    virtual void getFramebufferSize(core::i32& width, core::i32& height) const = 0;

    [[nodiscard]] virtual NativeWindow native() const = 0;

    // Relative mouse mode (pointer lock). While captured the cursor is hidden
    // and motion is delivered through InputState::mouseDelta.
    virtual void setMouseCapture(bool captured) = 0;

    virtual void requestClose() = 0;
    [[nodiscard]] virtual bool shouldClose() const = 0;
    virtual void setTitle(const char* title) = 0;
};

// Creates the platform-appropriate window. `desc.headless` always yields a
// window-less implementation suitable for tests, servers and CI.
std::unique_ptr<IWindow> createWindow(const WindowDesc& desc);
std::unique_ptr<IWindow> createNullWindow(const WindowDesc& desc);

} // namespace eee::platform
