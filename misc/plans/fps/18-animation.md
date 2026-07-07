# 18 — Game: Animation — Rigs, Blend Trees, Layers, Events, IK

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1–2 sessions.
> Prereqs: 08-anim (E26 freeform 2D, E27 retarget maps, E29 link state), 26-assets (packs +
> alias tables), 16-enemies session A (intent stream). Delivers `anim/rigs.zia`, `hooks.zia`,
> `ik.zia`: every skinned archetype's controller graph, the anim-event wiring, and IK rigs.
> Engine surface verified SOLID in validation: AnimController3D layers/root-motion/events
> (`rt_animcontroller3d_api.inc:352-473,687-883`), BlendTree3D, IKSolver3D, Retarget,
> `DrawMeshSkinned` accepts controllers directly (ridgebound N4).

## 1. Rig inventory & sources

| Rig | Source (26) | Clips needed (retargeted via E27 alias tables) |
|---|---|---|
| Humanoid-frame (Husk, Ranger, Vanguard, Marauder scale-up, Wraith) | Quaternius Animated Character / KayKit skeleton-crew style GLB | idle, walk, run, strafe L/R, back, crouch-walk, aim-pose, fire-add, reload-add, melee ×2, hit L/R, stagger, leap, death ×3, spawn-rise |
| Quadruped (Stalker) | Quaternius robot quadruped | idle, prowl, sprint, leap (root motion), pounce-pin, hit, death-tumble |
| Player arms (view-model) | Kenney Blaster-kit guns + procedural arms v1 (capsule arms; GLB arms if pack provides) | per-weapon: idle, fire, reload stages, inspect, melee swing — **procedurally posed first** (12 §4 motion stack), clip-based only where packs supply |
| Non-skinned (Drone, Turret yoke, Mites, Shrike, bosses) | node TRS animation (`NodeAnimator3D`) + game transforms | hover-bob, scan-sweep, spin-up, hatch-open... authored as node keyframes in `rigs.zia` (Animation3D.New/AddKeyframe — critters.zia precedent) |

Every rig has a **fallback body** (16) that uses the same intent stream with pose-snap
animation (no controller) — the graph layer degrades, gameplay timing does not.

## 2. Humanoid controller graph (built once in `rigs.zia`, instanced per AI)

- **Base layer — locomotion**: freeform-directional 2D BlendTree (E26): 9 samples
  (idle center; walk/run × F/B/L/R ring) parameterized by local-space velocity; crouch
  variant tree; auto-crossfade 0.15 s between stance trees. Speed-sync: playback rate scales
  with actual/authored speed (±20 % clamp) — kills foot-skate without foot IK.
- **Aim layer (additive, upper-body mask)**: `PlayLayerAdditive` aim-pose ±60° pitch sweep
  sampled by camera-relative aim; `SetLayerMask` from spine-01 up (mask tables per skeleton
  in config; alias-mapped names).
- **Action layer (masked replace)**: fire-add (0.1 s), reload stages, melee swings — events
  drive gameplay (below). Full-body overrides: stagger, leap (root motion ON via
  `SetRootMotionBone(hips)` + `ConsumeRootMotion` feeding NavAgent displacement during
  LEAP/lunge states — E29 link state triggers), deaths (kind-picked), spawn-rise.
- **Look-at IK** (`ik.zia`): head LookAt solver weight 0.7 within ±70°, target = player when
  SUSPICIOUS+; Turret yoke uses LookAt on pan/tilt bones; Marauder torso twist 0.3.
  TwoBone reserved for Wraith blade-plant moment (stretch, cut-safe).
- **Anim LOD**: > 25 m half-rate updates + IK off; > 45 m quarter-rate (engine
  `SetAnimationLOD` + game-side stagger already in 15).

## 3. Event wiring (`hooks.zia`)

`AddEvent` markers per clip (authored as time fractions in config tables since downloaded
clips lack markers): footstep L/R (→ noise events + audio surface variation), fire-frame
(muzzle FX sync for enemy weapons), melee-hit-frame (damage window open/close), reload
mag-out/in/chamber (audio + view-model), leap-takeoff/land, death-settled (ragdoll-lite
impulse handoff). Drained via `PollEvent` **loop-until-empty** (landmine rule) each tick into
the event bus. Probe: scripted walk = exact footstep count; melee damage only lands inside
hit-frame window.

## 4. Player view-model animation

Procedural stack (12 §4) is the base; clip layer on top where available: fire kick clips per
weapon (else procedural recoil pose), reload stage clips (else keyframed socket choreography
authored in `rigs.zia` — visible mag path, 0.9–2.4 s per weapon), inspect flourish (idle 12 s
→ once), melee swing. ADS pose table per weapon (socket transform + FOV). Weapon swap:
holster-draw crossfade 0.25/0.3 s with event-gated availability.

## 5. Retarget pipeline (with 26-assets)

Per pack: alias table (`Skeleton3D.SetBoneAlias` rows in config — e.g. `mixamorig:Hips→hips`),
`RetargetWithMap` at load in `assets/registry.zia`, warnings surfaced via
AssetDiagnostics3D → dev overlay (zero unmapped bones = green row; probe asserts green for
shipped packs). Clip trimming/rate tables per source clip (downloaded clips have padding).

## 6. Probes

(a) Locomotion math: velocity (0.7,0.7) → exactly 3 active clips (E26 property), rates
speed-synced; (b) layer masks: fire-add leaves legs' pose untouched (bone transform deltas
zero below hips during locomotion+fire); (c) root-motion leap: displacement equals clip
root delta ±ε, agent lands on link exit point; (d) events: footstep/hit-frame counts golden;
(e) retarget: shipped-pack alias tables produce zero warnings; clip golden poses at t=0.5
within tolerance vs source; (f) LOD: rate switching at distance thresholds (update counters);
(g) determinism VM==native on a 1,000-tick anim-heavy scene; (h) perf: 12 skinned AIs +
player arms ≤ 2.2 ms anim+skin on M-class Balanced (recorded; CPU-skin path on SW recorded
separately — informs SW enemy cap in 22 quality tiers).

## 7. Verification gate

Probes green → visual pass on Metal: strafe-circle a Ranger — no foot-skate at any angle,
aim tracks smoothly, fire never disturbs gait; Husk lunge reads as weight (root motion);
Stalker vent-leap chains link→anim→land cleanly. 27 §2 feel checklist rows scored. Full
build green.
