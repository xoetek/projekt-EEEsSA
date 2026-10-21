# Refactor & Multiplayer Architecture Plan

Status: **planning only** — no multiplayer code has been written yet.
Scope owner: whoever implements the next milestone.
Companion docs: [architecture.md](architecture.md), [multiplayer-plan.md](multiplayer-plan.md).

This is the working plan for turning the current deterministic single-player ARPG
into a **2-player LAN co-op game**, plus the order in which we will do it. Each
phase is meant to be shippable with the game still playable in single-player.

---

## Progress log

- **Phase 1 — Player array refactor: ✅ complete.**
  - `core::PlayerId`, `PlayerInput`, `InputFrame` types added.
  - `RpgGame` now owns `std::array<PlayerState, 2>`, with `activePlayers = 1`
    and local viewer `P0` by default.
  - `Loadout` holds inventory, equipment, totals, level/XP/gold/kills and the
    secondary-skill unlock per player.
  - Per-player accessors: `player(id)`, `inventory(id)`, `equipped(id, slot)`,
    `totals(id)`, `addItem(id, item)`.
  - `buildSnapshot()` now delegates to `buildSnapshot(viewer)`; the viewer
    parameter is accepted but still uses the single existing camera.
  - Single-player reset clears both player slots.
  - Existing suite green, plus a new isolation test proving P1 loadout changes
    do not affect P0.
  - **Completed in Phase 2:** `InputFrame` is now the gameplay input path and
    cameras are per player. Menus remain local/P0.
- **Phase 2 — Two-player simulation: ✅ complete.**
  - `updateFrame(dt, InputFrame)` drives 1–2 players; `update(InputState)` maps
    the local player and delegates to it.
  - Per-player cameras with independent follow/collision; `buildSnapshot(viewer)`
    now uses the requested player's camera.
  - Both player meshes are rendered; per-player stats/inventory/equipment/XP.
  - Enemies and boss patterns target the nearest living player.
  - Projectiles carry their owner; player damage/XP/kill credit goes to the
    attacker; enemy projectiles hit either player.
  - Player-vs-enemy separation applies to all active players.
  - Loot pickup/rest/interaction runs for the acting player (P1 included).
  - New tests: P1 movement/camera isolation, nearest-player targeting, P1 combat
    and kill credit, 2-player determinism.
  - **Deferred:** app-side local co-op input/UI, co-op difficulty scaling,
    P1 death/respawn polish. These land with the co-op UI/session phase.
- **Phase 3 — Fixed tick + SessionDriver: ✅ complete.**
  - New `eee_session` target with `SessionDriver`:
    - fixed 60 Hz simulation ticks,
    - max 5 catch-up ticks per render frame,
    - edge-triggered input buffered and consumed exactly once,
    - relative mouse motion/scroll consumed once per render frame,
    - menus/inventory/class select update once per rendered frame and do not
      advance simulation ticks.
  - `app/main.cpp` now drives the game through `SessionDriver`; single-player
    behaviour is unchanged.
  - New `eee_session_tests`: tick rate, edge consumption, render-cadence
    determinism, menu non-ticking, catch-up cap.
  - Camera interpolation was not needed for this pass.
- **Phase 4 — `eee_net` transport/protocol: ✅ complete.**
  - New `eee_net` target (depends on `eee_core` only).
  - Versioned binary protocol: Hello, Welcome, LobbyState, ClassChoice, StartRun,
    InputCommand, InputAck, Checksum, Ping/Pong, Disconnect.
  - Little-endian serialization, fixed PlayerInput layout, trailing-byte and
    malformed/oversized frame rejection.
  - `ITransport` + POSIX `TcpTransport`: listen/accept/connect, length-prefixed
    framing, non-blocking receive, bounded blocking send, SO_NOSIGPIPE.
  - `eee_net_tests`: every message round-trips; truncated/bad-version/bad-type/
    trailing frames rejected; real TCP loopback exchange passes.
  - **Completed in Phase 5:** command queues and checksums. Lobby/game wiring is
    Phase 6; UDP is Phase 9.
