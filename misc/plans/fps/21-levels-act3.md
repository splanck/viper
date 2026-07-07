# 21 — Game: Act III (Foundry, Spire Ascent, Helix Core) + Ending

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · **2-session chunk**
> (session A: L7 + L8; session B: L9 + ending flow + act probes). Prereqs: 17-bosses,
> 04-physics (E13/E15), full weapon/enemy rosters, 24-audio boss suites.

## 1. L7 — The Foundry (physics gauntlet + low-g, 18 min)

- **Look**: industrial inferno — molten channels (emissive + heat-shimmer refraction fake:
  scrolling normal-mapped transparent planes), sodium-orange LUT `l7_forge`, ember particle
  columns (stretch E41), silhouetted machinery against furnace glow (strong rim contrast per
  27 §3 silhouette rules).
- **Flow**: freight intake (conveyor sorting floor — kinematic mesh belts E15 carry player/
  crates/enemies; combat on moving ground) → press hall (timed **crusher** lanes — kinematic
  crushers with authored timelines; Husk packs herded into presses = environment-kill teach,
  salvage bonus) → smelt bridge (Marauder duel on a hinge-jointed swaying span — joints
  showcase; shootable counterweights tilt it) → **mag-lift shafts**: three low-g chambers
  (per-zone gravity 2.2 m/s²) — float-jump traversal, air-control combat vs Drones/Shrike
  pods, grenades arc long (arc-preview teaches) → barrel-yard sandbox (chain-detonation
  playground guarding the armory: Helix Lance *locked* teaser case) → **WARDEN Goliath**
  arena (17 §1) → aftermath cinematic + checkpoint.
- **Showcase**: E15 kinematic meshes (belts/crushers/bridge), joints, per-zone gravity,
  environment-as-weapon, the first full boss.

## 2. L8 — Spire Ascent (vertical gauntlet, 15 min)

- **Look**: dawn breaking during the climb (scripted sun rise across the mission — the
  day-cycle system's cinematic use), god-rays through superstructure (E39), lens-flare sun
  (E40), LUT `l8_dawn` (cold→gold gradient swap at the summit reveal), the valley below as
  vista (L3's streamed cells visible at far LOD — continuity flex, impostor tier E30).
- **Flow**: base atrium breach (full-roster squad fights in a rising stack of PVS zones) →
  **the elevator core**: three staged rides on a kinematic platform (E15) with waves boarding
  from passing floors (director lanes; Vanguard shield-walls at door reveals; fight-in-a-box
  design), between rides: maintenance climbs (Stalker links, exterior wind gusts push) →
  counterweight puzzle (shoot rope-joint clamps to drop counterweights → unlock express
  ride — joints again, readable) → summit ring **SHRIKE Prime** (17 §2) → antenna override,
  Candela hails, descent-to-core cinematic.
- **Showcase**: elevator combat choreography, dawn lighting arc, the aerial boss, vista
  continuity (streamed world seen from above).

## 3. L9 — Helix Core (finale, 14 min + ending)

- **Look**: reactor sanctum — clean white-blue HELIX aesthetic corrupted by ash-black growth
  veins (emissive pulse mapped to boss phase), LUT `l9_core` (clinical cyan, warm only on
  damage states), volumetric-feel via layered fog + shafts from the core column, motion blur
  authored to peak **only** in the escape run (per-sequence postfx override API in `fx/postfx.zia`).
- **Flow**: descent gallery (HELIX monologue, environmental storytelling: the offer replayed
  on monitors E20) → antechamber gauntlet (mirror-match squad: every archetype, LOCKDOWN
  rules, the game's hardest set fight — checkpoint before/after) → **Mara's drop pod**: Helix
  Lance delivery + suit overcharge (shield +50 — difficulty valve) → **HELIX Avatar** (17 §3,
  three phases, checkpointed per phase) → **escape run**: 90 s collapsing-corridor sprint
  (kinematic collapse timeline, slide/jump chains from L1's teach, motion blur + shake caps,
  the Candela's searchlight as the goal beacon E40 flare) → extraction cinematic → epilogue
  beat at the Redoubt (survivors board; the Kid returns Rook's wrench — callback) → credits.
- **Ending flow** (`game.zia` CREDITS state): stats card (25-meta: time, accuracy, medals,
  secrets, deaths), credits scroll over lookdev stills + music suite, unlocks NG+ mutators +
  arena wave-mode-plus, return-to-title.
- **Showcase**: everything, deliberately — the finale is the regression suite made flesh.

## 4. Act III probes

L7: belt-carry physics (player/crate velocities inherit belt), crusher kill volumes exact,
low-g arcs golden, bridge joint stability soak (5,000 ticks no NaN/explosion — E13/joints);
L8: elevator wave choreography deterministic, counterweight drops unlock exactly, rising-zone
PVS/light budgets hold during rides; L9: gauntlet completable by bot at Soldier, boss phase
probes (17 §5), escape-run timeline completable at min-speed +10 %, per-sequence postfx
overrides apply/revert cleanly; ending: stats math golden, NG+ flags persist through save
round-trip. All: render probes ×2 backends, full-path bots, teardown leaks, VM==native,
`-O0/-O2`.

## 5. Verification gate

Act III hands-on (one sitting, Metal fullscreen); full-campaign hands-on L1→L9 immediately
after (the first complete-game run — soft-locks/checkpoint audit); escape run feel check
(27 §2). Probes + full build green. Campaign-complete banner screenshot set recorded to
`lookdev/` (one per level — becomes the README gallery in P20).
