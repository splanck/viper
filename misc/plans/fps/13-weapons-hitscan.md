# 13 — Game: Weapon Framework + Hitscan Family

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1–2 sessions.
> Prereqs: 12-core-loop (view-model mount, actions), 02-audio (E5 pitch), 05-renderer
> (E18 pass, E19 additive sprites). Delivers: the weapon framework and the five hitscan
> weapons — Bolt Pistol, Scattergun, Cycler SMG, Pulse Rifle, Marksman Rail — with full FX,
> feel, and probes. 14 adds the physics/energy family on this framework.

## 1. Framework (`weapons/weapon_base.zia`, `weapon_defs.zia`, `recoil.zia`)

- **Data-driven defs**: parallel-list weapon table (id → all stats; struct rows per the
  bowling `BallWeightSpec` pattern): damage, headshot ×, falloffStart/End (m), pellets, ROF,
  magSize, reserveMax, reloadSec (+stage times), spreadBase/ADS/max/bloomPerShot/recovery,
  recoilPattern id, ADS fov/time, switch times, noise loudness, tracer/muzzle/shell params,
  upgrade hooks (25-meta multiplies through 3 tiers).
- **State machine per weapon instance**: `READY → FIRING → (COOLDOWN) → RELOADING(stages) →
  SWITCH_OUT/IN → CHARGING (rail) → OVERHEAT (lance, 14)`. Fire gating in the fixed tick;
  semi/auto/burst trigger modes; reload stages emit anim events (mag-out/mag-in/chamber) that
  `viewmodel` and audio consume; switch cancels reload after stage 1 (ammo rules exact:
  mag-out commits the mag).
- **Ammo**: mag + reserve per weapon; shared "light/heavy/energy/explosive" reserve classes
  for pickups (config table). HUD binds read-only views.
- **Recoil** (`recoil.zia`): per-weapon authored 2D pattern (List of (dx,dy) points, e.g.
  Pulse Rifle 12-point rising zigzag) applied as camera-offset impulse with recovery spring
  (recovers only the un-compensated remainder — modern-FPS style); view-model kick socket
  gets a matched 3D impulse (back + rot). Spread: `effective = base(stance,ADS) + bloom`,
  bloom decays exponentially; crosshair renders spread live (23-ui).
- **Determinism**: all spread/pellet angles from `rng.spread` stream — combat probes replay
  exactly; pattern indices reset on mag start.

## 2. Ballistics (`weapons/ballistics.zia`)

- Hitscan fire: ray from camera eye through spread-offset direction;
  `Physics3DWorld.Raycast(origin, dir, range, mask)` with layer mask WORLD|ENEMY|PROP —
  friendly/pickup layers excluded (physics3d.md layer masking verified). Hit → resolve handle
  (11 §3) → `applyDamage` with hit region from named collider (head/core/plate/tank tables
  per archetype), impact FX by surface tag.
- **Penetration (Rail)**: `RaycastAll` sorted by distance; walk hits while remaining
  penetration budget > surface cost (config per material: metal 2, rock 3, enemy 1);
  damage ×0.7 per layer; exit-side decal + debris burst on each pierced surface.
- **Falloff**: linear damage scale from falloffStart→End (SMG/Scattergun aggressive, Rail none).
- Muzzle origin vs eye origin reconciliation: trace from eye (accuracy), FX from muzzle socket
  (visual) — tracer bridges the two (industry standard, documented for probe expectations).

## 3. The five weapons (defs + quirks)

| Weapon | Core numbers (tune in P3/P17) | Signature systems |
|---|---|---|
| Bolt Pistol | 26 dmg, 2.0× head, 240 RPM semi, 12 mag, ∞ reserve, tight 0.4° | Silenced mod (upgrade T2): noise 14 m→3 m, damage ×0.85 — the stealth enabler (15-ai integration) |
| Scattergun | 8×9 dmg pellets, fixed 8-point ring+center pattern scaled by spread 4.5°, 70 RPM pump, 6 tube | Per-pellet decals; pump anim gates next shot; point-blank bonus ×1.15 inside 4 m |
| Cycler SMG | 14 dmg, 900 RPM, 36 mag, bloom-heavy hipfire (0.8°→3.2°), fast falloff 12–28 m | Pitch-varied shots ±8 % (E5); tracer every 3rd round; sprint-fire allowed with +50 % spread |
| Pulse Rifle | 22 dmg, 640 RPM, 30 mag, authored 12-pt recoil, ADS 1.25× zoom 0.14 s | The workhorse: recoil-pattern mastery loop; muzzle point light 1-tick (zone-budgeted, 22) |
| Marksman Rail | 95 dmg (charged 160, 2.5× head), charge 0.9 s, 5 mag, penetration budget 4 | 2-stage scope: RT inset at 4× (E20 material on scope lens quad) then full overlay 8×; lowpass focus DSP while scoped (02); breath-hold (Shift) steadies 2.5 s; glint lens-flare visible to player at charge (E40) |

## 4. Impact & muzzle FX (`weapons/impact_fx.zia`)

Surface-tagged tables: metal → sparks burst (Effects3D.Sparks) + `ImpactDecal` + ping SFX;
rock/ash → dust puff + pit decal; enemy → oil-spark mix + armor-clang vs core-thud (locational
audio tell). Tracers: additive stretched sprite (E19/E41 stretch on a 1-particle emitter or
Sprite3D scaled along the segment) fading over 60 ms. Muzzle: 2-frame additive sprite flash
(atlas from `fallbacks.zia` procedural), light impulse into the zone manager, smoke wisp after
sustained fire (SMG/Pulse ≥ 8 rounds). Shell casings: pooled 24 tiny dynamic bodies, sleep
fast, despawn 4 s — pure juice, disabled on Performance tier. Hitmarker event → HUD tick +
pitch-tiered confirm (body/head/kill), damage numbers optional toggle (accessibility).

## 5. Probes

`combat_probe` extensions: (a) spread determinism — 100 SMG shots replay to identical hit
coordinates (rng.spread golden); (b) Scattergun pattern — 8 pellet decals at expected ring
positions ±ε at 10 m; (c) falloff curve — damage at 5/15/30 m matches table; (d) Rail
penetration — 3-plate fixture reports 3 hits with ×0.7 chain and stops at budget; (e) reload
state walk — stage timings, switch-cancel ammo rules; (f) recoil — 12-shot pattern camera
deltas match authored table then recover to ±0.05°; (g) FX budget — 200-shot burst keeps
decals ≤ 64, zero pool growth. All headless, VM==native, `-O0/-O2` diff.

## 6. Verification gate

Probes green headless → arena hands-on (Metal, fullscreen): each weapon vs dummies feels
distinct (checklist in 27-gamefeel §2 scored); frame time flat during 200-shot bursts
(perf_probe delta ≤ 0.5 ms). Zia check clean; full build green. Rail's RT-scope inset renders
live at ≤ 0.3 ms (telemetry) — else falls to overlay-zoom-only pending E20 tuning (noted risk).
