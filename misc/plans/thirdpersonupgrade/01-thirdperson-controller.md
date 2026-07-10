# Plan 01 — `ThirdPersonController`: Collision-Aware Spring-Arm Camera + Character Drive

## 1. Objective & scope

Give Viper the one controller every third-person game needs and none of the four built-ins provide: an over-the-shoulder camera on a collision-aware spring arm that also drives the player character camera-relatively. Today `FollowController` tracks an entity with damping but has **no collision handling at all** — the camera clips through walls — and `FirstPersonController` is the only controller that drives a `CharacterController3D`.

**In scope:** (a) `Viper.Game3D.ThirdPersonController` runtime class; (b) spring-arm sphere-sweep pull-in; (c) opt-in occluder fading; (d) camera-relative character drive; (e) aim mode.
**Out of scope:** lock-on targeting (plan 02), camera rails (plan 10), controller scripting hooks.

**Zero external dependencies — absolute.** Pure in-tree C composing existing physics queries and controller machinery.

## 2. Current state (verified anchors)

- **No third-person controller exists.** `rt_game3d_controllers.c` (64K) holds FreeFly/FirstPerson/Orbit/Follow; a grep for `raycast|sweep|occlu|collision` in that file returns nothing — no built-in camera performs any collision query.
- **FollowController** is the nearest sibling: `rt_game3d_follow_controller_late_update` (`rt_game3d_controllers.c:1303`) repositions the camera after physics/sync; def block `runtime.def:15541-15557` (`RT_CLASS_BEGIN("Viper.Game3D.FollowController", …)` with `Target`/`Offset`/`Damping` props and `Update`/`LateUpdate` methods).
- **FirstPersonController owns the character-drive precedent:** a retained `character` slot (`rt_game3d_controllers.c:108,123,304`), created via `rt_character3d_new(radius, height, mass)`; `rt_game3d_first_person_controller_update` (`:735`) reads `Input3D`, moves the character before physics, and late-updates the camera eye.
- **Controller lifecycle:** installed via `World3D.setCameraController`; `update` runs inside `game3d_world_step_simulation_impl` (`rt_game3d.c:1711`) before the physics step, `lateUpdate` after body/anim/audio sync — so a lateUpdate camera sees final entity poses (documented frame order in `docs/viperlib/graphics/game3d.md` §Frame Order). Rebinding rules (one world at a time, stale-slot validation) are established controller behavior.
- **Sweep query exists:** `rt_world3d_sweep_sphere(world, center, radius, delta, mask)` (`rt_physics3d.h:171`) returns the first `PhysicsHit3D` with `fraction` (`rt_physics_hit3d_get_fraction`, `:192`) and `started_penetrating` (`:194`). `rt_world3d_raycast_all` (`:168`) returns a distance-sorted `PhysicsHitList3D`.
- **Per-entity material fade is possible today:** `Material3D.MakeInstance` + `set_Alpha` + `AlphaMode` (runtime.def Material3D block) — an occluder can be faded without touching shared materials.
- **Input:** `Input3D.lookAxis()` merges mouse and bound-pad right stick with sensitivity/deadzone handled (`rt_game3d_input.c`); `moveAxis()` gives normalized planar movement.
- **Class IDs:** next free is `INT64_C(-0x60304B)` (`rt_graphics3d_ids.h:115` ends at `-0x60304A`).

## 3. Design

### 3.1 State and file layout

New C file `src/runtime/graphics/3d/rt_game3d_thirdperson.c` (decls in `rt_game3d.h`, struct in `rt_game3d_internal.h`, class ID `RT_G3D_GAME3D_THIRDPERSON_CLASS_ID = -0x60304B`):

```c
typedef struct rt_game3d_thirdperson_controller {
    void *vptr;
    void *world;                 /* weak back-ref, controller-slot pattern */
    void *target;                /* Entity3D to orbit (usually the player) */
    void *character;             /* optional CharacterController3D drive slot */
    double yaw, pitch;           /* camera orbit state, degrees */
    double distance, min_distance, max_distance;
    double shoulder_offset[3];   /* local-space offset from target pivot */
    double pivot_height;         /* pivot above entity origin (eye/chest height) */
    double pitch_min, pitch_max; /* clamp, degrees (default -60..75) */
    double damping;              /* exponential position smoothing, 0 = snap */
    double collision_radius;     /* boom sweep sphere radius (default 0.25) */
    int64_t collision_mask;      /* layers the boom collides with */
    int8_t occlusion_fade;       /* opt-in occluder fading */
    double aim_blend;            /* 0..1 current aim interpolation */
    double aim_distance, aim_fov;
    double aim_shoulder_offset[3];
    double current_distance;     /* smoothed post-collision boom length */
    /* fade bookkeeping: faded material instances keyed by node */
    struct tp_fade_entry *fades; int32_t fade_count, fade_capacity;
} rt_game3d_thirdperson_controller;
```

### 3.2 Update (pre-physics): input + character drive

`Update(world, dt)`: snapshot `Input3D.lookAxis()` into yaw/pitch (clamped); if `character` is set, rotate `Input3D.moveAxis()` by camera yaw and call the same character-move path `FirstPersonController` uses (`CharacterController3D.update` semantics: planar move, jump, gravity). The character remains authoritative for the entity position; the controller never writes the entity transform directly.

### 3.3 LateUpdate (post-sync): spring arm