- **Phase 5 — Lockstep session: ✅ complete.**
  - New `LockstepSession` in `eee_session`:
    - symmetric peers, per-player command queues keyed by tick,
    - 3-tick input delay with neutral warm-up commands,
    - fixed 60 Hz ticks, catch-up cap,
    - checksum exchange every 30 ticks; mismatch sets `desynced`/tick,
    - explicit waiting-for-input and disconnect states.
  - Loopback tests with real TCP:
    - 600-tick two-player session with equal state and checksums,
    - 12 ms delayed transport still progresses deterministically,
    - injected health divergence detected at the next checksum.
  - **Deferred:** lobby handshake/start run (Phase 6), app integration, reconnect,
    UDP/loss handling. TCP is reliable, so packet-loss injection is not meaningful
    before the UDP transport.
- **Phase 6a — Playable co-op slice: ✅ complete.**
  - `--host[=port]` / `--join=host[:port]` in the app.
  - Host waits for the peer, exchanges `ClassChoice`, sends `StartRun` with both
    classes; client starts from the message.
  - Both peers call `RpgGame::beginCoop`, set their local viewer, and run
    `LockstepSession` over real TCP.
  - Local camera input, local HUD/skills, remote ally HP line.
  - ESC opens a **local non-pausing co-op menu** (Resume/Leave); neutral input is
    sent while it is open so the peer never stalls.
  - Two-process CTest (`coop_smoke`) runs host+client headless and verifies both
    exit cleanly.
  - **Deferred to 6b:** full lobby/ready UI (classes currently come from
    `--class`), co-op inventory, death/respawn/revive flow, reconnect.
- **Phase 6c — Dedicated relay server: ✅ complete.**
  - New headless `eee_server` executable (no window/GPU/simulation).
  - Listens on TCP, accepts two clients, sends `Welcome` with assigned player
    IDs, collects `ClassChoice`, sends `StartRun`, and relays `InputCommand`/
    `Checksum` between peers.
  - `--once` runs a single session for CI; default loops accepting sessions.
  - Two-client CTest (`dedicated_smoke`) passes with real TCP.
  - **Limits:** relay, not authoritative; plaintext and unauthenticated; internet
    play needs a reachable public IP/DNS and TCP port forwarding/firewall.
    NAT traversal, matchmaking, authentication, and UDP are later work.
- **A1 — Authoritative shadow simulation: ✅ complete.**
  - `eee_server` owns a headless `RpgGame`, consumes both clients' inputs, and
    advances fixed 60 Hz ticks with neutral input-delay warm-up.
  - Server checksums every 30 ticks are compared with both clients' checksums;
    `--once` exits non-zero on mismatch.
  - `dedicated_smoke` now requires the server log `checksums validated`.
  - Clients still simulate/relay; A2/A3 (server streaming + snapshots) are next.
- **A2 — Authoritative tick stream: ✅ complete (tick authority).**
  - `ServerTick` message; dedicated server releases each simulated tick to both
    clients.
  - Clients gate simulation on the server's tick and cannot run ahead.
  - Server checksums still validate clients every 30 ticks.
  - Clients still compute state locally; A3 snapshots/corrections are next.
- **A3 — Authoritative snapshots: ✅ complete (A3b).**
  - `core::WorldSnapshot` + protocol `Snapshot` message.
  - Server captures/sends the authoritative world every tick.
  - Authoritative clients apply snapshots, send inputs only and do not simulate.
  - Client-applied-state checksums are validated by the server.
  - Deferred: delta compression, prediction/correction, zone handshake (A5/A6).
- **A4 — Server-owned HUD/inventory/actions: ✅ complete.**
  - Snapshot carries cooldowns/combat stats/secondary unlock.
  - Server applies `ActionRequest` equip/use/drop; protocol + logic tests added.
  - Full inventory/equipment replication with modifiers, co-op inventory UI and action requests are complete.
- **A5 — Prediction/interpolation: ✅ complete.**
  - Local player movement prediction/reconciliation in `AuthoritativeClient`.
  - Pending input replay at 60 Hz, snap vs smooth corrections, local-only scope.
  - Remote interpolation for other player/enemies/projectiles/loot is complete.
- **A6 — Zones/reconnect: ✅ complete.**
  - Deterministic per-zone static worlds, server-driven zone changes, respawn-at-fire and 20s reconnect grace with resume tokens.
- **A7 — Security/robustness: next.**

---

## 1. Goals

### MVP (must ship)

- Host/join over LAN for **2 players**.
- Both players share one world: enemies, bosses, loot, zone progression, story.
- Each player has their own class, stats, inventory, equipment, skills and camera.
- Host and client both run the same deterministic simulation; the network carries
  **input commands**, not world snapshots.
