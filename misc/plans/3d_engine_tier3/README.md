# 3D Engine Tier-3 Feature Plans

Remaining engine-capability items from the fourth 3D runtime review (43-item
report + roadmap). The review's Tier-3 list contained six features; two shipped
with the review's fix pass:

- **Raycast vehicle physics** — landed (`rt_vehicle3d.c`, `Vehicle3D` class,
  suspension raycasts + load-scaled friction circle, unit-tested).
- **8-influence GPU skinning** — landed (extra-influence stream on Metal at
  buffer(10), `gpu_skinning_extras` capability bit, geometry-keyed cache).

The four features here need coordinated shader work across all four backends
(Metal, D3D11, OpenGL, software). Only Metal + software compile on the macOS
dev machine, so each plan isolates its GL/D3D11 patches into static-reviewable
string edits plus a validation checklist for a machine that runs them. The
backend conformance harness (`scripts/run_backend_conformance.sh`, SW golden
vs GPU image diff) is the acceptance gate for every plan in this directory —
extend `conformance_scene.zia` with the feature, then require the cross-backend
diff to stay within tolerance.

| Plan | Feature | Prereqs |
| --- | --- | --- |
| 01 | Screen-space contact shadows | none (postfx snapshot pattern) |
| 02 | Froxel volumetric fog | clustered-lighting grid (shipped) |
| 03 | GPU particle simulation | compute/transform-feedback per backend |
| 04 | Crowd skinning palettes + animation LOD | 8-influence stream (shipped) |
| 05 | Device-loss / resize fuzz scaffolding | D3D11/GL build machine |

Ordering: 01 is self-contained and cheapest. 02 reuses 01's depth plumbing
review. 03 and 04 are independent of each other. 05 is reliability tooling
that should ride along with whichever plan first touches a Windows/Linux box.
