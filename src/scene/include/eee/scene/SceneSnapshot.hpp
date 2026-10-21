#pragma once

#include "eee/core/Math.hpp"

#include <vector>

namespace eee::scene {

// ---------------------------------------------------------------------------
// The presentation contract.
//
// This header is the ONLY shared vocabulary between the logic layer and the
// rendering layer:
//
//     logic  --->  scene (this file)  <---  render
//
// It contains plain data. No logic types, no graphics API types, no ownership.
// The logic layer produces a SceneSnapshot; any renderer consumes it. This is
// what lets the simulation run headless (tests, server, replay) and lets the
// renderer be swapped without touching gameplay code.
// ---------------------------------------------------------------------------

struct Camera {
    core::Vec3 position{0.0f, 8.0f, 30.0f};
    core::Vec3 target{0.0f, 0.0f, 0.0f};
    core::Vec3 up{0.0f, 1.0f, 0.0f};
    core::f32 fovYRadians = 1.0471975512f; // 60 degrees
    core::f32 nearZ = 0.5f;
    core::f32 farZ = 400.0f;
};

// One instanced cube. `transform` is a full model matrix (translation, rotation,
// scale); the renderer applies the camera and lighting.
struct MeshInstance {
    core::Mat4 transform = core::Mat4::identity();
    core::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

// Screen-space quad for debug UI. Coordinates are in viewport pixels with the
// origin at the bottom-left, +Y up; the renderer applies an orthographic
// projection and disables depth testing for these.
struct OverlaySprite {
    core::Mat4 transform = core::Mat4::identity();
    core::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct SceneSnapshot {
    Camera camera{};
    core::Vec4 clearColor{0.02f, 0.03f, 0.06f, 1.0f};

    // Direction the light travels (from light towards the scene).
    core::Vec3 lightDirection{-0.45f, -1.0f, -0.35f};
    core::f32 ambient = 0.16f;

    // Drawn first, depth-tested, instanced.
    std::vector<MeshInstance> meshes;
    // Drawn last, unlit, no depth test (debug text, HUD).
    std::vector<OverlaySprite> overlay;
};

} // namespace eee::scene
