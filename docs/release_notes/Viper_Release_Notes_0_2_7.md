# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A focused continuation of the v0.2.6 hardening cycle, concentrated on the Graphics3D runtime. 3D animation, skinning, and asset import now fail closed on malformed input — non-finite values, invalid handles, stale private references, and overflow-prone counts can no longer reach renderer or simulation state — and the remaining open-world ("3D Next Level") gaps close with new cell-streaming and navmesh-serialization tooling, a recorded spatial-index speedup, and a broad regression-test and documentation pass.

- **3D animation & skinning fail closed.** Non-finite interpolation and playback inputs clamp across Skeleton3D, AnimController3D, BlendTree3D, IKSolver3D, and the node animator; long state names canonicalize, looping seeks wrap, near-duplicate keyframes collapse, and AnimBlend3D reuses scratch buffers instead of allocating every update. CPU skinning and morphing skip invalid bone influences and revert malformed output to the source vertex.
- **Asset-import hardening.** glTF validates morph-target accessors, CUBICSPLINE index math, sampler component counts, and non-finite skin weights before building animation data; FBX samples through a clamped binary search that rejects non-monotonic curve intervals; Model3D guards scene-name and imported-resource allocation against overflow.
- **Broad 3D robustness sweep.** Scene nodes, transforms, raycasts, navigation, physics, terrain, vegetation, water, particles, audio, and Game3D plumbing — plus every asset-import and backend-upload path — reject bad handles and non-finite or overflow-prone inputs and repair geometry and count state.
- **Scene and streaming residency are serialized and repaired.** `Scene3D.Save` now persists `Mesh3D.Resident` state for VSCN round trips, scene traversals skip wrong-class private root/child slots during count/save/draw/bounds/clear/rebase paths, visibility-zone/portal counters clamp and repair before PVS traversal or append, and `WorldStream3D` rechecks loaded payload sizes against the residency budget before publishing telemetry.
- **Open-world streaming & navmesh tooling (new).** `WorldStream3D` scene cells load a referenced binary `sidecar` payload into their resident-byte budget (`getCellSidecarBytes`, freed on unload). `NavMesh3D.Export` writes versioned `VNAVMSH2` assets — geometry, traversal costs, blocked flags, area labels, off-mesh links, obstacles, and agent params — while `Import` reads them and legacy `VNAVMSH1` geometry, rebuilding adjacency and query grids on load.
- **Texture-upload budget closure.** Native compressed `TextureAsset3D` uploads in Metal, D3D11, and OpenGL advance a per-mip block-row cursor, so a tight upload budget drains a large compressed mip across several frames instead of all at once.
- **3D runtime review fixes.** `TextureAsset3D` now validates native KTX2 mip byte lengths, `Mesh3D.Clone` repairs corrupt private counts before copying, Game3D collision wrappers and camera controllers validate stale private refs, legacy cached Physics3D primitive shapes remain queryable/collidable, Water3D rewrites retained mesh buffers instead of clearing/re-adding topology every update, Vegetation3D uses double-sided material state instead of mutating canvas culling, and `Assets3D.ClearCache` leaves the cache intact if active publishers do not quiesce within its wait window.
- **Coverage & a recorded cull baseline.** New behavioral tests close the vegetation, sprite/decal billboard, and navmesh round-trip gaps, and the Scene3D spatial index records a ~1,800× indexed-vs-flat cull-speedup baseline on a 10k-node fixture.

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 5 | +5 |
| Source files | 3,096 | 3,097 | +1 |
| Production SLOC | 669K | 678K | +9K |
| Test SLOC | 278K | 287K | +8K |
| Demo SLOC | 192K | 192K | +0 |

Counts via `scripts/count_sloc.sh` (production 677,501 / test 286,714 / demo 192,140 / source files 3,097); commits since the `v0.2.6-dev` tag.

---

Demos and docs tracked the work: the open-world software baseline and the Game3D showcase sample were refreshed alongside the runtime changes; `docs/viperlib/graphics/` and a new cross-platform verification runbook gained the streaming, navmesh-export, physics-joint, and spatial-index entries; 24 previously-undocumented Graphics3D/Game3D methods were documented; and a Doxygen pass covered the Graphics3D/Game3D runtime files and functions.

<!-- END DRAFT -->
