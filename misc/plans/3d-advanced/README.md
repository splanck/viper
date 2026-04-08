# Advanced 3D Runtime Plans

This directory contains the next implementation wave for Viper's 3D runtime and game-facing APIs. The emphasis is not on adding isolated primitives; it is on closing the workflow gaps that still force examples like `3dbowling` to write a lot of manual glue.

The plans here were written against the current tree, not against the older plan set alone. Each plan assumes the runtime-class pipeline described in `docs/runtime_class_howto.md` and the existing `Graphics3D` public surface in `src/il/runtime/runtime.def`.

## Verified Current State

The current runtime already ships:

- `Canvas3D`, `Mesh3D`, `Camera3D`, `Material3D`, `Light3D`
- `Scene3D` / `SceneNode3D`
- `Audio3D`, `AudioListener3D`, `AudioSource3D`
- `Physics3DWorld`, `Physics3DBody`, `Character3D`, `Trigger3D`
- `DistanceJoint3D`, `SpringJoint3D`
- `Skeleton3D`, `Animation3D`, `AnimPlayer3D`, `AnimBlend3D`
- `FBX`, `GLTF`, `NavMesh3D`, `NavAgent3D`

The highest-value missing layer is orchestration:

- bodies do not expose full rigid-body rotation/orientation control
- colliders are baked into body constructors instead of being reusable assets
- world queries and contact data are too thin for gameplay code
- imported 3D content is not exposed as a unified instantiable asset
- animation still requires manual orchestration in places
- materials now expose a first-class PBR workflow, but higher-level rendering polish still remains

## Old Plan Verification

The older plans were checked against the current source tree before writing this set.

| Older plan | Status in tree | What actually landed | What is still missing | Superseded by |
|---|---|---|---|---|
| `misc/plans/3d/14-angular-velocity-joints.md` | Partial | `DistanceJoint3D`, `SpringJoint3D`, world joint add/remove/count | no angular velocity, torque, orientation, hinge joint, ball joint, structured contact events | `01`, `02`, `04` |
| `misc/plans/3d/10-animation-state-machine.md` | Not implemented as written | none of the proposed 3D FSM / IK types shipped | no 3D controller, no root motion, no events, no IK hooks | `06` |
| `misc/plans/3d/11-pbr-materials.md` | Implemented via the advanced plan | `Material3D.NewPBR`, metallic/roughness/AO/emissive controls, alpha modes, double-sided, importer and scene round-trip support | full image-based lighting still intentionally out of scope | `08` |
| `misc/plans/3d/16-material-shader-hooks.md` | Implemented as a compatibility layer | `Material3D.SetShadingModel`, `Material3D.SetCustomParam`, plus the new PBR workflow and material-instance semantics | custom user-authored shaders still absent | `08` |
| `misc/plans/3d/07-audio3d-fixes.md` | Complete and extended | per-voice `max_distance` tracking plus `AudioListener3D` / `AudioSource3D` landed | doppler, cones, occlusion, and reverb routing still absent | `10` |
| `misc/plans/3d/08-navmesh-fixes.md` | Complete for its scoped fix | edge-hash adjacency, NavAgent3D path following, and binding coverage landed | local avoidance and off-mesh links still absent | `09` |
| `misc/plans/3d/04-fbx-animation.md` | Complete | FBX keyframe extraction, bind-pose handling, and TRS-based crossfade all shipped | higher-level animation-control workflow still absent | `05`, `06` |
| `misc/plans/3d/15-fbx-loader.md` | Largely implemented | FBX mesh, skeleton, animation, material, morph extraction shipped | still exposed as a raw importer, not a unified model/prefab asset | `05` |
| `misc/plans/3d-formats/01-gltf-loading.md` | Partial | glTF mesh/material import shipped | no scene graph, skins, animation, camera/light extraction | `05`, `08` |
| `misc/plans/3d-formats/06-scene-export.md` | Partial | `Scene3D.Save` and `Scene3D.Load` shipped as structural scene I/O | not elevated into a unified prefab/model workflow | `05`, `07` |

## Recommended Implementation Order

1. `01-physics3dbody-rotation-dynamics.md`
2. `02-collider3d-advanced-shapes.md`
3. `03-physics3d-world-queries.md`
4. `04-collisionevent3d-contact-manifolds.md`
5. `05-model3d-prefab-import.md`
6. `06-animcontroller3d.md`
7. `07-scenenode3d-bindings.md`
8. `09-navagent3d.md`
9. `10-audio3d-objects.md`
10. `08-material3d-pbr-instances.md`

That order is deliberate:

- Plans `01` through `04` establish the physics substrate that gameplay code needs.
- Plans `05` through `07` make imported content easy to instantiate and keep in sync.
- Plans `09` and `10` depend on the scene/binding layer for good ergonomics.
- Plan `08` is visually important, but it is the least blocking for gameplay correctness.

## Shared Integration Rules

Every plan in this directory assumes the following Viper-specific integration points:

- runtime C headers and implementations under `src/runtime/graphics/`
- public registration in `src/il/runtime/runtime.def`
- runtime class IDs in `src/il/runtime/classes/RuntimeClasses.hpp` when new classes are added
- `#include` coverage in `src/il/runtime/RuntimeSignatures.cpp`
- graphics build registration in `src/runtime/CMakeLists.txt`
- graphics-disabled stubs in `src/runtime/graphics/rt_graphics_stubs.c`
- export/link smoke coverage in `src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp`
- API documentation updates in `docs/graphics3d-guide.md`
- focused runtime tests under `src/tests/runtime/`
- user-facing audit or sample coverage under `examples/apiaudit/graphics3d/`

## Cross-Cutting Internal Work

Several plans depend on one internal refactor that is worth doing once instead of repeatedly:

- move `Physics3DWorld` broadphase from simple pair scanning toward an explicit queryable acceleration structure

Without that, advanced colliders, world queries, contact manifolds, and nav/character integrations will all duplicate expensive shape enumeration logic. The plans below call this out where relevant, but it should be treated as shared infrastructure, not as a one-off implementation detail.
