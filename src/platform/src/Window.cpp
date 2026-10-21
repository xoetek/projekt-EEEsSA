#include "eee/platform/Window.hpp"

#if defined(__APPLE__)
std::unique_ptr<eee::platform::IWindow> eeeCreateCocoaWindow(const eee::platform::WindowDesc& desc);
#endif

namespace eee::platform {

std::unique_ptr<IWindow> createWindow(const WindowDesc& desc) {
    if (desc.headless) {
        return createNullWindow(desc);
    }
#if defined(__APPLE__)
    return eeeCreateCocoaWindow(desc);
#else
    return createNullWindow(desc);
#endif
}

} // namespace eee::platform
