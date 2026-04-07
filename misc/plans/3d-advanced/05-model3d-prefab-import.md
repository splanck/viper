# Plan 05: Model3D and Prefab-Style Import/Instantiation

## Goal

Create a single high-level 3D asset API for imported content:

- load FBX, glTF, or native scene assets through one entry point
- keep meshes, materials, skeletons, animations, lights, cameras, and node hierarchy together
- instantiate that asset into a live `SceneNode3D` subtree or `Scene3D`

The current import surface is fragmented. `FBX` is relatively capable, `GLTF` is partial, and `Scene3D.Load` is a separate structural serializer.

## Verified Current State

- `FBX` already exposes meshes, skeleton, animations, materials, and morph targets.
- `GLTF` currently exposes only meshes and materials.
- `Scene3D.Load` exists, but it is not a unified authoring/import API and does not replace importer-level asset access.
- documentation still understates current FBX animation support.

## Status of Older Plans

- `misc/plans/3d/15-fbx-loader.md`: largely implemented
- `misc/plans/3d-formats/01-gltf-loading.md`: partially implemented
- `misc/plans/3d-formats/06-scene-export.md`: partially represented via `Scene3D.Save/Load`

This plan does not throw that work away. It wraps and normalizes it.

## API Shape

Introduce `Viper.Graphics3D.Model3D` as the preferred high-level asset type.

### `Model3D` core API

- `Model3D.Load(path)`
- `get_MeshCount`
- `get_MaterialCount`
- `get_SkeletonCount`
- `get_AnimationCount`
- `get_NodeCount`
- `Instantiate()`
- `InstantiateScene()`
- `GetMesh(index)`
- `GetMaterial(index)`
- `GetSkeleton(index)`
- `GetAnimation(index)`
- `FindNode(name)`

### Import behavior

- `.fbx` -> FBX adapter path
- `.gltf` / `.glb` -> glTF adapter path
- `.vscn` -> native scene path

### Compatibility policy

- keep `FBX` and `GLTF` as lower-level extractor APIs
- steer game code toward `Model3D` in docs and examples

## Internal Design

Recommended layering:

1. importer-specific loaders build importer-native asset structs
2. adapter layer converts those into a normalized `Model3DAsset`
3. `Instantiate()` walks the normalized node graph and allocates live `SceneNode3D` objects

The normalized asset should carry:

- node hierarchy and local transforms
- mesh/material references
- optional skins and animation clips
- optional cameras/lights
- import metadata such as source path and node names

This avoids duplicating importer logic while making instantiation semantics consistent.

## Implementation Phases

### Phase 1: normalized model asset

Files:

- new `src/runtime/graphics/rt_model3d.h`
- new `src/runtime/graphics/rt_model3d.c`
- `src/il/runtime/runtime.def`
- `src/il/runtime/classes/RuntimeClasses.hpp`
- `src/il/runtime/RuntimeSignatures.cpp`
- `src/runtime/CMakeLists.txt`
- `src/runtime/graphics/rt_graphics_stubs.c`

Work:

- add `Model3D`
- implement adapter layers for existing FBX and `Scene3D.Load`

### Phase 2: glTF scene completeness

Files:

- `src/runtime/graphics/rt_gltf.h`
- `src/runtime/graphics/rt_gltf.c`

Work:

- add node hierarchy extraction
- add skeleton/skin extraction
- add animation extraction
- add camera/light extraction if present

### Phase 3: instantiation semantics

Files:

- `src/runtime/graphics/rt_scene3d.h`
- `src/runtime/graphics/rt_scene3d.c`
- `src/runtime/graphics/rt_scene3d_vscn.c`

Work:

- instantiate root node trees
- preserve local transforms and names
- attach mesh/material/skeleton bindings
- optionally instantiate to a new `Scene3D`

### Phase 4: prefab polish

Work:

- decide whether a dedicated serialized prefab format is needed now or later
- if needed, add a `Model3D.Save` path backed by a versioned native format rather than overloading importer formats

## Testing

### Runtime tests

- loading a glTF with nodes produces the expected hierarchy
- FBX and glTF instantiation preserve node names and transforms
- imported skeleton and animation clips can be played after instantiation
- `Scene3D.Load` can be adapted into `Model3D.InstantiateScene`

### API audit examples

- one asset imported via `Model3D.Load`
- one example showing `Instantiate()` plus animation playback

### Test files

- new `src/tests/runtime/RTModel3DTests.cpp`
- existing importer tests expanded as needed
- `src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp`

## Documentation

- add `Model3D` to `docs/graphics3d-guide.md`
- correct stale FBX-animation wording
- clearly separate low-level importer APIs from the preferred game-facing asset API

## Risks and Non-Goals

Risks:

- importer normalization can accidentally erase source-specific data needed later
- `Instantiate()` can become too magic if ownership and sharing rules are not explicit

Non-goals:

- DCC re-export
- arbitrary editor metadata persistence in v1
