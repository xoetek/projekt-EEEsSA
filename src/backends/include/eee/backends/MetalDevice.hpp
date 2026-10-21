#pragma once

#include "eee/render/IRenderDevice.hpp"

namespace eee::render {

// Metal backend (macOS). Uses a CAMetalLayer attached to the native view.
std::unique_ptr<IRenderDevice> createMetalDevice();

} // namespace eee::render
