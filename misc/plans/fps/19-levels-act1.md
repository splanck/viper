# 19 — Game: Level Loader + Act I (Crashsite, Relay Outpost, Ashveil Crossing)

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · **2-session chunk**
> (session A: loader + manifest schema + L1; session B: L2 + L3 + act probes).
> Prereqs: 12/13/14/15/16 (playable combat), 22-world-systems (built alongside — lighting
> zones/doors/objectives APIs land there in the same phase window; this doc consumes them).

## 1. Level loader + manifest (`world/loader.zia`, `level_base.zia`)

JSON manifests (`world/data/l1.json`... authored as Zia string constants first, external files
when `asset` shipping lands in P19) parsed with `Viper.Text.Json`:
```json
{ "id": "l1", "name": "Crashsite Canyon", "music": "wastes",
  "environment": {"skyPhase": "dusk", "fog": {...}, "lut": "l1_amber", "weather": "ashfall"},
  "terrain": {"seed": 811, "size": 160, "params": {...}},            // outdoor levels
  "scene": [{"prefab": "hab_corridor_a", "pos": [...], "rot": ...}], // interior kit pieces
  "zones": [{"id": 1, "bounds": [...], "portals": [2], "lightSet": "corridor_cool", "dark": 0.5}],
  "lights": [{"zone": 1, "type": "spot", ...}],
  "nav": {"cell": 0.3, "agentR": 0.4, "links": [...]},
  "encounters": [{"id": "e1", "trigger": "t_yard", "waves": [{"husk": 4, "drone": 1}], "tokens": {...}}],
  "covers": [[x,y,z,fx,fz,height], ...],
  "patrols": [[...waypoints]], "spawns": [{...}],
  "pickups": [{"kind": "ammo_light", "pos": [...]}, ...],
  "objectives": [{"id": "o1", "kind": "reach|interact|defend|destroy|escort", ...}],
  "checkpoints": [{"id": "c1", "at": "o1", "pos": [...]}],
  "triggers": [{"id": "t_yard", "box": [...]}],
  "secrets": [{"core": [...], "log": "log_l1_a"}],
  "doors": [...], "destructibles": [...], "monitors": [...],        // interior systems (22)
  "par": {"timeSec": 900, "accuracy": 0.55, "secrets": 3} }
```
Loader spawns through the 11-registry + 22-systems in the ridgebound setup order (world →
quality → postfx → terrain/scene + colliders → zones/lights → nav bake (E28 tiled) →
encounters armed → audio → objectives → checkpoint restore if loading). **Loading screen**
(23) driven by `Assets3D` async handles + staged-spawn progress fractions. `level_base.zia`
defines the interface every `lN_*.zia` implements: `manifest() -> str`, `setupScripted()`,
`tick(dt)`, `onEvent(...)` — bespoke scripting stays in the level module, data stays in JSON.
Probe hook: every manifest round-trips parse→spawn→teardown leak-free (registry watermark 0).

## 2. L1 — Crashsite Canyon (assault tutorial, 12–15 min)

- **Look**: dusk amber (LUT `l1_amber`), low sun CSM through ash-fall particles (stretch, E41),
  height fog in the basin (E39), crash-smoke column landmark (always-visible wayfinding),
  sparse dead-tree Vegetation3D + rock InstanceBatches on a 160 m Perlin terrain (ridgebound
  lineage, `terrain` params in manifest).
- **Flow (beats)**: crash wake-up cinematic (spline over wreck) → movement gulley (jump/slide
  teach via geometry, no text walls — contextual key prompts once each) → pistol pickup +
  target wrecks (fire/ADS teach) → first Husks ×3 (open bowl, retreat room) → ridge climb
  (vista moment: colony sightline + title card) → drone pair over the antenna field →
  Scattergun cache in a wreck (secret 1) → canyon fork (optional dark cave: warmth of the
  first data core + Mites ambush) → beacon yard **defend objective** (2 waves, teaches
  director rhythm) → relay door interact → checkpoint → L2 transition.
- Enemies: Husk, Drone, Sapper (1, scripted intro: it chains a wreck explosion), Mites (cave).
  Weapons by exit: Pistol, Scattergun. Secrets: 3 cores, 2 logs.
- **Showcase**: terrain/weather/CSM/fog + the feel loop. Perf target scene: 60 FPS Balanced.

## 3. L2 — Relay Outpost (stealth-optional infiltration, 15 min)

- **Look**: night interior, cool corridors vs warm security pools (LUT `l2_teal`), spot-light
  cones + first **point-light shadows** (server room, E9), dark zones 0.4 factor.
- **Flow**: perimeter breach (fence gap choice L/R) → yard patrols (3 Husk + camera pole:
  first detection-UI read) → interior: locker room (silenced-mod bench intro = free T2 pistol
  mod for this level; upgrades proper come at hub) → server hall (turret + patrols; **hack
  teach**: disabled turret flips IFF) → comms tower climb (vertical, Stalker introduction
  stalks the stairwell — audio tell teach) → rooftop dish alignment (interact-hold under
  drone harassment) → alarm consequence branch: LOCKDOWN adds Ranger squad at exfil if ever
  fully alerted (stealth = quieter exfil; probes cover both) → exfil ridge → checkpoint.
- Enemies: + Turret, Stalker, Ranger (loud path). Weapons: + SMG (armory secret).
- **Showcase**: stealth ruleset, spot/point shadows, alert ladder, hack. Ghost medal viable.

## 4. L3 — Ashveil Crossing (streamed open valley, escort, 18–20 min)

- **Look**: full day cycle across the mission (dawn→noon scripted), the plan's biggest vista:
  1.2 km streamed valley (WorldStream3D cells + tiled terrain, floating origin threshold
  200 m), heat-shimmer noon LUT, dust devils (particle columns).
- **Flow**: overlook briefing (Mara radio) → solo descent (Ranger duo long-sightline duel —
  Rail introduction via supply pod) → **convoy escort** core: survivor crawler (kinematic
  mesh mover on a spline road, E15) with 3 stop-events (barricade clear → destructible +
  nav rebuild E28; bridge Stalker ambush from below-links; sniper knoll Rangers + first
  Marauder reveal) — player free-roams around the moving anchor (director tethers spawns to
  crawler cells) → canyon narrows night-fall holdout (crawler repairs, 3 waves + Shrike
  fly-by teaser, no fight) → gate arrival → Act I outro cinematic.
- Enemies: + Ranger (native), Marauder (1, set-piece), Vanguard pair at the gate. Weapons:
  + Rail, Arc Launcher (crawler stores).
- **Showcase**: streaming (cell mount/evict telemetry on F3), floating origin rebase mid-
  mission (probe asserts a rebase occurred + no visual pop via position-delta check), escort
  AI tether, long-range ballistics. Perf: streaming hitch budget ≤ 3 ms worst tick.

## 5. Act I probes

Per level: 1-frame render probe both backends; scripted full-path playthrough probe
(waypoint bot: move/interact/kill scripted minimum route — asserts objective chain,
checkpoint saves, par-time feasibility ≤ par×0.8 at bot speed); L2 dual-path probe (ghost vs
loud → distinct event logs, both complete); L3 streaming probe (SetCenter sweep along the
road: residency counts, zero unload-of-visible, rebase count == expected); teardown
leak probes. VM==native + `-O0/-O2` on all.

## 6. Verification gate

All Act I probes green headless; hands-on run of the act start-to-finish on Metal fullscreen
(one sitting, no traps, no soft-locks, checkpoints correct); look pass vs 27 §3 mood boards
(banner screenshots recorded to `lookdev/`); perf tables recorded. Full build green.
