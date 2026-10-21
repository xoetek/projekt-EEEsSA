# EEEsSA

A C++20 low-poly third-person ARPG with strict layer separation: gameplay logic
knows nothing about graphics, and the renderer knows nothing about gameplay.
Graphics APIs are swappable backends behind one device interface.

```
logic (pure simulation)  ──>  scene (plain data)  <──  renderer (shared)  ──>  backend (Metal / OpenGL / Vulkan / null)
```

## What is in the game

- **Three classes**: Warrior, Archer, Mage — different stats, basic attacks and
  Q/E skills, chosen from a class-select screen.
- **Action controls**: WASD movement relative to a mouse-driven third-person
  camera, left-click attacks, right-click interactions, Q/E skills, Space jump,
  Shift sprint, scroll zoom.
- **PoE-style itemization**: item bases, item levels, Normal/Magic/Rare/Unique
  rarities, prefixes/suffixes with stat rolls, weapons/armour/helmet/boots/rings/
  amulets, health/mana flasks, and skill gems that unlock the E skill.
- **Inventory & equipment** (I): 24-slot bag, keyboard navigation, tooltips with
  affixes, item-vs-equipped comparison (damage/armour/life/mana/crit/move), a
  character stat line, equip requirements, flasks, gem usage, and **X to drop**
  the selected item back into the world.
- **Six visually distinct biomes with bosses** and level bands:
  Verdant Hollow (flowers, village, well, fences), Ashen Wastes (dead trees,
  charred ground), Frozen Reach (ice spikes), Drowned Ruins (sunken pillars),
  Umbral Vale (obelisks), Obsidian Citadel (towers and lava). A larger world
  (113×113 ground tiles) with village props.
- **Unique boss patterns per zone**: root ring + adds, telegraphed charge,
  projectile fans, triple rings, blink burst, and meteor rain.
- **Progression 1–80** with an XP curve, level-scaled enemies, zone bosses, and
  portals that open after each boss.
- **Collision**: circle-vs-obstacle collision against trees, rocks, huts and the
  world bounds, enemy separation, and projectile/obstacle collision.
- **GTA-style camera**: pointer-lock mouse look, shoulder offset, smooth zoom,
  and a collision boom that shortens against trees/rocks/huts and the ground
  instead of phasing through them.
- **Options menu**: pause → GRAPHICS, or F2/G anywhere: MSAA 2x/4x/8x,
  Low–Ultra quality (draw distance, fog), FPS cap 30–240, vsync, invert Y.
- **Better visuals**: Blinn-Phong specular, hemisphere ambient tint, distance
  fog, blob shadows, and backend MSAA resolve.
- **Story**: a six-zone arc about the Sundered Crown, delivered through zone
  banners, boss intros/outros, and a villager per zone.
- **Procedural sound effects**: synthesized at runtime (no asset files) and
  triggered by gameplay events.
- **HUD**: class/level/zone, HP/MP/XP bars, gold/kills, Q/E cooldown boxes, boss
  health bar, interaction prompts, messages, dialogue, zone/boss banners.

## Not implemented in this pass

These are deliberately **not** faked or shipped as stubs:

- **MetalFX frame generation** — needs motion vectors, depth/history inputs and
  a MetalFX frame-interpolator pipeline; none of that exists yet.
- **Hardware raytracing** — needs acceleration structures, ray-tracing shaders
  and a fallback path. Current lighting is rasterised (Lambert + specular +
  fog).
- **Resolution scaling** — needs an offscreen render-resolution target plus an
  upscale/resolve path on both backends.
- **Networked multiplayer** — still a separate milestone; see
  [docs/multiplayer-plan.md](docs/multiplayer-plan.md).

Everything else requested (items/comparison/drop, fire checkpoint, unique boss
patterns, varied biomes, options menu) is implemented.

## Multiplayer status

