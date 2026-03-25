# Feature 7: Tiled/TMX Map Import

## Current Reality

`Viper.Graphics.Tilemap` already has:
- layered tile storage
- collision metadata
- rendering
- custom file I/O
- CSV loading support

What is missing is direct TMX/Tiled import.

## Problem

Tiled is the standard 2D level authoring tool. Without TMX import, users must either:
- write custom converters
- re-enter data manually
- avoid the existing editor ecosystem

## Corrected Scope

### Tilemap Extensions

```text
Tilemap.LoadTMX(path) -> Tilemap
Tilemap.LoadTMXFromString(xml) -> Tilemap
Tilemap.GetObjects(layerName) -> Seq
```

### New Object Class

`Viper.Graphics.TilemapObject`

```text
obj.Name -> String
obj.Type -> String
obj.X -> Integer
obj.Y -> Integer
obj.Width -> Integer
obj.Height -> Integer
obj.GetProperty(key) -> String
```

v1 scope:
- orthogonal finite maps
- CSV tile layers
- object layers
- tileset image loading
- custom properties

Not v1:
- isometric / hex maps
- infinite maps
- full animated-tile feature parity
- every compression mode Tiled supports

## Implementation

### Phase 1: TMX parser (2-3 days)

- Add `src/runtime/graphics/rt_tilemap_tmx.c` + `rt_tilemap_tmx.h`
- Parse TMX through the existing XML runtime
- Support:
  - map size
  - tile size
  - layers
  - objects
  - properties
  - tileset image references

### Phase 2: tilemap construction (1-2 days)

- Construct a normal `Tilemap`
- Reuse existing CSV parsing logic where possible
- Resolve TMX-relative paths for images
- Map Tiled GIDs into Viper tile indices

### Phase 3: object exposure and tests (1 day)

- Add `TilemapObject`
- Return an object sequence from `GetObjects(layerName)`
- Add parser and integration tests

## Viper-Specific Notes

- `docs/viperlib/graphics/pixels.md` is the current Tilemap documentation page
- Use the existing XML helpers already present in the runtime
- `Tilemap.LoadTMX` should sit next to current tilemap I/O code rather than replace it
- Keep unsupported TMX features explicitly documented

## Runtime Registration

- Extend `Viper.Graphics.Tilemap`
- Add `Viper.Graphics.TilemapObject`

## Files

| File | Action |
|------|--------|
| `src/runtime/graphics/rt_tilemap_tmx.c` | New |
| `src/runtime/graphics/rt_tilemap_tmx.h` | New |
| `src/runtime/graphics/rt_tilemap.c` | Modify or route entry points |
| `src/il/runtime/runtime.def` | Add TMX entry points and object class |
| `src/tests/runtime/RTTiledTests.cpp` | New |

## Documentation Updates

- Update `docs/viperlib/graphics/pixels.md`
- Update `docs/viperlib/graphics/README.md`
- Update `docs/viperlib/README.md`

## Cross-Platform Requirements

- File/path handling must work on macOS, Linux, and Windows
- Runtime wiring belongs in `src/runtime/CMakeLists.txt`
- Guard with `#ifdef VIPER_ENABLE_GRAPHICS`
