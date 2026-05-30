# 3D Next Level 2 — from bounded levels to a streamed, interactive open world

> Status: **DRAFT plan.** Successor to `misc/plans/game/3Dnextlevel/`. That plan
> built the `Viper.Game3D` ergonomics layer over the *existing* renderer. This
> plan grows the *engine itself* from a bounded single-scene renderer into one
> that can run a large, streamed, interactive open-world 3D adventure.
>
> Source of record: the 2026-05-29 deep review of the 3D backend
> (`src/runtime/graphics/3d/`), `Viper.Graphics3D`, and `Viper.Game3D`. Every gap
> closed here was identified there with file:line evidence.
>
> This plan also **closes out the verified-unfinished tail of `3Dnextlevel`**
> (verified 2026-05-29; the prior plan is ~90% implemented). That carryover is
> Phase C and is enumerated in `carryover.md`.
>
> Review/refinement entry point: `review.md`. That file records what to keep,
> what to tweak, and what to demote or throw out before implementation.

## 1. Why this exists

The review reached a clear two-part verdict:

1. Viper already has a **genuinely capable, broad, well-hardened mid-tier 3D
   engine** — three real GPU backends (Metal/D3D11/OpenGL) plus a software
   rasterizer behind one vtable, PBR + Blinn-Phong, shadow mapping, a full
   post-FX chain, glTF/FBX/OBJ/VSCN import, skeletal animation with a state
   machine, terrain, water, vegetation, particles, navmesh A*, and spatial audio.
   It can build a **bounded, level-based or hub-based 3D adventure today** and do
   it well.
2. It is **not architecturally ready for a *full open world*** — a seamless,
   km-scale, streamed world with many NPCs. The blockers are specific and
   structural, not cosmetic. The review named **five walls**, and an open world
   hits all five:

> **Source paths below reflect the current subsystem layout.** The 3D runtime
> was refactored into per-domain subdirectories under `src/runtime/graphics/3d/`
> (`scene/`, `physics/`, `nav/`, `anim/`, `render/`, `world/`, `assets/`,
> `audio/`, `backend/`); only `rt_game3d.c` remains at the top level. Citations
> were re-verified against this layout on 2026-05-29 (see `review.md`
> §"Source-comparison pass").

| # | Wall | Evidence (from review) |
|---|---|---|
| 1 | **No world streaming / partition** | Whole scene must fit in RAM; terrain is one heightmap hard-capped at 4096² (`world/rt_terrain3d.c:216`); all loaders are whole-file, all-or-nothing |
| 2 | **Single-threaded + blocking loads** | Zero worker threads in `src/runtime/graphics/3d/`; `Assets3D.LoadModel`/`Preload` are synchronous (`rt_game3d.c:4012`/`4048`); "All operations are main-thread-only" (`docs/graphics3d-architecture.md:485`) |
| 3 | **No scene spatial acceleration** | `draw_node` always walks every child (`scene/rt_scene3d.c:2100`); cull is O(total nodes), not O(visible); no octree/BVH/grid |
| 4 | **Physics scale & stability** | AABB pairs now expose bounded multi-point contact manifolds, but other shape pairs remain one representative point and there is no warm-starting yet; mesh narrow-phase still has brute-force fallback paths (`physics/rt_physics3d.c`); convex hulls now use support-point GJK/EPA for hull and simple primitive contacts; richer 6DOF pose-angle/stability coverage is still needed |
| 5 | **No floating origin** | Node transforms are `double` (`scene/rt_scene3d_internal.h:77`) but cull bounds + vertices + GPU pipeline are `float` (`scene/rt_scene3d_internal.h:102`); precision degrades a few km from origin |

The review also found secondary gaps that an open world needs: no occlusion
culling, a forward renderer capped at 16 lights / 2 shadow casters, no inverse
kinematics, no agent avoidance / navmesh auto-generation / off-mesh links, manual
mesh LOD only, no GPU texture compression, and no secondary glTF scene import.
And it flagged a **proven-vs-theoretical** gap: the largest 3D sample
(`examples/games/3dbowling`, ~2.9K lines of `.zia`) is fully procedural and uses
**zero** model-loading/skeleton/animation (verified: no `LoadModel`/`Skeleton`/
`Animator` calls), so the asset→skinned-character pipeline is test-proven but not
game-proven.

## 2. The reframe: a scale tier on a correct foundation