**LAN 2-player co-op is playable now**, using the deterministic
`LockstepSession` over TCP (`eee_net` + `eee_session`): command queues, 3-tick
input delay, checksums and desync detection. Classes are selected with `--class`;
`RpgGame` runs both players with independent cameras, stats, inventories and
combat. A dedicated relay server (`eee_server`) supports remote/internet
connections over TCP, and a listen-host mode (`--host`) is available for LAN.
Server-authoritative play is the next architecture migration; see
[docs/authoritative-server.md](docs/authoritative-server.md). A full in-app
lobby/ready UI and deeper co-op polish are the next phase; reconnect and
respawn-at-fire are implemented for dedicated authoritative sessions. See
[docs/refactor-plan.md](docs/refactor-plan.md) for the phased plan; Phases 1–6c
are complete, 6b (lobby/polish) is next.

## What is here

| Layer | Target | Contents | May depend on |
|---|---|---|---|
| Core | `eee_core` | types, 2D/3D math, input snapshot, sound ids, logging | — |
| Scene | `eee_scene` | `SceneSnapshot`, `Camera`, `MeshInstance`, `OverlaySprite` | core |
| Logic | `eee_logic` | `RpgGame`, `Classes`, `Items`, `Zones`, `World`, `ISimulation` | core, scene |
| Platform | `eee_platform` | window/input, procedural audio, Cocoa + null implementations | core |
| Render | `eee_render` | `IRenderDevice`, shared `Renderer`, `DebugText`, registry | core, scene, platform |
| Backends | `eee_backends` | `null`, `opengl`, `metal`, `vulkan` placeholder | render |
| App | `eee_demo` | frame loop, HUD/inventory/menu drawing, audio playback, wiring | logic, backends |

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
> `~/.pi/agent/bin` (on `PATH` inside pi sessions). If a fresh terminal does not
> find it: `export PATH="$HOME/.pi/agent/bin:$PATH"`.

## Run

```sh
./build/eee_demo                              # class select, then play
./build/eee_demo --backend=metal --class=mage # skip class select
./build/eee_demo --fps --no-vsync
./build/eee_demo --aa=4 --quality=high --fps-cap=120
./build/eee_demo --backend=null --headless --frames=120 --class=archer
```

Graphics can also be changed in-game with **F2** (or **G**): MSAA off/2x/4x/8x,
quality Low–Ultra, FPS cap 30–240, vsync and invert-Y. Settings are runtime-only.

Controls:

