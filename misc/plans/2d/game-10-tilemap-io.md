# Plan: Tilemap File I/O & Auto-Tiling

## 1. Summary & Objective

Extend `Viper.Graphics.Tilemap` with file save/load (JSON format), CSV import (Tiled-compatible), and auto-tiling (automatic tile variant selection based on neighbor adjacency). Enables external level editor workflows and procedural level generation with clean visuals.

**Why:** The platformer's `buildLevel()` function sets tiles one-by-one in code — ~50 lines of hardcoded level data. No way to load levels from files, iterate with external editors, or use auto-tiling for procedurally generated maps. This is the biggest level design workflow bottleneck.

## 2. Scope

**In scope:**
- `SaveToFile(path)` — serialize tilemap grid + collision + layer data to JSON
- `LoadFromFile(path)` — deserialize from JSON, return new Tilemap
- `LoadCSV(path, tileWidth, tileHeight)` — import single-layer from CSV (Tiled export format)
- Auto-tiling: 4-bit neighbor bitmask → tile variant selection (16-variant "blob" tileset)
- `SetAutoTile(baseTile, variantMap)` — configure auto-tile rules
- `ApplyAutoTile()` — recalculate all auto-tiled cells
- Per-tile custom properties (key-value metadata)

**Out of scope:**
- Tiled TMX/JSON import (complex format with many features)
- Tilemap editor UI
- Undo/redo for tilemap edits
- Auto-tile with 8-bit bitmask (47-variant "marching squares" — too complex for v1)
- Isometric/hex auto-tiling
- Animated tiles
- Object layers from Tiled

## 3. Zero-Dependency Implementation Strategy

**JSON I/O** reuses the existing `rt_json.c` parser/formatter (already in the runtime). The tilemap is serialized as a JSON object with grid arrays, collision map, layer data, and properties.

**CSV import** is trivial line-by-line parsing with comma splitting — ~50 LOC.

**Auto-tiling** uses a 4-bit neighbor bitmask (up/right/down/left) mapping to 16 tile variants. The developer provides a mapping array of 16 tile indices. When auto-tile is applied, each cell checks its 4 neighbors and selects the correct variant. ~100 LOC.

## 4. Technical Requirements

### Modified Files
- `src/runtime/graphics/rt_tilemap.h` — add I/O and auto-tile declarations
- `src/runtime/graphics/rt_tilemap.c` — add I/O and auto-tile implementations

### New Files (optional, for cleaner organization)
- `src/runtime/graphics/rt_tilemap_io.c` — file I/O implementations (~200 LOC)

### C API Additions (rt_tilemap.h)

```c
// === File I/O ===
int8_t    rt_tilemap_save_to_file(void *tm, rt_string path);        // JSON format
void     *rt_tilemap_load_from_file(rt_string path);                 // Returns new Tilemap
void     *rt_tilemap_load_csv(rt_string path, int64_t tile_w, int64_t tile_h); // CSV grid

// === Auto-Tiling ===
// Set auto-tile rule: baseTile is the logical tile ID, variants is 16-element array
// of actual tile indices for each 4-bit neighbor mask (up|right|down|left)
void      rt_tilemap_set_autotile(void *tm, int64_t base_tile,
                                   int64_t v0,  int64_t v1,  int64_t v2,  int64_t v3,
                                   int64_t v4,  int64_t v5,  int64_t v6,  int64_t v7,
                                   int64_t v8,  int64_t v9,  int64_t v10, int64_t v11,
                                   int64_t v12, int64_t v13, int64_t v14, int64_t v15);
void      rt_tilemap_clear_autotile(void *tm, int64_t base_tile);
void      rt_tilemap_apply_autotile(void *tm);                       // Recalculate all cells
void      rt_tilemap_apply_autotile_region(void *tm, int64_t x, int64_t y,
                                            int64_t w, int64_t h);  // Recalculate region

// === Per-Tile Properties ===
void      rt_tilemap_set_tile_property(void *tm, int64_t tile_index,
                                        rt_string key, int64_t value);
int64_t   rt_tilemap_get_tile_property(void *tm, int64_t tile_index,
                                        rt_string key, int64_t default_val);
int8_t    rt_tilemap_has_tile_property(void *tm, int64_t tile_index, rt_string key);
```

