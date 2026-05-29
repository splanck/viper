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
   engine** — four real GPU backends (Metal/D3D11/OpenGL) plus a software
   rasterizer behind one vtable, PBR + Blinn-Phong, shadow mapping, a full
   post-FX chain, glTF/FBX/OBJ/VSCN import, skeletal animation with a state
   machine, terrain, water, vegetation, particles, navmesh A*, and spatial audio.
   It can build a **bounded, level-based or hub-based 3D adventure today** and do
   it well.
2. It is **not architecturally ready for a *full open world*** — a seamless,
   km-scale, streamed world with many NPCs. The blockers are specific and
   structural, not cosmetic. The review named **five walls**, and an open world
   hits all five:

| # | Wall | Evidence (from review) |
|---|---|---|
| 1 | **No world streaming / partition** | Whole scene must fit in RAM; terrain is one heightmap hard-capped at 4096² (`rt_terrain3d.c:216`); all loaders are whole-file, all-or-nothing |
| 2 | **Single-threaded + blocking loads** | Zero worker threads in `src/runtime/graphics/3d/`; `Assets3D.LoadModel`/`Preload` are synchronous; "All operations are main-thread-only" (`graphics3d-architecture.md:485`) |
| 3 | **No scene spatial acceleration** | `draw_node` always walks every child (`rt_scene3d.c:2100`); cull is O(total nodes), not O(visible); no octree/BVH/grid |
| 4 | **Physics scale & stability** | One contact point per pair (`physics3d.md:145` "ContactCount = 1"), un-iterated solver (crates won't stack), brute-force O(triangles) mesh narrow-phase, no GJK, hinge/rope joints documented but absent |
| 5 | **No floating origin** | Node transforms are `double` (`rt_scene3d_internal.h:77`) but cull bounds + vertices + GPU pipeline are `float`; precision degrades a few km from origin |

The review also found secondary gaps that an open world needs: no occlusion
culling, a forward renderer capped at 16 lights / 2 shadow casters, no inverse
kinematics, no agent avoidance / navmesh auto-generation / off-mesh links, manual
mesh LOD only, no GPU texture compression, and no glTF camera/multi-scene import.
And it flagged a **proven-vs-theoretical** gap: the one substantial 3D sample
(`3dbowling`, ~5.3K lines) is fully procedural and uses **zero**
model-loading/skeleton/animation, so the asset→skinned-character pipeline is
test-proven but not game-proven.

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
| Navigation | Baked single-surface navmesh, A*, single-agent follow | Navmesh auto-gen + tiled/streamable mesh + dynamic carving + off-mesh links + local avoidance |
| Animation | 4-bone skinning, state machine, root motion | IK (foot/look-at), true additive layers, blend trees, retargeting, animation LOD |
| Asset pipeline | Raw-RGBA textures; no camera import | GPU texture compression + KTX2/precompressed blocks + streaming mips + glTF camera/multi-scene; Basis/Draco/meshopt as stretch |

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
- **No new asset *formats*.** Add decoders (KTX2, Draco, meshopt) to existing
  formats; do not invent containers. VSCN remains the native scene format and may
  gain a streamed/tiled variant.
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
| (deterministic   camera-relative    (cull + query +      world partition     |
|  sim policy)     double precision    broadphase)         terrain tiles/cells |
|                                                                              |
| Visibility            Lighting          Physics depth      Nav/AI depth      |
| occlusion + auto-LOD  clustered + CSM    manifold+solver,   autogen+tiled     |
| + HLOD/impostors      (>16 lights)       GJK/EPA, joints    +avoidance+links  |
|                                                                              |
| Animation depth (IK, additive, blend trees, retarget)                       |
| Asset pipeline depth (GPU tex compression, KTX2/precompressed blocks,       |
|                       streaming mips, glTF camera/multi-scene; stretch      |
|                       Basis/Draco/meshopt)                                  |
+------------------------------------------------------------------------------+
      |
      v
Existing renderer backends (software / Metal / D3D11 / OpenGL) behind vgfx3d vtable
      |
      v
Platform (rt_platform.h / PlatformCapabilities.hpp) — threads, fs, windows
```

## 8. Open decisions before implementation

Resolved in Phase 0 spikes before broad work (tracked in
`progress/02-decisions.md`):

1. **Determinism-under-threads contract.** *Recommended default:* simulation is
   deterministically scheduled (single-threaded or fixed-fork-join with ordered
   merge); workers handle only load/decode/cull/bake/transcode. Confirm this
   keeps `runFrames` VM/native parity (the first plan's determinism gate).
2. **Job-system shape.** Thread-pool + work-stealing deque vs. fixed per-domain
   worker queues. Must sit on `rt_platform.h`; no raw `_WIN32`/`__APPLE__`.
3. **Spatial index choice.** Loose octree vs. BVH vs. uniform grid — and the
   shared query contract. Do **not** require one physical tree for scene culling
   and physics; physics may keep a sibling broadphase if it wins on correctness
   or performance.
4. **Floating-origin strategy.** Periodic origin rebase of the active scene vs.
   per-cell local origins + camera-relative upload. Interaction with the existing
   `double` node transforms and `float` GPU path.
5. **Streaming granularity & format.** Cell size, terrain tile size, and the
   VSCN streaming-manifest shape. No new general-purpose scene format; binary
   sidecars are allowed only as payload storage referenced by VSCN.
6. **Lighting path.** Forward+ (clustered forward, keeps the existing forward
   shaders) vs. deferred (larger rewrite). *Lean forward+* to honor "no renderer
   rewrite".
7. **Sequencing override.** Whether to pull Phase 8 (physics) or Phase 9 (nav)
   ahead of streaming for a physics-/NPC-first game. Default keeps dependency
   order.
8. **Asset-pipeline core vs. stretch.** KTX2/precompressed blocks, streaming
   mips, and glTF camera/multi-scene are core. Basis supercompression,
   Draco, meshopt, and encoders need separate approval or Phase-11b scope.
9. **Scope of v0.3.x.** Which phases are in the first open-world milestone vs.
   later. Phases 0–5 are the minimum for "a small streamed world"; 6–12 raise
   ceiling and fidelity.

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
| `directx.md` | D3D11/Windows-specific implement + test checklist, per phase |
| `opengl.md` | OpenGL/Linux-specific implement + test checklist, per phase (incl. the GL 3.3 ceiling) |
| `progress/` | Gates, phase checklist, decisions, runtime/API/test trackers, waivers |
