# Plan 04: Tilemap Per-Tile Animation

## Context

XENOSCAPE manually animates lava tiles via a frame counter in renderer.zia, branching
per special tile type inside the draw loop. Tilemap has no per-tile animation support —
every animated tile must be special-cased by the developer.

## Changes

### rt_tilemap.c — animation data + auto-advance (~150 LOC)

**New fields in rt_tilemap_impl (after collision[]):**
```c
#define TM_MAX_ANIM_TILES 64

typedef struct {
    int64_t base_tile_id;   // The tile ID this animation applies to
    int64_t frame_count;    // Number of frames
    int64_t ms_per_frame;   // Milliseconds per frame
    int64_t tile_ids[8];    // Tile IDs for each frame (max 8 frames)
    int64_t timer;          // Current ms accumulator
    int64_t current_frame;  // Current frame index
} tm_tile_anim;

// In rt_tilemap_impl:
tm_tile_anim tile_anims[TM_MAX_ANIM_TILES];
int32_t tile_anim_count;
```

**New functions:**
```c
// Register an animated tile: base_id cycles through frame tile IDs
void rt_tilemap_set_tile_anim(void *tm, int64_t base_tile_id,
                              int64_t frame_count, int64_t ms_per_frame);
// Frame tile IDs default to base_id+0, base_id+1, ..., base_id+(count-1)

// Custom frame IDs for non-sequential animations
void rt_tilemap_set_tile_anim_frame(void *tm, int64_t base_tile_id,
                                    int64_t frame_idx, int64_t tile_id);

// Advance all tile animations by dt milliseconds. Call once per frame.
void rt_tilemap_update_anims(void *tm, int64_t dt_ms);
```

**Modify DrawRegion/Draw:**
When rendering tile at (col, row), look up tile_id in animation table.
If found, substitute with `tile_anims[i].tile_ids[current_frame]`.
Lookup: linear scan of tile_anim_count entries (small, ≤64).

### runtime.def
```
RT_FUNC(TilemapSetTileAnim,      rt_tilemap_set_tile_anim,       "Viper.Game.Tilemap.SetTileAnim",      "void(obj,i64,i64,i64)")
RT_FUNC(TilemapSetTileAnimFrame, rt_tilemap_set_tile_anim_frame, "Viper.Game.Tilemap.SetTileAnimFrame", "void(obj,i64,i64,i64)")
RT_FUNC(TilemapUpdateAnims,      rt_tilemap_update_anims,        "Viper.Game.Tilemap.UpdateAnims",      "void(obj,i64)")
```

### Zia usage
```zia
// Lava tile (ID 15) cycles through 4 frames at 200ms each
tilemap.SetTileAnim(TILE_LAVA, 4, 200)
// Frame 0 = TILE_LAVA, 1 = TILE_LAVA+1, 2 = TILE_LAVA+2, 3 = TILE_LAVA+3

// Each frame in game loop:
tilemap.UpdateAnims(dt)
tilemap.DrawRegion(canvas, 0, 0, camX, camY, SCREEN_W, SCREEN_H)
```

### Files to modify
- `src/runtime/graphics/rt_tilemap.c` — anim struct, 3 functions, modify Draw
- `src/runtime/graphics/rt_tilemap.h` — declarations
- `src/il/runtime/runtime.def` — 3 entries

### Tests

**File:** `src/tests/unit/runtime/TestTilemapAnim.cpp`
```
TEST(TilemapAnim, RegisterAndAdvance)
  — SetTileAnim(5, 3, 100), UpdateAnims(100), verify internal frame advanced

TEST(TilemapAnim, FrameWraps)
  — 3-frame anim, advance past end, verify wraps to frame 0

TEST(TilemapAnim, CustomFrameIds)
  — SetTileAnimFrame to non-sequential IDs, verify correct substitution

TEST(TilemapAnim, NoAnimUnchanged)
  — Tiles without animation render unchanged
```

### Doc update
- `docs/viperlib/game/tilemap.md` — add Tile Animation section