- Headless integration test: host + client simulate 600 ticks with identical
  checksums.
- Single-player remains fully playable and continues to pass its tests.

### Non-goals for MVP

- Internet matchmaking, NAT traversal, dedicated servers, 4+ players.
- Anti-cheat, server-authoritative loot rolls, rollback netcode.
- Late join into a running run.
- Voice/chat, friends list, accounts.
- Seamless host migration.

These are explicitly later phases.

---

## 2. Current state

What exists today:

- `RpgGame` is deterministic for a fixed sequence of `(dt, InputState)`.
- One `PlayerState`, one inventory, one equipment array, one camera.
- `ISimulation::update(dt, InputState)` is called from the app loop.
- Variable frame delta is clamped to 100 ms; there is no fixed-tick loop.
- Menus/inventory pause the whole simulation.
- Loot generation uses a game-owned RNG; both peers must see identical results.
- `SceneSnapshot` already separates simulation from rendering.
- `eee_logic` has no socket/platform dependency.

What blocks multiplayer:

| Blocker | Why |
|---|---|
| Single `player_` | combat, skills, loot, XP, cameras and UI all assume one player |
| `InputState` directly | no notion of “input for player 0/1”; platform input is global |
| Local pause | pausing freezes the other player's world |
| Variable `dt` | lockstep requires a fixed simulation step |
| App-owned loop | no reusable tick/session driver |
| No transport/protocol | no way to exchange commands or handshake |
| No checksums | cannot detect desync |

---

## 3. Target architecture

```
                  ┌──────────────────────────────┐
                  │            App               │
                  │  single / host / join modes  │
                  └───────┬──────────────┬───────┘
                          │              │
             ┌────────────▼─────┐   ┌────▼──────────────┐
             │  SessionDriver   │   │  LocalInputMapper │
             │ fixed 60 Hz tick │   │ keyboard/mouse    │
             └───────┬──────────┘   └────┬──────────────┘
                     │                   │ PlayerInput
             ┌───────▼───────────────────▼───────┐
             │             eee_logic             │
             │   RpgGame with players[0..1]      │
             │   shared world, per-player loadout│
             └───────┬───────────────────────────┘
                     │ SceneSnapshot(viewerId)
             ┌───────▼──────────┐
             │  eee_render/app  │
             │ local camera/HUD │
             └──────────────────┘

Host/client session:

  Host:  inputs[0], inputs[1] ──► RpgGame.tick ──► checksum
            ▲                            │
            │ TCP InputCommand           │ broadcast inputs
            │                            ▼
  Client:  inputs[0], inputs[1] ◄── Session ◄── RpgGame.tick
            │
            └─ local input for its own player
```

Dependency rule to preserve:

```
eee_core  <── eee_scene <── eee_logic
   ▲                          ▲
   └── eee_net <── eee_session ┘   (new targets, app-facing only)
```

`eee_logic` must never include sockets or `eee_net`.

---

## 4. Key decisions

| # | Decision | Choice | Rationale |
|---|---|---|---|
| D1 | Sync model for MVP | Deterministic lockstep | Already deterministic; avoids replicating inventories/world state; LAN latency is low |
| D2 | Tick rate | 60 Hz fixed | Matches current feel; 16.6 ms budget |
| D3 | Input delay | 3 ticks (50 ms) | Covers LAN jitter; tunable constant |
| D4 | Transport | TCP first | Ordered/reliable handshake and commands; simple correctness; UDP later |
| D5 | Max players | 2 | Limits UI/camera/authority complexity |
| D6 | Pause | Disabled globally in co-op; menu is a local overlay | A pause must not freeze the other client |
| D7 | Menus | Class select in lobby; inventory/overlay local and non-blocking | Keeps simulation running; ARPG-like |
| D8 | Late join | Not in MVP | Lockstep requires replay; defer |
| D9 | Desync | Checksum every 30 ticks → halt + report | Better than silently diverging |
| D10 | Authority | Host coordinates ticks/lobby; simulation is peer-lockstep | No item/snapshot authority needed in MVP |
| D11 | UDP | Phase 9, behind `ITransport` | TCP proves protocol first |

Trade-offs we accept in the MVP:

