# Architecture

## Dependency graph

```
                     ┌──────────────┐
                     │  eee::core   │  types, math, input snapshot, log
                     └──────┬───────┘
              ┌─────────────┴─────────────┐
              │                           │
     ┌────────▼────────┐         ┌────────▼────────┐
     │  eee::scene     │         │ eee::platform   │  IWindow: surface + input
     │  (header-only)  │         └────────┬────────┘
     └───┬─────────┬───┘                  │
         │         │                      │
┌────────▼───┐  ┌──▼──────────────────────▼──┐
│ eee::logic │  │ eee::render               │  IRenderDevice, Renderer, DebugText
└────────────┘  └──────────────┬────────────┘
                               │
                     ┌─────────▼──────────┐
                     │ eee::backends      │  null / opengl / metal / vulkan
                     └────────────────────┘
```

CMake expresses exactly this:

- `eee_logic` links `eee_core` + `eee_scene` only.
- `eee_render` links `eee_core` + `eee_scene` + `eee_platform` only.
- `eee_backends` links `eee_render`.
- `eee_demo` links `eee_logic` + `eee_backends`.

No target links both `eee_logic` and `eee_render`, so the compiler enforces the
boundary: a stray `#include <eee/render/...>` in logic fails to resolve, and
vice versa.

## Why a scene contract

`eee::scene::SceneSnapshot` is the narrow waist of the architecture:

```cpp
struct Camera { Vec3 position, target, up; f32 fovY, nearZ, farZ; };
struct MeshInstance { Mat4 transform; Vec4 color; };
struct OverlaySprite { Mat4 transform; Vec4 color; };

struct SceneSnapshot {
    Camera camera;
    Vec4 clearColor;
    Vec3 lightDirection;
    f32 ambient;
    std::vector<MeshInstance> meshes;    // instanced, depth-tested
    std::vector<OverlaySprite> overlay;  // screen-space debug/HUD
};
```

Because it is plain data:

- the simulation can run headless (tests, CI, a server, replays),
- rendering is deterministic given a snapshot,
- a new backend never requires touching gameplay,
- gameplay never pollutes its types with API handles.

`RpgGame` generates a village (ground grid, trees, rocks, huts, campfire) plus
the player, slimes, villagers and floating enemy health bars — roughly 9,900
instanced cubes at spawn. `buildSnapshot()` writes one `MeshInstance` per box
with a `trs3` model matrix; the renderer never knows what a "slime", a "sword"
or a "menu" is.

## Frame lifecycle

```
window.pollEvents(input)              // platform: Cocoa events → InputState
        │
simulation.update(dt, input)          // logic: pure, deterministic
        │
snapshot = simulation.buildSnapshot() // logic → scene data
        │
app.appendHud() / appendMenu()        // draw UI state as overlay quads
        │
device.beginFrame(clearColor)         // Metal: wait semaphore, nextDrawable, clear
        │
mesh pass:
  bindPipeline(mesh)
  setFrameConstants(viewProjection, light, ambient)
  updateBuffer(instance[slot], snapshot.meshes)
  bindVertex/Instance/Index
  drawIndexed(36, meshCount)          // depth test on
        │
overlay pass:
  bindPipeline(overlay)
  updateBuffer(overlayInstance[slot], ortho * sprite)
  drawIndexed(6, overlayCount)        // depth test off
        │
device.endFrame()                     // Metal: commit + completion handler
        │
input.endFrame()
```

The app owns all four objects and decides the order. The renderer never calls
into the simulation, and the simulation never calls into the renderer.

## Frames in flight and asynchronous Metal

`Renderer` keeps **three instance buffers per pass** and rotates them by
`frameIndex % 3`. This is not cosmetic: with Metal it is required for
correctness and throughput.

Metal's `MetalDevice`:

1. `beginFrame` waits on a `dispatch_semaphore` created with value 3, so at most
   three command buffers can be pending. By the time frame *N* starts, frame
   *N-3* has completed and its instance buffer is free.
2. Encodes the render pass into a command buffer (clear color + depth).
3. `endFrame` registers a completion handler that signals the semaphore and
   leaves a `dispatch_group`, calls `presentDrawable`, and returns **without
   waiting**.
4. `waitIdle()` waits on the dispatch group; `Renderer::shutdown()` calls it
   before destroying buffers.

