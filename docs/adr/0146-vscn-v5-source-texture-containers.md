---
status: active
audience: contributors
last-verified: 2026-07-20
---

# ADR 0146: Preserve Source Texture Containers in VSCN v5

## Status

Accepted (2026-07-20).

## Context

ADR 0141 deliberately made VSCN v4 store every material texture as canonical
decoded RGBA8 pixels. That preserves visible texels, but it destroys useful and
sometimes materially different source state:

- a `TextureAsset3D` loses its KTX2 header, data-format descriptor, key/value
  metadata, supercompression, mip boundaries, and native compressed blocks;
- a baked material reloads with `Pixels`, so GPU-native BC/ETC2/ASTC upload and
  mip streaming are no longer available; and
- embedded PNG, JPEG, GIF, and BMP inputs cannot round-trip their original bytes
  even when the importer had those bytes in memory.

`TextureAsset3D` already retains a canonical packed span of final mip payloads,
but that span is intentionally not the original container: supercompressed KTX2
levels are decompressed and container metadata is omitted. Exact preservation
therefore needs a second immutable backing. Changing retained-memory accounting,
the internal texture bridge, and the VSCN schema requires an ADR.

## Decision

### Immutable source-container backing

`TextureAsset3D` gains an optional immutable byte span plus a normalized container
kind (`ktx2`, `png`, `jpeg`, `gif`, or `bmp`). KTX2 memory, filesystem, and packed-
asset loaders copy the complete validated input into this span. Importers that
decode another supported encoded image wrap the decoded `Pixels` and the exact
encoded bytes in a `TextureAsset3D`; rendering still resolves the ordinary RGBA8
fallback, while source identity remains retained.

Canonical mip backing and source-container backing are separate by design.
Residency eviction may release decoded pixels but never the bytes required to
reconstruct them. `RetainedBytes` includes both spans without double-counting.
All container sizes use the existing texture-file and aggregate importer limits.
The finalizer releases each owned allocation exactly once.

Private C bridges expose the container to trusted serializers/importers without
adding a script-visible byte-array API:

```c
int8_t rt_textureasset3d_get_source_container(
    void *asset, const uint8_t **data, uint64_t *size, const char **kind);
void *rt_textureasset3d_wrap_encoded_pixels(
    void *pixels, const uint8_t *data, uint64_t size, const char *kind);
```

The wrap operation validates the container magic against `kind`, retains the
decoded `Pixels`, copies the complete input transactionally, and returns NULL
with an asset error for malformed data. No feature toggle or configuration is
added.

### Source validity

The decoded image associated with a source container is logically immutable
inside `TextureAsset3D`, but defensive serialization still records the Pixels
generation at construction. If a native/internal caller later mutates that
Pixels object, or if dimensions no longer match, VSCN does not emit stale source
bytes. It emits the current RGBA8 form and the fidelity report records
`texture-source-container: changed-after-import`.

KTX2 native/canonical mip state is reconstructed only from the retained complete
container; VSCN never synthesizes a KTX2 header from mip blocks. If an asset was
created without an exact container, it remains fully renderable but uses RGBA8
serialization.

### VSCN v5 texture table

`SceneAsset.Save` emits VSCN version 5. Versions 1 through 4 remain accepted
without reinterpretation. Each v5 texture table entry is discriminated:

```json
{"kind":"rgba8","width":2,"height":2,"rgbaBase64":"..."}
```

or:

```json
{"kind":"source","container":"ktx2","sourceBase64":"..."}
```

Material slots index the original texture reference rather than the result of
`rt_material3d_resolve_texture_pixels`. Pointer deduplication therefore preserves
one shared `TextureAsset3D` identity across slots and materials. Render-target
textures remain snapshots and serialize as RGBA8.

On load, `source` entries are Base64-decoded under the 256 MiB document and
texture-file limits. KTX2 is reconstructed with the ordinary bounded KTX2 parser;
PNG/JPEG/GIF/BMP is decoded by the existing in-tree codec and wrapped with its
exact bytes. Unknown kinds/containers, invalid magic, decode failures, excess
payloads, or a source whose decoded dimensions are invalid reject the complete
VSCN transaction. No decoded fallback is silently substituted for corrupt source
data.

SceneGraph-only saves use version 5 whenever a reachable material contains a
valid source-backed `TextureAsset3D`; otherwise they may retain their established
v2/v3 behavior. Complete `SceneAsset` saves always use v5. The new reader accepts
v4 `{width,height,rgbaBase64}` entries as RGBA8.

### Importer handoff and fidelity reporting

glTF/GLB and FBX loaders preserve raw bytes whenever they own or can safely stage
the complete encoded payload. This includes data URIs, buffer views, embedded FBX
`Content`, packed assets, and ordinary dependency files. The asynchronous glTF
preloader carries the encoded image dependency alongside any decoded work so
preloading does not reduce later bake fidelity. Failed optional images retain the
existing bounded warning behavior and do not publish an invalid wrapper.

`zanna asset bake` compares texture reference kind, source-container kind and
bytes, dimensions, mip count, compressed/native format, and decoded texels across
save/reload. Its structured fidelity report distinguishes:

- `preserved-source` — exact container bytes and runtime texture kind survived;
- `preserved-decoded` — only canonical decoded texels were available at import;
  and
- `changed-after-import` — a retained decoded surface was mutated, so current
  texels correctly replaced stale source bytes.

The second state is not reported as an importer loss when no source bytes ever
existed. Losing an available exact container is a bake regression.

## Explicit Rejections That Remain

Malformed Base64, mismatched magic/kind, truncated encoded images, unsupported
containers, excessive payloads, impossible dimensions/mip tables, and a decoded
length other than exactly `width * height * 4` reject publication. Source bytes
are data only; VSCN never interprets metadata as a path, URI, executable command,
or additional dependency.

## Consequences

- A baked KTX2 material reloads as `TextureAsset3D` with the same exact container,
  mip hierarchy, native upload capability, and streaming behavior.
- Supported ordinary encoded images can retain provenance without adding another
  public texture class or external codec dependency.
- VSCN files may be larger because exact source bytes are stored instead of, or
  occasionally alongside in memory with, decoded pixels. Existing size ceilings
  keep the cost bounded.
- Older runtimes intentionally reject v5; the new runtime continues to load all
  prior VSCN versions.

## Validation

- Unit fixtures cover exact KTX2/PNG/JPEG/GIF/BMP byte retention, magic rejection,
  retained-byte accounting, mutation fallback, and finalizer ownership.
- glTF data-URI/buffer-view/external/preloaded and FBX embedded/external fixtures
  verify that every available source path reaches the material as a source-backed
  `TextureAsset3D`.
- VSCN tests cover v4 compatibility, v5 RGBA/source entries, reference
  deduplication, every container, malformed/oversized rollback, native mip and
  decoded-texel equality, and complete `SceneAsset` bake/reload.
- Bake CLI golden tests cover all three fidelity states. Registry/ABI checks,
  platform-policy lint, graphics-disabled linking, cross-platform smoke, and the
  official build scripts complete the gate.