- Both peers must run the same build and the same platform (float determinism).
- Input latency includes the command delay.
- No reconnect mid-run; a disconnect ends the session.
- Menus do not pause; the player can be attacked while in inventory.
- Mods/cheats are not prevented.

---

## 5. Simulation refactor

### 5.1 Core input types

Add to `eee/core` (platform-independent):

```cpp
enum class PlayerId : u8 { P0 = 0, P1 = 1, Count = 2 };

struct PlayerInput {
    f32 moveX = 0.0f;        // -1..1, camera-relative
    f32 moveZ = 0.0f;        // -1..1
    f32 aimYaw = 0.0f;       // camera/character facing for attacks
    f32 cameraYaw = 0.0f;    // needed for movement direction + prediction
    f32 cameraPitch = 0.0f;
    bool jump = false;       // edge
    bool attack = false;     // held (basic attack repeats)
    bool interact = false;   // edge (RMB)
    bool skillQ = false;     // edge
    bool skillE = false;     // edge
    bool sprint = false;     // held
    bool inventory = false;  // local overlay toggle (not sent in co-op?)
};

struct InputFrame {
    std::array<PlayerInput, 2> players{};
    u8 activePlayers = 1;
};
```

`InputState` remains the platform snapshot. The app maps it to `PlayerInput`
for the local player. Network commands carry `PlayerInput` (fixed-size, easy to
serialize).

### 5.2 Player state and loadout

```cpp
struct Loadout {
    std::vector<Item> inventory;
    std::array<Item, kItemSlotCount> equipment{};
    bool secondaryUnlocked = false;
    i32 gold = 0;
    i32 xp = 0;
    i32 level = 1;
    i32 kills = 0;
    f32 xpToNext = 0;
};

struct PlayerState {
    PlayerId id;
    ClassId classId;
    Vec3 position, yaw, health, mana, ...;
    Loadout loadout;
    CameraState camera;
    Vec3 respawnPoint;
    bool dead;
    f32 respawnTimer;
};
```

Rules:

- `RpgGame` owns `std::array<PlayerState, 2> players_` and `u8 activePlayers_`.
- All helpers take `PlayerId`/`PlayerState&`: movement, attacks, skills, damage,
  loot, XP, level-up, respawn.
- Inventory/UI accessors take `PlayerId`; the app passes `localPlayerId`.
- Single-player constructs a frame with `activePlayers = 1` and empty P1 input;
  all existing tests should keep passing through a compatibility adapter.

### 5.3 World rules

- Enemies aggro/target the **nearest living player**; boss patterns target nearest.
- Projectiles store `owner PlayerId` and `fromPlayer`; friendly fire off.
- Enemies scale with `activePlayers`: HP ×1.6 (configurable), damage ×1.15.
- Loot is shared on the ground; pickup goes to the interacting player's inventory.
- XP/gold/kills go to the player who lands the killing blow (MVP).
- Boss death/portal/zone transition is shared simulation state.
- Portal interaction by either player transitions both in the same tick.
- Dialogue is shared; message/banner are shared; per-player HUD uses own stats.
- Death: the dead player becomes a spectator until they respawn at the last fire
  (RMB on fire by the living player can revive them; MVP: respawn timer + own
  fire interaction).
- Zone story persists; class/inventory UI is local and does not stop the world.

### 5.4 Cameras and snapshots

- `CameraState` per player (yaw, pitch, desired/smoothed distance, target).
- `buildSnapshot(PlayerId viewer)` returns:
  - the selected player's camera,
  - all player meshes,
  - shared enemies/loot/projectiles/effects/portal/fire,
  - overlay only for the viewer's UI (or app appends UI after).
- App renders once per client with its own viewer id; split-screen is out of scope.
- Headless tests can build snapshots for either viewer.

### 5.5 Simulation loop

- Extract a `SimulationDriver`:
  - accumulator at fixed `dt = 1/60`,
  - consumes one `InputFrame` per tick,
  - never uses wall-clock inside `RpgGame`,
  - advances render time separately.
- Single-player: driver feeds local input every tick; render interpolates camera
  if needed (or uses latest tick).
- Co-op: driver waits until commands for the upcoming tick are available.

---

## 6. Network layer

### 6.1 Targets/files

