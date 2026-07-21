---
status: active
audience: contributors
last-verified: 2026-07-20
---

# ADR 0144: Complete Tiled Map Import and Projected Tilemap Rendering

## Status

Accepted (2026-07-20).

## Context

ADR 0140 introduced dependency-free Tiled JSON/TMX import, but deliberately
limited the adapter to finite orthogonal maps whose layers each referenced one
atlas tileset. The parser rejected infinite chunks, projected orientations,
GID transformations, mixed/image-collection tilesets, non-cell-aligned layer
placement, non-default layer rendering state, large or offset tile artwork, and
metadata from more than one tileset. Those checks prevented silent loss, but
they also exposed that the old `Tilemap` representation conflated four separate
concepts:

- logical map-cell width and height;
- the source atlas frame width and height;
- the projection from signed Tiled tile coordinates to pixels; and
- a layer's pixel placement and camera-relative parallax.

Tiled GIDs are map-wide and can name any tileset. Their high four bits encode
horizontal/vertical/diagonal transforms, or 60/120-degree rotation in a
hexagonal map. Tiled also anchors artwork larger than the grid cell at the
bottom-left, permits a tileset drawing offset, and permits a different image
and size for each tile. Mapping such inputs to a per-layer local ID and a
grid-sized atlas is inherently lossy.

The product remains dependency-free and must retain the existing bounded,
transactional failure model. Supporting a format feature does not require
accepting malformed payloads, overlapping/overflowing chunks, impossible
dimensions, unbounded generated atlases, unsafe dependencies, or invalid GIDs.

## Decision

### Signed logical bounds and orientation

The importer accepts Tiled `orthogonal`, `isometric`, `staggered`, `hexagonal`,
and `oblique` orientations. It preserves render order, stagger axis/index,
hex-side length, oblique skew, parallax origin, and whether the source map was
infinite.

An infinite map is scanned transactionally before layer materialization. The
union of all valid chunk rectangles becomes the dense editable
`SceneDocument` extent. Its signed Tiled top-left coordinate is retained as
`originTileX/originTileY`; an empty infinite map becomes a bounded 1-by-1 empty
document with origin `(0, 0)`. Chunk coordinates and dimensions use checked
signed arithmetic. Chunks with inconsistent payload lengths or overlapping
cell domains are malformed and reject the load. The existing one-million cells
per layer and four-million cells per document limits continue to apply to the
flattened result.

`Tilemap` retains a rectangular logical grid, but gains private projection
state. The projection also retains the map's declared source height separately
from the flattened height, because infinite isometric maps conventionally
declare height zero. Rendering projects `grid + signed origin` using Tiled's
coordinate rules:

- orthogonal uses cell width/height and authored render order;
- isometric uses half-width/half-height diamonds and the finite-map horizontal
  origin used by Tiled;
- staggered and hexagonal use the authored stagger axis/index and, for hex,
  the authored straight-edge length;
- oblique uses the two authored shear terms.

Viewport culling applies a conservative inverse projection and artwork margin
before walking cells. It never scans the complete flattened map merely because
the map is projected. Pixel destinations, including fractional layer offsets,
are rounded once using nearest-integer, ties away from zero. This rule is
cross-platform and does not depend on the host floating-point environment.

### Map-wide canonical tile IDs

The normalized document no longer stores a per-layer tileset index. Every
nonempty cell stores a positive map-wide canonical tile ID. Base IDs are
assigned in sorted `firstgid`/local-ID order, with tile `0` still meaning empty.
The importer then interns only the additional variants reachable from layer
cells or animation frames. A variant key contains:

- source tileset and local tile ID;
- all four GID transform bits, interpreted according to map orientation; and
- the effective inherited tint and opacity of its layer.

Thus identical variants are shared, while two visually different cells never
alias the same runtime ID. Diagonal transformation occurs before horizontal and
vertical flips. Hexagonal 60/120-degree flags rotate about the tile-art center.
The unused fourth bit is always cleared before GID lookup, including on
non-hexagonal maps. Tile-object GIDs preserve the same decoded base/transform
metadata even though object rendering remains an application concern.

Collision, integer/Boolean runtime properties, and animations are remapped
from each tileset's local namespace to every canonical variant that uses the
tile. All other typed tile properties and editor metadata remain queryable in a
preserved `tiledTileMetadata` section; they do not fail merely because
`Tilemap` has no typed-property slot for them. Conflicting metadata can no
longer occur through local-ID aliasing because canonical IDs are globally
unique.

### Composed atlas and image collections

`TiledMapLoader.Load*` materializes one composed `Pixels` atlas after all text
input has validated. Atlas-backed tiles are cropped using margin, spacing,
columns, and tile count. Image-collection tiles load their per-tile images.
Declared image dimensions must match decoded images. Missing tile images are
transparent only when the tile is not reachable; a reachable missing image is
an error.

