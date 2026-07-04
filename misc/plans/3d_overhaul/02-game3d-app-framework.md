# Plan 02 — Game3D Application Framework (Scenes, Behaviors, Input Map, Bone Sockets)

## 1. Objective & scope

Give 3D games the application-tier framework the 2D layer already has. Every shipped-looking 3D demo hand-rebuilds the same layer: game loop + dt clamp, screen-state machines and menus (ridgebound `menu3d.zia` 469 lines, 3dbowling `menu.zia` 353 lines), per-entity update logic, and key polling. The 2D stack solved this with `Viper.Game.SceneManager`, `Viper.Game.Behavior`, `Viper.Input.Action`, and `examples/games/lib/gamebase.zia` ("eliminates ~100+ lines of boilerplate per game") — none of which have a 3D-reachable equivalent.

**In scope:** (a) `GameBase3D`/`IScene3D` Zia framework library; (b) `Behavior3D` C runtime class with preset behaviors + optional script callback; (c) input action mapping usable from Game3D; (d) bone sockets (`Entity3D.AttachToBone`).
**Out of scope:** UI widgets (plan 08), cutscene/timeline, dialogue, save-data conveniences (future).

**Zero external dependencies — absolute.** No ECS/framework/input libraries; everything is pure in-tree C runtime + Zia library code, mirroring the existing from-scratch 2D implementations (`rt_scenemanager.c`, `rt_behavior.c`, `rt_action.c`).

## 2. Current state (verified anchors)

- **2D SceneManager is canvas-agnostic pure C** (`src/runtime/game/rt_scenemanager.c`, 261 lines; def block runtime.def:6275-6287): 16 named scenes, instant + timed transitions, one-shot `just_entered/just_exited` flags, `transition_progress`. It has **no 2D dependency** — 3D games can use `Viper.Game.SceneManager` as-is today.
- **Scene lifecycle callbacks live in Zia, not C**: `examples/games/lib/iscene.zia` (`interface IScene { update(dt); draw(canvas); onEnter(); onExit(); }`) + `gamebase.zia` (`GameBase` holds `currentScene/pendingScene`, `NullScene` sentinel; loop at `run():88-139`: poll → dt clamp (50 ms max) → ScreenFX update → deferred scene swap at frame boundary (`:113-121`) → `update`/`draw` → flip; fade transitions via `Viper.Game.ScreenFX`).
- **2D Behavior** (`src/runtime/game/rt_behavior.c`, 335 lines; def:6148-6160): flag bitmask `BHV_PATROL/CHASE/GRAVITY/EDGE_REVERSE/WALL_REVERSE/SHOOT/SINE_FLOAT/ANIM_LOOP` (`:28-35`), params struct (`behavior_impl:100-129`), ticked by `rt_behavior_update(bhv, entity, tilemap, target_x, target_y, dt)` (`:236`) calling `rt_entity_*` accessors. No closures.
- **Entity3D** struct `rt_game3d_entity` (`rt_game3d_internal.h:139-158`): `id, node, mesh, material, body, anim, layer, collision_mask_bits, name, world, parent, children, group, alive, spawned, destroyed`. Impl `rt_game3d_entity.c` (873 lines); def:14935-14999.
- **World3D sim step** `game3d_world_step_simulation_impl` (`rt_game3d.c:1554-1578`), verified order: `game3d_asset_async_drain_commits` → dt clamp → `game3d_world_update_controller` → `game3d_world_update_animations` → `rt_world3d_step(physics)` → `rt_scene3d_sync_bindings(scene, dt)` → audio prune → `rt_game3d_effects_update` → `game3d_world_late_update_controller` → `game3d_world_rebase_if_needed`. **Behavior tick inserts here.**
- **Script-callback precedent** (the only one in Game3D): the `World3D.Run*` family (`rt_game3d.c:2026-2169`) takes native fn pointers `rt_game3d_update_fn = void(*)(double)` (`:128`); pointers validated by `game3d_callback_pointer_is_native` (`:364`). In interpreted mode, `src/vm/Game3DRuntime.cpp` installs trampolines: `resolveVmCallback` (`:171`) resolves the function ref against the module, `validateUpdateSignature` (`:67`, requires `(F64)->Void`), thread-local `VmGame3DCallbackScope` (`:54`), re-entry via `detail::VMAccess::callFunction` with trap recovery (`invokeGame3DLoopWithScope:142`). runtime.def marks callback params with `RT_BRIDGE(FnId, "none,callback")` (see 15235-15252). **Constraint: callbacks execute on the VM thread that set the scope.**
- **Input**: `Viper.Input.Action` (`src/runtime/graphics/2d/rt_action.c` + `rt_action_io.c` + `rt_action_presets.c`; def flat 5409-5455, class `"none"` 10047-10111) is a process-global registry — define actions/axes, bind keyboard/chords/mouse/pad, query `Pressed/Released/Held/Strength/Axis`, persist `Save()->str`/`Load(str)`, `LoadPreset`. It reads **global device state** (`rt_keyboard_*`, `rt_mouse_*` from `rt_input.h`) — the same globals `rt_game3d_input_update` (`rt_game3d_input.c:101`) snapshots for `Input3D`. So **Action already works in 3D games today** (3dbowling proves it: `game_flow.zia:110-204`); what's missing is pad/axis convenience on `Input3D` and documentation.
- **Bone matrices**: `rt_anim_controller3d_get_bone_matrix` (`rt_animcontroller3d_api.inc:954`) returns a fresh `Mat4` of `controller->final_globals[bone*16]` — **model-space (skeleton-root-relative) global pose**, not world space (header comment at `rt_animcontroller3d.h:121` says "world-space" — it is wrong; fix it). Node world transform: `rt_scene_node3d_get_world_matrix` (`rt_scene3d_node.c:335`, lazy `recompute_world_matrix`). **The per-tick sync traversal where a socket must update:** `scene_node_sync_recursive` (`rt_scene3d_helpers.inc:790-858`) — animators update at `:811`, then body/node sync by `sync_mode`. Game3D `Animator3D` (def:15677-15692) exposes **no** bone accessor today.