```
src/net/include/eee/net/Transport.hpp     // ITransport, peer ids, endpoint
src/net/include/eee/net/Protocol.hpp      // message structs + serialize
src/net/src/Protocol.cpp
src/net/include/eee/net/Session.hpp       // handshake, command queue, checksums
src/net/src/TcpTransport.cpp
src/net/src/TcpTransport.mm               // POSIX sockets, no extra deps
src/session/include/eee/session/SessionDriver.hpp
src/session/src/SessionDriver.cpp         // fixed tick + lockstep state machine
```

`eee_net` depends on `eee_core` only.
`eee_session` depends on `eee_core`, `eee_net`, `eee_logic`.

### 6.2 Wire protocol (v1)

Little-endian, length-prefixed:

```
[u32 payloadSize][u16 type][u16 version][payload]
```

Messages:

| Type | Direction | Fields |
|---|---|---|
| `Hello` | C→S | protocol version, desired class, client nonce |
| `Welcome` | S→C | playerId (1), sessionId, tickRate |
| `LobbyState` | S→C | host class, client ready, seed |
| `ClassChoice` | C→S | classId |
| `StartRun` | S→both | seed, zoneIndex, activePlayers, startTick |
| `InputCommand` | both→both | playerId, tick, PlayerInput |
| `InputAck` | S→C | highest tick received |
| `Checksum` | both | tick, u64 hash |
| `Ping/Pong` | both | timestamp |
| `Disconnect` | both | reason |

Maximum message size: 4 KiB. Reject malformed frames; disconnect on protocol
error. Version mismatch → human-readable error.

### 6.3 Lockstep algorithm (MVP)

- Host owns the lobby and assigns ticks.
- Input delay `D = 3` ticks. For tick `T`, both peers need inputs for `T` from
  both players; inputs were collected at `T - D`.
- Each peer sends its own input command every tick and relays nothing (host
  rebroadcasts all commands).
- A peer advances simulation only when the command set for `T` is complete;
  otherwise it stalls (HUD “WAITING FOR PLAYER”).
- Host starts run only when both clients are ready in the lobby.
- Checksum (FNV-1a over positions, health, tick, RNG state, zone, boss HP,
  projectiles/loot counts) every 30 ticks. Mismatch → halt, log both hashes,
  show “DESYNC”.
- No wall-clock inside simulation; all RNG seeded from `StartRun.seed`.

### 6.4 TCP details

- `TCP_NODELAY`, blocking sockets on a dedicated receive thread or non-blocking
  poll with `select` on the main loop.
- One connection per client, host listens on a configurable port (default 27015).
- Framing handles partial reads/writes; queue writes.
- Heartbeat every second; timeout 5 s → disconnect.

### 6.5 Later: UDP

Keep `ITransport` so the session is transport-agnostic. UDP adds sequence
numbers, ack bitfield, retransmission for commands, and MTU-safe packets.
Do not start UDP until the TCP loopback test is green.

---

## 7. App modes and CLI

```
./build/eee_demo                         # single-player (unchanged)
./build/eee_demo --host[=27015]          # lobby host
./build/eee_demo --join=192.168.1.20[:27015]
```

- Host starts a lobby/class-select screen. Client joins, picks class, marks ready.
- Both see both players; each app renders its own camera and HUD.
- Single-player uses the same code path with `activePlayers = 1`.
- `--headless` co-op for tests: no window, null renderer, fixed frame count.

Lobby UI (app-level, not logic):

- Host/client class cards, ready state, seed (host), latency, “START” (host).
- In-game HUD adds the other player's name/class/HP bar.
- ESC opens a local overlay (resume / inventory / options / leave), which does
  **not** stop the simulation in co-op.
- Leaving sends `Disconnect` and returns to single-player/class select.

---

## 8. Tests

### Unit / logic

- Existing tests still pass for single-player.
- 2-player deterministic update: same `InputFrame` sequence → identical state.
- Player-specific inventory/equipment/stats are isolated.
- Enemy targets nearest player; damage goes to the intended player.
- Loot pickup goes to the interacting player; drop ownership.
- XP/level/gold are per player.
- Zone/portal transition with one player interacting moves both.
- Dead-player respawn at fire; revive rules.

### Protocol

- Round-trip serialize/deserialize for every message.
- Reject truncated/oversized/unknown-version frames.
- Fuzz payload lengths; no crashes.

### Session / loopback

- Host + client in one process (loopback transport), 600 ticks, identical
  checksums, no stalls.