Like the first plan, this is **not a renderer rewrite**. The scene-level renderer
and the breadth of features are correct and hardened. What is missing is the
**scale tier** — the systems a mature engine adds *on top of* a solid scene
renderer to go from "big level" to "streamed open world".

Be honest about the shape of the work, though: unlike "3D Next Level" (which was
mostly contracts + composition over existing capability), several items here are
**genuinely new subsystems** — a job/threading runtime, async streaming, a
spatial index, a physics solver upgrade, a navmesh baker. They are *additive
layers over a correct foundation*, not a rewrite, but they are real engineering,
not glue.

| Area | Existing capability (keep) | Gap this plan adds |
|---|---|---|
| Concurrency | Single main thread; one dormant model-cache mutex | A deterministic-by-policy job/worker system for non-sim throughput |
| Coordinates | `double` scene-node transforms | Floating origin + camera-relative rendering + `double` cull bounds/physics |
| Spatial structure | Flat scene tree, per-node frustum cull | Octree/BVH index shared by cull, queries, streaming, and physics broadphase |
| Asset loading | Correct, well-tested, **blocking** glTF/FBX/OBJ/VSCN | Async load/decode on workers + main-thread GPU upload queue + residency/streaming |
| World size | One ≤4 km² heightmap, single in-memory scene | Streamable terrain tiles + scene cells loaded/unloaded around the player |
| Visibility | Frustum cull + front-to-back sort | Occlusion culling + automatic mesh LOD + HLOD/impostors |
| Lighting | Forward, 16 lights, 2 shadow casters | Clustered/forward+ many-light path + cascaded shadow maps |
| Physics | Primitives, character controller, raycast BVH, single contact | Contact manifolds + iterated/warm-started solver + BVH mesh narrow-phase + GJK/EPA + hinge/rope/6DOF joints |
| Navigation | Baked single-surface navmesh, scene-flatten bake baseline, A*, single-agent follow | Voxel/region navmesh auto-gen + real tiled/streamable mesh + dynamic carving/removal + off-mesh links + local avoidance |
| Animation | 4-bone skinning, state machine, root motion | IK (foot/look-at), true additive layers, blend trees, retargeting, animation LOD |
| Asset pipeline | Raw-RGBA textures; scene-local glTF cameras + secondary scenes | GPU texture compression + KTX2/precompressed blocks + streaming mips; Basis/Draco/meshopt as stretch |

## 3. Ordering principle — "fundamental" = foundational dependency + breadth of impact

The ask is to fill the gaps **in the order of how fundamental they are to 3D
games**. This plan reads "fundamental" as **how many other systems depend on a
gap, and how close it sits to the engine core** — i.e., a buildable dependency
order, not a popularity ranking. Concretely:

1. **Enablers first.** A job system unblocks all async work; a coordinate
   foundation unblocks all large-world rendering/physics; a spatial index is the
   shared substrate for cull, queries, streaming, and broadphase. Nothing
   above can be built correctly before these.
2. **Then the open world itself** (async streaming → world partition / terrain
   streaming).
3. **Then make it fast and pretty at scale** (visibility, then lighting).
4. **Then deepen simulation** (physics, then navigation/AI).
5. **Then deepen characters and content** (animation/IK, then the asset
   pipeline).
6. **Then prove it end-to-end** (open-world vertical slice + tooling + perf
   baselines).

A team whose game is physics-first or NPC-first may legitimately pull Phase 8 or
9 earlier; the dependency edges that *cannot* be reordered are called out
per-phase in `roadmap.md`. See §8 for the open sequencing decision.

## 4. Goals

1. A **deterministic-by-policy job system** that parallelizes loading, decode,
   culling, navmesh baking, and texture transcoding without breaking VM/native
   determinism for simulation.
2. **Floating origin** so a player can travel kilometres from origin without
   rendering precision collapse or physics jitter.
3. A **spatial acceleration index** so per-frame cost scales with *visible*
   content, not *total* content, and so spatial queries/broadphase share one
   structure.
4. **Async, non-blocking asset streaming** with a main-thread GPU upload queue —
   no frame hitch when content enters range.
5. **World partition + streamable terrain** beyond the single-heightmap ceiling:
   tiles and scene cells loaded/unloaded around the player with seamless seams.
6. **Visibility scaling**: occlusion culling and automatic LOD/HLOD/impostors so
   dense scenes stay within frame budget.
7. **Lighting scaling**: a clustered/forward+ path beyond 16 lights and cascaded
   shadow maps for large outdoor scenes.
