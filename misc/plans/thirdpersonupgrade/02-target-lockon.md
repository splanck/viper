# Plan 02 — `TargetLock3D`: Target Acquisition, Cycling, and Lock-On Camera Framing

## 1. Objective & scope

Add the lock-on targeting layer third-person combat is built on: acquire the best enemy in view, cycle between candidates, expose the locked target to gameplay, and let the plan-01 camera frame player + target. No such system exists; Xenoscape/ASHFALL hand-roll aim logic per game.

**In scope:** (a) `Viper.Game3D.TargetLock3D` runtime class; (b) candidate scoring/acquisition/cycling; (c) `ThirdPersonController` lock framing integration; (d) input magnetism helper.
**Out of scope:** hitboxes/damage (plans 05/06), on-screen lock reticle (game-side overlay; expose screen position instead).

**Zero external dependencies — absolute.** Composes existing overlap/raycast queries and entity registry lookups.

## 2. Current state (verified anchors)

- **Queries:** `rt_world3d_overlap_sphere(world, center, radius, mask)` → `PhysicsHitList3D` (`rt_physics3d.h:176`); LoS via `rt_world3d_raycast` (`:165`). Hit → body → entity resolution goes through the world entity registry (bodies attached via `Entity3D.attachBody` are registered; `rt_game3d_entity.c` owns the body↔entity association).
- **Layer filtering:** single-bit layers + masks (`Game3D.LayerMask`, validated single-bit semantics per `game3d.md` §Entities, Layers, And Masks) — targetable actors get a dedicated layer bit.
- **Camera math:** `rt_camera3d_look_at` / `smooth_look_at`, `screen_to_ray` exist (`rt_camera3d.c`); a `worldToScreen` projection helper does not — plan 25 adds one; this plan exposes target world position only.
- **Controller integration point:** plan 01's `LateUpdate` computes pivot + look; lock-on replaces the look target with a framed midpoint.
- **Polling conventions:** one-shot flags (`just_entered` style) and buffered events are the house pattern; no VM callbacks.

## 3. Design

### 3.1 Class and state

Same file family as plan 01 (`rt_game3d_thirdperson.c` or a small `rt_game3d_targetlock.c`; prefer separate file for source-health granularity). Class ID next free (`-0x60304C`).

```c
typedef struct rt_game3d_targetlock {
    void *vptr; void *world; void *owner;    /* owner Entity3D (the player)   */
    void *target;                            /* current locked Entity3D|NULL  */
    double max_distance;                     /* default 18.0                  */
    double cone_degrees;                     /* default 65 (half-angle from camera fwd) */
    int64_t candidate_mask;                  /* which layers are targetable   */
    int8_t require_los;                      /* default true                  */
    double stickiness;                       /* score bonus for current target, default 1.25 */
    double break_distance;                   /* auto-release distance, default max*1.25 */
    int8_t just_acquired, just_lost;         /* one-shot flags                */
} rt_game3d_targetlock;
```

### 3.2 Scoring and acquisition

`Acquire()`: overlap-sphere around the owner at `max_distance`, resolve hits to entities, reject: owner itself, non-matching mask, dead/stale entities. Score = `w_angle × (1 − angleFromCameraForward/cone) + w_dist × (1 − dist/maxDist)`, angle-weighted 2:1; candidates outside the cone are rejected. If `require_los`, raycast owner-pivot → candidate-pivot with a static-world mask; blocked candidates are skipped. Highest score wins.

`Cycle(direction)`: rank candidates by signed screen-space-equivalent yaw offset from the current target (camera-basis projection — no screen projection needed); pick the nearest in the requested direction.

Per-step maintenance (ticked by the world while installed on a controller, or manually via `Update(dt)`): auto-release on distance > `break_distance`, target death/despawn (stale-entity check), or LoS broken for > 0.5 s (grace timer avoids flicker); sets `just_lost`.

### 3.3 Camera framing (plan 01 hook)

`ThirdPersonController.set_lockTarget(lock)` (property `LockTarget`): while `lock.target` is non-null, LateUpdate replaces free-look yaw/pitch with framing — look point = lerp(pivot, targetPivot, 0.4); yaw eases toward the player→target bearing; pitch eases to a distance-adaptive value (`atan2` of height delta, clamped). Player look input is ignored except `Cycle` gestures (game code maps flick input to `Cycle(±1)`). On release, yaw/pitch resume from the framed values (no snap).

