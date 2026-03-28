# 3D Graphics Runtime — Implementation Plans

Ordered by priority. Each plan is independently implementable.

> **Verification note (2026-03-27):** All plans verified against actual source code.
> Several bugs from the original audit were already fixed. Plans updated to reflect
> actual state. Items marked ~~strikethrough~~ in plan files are already resolved.

## Bug Fixes

| # | Plan | Confirmed Issues | Already Fixed |
|---|------|-----------------|---------------|
| 01 | [P0 Crash Fixes](01-p0-crash-fixes.md) | 1 (sprite3d use-after-free needs verification) | 2 (div-by-zero, asin clamp) |
| 03 | [Memory Leak Fixes](03-memory-leaks.md) | 1 confirmed (missing mesh clear API) + 3 need verification | 2 (water mesh, particle material) |
| 04 | [FBX Animation Fixes](04-fbx-animation.md) | 1 (crossfade matrix lerp instead of TRS+SLERP) | 3 (keyframe extraction, bind pose, duration loop) |
| 07 | [Audio3D Fixes](07-audio3d-fixes.md) | 1 (global max_distance) | 1 (stereo panning correct) |
| 08 | [NavMesh Fixes](08-navmesh-fixes.md) | 2 (O(n²) adjacency + malloc null checks) | 0 |
| 13 | [Miscellaneous Fixes](13-misc-fixes.md) | 2 confirmed (OBJ dedup, normal transform) + 4 need verification | 3 (perspective matrix, friction, set_static) |

## Core Features (No existing implementation)

| # | Plan | What It Enables | Effort |
|---|------|----------------|--------|
| 02 | [Physics Overhaul](02-physics-overhaul.md) | Sphere/capsule narrow-phase, character controller, angular dynamics, joints, callbacks | Large |
| 05 | [Spot Lights + Ortho Camera](05-spot-lights-ortho-camera.md) | Indoor lighting, isometric/strategy games | Medium |
| 06 | [Collision Callbacks + Joints](06-collision-callbacks-joints.md) | Event-driven game logic, doors/vehicles/ragdoll | Large |

## Visual Quality (No existing implementation)

| # | Plan | What It Enables | Effort |
|---|------|----------------|--------|
| 09 | [Terrain Splatting](09-terrain-splatting.md) | Multi-texture terrain (grass/dirt/rock) | Medium |
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
9. **06** → Collision callbacks + joints (builds on #2)
10. **10** → Animation state machine + IK
11. **09** → Terrain splatting
12. **11** → PBR materials
13. **12** → Shadow improvements