### JSON Save Format

```json
{
  "version": 1,
  "width": 20,
  "height": 15,
  "tileWidth": 16,
  "tileHeight": 16,
  "layers": [
    {
      "name": "base",
      "tiles": [0, 0, 1, 1, 2, ...],
      "visible": true
    },
    {
      "name": "foreground",
      "tiles": [-1, -1, 5, -1, ...],
      "visible": true
    }
  ],
  "collision": {
    "layer": 0,
    "types": { "1": 1, "2": 2 }
  },
  "properties": {
    "1": { "damage": 10 },
    "5": { "animated": 1 }
  }
}
```

### Auto-Tile Bitmask Convention

```
Bit 0 (1): neighbor UP    has same base tile
Bit 1 (2): neighbor RIGHT has same base tile
Bit 2 (4): neighbor DOWN  has same base tile
Bit 3 (8): neighbor LEFT  has same base tile

Mask  Visual     Example
0     isolated   ·
1     top only   ╵
2     right only ╶
3     top+right  └
...
15    all four   ┼
```

The developer provides 16 tile indices (one per mask value) when configuring auto-tile for a base tile type.

## 5. runtime.def Registration

```c
//=============================================================================
// GRAPHICS - TILEMAP FILE I/O & AUTO-TILE
//=============================================================================

// File I/O
RT_FUNC(TilemapSaveToFile,   rt_tilemap_save_to_file,   "Viper.Graphics.Tilemap.SaveToFile",   "i1(obj,str)")
RT_FUNC(TilemapLoadFromFile, rt_tilemap_load_from_file,  "Viper.Graphics.Tilemap.LoadFromFile", "obj(str)")
RT_FUNC(TilemapLoadCSV,      rt_tilemap_load_csv,        "Viper.Graphics.Tilemap.LoadCSV",      "obj(str,i64,i64)")

// Auto-tiling (16 variant args for the 4-bit bitmask)
RT_FUNC(TilemapSetAutoTile,      rt_tilemap_set_autotile,          "Viper.Graphics.Tilemap.SetAutoTile",      "void(obj,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64)")
RT_FUNC(TilemapClearAutoTile,    rt_tilemap_clear_autotile,        "Viper.Graphics.Tilemap.ClearAutoTile",    "void(obj,i64)")
RT_FUNC(TilemapApplyAutoTile,    rt_tilemap_apply_autotile,        "Viper.Graphics.Tilemap.ApplyAutoTile",    "void(obj)")
RT_FUNC(TilemapApplyAutoTileRgn, rt_tilemap_apply_autotile_region, "Viper.Graphics.Tilemap.ApplyAutoTileRegion","void(obj,i64,i64,i64,i64)")

// Tile properties
RT_FUNC(TilemapSetTileProp,  rt_tilemap_set_tile_property,"Viper.Graphics.Tilemap.SetTileProperty","void(obj,i64,str,i64)")
RT_FUNC(TilemapGetTileProp,  rt_tilemap_get_tile_property,"Viper.Graphics.Tilemap.GetTileProperty","i64(obj,i64,str,i64)")
RT_FUNC(TilemapHasTileProp,  rt_tilemap_has_tile_property,"Viper.Graphics.Tilemap.HasTileProperty","i1(obj,i64,str)")

// Add to existing Tilemap RT_CLASS_BEGIN block:
    RT_METHOD("SaveToFile", "i1(str)", TilemapSaveToFile)
    RT_METHOD("ApplyAutoTile", "void()", TilemapApplyAutoTile)
    RT_METHOD("ApplyAutoTileRegion", "void(i64,i64,i64,i64)", TilemapApplyAutoTileRgn)
    RT_METHOD("SetAutoTile", "void(i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64,i64)", TilemapSetAutoTile)
    RT_METHOD("ClearAutoTile", "void(i64)", TilemapClearAutoTile)
    RT_METHOD("SetTileProperty", "void(i64,str,i64)", TilemapSetTileProp)
    RT_METHOD("GetTileProperty", "i64(i64,str,i64)", TilemapGetTileProp)
    RT_METHOD("HasTileProperty", "i1(i64,str)", TilemapHasTileProp)

// Static factory methods (not on instance):
// LoadFromFile and LoadCSV are registered as standalone RT_FUNC entries
```

