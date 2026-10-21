# EEEsSA

A C++20 low-poly third-person RPG with strict layer separation: gameplay logic
knows nothing about graphics, and the renderer knows nothing about gameplay.
Graphics APIs are swappable backends behind one device interface.

```
logic (pure simulation)  ──>  scene (plain data)  <──  renderer (shared)  ──>  backend (Metal / OpenGL / Vulkan / null)
```

## The RPG

- Third-person camera that follows the player (mouse right-drag, arrow keys,
  scroll to zoom).
- A village square with huts, a campfire, trees, rocks and a grass grid
  (~9,900 instanced cubes at spawn).
- A player character built from boxes, with walking/attack animation.
- 8 slimes that chase and attack, show floating health bars, and respawn.
- 3 villagers with dialogue (`E`).
- Combat (`Space` / left click), XP, levels, gold, kills, health regen and death.
- HUD: level/gold/kills, health and XP bars, slime count, controls hint,
  dialogue box.
- **ESC opens a pause menu** (Resume / Restart / Quit) and a death menu
  (Restart / Quit) with live stats.

## What is here

| Layer | Target | Contents | May depend on |
|---|---|---|---|
| Core | `eee_core` | types, 2D/3D math, input snapshot, logging | — |
| Scene | `eee_scene` | `SceneSnapshot`, `Camera`, `MeshInstance`, `OverlaySprite` | core |
| Logic | `eee_logic` | `RpgGame`, `World`, `ISimulation` | core, scene |
| Platform | `eee_platform` | window + input pump, Cocoa and null implementations | core |
| Render | `eee_render` | `IRenderDevice`, shared `Renderer`, `DebugText`, registry | core, scene, platform |
| Backends | `eee_backends` | `null`, `opengl`, `metal`, `vulkan` placeholder | render |
| App | `eee_demo` | argument parsing, frame loop, HUD/menu drawing, wiring | logic, backends |

The build system enforces the important part: **`eee_logic` does not link
`eee_render`/`eee_platform`/`eee_backends`, and `eee_render` does not link
`eee_logic`.** They only share the data contract in `src/scene`.

## Build

Requirements: CMake ≥ 3.24, Xcode Command Line Tools (clang, ObjC++).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

> CMake 3.31.6 is installed at `~/.pi/agent/tools/cmake-3.31.6` with symlinks in
> `~/.pi/agent/bin` (already on `PATH` inside pi sessions). If a fresh terminal
> does not find it, add `export PATH="$HOME/.pi/agent/bin:$PATH"`.
> Alternatives: `brew install cmake` (Homebrew is not installed) or
> `pip3 install --user cmake`.

## Run

```sh
./build/eee_demo                       # first available backend (metal, then opengl)
./build/eee_demo --backend=metal
./build/eee_demo --fps                 # start with the FPS overlay visible
./build/eee_demo --no-vsync            # uncap the frame rate (V toggles at runtime)
./build/eee_demo --list-backends
./build/eee_demo --backend=null --headless --frames=300   # CI / smoke test
```

Controls:

