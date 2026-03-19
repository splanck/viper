# Plan: Multi-Layer Tilemap

## 1. Summary & Objective

Extend the existing `Viper.Graphics.Tilemap` class to support multiple named layers. Each layer stores its own tile grid and can be drawn independently, enabling background/midground/foreground/collision layer separation.

**Why:** Platformer uses one tilemap for both visuals and collision. No way to have decorative foreground tiles that render over the player. Real tile-based games need at least 3-4 layers. Currently developers manage multiple Tilemap objects manually — error-prone and inefficient.

## 2. Scope

**In scope:**
- Named layers within a single Tilemap (up to 16 layers)
- Default layer 0 ("base") for backwards compatibility
- Per-layer tile grid with independent tilesets
- `DrawLayer()` for selective rendering (foreground after sprites)
- Designate collision layer (defaults to layer 0)
- All existing single-layer methods continue to work on layer 0
- Layer visibility toggle
- Layer ordering (draw order = layer index, 0 first)

**Out of scope:**
- Per-layer parallax scrolling (use Camera.AddParallax for that)
- Layer blending modes / opacity per layer
- Infinite/chunked tilemaps
- Isometric or hex tilemaps
- Layer groups / nesting

## 3. Zero-Dependency Implementation Strategy

Each layer is an additional `int16_t*` grid array allocated alongside the existing grid. The existing single-layer Tilemap already allocates `width × height` tile indices — adding layers is `N × width × height` arrays. Layer metadata (name, tileset, visibility) stored in a small fixed-size array. ~200 LOC added to existing `rt_tilemap.c`.

## 4. Technical Requirements

### Modified Files
- `src/runtime/graphics/rt_tilemap.h` — add layer API declarations
- `src/runtime/graphics/rt_tilemap.c` — add layer storage and methods

### Internal Data Structure Changes

```c
#define TM_MAX_LAYERS 16

typedef struct {
    int16_t  *tiles;          // width × height tile indices (-1 = empty)
    void     *tileset;        // Pixels handle for this layer's tileset (NULL = use base)
    char      name[32];       // Layer name
    int8_t    visible;        // 1 = visible, 0 = hidden
    int32_t   tile_count;     // Tiles in this layer's tileset
} tm_layer;

// Extend existing rt_tilemap_impl:
struct rt_tilemap_impl {
    // ... existing fields ...
    tm_layer  layers[TM_MAX_LAYERS];
    int32_t   layer_count;         // Current number of layers (starts at 1)
    int32_t   collision_layer;     // Which layer has collision data (default 0)
};
```

### C API Additions (rt_tilemap.h)

```c
// Layer management
int64_t rt_tilemap_add_layer(void *tm, rt_string name);           // Returns layer ID (1-15)
int64_t rt_tilemap_get_layer_count(void *tm);
int64_t rt_tilemap_get_layer_by_name(void *tm, rt_string name);   // Returns layer ID or -1
void    rt_tilemap_remove_layer(void *tm, int64_t layer);
void    rt_tilemap_set_layer_visible(void *tm, int64_t layer, int8_t visible);
int8_t  rt_tilemap_get_layer_visible(void *tm, int64_t layer);

// Per-layer tile access
void    rt_tilemap_set_tile_layer(void *tm, int64_t layer, int64_t x, int64_t y, int64_t tile);
int64_t rt_tilemap_get_tile_layer(void *tm, int64_t layer, int64_t x, int64_t y);
void    rt_tilemap_fill_layer(void *tm, int64_t layer, int64_t tile);
void    rt_tilemap_clear_layer(void *tm, int64_t layer);

// Per-layer tileset
void    rt_tilemap_set_layer_tileset(void *tm, int64_t layer, void *pixels);

// Per-layer rendering
void    rt_tilemap_draw_layer(void *tm, void *canvas, int64_t layer,
                               int64_t cam_x, int64_t cam_y);

// Collision layer designation
void    rt_tilemap_set_collision_layer(void *tm, int64_t layer);
int64_t rt_tilemap_get_collision_layer(void *tm);
```

### Backwards Compatibility

All existing methods (`SetTile`, `GetTile`, `Fill`, `Clear`, `Draw`) operate on layer 0 by default. The new layer 0 is automatically created at Tilemap construction time. Existing code works unchanged.

## 5. runtime.def Registration