## 3. Design

### 3.1 `GameBase3D` / `IScene3D` — Zia framework library (no new C)

Mirror `gamebase.zia` for 3D at `examples/games/lib/gamebase3d.zia` + `iscene3d.zia`:

```zia
interface IScene3D {
    func onEnter(world: World3D);
    func onExit(world: World3D);
    func update(world: World3D, dt: Float);
    func drawOverlay(world: World3D);   // HUD pass, inside BeginOverlay/EndOverlay
}
class GameBase3D {
    var world: World3D; var currentScene: IScene3D; var pendingScene: IScene3D; ...
    func switchScene(next: IScene3D) { ... }          // deferred to frame boundary
    func transitionTo(next: IScene3D, seconds: Float) // overlay fade (full-screen rect alpha ramp)
    func run() {
        while (world.Tick()) {
            applyPendingScene();                       // onExit/onEnter at boundary
            currentScene.update(world, clampDt(world.Dt));
            world.StepSimulation(...); world.BeginFrame(); world.DrawScene(); world.DrawEffects();
            world.EndScene(); world.BeginOverlay(); currentScene.drawOverlay(world);
            drawTransitionFade(); world.EndOverlay(); world.Present();
        }
    }
}
```

- Uses `Viper.Game.SceneManager` internally for named-state queries where useful (it's already 3D-safe), but the scene objects/dispatch are Zia interface calls — same architecture as 2D.
- Fade transition = overlay `DrawRect2D` alpha ramp. **Dependency note:** public `DrawRect2D` has alpha hardcoded to 1.0 (`rt_canvas3d_draw.inc:597-599`); plan 08 step 1 adds `DrawRect2DAlpha`. Until then, fade uses a black rect + stepped dithering or lands after plan 08's primitive commit (preferred: land plan 08 step 1 first — it's small).
- Ship it as a library the demos copy (like `lib/gamebase.zia`), documented in `docs/viperlib/graphics/game3d.md`. If/when a shared stdlib location for Zia exists, migrate.

### 3.2 `Behavior3D` — C runtime class

New `src/runtime/graphics/3d/rt_game3d_behavior.c` (+ decl in `rt_game3d.h`, internals in `rt_game3d_internal.h`), class ID `RT_G3D_GAME3D_BEHAVIOR3D_CLASS_ID` in `rt_graphics3d_ids.h` (next free `-0x6030xx`).

