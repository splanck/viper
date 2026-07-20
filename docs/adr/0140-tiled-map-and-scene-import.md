---
status: active
audience: contributors
last-verified: 2026-07-20
---

# ADR 0140: Import Tiled Maps as Scene Documents and Tilemaps

## Status

Accepted (2026-07-20)

## Context

Zanna exposes `Zanna.Graphics.TiledMapLoader`, but that class currently only
stores a tile size and constructs an empty `Tilemap`. It does not read Tiled
JSON (`.tmj`/`.json`) or TMX, despite its public name and documentation. The
existing `Zanna.Game2D.SceneDocument` is the runtime object that can preserve
tile layers, placed objects, typed properties, asset references, diagnostics,
and a renderable `Tilemap` copy. Implementing real Tiled import therefore
requires additive runtime C ABI and registry rows and one deliberate Game2D to
Graphics2D adapter boundary.

The product cannot add an XML, image, or compression dependency. Zanna already
ships from-scratch JSON, XML, Base64, DEFLATE/GZIP, Zstandard, image, asset-pack,
and tilemap facilities, so the importer can be dependency-free.

## Decision

### Runtime surface

`Zanna.Game2D.SceneDocument` gains four static import operations:

```text
ImportTiled(path: String) -> SceneDocument
ImportTiledResult(path: String) -> Result
ImportTiledAsset(path: String) -> SceneDocument
ImportTiledAssetResult(path: String) -> Result
```

Their C ABI entry points are:

```c
void *rt_game_scene_import_tiled(rt_string path);
void *rt_game_scene_import_tiled_result(rt_string path);
void *rt_game_scene_import_tiled_asset(rt_string path);
void *rt_game_scene_import_tiled_asset_result(rt_string path);
```

The compatibility object-returning functions return `NULL` on failure. The
Result functions return `Err(message)` for I/O, dependency, syntax, schema,
resource-limit, or unsupported-feature failures; they never publish a partial
document.

`Zanna.Graphics.TiledMapLoader` gains render-oriented counterparts:

```text
Load(path: String) -> Tilemap
LoadResult(path: String) -> Result
LoadAsset(path: String) -> Tilemap
LoadAssetResult(path: String) -> Result
```

Their C ABI entry points take the loader handle followed by the path. They use
the same importer, call `SceneDocument.BuildTilemap()`, bind the resolved
tileset image for each layer, and then release the temporary document. A null
or wrong-class loader fails safely. Map-authored tile dimensions override the
loader's empty-map construction defaults.

The parser implementation lives with Game2D scene documents and consumes only
public opaque Graphics2D functions. It does not inspect private `Tilemap`,
`Pixels`, JSON, XML, or asset-manager layouts.

### Accepted Tiled inputs

- Root files are Tiled JSON maps (`.tmj` or `.json`) or TMX maps (`.tmx`).
  JSON roots must identify a map; XML roots must be `<map>`.
- Filesystem imports resolve relative dependencies beside their owner file.
  Asset imports resolve the root, external `.tsj`/`.tsx` tilesets, templates,
  and images through embedded/ZPAK/filesystem asset lookup using normalized
  logical paths. Absolute paths, URI dependencies, and logical paths that
  escape their asset root are rejected in asset mode.
- Finite orthogonal maps are supported. Positive map and tile dimensions are
  required and are checked against `SceneDocument`'s cell/layer/object limits.
- Tile layers accept JSON integer arrays, CSV text, and Base64 little-endian GID
  data with no compression, zlib, gzip, or Zstandard compression. The decoded
  byte count must be exactly `width * height * 4`.
- Group layers are flattened in source order with inherited visibility and a
  slash-separated name. Tile offsets must be integral tile offsets.
- Inline and external atlas tilesets (`.tsj`, JSON, or `.tsx`) are supported,
  including margin and spacing. Each imported tile layer must use at most one
  tileset; GIDs are normalized to Zanna's `0 = empty`, `N = frame N - 1`
  convention. The render adapter repacks spaced/margined atlases before binding.
- Map properties and object properties preserve null, Boolean, integer, finite
  floating-point, string, color, file, and object-reference values. File
  properties are resolved relative to their owner. Layer properties are
  namespaced in the scene document so they remain queryable.
- Object layers preserve rectangle, ellipse, point, polygon, polyline, text,
  and tile-object records. The SceneDocument object ID is the authored name
  when unique and nonempty, otherwise the numeric Tiled ID. Class/type, source
  layer, size, rotation, visibility, shape, template, GID, and non-integral
  source coordinates are retained as typed object properties.
- Image layers become `tiled.image-layer` SceneDocument objects with their
  resolved image asset and display metadata.
- For a map whose tile layers all use one tileset, tile collision object groups,
  integer tile properties, and tile animations are converted to the canonical
  `collision`, `tileProperties`, and `animations` sections consumed by
  `BuildTilemap()`. Animation durations are expanded at their greatest-common-
  divisor tick only when the result fits the runtime's eight-frame limit.

### Explicit rejections

The importer fails with a specific diagnostic instead of approximating these
constructs:

- infinite/chunked, isometric, staggered, or hexagonal maps;
- a tile layer that mixes multiple tilesets;
- horizontal, vertical, diagonal, or hexagonal GID transform flags;
- non-integral tile-layer offsets;
- collection-of-images tilesets, tiles larger than the map tile size, or
  nonzero tileset tile offsets;
- encoded data with an unknown encoding/compression or a decoded-size mismatch;
- external dependencies that are missing, oversized, malformed, cyclic, or
  unsafe;
- animation/collision metadata that would become ambiguous across multiple
  used tilesets, or animations that cannot fit the bounded runtime frame model.

Editor-only Tiled metadata that has no runtime effect (editor colors, parallax
origin, terrain/Wang authoring helpers, export target metadata) is not copied
into SceneDocument. This is documented format adaptation, not a round-trip
Tiled editor serializer.

### Bounds and ownership

- Root and external text dependencies are capped at 16 MiB each; dependency
  depth is capped at 16; decoded tile payloads are bounded by the exact map
  cell count; image decoders retain their existing 256 MiB caps.
- Parsing and dependency loading stage all normalized data before creating the
  public SceneDocument or Tilemap. Failure releases runtime objects and native
  buffers and publishes nothing.
- Returned SceneDocument and Tilemap handles are owned runtime objects. Borrowed
  JSON/XML nodes, strings, images, and loader handles are never retained without
  an explicit runtime retain/clone operation.

## Consequences

- Tiled-authored finite orthogonal maps become first-class external 2D scene
  inputs and work from loose files, embedded assets, and mounted ZPAKs.
- Callers can choose a rich editable `SceneDocument`, a render-ready `Tilemap`,
  or Result-based diagnostics without parsing Tiled themselves.
- The supported mapping is deliberately narrower than the full Tiled editor
  model, but every unsupported runtime-semantic feature fails explicitly.
- A future richer Tilemap transform/atlas model can remove the one-tileset-per-
  layer and flip-flag restrictions without changing these method names.

## Alternatives Considered

- **Return only `Tilemap`.** Rejected because object layers and typed properties
  would be irretrievably lost.
- **Create a second Tiled-specific scene object.** Rejected because
  `SceneDocument` already supplies the required ownership, diagnostics, editing,
  serialization, and Tilemap adapter contracts.
- **Silently ignore unsupported Tiled sections.** Rejected because a successful
  scene import must not conceal gameplay-relevant loss.
- **Add libxml2, zlib, or a Tiled SDK.** Rejected by Zanna's zero-dependency
  product policy and unnecessary given the existing runtime codecs.