8. **Physics depth**: stable stacking (manifolds + iterated/warm-started solver),
   BVH-driven mesh narrow-phase, real convex (GJK/EPA), and the joints the docs
   already promise (hinge/rope) plus 6DOF.
9. **Navigation/AI depth**: navmesh auto-generation, tiled/streamable navmesh,
   dynamic obstacle carving, off-mesh links, agent-radius corridors, and local
   avoidance, scaled to hundreds of agents.
10. **Animation depth**: IK (two-bone foot placement, look-at/aim), true additive
    layers, blend trees/blendspaces, cross-skeleton retargeting, and animation
    LOD.
11. **Asset pipeline depth**: GPU-compressed textures, KTX2 with
    already-transcoded backend blocks first, streaming mips, and glTF camera +
    multi-scene import. Basis supercompression and Draco/meshopt decode are
    stretch items, not vertical-slice gates.
12. A **playable open-world vertical slice** that proves the asset → streamed
    world → many-NPC → animated-character pipeline end-to-end, plus authoring
    tooling hooks and recorded performance baselines.

## 5. Non-goals

- **No third-party dependencies.** Per Viper Core Principle #6, every system here
  is implemented from scratch in the Viper tree. "Recast-style", "Jolt-style",
  and "Basis-style" describe the *technique*, never a library to vendor. For the
  first open-world milestone, do not gate success on heroic from-scratch
  supercodec work; prefer precompressed/transcoded asset inputs and software
  fallbacks, with harder codecs tracked as stretch scope.
- **No renderer rewrite and no new GPU API backend.** Work extends the existing
  software/Metal/D3D11/OpenGL backends behind the existing vtable.