| Input | Action |
|---|---|
| **WASD** | Move relative to camera; **Shift** sprints |
| **Mouse** | GTA-style mouse look (pointer captured while playing); wheel zooms |
| **Arrows** | Adjust camera yaw/pitch (keyboard alternative) |
| **Left click** | Basic attack (hold to repeat) |
| **Right click** | Interact: pick up loot, talk, enter portal |
| **Q / E** | Class skills (E unlocked by a skill gem) |
| **Space** | Jump (enemy projectiles can be jumped over) |
| **I** | Inventory / equipment |
| **X** (in inventory) | Drop the selected item |
| **ESC** | Pause menu (Resume/Restart/Change Class/Quit) |
| **F1 / `** | FPS overlay |
| **F2 or G** | Graphics settings |
| **V** | Vsync |

Inventory: arrows/WASD select, Enter equips/uses, I or ESC closes.
Menu: W/S select, Enter confirms, ESC resumes.

`--backend=null --headless` runs the complete logic → snapshot → renderer path
without a window or GPU, which is how the smoke tests in CTest work.

CMake options:

| Option | Default | Meaning |
|---|---|---|
| `EEE_BUILD_TESTS` | `ON` | build unit tests + CTest entries |
| `EEE_BACKEND_OPENGL` | `ON` | build the OpenGL 4.1 backend (macOS) |
| `EEE_BACKEND_METAL` | `ON` | build the Metal backend (macOS) |
| `EEE_BACKEND_VULKAN` | `OFF` | compile/register the Vulkan placeholder |

Graphics settings are runtime-only (not persisted). MSAA is clamped to what the
device reports; OpenGL uses a multisampled FBO, Metal resolves MSAA into the
drawable.

## Layer rules (the point of the project)

1. **Logic produces data, not draw calls.** `RpgGame::buildSnapshot()` returns a
   `scene::SceneSnapshot` — camera, clear color, light, instanced cube
   transforms, and an optional screen-space overlay. No device handles, no API
   enums, no window.
2. **Rendering consumes data, not gameplay.** The shared `Renderer` turns a
   snapshot into two instanced passes through `IRenderDevice`. It never includes
   a logic header.
3. **Menus and inventory are game state.** `RpgGame` owns pause/death/victory,
   selection, stats, dialogue, item/equipment state and the quit request; the app
   only draws that state as overlay quads/text.
4. **Sound is event data.** Logic emits `core::SoundId` values
   (`takeSoundEvents()`); the platform layer synthesizes and plays them. No audio
   API appears in gameplay code.
5. **Backends are replaceable.** A backend implements buffer/pipeline/draw
   primitives, depth setup and its own shader language (GLSL, MSL, SPIR-V…).
6. **The host wires the layers.** `src/app/main.cpp` is the only place that
   knows about a window, a device, a simulation, a renderer, HUD layout and audio.

## Scene and renderer

- **Mesh pass:** unit cube (24 vertices, 36 indices), instanced ~10k times with
  a model matrix and color. Vertex shader applies `viewProjection * model` and
  passes a world normal; fragment shader does one directional light + ambient.
- **Overlay pass:** screen-space quads for HUD/menu/text, orthographic
  projection, depth test disabled, drawn after the world.
- **Three instance buffers** per pass rotate per frame, so up to three frames can
  be in flight. Required by the asynchronous Metal backend; also avoids driver
  stalls on OpenGL.

See [docs/architecture.md](docs/architecture.md) for the full frame lifecycle,
GPU layouts, item/skill/zone modules and backend notes.

## Tests

Four unit suites plus end-to-end smoke tests (10 CTest entries):

`eee_tests` links only `eee_logic` — it cannot accidentally pull in a graphics
header. Covers:

- Vec3/matrix math, perspective/look-at/orthographic projection
- `World` entity lifetime and id recycling, input edge detection
- class selection, scene scale, camera target/distance
- GTA-style mouse-look camera, shoulder pivot, movement, sprint, jump, timestep clamp
- camera collision against huts and the floor
- collision against huts, world bounds
- pause menu (5 options incl. graphics), restart, change class, quit, frozen movement
- two-player simulation: P1 movement/camera, nearest-player targeting, P1 kill credit,
  per-player loadouts, 2-player determinism
- item generation invariants (rarity/mod counts/names/skill gems), stat totals
- inventory equip, item comparison, item dropping, flasks, skill-gem unlock
- Q/E skills, mana costs, cooldowns, locked E
- combat, kills, XP, gold, guaranteed boss loot, right-click pickup
- six-zone boss progression, unique boss pattern execution, boss intro banner,
  victory screen
- fire rest/checkpoint and respawn-at-fire, NPC dialogue
- level cap at 80
- deterministic simulation for identical `(dt, input)` sequences

`eee_session_tests` verifies fixed 60 Hz ticks, edge-triggered input consumed once,
render-cadence determinism, menu behavior, catch-up limiting, and deterministic
lockstep networking: a 600-tick two-peer TCP session, a delayed-transport run,
and injected-desync detection at checksum ticks.

`eee_net_tests` covers the new `eee_net` target: every protocol message round-trip,
malformed/oversized/trailing frame rejection, and a real TCP loopback exchange
using `TcpTransport`.

`eee_render_tests` uses a recording `MockDevice` (no GPU/window) and checks:
renderer init, mesh/overlay draws, frame constants, triple-buffer rotation,
capacity growth, shutdown resource cleanup, and debug-text/rect/centering.

CTest then runs the null backend on the class-select screen, a started Warrior
gameplay run, the FPS overlay, vsync-off, and two-process LAN co-op smoke tests
(host/client and dedicated server over real TCP). Network tests return CTest's
skip code when the environment denies `bind()`; their non-network assertions
still run.

To run the full networking/co-op suite from a normal shell (outside the pi
sandbox), use:

```sh
./scripts/run-network-tests.sh
```

It configures/builds, runs `net`, `session`, host/join and dedicated-server
smokes, runs full CTest, and writes logs to `build/test-logs/`. Override
`BUILD_DIR` or `PORT_BASE` if needed. It exits non-zero only on a real failure;
sandbox-blocked network tests are reported as skipped.

Reconnect test: in a dedicated-server client press **F8** to drop the TCP
connection; the client shows CONNECTION LOST, then Enter reconnects within the
server's 20-second grace window.

### LAN co-op (first playable slice)

```sh
# terminal 1
./build/eee_demo --host=27015 --class=warrior