- Injected 20–80 ms latency: both still progress, input delay respected.
- Dropped a command: stall then recover (TCP retransmit).
- Injected state difference → checksum mismatch detected at next checkpoint.

### Two-process integration (CTest)

- Spawn `--host --headless` and `--join=127.0.0.1 --headless --frames=N`;
  both exit 0 and report the same final checksum.
- Requires a free-port helper and a timeout.

### Manual

- Two windows on one Mac or two Macs: class select, walk, fight, loot, portal,
  boss, death/respawn, disconnect.

---

## 9. Phased implementation checklist

Each phase must leave single-player green. Do not start the next phase until the
previous acceptance test passes.

### Phase 1 — Player array refactor (no net) ✅

- ✅ Introduce `PlayerId`, `PlayerInput`, `InputFrame`, `Loadout`, per-player state.
- ✅ Per-player state array; existing single-player paths go through `player(P0)`.
- ✅ Level/gold/inventory/equipment/totals moved into `Loadout`.
- ✅ `activePlayers` + local viewer; input-frame adapter is the first Phase 2 task.
- ✅ `buildSnapshot(PlayerId)`; app passes the local viewer (currently P0).
- Tests: existing suite passes; two `RpgGame` instances with same inputs still
  deterministic.

**Exit:** single-player identical behaviour; all tests green.

### Phase 2 — 2-player simulation ✅

- ✅ Second player input (`updateFrame`), rendering, per-player cameras.
- ✅ Enemy/boss nearest-player targeting.
- ✅ Projectile ownership and per-player kill/XP credit.
- ✅ Shared loot; pickup/rest/interactions execute for the acting player.
- ✅ Per-player inventory/equipment/stats.
- ✅ Tests: P1 movement/camera, targeting, P1 combat/kill credit, 2-player
  determinism.
- ⏳ Co-op difficulty scaling deferred to co-op balance (Phase 6).

**Exit met:** two local inputs can play one world headlessly; no networking yet.

### Phase 3 — Fixed tick + SessionDriver ✅

- ✅ Extracted fixed 60 Hz driver into `eee_session`.
- ✅ Single-player drives through it; UI modes stay frame-based.
- ✅ Input edges/mouse delta buffered and consumed once.
- ✅ Tick determinism across render cadences; catch-up cap tested.

**Exit met:** single-player feel preserved; tick tests green.

### Phase 4 — `eee_net` transport and protocol ✅

- Add `ITransport`, TCP transport, framing, messages, serializer.
- Add protocol/unit tests and a loopback echo test.

**Exit:** host/client exchange Hello/Welcome/InputCommand reliably.

### Phase 5 — Lockstep session ✅

- ✅ Command queues, 3-tick input delay, checksums, waiting/disconnect states.
- ✅ Loopback headless 600-tick test with equal checksums.
- ✅ Delayed-transport test and injected-desync detection.
- ⏳ Packet-loss injection waits for the UDP transport (TCP retransmits).

**Exit met:** deterministic 2-peer session test green.

### Phase 6a — Playable co-op slice ✅

- ✅ `--host` / `--join`, class via `--class`, StartRun with both classes.
- ✅ Remote player HUD, local camera/HUD/skills.
- ✅ Local non-pausing menu (Resume/Leave).
- ✅ Two-process CTest.
- ⏳ Full lobby/ready UI, co-op inventory, death/respawn, reconnect → 6b/7.

**Exit met:** two real apps can play a zone together over LAN.

### Phase 6c — Dedicated relay server ✅

- ✅ `eee_server --port=... [--once]`.
- ✅ Two-client accept, Welcome/ClassChoice/StartRun, input/checksum relay.
- ✅ `dedicated_smoke` CTest with two real clients.
- ⏳ Authoritative simulation, auth, matchmaking, NAT traversal/UDP later.

### Phase 6b — Lobby and co-op polish

- In-app lobby with class cards and ready/start handshake.
- Co-op inventory (local overlay while simulation continues).
- Death → revive/respawn rules; reconnect/leave messages.
- Interaction prompts for the local viewer (currently P0-oriented).

### Phase 7 — Authoritative server

See [authoritative-server.md](authoritative-server.md) for the full architecture
and migration A1–A8. **A1 is complete:** `eee_server` owns a headless `RpgGame`,
applies both clients' inputs at a fixed 60 Hz, exchanges checksums and fails the
session on divergence. It is still a relay for client state; A2/A3 add
authoritative streaming and snapshots.