## 6. CMakeLists.txt Changes

Option A (extend existing file): No changes needed — additions go into `rt_tilemap.c`.

Option B (separate file): Add to `RT_GRAPHICS_SOURCES`:
```cmake
graphics/rt_tilemap_io.c
```

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| SaveToFile: path not writable | Return 0 (false) |
| LoadFromFile: file not found | Return NULL |
| LoadFromFile: invalid JSON | Return NULL |
| LoadFromFile: missing required fields | Return NULL |
| LoadCSV: file not found | Return NULL |
| LoadCSV: malformed row (wrong column count) | Skip row, use 0 for missing |
| SetAutoTile: base_tile > max tiles | No-op |
| ApplyAutoTile: no auto-tile rules configured | No-op |
| Tile property: tile_index out of range | No-op / return default |
| Tile property: key > 31 chars | Truncated |

## 8. Tests

### Zia Runtime Tests (`tests/runtime/test_tilemap_io.zia`)

1. **JSON round-trip**
   - Given: Tilemap with known tiles and collision
   - When: `tm.SaveToFile("test_map.json")` then `Tilemap.LoadFromFile("test_map.json")`
   - Then: Loaded tilemap matches original (size, tiles, collision)

2. **CSV import**
   - Given: CSV file `"0,0,1\n0,2,1\n1,1,0"` (3×3 grid)
   - When: `Tilemap.LoadCSV("test.csv", 16, 16)`
   - Then: `tm.Width == 3`, `tm.GetTile(1, 1) == 2`

3. **Auto-tile neighbor calculation**
   - Given: 5×5 tilemap with "ground" tile at (1,1), (2,1), (3,1)
   - When: Auto-tile configured for ground, `ApplyAutoTile()`
   - Then: (1,1) = left-edge variant, (2,1) = middle variant, (3,1) = right-edge variant

4. **Auto-tile isolated cell**
   - Given: Single ground tile surrounded by empty
   - When: `ApplyAutoTile()`
   - Then: Cell gets mask=0 variant (isolated)

5. **Tile properties**
   - When: `tm.SetTileProperty(5, "damage", 10)`
   - Then: `tm.GetTileProperty(5, "damage", 0) == 10`

6. **Properties in save/load**
   - Given: Tilemap with tile properties
   - When: Save then load
   - Then: Properties preserved

## 9. Documentation Deliverables

| Action | File |
|--------|------|
| UPDATE | `docs/viperlib/graphics/pixels.md` — add File I/O and Auto-Tiling sections to Tilemap docs |
| UPDATE | `docs/viperlib/game.md` — mention tilemap I/O and auto-tile in overview |

## 10. Code References

| File | Role |
|------|------|
| `src/runtime/graphics/rt_tilemap.c` | **Primary file to extend** |
| `src/runtime/graphics/rt_tilemap.h` | **Primary header to extend** |
| `src/runtime/text/rt_json.c` | JSON parse/format (reuse for save/load) |
| `src/runtime/text/rt_csv.c` | CSV parsing (reuse for LoadCSV) |
| `src/runtime/io/rt_savedata.c` | Pattern: JSON serialization to platform-appropriate paths |
| `src/il/runtime/runtime.def:1385-1406` | Existing Tilemap entries to extend |
| `examples/games/platformer/play_scene.zia` | Evidence: hardcoded buildLevel() |
