# 11 — Game: Architecture, Module Map, Simulation Contract

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1 session (produces the
> skeleton every later doc fills). Prereqs: 10-toolchain (struct returns), 01-input (E3/E4).

## 0. TL;DR

ASHFALL's foundation: a foldered ~72-module project at `examples/games/ashfall/`, a 60 Hz
fixed-timestep simulation on `World3D.RunFixed` with interpolated rendering, a pooled entity
registry with generation handles, a typed event bus, one damage pipeline, seeded RNG streams,
budget-asserting diagnostics, and the headless probe pattern. Everything here is patterns +
contracts — later docs add content into these sockets.

## 1. Project layout

```
examples/games/ashfall/
  viper.project            # entry main.zia; pack directives added in P19 (26-assets)
  main.zia                 # args: --smoke, --level N, --windowed, --backend override passthrough
  config.zia               # ALL tunables (~900 lines by ship; sectioned, no magic numbers elsewhere)
  game.zia                 # orchestrator: states, subsystem wiring, RunFixed callbacks
  core/    entity.zia events.zia rng.zia mathutil.zia savegame.zia devtools.zia
           cinematic.zia strings.zia
  player/  playerctl.zia camera_rig.zia viewmodel.zia health.zia stealth.zia
  weapons/ weapon_base.zia ballistics.zia projectiles.zia explosions.zia
           weapon_defs.zia impact_fx.zia upgrades.zia recoil.zia
  ai/      perception.zia director.zia cover.zia ai_base.zia
           husk.zia sapper.zia drone.zia ranger.zia marauder.zia vanguard.zia
           stalker.zia wraith.zia turret.zia mites.zia shrike.zia
           boss_warden.zia boss_shrike.zia boss_helix.zia
  anim/    rigs.zia hooks.zia ik.zia
  world/   level_base.zia loader.zia lighting_zones.zia doors.zia pickups.zia
           objectives.zia destructibles.zia environment.zia streaming.zia
           l1_crashsite.zia l2_relay.zia l3_crossing.zia l4_arcology.zia l5_caverns.zia
           l6_terraces.zia l7_foundry.zia l8_spire.zia l9_core.zia hub_redoubt.zia arena.zia
  fx/      effects.zia postfx.zia
  audio/   mix.zia music.zia synth_bank.zia
  ui/      hud.zia menus.zia feedback.zia photo.zia dialogue.zia
  meta/    economy.zia scoring.zia difficulty.zia stats.zia
  assets/  registry.zia materials.zia fallbacks.zia
  probes/  smoke_probe.zia combat_probe.zia level_probes.zia save_probe.zia perf_probe.zia
  assets_src/ (downloaded GLB/PNG/OGG; see 26) · CREDITS.md · README.md
```
Conventions (README §4 apply throughout): `module` + `bind ... as Alias` headers; one primary
class per module; `hide` fields, `expose` API; modules 500–1,500 lines.

## 2. Simulation contract

- `World3D.RunFixedWithOverlay(60, update, overlay)` — **all** gameplay in the 60 Hz update:
  input latch → player → weapons → AI (staggered) → projectiles → physics `StepSimulation` →
  damage resolution → objectives/triggers → audio sync (`SpatialAudio3D.SyncBindings`) →
  event-bus drain. Render interpolates via `get_FixedInterpolationAlpha` (engine-side for
  physics-bound nodes; camera manually interpolated in `camera_rig`).
- Determinism: gameplay RNG only from `core/rng.zia` streams (xorshift64*, streams:
  `gameplay`, `spread`, `ai`, `cosmetic` — cosmetic excluded from replay checksums); no wall
  clock in sim (use accumulated fixed dt); probes drive `SetClockSource`/synthetic input
  (Canvas3D deterministic hooks, runtime.def 13168-13174 region).
- Frame order contract (render): `BeginFrame` → scene draws → `DrawEffects` → view-model pass
  (E18) → `EndScene` → overlay (HUD after EndScene — landmine rule) → `Present`.
- Struct returns allowed in hot paths **after** 10-toolchain E34 lands (P0); until then the
  reusable-instance pattern is mandatory (this doc's skeleton lands after P0, so natural style
  from day one).

## 3. Entity registry (`core/entity.zia`)

- Archetype pools with parallel Lists (no Map): per archetype, dense arrays of fields +
  `alive: List[Integer]` (0/1) + free-list; handle = `(archetypeId << 40) | (slot << 16) | gen`
  packed in Integer. `EntityReg` API: `alloc(arch) -> Integer`, `free(h)`, `valid(h) -> Boolean`,
  `slot(h) -> Integer`. Generation bumps on free; stale handles fail `valid`.