1. Pivot = target entity world position (post-physics, same read `FollowController.LateUpdate` uses) + `pivot_height` + shoulder offset rotated by yaw.
2. Desired eye = pivot − forward(yaw, pitch) × lerp(`distance`, `aim_distance`, `aim_blend`).
3. **Boom sweep:** `rt_world3d_sweep_sphere(world, pivot, collision_radius, eye − pivot, collision_mask)`. On hit, boom length = `fraction × |eye − pivot|` minus a skin epsilon; `started_penetrating` snaps to `min_distance`. Smooth: pull-in is instant (never clip), release lerps back at `damping` rate (no pop-out).
4. Camera: `Camera3D.set_position(eye)` + `look_at(pivot)`; aim mode lerps `Camera3D.set_fov` toward `aim_fov`. All writes go through existing `rt_camera3d_*` setters.

### 3.4 Occluder fade (opt-in)

When `occlusion_fade` is on: `rt_world3d_raycast_all(pivot → eye)` each lateUpdate; for each hit body's bound node (via the entity registry), swap in a `Material3D.MakeInstance` clone with `AlphaMode` blend and animate alpha toward 0.35; restore original material handles when no longer occluding. Bodies without node bindings and the target's own body are skipped. Bookkeeping lives in the controller's `fades` array; `Destroy`/detach restores everything.

### 3.5 Defaults

`New(world, targetEntity)`: distance 4.0, min 0.75, max 8.0, pivot 1.5, shoulder (0.35, 0, 0), pitch −60..75, damping 12, collision radius 0.25, mask `LayerMask.All()`, fade off, aim distance 1.6, aim fov 45.

## 4. Implementation steps

1. Struct + class ID + `New/Destroy` + controller-slot install/detach (mirror FollowController registration) — compiles, no behavior.
2. `Update`: look input + camera-relative character drive; probe: synthetic input moves a character diagonally under a 45° camera yaw.
3. `LateUpdate` spring arm without collision (pure orbit); probe: camera keeps distance/pitch through target motion.
4. Boom sphere-sweep pull-in + smoothed release; C unit test with a wall body.
5. Aim mode blend (distance/shoulder/FOV).
6. Occluder fade bookkeeping + restore paths.
7. runtime.def block + surface audits + ADR + docs (`game3d.md` §Camera And Character Controllers).
8. Zia probe + example update (`examples/3d/game3d_showcase/` gains a third-person scene).

## 5. Public API changes (runtime.def)

New class after the FollowController block:

```
RT_FUNC(Game3DThirdPersonNew, rt_game3d_thirdperson_controller_new, "Viper.Game3D.ThirdPersonController.New", "obj(obj,obj<Viper.Game3D.Entity3D>)")
RT_CLASS_BEGIN("Viper.Game3D.ThirdPersonController", Game3DThirdPersonController, "obj", Game3DThirdPersonNew)
    RT_PROP("Target",   "obj<Viper.Game3D.Entity3D>", …)
    RT_PROP("Character","obj", …)                       /* CharacterController3D slot */
    RT_PROP("Distance","f64", …)  RT_PROP("MinDistance","f64", …)  RT_PROP("MaxDistance","f64", …)
    RT_PROP("ShoulderOffset","obj<Viper.Math.Vec3>", …) RT_PROP("PivotHeight","f64", …)
    RT_PROP("Damping","f64", …)   RT_PROP("CollisionRadius","f64", …) RT_PROP("CollisionMask","i64", …)
    RT_PROP("OcclusionFade","i1", …) RT_PROP("Aiming","i1", …)
    RT_PROP("AimDistance","f64", …)  RT_PROP("AimFov","f64", …)
    RT_METHOD("Update","void(obj,f64)", …)  RT_METHOD("LateUpdate","void(obj,f64)", …)
RT_CLASS_END()
```

Leaf `ThirdPersonController` is globally unique. New file → `scripts/source_health_baseline.tsv` bump. ADR: `docs/adr/00xx-game3d-thirdperson-controller.md` (pattern: `docs/adr/0064-game3d-character-controller-gravity.md`).

## 6. Tests

- **Boom collision (C unit):** Given a target at origin, a wall body 2.0 behind it, distance 4.0 — When the world steps — Then camera distance ≤ 2.0 − skin and never behind the wall plane (fail-before: no API).
- **No-clip sweep:** Given the camera starts penetrating (spawn inside a box), Then boom snaps to `MinDistance` (uses `started_penetrating`).
- **Character drive parity:** camera yaw 90° + forward input moves the character along −X (world-relative check), grounded flag stays true on the ground plane; deterministic `runFramesOnly` replay twice, byte-identical positions.
- **Aim blend:** toggling `Aiming` converges distance/FOV within 0.5 s and back.
- **Occluder fade:** wall between pivot and camera gets a faded material instance; moving clear restores the original handle (assert same pointer).
- **Zia probe** `g3d_test_game3d_thirdperson_probe`: synthetic-input orbit + wall pull-in + final-frame capture; registered per the ctest pattern in `docs/viperlib/graphics/game3d.md` §Tests.

## 7. Verification gates

Full build + ctest; `-L graphics3d` + `check_runtime_completeness.sh` + surface audits after the def change; Game3D determinism gate (worker replay probes) since the controller runs inside `stepSimulation`; `-L slow` before phase close.

## 8. Risks & constraints

- **Query cost:** one sphere sweep (+ optional raycast-all) per frame is broadphase-bounded; document that `collision_mask` should exclude character/projectile layers.
- **Fade ownership:** material instances must be restored on despawn/world-destroy — hook the stale-entity path so a despawned occluder can't leak a clone.
- **Damping asymmetry** (instant pull-in, smoothed release) is a semantic commitment; changing it later shifts golden baselines.
- **Do not** write entity transforms from the camera; the character controller owns motion (avoids fighting `SyncMode`).
