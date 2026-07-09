# 14 — Game: Physics & Energy Weapons, Grenades, Melee, Explosions

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1–2 sessions.
> Prereqs: 13-weapons framework; 04-physics (E13 swept CCD, E16 query capacity); 07 (E41
> trails). Delivers: Arc Launcher, Rivet Driver, Shard Caster, EMP Projector, Helix Lance,
> the three grenades, wrench melee, and the explosion system feeding destructibles (22).

## 1. Projectile system (`weapons/projectiles.zia`)

Pool of 48 (11 §3), two integration classes:
- **Rigid CCD bodies** (Arc rounds, all grenades): `Physics3DBody` sphere r 0.07, mass 0.4,
  `set_UseCcd(true)` (E13 swept TOI — no game-side sweep code), restitution 0.55
  (Arc)/0.2 (frag), collision events drive bounce SFX (pitch by impact speed, E5) and fuse
  logic; spin via `ApplyTorque` on launch (visual).
- **Kinematic swept projectiles** (Rivets, Shard pellets): game-integrated in the fixed tick —
  position += v·dt with `SweepSphere` (r 0.03) along the step; hit → embed/bounce logic.
  No body allocation; pure pool math (fast + deterministic; 90 m/s safe by construction).
Trails: E41 ribbon on rivets (0.25 s, emissive), stretch on shard pellets; despawn returns to
pool (never destroys engine objects — reuses a parked emitter set).

## 2. The five weapons

| Weapon | Mechanic | Key systems |
|---|---|---|
| Arc Launcher | Lobbed CCD grenade, 2.5 s fuse or contact-detonate on ENEMY layer, 2 bounces max then sticks | Launch velocity from camera + arc-preview (23-ui dotted trajectory using the same integrator — bit-identical preview); collision Enter events count bounces |
| Rivet Driver | 3-round burst kinematic darts 85 m/s; embed in WORLD (decal + emissive glow fading 3 s), pin ENEMY (attach: store bone-local offset, 6 dmg/s burn ×4 s stack ≤3) | Swept kinematics; DoT registry in damage pipeline; pinned-Husk visual: rivet quad parented via `AttachToBone` |
| Shard Caster | 5 pellets, 2 ricochets each: reflect dir about hit normal (`d − 2(d·n)n`), −25 % dmg/bounce, ricochet whine pitch ramp | Pure raycast chains (3 rays/pellet max); bounce spark FX; wall-bank kills = mastery skill |
| EMP Projector | Charge-release burst: `OverlapSphere` r 9 m → chain arcs to 4 nearest valid (drones/turrets/Vanguard shields/Wraith cloaks/L4 monitors), disable 4 s | Additive Sprite3D arc chains (E19) jittered per tick; no damage — pure utility; feeds stealth (kills alarms) |
| Helix Lance (L9) | Held beam: per-tick raycast 40 m, 90 dmg/s, heat 0→100 in 3.5 s → 2 s vent; melts WARDEN/HELIX plates ×2 | Beam = 8-segment ribbon (E41) with noise wobble + impact glow particles; loop SFX pitch = f(heat) (E6); heat HUD ring |

## 3. Grenades (`G` cycle: frag / EMP / decoy)

Shared throw: 0.3 s wind-up, camera-relative launch 14 m/s + up 3, cook on hold (frag only,
fuse 3.2 s, HUD tick audible). Frag: blast core (§4). EMP: as projector burst r 7 m on fuse.
**Decoy**: on land, emits looping synthetic chatter + periodic `EV_GUNSHOT(loudness 10)`
hearing events for 8 s — AI investigate magnet (15-ai integration test fixture); Wraith
prioritizes decoys (its counter-tell).

## 4. Explosions (`weapons/explosions.zia`)

`detonate(x,y,z, radius, damage, impulse, instigator)`: `OverlapSphere` (capacity via E16 —
set 1024 at init; assert `Truncated == false` in probes) → per victim: occlusion raycast to
epicenter (blocked = 35 % damage, cover matters), falloff (1 − d/r)^1.5, `ApplyImpulseAtPoint`
on bodies (boxes/casings/enemies stagger), camera shake ∝ proximity (clamped),
`Effects3D.Explosion` + scorch `ImpactDecal` + light impulse (zone-budgeted) + smoke column +
sub-bass thump with distance lowpass (02 occlusion API reused for distance muffle).
**Chains**: explosive barrels (22-destructibles) take blast damage → their own detonate next
tick (event-queued, max 8/tick to bound cascades — deterministic order by handle).

## 5. Melee (`wrench`)

`SweepCapsule` 0.5 m arc over 3 ticks in front of camera; 45 dmg + 3 m·s impulse;
vs unaware enemy (15-ai state) from behind cone 120° → **takedown**: 10× damage, silent,
triggers takedown anim event pair (player view-model swing + enemy stagger-death), no alert
event. Cooldown 0.8 s. The stealth loop's teeth.

## 6. Probes

(a) CCD grenade vs 0.1 m wall at 30 m/s — zero tunnels over 200 throws (leans on E13 suite but
asserts at game speeds); (b) bounce determinism — scripted throw replays identical rest
position ±1 mm; (c) ricochet geometry — pellet into 45° wall reflects to expected target ±ε;
(d) DoT stack — 3 rivets = 18 dmg/s for 4 s exactly, kills log ordered; (e) blast occlusion —
victim behind pillar takes 35 %; impulse magnitudes match table; truncation false with 300
bodies; (f) chain cap — 12-barrel line detonates over ≥2 ticks, deterministic order;
(g) decoy — scripted Husk pathfinds to decoy within 3 s (fixed seed); (h) takedown — behind
cone honored, front rejected, no alert event emitted; (i) pool watermark — 40 simultaneous
projectiles, zero growth. All VM==native + `-O0/-O2`.

## 7. Verification gate

Probes green → arena hands-on: grenade arc preview matches flight; ricochet banks readable;
EMP arcs render across 4 targets; Lance beam + heat loop feel (checklist 27 §2). Perf: 12
simultaneous detonations + full projectile pool ≤ +2.5 ms on Balanced (perf_probe scenario
added). Full build green.
