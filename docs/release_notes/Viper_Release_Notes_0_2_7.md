# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A continuation of the v0.2.6 hardening cycle, concentrated on the Graphics3D runtime. The work makes 3D animation, skinning, and asset import fail closed on malformed input — non-finite values, invalid handles, stale private references, and overflow-prone counts can no longer reach renderer or simulation state — and closes the remaining open-world ("3D Next Level") gaps with new cell-streaming and navmesh-serialization tooling, a recorded spatial-index speedup, and a broad regression-test and documentation expansion.

- **3D animation and skinning correctness.** Non-finite interpolation and playback inputs clamp across Skeleton3D, AnimController3D, BlendTree3D, IKSolver3D, and the node animator; long state names canonicalize, looping seeks wrap, near-duplicate keyframes collapse, and AnimBlend3D reuses scratch buffers instead of allocating per update. CPU skinning and morphing skip invalid bone influences and revert malformed output to the source vertex.
- **Asset-import hardening.** glTF validates morph-target accessors, CUBICSPLINE index math, sampler component counts, and non-finite skin weights before building animation data; FBX samples through a clamped binary search that rejects non-monotonic curve intervals; Model3D guards scene-name and imported-resource allocation against overflow and auto-plays the correct default clip for long imported names.
- **Broad fail-closed sweep.** A robustness pass across the rest of the 3D runtime — scene nodes, transforms, raycasts, navigation, physics, terrain, vegetation, water, particles, audio, and Game3D plumbing, plus every asset-import and backend-upload path — rejects bad handles and non-finite or overflow-prone inputs and repairs geometry and count state. Canvas3D clamps queued bone counts and requires an active palette before advertising GPU skinning, and a new internal `rt_anim_blend3d_get_skeleton` helper is classified as internal surface policy.
- **Open-world streaming and navmesh tooling (new).** `WorldStream3D` scene cells now load a referenced binary `sidecar` payload into their resident-byte budget (`getCellSidecarBytes`, freed on unload), and its inspection APIs are documented/tested as manifest-indexed. `NavMesh3D.Export` now writes `VNAVMSH2` assets with geometry, source/current triangles, traversal costs, blocked flags, area labels, off-mesh links, obstacles, and agent params; `Import` remains compatible with legacy `VNAVMSH1` geometry files and rebuilds adjacency/query grids on load.
- **Texture streaming budget closure.** Native compressed TextureAsset3D uploads in Metal, D3D11, and OpenGL now use a per-mip block-row cursor, so tight texture-upload budgets can drain a large compressed mip across multiple frames instead of uploading the whole mip at once.
- **Coverage, docs, and a recorded cull baseline.** New behavioral tests close the vegetation, sprite/decal billboard, and navmesh round-trip gaps; 24 previously-undocumented Graphics3D/Game3D methods (joint motors and limits, streaming metadata getters, overlay draws) are now documented; and the Scene3D spatial index gains a recorded indexed-vs-flat cull-speedup baseline (~1,800× on a 10k-node fixture).

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 4 | +4 |
| Source files | 3,096 | 3,097 | +1 |
| Production SLOC | 669K | 677K | +8K |
| Test SLOC | 278K | 287K | +9K |
| Demo SLOC | 192K | 192K | +0 |

Counts via `scripts/count_sloc.sh` (production 676,728 / test 286,634 / demo 192,140 / source files 3,097); commits since the `v0.2.6-dev` tag.

---

Demos and docs tracked the work: the open-world software baseline and the Game3D showcase sample were refreshed alongside the runtime changes, and `docs/viperlib/graphics/` plus a new cross-platform verification runbook gained the streaming, navmesh-export, physics-joint, and spatial-index entries.

<!-- END DRAFT -->
