# Authoritative Server Architecture

Status: **A1–A6 complete: authoritative world, inventory, prediction/interpolation, zones and reconnect.**
Current build: deterministic client lockstep + a TCP relay (`eee_server`) that
now also owns a **shadow authoritative `RpgGame`**: it applies both clients'
inputs at a fixed 60 Hz, computes server checksums every 30 ticks and validates
them against client checksums. Dedicated-server clients now apply server
snapshots and **do not simulate the world**; they only send inputs and render the
latest authoritative state. Server-owned inventory/actions (A4), prediction
(A5), zone transitions/reconnect (A6) and security (A7) are next.

This document defines what must change to make the server authoritative.

---

## 1. What "authoritative" means here

The server is the only source of truth for:

- player positions, health, mana, cooldowns, jump state
- enemy and boss state
- projectiles, effects, loot drops
- XP, levels, gold, inventory, equipment, skill unlocks
- zone progression, boss deaths, portals, story state
- valid input ordering and tick timing

Clients send **intent** (input commands, interact/equip/drop requests). They never
tell the server what happened. The server decides and replicates results.

Clients may predict their own movement for responsiveness, but the server can
correct them. Remote entities are interpolated, not simulated by clients.

---

## 2. Current vs target

| Concern | Current lockstep | Target authoritative |
|---|---|---|
| Simulation | both clients | server only |
| Tick timing | clients | server |
| Inputs | exchanged peer-to-peer via relay | uploaded to server; server applies |
| State | implicit/deterministic | explicit snapshots/deltas |
| Checksums | peer desync detection | server validation + correction |
| Loot/XP | client simulation | server rolls and assigns |
| Inventory | local | server-owned, replicated deltas |
| Cheating | not prevented | server rejects invalid intents |
| Client role | full simulation | prediction + interpolation + rendering |

---

## 3. Protocol v2

### Client → Server

| Message | Purpose |
|---|---|
| `AuthRequest` | protocol version, build hash, optional token |
| `AuthResult` / `Welcome` | assigned playerId, sessionId, tickRate |
| `ClassChoice` | lobby class |
| `Ready` | ready/start intent |
| `InputCommand` | tick, PlayerInput (movement/aim/buttons) |
| `ActionRequest` | interact/pickup/equip/drop/use gem/flask |
| `Ping` | RTT / clock sync |
| `Disconnect` | leave |

### Server → Client

| Message | Purpose |
|---|---|
| `LobbyState` | classes, ready flags, seed |
| `StartRun` | zone, players, authoritative start tick |
| `ServerTick` | authoritative tick release; clients may not run ahead |
| `Snapshot` | full/delta world state at tick |
| `PlayerStats` | HP/MP/XP/level/gold/cooldowns (local + ally HUD) |
| `InventoryDelta` | authoritative bag/equipment changes |
| `ActionResult` | accepted/rejected + reason |
| `ServerChecksum` | periodic validation stamp |
| `CorrectPlayer` | authoritative position/state correction |
| `ZoneChange` | new zone + story/boss state |
| `Disconnect` / `Kicked` | reason |

Snapshots should be:

- **delta-compressed** against the last acknowledged snapshot,
- **interest-managed** (only nearby enemies/loot/projectiles, not the whole
  static world),
- sequenced by tick and acknowledged by the client.

### Static world

Options, in order of preference:

1. Client regenerates the zone deterministically from `(zoneIndex, worldSeed)`
   sent at zone change (requires the generation code to be version-locked).
2. Send the static mesh list once per zone (~13k instances currently, roughly
   1 MB) and cache it per zone.
3. Move static world data to a content pack shared by client and server.

Dynamic entities are always replicated from server snapshots.

---

## 4. Server loop

```
fixed 60 Hz tick:
  collect/validate inputs for this tick (or reuse last input for missing)
  apply intents (movement, attack, skills, interact, item actions)
  advance RpgGame
  resolve authoritative combat/loot/XP/inventory
  build per-client interest snapshots + HUD/inventory deltas
  write checksums every 30 ticks
  send frames
```

- Input delay needs to cover RTT + jitter. Clients send commands ahead with a
  sequence number; the server buffers them.
