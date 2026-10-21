#pragma once

#include "eee/render/IRenderDevice.hpp"

namespace eee::render {

// Vulkan backend placeholder. Build with -DEEE_BACKEND_VULKAN=ON after
// installing the Vulkan SDK (or MoltenVK on macOS) to enable it.
std::unique_ptr<IRenderDevice> createVulkanDevice();

} // namespace eee::render
