# ADR 0079: Game3D Time Control (TimeScale, Pause, Hit-Stop)

Date: 2026-07-10

## Status

Accepted

## Context

Combat feel and menus both need time control the runtime lacked entirely.
Every simulation consumer reads the same clamped dt from one choke point
(`game3d_world_step_simulation_impl`), so one multiplication site can scale
controllers, behaviors, animation, physics, scene sync, and effects
coherently.

## Decision

World3D gains `TimeScale` (clamped 0–4), `Paused`, `HitStop(seconds)`
(max-latched, decays by real time with an epsilon clamp so N frames of
1/N-second decay expire exactly), and `UnscaledDt`/`UnscaledElapsed` for UI.
The effective scale is applied where each loop acquires real time — `tick()`,
the public `stepSimulation`, and `runFramesOnly` — so `world->dt` and
`Elapsed` are **scaled** everywhere gameplay reads them. A zero effective
scale takes a dedicated paused-frame path (async asset commits still drain;
the camera late-update runs with dt 0 so resize/aspect stays live) because the
legacy dt clamp resurrects non-positive steps as a full default step. The
fixed-step loop accumulates scaled time while the fixed step *size* never
changes, so simulation determinism is untouched; `fixedInterpolationAlpha`
freezes naturally under pause. Scaling lives entirely in the Game3D facade —
raw `Physics3DWorld` users pass their own dt.

## Consequences

`world->dt` now means scaled time (the point of the feature); UI reads the new
unscaled accessors. With scale 1 the code path is numerically identical to the
old one, so existing determinism suites pin no-change behavior. Effects freeze
under pause by design; pause menus draw their own overlay effects. Audio
voices keep playing under pause (documented; per-voice pause hooks are the
audio plan's territory). Covered by four scenarios in
`test_rt_game3d_ragdoll_time` and the combat Zia probe's time-control section.
