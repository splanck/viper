# Zia/BASIC consumer feasibility

The scale tier is **runtime-first and mostly internal C**. The job system,
spatial index, floating origin, streaming engine, solver, navmesh baker, IK
solver, and texture upload/transcode pipeline are not Zia surface; they are C
subsystems behind small control/capability getters. So the Zia-consumer risk is
low and largely already settled by `3Dnextlevel`.

## Status legend

- **Confirmed** — a checked-in test/example exercises it.
- **Inherited** — proven by `3Dnextlevel`; this plan reuses it unchanged.
- **Confirm-0** — pin down in a Phase-0 spike.
- **Internal** — C-only; no Zia surface needed.

| Feature Zia consumers use | Used for | Status | Proof / note |
|---|---|---|---|
| Runtime namespace bind + alias | `Viper.Game3D` / `Viper.Graphics3D` calls | Inherited | `tests/runtime/game3d_surface_probe.zia` |
| Enums → Integer coercion | streaming/quality/joint-type/LOD codes | Inherited | `tests/zia_runtime/33_enum_runtime.zia` |
| Optional types + `?.` + `??` | `AssetHandle3D.getEntity() -> Entity3D?`, `Model3D.GetCamera(scene, i) -> Camera3D?` | Inherited | `tests/zia_runtime/48_optional_method_chain.zia` |
| Fluent chaining | `WorldStream3D`/`BlendTree3D`/joint builders | Inherited | first-plan evidence |
| `Seq[T]` returns | `Scene3D.QueryAABB -> Seq[SceneNode3D]` | Inherited | existing `Seq` return APIs in runtime |
| New classes via `runtime.def` | `Viper.Game3D.WorldStream3D`, `Viper.Game3D.AssetHandle3D`, `Viper.Graphics3D.IKSolver3D`, `Viper.Graphics3D.TextureAsset3D`, `Viper.Graphics3D.BlendTree3D`, `Viper.Graphics3D.HingeJoint3D`, `Viper.Graphics3D.RopeJoint3D`, `Viper.Graphics3D.SixDofJoint3D` | Confirmed-by-pattern | same registration path as existing Graphics3D/Game3D classes, plus appended `RT_G3D_*_CLASS_ID` sentinels |
| Handle objects (`AssetHandle3D`) | async asset residency | Confirm-0 | confirm GC ownership/finalizer for async handles |
| **Closure callbacks to native loops** | `world.run(update)`, `onCollision`, streaming callbacks | **Waived ergonomic gap (CO-2)** | W-001; managed Zia callbacks lack a VM trampoline. Manual loop / `runFramesOnly` is the proven fallback and authoritative path. |

## Phase-0 confirmation scope

1. **Async handle lifetime in Zia.** Confirm `AssetHandle3D` (and any future
   streaming handle) is a normal GC-managed runtime object with a clean
   finalizer, and that `getEntity()` returning `Entity3D?` composes with
   `?.`/`??`.
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
