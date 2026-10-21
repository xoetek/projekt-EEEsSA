#include "eee/backends/VulkanDevice.hpp"

#include "eee/core/Log.hpp"

namespace eee::render {

// Placeholder. The registration hook and build option exist so that adding a
// real implementation is a local change: see docs/architecture.md for the
// Vulkan/MoltenVK checklist.
std::unique_ptr<IRenderDevice> createVulkanDevice() {
    core::logError("vulkan: backend is a placeholder and has no implementation yet "
                   "(see docs/architecture.md)");
    return nullptr;
}

} // namespace eee::render
