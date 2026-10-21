#pragma once

#include "eee/render/IRenderDevice.hpp"

namespace eee::render {

// Headless backend: validates the whole logic -> snapshot -> renderer path
// without a GPU or a window. Used by tests and `--headless` runs.
std::unique_ptr<IRenderDevice> createNullDevice();

} // namespace eee::render
