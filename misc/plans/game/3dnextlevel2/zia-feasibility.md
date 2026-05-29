# Zia/BASIC consumer feasibility

The scale tier is **runtime-first and mostly internal C**. The job system,
spatial index, floating origin, streaming engine, solver, navmesh baker,
IK solver, and texture transcoder are not Zia surface — they are C subsystems
behind small control/capability getters. So the Zia-consumer risk is low and
largely already settled by `3Dnextlevel`.

## Status legend

- **Confirmed** — a checked-in test/example exercises it.
- **Inherited** — proven by `3Dnextlevel`; this plan reuses it unchanged.
- **Confirm-0** — pin down in a Phase-0 spike.
- **Internal** — C-only; no Zia surface needed.

| Feature Zia consumers use | Used for | Status | Proof / note |
|---|---|---|---|
| Runtime namespace bind + alias | `Viper.Game3D` / `Viper.Graphics3D` calls | Inherited | `tests/runtime/game3d_surface_probe.zia` |
| Enums → Integer coercion | streaming/quality/joint-type/LOD codes | Inherited | `tests/zia_runtime/33_enum_runtime.zia` |
| Optional types + `?.` + `??` | `ModelHandle.get() -> Entity3D?`, `Model3D.GetCamera(i) -> Camera3D?` | Inherited | `tests/zia_runtime/48_optional_method_chain.zia` |
| Fluent chaining | `WorldStream`/`BlendTree3D`/joint builders | Inherited | first-plan evidence |
| `Seq[T]` returns | `Scene3D.queryAABB -> Seq[SceneNode3D]` | Inherited | existing `Seq` return APIs in runtime |
| New classes via `runtime.def` | `WorldStream`, `IK3D`, `BlendTree3D`, `HingeJoint3D`, `RopeJoint3D`, `SixDOFJoint3D` | Confirmed-by-pattern | same registration path as existing Graphics3D classes |
| Handle objects (`ModelHandle`) | async asset residency | Confirm-0 | confirm GC ownership/finalizer for async handles |
| **Closure callbacks to native loops** | `world.run(update)`, `onCollision`, streaming callbacks | **Blocked (CO-2)** | W-001; managed Zia callbacks lack a VM trampoline. Manual loop / `runFramesOnly` is the proven fallback. |

## Phase-0 confirmation scope

1. **Async handle lifetime in Zia.** Confirm `ModelHandle` (and any future
   streaming handle) is a normal GC-managed runtime object with a clean
   finalizer, and that `get()` returning `Entity3D?` composes with `?.`/`??`.
2. **Determinism visible to Zia tests.** Confirm `runFramesOnly` stays
   bit-reproducible with the job pool enabled (Phase 1 gate) so subsystem
   `*_probe.zia` tests remain deterministic.
3. **Capability gating in Zia.** Confirm `BackendSupports("occlusion"/"bc7"/
   "rt-finalize"/...)` reads cleanly so samples branch without traps.

## Fallbacks if a feature regresses

- **Callback sugar (CO-2)** → manual `tick`/`stepSimulation`/frame loop and
  `runFramesOnly` (proven). Authoritative APIs stay poll/handle-based, so no Zia
  ergonomic gap blocks the runtime-first design.
- **Async handles** → synchronous `LoadModel` (already shipped) with a loading
  screen between zones.
- **New joint/IK classes** → if a class registration is awkward, expose factory
  functions on the owning world/skeleton instead.

As in the first plan, none of the fallbacks block delivery; they only trade
sample ergonomics. The authoritative surface is the C runtime.
