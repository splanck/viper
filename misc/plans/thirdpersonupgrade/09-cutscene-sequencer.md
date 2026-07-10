# Plan 09 — `Timeline3D` Cutscene Sequencer

## 1. Objective & scope

The cinematics pillar: a multi-track, skippable, engine-ticked sequencer for in-engine cutscenes — camera cuts and spline moves, entity animation, audio, subtitles, letterbox/fade, and event markers. Nothing like it exists in the 3D runtime; `Viper.Game.AnimTimeline` proves the track/marker/polling conventions but knows nothing about cameras, worlds, or 3D animation.

**In scope:** (a) `Viper.Game3D.Timeline3D` with seven track types; (b) world integration (tick, camera ownership, skip); (c) letterbox/fade/subtitle overlay rendering; (d) polled event markers.
**Out of scope:** an IDE timeline editor (future ViperIDE work), branching dialogue (plan 25 consumes subtitles), video tracks (`Viper.Graphics.VideoPlayer` already covers pre-rendered cutscenes).

**Zero external dependencies — absolute.**

## 2. Current state (verified anchors)

- **Convention source:** `Viper.Game.AnimTimeline` (`rt_animtimeline.c/h`) — `add_marker`, `events_fired_count`, `event_fired_id`, `poll_events` (`rt_animtimeline.h`), play/pause/stop/advance lifecycle. Timeline3D mirrors this polling surface exactly.
- **Camera primitives:** `rt_camera3d_set_position/look_at/set_fov/shake` (`rt_camera3d.c`); plan 10 contributes the shared spline evaluator (`Path3D` sampling: `rt_path3d_get_position_at/get_direction_at`, `rt_path3d.h`).
- **Controller suspension point:** the world holds one installed camera controller ticked in `game3d_world_step_simulation_impl` (`rt_game3d.c:1711`) — the timeline suspends it while playing (skip restores).
- **Animation:** `rt_anim_controller3d_play/crossfade` via `Animator3D` (`game3d.md` §Animator3D); entity resolution by name via `World3D.findEntityOption`.
- **Audio:** `Sound3D.play2D/playAt` (`game3d.md` §Sound3D) — track fires clips at their start times.
- **Overlay pass:** final overlays draw after post-FX (`Debug3D` path, `game3d.md` §Debug3D); `Canvas3D.DrawRect2DAlpha` exists for letterbox bars and fades (used by GameBase3D transitions, §GameBase3D). Subtitle text via the overlay text path GameUI/HUD widgets use.
- **Time base:** plan 08's scaled time — the timeline advances by the world's scaled dt so `hitStop`/pause behave sensibly inside cutscenes.

## 3. Design

### 3.1 Data model

New C `src/runtime/graphics/3d/rt_game3d_timeline.c`. A timeline is an immutable-after-play track list plus a playhead:

| Track | Add call | Fields |
|-------|----------|--------|
| Camera cut | `addCameraCut(t, pos, lookAt, fov)` | pose applied at `t`, holds until next camera key |
| Camera spline | `addCameraMove(t0, t1, path, lookTarget, ease)` | `Path3D` + look = Vec3 \| Entity3D \| second Path3D; ease in {linear, smoothstep, easeIn, easeOut} |
| FOV ramp | `addFovRamp(t0, t1, fov0, fov1, ease)` | lerped over the span |
| Anim | `addAnim(t, entityName, stateName, crossfadeSeconds)` | fires `Animator3D.crossfade` at `t` |
| Audio | `addAudio(t, clip, positional, posOrEntityName)` | `play2D` or `playAt`/attached at `t` |
| Subtitle | `addSubtitle(t0, t1, text)` | rendered by the overlay hook; plan 25 replaces raw text with localization keys |
| Letterbox/fade | `addLetterbox(t0, t1, amount)` / `addFade(t0, t1, a0, a1)` | overlay bars / full-screen alpha |
| Marker | `addMarker(t, id)` | polled events, AnimTimeline-style |

All `add*` validate `t` ordering lazily (sorted at `play()`); fire-once tracks keep a fired flag reset by `stop()`/`play()`.

### 3.2 Playback and world integration

- `World3D.playTimeline(tl)` installs it as the world's active timeline (one at a time; replacing stops the old one). Ticked inside `stepSimulation` after controllers-suspension check and before animations, advancing by scaled dt.
- While active with any camera track: the installed camera controller's update/lateUpdate are skipped (suspended, not detached); the timeline writes the camera in the lateUpdate slot so it observes post-physics entity poses for look-targets.
- `skip()`: jump playhead to duration — fire-once tracks past the playhead fire in order (anim/audio events are *not* replayed audibly: audio tracks past-fire silently, anim tracks apply their final state) so world state lands where the cutscene ends; `skippable` (default true) gates it.
- `stop()` restores the controller; `finished` prop + `justFinished` one-shot flag; markers polled via `eventsFiredCount()/eventFiredId(i)`.

