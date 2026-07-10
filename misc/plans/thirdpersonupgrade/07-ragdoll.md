# Plan 07 — `Ragdoll3D`: Skeleton→Bodies+Joints Builder, Anim Handoff, Powered Blend

## 1. Objective & scope

Turn the existing joint/physics primitives into a ragdoll pipeline: auto-build capsule bodies and limited 6-DoF joints from a skeleton, hand off from the animated pose with velocities, blend back to animation for get-ups, and drive joints toward the animated pose for powered hit reactions. Everything needed exists (capsule bodies, 6-DoF joints with limits and motors, bone palettes); nothing assembles it.

**In scope:** (a) `Viper.Graphics3D.Ragdoll3D` builder + lifecycle; (b) anim→ragdoll activation with velocity seeding; (c) ragdoll→anim blend-out; (d) powered/partial mode; (e) Game3D `Entity3D.enableRagdoll/disableRagdoll` sugar.
**Out of scope:** cloth (plan 27), dismemberment, custom per-game body shapes beyond the config knobs.

**Zero external dependencies — absolute.** Joint fitting and pose math from first principles on in-tree primitives.

## 2. Current state (verified anchors)

- **No ragdoll code exists** (grep `ragdoll` across runtime: only a 2D material false-positive).
- **Joints:** `rt_sixdof_joint3d_new(body_a, body_b, frame_a, frame_b)` + `set_angular_limits(min,max)` + `set_linear_motor` (`rt_joints3d.h:80-88`); hinge with motor+limits (`:59-68`); solved inside the world's iterative solver (`SolverIterations` default 6, `physics3d.md`).
- **Bodies:** `rt_body3d_new_capsule(radius, height, mass)` (`rt_physics3d.h:250`), damping setters, sleep control, `apply_impulse_at_point`.
- **Skeleton data:** bones stored parent-first with parent index and bind matrix (`rt_skeleton3d_add_bone`, `rt_skeleton3d.h:35-37`), `get_bone_bind_pose`, `get_bone_count`, `find_bone`; **no public parent-index getter** — add an internal `rt_skeleton3d_get_bone_parent_raw` (header-internal, `RuntimeSurfacePolicy.inc` entry).
- **Animated pose:** `rt_anim_controller3d_get_final_palette_data` / `get_previous_palette_data` (`rt_animcontroller3d.h`) give current and previous model-space bone globals — the pair yields per-bone velocities at handoff (`(cur − prev)/dt`).
- **Writing pose back:** the skinning palette consumes model-space globals; blend-out needs a "pose override" input to the controller — `AnimController3D.SetIKSolver` already applies a post-overlay pose mutation (`rt_animcontroller3d.h`), proving the palette-mutation hook pattern the ragdoll blender reuses.
- **Node/world transforms:** entity node world matrix from `rt_scene_node3d_get_world_matrix` (`rt_scene3d_node.c`).

## 3. Design

### 3.1 Builder

New C `src/runtime/graphics/3d/physics/rt_ragdoll3d.c/.h`. `Ragdoll3D.FromSkeleton(skeleton)` builds a default humanoid-agnostic rig:

- **Bone selection:** bones with length ≥ `min_bone_length` (default 0.12 in model units) and their chain parents; leaf micro-bones (fingers) collapse into their parent body. Bone length = |child bind translation|; terminal bones use a configurable cap length.
- **Bodies:** one capsule per selected bone — axis along the bone, radius `radius_scale × length` (default 0.22, clamped), mass distributed by `total_mass` (default 70) × relative volume; linear/angular damping 0.05/0.1.
- **Joints:** child↔parent `SixDofJoint3D` with joint frames at the bind-pose joint origin; angular limits default ±30° swing / ±20° twist, overridable per bone: `SetJointLimits(boneName, swingDeg, twistDeg)` pre-`Activate`.
- Config knobs before activation: `total_mass`, `radius_scale`, `min_bone_length`, per-bone limits. The builder stores the bone→body map and per-bone body-local→bone-local frames.

### 3.2 Activation (anim → ragdoll)

`Activate(world, animator, node)`:
1. Read current + previous palettes; entity world matrix from `node`.
2. Pose each body at `world × palette[bone] × frame`; velocity = finite difference of the world-space bone origins (angular from the rotation delta, log-map to axis-angle / dt).
3. Add bodies + joints to `world`; set the entity node's sync path so **bones now follow bodies**: each step the ragdoll writes model-space globals (inverse entity world × body pose × inverse frame) into the controller palette via the pose-override hook; skinning renders the ragdoll pose. Root bone drives the entity node translation (so cameras/audio follow the corpse).

### 3.3 Deactivation and powered mode

