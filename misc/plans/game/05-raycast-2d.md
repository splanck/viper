# Plan 05: 2D Raycast + Line-of-Sight

## Context

rt_collision.c has AABB and circle overlap tests but NO line intersection.
XENOSCAPE's ai.zia (259 lines) manually scans tiles for line-of-sight.
2D raycasting is fundamental for shooting, AI sight, and platformer mechanics.

## Changes

### rt_collision.c — add line intersection functions (~200 LOC)

**Line-segment vs AABB:**
```c
// Returns 1 if line (x1,y1)→(x2,y2) intersects rect (rx,ry,rw,rh).
// Optionally outputs hit point (hx, hy) and parametric t (0-1000).
int8_t rt_collision_line_rect(double x1, double y1, double x2, double y2,
                              double rx, double ry, double rw, double rh);
```
Algorithm: Liang-Barsky (axis-aligned clipping, ~30 lines).

**Line-segment vs circle:**
```c
int8_t rt_collision_line_circle(double x1, double y1, double x2, double y2,
                                double cx, double cy, double r);
```
Algorithm: quadratic formula on parameterized line, ~20 lines.

### New file: `src/runtime/game/rt_raycast2d.c` (~150 LOC)

**Tilemap raycast (DDA algorithm):**
```c
typedef struct {
    int8_t hit;         // 1 if solid tile found
    int64_t tile_x;     // Tile column of hit
    int64_t tile_y;     // Tile row of hit
    int64_t world_x;    // World pixel X of hit point
    int64_t world_y;    // World pixel Y of hit point
    int64_t distance;   // Distance from origin (pixels)
} rt_raycast_result;

// Cast ray from (x1,y1) toward (x2,y2), stopping at first solid tile.
rt_raycast_result rt_raycast_tilemap(void *tilemap,
                                     int64_t x1, int64_t y1,
                                     int64_t x2, int64_t y2);

// Convenience: returns true if no solid tile between two points.
int8_t rt_has_line_of_sight(void *tilemap,
                            int64_t x1, int64_t y1,
                            int64_t x2, int64_t y2);
```

DDA (Digital Differential Analyzer) algorithm: step through tiles along the ray,
one tile at a time, checking tilemap collision flags. O(distance/tile_size).

### runtime.def
```
RT_FUNC(CollisionLineRect,   rt_collision_line_rect,   "Viper.Game.Collision.LineRect",   "i1(f64,f64,f64,f64,f64,f64,f64,f64)")
RT_FUNC(CollisionLineCircle, rt_collision_line_circle, "Viper.Game.Collision.LineCircle", "i1(f64,f64,f64,f64,f64,f64,f64)")
RT_FUNC(RaycastTilemap,      rt_raycast_tilemap_zia,   "Viper.Game.Raycast.Cast",         "obj(obj,i64,i64,i64,i64)")
RT_FUNC(HasLineOfSight,      rt_has_line_of_sight,     "Viper.Game.Raycast.HasLineOfSight","i1(obj,i64,i64,i64,i64)")
```

The `RaycastTilemap` wrapper returns a result object with Hit, TileX, TileY, WorldX, WorldY, Distance properties.

### Zia usage
```zia
if Raycast.HasLineOfSight(tilemap, enemyX, enemyY, playerX, playerY) {
    // chase the player
}

var hit = Raycast.Cast(tilemap, gunX, gunY, targetX, targetY)
if hit.get_Hit() {
    particles.spawn(hit.get_WorldX(), hit.get_WorldY(), "spark")
}
```

### Files to modify
- `src/runtime/game/rt_collision.c` — add LineRect, LineCircle
- New: `src/runtime/game/rt_raycast2d.c` (~150 LOC)
- New: `src/runtime/game/rt_raycast2d.h` (~30 LOC)
- `src/il/runtime/runtime.def` — 4 entries + result class
- `src/il/runtime/RuntimeSignatures.cpp` — include new header
- `src/il/runtime/classes/RuntimeClasses.hpp` — add RTCLS_RaycastResult
- `src/runtime/CMakeLists.txt` — add source

### Tests

**File:** `src/tests/unit/runtime/TestRaycast2D.cpp`
```
TEST(Raycast, LineRectHit)
TEST(Raycast, LineRectMiss)
TEST(Raycast, LineCircleHit)
TEST(Raycast, LineCircleMiss)
TEST(Raycast, TilemapHitsSolidTile)
TEST(Raycast, TilemapMissesEmpty)
TEST(Raycast, TilemapReturnsCorrectTileCoords)
TEST(Raycast, LineOfSightClearPath)
TEST(Raycast, LineOfSightBlockedByWall)
TEST(Raycast, DiagonalRayHitsCorrectTile)
```

### Doc update
- New: `docs/viperlib/game/raycast.md`
- `docs/viperlib/game/collision.md` — add LineRect, LineCircle
