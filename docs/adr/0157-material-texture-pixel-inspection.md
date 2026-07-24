---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0157: Expose Read-Only Material Texture Pixels for Authoring Tools

## Status

Accepted (2026-07-23)

## Context

`Zanna.Graphics3D.Material3D` accepts `Pixels`, `TextureAsset3D`, and
`RenderTarget3D` sources for its texture slots, but its public inspection
surface only reports Boolean `Has*` properties. A renderer can use the retained
source, while an authoring tool cannot show the texture that is already
embedded in a loaded VSCN document.

Zanna Studio's material inspector therefore had to describe assigned maps using
text alone. Caching the path or the pixels only at assignment time would fail
after load, undo, redo, cross-document paste, or imported-material cloning and
would create editor state that can disagree with the canonical scene graph.
Parsing VSCN JSON in Studio would duplicate runtime format logic and bypass its
validation and texture-source rules.

Adding public runtime properties changes the runtime C ABI and registry surface,
so ADR 0006 requires an explicit decision.

## Decision

`Material3D` gains seven additive read-only properties:

```text
TexturePixels: Pixels?
NormalMapPixels: Pixels?
SpecularMapPixels: Pixels?
EmissiveMapPixels: Pixels?
MetallicRoughnessMapPixels: Pixels?
AmbientOcclusionMapPixels: Pixels?
LightmapPixels: Pixels?
```

Their C ABI entry points are:

```c
void *rt_material3d_get_texture_pixels(void *material);
void *rt_material3d_get_normal_map_pixels(void *material);
void *rt_material3d_get_specular_map_pixels(void *material);
void *rt_material3d_get_emissive_map_pixels(void *material);
void *rt_material3d_get_metallic_roughness_map_pixels(void *material);
void *rt_material3d_get_ao_map_pixels(void *material);
void *rt_material3d_get_lightmap_pixels(void *material);
```

Each property returns the currently drawable decoded `Pixels` source for that
slot, or null when the material, slot, or current source has no decoded
fallback. A direct `Pixels` source is returned as-is. `TextureAsset3D` resolves
to its currently resident decoded fallback, and `RenderTarget3D` resolves to its
current material-facing pixels.

The getter is observational. It does not replace the unresolved retained
source, mutate residency, discard encoded provenance, or change VSCN
serialization. The returned object is a managed borrowed reference; callers
that retain it through a language object reference participate in normal
runtime ownership. Graphics-disabled builds return null without trapping.

The environment map is excluded because it is a cubemap rather than one 2D
`Pixels` slot. The original `TextureAsset3D` or `RenderTarget3D` source is also
not exposed: authoring previews need decoded pixels, while source identity and
native compressed payloads remain implementation-owned.

## Consequences

- Authoring tools can show persistent thumbnails from the canonical loaded
  material after load, undo/redo, import, clone, and VSCN round trips.
- Applications can inspect every serializable 2D material map without parsing
  VSCN or reaching into native structs.
- Texture streaming can still make a property temporarily null when no decoded
  fallback mip is resident; `Has*` and the new pixel properties intentionally
  answer different questions.
- Runtime registry metadata, generated API documentation, graphics-disabled
  stubs, native tests, and Studio probes must cover the additive surface.

## Alternatives Considered

- **Cache the assignment path or thumbnail in Studio.** Rejected because the
  cache cannot authoritatively follow scene load, history, paste, or imported
  material sharing.
- **Parse the current VSCN text in Studio.** Rejected because it duplicates the
  runtime serializer contract and can disagree with live scene state.
- **Expose the unresolved texture object.** Rejected because authoring tools
  would then need runtime-class branching and access to private
  `TextureAsset3D` fallback state.
- **Expose only the five maps currently editable in Studio.** Rejected because
  the runtime also serializes specular and lightmap slots; a coherent
  inspection surface should cover every 2D material map.