### 3.3 Overlay rendering

`World3D` overlay hook: when a timeline is active (or fading), after user overlay callbacks draw letterbox bars (`DrawRect2DAlpha`, top/bottom, `amount` fraction of height), fade quad, and the active subtitle centered at 85% height using the HUD text path. No new render features.

## 4. Implementation steps

1. Track storage + add-API + sort/validate at play; C unit tests on track ordering/firing math (no world).
2. World integration: active-timeline slot, tick, controller suspension/restore; camera cut + FOV tracks; unit test with a scripted world.
3. Camera spline track over the plan-10 evaluator (shared C helper `g3d_spline_eval`); look-target resolution (Vec3/entity/path).
4. Anim + audio fire-once tracks; `skip()` fast-forward semantics.
5. Letterbox/fade/subtitle overlay hook.
6. Markers + polling + `justFinished`.
7. runtime.def + audits + ADR + docs (`game3d.md` new §Cutscenes) + Zia probe.

## 5. Public API changes (runtime.def)

```
RT_FUNC(Game3DTimelineNew, rt_game3d_timeline_new, "Viper.Game3D.Timeline3D.New", "obj(obj)")
RT_CLASS_BEGIN("Viper.Game3D.Timeline3D", Game3DTimeline3D, "obj", Game3DTimelineNew)
    RT_PROP("duration","f64",get) RT_PROP("time","f64",get) RT_PROP("playing","i1",get)
    RT_PROP("skippable","i1",get/set) RT_PROP("finished","i1",get)
    RT_METHOD("addCameraCut","obj(obj,f64,obj<Viper.Math.Vec3>,obj<Viper.Math.Vec3>,f64)",…)
    RT_METHOD("addCameraMove","obj(obj,f64,f64,obj<Viper.Graphics3D.Path3D>,obj,i64)",…)
    RT_METHOD("addFovRamp","obj(obj,f64,f64,f64,f64,i64)",…)
    RT_METHOD("addAnim","obj(obj,f64,str,str,f64)",…)
    RT_METHOD("addAudio","obj(obj,f64,obj,i1,obj)",…)
    RT_METHOD("addSubtitle","obj(obj,f64,f64,str)",…)
    RT_METHOD("addLetterbox","obj(obj,f64,f64,f64)",…) RT_METHOD("addFade","obj(obj,f64,f64,f64,f64)",…)
    RT_METHOD("addMarker","obj(obj,f64,i64)",…)
    RT_METHOD("skip","void(obj)",…) RT_METHOD("stop","void(obj)",…)
    RT_METHOD("eventsFiredCount","i64(obj)",…) RT_METHOD("eventFiredId","i64(obj,i64)",…)
    RT_METHOD("justFinished","i1(obj)",…)
RT_CLASS_END()
```

Plus `World3D.playTimeline(tl)` / `activeTimeline` (get) / `stopTimeline()`. All `add*` fluent (`obj` return). Leaf `Timeline3D` unique (2D class is `AnimTimeline`). New file → source-health bump; ADR `00xx-game3d-timeline.md`.

## 6. Tests

- **Firing math (C unit):** anim track at t=1.0 fires exactly once when the playhead crosses it, regardless of step size (0.4 s steps included) (fail-before: no API).
- **Camera ownership:** installed FollowController camera position is untouched while a camera-cut timeline plays; restored control next step after `stop()`.
- **Spline move:** camera follows `Path3D` positions within 1e-6 of the evaluator; look-entity target reads post-physics pose (entity moved by physics during playback).
- **Skip:** `skip()` applies final anim states and end camera, does not play pending audio, sets `finished`+`justFinished`.
- **Overlay:** letterbox at 0.12 writes black rows top/bottom in the final capture (pixel assert); subtitle text present (structural HUD assert, showcase-style).
- **Zia probe** `g3d_test_game3d_timeline_probe`: full cutscene (cut → spline → subtitle → marker → fade) deterministic replay ×2 identical captures.

## 7. Verification gates

Full build + ctest; `-L graphics3d`; surface audits; determinism gate (ticks inside stepSimulation); `-L slow`.

## 8. Risks & constraints

- **Skip semantics** (silent past-fire) is the industry-standard behavior; document it — replaying audio on skip is always wrong.
- **One active timeline** per world (v1); nested/parallel timelines compose game-side by sequencing.
- **Track count bounds:** fixed-capacity arrays grown on demand; sorting once at `play()` keeps the tick allocation-free.
- **Letterbox interaction with user overlays:** timeline overlay draws *after* the user overlay callback so bars always win; documented ordering.
- Depends on plan 10's evaluator (build 10 first) and plan 08's scaled dt (soft — falls back to raw dt if 08 lands later).
