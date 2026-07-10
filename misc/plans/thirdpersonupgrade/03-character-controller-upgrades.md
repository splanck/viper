# Plan 03 — Character3D Upgrades: Crouch, Dynamic-Body Interaction, Moving Platforms

## 1. Objective & scope

Close the three gaps that make `Character3D` insufficient for an adventure game: it walks *through* dynamic bodies (they are filtered out of collision entirely), it cannot crouch (fixed capsule), and it does not ride moving platforms. Add slope-slide behavior and ground-body inspection while in the file.

**In scope:** (a) crouch/stand capsule resize with clearance check; (b) dynamic bodies as blockers + mass-based push; (c) kinematic/static moving-platform riding; (d) steep-slope sliding; (e) `GroundBody` accessor; (f) Game3D `CharacterController3D` wrapper props.
**Out of scope:** swimming/flying modes, ledge grabs (plan 04), root-motion changes.

**Zero external dependencies — absolute.** All changes inside the existing from-scratch controller.

## 2. Current state (verified anchors)

- **Dynamic bodies excluded:** `character3d_candidate_body` (`rt_physics3d_character.c:183-191`) rejects `other->is_trigger || other->motion_mode == PH3D_MODE_DYNAMIC` — comment: "the controller is kinematic so we don't want it pushing dynamics around as solid blockers". Consequence: no blocking *and* no pushing.
- **Controller structure:** capsule body + `step_height` (sanitized ≤ `CHARACTER3D_STEP_HEIGHT_MAX 100.0`, `:72,129`), `slope_limit_cos` walkable test (`character3d_surface_is_walkable`, `:168-175`), swept-slide via binary-searched sweeps over `character3d_test_position` (`:193+`, "temporarily moves the body… returns the deepest contact"), ground probing for grounded state, `just_landed` edge flag (`rt_physics3d.h:386`).
- **No capsule resize API:** `rt_character3d_new(radius, height, mass)` fixes dimensions (`rt_physics3d.h:370`); `rt_collider3d_reset_*_raw` helpers exist for box/sphere (`rt_collider3d.h:84,88`) — a capsule reset twin is needed.
- **Grounded state:** `rt_character3d_is_grounded` (`rt_physics3d.h:384`) — the ground *body* is found during probing but not retained/exposed.
- **Kinematic bodies** move by explicit velocity (`rt_physics3d.h:17` invariants), so a platform's per-step displacement is `velocity × dt` (linear) plus angular yaw — derivable without history.
- **Game3D wrapper:** `CharacterController3D` (`rt_game3d_controllers.c:304` creates `rt_character3d_new`; wrapper exposes `speed/jumpSpeed/gravity/teleport/grounded` per `game3d.md` §Camera And Character Controllers).

## 3. Design

### 3.1 Dynamic bodies: block + push

Change `character3d_candidate_body` to accept dynamic bodies (still honoring layer/mask). During slide resolution, when the blocking contact is dynamic:

- Apply a push impulse at the contact point: `impulse = push_strength × min(1, ctrl_mass / other_mass) × contact_normal × |v_into|`, via the existing `rt_body3d_apply_impulse_at_point` path, and wake the body.
- The controller still slides off the contact this step (never sinks); light bodies accelerate away and stop blocking within a few steps — heavy bodies effectively wall the player. `push_strength = 0` restores block-only.
- New: `rt_character3d_set_push_strength(ctrl, s)` / getter; default `1.0`. Escape hatch `rt_character3d_set_collide_dynamic(ctrl, 0)` preserves today's ghosting for compatibility (default **on** — new behavior — with a release note; the old behavior was a correctness bug for gameplay).

### 3.2 Crouch / capsule resize

- `rt_collider3d_reset_capsule_raw(collider, radius, height)` (twin of the box/sphere resets, `rt_collider3d.h:84-88` pattern) updating cached bounds + revision.
- `rt_character3d_set_height(ctrl, h)`: shrinking always succeeds (feet stay planted — reposition center by `(old−new)/2`); growing runs `character3d_test_position` at the stand-height pose and fails (returns 0) if blocked, so `TryStand` semantics come free. `rt_character3d_get_height`.

### 3.3 Moving platforms