### Phase 8 — Robustness

- Desync reporting, protocol errors, timeouts, reconnect policy (MVP: refuse),
  port configuration, session logs.
- Stress: 30 minutes idle + combat, checksum stable.

**Exit:** no silent divergence; failures are explicit.

### Phase 9 — Polish

- Other-player nameplates, party/ready UI, latency indicator.
- Loot rules/party assignment (optional), revive interaction.
- Pause-vote (optional) or local pause overlay behaviour documented.

### Phase 10 — Optional transport upgrade

- UDP + reliability/acks, jitter buffer, packet loss handling.
- Keep TCP as fallback.

### Phase 11 — Post-MVP candidates

- 4 players, late join via state replay, host migration, matchmaking, dedicated
  servers, server-authoritative loot.

---

## 10. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Float determinism differs between builds | same binary/platform for MVP; checksum early; lock protocol version to build hash |
| Input lag feels bad | start with 3 ticks, expose a `--input-delay`; later prediction for local camera only |
| Menus no longer pause | design overlay as non-blocking; document; single-player keeps pause |
| Inventory/UI doubling | per-player accessors; app passes `localPlayerId`; avoid duplicating UI code |
| Loot/XP disputes | killing-blow XP, shared drops, pickup by interacting player; revisit party rules later |
| Desync debugging | checksum fields + optional state dump on mismatch; deterministic RNG |
| Network thread safety | sockets on one thread; session exchanges immutable commands/tick results |
| Scope creep | MVP is 2 players, TCP, LAN, no late join, no matchmaking |

---

## 11. Deferred graphics track (separate from multiplayer)

These remain separate milestones because they need GPU validation:

### G1 — Resolution scaling

- Add `IRenderDevice::setRenderScale(f32)`.
- Offscreen render target at `displaySize * scale`; upscale on present:
  - Metal: `CAMetalLayer.drawableSize` + compositor upscale, or explicit
    render pass + linear sampler.
  - OpenGL: scaled FBO + `glBlitFramebuffer(..., GL_LINEAR)`.
- UI must use render-resolution coordinates or a dedicated UI pass at native
  resolution.

### G2 — MetalFX spatial upscaling

- Replace the linear upscale with `MTLFXSpatialScaler` in the Metal backend.
- Fallback to linear when unavailable.
- Needs color input + output texture + descriptor validation.

### G3 — MetalFX frame interpolation (native frame generation)

- Requires motion vectors, depth, previous color/history textures and
  `MTLFXFrameInterpolator`.
- Must run only when the frame pacing and history are valid; user toggle.
- Needs a motion-vector pass from camera/instance data.

### G4 — Hardware raytracing

- Metal acceleration structures for static world + dynamic proxies.
- Ray-traced shadows/reflections with a raster fallback.
- Requires GPU/feature detection and significant shader work; only after G1–G3.

---

## 12. Definition of done (MVP multiplayer)

- Two players, LAN, host/join.
- Shared world, enemies, bosses, loot, zones, story.
- Per-player classes, stats, inventory, equipment, skills, camera.
- Fixed 60 Hz, 3-tick input delay, checksums.
- Headless loopback and two-process tests green.
- Single-player tests unchanged and green.
- Protocol and session code has no dependency from `eee_logic`.
- Failure modes (desync, disconnect, version mismatch) are explicit in UI/logs.

---

## 13. Open questions to settle at Phase 1 kickoff

1. TCP-only MVP acceptable? (Recommended: yes.)
2. Co-op death: respawn timer at fire, or require the other player to revive?
3. Loot: free-for-all pickup or per-player instanced drops?
4. XP: killing blow or shared party XP?
5. Should the host be able to pause the run in co-op?
6. Are menus non-blocking from day one, or should we disable inventory in co-op
   until Phase 8?
7. Do we need split-screen for local co-op, or LAN only?

Default answers in this plan: TCP, respawn at fire, free-for-all pickup,
killing-blow XP, no global pause, inventory non-blocking, LAN only.

---

## 14. Immediate next step

Start **Phase 1**: introduce `PlayerId` / `InputFrame` / `Loadout` and convert
`RpgGame` to an array of players while keeping single-player behaviour identical.
No networking until the full single-player suite is green again.