Each tile image is positioned relative to the map cell using Tiled's
bottom-left anchor and tileset drawing offset. The union of reachable artwork
bounds defines one shared source-frame rectangle. Every canonical tile is
written to a transparent frame of that size, after GID transform, tint, and
opacity. `Tilemap` stores this source-frame size and a common draw offset
separately from its logical cell size. This supports artwork larger or smaller
than the grid without clipping or changing collision coordinates.

The composed atlas is capped at 256 MiB of RGBA pixels, and every multiplication
and placement is checked before allocation. Atlas columns are chosen to keep
both dimensions bounded while retaining row-major canonical IDs. A failure
releases all decoded source images and publishes neither a partial atlas nor a
partial `Tilemap`.

Plain one-tileset, untransformed, untinted maps retain their previous canonical
IDs (`local ID + 1`), so existing scene logic and metadata remain stable.
Scene-document layer asset fields keep the original image only when that image
alone can render the layer. All source images are also retained as dependency
descriptors in the preserved Tiled runtime section.

### Layer placement, composition, and animation

Group state composes in source order:

- visibility is logical AND;
- opacity and parallax factors multiply;
- tint colors multiply per channel in linear integer arithmetic; and
- pixel offsets add without requiring integral or cell-aligned values.

Tile-layer, object-layer, and image-layer source coordinates retain their
finite double values. `SceneDocument`'s integer object coordinate remains the
nearest runtime placement, while exact source coordinates stay in the reserved
`tiled.sourceX/tiled.sourceY` properties. Image layers retain effective
opacity, tint, parallax, repeat, and fractional placement metadata.

Imported tile animations retain the authored per-frame millisecond durations.
The private Tilemap animation record becomes dynamically sized and advances by
duration rather than by an expanded greatest-common-divisor frame list. The
legacy uniform-duration setters continue to construct the same public behavior.
Importer-only duration configuration is additive and does not change existing
runtime names.

### SceneDocument persistence bridge

The canonical scene remains version 1. A preserved object section named
`tiledRuntime` carries projection, declared projection height, signed origin,
source-frame layout, map parallax origin, render order, effective per-layer
placement/parallax, and all source image paths.
`SceneDocument.BuildTilemap()` validates and applies this section through opaque
Graphics2D configuration functions. Save/reload therefore retains imported
placement even when callers edit ordinary scene tiles before rebuilding the
Tilemap. Tilemap's own JSON save/load also retains this private imported-layout
state and authored variable animation durations while accepting older files
that omit it.

Malformed `tiledRuntime` data in a user-authored SceneDocument is ignored
field-by-field and falls back to ordinary orthogonal behavior; it never permits
out-of-bounds access or an invalid allocation.

## Explicit Rejections That Remain

The following are invalid or unsafe inputs, not unsupported format features:

- unknown orientations, encodings, compressions, transform combinations, or
  layer kinds;
- non-finite numbers, invalid stagger/hex/skew parameters, GIDs outside the
  unsigned 32-bit domain, undeclared GIDs, and tiles beyond declared counts;
- duplicate `firstgid`, cyclic/unsafe/missing dependencies, mismatched declared
  image sizes, and malformed external tilesets/templates;
- chunks with non-positive dimensions, size overflow, overlapping domains, or
  decoded payload lengths different from exactly `width * height * 4`;
- flattened grids, dependency inventories, metadata, animation tables, source
  images, or composed atlases beyond the documented resource ceilings.

Unknown editor-only metadata that has no runtime meaning is preserved when it
fits the bounded metadata section; it is never interpreted as executable data.

## Consequences

- Tiled maps can mix atlas and image-collection tilesets in any tile layer and
  retain unambiguous collision, property, and animation semantics.
- Infinite and projected maps remain editable as ordinary bounded
  `SceneDocument` grids while rendering at their original signed coordinates.
- The common Tilemap implementation gains projection/source-layout behavior,
  but ordinary orthogonal maps keep the same API, IDs, collision grid, and fast
  path.
- Generated pixels trade bounded memory for a simple backend-independent draw
  path. No renderer backend or third-party image/XML/compression library is
  added.

## Validation

- JSON and TMX fixtures cover negative infinite chunks, every supported data
  encoding/compression, empty infinite maps, overlap/overflow rejection, all
  orientations, both stagger axes/indices, hex-side lengths, and oblique skew.
- Pixel-exact canvas fixtures cover horizontal/vertical/diagonal transform
  order, hex rotations, bottom-left anchoring, drawing offsets, mixed atlases,
  image collections, fractional layer placement, tint/opacity composition, and
  viewport culling.
- Round-trip fixtures verify `SceneDocument.ToJson`/reload retains the signed
  origin, projected layout, dependencies, canonical IDs, metadata, and authored
  animation durations.
- Existing Tilemap tests cover unchanged orthogonal drawing, collision, scaled
  drawing, legacy animation setters, malformed runtime sections, and allocation
  rollback. Platform-policy lint and the official macOS/Linux/Windows build
  scripts remain the final gate.