- **WASD** — move (relative to the camera), **Shift** — sprint
- **Right mouse drag** or **arrow keys** — orbit the camera
- **Mouse wheel** — zoom
- **Space** or **left click** — attack
- **E** — talk to a nearby villager
- **ESC** — open/close the pause menu
- **F1** or **`` ` ``** — toggle the FPS overlay
- **V** — toggle vsync

In the menu: **W/S** or arrows select, **Enter/Space** confirms, **ESC** resumes.
**Quit** closes the application.

`--backend=null --headless` runs the complete logic → snapshot → renderer path
without a window or GPU, which is how the smoke tests in CTest work.

CMake options:

| Option | Default | Meaning |
|---|---|---|
| `EEE_BUILD_TESTS` | `ON` | build `eee_tests`/`eee_render_tests` + CTest entries |
| `EEE_BACKEND_OPENGL` | `ON` | build the OpenGL 4.1 backend (macOS) |
| `EEE_BACKEND_METAL` | `ON` | build the Metal backend (macOS) |
| `EEE_BACKEND_VULKAN` | `OFF` | compile/register the Vulkan placeholder |

## Layer rules (the point of the project)

1. **Logic produces data, not draw calls.** `RpgGame::buildSnapshot()` returns a
   `scene::SceneSnapshot` — camera, clear color, light, instanced cube
   transforms, and an optional screen-space overlay. No device handles, no API
   enums, no window.
2. **Rendering consumes data, not gameplay.** The shared `Renderer` turns a
   snapshot into two instanced passes through `IRenderDevice`. It never includes
   a logic header.
3. **Menus are game state.** `RpgGame` owns pause/death state, selection, stats
   and dialogue; the app only draws that state as overlay quads/text. The logic
   layer still has no idea what a "menu pixel" is.
4. **Backends are replaceable.** A backend implements buffer/pipeline/draw
   primitives, depth setup and its own shader language (GLSL, MSL, SPIR-V…).
   Adding one does not touch gameplay or the shared renderer.
5. **The host wires the layers.** `src/app/main.cpp` is the only place that
   knows about a window, a device, a simulation, a renderer and HUD layout.

## Scene and renderer

- **Mesh pass:** unit cube (24 vertices, 36 indices), instanced ~10k times with
  a model matrix and color. Vertex shader applies `viewProjection * model` and
  passes a world normal; fragment shader does one directional light + ambient.
- **Overlay pass:** screen-space quads for HUD/menu/text, orthographic
  projection, depth test disabled, drawn after the world.
- **Three instance buffers** per pass rotate per frame, so up to three frames can
  be in flight. This is required by the asynchronous Metal backend and also
  avoids driver stalls on OpenGL.

See [docs/architecture.md](docs/architecture.md) for the full frame lifecycle,
GPU layouts and backend notes.

## Tests

Two unit suites plus end-to-end smoke tests (5 CTest entries total):

`eee_tests` links only `eee_logic` — it cannot accidentally pull in a graphics
header. Covers:

- Vec3/matrix math: dot/cross/normalize, `trs3`, `lookAt`, perspective near/far
  mapping, orthographic overlay mapping
- entity creation, destruction and id recycling in `World`
- input edge detection
- third-person camera target/distance and scene scale (~9.9k meshes)
- WASD movement, sprinting, timestep clamping
- camera orbit via arrows and right-drag mouse
- pause menu toggle/navigation, restart, quit, movement frozen while paused
- combat: damage, kills, gold, XP, level-up and full heal
- enemy damage, death menu (Restart/Quit)
- NPC dialogue activation and timeout
- deterministic simulation for identical `(dt, input)` sequences

`eee_render_tests` uses a recording `MockDevice` (no GPU/window) and checks:

- renderer initialization: 10 buffers, 2 pipelines, correct shader selection
- mesh pass: one indexed instanced draw, 24/80-byte strides, frame constants,
  uploaded instance bytes match the snapshot
- overlay pass: second draw, 6 indices, depth test disabled, orthographic MVP
- triple-buffered instance handles rotating `0,1,2,0`
- empty frames, capacity growth, uninitialized renderer
- shutdown drains the device and destroys every resource
- debug-text glyph counts, centering, screen rects, unknown characters

CTest then runs the null backend with default settings, with `--fps`, and with
`--no-vsync`.

## Status

- Working: null backend, RPG scene/gameplay/UI, logic/render separation, unit
  tests, mock-device renderer tests, CTest.
- Metal: asynchronous command buffers (3 in flight, semaphore + completion
  handler), depth texture, two pipelines (mesh/overlay). Compiles on macOS.
- OpenGL 4.1: depth buffer, instanced cubes, uniform frame constants. Compiles.
- The Metal/OpenGL paths could not be executed in the environment where this was
  built (no GPU); run them on a normal desktop session to verify visually.
- Vulkan is a placeholder: the file and registration hook exist, but creation
  logs an error until a real implementation is added.
