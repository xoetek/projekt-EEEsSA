#include "eee/render/DeviceRegistry.hpp"

#include "eee/backends/MetalDevice.hpp"
#include "eee/backends/NullDevice.hpp"
#include "eee/backends/OpenGLDevice.hpp"
#include "eee/backends/VulkanDevice.hpp"

namespace eee::render {

void registerBuiltinDevices() {
    DeviceRegistry& registry = DeviceRegistry::instance();

    // Always available: dependency free.
    registry.registerDevice("null", &createNullDevice);

#if EEE_HAS_OPENGL
    registry.registerDevice("opengl", &createOpenGLDevice);
#endif

#if EEE_HAS_METAL
    registry.registerDevice("metal", &createMetalDevice);
#endif

#if EEE_HAS_VULKAN
    registry.registerDevice("vulkan", &createVulkanDevice);
#endif
}

} // namespace eee::render
