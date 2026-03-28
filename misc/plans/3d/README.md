# 3D Graphics Runtime — Implementation Plans

Ordered by priority. Each plan is independently implementable.

> **Verification note (2026-03-27):** All plans verified against actual source code.
> Several bugs from the original audit were already fixed. Plans updated to reflect
> actual state. Items marked ~~strikethrough~~ in plan files are already resolved.

## Bug Fixes

| # | Plan | Confirmed Issues | Already Fixed |
|---|------|-----------------|---------------|
| 01 | [P0 Crash Fixes](01-p0-crash-fixes.md) | **DONE** — Sprite3D fixed + Mesh3D.Clear added + 9 tests | 2 already fixed |
| 03 | [Memory Leak Fixes](03-memory-leaks.md) | **DONE** — Canvas3D clear optimized, skeleton verified, Mesh3D.Clear + Sprite3D cache from Plan 01 | 3 already fixed |
| 04 | [FBX Animation Fixes](04-fbx-animation.md) | **DONE** — Crossfade rewritten with TRS+SLERP + 2 tests | 3 already correct |
| 07 | [Audio3D Fixes](07-audio3d-fixes.md) | **DONE** — Per-voice max_distance tracking table | 1 already correct |
| 08 | [NavMesh Fixes](08-navmesh-fixes.md) | **DONE** — O(n²) → O(n) edge hash adjacency + 2 tests | Heap+mallocs already safe |
| 13 | [Miscellaneous Fixes](13-misc-fixes.md) | 2 confirmed (OBJ dedup, normal transform) + 4 need verification | 3 (perspective matrix, friction, set_static) |

## Core Features (No existing implementation)

| # | Plan | What It Enables | Effort |
|---|------|----------------|--------|
| 02 | [Physics Overhaul](02-physics-overhaul.md) | **PARTIAL** — Sphere narrow-phase + character controller + collision events DONE. Angular dynamics + joints deferred. | Large |
| 05 | [Spot Lights + Ortho Camera](05-spot-lights-ortho-camera.md) | **DONE** — NewSpot with cone attenuation + NewOrtho camera + 5 tests | Medium |
| 06 | [Collision Callbacks + Joints](06-collision-callbacks-joints.md) | **DONE** — Callbacks (Plan 02) + DistanceJoint3D + SpringJoint3D + 5 tests | Large |
| 14 | [Angular Velocity + Joints](14-angular-velocity-joints.md) | Rotational dynamics, torque, distance/hinge/ball/spring joints | Large |

## Visual Quality (No existing implementation)

| # | Plan | What It Enables | Effort |
|---|------|----------------|--------|
| 09 | [Terrain Splatting](09-terrain-splatting.md) | **DONE** (baked) — API added. Plan 15 upgrades to per-pixel. | Medium |
| 15 | [Per-Pixel Splatting](15-per-pixel-splatting.md) | Replaces baked splat with per-pixel shader splatting (SW + Metal). OGL/D3D11 deferred. | Medium |
| 10 | [Animation State Machine + IK](10-animation-state-machine.md) | Proper character animation, foot placement | Large |
| 11 | [PBR Materials](11-pbr-materials.md) | Physically-based rendering (metallic/roughness) | Large |
| 12 | [Shadow Improvements](12-shadow-improvements.md) | Soft shadows, cascaded shadow maps | Medium |

## Verified Facts About Current State

These items were flagged as bugs but are actually working correctly:
- Canvas3D division-by-zero: already guarded with `cs < 0.001f` check
- Camera3D asin NaN: already clamped with `fmax(-1.0, fmin(1.0, fy))`
- Physics3D friction: fully implemented (Coulomb model with tangential impulse)
- Physics3D set_static(false): correctly restores inv_mass
- Water3D mesh: properly reused (vertex/index count reset, not reallocated)
- Particles3D material: cached via `cached_material` field
- FBX keyframe extraction: fully implemented (lines 1190-1400)
- FBX bind pose: reads full TRS (translation + Euler rotation + scale)
- Animation duration loop: uses `fmodf` correctly
- Audio3D stereo panning: cross product is correct
- Perspective matrix: `rt_mat4_perspective` and Camera3D match exactly

## Recommended Execution Order

1. **02** → Physics narrow-phase (biggest correctness gap)
2. **04** → Animation crossfade fix (only remaining animation bug)
3. **05** → Spot lights + ortho camera (enables new game genres)
4. **07** → Audio3D max_distance fix (small, self-contained)
5. **08** → NavMesh O(n²) fix (performance)
6. **13** → Misc fixes (OBJ dedup, normal transform)
7. **03** → Memory leak fixes (add mesh clear API, verify others)
8. **01** → P0 crash fix (verify sprite3d)
9. **14** → Angular velocity + joints (builds on #2)
10. **10** → Animation state machine + IK
11. **09** → Terrain splatting
12. **11** → PBR materials
13. **12** → Shadow improvements
