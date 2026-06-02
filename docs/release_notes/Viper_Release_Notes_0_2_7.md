# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.7 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.6 was cut on 2026-06-01. -->

### What this release is about

A continuation of the v0.2.6 hardening cycle, so far concentrated on the Graphics3D runtime. The work since v0.2.6 makes 3D animation, skinning, asset import, and the broader 3D runtime fail closed on malformed input — non-finite values, invalid handles, stale private references, and overflow-prone counts can no longer leak into renderer or simulation state — backed by a large regression-test expansion.

- **3D animation and skinning correctness.** Non-finite interpolation and playback inputs clamp across Skeleton3D, AnimController3D, BlendTree3D, IKSolver3D, and the node animator; long animation-state names canonicalize, looping seeks wrap, near-duplicate keyframes collapse, and AnimBlend3D reuses scratch buffers instead of allocating per update. CPU skinning and morphing skip invalid bone influences and revert malformed output to the source vertex.
- **Asset-import hardening.** glTF validates morph-target accessors, CUBICSPLINE index math, sampler component counts, and non-finite skin weights before building animation data; FBX samples through a clamped binary search that rejects non-monotonic curve intervals; Model3D guards scene-name and imported-resource allocation against overflow and auto-plays the correct default clip for long imported names.
- **Render-path guards.** Canvas3D clamps queued bone counts to the runtime limit and requires an active palette before advertising GPU skinning; Scene3D keeps final and previous palette counts independent so inherited skinned child meshes render before a previous-frame palette exists; the Game3D animator drains event-buffer overflow between frames.
- **Broad fail-closed robustness pass.** A sweep across the rest of the 3D runtime — scene nodes, transforms, raycasts, navigation, physics, terrain, vegetation, water, particles, audio, and Game3D entity/controller plumbing, plus every asset-import and render/backend-upload path — rejects bad handles and non-finite or overflow-prone inputs and repairs geometry and count state. A new internal `rt_anim_blend3d_get_skeleton` helper is classified as internal surface policy rather than frontend API.

### By the Numbers

| Metric | v0.2.6 | v0.2.7 | Delta |
|---|---|---|---|
| Commits | — | 3 | +3 |
| Source files | 3,096 | 3,096 | +0 |
| Production SLOC | 669K | 677K | +8K |
| Test SLOC | 278K | 286K | +8K |
| Demo SLOC | 192K | 192K | +0 |

Counts via `scripts/count_sloc.sh` (production 676,536 / test 285,973 / demo 192,140 / source files 3,096); commits since the `v0.2.6-dev` tag.

---

Demos: the Game3D showcase sample and the open-world software baseline were refreshed alongside the runtime changes.

<!-- END DRAFT -->
