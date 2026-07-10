# Plan 10 — `RailCamera3D` Spline Camera + DOF Focus Drive

## 1. Objective & scope

A gameplay-installable spline camera (crawlspaces, vista reveals, boss intros, fixed-path sections) plus the shared smooth-spline evaluator plan 09's camera-move track consumes, and a one-call DOF focus drive for focus pulls. Path3D exists but is linear-segment sampling with no camera binding, no smoothing, no FOV/roll keys.

**In scope:** (a) Catmull-Rom smooth evaluation over `Path3D` (shared C helper); (b) `Viper.Game3D.RailCamera3D` controller; (c) FOV/roll keys + easing; (d) `World3D.setDofFocus`.
**Out of scope:** the sequencer itself (plan 09), path authoring UI (scene editor future).

**Zero external dependencies — absolute.** Catmull-Rom from the published formulation.

## 2. Current state (verified anchors)

- **Path3D:** `rt_path3d_add_point/get_position_at/get_direction_at/get_length/set_looping` (`rt_path3d.h`) — arclength-parameterized *linear* interpolation between waypoints (Misc 3D Helpers, `rendering3d.md` §Path3D). Constant-speed traversal precedent: `Behavior3D.AddFollowPath` (`game3d.md` §Behavior3D).
- **No smooth-spline evaluation** exists anywhere in the 3D runtime (grep `catmull|spline` — none).
- **Controller machinery:** install/detach via `World3D.setCameraController`; `update`/`lateUpdate` slots (FollowController def block `runtime.def:15541-15557`, impl `rt_game3d_controllers.c:1303`).
- **Camera writes:** `rt_camera3d_set_position/look_at/set_fov` (`rt_camera3d.c`). Roll: `Camera3D` has yaw/pitch but **no roll** — the camera basis comes from look-at with +Y up; a roll key needs a camera-side up-vector override `rt_camera3d_set_up_hint(x,y,z)` (small addition, default +Y, consumed by look-at basis construction).
- **DOF:** `rt_postfx3d_add_dof` exists (`rt_postfx3d.h`); focus distance is a construction-time parameter — a live setter is needed for focus pulls. `EffectRegistry3D`/`PostFX` own the world's chain (`game3d.md` §Sound3D And Effects3D).

## 3. Design

### 3.1 Shared evaluator

Internal helper (header in `rt_game3d_internal.h` or a small `rt_g3d_spline.h` with `RuntimeSurfacePolicy.inc` entry):

```c
/* Centripetal Catmull-Rom over a Path3D's points; t in [0,1] arclength-normalized.
   Falls back to linear for <4 points. Writes position and unit tangent. */
void g3d_spline_eval(void *path3d, double t, double *pos_out, double *tan_out);
```

Centripetal parameterization (α = 0.5) avoids loops/cusps on uneven waypoint spacing. Arclength normalization by cached per-segment length table (rebuilt on path revision; `Path3D` gains an internal revision counter bumped by `add_point/clear`). Looping paths wrap phantom endpoints; open paths clamp them.

### 3.2 `RailCamera3D`

Controller struct mirrors FollowController plumbing (world slot, install/detach validation):

- `New(world, path)`; `progress` prop [0,1] (game drives it — from player position projection, a timer, or a trigger); `speed` prop for auto-advance (units/s along arclength; 0 = manual).
- Look target: `set_lookEntity(entity)` | `set_lookPoint(vec3)` | `set_lookPath(path2)` (evaluated at the same `t`) | none ⇒ face the spline tangent.
- Keys: `addFovKey(t, fov)`, `addRollKey(t, degrees)` — piecewise-linear with smoothstep option (`set_keyEase`); roll applies via the up-hint (rotate +Y about the view axis).
- `update`: advance `progress` by `speed × dt / length`. `lateUpdate`: evaluate spline + keys, write camera (post-physics look-entity reads, FollowController convention).
- Damping: `positionDamping` exponential smoothing for progress-driven jumps (game teleports `progress`).

### 3.3 DOF focus drive