```c
enum { BHV3D_SPIN = 1<<0, BHV3D_ORBIT = 1<<1, BHV3D_SINE_FLOAT = 1<<2,
       BHV3D_LOOK_AT = 1<<3, BHV3D_FOLLOW_PATH = 1<<4, BHV3D_CHASE = 1<<5,
       BHV3D_LIFETIME = 1<<6, BHV3D_FACE_VELOCITY = 1<<7 };
typedef struct rt_game3d_behavior { uint32_t flags; /* per-preset params */ ...
    void *path;            /* Path3D for FOLLOW_PATH */
    void *nav_agent;       /* NavAgent3D for CHASE (optional route) */
    void *target_entity;   /* CHASE/LOOK_AT target */
    double lifetime_remaining; ... } rt_game3d_behavior;
```

- Fluent config mirroring 2D: `AddSpin(axisX,axisY,axisZ,degPerSec)`, `AddOrbit(center..., radius, degPerSec)`, `AddSineFloat(amplitude, speed)`, `AddLookAt(targetEntity)`, `AddFollowPath(path, speed, loop)`, `AddChase(targetEntity, speed, range)` (direct-steer; if a `NavAgent3D` is bound via `SetNavAgent`, chase sets the agent target instead), `AddLifetime(seconds)` (despawns via world), `AddFaceVelocity()`.
- Attach slot on the entity: add `void *behavior;` to `rt_game3d_entity` + `Entity3D.AttachBehavior(behavior)` (fluent, retains via `game3d_assign_ref`, same pattern as `AttachBody`).
- Tick: new `game3d_world_update_behaviors(world, dt)` called in `game3d_world_step_simulation_impl` **after** `game3d_world_update_controller` and **before** `game3d_world_update_animations` (behaviors write node/body targets; physics + sync then act on them). Iterates `world->entities`, skips `!alive || !behavior`. Presets mutate through existing APIs only: `rt_scene_node3d_set_position/rotation`, `rt_body3d_*`, `rt_navagent3d_set_target` — no new mutation paths.
- **Optional script hook** (second commit, separable): `Entity3D.SetUpdateHandler(fn)` with sig `(obj<Entity3D>, f64) -> void`… ONLY if the trampoline cost is justified. Simpler v1 alternative that avoids new VM work: `World3D.SetEntityUpdateHandler(fn)` — one world-level callback `(i64 entityId, f64 dt)` invoked per entity flagged `EnableScriptUpdate()`, reusing the existing `RT_BRIDGE`/`Game3DRuntime.cpp` scope machinery (add `vm_game3d_entity_update_trampoline` beside the run-loop ones, same `(F64)`-style validation extended to `(I64,F64)->Void`). Ship v1 with presets + the world-level hook; per-entity closures deferred.

### 3.3 Input mapping

No new class. **Adopt `Viper.Input.Action` as the canonical Game3D binding layer**:
- Verify/complete pad + mouse-motion binding coverage against `Input3D`'s snapshot semantics (Action reads live globals; Input3D snapshots per tick — document that Action queries inside `GameBase3D.run()` see the same frame state because `World3D.Tick` polls first; add a test).
- Add `docs/viperlib/graphics/game3d.md` §Input rewrite + a `game3d_starter` action-map example; add `Action` preset file for the common 3D scheme (WASD+mouse+pad) via `rt_action_presets.c` (`LoadPreset("fps3d")`).
- Only if snapshot/live divergence proves real in the test: add `Input3D.BindAction(name)` bridging reads to the snapshot arrays (`rt_game3d_input.c`, struct `internal.h:122-135`). Decision recorded in the test, not assumed.

### 3.4 Bone sockets

