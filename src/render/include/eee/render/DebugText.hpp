#pragma once

#include "eee/core/Math.hpp"
#include "eee/scene/SceneSnapshot.hpp"

#include <string_view>

namespace eee::render {

// Debug-only text overlay built from the existing instanced-quad overlay pass.
//
// Each lit pixel of a 5x7 glyph becomes one quad appended to
// `snapshot.overlay`, so no textures, font atlases or extra shaders are required
// and every backend supports it for free. Coordinates are overlay pixels with
// the origin at the bottom-left, +Y up (the renderer projects them with an
// orthographic matrix).
//
// This lives in the render layer (not logic): it is presentation-only and uses
// nothing but the scene contract.
void appendDebugText(scene::SceneSnapshot& snapshot, std::string_view text,
                     core::Vec2 topLeft, core::f32 pixelSize, core::Vec4 color);

// Appends one screen-space rectangle (pixels, origin bottom-left, +Y up).
void appendOverlayRect(scene::SceneSnapshot& snapshot, core::Vec2 center, core::Vec2 size,
                       core::Vec4 color);

// Width in pixels of the widest line of `text` at `pixelSize`.
[[nodiscard]] core::f32 debugTextWidth(std::string_view text, core::f32 pixelSize);

// Like appendDebugText, but centred horizontally around `centerX`.
void appendDebugTextCentered(scene::SceneSnapshot& snapshot, std::string_view text,
                             core::f32 centerX, core::f32 topY, core::f32 pixelSize,
                             core::Vec4 color);

} // namespace eee::render
