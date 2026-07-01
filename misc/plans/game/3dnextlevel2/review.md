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
| Public API naming | New classes and methods must match existing runtime style: low-level `Viper.Graphics3D` keeps PascalCase methods/properties, while stateful `Viper.Game3D` objects keep the existing lower/camel ergonomic style (`World3D.tick`, `Entity3D.worldPosition`, `BodyDef.get_restitution`). |
| Namespace ownership | Low-level render/physics/nav/animation types belong under `Viper.Graphics3D`; ergonomic gameplay wrappers belong under `Viper.Game3D`. |
| Class registration / ABI | Every new public class needs `RT_CLASS_BEGIN`, an appended permanent `RT_G3D_*_CLASS_ID`, real + disabled-graphics stubs, docs, and ctests; never renumber existing 3D class ids. |
| Async asset handles | Replace generic `ModelHandle` with `Viper.Game3D.AssetHandle3D`, with explicit `ready`, `progress`, `getEntity`, `getTemplate`, `cancel`, and `error` semantics. |
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
| Duplicate material texture setters | Throw out `SetTextureAsset` / `SetNormalMapAsset` style duplicates. Extend the existing `Material3D.SetTexture` / `SetAlbedoMap` / `SetNormalMap` / `SetSpecularMap` / `SetEmissiveMap` validation to accept `TextureAsset3D`. |
| Mutable `Model3D.SelectScene` | Throw out. Cached `Model3D` assets should stay immutable; add explicit scene-indexed queries / `InstantiateSceneAt(index)` instead. |

## Refined API Naming Rules

- Runtime class names must be fully qualified in the plan, e.g.
  `Viper.Game3D.WorldStream3D`, not just `WorldStream`.
- New public runtime objects should end in `3D` unless they are static helper
  namespaces already matching existing Game3D style (`Assets3D`, `Materials`,
  `Prefab`, `Quality`).
- Low-level `Viper.Graphics3D` methods/properties use PascalCase:
  `QueryAABB`, `RaycastNodes`, `SolverIterations`, `ContactCount`.
- Stateful `Viper.Game3D` methods/properties use the current ergonomic style:
  `setCenter`, `setRadii`, `update`, `workerCount`, `floatingOrigin`,
  `residentCellCount`, `ready`, `progress`.
- Static Game3D helper namespaces keep PascalCase factories/actions:
  `Assets3D.LoadModelAsync`, `Assets3D.LoadModelTemplateAsync`,
  `Prefab.Box`, `Quality.Apply`.
- New public classes append class ids in `rt_graphics3d_ids.h`; never reuse or
  renumber existing `RT_G3D_*_CLASS_ID` constants.
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

## Source-comparison pass (2026-05-29)

A second pass verified every `file:line` evidence claim in the plan against the
current `src/runtime/graphics/3d/` tree. The plan's **diagnosis is sound** — all
five walls and the secondary gaps reproduce against source — but several
**evidence pointers had drifted** and a few characterizations were stale. All of
the following are now corrected in the plan files.

### Path drift (systematic — now fixed everywhere)

The 3D runtime was refactored into per-domain subdirectories (`scene/`,
`physics/`, `nav/`, `anim/`, `render/`, `world/`, `assets/`, `audio/`,
`backend/`); only `rt_game3d.c` remains at the top level. Every flat `rt_*.c`
citation (`rt_scene3d.c`, `rt_physics3d.c`, `rt_terrain3d.c`, …) was repointed to
its subdir. Line numbers mostly survived the move (content shifted little within
files): `MAX_LIGHTS`/`MAX_SHADOW_LIGHTS` 308–309, terrain cap 216, node `double`
77 / cull `float` 102, per-node LOD 2012, `draw_node` walk 2100, navmesh
string-pull 663 and `agent_radius` 80 all verified exact.

### Factual corrections

| Claim (old) | Corrected to |
|---|---|
| "**four** real GPU backends (Metal/D3D11/OpenGL)" (`README §1`) | **three** GPU backends + software (4 backends total) |
| `3dbowling` "~5.3K lines" | **2943 lines** of `.zia` (~2.9K); zero asset/skeleton/animation use re-confirmed |
| GJK: "convex hull is triangle soup" | originally corrected to a true gap; the implementation now uses support-point GJK/EPA for convex hull contacts against other hulls and simple primitives, while triangle meshes keep BVH-pruned surface tests |
| model-cache mutex "dormant" | the mutex is **actively used** to serialize the shared model cache (lock/unlock around the load path; broadcast at `:478`); there is simply no worker *pool* |
| `examples/3d` "only an 18-line triangle.gltf" | now has `walk_min.zia`, `game3d_showcase/`, `game3d_starter/`, `game3d_hello.zia`, a `baselines/` PNG — only the *model asset* is still triangle.gltf |

### Line-number drift (now fixed)

`rt_game3d.c` model-cache mutex 439→460, `LoadModel` 3733→4012, `Preload`
3785→4048; OBJ `.mtl` skip 2165→1172/2175; occlusion comment 443→450; physics
broadphase qsort →3691; joint solve loop →4424; single-contact "reserved" note
→4814.

### Naming

Joint type macros use the `RT_JOINT_*` spelling. The current set is
`RT_JOINT_DISTANCE 0`, `RT_JOINT_SPRING 1`, `RT_JOINT_HINGE 2`,
`RT_JOINT_ROPE 3`, and `RT_JOINT_SIXDOF 4`. Public classes use the low-level
`Viper.Graphics3D.*` PascalCase naming, with the symbol-friendly
`SixDofJoint3D` spelling.

### Doc paths

`graphics3d-architecture.md` lives at **`docs/`** (not `docs/viperlib/graphics/`);
the API pages (`physics3d.md`, …) live at `docs/viperlib/graphics/`. References
and the Phase-12 docs task were corrected to the right locations.

### Structural addition: `metal.md`

The plan had `directx.md` (D3D11) and `opengl.md` (OpenGL) but **no Metal
companion**, despite Metal being a ~4.3K-line backend and macOS the primary dev
platform. A `metal.md` was added with the same per-phase structure. Its defining
Metal-specific concern is the **Apple-Silicon BC↔ASTC compression split** (no
BC on Apple Silicon, no ASTC/ETC2 on Intel/AMD Macs → Phase 11 must probe GPU
family and software-fall-back), plus Metal's compute availability (unlike GL 3.3)
and runtime MSL compilation. The README package index and the Phase-11 roadmap
line now reference it.