- Fix the `rt_animcontroller3d.h:121` doc comment (model-space, not world-space).
- New C: `rt_scene_node3d_attach_to_bone(node, animator, bone_index, const float offset[16])` — stores `{bound_socket_animator, socket_bone_index, socket_offset[16]}` on the node (fields added beside the existing `bound_animator/bound_body` binding slots, `rt_scene3d_node.c:220-224`).
- Follow hook in `scene_node_sync_recursive` (`rt_scene3d_helpers.inc:790-858`): after the animator-update step (`:811`) and before children recurse, if the node has a socket binding: `world = parentEntityNodeWorld × final_globals[bone] × offset` → `scene_node_set_world_transform` (the same entry the body-sync path uses at `:825-829`). Ordering guarantee: animators update before sockets read `final_globals` because both happen in this traversal — parent-before-child ordering must hold (socket nodes are children of the skinned entity's node; the traversal is already parent-first).
- Public API: `Animator3D.GetBoneMatrix(boneIndex) -> Mat4` and `Animator3D.FindBone(name) -> i64` (forwarders to the Graphics3D controller/skeleton fns), plus `Entity3D.AttachToBone(child: Entity3D, boneName: str, offset: Transform3D|null) -> obj` — resolves bone by name via the entity's `anim` slot, parents `child.node` under `this.node`, installs the socket binding.

## 4. Implementation steps

1. Bone sockets C core (node fields + sync hook) + unit test (rotating-bone follows; parent-rotated composition test mirroring the IK rotated-parent test pattern).
2. `Animator3D.GetBoneMatrix/FindBone` + `Entity3D.AttachToBone` runtime.def entries + surface audits + doc-comment fix.
3. `Behavior3D` presets + entity slot + sim-step tick + unit tests per preset (deterministic `StepSimulation` scenarios, `showcase.zia` style).
4. World-level script update hook (`RT_BRIDGE` + Game3DRuntime.cpp trampoline + AOT native path) + VM/native parity test.
5. Action preset `"fps3d"` + Input3D/Action same-frame consistency test + docs.
6. `gamebase3d.zia`/`iscene3d.zia` library + rewrite `examples/3d/game3d_starter` onto it (reference usage) + a two-scene transition example.
7. Docs: `game3d.md` new sections (Scenes, Behaviors, Input, Sockets).

## 5. Public API changes (runtime.def)

- New class `Viper.Game3D.Behavior3D` (`"obj"` layout, ctor `Game3DBehavior3DNew`): methods `AddSpin/AddOrbit/AddSineFloat/AddLookAt/AddFollowPath/AddChase/AddLifetime/AddFaceVelocity/SetNavAgent`, all fluent `obj(...)`.
- `Entity3D`: `RT_METHOD("AttachBehavior","obj(obj<Viper.Game3D.Behavior3D>)")`, `RT_METHOD("AttachToBone","obj(obj<Viper.Game3D.Entity3D>,str)")` (+ offset overload), `RT_METHOD("EnableScriptUpdate","obj()")`.
- `Animator3D`: `RT_METHOD("GetBoneMatrix","obj<Viper.Math.Mat4>(i64)")`, `RT_METHOD("FindBone","i64(str)")`.
- `World3D`: `RT_FUNC` + `RT_BRIDGE(..., "none,callback")` for `SetEntityUpdateHandler`.
- Leaf-name check: `Behavior3D` leaf is unique (2D class is `Behavior`). New file `rt_game3d_behavior.c` → bump `scripts/source_health_baseline.tsv`. ADR for the new public surface (pattern: `docs/adr/0064-game3d-character-controller-gravity.md`).

## 6. Tests

- Socket: Given a skinned entity with a spinning bone, When a child entity is attached to that bone, Then the child's world position tracks the bone within 1e-5 across 60 fixed steps (fail-before: no API).
- Behaviors: per-preset deterministic trajectory asserts (orbit radius constant; lifetime despawns at T; chase closes distance; path loops).
- Script hook: VM and native runs produce identical entity positions after N steps (determinism gate).
- Input: action bound to key K reports Held on the same tick `Input3D.IsDown(K)` does.
- Framework: two-scene `gamebase3d` example runs `RunFrames`-style deterministically; transition fires onExit/onEnter exactly once at a frame boundary.

## 7. Verification gates

Full build + ctest; `-L graphics3d` + surface audits (`check_runtime_completeness.sh`, `test_runtime_surface_audit`, leaf-name test) after each def change; `-L slow` before phase close. `game3d_starter` rewrite runs visually identical.

## 8. Risks & constraints

- **VM callback threading:** entity-update trampolines must fire on the VM thread inside the existing scope machinery — never from job-pool workers (`game3d_world_update_animations` parallelism must not be extended to behavior ticks that might call script).
- **Tick-order sensitivity:** behaviors before physics/sync is a semantic commitment — document it; changing later breaks determinism baselines.
- **Scope creep:** per-entity closures, tweens, dialogue are explicitly out; the framework must stay as thin as `gamebase.zia`.
- Plan 08 step 1 (overlay alpha rect) is a soft dependency for fade transitions.
