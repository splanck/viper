# 22 — Game: World Systems — Zones/Lighting, Doors, Pickups, Objectives, Destructibles, Environment, Streaming, Arena

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1–2 sessions (built in
> the P8 window, consumed by every level doc). Prereqs: 11-architecture, 03 (E11/E12
> telemetry), 08 (E28 tiled nav), 05 (E20/E23).

## 1. Lighting zones (`world/lighting_zones.zia`)

The budget brain. Levels register lights per PVS zone (manifest `lights` rows); the manager:
- Activates the union of lights for zones in the current portal-clipped visible set (E23),
  clamped to the backend path: clustered (all 4 backends post-06/07 era; ≤ 64) with
  per-cluster budget set via E11; fixed-forward fallback ≤ 16 by priority (sun > player
  muzzle/flashlight > objective markers > zone fill), `get_DroppedLightCount` asserted 0 in
  probes at authored densities.
- **Shadow slots** (03 E10, 8 total): sun CSM always (outdoor); indoor priority table per
  zone (manifest `lightSet` names a shadow plan: e.g. `l5` rotates 2 cube + 2 hemisphere
  point slots among the 12 bioluminescent lights by player proximity, 0.4 s fade handoffs —
  pop-free rotation is this module's hard problem, solved by fading `SetShadowMode` handoffs).
- **Transient lights**: muzzle flashes / explosions / beacons request 1-tick or timed slots
  from a per-zone transient pool (4); denied requests degrade to emissive-only (never trap,
  never steal authored shadows).

## 2. Doors & devices (`world/doors.zia`)

Powered doors (kinematic bodies, panel interact / auto-proximity / lockdown-sealed states,
nav dynamic obstacles while closed E28-carve), airlocks (paired doors + pressure beat),
elevators/lifts (kinematic mesh platforms E15 with authored timelines + call panels),
switches/levers/valves (interact-hold with progress ring), hackable panels (turret IFF,
monitor feeds, door overrides — hold 2.5 s, interrupted by damage, alerted zones lock panels
45 s), physics props (crates/barrels registry: dynamic bodies with reduced hulls E14).

## 3. Pickups (`world/pickups.zia`)

Trigger3D + bob/spin mesh + magnetize ≤ 1.6 m (accel toward player, feel per 27): ammo by
class, medkit (hold-to-use from inventory: 1 carried + instant floor use), armor cell,
salvage clusters (auto-magnet, no interact), weapons (first-time = acquire cinematic beat +
HUD unlock toast; repeat = ammo), data cores/logs (secret chime + collection UI), grenade
restocks. Drop tables: enemies drop salvage always + contextual ammo (director-aware: drops
bias toward the player's LOW reserve class ×0.35 weight — anti-starvation without vending
machines; probe-asserted distribution).

## 4. Objectives & checkpoints (`world/objectives.zia`)

Objective graph per manifest (serial + optional branches): kinds reach / interact / defend
(timed waves) / destroy (target set) / escort (tether anchor) / collect. HUD binding: marker
diamonds with off-screen edge arrows + distance, objective text panel (Tab expands log),
radio-bark hooks on state changes. **Checkpoints**: manifest-placed at objective boundaries;
save = 11 §6 checkpoint.json (loadout/ammo/health/objective state/RNG cursors/door states/
destroyed-destructible list — world diffs, not full world state); death → reload checkpoint
(2.2 s fade, death-cause line). Autosave also on level exit + hub.

## 5. Destructibles (`world/destructibles.zia`)

Registry of breakables: **barrels** (explosive: chain via 14 §4; fuel-fire variant burns
8 s area), **crates** (loot), **barricades/fins** (HP'd cover that chews away: swaps intact→
damaged→debris models, carves nav via dynamic obstacle then `RebuildTile` E28 on final break),
**glass** (panes shatter to particle burst + noise event — stealth consequence), **vents/
grates** (melee-openable paths). All emit surface-tagged debris bursts (pooled) + event rows
(AI hearing, stats). Placement from manifests; state serialized in checkpoints.

## 6. Environment (`world/environment.zia`)

The ridgebound lineage, generalized: sky phase sets (per-level cubemap banks, scripted
progressions for L3 dawn→noon and L8 night→dawn), sun/moon arc + CSM strength curves,
height-fog + shaft parameters per phase (E39), weather states (clear / ashfall / storm with
wind vector feeding particle stretch E41 + ballistic drift + audio bed), lightning scheduler
(L6: flash light-spike + delayed thunder + optional strike-point FX), wetness/ash-accumulation
material hooks (albedo darken on horizontal surfaces during storm — material param anim),
LUT + auto-exposure rig per level (07), per-sequence postfx overrides (L9 escape).

## 7. Streaming (`world/streaming.zia`)

L3/L8-far-vista wrapper over `WorldStream3D`: manifest cell grid + tiled terrain mount,
`SetCenter` follows player (or crawler anchor), radii per quality tier, residency budget +
eviction telemetry to F3, floating-origin rebase at 200 m threshold (`RebaseOrigin` between
frames — engine contract) with a rebase event row (probes assert particle/decal/audio
continuity through rebase — the classic bug class).

## 8. Arena (`world/arena.zia` final form)

Graybox → dressed testbed: the P1 box canyon + **wave mode** (menu-accessible): escalating
director waves (all archetypes by wave 8, boss-lite WARDEN mini at wave 12), salvage scoring,
best-wave persistence, the perf harness scenario embedded (wave 10 = the stress benchmark).
Ships in the final game (arcade mode) — the tuning tool becomes content.

## 9. Probes

Zone/light: authored densities per level → zero dropped lights / zero cluster overflow /
shadow rotation never exceeds slots (telemetry sweep probe per level manifest). Doors: state
machines + nav carve/restore; elevator timelines deterministic. Pickups: magnet math, drop-
table distribution over 1,000 kills within ±3 %. Objectives: graph walker covers every kind;
checkpoint round-trip restores world diffs exactly (door/destructible states). Destructibles:
barricade break → tile rebuild → agent re-path ≤ 2 ticks; glass noise event. Environment:
phase interpolation continuity (no > 1-step light jumps except lightning), LUT swap
correctness. Streaming: §7 telemetry + rebase continuity. Arena: wave determinism.
All VM==native.

## 10. Verification gate

System probes green headless; the L1-slice integration run (P8 exit criterion): a vertical
slice of L1 built ONLY from manifests + these systems plays start-to-finish. Full build green.