- Missing input: hold last valid command briefly, then neutral; never accept a
  command for an already simulated tick.
- Rate-limit malformed or spammed messages; disconnect abusive clients.
- The server never trusts client positions, hit results, loot rolls or stats.

---

## 5. Client loop

```
sample input
  → send InputCommand(seq, predicted tick)
  → locally predict own movement/camera (optional)
  → render interpolation buffer of server snapshots
  → reconcile local player against latest authoritative state
  → apply authoritative inventory/HUD deltas
```

- Prediction is limited to the local player's movement/camera; not enemies,
  loot, damage or item outcomes.
- Interpolation renders remote entities ~100 ms in the past.
- Reconciliation uses server position + last acknowledged input sequence.
- Menus/inventory send requests; the UI updates only on server confirmation.

---

## 6. Authority boundaries

### Movement
Server simulates movement from `PlayerInput` + collision. Client prediction is
visual only; server corrections are smoothed.

### Combat
Server computes hits, crits, cooldowns, mana, damage and death. Clients send
attack/skill intents, never “I hit X for Y”.

### Loot and items
Server rolls drops (server RNG), owns bag/equipment and validates pickup/equip/
drop/use requests (range, level requirement, slot, inventory space). Item rolls
are never generated on the client for authoritative play.

### XP/progression
Server awards kill credit, XP, gold, levels and zone unlocks.

### Zones/story
Server decides portal transitions and boss state; clients receive a zone change
and static world identity.

---

## 7. Security (required before public internet)

- TLS or a game-specific secure transport; current TCP is plaintext.
- Authentication/session tokens, max message sizes, rate limits.
- Reject input for future ticks beyond a window; reject duplicate/old ticks.
- Server-side validation of all item/movement actions.
- Logs/audit for kicks and anomalies.
- Keep the relay mode for trusted LAN only; do not expose it publicly.

---

## 8. Migration phases

### A1 — Server-owned simulation skeleton ✅
- ✅ `eee_server` owns a headless `RpgGame` for two players.
- ✅ Accept clients, class handshake, StartRun, receive inputs.
- ✅ Simulate fixed 60 Hz ticks; neutral warm-up for the input-delay window.
- ✅ Server checksums every 30 ticks compared to both clients' checksums.
- ✅ `dedicated_smoke` verifies the server reports `checksums validated`; a mismatch makes the server exit non-zero with `--once`.
- ⏳ Snapshots/corrections are A2/A3; clients still simulate and relay.

### A2 — Authoritative tick/input stream ✅ (tick authority)
- ✅ Server runs fixed 60 Hz ticks and emits `ServerTick` after each tick.
- ✅ Dedicated-server clients gate simulation on `ServerTick`; they cannot run
  ahead of the authoritative simulation.
- ✅ Server checksums still validate both clients every 30 ticks.
- ⏳ Clients still compute state locally; server snapshots/corrections are A3.
- ⏳ Real-time wall-clock tick pacing (currently input-bounded) is a later A2 polish.

### A3 — World snapshots ✅ (A3b)
- ✅ `core::WorldSnapshot` for players, enemies, projectiles, loot, zone/portal/tick.
- ✅ Versioned `Snapshot` protocol message with bounds checks.
- ✅ Server captures the authoritative `RpgGame` every tick and sends snapshots.
- ✅ Authoritative clients apply snapshots, send inputs only and never simulate.
- ✅ Client-side applied-state checksum is compared with the server checksum.
- ⏳ Delta compression/interest management and prediction are still future work.
- ⏳ Static-world zone transitions need the A6 world-identity handshake.

### A4 — HUD, inventory, actions ✅
- ✅ Player snapshots include cooldowns, combat stats and secondary-skill unlock.
- ✅ Inventory and all 8 equipment slots replicate with item names, bases, rarities,
  flask/gem fields and full modifier labels/values (bounded strings/counts).
- ✅ `ActionRequest` (equip/use/drop) is validated against the sender's player id
  and applied to the authoritative `RpgGame`.
- ✅ Co-op inventory UI: `I` opens it while the session keeps running; arrows/WASD
  select, Enter equips/uses, X drops; server snapshot refreshes the bag/equipment.
