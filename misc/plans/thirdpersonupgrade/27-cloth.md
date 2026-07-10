# Plan 27 — `Cloth3D`: Verlet Chains and Patches (Capes, Banners, Hair Tails)

## 1. Objective & scope

Secondary motion for the thing the player stares at all game — the back of their character — plus world dressing: a from-scratch verlet simulator with two topologies (**chains** for capes-as-strips, hair tails, pouches, tabards; **patches** for banners/flags), sphere/capsule collision, pinning, wind coupling (plan 16), fixed-substep determinism, and two output bindings (skeleton bone chains or a generated mesh).

**In scope:** (a) verlet core (points, distance constraints, iterations); (b) chain + patch topologies; (c) collider list (spheres/capsules, incl. tracking entity bodies); (d) pinning; (e) wind; (f) bone-chain and mesh outputs; (g) world tick + floating-origin/rebase handling.
**Out of scope:** garment-grade cloth (self-collision, tearing), mesh-authored arbitrary topologies (v2), GPU sim.

**Zero external dependencies — absolute.** Verlet + Jakobsen-style constraint relaxation from the published method.

## 2. Current state (verified anchors)

- **No cloth exists** (grep: only a 2D joint false-positive).
- **Verlet-ish precedent in-tree:** 2D physics joints and the particle systems prove the integration/constraint idiom; nothing 3D.
- **Bone write path:** plan 07 establishes the post-anim palette-override slot (IK → ragdoll → facial ordering); cloth bone-chains join it (order: … → cloth, last — cloth reads final bone anchors).
- **Bone anchors:** `rt_anim_controller3d_get_bone_matrix/get_bone_pose` for the chain root anchor; `rt_skeleton3d` parent-first ordering (`rt_skeleton3d.h:14`).
- **Mesh output:** `Mesh3D.AddVertex`-built meshes rewritten in place is established (`Water3D.Update` "rewrites the retained mesh vertex buffer in place", `rendering3d.md` §Water3D) — the patch banner uses the same in-place update; identity-matrix raw meshes participate in floating-origin camera-relative upload (`game3d.md` §World3D floating origin list).
- **Wind source:** plan 16's `World3D.get_wind` (falls back to a locally-set `setWind` when 16 absent).
- **Collision shapes:** analytic point-vs-sphere/capsule pushout is self-contained; entity-tracking colliders read body poses via `rt_body3d_get_pose_raw` (`rt_physics3d.h:268`).
- **Determinism/rebase:** fixed substeps inside `stepSimulation`; `rebaseOrigin` shifts world-space state (particles/decals precedent, `game3d.md` §Effects3D rebase note) — cloth points shift identically.

## 3. Design

### 3.1 Core

New C `src/runtime/graphics/3d/physics/rt_cloth3d.c`:

```c
typedef struct rt_cloth3d {
    void *vptr;
    double *pos, *prev;            /* 3 × point_count                        */
    int32_t point_count;
    struct { int32_t a, b; double rest; } *constraints; int32_t constraint_count;
    uint8_t *pinned;
    int32_t width, height;         /* patch dims; chains: width=1            */
    double damping, gravity_scale, wind_response;
    int32_t iterations;            /* constraint relaxation, default 4      */
    double substep_dt;             /* fixed, default 1/120                   */
    double accumulator;
    /* colliders: fixed-capacity list of {type, params, tracked_body|NULL}   */
    /* output binding: BONES(animator, bone indices…) | MESH(mesh handle)    */
} rt_cloth3d;
```

Integration: classic verlet (`x' = x + (x − prev) × (1−damping) + a·dt²`), gravity + wind (wind force = `wind_response × (wind − pointVelocity)`, per-point normal-scaled on patches for flag billow), then `iterations` passes of distance-constraint relaxation (structural for chains; structural + shear for patches), then collider pushout, then pin re-fix. Substeps: fixed `substep_dt` with an accumulator fed by the (scaled) world dt — deterministic at any frame rate; max 8 substeps/step with the remainder carried (spiral guard).

### 3.2 Construction + binding