### 3.4 Input magnetism

`LockedMoveBias(moveAxis) -> Vec3` helper: rotates the input vector up to 12° toward the target bearing when within 30° — soft aim assist for melee approach. Pure function on the lock object; gameplay opts in by filtering its own move vector.

## 4. Implementation steps

1. Struct + class ID + `New(world, ownerEntity)` + property plumbing.
2. Candidate collection + scoring + `Acquire`/`Clear`; C unit test with three targetable bodies.
3. LoS gating + grace-timer release + one-shot flags.
4. `Cycle(direction)` ordering test (three targets left/center/right).
5. Plan-01 `LockTarget` property + framed LateUpdate path.
6. `LockedMoveBias` helper.
7. runtime.def + audits + ADR + docs section under §Camera And Character Controllers.
8. Zia probe `g3d_test_game3d_lockon_probe` (synthetic input, deterministic frames).

## 5. Public API changes (runtime.def)

```
RT_FUNC(Game3DTargetLockNew, rt_game3d_targetlock_new, "Viper.Game3D.TargetLock3D.New", "obj(obj,obj<Viper.Game3D.Entity3D>)")
RT_CLASS_BEGIN("Viper.Game3D.TargetLock3D", Game3DTargetLock3D, "obj", Game3DTargetLockNew)
    RT_PROP("Target","obj<Viper.Game3D.Entity3D>", get only)
    RT_PROP("MaxDistance","f64",…) RT_PROP("ConeDegrees","f64",…)
    RT_PROP("CandidateMask","i64",…) RT_PROP("RequireLineOfSight","i1",…)
    RT_PROP("Stickiness","f64",…) RT_PROP("BreakDistance","f64",…)
    RT_METHOD("Acquire","i1(obj)",…) RT_METHOD("Clear","void(obj)",…)
    RT_METHOD("Cycle","i1(obj,i64)",…) RT_METHOD("Update","void(obj,f64)",…)
    RT_METHOD("JustAcquired","i1(obj)",…) RT_METHOD("JustLost","i1(obj)",…)
    RT_METHOD("LockedMoveBias","obj<Viper.Math.Vec3>(obj,obj<Viper.Math.Vec3>)",…)
RT_CLASS_END()
```

Plus `ThirdPersonController` gains `RT_PROP("LockTarget","obj<Viper.Game3D.TargetLock3D>",…)`. Leaf `TargetLock3D` unique. ADR shared with plan 01 or its own (`00xx-game3d-target-lock.md`).

## 6. Tests

- **Acquisition:** Given three targetable entities (center 5 m, off-cone 5 m, center 15 m) — When `Acquire()` — Then the near-center one is chosen (fail-before: no API).
- **LoS:** wall between owner and best candidate ⇒ next-best acquired; `RequireLineOfSight=false` ⇒ walled one acquired.
- **Cycle:** `Cycle(1)` from center target selects the right-hand target; `Cycle(-1)` returns.
- **Auto-release:** target despawn ⇒ `Target == null` + `JustLost` one frame; distance > break ⇒ same.
- **Framing:** with lock installed on plan-01 controller, camera forward converges on the player→target bearing within 1 s of fixed steps (deterministic replay ×2, identical).
- **Stale safety:** despawned owner ⇒ getters return neutral, `StaleEntityCalls` increments (documented Entity3D fail-closed pattern).

## 7. Verification gates

Full build + ctest; `-L graphics3d`; `check_runtime_completeness.sh` + surface audits + leaf-name check; determinism gate (runs inside stepSimulation when installed); `-L slow` before phase close.

## 8. Risks & constraints

- **Score weight churn:** tune once, freeze, and encode in tests loosely (ordering asserts, not exact scores) so tuning doesn't churn goldens.
- **Overlap allocation:** reuse the world's bounded query-hit capacity (`rt_world3d_set_max_query_hits`) — no per-frame growth.
- **Entity resolution:** bodies not attached through `Entity3D` resolve to no entity; document that lock-on requires `attachBody`-managed actors.
- **Grace-timer LoS** is a semantic commitment (0.5 s); note it in docs so games relying on instant break can set it to 0 (expose `LosGraceSeconds` if contested during review).