- ✅ Client comparison/tooltips use replicated equipment data.
- ⏳ Item IDs, action acknowledgements/rejections and rate limiting are future work.
- ✅ Player snapshots now include cooldowns, damage, armour, move speed, crit and secondary-skill unlock.
- ✅ `ActionRequest` protocol message (equip/use/drop) validated against the sender's player id.
- ✅ Server applies inventory actions to the authoritative `RpgGame`; the next snapshot carries the resulting state.
- ✅ Logic/protocol tests cover server-side equip/drop.
- ⏳ Inventory/equipment contents are not yet replicated; no co-op inventory UI or action buttons yet (A4b).
- ⏳ Loot pickup/NPC/fire/portal interactions already go through `PlayerInput.interact` on the server.
- `PlayerStats`, `InventoryDelta`, `ActionRequest`.
- Loot pickup/equip/drop/flask/gem go through server validation.
- Co-op inventory UI becomes server-confirmed.
- Tests: client cannot equip above level; cannot duplicate items; reach limits.

### A5 — Prediction/interpolation ✅
- ✅ Local movement prediction/replay/reconciliation (A5a).
- ✅ Remote interpolation (A5b): the other player, enemies, projectiles and loot interpolate between the last two snapshots; local prediction owns the local player.
- ✅ One-snapshot visual delay; alpha resets per snapshot; first snapshot applies immediately.
- ✅ Local-only prediction scope; health/stats/loot remain server-authoritative.
- ⏳ Remaining polish: shortest-angle yaw interpolation, index-aware entity IDs (avoid swap on reorder), snapshot extrapolation, rollback for actions.
- ⏳ TCP tests skip automatically when the sandbox denies bind; non-network logic/interpolation tests still run.
- ✅ Local movement prediction: client keeps sent inputs by target tick and replays unacknowledged ones at 60 Hz.
- ✅ Snapshot reconciliation: small errors blend away with a decaying visual offset; large errors snap.
- ✅ Prediction touches only the local player position/yaw/jump/walk animation; enemies, loot, stats and RNG remain server-authoritative.
- ✅ Added logic/session tests for prediction, jump prediction, pruning, snap threshold and smoothing.
- ⏳ No snapshot interpolation buffer for remote players/enemies yet (A5b).
- ⏳ No rollback/replay of actions; only movement prediction.
- Local movement prediction + reconciliation; remote interpolation.
- Camera stays local.
- Tests: simulated 80–150 ms RTT; no rubber-banding beyond threshold.

### A6 — Zones, respawn and reconnect ✅
- ✅ Static worlds are deterministic from the zone index (same geometry/collision on server and clients).
- ✅ Server-driven zone index in snapshots; clients regenerate the matching static zone.
- ✅ Death → Respawn at Fire via an authoritative action; P0 respawn clears the death pause.
- ✅ Reconnect: per-player resume tokens in `Welcome`, `ResumeRequest`, server slot grace of 20s, fresh snapshot on resume.
- ✅ Client reconnect menu (CONNECT LOST → Reconnect/Leave).
- ✅ Debug hook: **F8** drops the client TCP connection so reconnect can be exercised on localhost.
- ⏳ Reconnect currently requires the server process/session to still be within its 20s grace; no reconnect after session end.

### A7 — Security hardening
- TLS/auth, rate limiting, tick-window validation, kick/ban plumbing.
- Abuse tests: malformed frames, replay, impossible input rate.

### A8 — Matchmaking / NAT
- Lobby service, server directory, relay fallback.
- UDP transport if needed.

---

## 9. Immediate next step

Implement **A1**: make `eee_server` own a headless `RpgGame`, apply two clients'
inputs at a fixed 60 Hz and expose a server checksum. No client rendering changes
until A2/A3.

This gives a real authoritative simulation core, but the current relay should
keep working until snapshots land so the game remains playable during migration.

---

## 10. What not to claim until A3

Do not describe the current server as authoritative. Today it is:

- a relay,
- plaintext/unauthenticated,
- client-simulated,
- checksum-aware only if clients compare among themselves.

A server that merely relays inputs is not authoritative, even if it runs a
shadow simulation.
