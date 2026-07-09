# 17 — Game: The Three Bosses

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1–2 sessions.
> Prereqs: 16-enemies (shared substrate), 14-weapons (blasts, CCD), 04-physics (E13/E15),
> 03-shadows (arena lighting). Bosses are choreographed set-pieces: authored phase machines
> on the AI base, arena scripts in their level modules, music integration (24), and unique
> mechanics that test what the player learned. Each gets a health-bar HUD banner, intro
> stinger + camera moment (cinematic spline, skippable), and a no-damage medal hook (25).

## 1. WARDEN Goliath — L7 Foundry (mini-form preview in L4)

Mining mech, 12 m tall. Arena: circular smelter floor, 3 coolant pylons, barrel racks,
low catwalk ring. **HP 2,800 + 6 armor plates ×220** (compound colliders; plates ablate with
physical pop-off — each spawns a debris body).

- **Phase 1 — Siege (100–70 %)**: slow stalk; Mortar volley (3 s telegraph: dorsal hatch
  glow + klaxon → 6 arcing CCD shells, blast r 4 m — dodge by watching impact decal markers);
  Cannon sweep (horizontal beam 1.5 s, duck under via catwalk). Player goal: strip rear
  plates (flank loops around pylons).
- **Phase 2 — Wrath (70–35 %)**: adds Charge — 1.2 s crouch-glow + horn → line charge with
  physics push (`ApplyImpulse` on player controller knock + any barrels it clips detonate on
  it: **environmental damage is the intended DPS**, ×3 vs self-clipped barrels); pylon
  coolant vents (interact) flash-stagger it 4 s when charged through (E15 kinematic pylon
  arms swing). Mites released on plate loss.
- **Phase 3 — Meltdown (35–0 %)**: core exposed (2.5×), arena floor sections heat-glow in a
  rotating pattern (stand = 12 dmg/s — movement test), mortar becomes continuous drizzle,
  charge chains twice. Kill → kneel → chain plate-pops → core column explosion (shake +
  white-out, 27 §feedback caps honored).
- Mini-form (L4): 40 % stats, phases 1 only, no plates — teaches the mortar/charge language
  early; flees at 50 % (scripted door exit) so the L7 rematch reads as a grudge.

## 2. SHRIKE Prime — L8 Spire summit

Gunship. Arena: open rooftop ring, 4 cover fins, 2 mountable **turret pods** (hijacked sentry
guns — the L2+ hack skill pays off), elevator core center. **HP 2,200**, flying (kinematic
lanes + hover nodes).

- **Phase 1 — Strafe (100–65 %)**: figure-8 lanes, rocket pairs (CCD, shootable), mite drops;
  vulnerable in hover-turns (2.2 s, lens-flare glint marks the window, E40).
- **Phase 2 — Suppression (65–30 %)**: adds chin-gun sweeps that shred fin cover
  (destructibles: fins have 400 HP, nav rebuild on loss, E28 — the arena physically opens up),
  landed Shrike-guard waves (2 Shrikes at 20 % stats), EMP-able: a full EMP burst during
  hover drops it to the deck for 5 s (melee/point-blank window; achievement hook).
- **Phase 3 — Talon (30–0 %)**: locks onto the elevator core, drags it (kinematic mesh
  platform tilts 8° — aim under slide), continuous mite stream; turret pods overheat-limited.
  Kill → wing-shear spiral, crashes through the ring edge (scripted kinematic path + blast).
- Anti-air ammo economy: heavy/energy crates on a respawn timer — forces movement across
  the ring under strafes.

## 3. HELIX Avatar — L9 Core

Not a body — the room. A 20 m reactor column with **3 manipulator arms** (kinematic mesh
chains, E15) + a traveling **consciousness core** (the only damageable part, 3× glow).
**HP 3×900 (per phase core)**. Music: the 24 §boss suite's three-movement piece is
phase-synced.

- **Phase 1 — Manipulation**: arms sweep/slam (floor shock rings), core rides arm tips —
  hit it during slam recovery; HELIX dialogue (radio hijack) reacts to player weapon choices
  (barked lines table — presentation, not logic).
- **Phase 2 — Carousel**: core enters the column; ring of 8 shield pods orbits (Vanguard-
  shield material) — Helix Lance (acquired at arena start from Mara's drop pod) melts pods
  ×2 speed; between-pod gaps expose the core in a rotating firing-solution puzzle; arms now
  grab-and-throw barrel racks (dodge telegraphs by floor decals); floor sections vent
  (heat pattern from WARDEN language, escalated).
- **Phase 3 — Collapse**: core detaches, erratic flight (drone steering ×3 scale), room
  integrity fails — **escape-run overlap**: damage the core while platforming out as sections
  fall (kinematic choreography timeline), motion blur + shafts + shake at authored caps;
  final hit triggers the ending cinematic (22 §objectives handoff).
- Fail-safes: phase checkpoints (death restarts current phase), core hit-flash + damage
  numbers always on for boss cores (readability override).

## 4. Shared boss systems (this doc implements once)

`ai/boss_base` pattern inside each module (no premature abstraction — three bespoke FSMs
sharing: phase table walker, HP-banner binding, telegraph scheduler (tick-exact, probe-
readable), arena-script hooks (spawn waves, hazards on/off), music-cue emission, intro/outro
cinematic triggers, checkpoint integration).

## 5. Probes

Per boss, scripted no-input observation + scripted optimal-play runs (fixed seeds):
(a) phase thresholds fire at exact HP crossings, telegraph→strike timings match tables;
(b) WARDEN: plate ablation order free, barrel-clip detonations register ×3, coolant stagger
window honored; (c) SHRIKE: rocket CCD never tunnels the deck, fin destruction rebuilds nav
(agents re-path), EMP-drop window; (d) HELIX: pod-gap exposure windows match rotation math,
collapse timeline deterministic, escape-run completable at min movement speed +10 % margin
(auto-verified by scripted run); (e) full fights replay identical event logs VM==native;
(f) perf: worst boss moment ≤ Balanced-tier frame budget on the perf harness scene clone.

## 6. Verification gate

Probes green → hands-on each fight on Metal fullscreen: readable telegraphs (name every
attack from its tell — 27 §2 checklist), no unfair deaths in 5 clean attempts, music sync
lands phase hits. Full build + `-O0/-O2` diff green.
