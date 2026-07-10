# Plan 08 — World3D Time Scale, Hit-Stop, Pause

## 1. Objective & scope

Combat feel and menus both need time control the runtime currently lacks entirely (verified: no time-scale API in Game3D). Add world-level `timeScale`, a latched `paused` state, and a one-shot `hitStop(seconds)` — all applied at one choke point so controllers, animation, physics, effects, and audio-binding sync scale coherently and deterministically.

**In scope:** (a) `World3D.timeScale` / `paused` / `hitStop`; (b) coherent application in `stepSimulation` and the fixed-step accumulator; (c) unscaled-time accessors for UI.
**Out of scope:** per-entity time scaling, audio pitch shifting (mixer-side; note as a follow-up in plan 24's file), slow-motion post-FX.

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **No time scale exists:** grep `time_scale|timescale` across `rt_game3d*.c/.inc` returns nothing.
- **Single choke point:** `game3d_world_step_simulation_impl` (`rt_game3d.c:1711`) receives the frame step, clamps it, stores `world->dt`, advances `frame`/`elapsed`, then runs controllers → behaviors → animations → physics → sync → audio prune → effects → late controllers (documented order, `game3d.md` §Frame Order). Every consumer reads the same clamped dt — one multiplication site covers all of them.
- **Fixed-step loop:** `World3D.runFixed` accumulates real dt and drains fixed steps with a spiral guard (`droppedFixedSteps`, `fixedInterpolationAlpha`; `game3d.md` §World3D Defaults). Physics has its own accumulator twin `rt_world3d_step_fixed` (`rt_physics3d.h:65`).
- **Timing state:** `world->dt`, `world->elapsed`, `world->frame` advance in `tick()`/`stepSimulation` (`game3d.md` §Frame Order); overlay/UI code reads these today.
- **Determinism gate:** `game3d.md` documents the replay/parity suite any simulation-semantics change must rerun.

## 3. Design

### 3.1 State and semantics

World fields: `time_scale` (default 1.0, clamped [0, 4]), `paused` (i8), `hitstop_remaining` (seconds, real time). Effective scale each frame:

```
effective = paused ? 0.0 : (hitstop_remaining > 0 ? 0.0 : time_scale)
```

`hitStop(seconds)` sets `hitstop_remaining = max(remaining, seconds)` (non-stacking, max-latch — repeated hits don't freeze forever). `hitstop_remaining` decrements by **real** (unscaled) dt so a hit-stop always ends.

### 3.2 Application points

- **`stepSimulation(step)`:** scale once at entry — `scaled = clamp(step) × effective` — then run the existing pipeline with `scaled`. `world->dt` stores the *scaled* value (gameplay sees scaled time; that is the point), `world->elapsed` advances by scaled dt. New `world->unscaled_dt` + `world->unscaled_elapsed` advance by the real clamped step for UI/menus.
- **`runFixed`:** the accumulator gains `real_dt × effective`, so at `timeScale 0.5` fixed steps fire half as often — fixed-step *size* never changes (determinism: simulation always sees identical fixed dt). Pause simply stops accumulation; `fixedInterpolationAlpha` freezes (render interpolation holds the last pose — no drift).
- **Zero-step skip:** when `effective == 0`, skip the pipeline entirely except: input tick (already outside), effect-registry expiry **not** advanced (particles freeze — correct for pause and hit-stop), and late camera controller still runs with dt 0 so resize/aspect stays live. Animator/audio/physics untouched ⇒ frame is a pure re-render.
- **Direct `Physics3DWorld` users:** untouched — scaling lives in the Game3D facade only (raw users pass their own dt), which keeps the physics runtime semantics-free.

### 3.3 API

`timeScale` (get/set prop), `paused` (get/set prop), `hitStop(seconds)`, `unscaledDt`/`unscaledElapsed` (get props). Setter sanitization: non-finite → ignored (existing transform-setter convention).

## 4. Implementation steps

1. World fields + props + sanitization; getters wired.
2. `stepSimulation` scaling + zero-step skip path; C unit tests.
3. `runFixed` accumulator scaling + interpolation-alpha freeze; unit tests.
4. `hitStop` latch + real-time decay; unit test.
5. runtime.def + audits + docs (`game3d.md` §Frame Order addendum + new §Time Control) + ADR note (simulation-semantics change ⇒ GATE-009-style proof note per `game3d.md`).
6. Zia probe `g3d_test_game3d_timescale_probe`; rerun the full determinism gate.

## 5. Public API changes (runtime.def)

`Viper.Game3D.World3D` additions (RT_FUNC per accessor, then in the class block):

```
RT_PROP("timeScale","f64", Game3DWorldGetTimeScale, Game3DWorldSetTimeScale)
RT_PROP("paused","i1",     Game3DWorldGetPaused,    Game3DWorldSetPaused)
RT_PROP("unscaledDt","f64",      Game3DWorldGetUnscaledDt, none)
RT_PROP("unscaledElapsed","f64", Game3DWorldGetUnscaledElapsed, none)
RT_METHOD("hitStop","void(obj,f64)", Game3DWorldHitStop)
```

(Case follows World3D's existing lowerCamel property style: `dt`, `elapsed`, `floatingOrigin`.) No new classes/files. ADR/proof note per the determinism-gate policy.

## 6. Tests

- **Scale (C unit):** Given a falling dynamic sphere — When 60 steps at `timeScale 0.5`, step 1/60 — Then displacement equals 30 real steps of the unscaled run within 1e-9 for the fixed-loop path (identical fixed dts, half count) (fail-before: no API).
- **Pause:** `paused=true` ⇒ body position, animator state time, particle ages, `elapsed` all frozen across 60 ticks; `unscaledElapsed` advances; render still produces frames (capture non-null).
- **Hit-stop:** `hitStop(0.1)` at 60 Hz freezes exactly 6 frames then resumes; a second `hitStop(0.05)` during the window does not extend past the first.
- **Interpolation:** with RenderInterpolation on, pausing mid-accumulator holds a stable pose (two consecutive captures byte-identical).
- **Determinism gate:** worker replay, `test_rt_game3d`, native parity suite — all green with scale 1.0 (no behavior change when unused), plus a scaled replay ×2 bit-identical.

## 7. Verification gates

Full build + ctest; determinism gate is the headline (this touches `stepSimulation` semantics); `-L graphics3d`; `-L slow`; surface audits.

## 8. Risks & constraints

- **`world->dt` meaning changes** (scaled) — audit in-tree consumers (controllers, behaviors, showcase samples) — all want scaled time; only UI wants unscaled, hence the new accessors. Call this out in release notes.
- **Effects freeze on pause** is a deliberate choice; a pause menu wanting live particles draws its own overlay effects.
- **Never scale inside the physics runtime** — keeps `Physics3DWorld` deterministic contracts and existing tests untouched.
- **Audio:** voices keep playing under pause (menu music continues; positional one-shots finish). Documented; per-voice pause hooks are plan 24 territory if demanded.