This removes the per-frame CPU↔GPU round trip that an earlier synchronous
`waitUntilCompleted` version paid. The frame rate now reflects the same thing on
Metal and OpenGL: how fast the CPU can submit frames (plus any real GPU cost).

Buffer growth (`ensureMeshCapacity`) also calls `waitIdle()` before reallocating,
because frames in flight may still reference the old buffers.

## Ownership and lifetimes

- `IWindow` outlives the render device that uses its native view.
- `IRenderDevice` outlives `Renderer` (the renderer destructor releases GPU
  resources through the device after draining work).
- Resource handles are `u32` indices into per-backend pools; `0` means invalid.
  Destroying a resource returns a free slot for reuse.
- `Renderer` owns geometry, both pipelines, both triple-buffered instance sets,
  an overlay scratch buffer and the viewport size.
- `World` owns entity records; `SceneSnapshot` is a value produced each frame.

## GPU contract

Two passes, one small fixed layout each:

### Mesh pass

| Resource | Format | Stride |
|---|---|---|
| Vertex buffer | 24 × (`vec3` position + `vec3` normal) | 24 bytes |
| Index buffer | 36 × `u16` | 2 bytes |
| Instance buffer | `mat4` model (64) + `vec4` color (16) | 80 bytes |

### Overlay pass

| Resource | Format | Stride |
|---|---|---|
| Vertex buffer | 4 × `vec2` | 8 bytes |
| Index buffer | 6 × `u16` | 2 bytes |
| Instance buffer | `mat4` MVP (64) + `vec4` color (16) | 80 bytes |

The mesh instance buffer is uploaded directly from `scene::MeshInstance`; a
`static_assert` guarantees the layout. Per-frame data travels through
`FrameConstants`:

```cpp
struct FrameConstants {
    Mat4 viewProjection;
    Vec4 lightDirection;  // xyz used, w is padding
    f32 ambient;
    f32 padding[3];       // sizeof == 96, matches MSL alignment
};
```

The overlay MVP is computed on the CPU as `orthographic(0,w,0,h) * transform`,
so the overlay shader needs no uniforms.

Shader sources come from the device, one program per kind:

- OpenGL: GLSL 4.1 core, explicit attribute locations (mesh: pos 0, normal 1,
  model 2–5, color 6; overlay: pos 0, mvp 1–4, color 5).
- Metal: MSL, `packed_float3` vertices read from `buffer(0)`, `Instance` structs
  from `buffer(1)`, `FrameConstants` from `buffer(2)`.
- Null: no shaders.
- Vulkan: would return GLSL/SPIR-V once implemented.

## Camera, depth and lighting

- Right-handed perspective projection with depth in [-1, 1] (shared convention).
- `lookAt` view matrix in `core::Mat4`.
- Depth: OpenGL requests a 24-bit depth buffer in its pixel format; Metal owns a
  `Depth32Float` texture recreated on resize. Depth test `LESS`, depth writes on
  for meshes, test/writes disabled for the overlay via pipeline state.
- Lighting: one directional light plus ambient, applied per fragment using the
  world-space normal. Enough to make ~10k cubes readable without a material
  system.
- Third-person camera: `RpgGame` places the eye on a sphere around
  `player.position + (0, 1.5, 0)` using yaw/pitch/distance. Mouse right-drag,
  arrow keys and the scroll wheel drive it; movement is computed relative to the
  camera yaw. The renderer only receives the resulting `scene::Camera`.

## Presentation and DPI

- The window reports **framebuffer pixels**, not points. On Retina this is 2×
  the point size.
- The app compares the framebuffer size every frame and calls
  `Renderer::resize()` when it changes.
- The renderer forwards the size to the device (`glViewport`, Metal
  `drawableSize` + depth texture) and uses it for the camera aspect ratio and
  the overlay orthographic matrix.
- Overlay coordinates are framebuffer pixels with the origin at the bottom-left,
  +Y up. `appendDebugText` emits 5x7 pixel quads at a size derived from the
  framebuffer height.

## Resize, vsync and pacing

vsync is part of the device contract (`IRenderDevice::setVSync`):