- `Deactivate(blendSeconds)`: bodies/joints leave the world; for `blendSeconds`, the pose-override lerps captured-ragdoll-pose → live-anim-pose (nlerp rotations), then the override uninstalls. The standard get-up: gameplay plays the get-up state at `Deactivate` time and the blend covers the seam.
- `SetPowered(boneMask, stiffness)`: while active, per-joint angular motors drive toward the *animated* target relative rotation each step (target from the live controller palette). `stiffness` scales motor strength; `boneMask` limits to a chain (e.g., upper body flinch while legs stay animated — bones outside the mask keep palette animation and their bodies are kinematic followers).

### 3.4 Game3D sugar

`Entity3D.enableRagdoll()` — builds (lazily, cached on the entity) from `entity.anim`'s skeleton and activates against the world; `disableRagdoll(blendSeconds)`. Death pairing with plan 06 documented (`JustDied → enableRagdoll`).

## 4. Implementation steps

1. Parent-index internal accessor + builder (bone selection, capsules, frames) + C unit test on a synthetic 5-bone chain skeleton (body count, lengths, masses).
2. Joint construction + limits + config knobs; test joint frame correctness (bind pose ⇒ zero joint error).
3. Activation: posing + velocity seeding; test: a T-posed chain activated with a moving previous palette inherits velocity.
4. Palette write-back (pose-override hook) + root-follow; test: skinned vertices follow fallen bodies (bone matrix equality, no rendering needed).
5. Deactivate blend-out; test pose convergence over `blendSeconds`.
6. Powered mode motors; test: 90° perturbed arm returns toward animated pose with stiffness-scaled convergence.
7. Game3D sugar + runtime.def + audits + ADR + docs (`physics3d.md` §Ragdoll3D, `game3d.md` cross-ref).
8. Zia probe `g3d_test_game3d_ragdoll_probe`: skinned glTF agent (openworld_slice fixture) dies, ragdolls onto terrain, deterministic replay ×2.

## 5. Public API changes (runtime.def)

```
RT_FUNC(G3dRagdollFromSkeleton, rt_ragdoll3d_from_skeleton, "Viper.Graphics3D.Ragdoll3D.FromSkeleton", "obj(obj<Viper.Graphics3D.Skeleton3D>)")
RT_CLASS_BEGIN("Viper.Graphics3D.Ragdoll3D", G3dRagdoll3D, "obj", G3dRagdollFromSkeleton)
    RT_PROP("TotalMass","f64",…) RT_PROP("RadiusScale","f64",…) RT_PROP("MinBoneLength","f64",…)
    RT_PROP("BodyCount","i64",get) RT_PROP("Active","i1",get)
    RT_METHOD("SetJointLimits","void(obj,str,f64,f64)",…)
    RT_METHOD("Activate","void(obj,obj,obj,obj)",…)      /* world, animController, node */
    RT_METHOD("Deactivate","void(obj,f64)",…)
    RT_METHOD("SetPowered","void(obj,i64,f64)",…)
    RT_METHOD("GetBody","obj(obj,str)",…)                 /* per-bone body escape hatch */
RT_CLASS_END()
```

Plus `Entity3D.enableRagdoll()/disableRagdoll(f64)`. Leaf `Ragdoll3D` unique; new files → source-health bump; internal header entries → `RuntimeSurfacePolicy.inc`. ADR `00xx-graphics3d-ragdoll.md`.

## 6. Tests

- **Builder (C unit):** 5-bone chain ⇒ 5 capsules, 4 joints, masses sum to `TotalMass`, parent-first frames zero-error at bind (fail-before: no API).
- **Velocity handoff:** previous palette displaced +X 0.1 @ dt 1/60 ⇒ root body velocity ≈ 6 m/s.
- **Settle:** activated chain over a static floor comes to rest (sleeping) within 5 s of fixed steps; no NaNs; joint limits respected (relative angles within limits + solver tolerance).
- **Blend-out:** after `Deactivate(0.5)`, palette equals live animation within 1e-4 at t=0.6.
- **Powered:** perturbed pose error decreases monotonically at stiffness 1.0.
- **Determinism:** full activate/settle sequence replayed ×2 bit-identical; worker-count parity (solver is main-thread).

## 7. Verification gates

Full build + ctest; `-L graphics3d`; surface audits; determinism gate (physics-touching); `-L slow`. Solver-stability spot check with `SolverIterations` 6 and 12 (no explosion at 6).

## 8. Risks & constraints

- **Solver budget:** ~15 bodies + ~14 joints per ragdoll; multiple simultaneous ragdolls raise island cost — document a practical cap and rely on sleeping.
- **6-DoF limit fidelity:** swing/twist decomposition must be stable near poles (use swing-twist decomposition, not Euler).
- **Palette override ordering:** write-back must run after animation update and before skinning/scene sync — same slot the IK hook occupies; define a fixed order (IK, then ragdoll override) and test it.
- **Scaled entities:** v1 requires uniform entity scale (trap with a clear diagnostic otherwise); non-uniform scale distorts capsules.