- **No new IL/VM semantics unless forced.** Most work is C runtime. Any change
  that touches IL or VM determinism requires an ADR (Core Principle #1).
- **No Viper-invented asset containers.** Add KTX2 support and optional future
  Draco/meshopt decoders for existing external formats, but do not invent a new
  general scene container. VSCN remains the native scene format and may gain a
  streamed/tiled manifest extension.
- **Not guaranteeing AAA fidelity.** The target is a *shippable, performant*
  open-world adventure on the existing renderer, not photoreal rendering.
- **Not building the ViperIDE level editor here.** This plan exposes the runtime
  hooks an editor needs (cells, partition, residency, navmesh bake); the editor
  itself is tracked under the separate ViperIDE roadmap.

## 6. Design principles

- **Determinism is sacred (Core Principle #5).** Simulation (physics step,
  animation sampling, gameplay) stays deterministic and reproducible across
  VM/native. Worker threads are confined to non-simulation throughput work whose
  results are merged back in a deterministic order. This contract is decided in
  Phase 0 and enforced in every later phase.
- **Cross-platform parity (Core Principle #7).** Every feature ships on macOS,
  Windows, and Linux at the same time. Threading uses the shared
  `rt_platform.h` / `PlatformCapabilities.hpp` abstraction; texture compression
  works on all four backends (software decodes, GPU backends upload native).
- **Zero dependencies (Core Principle #6).** From-scratch implementations only.
- **Software backend is the canonical correctness baseline.** New visual features
  (occlusion, clustered lighting, compressed textures, IK) must have a correct
  software path; GPU backends get capability-gated parity plus smoke checks.
- **Capability-gated, never trapping.** New systems are feature-flagged and
  queryable (`Canvas3D.BackendSupports`, new `World3D` capability getters). A
  backend or platform that lacks a path degrades safely, exactly like the
  existing post-FX/instancing fallbacks.
- **Runtime API names follow the current split.** Low-level `Viper.Graphics3D`
  APIs keep PascalCase methods/properties. Stateful `Viper.Game3D` APIs keep the
  existing lower/camel ergonomic style; only static helper namespaces such as
  `Assets3D`, `Prefab`, and `Quality` use PascalCase factories/actions.
- **Heavy work stays in C; Zia/BASIC consume.** Streaming, partition, solver,
  baker, transcoder, and the job system are C runtime, registered through
  `runtime.def`, with `#ifdef VIPER_ENABLE_GRAPHICS` real-impl + stub pairs and
  `check_runtime_completeness.sh` green.
- **Backward compatible by default.** Every new system is opt-in. A current
  bounded-scene game keeps working unchanged with all flags off (the "small
  level" path must never regress).
- **Measure before and after (Core Principle: Always Green + perf honesty).**
  No scale feature merges without a recorded before/after number on a named
  fixture from the Phase 0 performance harness.

## 7. Architecture

```text
Game code (.zia/.bas)
      |
      v
Public API: Viper.Game3D  (World3D, Entity3D, controllers, Assets3D, ...)
            Viper.Graphics3D  (Canvas3D, Scene3D, Physics3D, NavMesh3D, ...)
      |
      v
+----------------------- NEW: 3D scale tier (this plan) -----------------------+
| Concurrency      Coordinates        Spatial index       Streaming           |
| job/worker pool  floating origin    octree/BVH           async asset I/O     |
| (deterministic   camera-relative    (shared query        world partition     |
|  sim policy)     double precision    contract)           terrain tiles/cells |
|                                                                              |
| Visibility            Lighting          Physics depth      Nav/AI depth      |
| occlusion + auto-LOD  clustered + CSM    manifold+solver,   autogen+tiled     |
| + HLOD/impostors      (>16 lights)       GJK/EPA, joints    +avoidance+links  |
|                                                                              |
| Animation depth (IK, additive, blend trees, retarget)                       |
| Asset pipeline depth (GPU tex compression, KTX2/precompressed blocks,       |
|                       streaming mips, secondary glTF scenes; stretch        |
|                       Basis/Draco/meshopt)                                  |
+------------------------------------------------------------------------------+
      |
      v
Existing renderer backends (software / Metal / D3D11 / OpenGL) behind vgfx3d vtable
      |
      v
Platform (rt_platform.h / PlatformCapabilities.hpp) — threads, fs, windows
```

## 8. Phase 0 decisions

The Phase 0 decision set is closed in `progress/02-decisions.md`. The current
outcomes are:

1. **Determinism-under-threads contract.** Simulation is deterministically
   scheduled; worker results merge in fixed order before visible/runtime state
   changes. `g3d_3dnext2_surface_probe` and `test_rt_game3d` guard the current
   replay surface.
2. **Job-system shape.** Reuse `Viper.Threads.Pool` /
   `Viper.Threads.Parallel` on `rt_platform.h`; do not add a second public
   3D-only job API.
3. **Spatial index choice.** Scene3D's current indexed candidate store is the
   scene cull/query contract. Physics keeps a sibling broadphase until a shared
   tree wins on correctness and performance.
4. **Floating-origin strategy.** Use periodic active-world rebase through
   `World3D.floatingOrigin` and `Scene3D.RebaseOrigin`; per-cell or
   camera-relative upload is a later backend refinement.
5. **Streaming granularity & format.** Use VSCN streaming manifests for cells
   and terrain tiles; sidecars are payload storage only.
6. **Lighting path.** Keep forward shaders and add capability-gated
   clustered/forward+ behavior; no deferred rewrite in this plan.
7. **Sequencing override.** Keep dependency order; only isolated Phase 8/9
   slices may land early.
8. **Asset-pipeline core vs. stretch.** KTX2/precompressed blocks, streaming
   mips, and glTF camera/multi-scene import are core. Basis supercompression,
   Draco, meshopt, and encoders need separate Phase 11b approval.
9. **Scope.** The tracker is capability-driven, not tied to a version label.
   Phases 0–5 prove the small streamed-world base; 6–12 raise scale and
   fidelity.

## 9. Package index

| File | Contents |
|---|---|
| `README.md` | This file: why, reframe, ordering, goals, principles, architecture, decisions |
| `review.md` | Deep-dive review: keep/tweak/throw-out decisions and refined API naming rules |
| `roadmap.md` | Phase C + Phase 0–12 with goals/exits, testing strategy, ctest inventory, acceptance criteria, risks |
| `carryover.md` | Verified-unfinished items from `3Dnextlevel` (Phase C), mapped to prior IDs |
| `runtime-changes.md` | C runtime contracts/subsystems per phase, validation points |
| `api-spec.md` | New/changed public `Viper.Graphics3D` / `Viper.Game3D` surface |
| `zia-feasibility.md` | Whether Zia/BASIC can consume the new surface; what stays internal C |
| `metal.md` | Metal/macOS-specific implement + test checklist, per phase (incl. the Apple-Silicon BC↔ASTC compression split and Metal compute availability) |
| `directx.md` | D3D11/Windows-specific implement + test checklist, per phase |
| `opengl.md` | OpenGL/Linux-specific implement + test checklist, per phase (incl. the GL 3.3 ceiling) |
| `progress/` | Gates, phase checklist, decisions, runtime/API/test trackers, waivers |