- Body↔entity map: physics bodies registered `bodyToHandle` parallel lists (body id from
  `Physics3DBody` object identity is not hashable → store handle on spawn in a per-archetype
  list indexed by slot, and resolve raycast/collision hits via `Collider3D`→user-tag:
  each spawned body sets `CollisionLayer` per archetype and the game keeps
  `bodiesByLayerSlot`; hit resolution walks the archetype pool matching the hit body object
  reference — reference equality on obj handles is supported).
- Pools sized in config: projectiles 48, casings 24, decals 64 (engine registry also pools),
  damage popups 32, AI 16 (12 active cap + margin). **Zero allocation during combat** —
  asserted by devtools (see §7).

## 4. Event bus (`core/events.zia`)

Fixed ring (256) of `(kind, a, b, c, f0, f1, f2)` integer/float rows in parallel Lists.
Kinds (config constants): `EV_GUNSHOT(pos, loudness)`, `EV_EXPLOSION`, `EV_ENEMY_DIED`,
`EV_PLAYER_DAMAGED`, `EV_OBJECTIVE`, `EV_CHECKPOINT`, `EV_PICKUP`, `EV_DIALOGUE`,
`EV_MUSIC_CUE`, `EV_ALERT_RAISED`. Producers push; consumers (AI hearing, music, HUD toasts,
stats) iterate the frame's slice in the fixed update; cleared after drain. No allocation.

## 5. Damage pipeline (`weapons/ballistics.zia` owns entry point)

`applyDamage(targetHandle, amount, kind, hitX,hitY,hitZ, nX,nY,nZ, instigator)`:
locational multiplier (named collider region table per archetype), armor/shield split
(player) or plate/core split (Marauder/WARDEN), DoT registration (rivet burn), death →
`EV_ENEMY_DIED` + effects + salvage drop. Sources: hitscan (Physics raycast hit → handle),
collision events (Enter phase, `NormalImpulse` > threshold → crush damage), blasts
(`OverlapSphere` + per-victim occlusion raycast at 60 % damage through cover=none rule).
One pipeline, three feeders — no scattered health math (validated by damage-matrix probe).

## 6. Save/config (`core/savegame.zia`)

JSON via `Viper.Data.Json.Parse` + hand-built writer (strings.zia helpers) at
`Path.DataDir("ashfall")` (E4): `settings.json` (video/audio/controls/accessibility, written
on every change — bowling crash-safety pattern), `profile.json` (campaign progress, medals,
stats, upgrades, collectibles), `checkpoint.json` (level id, checkpoint id, loadout, ammo,
health, difficulty, objective state, RNG stream cursors for determinism). Versioned
(`"version": 1`) with forward-migration switch (bowling SaveData precedent). Save probe:
round-trip + corrupt-file recovery (falls back to defaults + backup rename, never traps).

## 7. Diagnostics & budgets (`core/devtools.zia`)

F3 overlay: FPS/frame ms, draws submitted/culled (frustum/occlusion/PVS counters), lights
active/dropped (E12), cluster overflow (E11), shadow slots used/dropped (03), awake bodies,
CCD TOI count, active enemies/projectiles/particles, mixer voices, entity pool watermarks,
streaming residency. **Budget asserts** (config): exceeding any budget in a probe run fails
the probe; in dev builds it flashes the overlay row red. Cheat flags behind
`config.DEV_CHEATS`: god, noclip (FreeFlyController swap), give-all, level skip.

## 8. `game.zia` state machine

`BOOT → TITLE → HUB → LOADING → PLAYING ⇄ PAUSED → (DEATH → reload checkpoint) →
LEVEL_COMPLETE → HUB/next → CREDITS`. Photo mode is a PLAYING sub-state (sim frozen: fixed
update short-circuits to camera-only). Menu screens return command enums (bowling pattern);
all transitions logged to the event bus (stats + probes assert legal transition tables).

## 9. Line budgets (enforced ±20 % at phase gates)

Per 00-vision §8 table; config.zia sections mirror module list. `probes/` target ~1,500 lines.
Any module crossing 1,500 lines splits before its phase gate closes.

## 10. Verification gate (this doc's implementation session)

Skeleton compiles: all modules created with class stubs + config sections + registry/events/
rng/save implemented for real (they're pure logic — fully unit-testable now); `smoke_probe`
runs 120 fixed ticks headless (synthetic clock) asserting: state machine walk
TITLE→PLAYING(arena stub)→PAUSED→PLAYING, event-bus round-trip, registry alloc/free/stale-
handle, RNG stream stability (golden values), save round-trip in a temp DataDir, zero budget
asserts. `viper check examples/games/ashfall --diagnostic-format=json` clean. VM==native on
the probe.
