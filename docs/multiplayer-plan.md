# Multiplayer plan (not implemented)

> The detailed, phase-by-phase implementation plan lives in
> [refactor-plan.md](refactor-plan.md). This file remains a short rationale.

Networked play is deliberately **not** part of the current build. This document
records the design the codebase is already shaped for, so the work can proceed
without destabilising gameplay or rendering.

## Why it is a separate milestone

A co-op ARPG needs, at minimum:

- an authoritative simulation and a transport (UDP with reliability layers, or
  TCP for a first LAN prototype),
- input command packets and server tick loop,
- delta-compressed world snapshots (players, enemies, projectiles, loot),
- client-side prediction + reconciliation for movement,
- interpolation for remote entities,
- session lifecycle (host/join, zone transitions, disconnect/reconnect),
- authoritative item/inventory checks.

Adding these inside `RpgGame::update()` would couple networking, gameplay and
rendering. Instead, the existing boundaries are the foundation:

- `RpgGame` is deterministic for a given `(dt, InputState)` sequence and owns all
  state behind accessors.
- `InputState` is a plain value; a network layer can construct one from a remote
  input command.
- `SceneSnapshot` is already the render boundary; remote clients can render a
  replicated snapshot or a locally predicted one.
- Menus/inventory are game state, so authority checks live in logic, not UI.

## Proposed architecture

```
client input ──> NetSession (transport, sequence numbers)
                     │
                     ▼
              Server: fixed-tick RpgGame
                     │  delta snapshots (zone, players, enemies, loot, projectiles)
                     ▼
              Client: prediction + interpolation
                     │
                     ▼
              Renderer (unchanged)
```

- **Tick rate:** 20–30 Hz simulation, render at display rate.
- **Transport:** UDP + reliability for state-changing events (loot/items), raw
  unreliable for movement snapshots; TCP fallback for a first prototype.
- **Prediction:** re-run `RpgGame` locally for the own player from acknowledged
  inputs; reconcile against server position with smoothing.
- **Remote entities:** render two snapshots in the past and interpolate.
- **Items:** server-authoritative rolls and inventory transfers; clients send
  intent (`pickup`, `equip`) and receive results.
- **Zones:** server decides when a portal transition happens; clients receive a
  zone snapshot, not a local `loadZone` call.

## Code seams to add

1. Split `RpgGame::update` into `applyInput(player, command)` and `tick(dt)` so a
   server tick is explicit.
2. Make `PlayerState`/`EnemyState`/`Item` serializable (fixed-size POD writer).
3. Add `net::Session` in a new `eee_net` target (no dependency from `eee_logic`
   on sockets; a host loop drives both).
4. Add `playerId` to game state instead of a single `player_`; keep a local
   single-player adapter so the current app keeps working.
5. Add `--host` / `--join=host:port` to the app and a small lobby/class-select
   handshake.

## Local-first alternative

If the goal is couch co-op rather than online play, the smaller step is to make
`player_` an array and feed two `InputState`s (keyboard halves / gamepad). That
still requires camera framing for two characters, shared loot rules and UI
changes, so it should be a dedicated milestone too.
