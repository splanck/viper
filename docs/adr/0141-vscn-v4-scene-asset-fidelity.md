---
status: active
audience: contributors
last-verified: 2026-07-20
---

# ADR 0141: Preserve Complete Scene Assets in VSCN v4

## Status

Accepted (2026-07-20)

## Context

VSCN versions 1 and 2 preserve a single runtime scene hierarchy, meshes,
materials, decoded texture pixels, lights, and LOD state. Version 3 adds
skeletons, skeletal animation clips, skin palette maps, and additional vertex
influences. That is sufficient for `SceneGraph.Save`, but not for baking a
complete imported `SceneAsset`.

The existing `zanna asset bake` implementation first instantiates scene zero
and then calls `SceneGraph.Save`. This necessarily discards secondary scenes,
scene cameras, node/object/morph animation clips, material-variant names and
tables, and static morph-target payloads before serialization starts. It can
also discard enumerable meshes or materials that are not reachable from scene
zero. The bake diagnostics expose those losses, but the native baked format
must be capable of avoiding them.

Changing the VSCN schema and adding a runtime save entry point changes both an
asset contract and the runtime C ABI, so repository policy requires an ADR.

## Decision

### Runtime surface

`Zanna.Graphics3D.SceneAsset` gains:

```text
Save(path: String) -> Int64
MorphTargetCount: Int64
MorphShapeCount: Int64
GetMorphTarget(meshIndex: Int64) -> MorphTarget3D
```

backed by:

```c
int64_t rt_model3d_save(void *asset, rt_string path);
int64_t rt_model3d_get_morph_target_count(void *asset);
int64_t rt_model3d_get_morph_shape_count(void *asset);
void *rt_model3d_get_morph_target(void *asset, int64_t mesh_index);
```

It returns one only after a complete VSCN file has been flushed and atomically
replaced at the destination; it returns zero for an invalid receiver, invalid
path, validation/allocation failure, or I/O failure. Existing
`SceneGraph.Save` remains available and retains its single-live-scene
semantics. `zanna asset bake` calls `SceneAsset.Save`, then reloads the output
and produces its existing fidelity report. The two morph counts make static
morph persistence enumerable in that report; `GetMorphTarget` uses a mesh
index (not a compact morph-only index) so it stays aligned with `GetMesh`.

### VSCN v4 document

A `SceneAsset.Save` operation always emits version 4. Versions 1 through 3
remain accepted without reinterpretation. Version 4 keeps the established
shared `textures`, `cubemaps`, `materials`, `skeletons`, `animations`, and
`meshes` tables and adds:

- `nodeAnimations`: named fixed-duration clips containing target name and
  import-node index, path, interpolation, key times, values, and cubic in/out
  tangents;
- `cameras`: projection parameters, world eye position, and the complete view
  matrix for every camera referenced by an immutable scene;
- `variantNames`: the ordered material-variant display names;
- `scenes`: ordered immutable scene records, each with a name, its own nested
  node hierarchy, and indexes into the shared camera table;
- `importIndex` on a node: the stable source-node identity used to disambiguate
  animation targets whose display names are duplicated;
- `variantMaterials` on a node: one material-table index or `-1` per variant;
- `morphTargets` on a mesh: the vertex count and ordered named shapes, including
  current weights and complete position, optional normal, and optional tangent
  delta streams.

Binary floating-point streams use explicitly labelled little-endian IEEE-754
formats and Base64. Counts and decoded byte lengths are validated before
allocation. Texture content remains canonical decoded RGBA pixels: container
compression is intentionally not preserved, while texels, dimensions,
material texture slots, UV transforms, wrap/filter state, and cubemap faces
remain semantically identical.

The established material record now writes all twelve custom shader parameters
(rather than only the first eight) and per-texture-slot anisotropy. This is
required for imported glTF extension state such as `KHR_materials_anisotropy`;
older readers ignore the additional array lanes and object member.

Version 4 serializes the `SceneAsset` inventories before walking the scene
trees, so valid enumerable resources survive even when no node in the default
scene references them. Shared resources are pointer-deduplicated and every
node/scene reference is an index into the corresponding table. The first
`scenes` entry is the default scene returned by `SceneGraph.Load` and used by
`SceneAsset.Instantiate`; secondary entries remain addressable through
`InstantiateSceneAt`.

### Loading and ownership

The VSCN parser continues to build a default `SceneGraph`, but a private loaded
asset payload carries the v4 resource inventories, immutable scene roots,
camera associations, node animation clips, and variant names until the
`SceneAsset` wrapper claims them. This carrier is not a public object layout or
registry surface. It owns retained runtime handles and native names/index
arrays and releases all of them if parsing fails or if a VSCN is loaded only as
a `SceneGraph`.

Loading is stage-then-publish. All table entries and scene records must parse,
all indexes must be in range, and every decoded stream must have the exact
declared size before the returned scene or asset is published. No partial v4
asset is returned.

### Compatibility and bounds

- VSCN versions 1 through 3 keep their existing behavior and schema.
- `SceneGraph.Save` may continue to emit version 2 or 3 when no v4 asset carrier
  is involved; callers that need complete imported-asset fidelity use
  `SceneAsset.Save`.
- Version 4 rejects unknown binary layout labels, malformed Base64, non-finite
  animation/camera/morph values, invalid resource indexes, inconsistent morph
  vertex counts, excessive nesting, and payloads over the existing 256 MiB
  VSCN document limit.
- The implementation uses no external library and must behave the same on
  macOS, Windows, and Linux.

## Consequences

- Offline bakes can preserve multiple scenes, cameras, skeletal and node
  animation, morph targets, material variants, textures, and unreferenced but
  enumerable resources in one dependency-free file.
- `SceneGraph.Save` remains a focused runtime-scene snapshot instead of being
  burdened with metadata that a live scene does not own.
- Version 4 files are intentionally rejected by older runtimes, while the new
  runtime continues to load every older VSCN version.
- Bake fidelity diagnostics remain useful as a regression oracle: a supported
  source asset should report no reduced resource class after a v4 round trip.

## Alternatives Considered

- **Add more fields to the scene-zero v3 save.** Rejected because secondary
  scene roots and their camera membership have already been discarded before
  that saver runs.
- **Store all imported metadata permanently on every live SceneGraph.** Rejected
  because authored runtime scenes do not own immutable asset inventories and
  should not pay that lifetime or API cost.
- **Write one VSCN per source scene.** Rejected because it breaks shared-resource
  identity, variant and animation ownership, and the single-output bake
  contract.
- **Preserve source image/container compression byte-for-byte.** Rejected as a
  format-container concern rather than scene fidelity; VSCN stores canonical
  renderable texels and sampler semantics.
