# 12 — Game: Core Loop — Player Controller, Camera, Input, Graybox Arena

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1–2 sessions.
> Prereqs: 11-architecture skeleton; 01-input (E1 raw look, E3 fullscreen); 10-toolchain.
> Deliverable: **the game feels great with nothing in it** — move/look/jump/crouch/slide in a
> graybox arena at 60 Hz, fullscreen by default, with the perf harness attached. This phase is
> where hand-feel is tuned; everything later inherits it.

## 1. Player controller (`player/playerctl.zia`)

Built on `Character3D` (capsule r 0.35, h 1.8, step 0.35, slope 50°) — engine handles
step-up/slope/ground (`rt_physics3d_character.c:47-175` verified). Game adds:
- **Movement model** (config-tuned): ground accel 90 m/s², air accel 14, friction 8 (halved
  on slide), max walk 5.2, sprint 7.6 (stamina-free; sprint is a stance), crouch 2.6.
  Velocity is game-owned; `Character3D.Move` receives the integrated displacement per tick.
- **Jump/coyote/buffer**: jump v 4.6; coyote 0.1 s after leaving ground; 0.15 s input buffer;
  `get_JustLanded` triggers landing dip + FX. No double jump (kept for a possible NG+ mutator).
- **Crouch-slide**: sprint+crouch → 0.8 s slide (friction drop, capsule shrink to 1.2 m,
  camera drop with spring), cancels into crouch; slide-jump preserves speed (skill tech).
- **Stances** drive noise radius (stealth, 15-ai): sprint 14 m, walk 7, crouch 2.5.
- Footsteps: distance-accumulator, surface from ground-hit material tag (level data), rate by
  stance; emits `EV_GUNSHOT`-family noise events at stance loudness (hearing reuse).

## 2. Camera rig (`player/camera_rig.zia`)

- `Camera3D` FPS mode (`FPSInit/FPSUpdate` exist — runtime.def 14178-14183) but the rig owns
  yaw/pitch directly from `Input3D.LookAxis` (raw mode E1; sensitivity from settings,
  invert-Y option) — pitch clamp ±89°.
- Eye height 1.62 (crouch 1.05) with 12 Hz spring; **manual render interpolation**: sim
  produces (posPrev, posCur); overlay-phase camera uses `lerp(prev, cur, FixedInterpolationAlpha)`
  — the one place render-state math is allowed outside the fixed tick.
- Feel layers (each toggleable in accessibility): landing dip (spring impulse ∝ fall speed),
  sprint FOV +6° (0.25 s ease), slide roll 2°, damage directional kick, `Camera3D.Shake`
  budgeted through a single shake manager (max amplitude clamp; scale setting 0–150 %).
- ADS: FOV lerp to per-weapon zoom over 0.12–0.22 s; look sensitivity scales by FOV ratio
  (consistent flick distance).

## 3. Input map (`Input3D` + rebinds)

Actions table in config (action → default key + pad button): move WASD/left stick, look
mouse/right stick (E2 `BindPad`), jump Space/A, crouch-slide Ctrl/B (hold or toggle setting),
sprint Shift/L3, fire LMB/RT, ADS RMB/LT (hold or toggle), reload R/X, interact E/Y (hold
0.25 s for heavy interacts), weapon 1–0 + wheel, grenade G/RB (cycle type Q), melee V/R3,
flashlight F, photo mode F10 (dev), diagnostics F3, pause Esc/Start. Rebind screen (23-ui)
writes into the same table; probes drive actions, not raw keys, via a thin
`actions.pressed(A_FIRE)` layer over `Input3D` — synthetic input then only needs key pushes.
Fullscreen: start via `World3D.NewFullscreen` (E3) unless `--windowed`/smoke; F11 toggles;
mode persisted in settings.

## 4. View-model mount (`player/viewmodel.zia` — stub here, weapons fill it)

Establishes the E18 pass call order (after DrawEffects, before EndScene), the weapon socket
transform (offset config per weapon), and the **procedural motion stack** (evaluated in
overlay-time from interpolated inputs, additive springs): sway (look-velocity lag, 2D spring
k=140 ζ=0.9), bob (Lissajous from move speed, x=2ωt y=ω... amplitude 0.006), landing dip
share, recoil kick socket (weapons drive), ADS lerp to centered pose. Numbers land in config
with comment ranges — this stack is THE feel signature; 27-gamefeel owns final tuning pass.

## 5. Graybox arena (`world/arena.zia` v0)

40×40 m box canyon: floor plane + perimeter walls (prototype-grid procedural material,
Kenney-prototype style via `assets/fallbacks.zia`), crates/ramps/catwalk for movement tuning,
8 static target dummies (cylinder + head sphere, named hit regions) that report hits/flash,
3 lighting setups cyclable by dev key (noon sun CSM / dusk / interior 12-light) for feel
checks under each. This map never ships in the campaign but never dies — it is the standing
perf + tuning scene (perf_probe).

## 6. Perf harness (`probes/perf_probe.zia`)

Arena + N spinning instanced crates + M skinned dummies + particle storm, parameterized;
prints `fps= frame_ms= draws= culled= lights= bodies=` lines (open-world slice probe format,
`perf_macos_apple_m4_max.md` precedent). Baselines recorded at P1 into 28-phasing §4 table;
every later phase re-runs and diffs ≥ −10 %.

## 7. Verification gate

Headless: `smoke_probe` extends to scripted 600-tick movement course (walk/sprint/jump/slide
along waypoints; assert final position ±1 cm, jump apex, slide distance, footstep-event count
— catches feel-regressions numerically). `combat_probe` v0: 100 pistol shots at dummy →
100 hit events (hitscan wired in 13 but the dummy + hit-region resolve land here).
VM==native; `-O0/-O2` diff (E35). On-hardware (Metal): 60 FPS locked in arena, fullscreen
default, raw-look smooth, input latency subjectively ≤ 1 frame (fire-to-flash probe via
synthetic timestamp diff recorded). Windowed flag + F11 verified.