- `rt_postfx3d_set_dof_focus(chain, distance)` — mutates the live DOF effect's focus parameter (no chain rebuild); no-op with a recoverable false if the chain has no DOF effect.
- `World3D.setDofFocus(distance)` forwards through the world's effect registry chain. Timeline3D (09) and gameplay (focus the lock-on target: distance = |camera − target|) both consume it.

## 4. Implementation steps

1. `g3d_spline_eval` + segment-length cache + Path3D revision counter; C unit tests (4-point S-curve: C1 continuity sampling, loop wrap, <4-point linear fallback).
2. `RailCamera3D` struct + install/detach + manual `progress` + tangent-facing; unit test.
3. Look-target modes + FOV/roll keys + up-hint camera addition; unit tests.
4. Auto-advance + damping.
5. `set_dof_focus` postfx mutation + `World3D.setDofFocus`.
6. runtime.def + audits + ADR + docs (`game3d.md` §Camera And Character Controllers + cross-ref from §Cutscenes).
7. Zia probe `g3d_test_game3d_railcamera_probe` (deterministic progress sweep, final capture).

## 5. Public API changes (runtime.def)

```
RT_FUNC(Game3DRailCameraNew, rt_game3d_rail_camera_new, "Viper.Game3D.RailCamera3D.New", "obj(obj,obj<Viper.Graphics3D.Path3D>)")
RT_CLASS_BEGIN("Viper.Game3D.RailCamera3D", Game3DRailCamera3D, "obj", Game3DRailCameraNew)
    RT_PROP("progress","f64",…) RT_PROP("speed","f64",…) RT_PROP("positionDamping","f64",…)
    RT_METHOD("setLookEntity","void(obj,obj<Viper.Game3D.Entity3D>)",…)
    RT_METHOD("setLookPoint","void(obj,obj<Viper.Math.Vec3>)",…)
    RT_METHOD("setLookPath","void(obj,obj<Viper.Graphics3D.Path3D>)",…)
    RT_METHOD("addFovKey","obj(obj,f64,f64)",…) RT_METHOD("addRollKey","obj(obj,f64,f64)",…)
    RT_METHOD("Update","void(obj,f64)",…) RT_METHOD("LateUpdate","void(obj,f64)",…)
RT_CLASS_END()
```

Plus `Viper.Graphics3D.Camera3D` `RT_METHOD("SetUpHint","void(obj,f64,f64,f64)")`, `Viper.Graphics3D.PostFX3D` `RT_METHOD("SetDofFocus","i1(obj,f64)")`, `World3D.setDofFocus(f64) -> i1`. Leaf `RailCamera3D` unique. ADR shared with plan 09 (`00xx-game3d-cinematic-cameras.md`).

## 6. Tests

- **Evaluator (C unit):** centripetal CR through 4 collinear points reproduces the line; S-curve sampling has no position discontinuity > step bound between adjacent t's; loop wrap continuous at t=0/1 (fail-before: helper absent).
- **Constant speed:** equal-`speed` auto-advance covers equal arclengths per step on wildly uneven waypoint spacing (±2%).
- **Keys:** FOV at key t hits the key value; roll 90° at t produces a camera up ⊥ +Y (basis assert).
- **Look-entity:** entity displaced by physics mid-run is faced using its post-step position.
- **DOF:** `SetDofFocus` on a chain with DOF returns true and the blur plane moves (software-backend pixel variance assert near/far); chain without DOF returns false.
- **Determinism:** rail sweep replay ×2 identical captures.

## 7. Verification gates

Full build + ctest; `-L graphics3d`; surface audits; `-L slow`. DOF change re-runs the postfx unit lane (`test_rt_canvas3d*` postfx cases).

## 8. Risks & constraints

- **Path3D mutation during playback:** revision counter invalidates the length cache; a path edited mid-play re-parameterizes next evaluation (documented; don't edit paths under an active rail).
- **Up-hint** must degrade safely when view ∥ up (renormalize with a fallback axis) — test the straight-down case.
- **DOF live-mutation** must not touch chain topology (no reallocation) — the backend reads effect params per frame already; keep it a parameter write.
- Roll keys and FOV keys are per-rail, not per-path — two rails may share one path with different keys.
