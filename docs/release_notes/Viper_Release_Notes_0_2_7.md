# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A focused continuation of the v0.2.6 hardening cycle, concentrated on the Graphics3D runtime. 3D animation, skinning, and asset import now fail closed on malformed input — non-finite values, invalid or stale handles, wrong-class private references, and overflow-prone counts can no longer reach renderer or simulation state — the remaining open-world ("3D Next Level") gaps close with new cell-streaming and navmesh-serialization tooling and a recorded spatial-index speedup, and a deep review-and-refactor pass tightens correctness and overflow safety across the asset, scene, and physics surfaces. Outside the renderer, the CLI tooling layer gains a bytecode-VM instruction budget and real program exit codes, a bytecode disassembler, and hardened temp-file, source-loading, and packaging paths.

- **3D animation & skinning fail closed.** Non-finite interpolation and playback inputs clamp across Skeleton3D, AnimController3D, BlendTree3D, IKSolver3D, and the node animator; long state names canonicalize, looping seeks wrap, near-duplicate keyframes collapse, and AnimBlend3D reuses scratch buffers instead of allocating every update. CPU skinning and morphing skip invalid bone influences and revert malformed output to the source vertex; the 2D blend tree snaps a degenerate blend to the nearest sample, and the glTF cubic-spline sampler coerces non-finite curve time.
- **Asset import is contract-checked.** glTF validates morph-target accessors, CUBICSPLINE index math, sampler component counts, and non-finite skin weights before building animation data; FBX samples through a clamped binary search that rejects non-monotonic intervals; Model3D guards scene-name and resource allocation against overflow. `extensionsRequired` now accepts only extensions with a working parser path and rejects the rest, while best-effort extensions are honored only when listed as merely used.
- **Robustness sweep with overflow-safe math.** Scene nodes, transforms, raycasts, navigation, physics, terrain, vegetation, water, particles, audio, and Game3D plumbing reject bad handles and non-finite or overflow-prone inputs and repair geometry and count state. Vector lengths and normalizations factor out the largest component so large but finite coordinates no longer square to infinity, and `Mesh3D.Transform` computes its invertibility determinant in double precision.
- **Reference safety across private slots.** Stored camera, node, character, navmesh, and animator references are class-checked before they are used or released — a slot whose memory was reused for a different-class object is nulled rather than dereferenced or double-released — and the audio-source, texture-atlas, and node-animator tables compact corrupt entries before any read.
- **Scene & streaming residency serialized and repaired.** `Scene3D.Save` persists `Mesh3D.Resident` state across VSCN round trips, traversals skip wrong-class private root/child slots, visibility-zone and portal counters clamp and repair before PVS traversal, and `WorldStream3D` rechecks loaded payload sizes against the residency budget before publishing telemetry.
- **Open-world streaming & navmesh tooling (new).** `WorldStream3D` scene cells load a referenced binary `sidecar` payload into their resident-byte budget (`getCellSidecarBytes`, freed on unload). `NavMesh3D.Export` writes versioned `VNAVMSH2` assets — geometry, traversal costs, blocked flags, area labels, off-mesh links, obstacles, and agent params — while `Import` reads them plus legacy `VNAVMSH1` geometry, rebuilding adjacency and query grids on load.
- **Texture-upload budget closure.** Native compressed `TextureAsset3D` uploads in Metal, D3D11, and OpenGL advance a per-mip block-row cursor, draining a large compressed mip across several frames instead of all at once; loads also validate native KTX2 mip byte lengths and reject unsupported supercompression.
- **Targeted runtime fixes.** Legacy cached Physics3D primitive shapes stay queryable and collidable, `Mesh3D.Clone` repairs corrupt counts before copying, Water3D rewrites its retained mesh buffers in place and range-reduces wave phase over long runtimes, Vegetation3D draws double-sided instead of mutating canvas backface culling, and `Assets3D.ClearCache` leaves the cache intact when active publishers do not quiesce within its wait window.
- **Coverage & a recorded cull baseline.** New behavioral tests close the vegetation, sprite/decal billboard, navmesh round-trip, reference-repair, and texture-atlas gaps, and the Scene3D spatial index records a ~1,800× indexed-vs-flat cull-speedup baseline on a 10k-node fixture.
- **VM instruction budget & real exit codes.** `BytecodeVM.setMaxInstructions` traps with an interrupt once the step budget is exceeded, and the shared `viper run` / `vbasic` / `zia` executor now returns the program's `main` value as the process exit code — flagging values outside the host `int` range — instead of always exiting `0`.
- **CLI toolchain hardening.** `il-dis` becomes a bytecode disassembler (decoded instructions, pools, and side tables); native-compile temporaries are reserved atomically (`O_CREAT|O_EXCL`) to close a unique-name race; the source and IL loaders enforce a 256 MB ceiling with out-of-memory handling; project-manifest path resolution and exclude matching factor into shared helpers; and `install-package` gains Windows/macOS signing plumbing.

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 10 | +10 |
| Source files | 3,096 | 3,098 | +2 |
| Production SLOC | 669K | 682K | +13K |
| Test SLOC | 278K | 289K | +11K |
| Demo SLOC | 192K | 192K | +0 |

Counts via `scripts/count_sloc.sh` (production 682,293 / test 289,294 / demo 192,151 / source files 3,098); commits since the `v0.2.6-dev` tag.

---

Demos and docs tracked the work: the open-world software baseline and the Game3D showcase sample were refreshed alongside the runtime changes; the largest asset-import, scene, render, and terrain routines were decomposed into focused in-file helpers with no ABI change; `docs/graphics3d-guide.md`, `docs/viperlib/graphics/`, and a new cross-platform verification runbook gained the KTX2, glTF-extension, streaming, navmesh-export, physics-joint, and spatial-index entries; 24 previously-undocumented Graphics3D/Game3D methods were documented; and a Doxygen pass covered the Graphics3D/Game3D runtime files and functions plus every source file under the tools layer (frontend drivers, language servers, IL utilities, the packaging library, and the runtime generator).

<!-- END DRAFT -->
