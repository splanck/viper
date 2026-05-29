# Plan review and refinement notes

Date reviewed: 2026-05-29

This review treats the `3dnextlevel2` plan as the next engine-scale roadmap
after `3Dnextlevel`. The core direction is right: do not rewrite the renderer,
add the missing scale tier around it. The refinements below keep that direction
but narrow the first open-world milestone to work that can be implemented,
tested, and shipped without turning the plan into a multi-year research dump.

## Keep

| Area | Why it is good |
|---|---|
| Five-wall diagnosis | Streaming, threading, spatial indexing, physics depth, and floating origin are the real open-world blockers. |
| Foundation-first order | Jobs, coordinates, and spatial indexing are correct prerequisites for streaming and scale. |
| Opt-in flags | Existing bounded-scene games must remain byte-compatible with all new systems off. |
| Software baseline | The software backend is the right correctness oracle for visual features and headless tests. |
| Capability gates | `Canvas3D.BackendSupports(name)` already exists and should remain the primary backend feature probe. |
| Progress trackers | The plan has enough tracker granularity to keep scope honest if the rows are maintained. |

## Tweak

| Area | Required refinement |
|---|---|
| Public API naming | New classes and methods must match existing runtime style: PascalCase classes with `3D` suffix when they represent 3D runtime objects, PascalCase methods, PascalCase properties exposed as `get_`/`set_` bindings. |
| Namespace ownership | Low-level render/physics/nav/animation types belong under `Viper.Graphics3D`; ergonomic gameplay wrappers belong under `Viper.Game3D`. |
| Async asset handles | Replace generic `ModelHandle` with `Viper.Game3D.AssetHandle3D`, with explicit `Ready`, `Progress`, `GetEntity`, `GetTemplate`, `Cancel`, and `Error` semantics. |
| World streaming class | Rename `WorldStream` to `Viper.Game3D.WorldStream3D` to match `World3D`, `Entity3D`, and `EffectRegistry3D`. |
| IK class | Rename generic `IK3D` to `Viper.Graphics3D.IKSolver3D`; expose Game3D wrappers only through `Animator3D`. |
| Six-DOF joint name | Use `Viper.Graphics3D.SixDofJoint3D` for symbol friendliness; keep docs spelling "6DOF". |
| Spatial index | Do not require one physical tree to serve scene culling and physics. Require one shared query contract and allow separate tuned indexes behind it. |
| Streaming container | Do not invent a new general scene format. Prefer a VSCN streaming manifest/extension plus binary sidecars for tile payloads. |
| LOD scope | Ship runtime LOD selection, authored HLOD, and impostors first. Treat automatic mesh simplification as a later tool/bake feature. |
| Phase C accuracy | Carryover rows must reflect what is already implemented after the 2026-05-29 Graphics3D/Game3D hardening pass. |

## Throw Out Or Demote

| Item | Decision |
|---|---|
| CO-2 as a blocker for scale work | Demote to ergonomic work. The authoritative APIs must remain poll/handle/manual-loop based until a VM callback trampoline exists. |
| One spatial structure for culling, queries, and physics | Throw out as a hard requirement. Keep a shared interface; let physics keep a sibling broadphase if that is faster/cleaner. |
| From-scratch Basis Universal transcoder as a Phase-11 gate | Demote. First milestone should load already-transcoded KTX2/BCn/ASTC/ETC2 blocks and fall back to software RGBA decode. Basis supercompression can be Phase 11b. |
| Draco/meshopt decode as a vertical-slice gate | Demote to optional import-coverage work. It is valuable, but not required to prove open-world streaming. |
| Auto-generated mesh LOD as the first LOD deliverable | Demote behind manual/authoring-time HLOD and runtime selection. |
| GPU occlusion queries as the cross-backend default | Demote. They can be backend accelerators, but software occluder/PVS is the portable correctness path. |
| Public user-authored job APIs | Throw out for this milestone. The job system should be internal-first with only `World3D` control/telemetry. |

## Refined API Naming Rules

- Runtime class names must be fully qualified in the plan, e.g.
  `Viper.Game3D.WorldStream3D`, not just `WorldStream`.
- New public runtime objects should end in `3D` unless they are static helper
  namespaces already matching existing Game3D style (`Assets3D`, `Materials`,
  `Prefab`, `Quality`).
- Method names use existing runtime style: `SetCenter`, `SetRadii`, `Update`,
  `QueryAABB`, `RaycastNodes`, `LoadModelAsync`, `SetWorkerCount`.
- Properties use PascalCase names: `Ready`, `Progress`, `ResidentCellCount`,
  `WorkerCount`, `FloatingOrigin`, `WorldOrigin`.
- Capability strings are lower-case kebab-like tokens at the backend boundary:
  `"rt-finalize"`, `"occlusion"`, `"clustered-lighting"`, `"shadow-csm"`,
  `"bc7"`, `"astc"`, `"etc2"`.

## Refined Milestone Split

| Milestone | Scope |
|---|---|
| M0: Carryover + gates | Cross-platform graphics3d lane, perf lane, skinned fixture, remaining diagnostics/docs. |
| M1: Small streamed world | Phases 0-5: jobs, coordinate policy, spatial query contract, async assets, world/terrain streaming. |
| M2: Scale performance | Phases 6-7: visibility/LOD/HLOD, clustered lighting, CSM. |
| M3: Simulation depth | Phases 8-10: physics, navigation/AI, animation/IK. |
| M4: Asset depth + vertical slice | Phase 11 core texture pipeline plus Phase 12 sample. Basis/Draco/meshopt remain stretch unless separately approved. |

These refinements are reflected in `README.md`, `api-spec.md`,
`runtime-changes.md`, `roadmap.md`, and the progress trackers.
