#pragma once

#include "eee/render/IRenderDevice.hpp"

namespace eee::render {

// OpenGL 4.1 core backend (the highest core profile macOS exposes).
std::unique_ptr<IRenderDevice> createOpenGLDevice();

} // namespace eee::render