- `NewChain(segments, totalLength)` / `NewPatch(w, h, width, height)`.
- **Bone output:** `bindBoneChain(animator, rootBoneName)` — walks the child chain from the root bone (parent-first skeleton order), pins point 0 to the root bone's animated world pose each step, and after simulation writes each bone's rotation to aim at the next point (position preserved — length-safe). Registered in the post-anim override chain.
- **Mesh output:** `bindMesh(mesh)` (patch only) — rewrites vertex positions + recomputed normals in place per step (Water3D pattern); the game parents the mesh node wherever (flagpole).
- **Pins:** `pin(index)` / `pinRange(...)`; patch banners pin their top row; chains auto-pin point 0 when bone-bound.
- **Colliders:** `addSphere(center, r)` / `addCapsule(a, b, r)` static, `addBodyCollider(entityOrBody)` tracking (reads pose each substep; typical: character's own capsule so the cape doesn't pass through the torso).
- World registration: `World3D.addCloth(c)` (or `Entity3D.attachCloth` for bone-bound) — ticked after animations/overrides that produce its anchors.

## 4. Implementation steps

1. Verlet core + constraints + substepping; pure-C determinism/energy tests (hanging chain settles to analytic catenary-ish length; no drift over 10 k steps; replay bit-identical).
2. Patch topology + shear constraints + in-place mesh rewrite + normals.
3. Collider pushout (static then tracked); capsule pushout correctness tests.
4. Wind coupling + plan-16 world wind read.
5. Bone-chain binding + aim-rotation write-back in the override chain (order test with IK/ragdoll/facial).
6. Rebase handling + floating-origin parity test (50 km pattern).
7. runtime.def + audits + ADR + docs (`physics3d.md` §Cloth3D) + Zia probe (caped skinned fixture walking; banner in wind; deterministic replay; SW golden).

## 5. Public API changes (runtime.def)

```
RT_FUNC(G3dClothNewChain, rt_cloth3d_new_chain, "Viper.Graphics3D.Cloth3D.NewChain", "obj(i64,f64)")
RT_FUNC(G3dClothNewPatch, rt_cloth3d_new_patch, "Viper.Graphics3D.Cloth3D.NewPatch", "obj(i64,i64,f64,f64)")
RT_CLASS_BEGIN("Viper.Graphics3D.Cloth3D", G3dCloth3D, "obj", G3dClothNewChain)
    RT_PROP("Damping","f64",…) RT_PROP("Iterations","i64",…) RT_PROP("GravityScale","f64",…)
    RT_PROP("WindResponse","f64",…) RT_PROP("PointCount","i64",get)
    RT_METHOD("pin","obj(obj,i64)",…) RT_METHOD("addSphere","obj(obj,obj<Vec3>,f64)",…)
    RT_METHOD("addCapsule","obj(obj,obj<Vec3>,obj<Vec3>,f64)",…)
    RT_METHOD("addBodyCollider","obj(obj,obj)",…)
    RT_METHOD("bindBoneChain","obj(obj,obj,str)",…) RT_METHOD("bindMesh","obj(obj,obj)",…)
    RT_METHOD("setWind","void(obj,obj<Vec3>,f64)",…)   /* local override */
    RT_METHOD("getPoint","obj<Vec3>(obj,i64)",…)        /* inspection/tests */
RT_CLASS_END()
```

Plus `World3D.addCloth/removeCloth`, `Entity3D.attachCloth`. Leaf `Cloth3D` unique. New file → source-health; ADR `00xx-cloth3d.md`.

## 6. Tests

- **Settle (C unit):** 10-segment chain pinned at top settles with total length within 0.5% of rest length; energy decays monotonically post-settle (fail-before: no API).
- **Determinism:** 1 k steps replay bit-identical; substep accumulator invariant to frame-dt slicing (16 ms steps vs 4×4 ms produce identical states at aligned times).
- **Collision:** chain draped over a capsule rests outside radius (min-distance assert); tracked body moving through pushes points.
- **Bone aim:** bone-bound chain — child bone directions match point directions each step; base animation still owns the root anchor.
- **Wind/flag:** patch in constant wind reaches a steady billow (mean displacement direction ≈ wind, oscillation bounded).
- **Rebase:** floating-origin parity (near-origin vs post-rebase captures identical, existing harness).

## 7. Verification gates

Full build + ctest; determinism gate (in-step simulation); SW golden for the cape/banner probe; `-L graphics3d`; `-L slow`; surface audits.

## 8. Risks & constraints

- **Stiffness vs iterations:** 4 iterations at 1/120 substeps is the tuned default; long chains stretch under fast motion — the docs give the knob order (iterations → substep → damping).
- **Bone-chain rigs vary:** chains assume a linear child chain from the root bone; branching chains trap with a clear diagnostic.
- **Cost:** a cape (12 points) is trivial; a 32×24 banner is ~2.3 k constraint solves ×4 ×substeps — fine on main thread, but cap patches at 64×64 and document.
- Cloth never feeds back into rigid physics (one-way coupling) — by design, stated in docs.
