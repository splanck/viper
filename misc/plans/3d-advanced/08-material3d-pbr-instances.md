# Plan 08: Material3D PBR and Material Instances

## Goal

Upgrade `Material3D` from a mostly Blinn-Phong-centric API into a modern material surface that supports:

- metallic/roughness PBR
- ambient occlusion and emissive controls
- material instances for cheap per-object overrides
- clean importer mapping from glTF

The current shading-model and custom-param hooks are useful, but they are not a substitute for an actual material workflow.

## Verified Current State

- `Material3D` currently exposes `SetShadingModel` and `SetCustomParam`.
- the guide documents toon, fresnel, emissive, and unlit variants
- glTF metallic/roughness is still down-converted to Blinn-Phong in `rt_gltf.c`
- there is no material-instance API

## Status of Older Plans

- `misc/plans/3d/11-pbr-materials.md`: not implemented
- `misc/plans/3d/16-material-shader-hooks.md`: partially implemented via shading model and custom-param support

This plan preserves the useful low-level hooks that landed, but moves the primary user experience to a first-class PBR surface.

## API Shape

Extend `Viper.Graphics3D.Material3D`.

### New constructors and methods

- `Material3D.NewPBR(r, g, b)`
- `Clone()`
- `MakeInstance()`
- `SetMetallic(value)`
- `SetRoughness(value)`
- `SetAO(value)`
- `SetEmissiveIntensity(value)`
- `SetAlbedoMap(pixels)`
- `SetMetallicRoughnessMap(pixels)`
- `SetAOMap(pixels)`
- `SetEmissiveMap(pixels)`
- `SetNormalScale(value)`
- `SetAlphaMode(mode)`
- `SetDoubleSided(i1)`

### Compatibility policy

- `SetShadingModel` and `SetCustomParam` stay public
- older non-PBR paths remain valid
- glTF import should prefer native PBR fields instead of approximating them into shininess

## Internal Design

Keep one material class instead of adding `PBRMaterial3D`.

Recommended internal model:

- core material struct with workflow enum
- eager-copy material objects for `Clone()` / `MakeInstance()`
- shared retained texture/resource pointers for the currently assigned maps
- independent scalar and map replacement after cloning/instancing

This keeps importers, scene nodes, scene serialization, and render backends simpler than maintaining either multiple public material classes or a live parent-linked override graph.

## Implementation Phases

### Phase 1: runtime material surface

Files:

- `src/runtime/graphics/rt_material3d.h`
- `src/runtime/graphics/rt_material3d.c`
- `src/il/runtime/runtime.def`
- `src/runtime/graphics/rt_graphics_stubs.c`

Work:

- add metallic/roughness/AO/emissive fields
- add clone/instance APIs
- preserve current material behavior for non-PBR workflows

### Phase 2: backend support

Files:

- `src/runtime/graphics/vgfx3d_backend_*.c`
- backend shared headers

Work:

- extend material parameter packing
- implement PBR lighting for all shipped backends
- start with direct-light PBR first

### Phase 3: importer integration

Files:

- `src/runtime/graphics/rt_gltf.c`
- `src/runtime/graphics/rt_fbx_loader.c`

Work:

- map glTF metallic/roughness directly
- map double-sided and alpha modes where possible
- keep importer fallbacks explicit when source data exceeds runtime support

### Phase 4: instance optimization and polish

Work:

- share textures and immutable base fields
- avoid deep-copying materials for repeated instantiation

## Testing

### Runtime tests

- PBR fields round-trip correctly
- `MakeInstance()` shares immutable data but keeps overrides independent
- glTF import preserves metallic/roughness values rather than converting them away

### Render verification

- add backend-facing material packing tests where possible
- if image-golden tests already exist for 3D rendering, add one metallic and one rough material scene

### Test files

- new `src/tests/runtime/RTMaterial3DTests.cpp`
- extend importer tests as needed
- update `src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp`

## Documentation

- update the material section of `docs/graphics3d-guide.md`
- explicitly document when to use PBR versus alternate shading models
- document `SetCustomParam` as an advanced escape hatch, not the main PBR API

## Risks and Non-Goals

Risks:

- backend divergence if each renderer invents its own parameter interpretation
- instances can become expensive if they deep-copy textures or shader state

Non-goals:

- arbitrary user-authored shaders in v1
- full image-based lighting before direct-light PBR is stable