- OpenGL: `NSOpenGLContextParameterSwapInterval` (1 = on, 0 = off).
- Metal: `CAMetalLayer.displaySyncEnabled`.
- Vulkan (future): present mode `FIFO` vs `IMMEDIATE`/`MAILBOX`, requiring
  swapchain recreation; the backend must handle that inside `setVSync`.

The app starts with vsync on (or `--no-vsync`), and `V` calls `setVSync` at
runtime. The overlay prints the state so uncapping is easy to verify.

`dt` comes from `std::chrono::steady_clock` and is clamped to 100 ms in logic so
a stalled frame cannot teleport the scene. Deterministic simulation should
eventually accumulate time and run fixed substeps.

## Debug overlay

`eee::render::appendDebugText()` draws text without textures or extra shaders:
every lit pixel of a 5x7 glyph becomes a small quad appended to
`snapshot.overlay`. It lives in the render layer and touches only the scene
contract, so no backend code is needed and the logic layer never learns that a
counter exists.

`appendOverlayRect()` adds filled screen-space rectangles (HUD bars, menu panels
and borders), and `appendDebugTextCentered()` centers text using
`debugTextWidth()`. Everything is in the same pixel-space overlay list, drawn in
submission order with depth testing disabled.

## Game state vs. UI drawing

The pause/death menu is **game state**: `RpgGame` owns `paused`/`dead`, the
selected option, menu option strings, stats, dialogue text and the quit request.
It consumes input edges (Escape, W/S, Enter) inside `update()`.

The app owns **presentation**: `appendHud()` and `appendMenu()` read the public
game state and emit overlay quads/text. That keeps the logic layer free of fonts,
pixels and HTML-like layout, while still letting the menu be fully tested
headlessly (toggle, navigation, restart, quit, death screen are all unit-tested).

The FPS overlay is separate: `F1`/`` ` `` toggles it, `--fps` starts it visible,
frame time is smoothed with an exponential moving average, and it shows `FPS`,
frame milliseconds, vsync state and mesh count.

## Adding a backend

Implement `eee::render::IRenderDevice` (see
`src/render/include/eee/render/IRenderDevice.hpp`):

- `initialize` / `shutdown` / `resize` / `setVSync`
- `beginFrame` / `endFrame` / `waitIdle`
- `createBuffer` / `updateBuffer` / `destroyBuffer`
- `createPipeline` / `destroyPipeline`
- `bindPipeline` / `setFrameConstants` / `bindVertexBuffer` /
  `bindInstanceBuffer` / `bindIndexBuffer` / `drawIndexed`
- `meshShaderSources()` and `overlayShaderSources()`
- depth buffer setup and per-pipeline depth state (mesh: on, overlay: off)
- correct handling of 3 in-flight frames (the renderer rotates instance buffers)

Then add the source to `eee_backends` in `CMakeLists.txt`, guard it with a new
option, and register a factory in `src/backends/registry/Backends.cpp`. The null
backend is the smallest complete example; OpenGL and Metal are reference
implementations.

### Vulkan / MoltenVK checklist (macOS)

The placeholder is intentionally inert: `createVulkanDevice()` logs an error and
returns `nullptr`. To make it real:

- Install the Vulkan SDK (LunarG) or MoltenVK. `find_package(Vulkan)` is already
  wired; enable with `-DEEE_BACKEND_VULKAN=ON`.
- Create `VkInstance`, pick queues, and create a surface from
  `platform::NativeWindow` (`VK_EXT_metal_surface` via MoltenVK).
- Build swapchain + per-frame command buffers, sync objects, and a depth image.
- Implement the two documented vertex/instance layouts with vertex input
  bindings, attributes and instancing.
- Upload `FrameConstants` as a uniform buffer (or push constants) for the mesh
  pipeline.
- Compile the GLSL shaders to SPIR-V (offline `glslangValidator` or precompiled).
- Implement `setVSync` as swapchain present-mode selection (recreate the
  swapchain internally).
- Keep 3 frames in flight to match the renderer's rotating instance buffers.

## Conventions

- C++20, no exceptions in engine code, no RTTI assumptions.
- `eee::core` types (`u32`, `f32`, …) for engine-facing APIs.
- Headers are self-contained and include what they use.
- Backend implementations stay in `.cpp`/`.mm`; public headers expose only
  factories and shared interfaces.
- New data crossing the logic/render boundary goes into `eee::scene` as plain
  data, never as a pointer to a logic type.