```c
// Layer management (add near existing Tilemap entries)
RT_FUNC(TilemapAddLayer,          rt_tilemap_add_layer,           "Viper.Graphics.Tilemap.AddLayer",           "i64(obj,str)")
RT_FUNC(TilemapGetLayerCount,     rt_tilemap_get_layer_count,     "Viper.Graphics.Tilemap.get_LayerCount",     "i64(obj)")
RT_FUNC(TilemapGetLayerByName,    rt_tilemap_get_layer_by_name,   "Viper.Graphics.Tilemap.GetLayerByName",     "i64(obj,str)")
RT_FUNC(TilemapRemoveLayer,       rt_tilemap_remove_layer,        "Viper.Graphics.Tilemap.RemoveLayer",        "void(obj,i64)")
RT_FUNC(TilemapSetLayerVisible,   rt_tilemap_set_layer_visible,   "Viper.Graphics.Tilemap.SetLayerVisible",    "void(obj,i64,i1)")
RT_FUNC(TilemapGetLayerVisible,   rt_tilemap_get_layer_visible,   "Viper.Graphics.Tilemap.GetLayerVisible",    "i1(obj,i64)")
RT_FUNC(TilemapSetTileLayer,      rt_tilemap_set_tile_layer,      "Viper.Graphics.Tilemap.SetTileLayer",       "void(obj,i64,i64,i64,i64)")
RT_FUNC(TilemapGetTileLayer,      rt_tilemap_get_tile_layer,      "Viper.Graphics.Tilemap.GetTileLayer",       "i64(obj,i64,i64,i64)")
RT_FUNC(TilemapFillLayer,         rt_tilemap_fill_layer,          "Viper.Graphics.Tilemap.FillLayer",          "void(obj,i64,i64)")
RT_FUNC(TilemapClearLayer,        rt_tilemap_clear_layer,         "Viper.Graphics.Tilemap.ClearLayer",         "void(obj,i64)")
RT_FUNC(TilemapSetLayerTileset,   rt_tilemap_set_layer_tileset,   "Viper.Graphics.Tilemap.SetLayerTileset",    "void(obj,i64,obj)")
RT_FUNC(TilemapDrawLayer,         rt_tilemap_draw_layer,          "Viper.Graphics.Tilemap.DrawLayer",          "void(obj,obj,i64,i64,i64)")
RT_FUNC(TilemapSetCollisionLayer, rt_tilemap_set_collision_layer, "Viper.Graphics.Tilemap.set_CollisionLayer", "void(obj,i64)")
RT_FUNC(TilemapGetCollisionLayer, rt_tilemap_get_collision_layer, "Viper.Graphics.Tilemap.get_CollisionLayer", "i64(obj)")

// Add to existing Tilemap RT_CLASS_BEGIN block:
    RT_PROP("LayerCount", "i64", TilemapGetLayerCount, none)
    RT_PROP("CollisionLayer", "i64", TilemapGetCollisionLayer, TilemapSetCollisionLayer)
    RT_METHOD("AddLayer", "i64(str)", TilemapAddLayer)
    RT_METHOD("GetLayerByName", "i64(str)", TilemapGetLayerByName)
    RT_METHOD("RemoveLayer", "void(i64)", TilemapRemoveLayer)
    RT_METHOD("SetLayerVisible", "void(i64,i1)", TilemapSetLayerVisible)
    RT_METHOD("GetLayerVisible", "i1(i64)", TilemapGetLayerVisible)
    RT_METHOD("SetTileLayer", "void(i64,i64,i64,i64)", TilemapSetTileLayer)
    RT_METHOD("GetTileLayer", "i64(i64,i64,i64)", TilemapGetTileLayer)
    RT_METHOD("FillLayer", "void(i64,i64)", TilemapFillLayer)
    RT_METHOD("ClearLayer", "void(i64)", TilemapClearLayer)
    RT_METHOD("SetLayerTileset", "void(i64,obj)", TilemapSetLayerTileset)
    RT_METHOD("DrawLayer", "void(obj,i64,i64,i64)", TilemapDrawLayer)
```

## 6. CMakeLists.txt Changes

No new files — extends existing `rt_tilemap.c` already in `RT_GRAPHICS_SOURCES`.

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| AddLayer when at 16 layers | Return -1 |
| Invalid layer ID | No-op / return default (0 or -1) |
| RemoveLayer(0) | Not allowed (base layer permanent), no-op |
| Layer name duplicate | Allowed (names are hints, not unique keys) |
| Layer name > 31 chars | Truncated |
| NULL tileset for layer | Uses base layer tileset |
| DrawLayer with hidden layer | No-op |
| Collision on non-base layer | IsSolidAt reads designated collision layer |

## 8. Tests

### Zia Runtime Tests (`tests/runtime/test_tilemap_layers.zia`)

1. **Default single layer**
   - Given: New tilemap
   - Then: `LayerCount == 1`, existing SetTile/GetTile works on layer 0

2. **Add and name layers**
   - When: `tm.AddLayer("foreground")`
   - Then: `LayerCount == 2`, `GetLayerByName("foreground") == 1`

3. **Per-layer tile independence**
   - Given: Tilemap with 2 layers
   - When: `SetTileLayer(0, 5, 5, 1)` and `SetTileLayer(1, 5, 5, 2)`
   - Then: `GetTileLayer(0, 5, 5) == 1`, `GetTileLayer(1, 5, 5) == 2`

4. **Collision layer designation**
   - Given: Tilemap with layer 1 as collision layer
   - When: Collision tile set on layer 1
   - Then: `IsSolidAt(x, y)` reads layer 1

5. **Layer visibility**
   - When: `SetLayerVisible(1, false)`
   - Then: `GetLayerVisible(1) == false`

6. **Backwards compatibility**
   - Given: Code using old API (SetTile, GetTile, Draw)
   - Then: All operate on layer 0, identical behavior to before

## 9. Documentation Deliverables

| Action | File |
|--------|------|
| UPDATE | `docs/viperlib/graphics/pixels.md` — add Layers subsection to Tilemap documentation |
| UPDATE | `docs/viperlib/game.md` — mention layer support in Tilemap summary |

## 10. Code References

| File | Role |
|------|------|
| `src/runtime/graphics/rt_tilemap.c` | **Primary file to extend** |
| `src/runtime/graphics/rt_tilemap.h` | **Primary header to extend** |
| `src/runtime/graphics/rt_camera.c` | Pattern: rendering with viewport offset |
| `src/il/runtime/runtime.def:1385-1406` | Existing Tilemap RT_FUNC entries |
| `src/il/runtime/runtime.def:5600` | Existing Tilemap RT_CLASS_BEGIN block |
| `examples/games/platformer/play_scene.zia` | Consumer: single-layer tilemap usage |