- Track the ground body found by the ground probe: retain `ground_body` (weak) each `Move`.
- New `ride_platforms` flag (default on): at the start of `Move`, if grounded on a kinematic or static body with nonzero velocity/angular velocity, pre-displace the controller by the platform's step displacement (`v×dt`, plus yaw rotation about the platform origin applied to the controller offset), then run the normal swept move. Sweeps run *after* pre-displacement so a wall on the platform still blocks.
- `rt_character3d_get_ground_body(ctrl)` exposes the retained body (NULL when airborne) — gameplay uses it for conveyor logic and plan 23's surface queries.

### 3.4 Steep-slope slide

When grounded on a non-walkable surface (fails `character3d_surface_is_walkable`), project gravity onto the slope plane and add it to the frame velocity instead of treating the surface as ground — the standard "slide down too-steep slopes" behavior. New `rt_character3d_is_sliding(ctrl)` state getter.

### 3.5 Game3D wrapper

`CharacterController3D` gains `crouchHeight` (f64; `setCrouching(bool)` sugar swaps height/crouchHeight via `TryStand` semantics), `pushStrength`, `ridePlatforms`, `isSliding()`, `groundEntity()` (resolves ground body → owning `Entity3D` through the registry, null if unmanaged).

## 4. Implementation steps

1. `reset_capsule_raw` + `set_height/get_height` + clearance-checked stand; C unit tests (shrink under ledge OK, grow blocked, grow clear OK).
2. Dynamic-body candidate acceptance + push impulse + `set_push_strength` + compat flag; unit tests (crate pushed, heavy crate blocks, `push_strength 0` blocks).
3. Ground-body retention + `get_ground_body` + platform pre-displacement; unit test (kinematic platform carries idle controller; wall on platform blocks).
4. Slope-slide state + `is_sliding`.
5. Game3D wrapper props/methods + runtime.def + audits + ADR.
6. Docs (`physics3d.md` §Character3D, `game3d.md` controller table) + Zia probe.

## 5. Public API changes (runtime.def)

`Viper.Graphics3D.Character3D` additions: `RT_PROP("Height","f64",…)`, `RT_PROP("PushStrength","f64",…)`, `RT_PROP("RidePlatforms","i1",…)`, `RT_PROP("CollideDynamic","i1",…)`, `RT_METHOD("TrySetHeight","i1(obj,f64)",…)`, `RT_METHOD("IsSliding","i1(obj)",…)`, `RT_METHOD("GetGroundBody","obj(obj)",…)`.
`Viper.Game3D.CharacterController3D` additions: `CrouchHeight`, `PushStrength`, `RidePlatforms` props; `SetCrouching`, `IsSliding`, `GroundEntity` methods. RT_FUNC per entry; no new classes/leaf names; ADR `00xx-character3d-dynamics-and-platforms.md` (behavior-change ADR since the dynamic-body default flips).

## 6. Tests

- **Push:** Given a 1 kg dynamic crate ahead — When the controller walks into it for 30 fixed steps — Then crate displaces > 0.3 m and controller advances (fail-before: controller ghosts through).
- **Block:** 1000 kg crate ⇒ controller stops within capsule radius + skin; `CollideDynamic=false` ⇒ ghost-through (compat).
- **Crouch:** under a 1.2 m ceiling, `TrySetHeight(1.8)` returns false; after walking clear, returns true and height is 1.8.
- **Platform:** controller idle on kinematic platform moving +X at 2 m/s tracks within 1e-3/step; platform yaw rotates controller position about platform origin.
- **Slide:** on a 70° slope with 45° limit, controller descends and `IsSliding()` true; on 30° it stays grounded, not sliding.
- **Determinism:** all of the above as deterministic replays (VM==VM across runs); worker-count parity unaffected (controller is main-thread).

## 7. Verification gates

Full build + ctest; existing character tests (`g3d_test_game3d_character_controller_probe`, `walk_min` baselines, `g3d_bounded_no_regression_probe`) must stay green — they pin today's behavior for scenes without dynamics; determinism gate; `-L slow`; `-L graphics3d`.

## 8. Risks & constraints

- **Default flip** (dynamics now collide) can change existing demo behavior — audit demos that mix characters and dynamic bodies (bowling, showcase) before landing; the compat flag is the escape hatch.
- **Push feedback loops:** impulse is applied once per contact per step and capped by `|v_into|`; never apply inside the binary-search iterations (only on the resolved contact) or light bodies gain energy.
- **Platform rotation:** yaw-only in v1 (full angular ride needs orientation tracking on the controller); document the limit.
- **Capsule resize + physics queries:** bounds revision must bump (`rt_collider3d_get_bounds_revision_raw`) so the broadphase re-inserts.