# terminal 2
./build/eee_demo --join=127.0.0.1:27015 --class=archer
```

Both peers run the deterministic 60 Hz lockstep simulation; each sees its own
camera and HUD plus the ally HP line. ESC opens a local non-pausing menu
(Resume/Leave). Classes currently come from `--class`; a full lobby/ready UI and
co-op inventory are the next polish phase. Single-player is unchanged.

### Dedicated server

```sh
# host/VM with a reachable address
./build/eee_server --port=27015

# each player, anywhere on the internet (TCP reachability required)
./build/eee_demo --join=YOUR_SERVER_HOST:27015 --class=warrior
./build/eee_demo --join=YOUR_SERVER_HOST:27015 --class=archer
```

`eee_server` is a **dedicated relay with a shadow authoritative simulation**:
it assigns player IDs, exchanges class choices, sends `StartRun`, relays
input/checksum frames, and runs the same 60 Hz `RpgGame` from both clients'
inputs. Every 30 ticks it compares checksums with clients and aborts on
divergence. After each tick it sends an authoritative `ServerTick` and a
`Snapshot` of players/enemies/projectiles/loot. Dedicated-server clients apply
those snapshots and **do not simulate the world**; they send inputs only and
validate their applied state against the server checksum. The server also applies
`ActionRequest` equip/use/drop messages authoritatively. Inventory/equipment are
replicated in snapshots; the co-op inventory UI (`I`, arrows/WASD select,
Enter equip/use, X drop) operates while the simulation keeps running. Add
`--once` to exit after one session (used by CTest).

> **A1–A6 are implemented**: the server owns/validates the simulation, releases
> authoritative ticks, replicates world snapshots, owns inventory/action results,
> uses deterministic per-zone worlds, and supports respawn plus a 20-second
> reconnect grace with resume tokens. Dedicated clients stop simulating, render
> server state, send action requests, predict local movement, and interpolate
> remote entities. See
> [docs/authoritative-server.md](docs/authoritative-server.md). Do not describe
> the current relay as a fully authoritative server.

Internet requirements and caveats:

- The server needs a public IP/DNS name and an open TCP port (firewall/security
  group + port forwarding on a home router).
- The protocol is **plaintext and unauthenticated**. The server now applies
  rate limits, input tick-window checks and player-id validation, but those are
  robustness guards, not authentication. Use it only with trusted players, or put
  it behind a firewall/VPN/TLS terminator. Do not expose it openly as-is.
- NAT traversal, matchmaking, relay authentication and authoritative server
  simulation are not implemented yet.

## Status

- Working: null backend, full single-player ARPG loop, class select, items,
  inventory, skills, zones/bosses, level 1–80, collision, jump, procedural SFX,
  logic/render separation, unit tests, mock-device renderer tests, CTest.
- Metal/OpenGL compile; GPU execution needs a desktop with a working GPU (the
  development environment used here has none).
- Multiplayer: not implemented (see above).

