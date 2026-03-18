# Plan: Grid Pathfinding (A*)

## 1. Summary & Objective

Add a `Viper.Game.Pathfinder` class implementing the A* algorithm on 2D grids. Integrates directly with existing Grid2D and Tilemap classes. Returns waypoint lists compatible with the existing PathFollower class.

**Why:** Pac-Man needs ghost AI pathfinding. Any strategy, RPG, tower defense, or top-down game needs A*. Currently developers must implement it from scratch — a non-trivial algorithm with heap management and heuristic tuning.

## 2. Scope

**In scope:**
- A* pathfinding on uniform-cost 2D grids
- 4-way and 8-way movement modes
- Configurable diagonal cost (default: 141 for √2 × 100 fixed-point)
- Walkability grid with per-cell cost weights
- `FromTilemap()` constructor that reads collision data
- `FromGrid2D()` constructor that reads non-zero cells as walls
- Path result as list of (x,y) waypoint pairs
- `FindNearest()` for nearest reachable cell of a given type
- Max search limit to prevent pathological cases from freezing

**Out of scope:**
- Navigation meshes (non-grid)
- Hierarchical pathfinding (HPA*)
- Dynamic obstacle avoidance / steering behaviors
- 3D pathfinding
- Jump Point Search optimization (can be added later)
- Flow fields

## 3. Zero-Dependency Implementation Strategy

A* uses a binary min-heap as open set and a flat 2D array for visited/parent tracking. All data structures are simple arrays — no external libraries. The heuristic is octile distance for 8-way, Manhattan distance for 4-way. ~400 LOC total.

## 4. Technical Requirements

### New Files
- `src/runtime/collections/rt_pathfinder.h` — public API
- `src/runtime/collections/rt_pathfinder.c` — implementation (~400 LOC)

### C API (rt_pathfinder.h)

```c
// Construction
void   *rt_pathfinder_new(int64_t width, int64_t height);          // Blank grid
void   *rt_pathfinder_from_tilemap(void *tilemap);                  // Read collision data
void   *rt_pathfinder_from_grid2d(void *grid);                      // Non-zero = wall
void    rt_pathfinder_destroy(void *pf);                            // GC finalizer

// Configuration
void    rt_pathfinder_set_walkable(void *pf, int64_t x, int64_t y, int8_t walkable);
int8_t  rt_pathfinder_is_walkable(void *pf, int64_t x, int64_t y);
void    rt_pathfinder_set_cost(void *pf, int64_t x, int64_t y, int64_t cost); // 100 = normal
int64_t rt_pathfinder_get_cost(void *pf, int64_t x, int64_t y);
void    rt_pathfinder_set_diagonal(void *pf, int8_t allow);        // 0=4-way, 1=8-way
void    rt_pathfinder_set_max_steps(void *pf, int64_t max);        // 0=unlimited

// Properties
int64_t rt_pathfinder_get_width(void *pf);
int64_t rt_pathfinder_get_height(void *pf);

// Pathfinding
void   *rt_pathfinder_find_path(void *pf, int64_t sx, int64_t sy,
                                 int64_t gx, int64_t gy);           // → List[Integer] of x,y pairs
int64_t rt_pathfinder_find_path_length(void *pf, int64_t sx, int64_t sy,
                                        int64_t gx, int64_t gy);   // → path cost or -1
void   *rt_pathfinder_find_nearest(void *pf, int64_t sx, int64_t sy,
                                    int64_t target_value);           // → List[Integer] (x,y) or empty

// State query (after last findPath)
int64_t rt_pathfinder_get_last_steps(void *pf);                    // Nodes expanded in last search
int8_t  rt_pathfinder_get_last_found(void *pf);                    // 1 if path found
```

### Internal Data Structure

```c
typedef struct {
    int16_t  cost;       // Movement cost (100 = normal, 0 = impassable)
    int8_t   walkable;   // 1 = passable
} pf_cell;

typedef struct {
    int32_t  x, y;
    int32_t  g, f;       // g-cost, f-cost (g + heuristic)
    int32_t  parent;     // Index of parent in flat array (-1 = start)
    int8_t   closed;
} pf_node;

struct rt_pathfinder_impl {
    pf_cell  *cells;           // width × height grid
    int32_t   width, height;
    int8_t    allow_diagonal;  // 0 = 4-way, 1 = 8-way
    int32_t   max_steps;       // 0 = unlimited
    int32_t   last_steps;      // Stats from last search
    int8_t    last_found;
};
```

### Binary Min-Heap (inline, ~60 LOC)

```c
// Open set stored as array of node indices, heapified by f-cost
// Standard sift-up / sift-down operations
// No dynamic allocation beyond initial node array (width × height pre-allocated)
```

## 5. runtime.def Registration

```c
//=============================================================================
// GAME - PATHFINDER (A*)
//=============================================================================

RT_FUNC(PathfinderNew,          rt_pathfinder_new,           "Viper.Game.Pathfinder.New",           "obj(i64,i64)")
RT_FUNC(PathfinderFromTilemap,  rt_pathfinder_from_tilemap,  "Viper.Game.Pathfinder.FromTilemap",   "obj(obj)")
RT_FUNC(PathfinderFromGrid2D,   rt_pathfinder_from_grid2d,   "Viper.Game.Pathfinder.FromGrid2D",    "obj(obj)")
RT_FUNC(PathfinderSetWalkable,  rt_pathfinder_set_walkable,  "Viper.Game.Pathfinder.SetWalkable",   "void(obj,i64,i64,i1)")
RT_FUNC(PathfinderIsWalkable,   rt_pathfinder_is_walkable,   "Viper.Game.Pathfinder.IsWalkable",    "i1(obj,i64,i64)")
RT_FUNC(PathfinderSetCost,      rt_pathfinder_set_cost,      "Viper.Game.Pathfinder.SetCost",       "void(obj,i64,i64,i64)")
RT_FUNC(PathfinderGetCost,      rt_pathfinder_get_cost,      "Viper.Game.Pathfinder.GetCost",       "i64(obj,i64,i64)")
RT_FUNC(PathfinderSetDiagonal,  rt_pathfinder_set_diagonal,  "Viper.Game.Pathfinder.set_Diagonal",  "void(obj,i1)")
RT_FUNC(PathfinderSetMaxSteps,  rt_pathfinder_set_max_steps, "Viper.Game.Pathfinder.set_MaxSteps",  "void(obj,i64)")
RT_FUNC(PathfinderGetWidth,     rt_pathfinder_get_width,     "Viper.Game.Pathfinder.get_Width",     "i64(obj)")
RT_FUNC(PathfinderGetHeight,    rt_pathfinder_get_height,    "Viper.Game.Pathfinder.get_Height",    "i64(obj)")
RT_FUNC(PathfinderFindPath,     rt_pathfinder_find_path,     "Viper.Game.Pathfinder.FindPath",      "obj(obj,i64,i64,i64,i64)")
RT_FUNC(PathfinderFindPathLen,  rt_pathfinder_find_path_length,"Viper.Game.Pathfinder.FindPathLength","i64(obj,i64,i64,i64,i64)")
RT_FUNC(PathfinderFindNearest,  rt_pathfinder_find_nearest,  "Viper.Game.Pathfinder.FindNearest",   "obj(obj,i64,i64,i64)")
RT_FUNC(PathfinderLastSteps,    rt_pathfinder_get_last_steps,"Viper.Game.Pathfinder.get_LastSteps", "i64(obj)")
RT_FUNC(PathfinderLastFound,    rt_pathfinder_get_last_found,"Viper.Game.Pathfinder.get_LastFound", "i1(obj)")

RT_CLASS_BEGIN("Viper.Game.Pathfinder", Pathfinder, "obj", PathfinderNew)
    RT_PROP("Width", "i64", PathfinderGetWidth, none)
    RT_PROP("Height", "i64", PathfinderGetHeight, none)
    RT_PROP("Diagonal", "i1", none, PathfinderSetDiagonal)
    RT_PROP("MaxSteps", "i64", none, PathfinderSetMaxSteps)
    RT_PROP("LastSteps", "i64", PathfinderLastSteps, none)
    RT_PROP("LastFound", "i1", PathfinderLastFound, none)
    RT_METHOD("SetWalkable", "void(i64,i64,i1)", PathfinderSetWalkable)
    RT_METHOD("IsWalkable", "i1(i64,i64)", PathfinderIsWalkable)
    RT_METHOD("SetCost", "void(i64,i64,i64)", PathfinderSetCost)
    RT_METHOD("GetCost", "i64(i64,i64)", PathfinderGetCost)
    RT_METHOD("FindPath", "obj(i64,i64,i64,i64)", PathfinderFindPath)
    RT_METHOD("FindPathLength", "i64(i64,i64,i64,i64)", PathfinderFindPathLen)
    RT_METHOD("FindNearest", "obj(i64,i64,i64)", PathfinderFindNearest)
RT_CLASS_END()
```

## 6. CMakeLists.txt Changes

In `src/runtime/CMakeLists.txt`, add to `RT_COLLECTIONS_SOURCES`:
```cmake
collections/rt_pathfinder.c
```

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| Start/goal out of bounds | Return empty list, `LastFound = false` |
| Start == goal | Return single-element list [sx, sy] |
| Start or goal is not walkable | Return empty list |
| No path exists (fully blocked) | Return empty list, `LastFound = false` |
| Max steps exceeded | Return empty list, `LastSteps = max_steps` |
| NULL tilemap/grid passed to factory | Return NULL |
| Grid too large (>65536 × 65536) | Clamp to 65536, log warning |
| Allocation failure | Return NULL |

## 8. Tests

### Zia Runtime Tests (`tests/runtime/test_pathfinder.zia`)

1. **Simple 4-way path**
   - Given: 5×5 grid, all walkable
   - When: `FindPath(0, 0, 4, 4)` with diagonal=false
   - Then: Path length = 8 moves (Manhattan), path is valid sequence

2. **8-way diagonal path**
   - Given: 5×5 grid, all walkable, diagonal=true
   - When: `FindPath(0, 0, 4, 4)`
   - Then: Path length = 4 moves (diagonal shortcut)

3. **Wall avoidance**
   - Given: 5×5 grid, vertical wall at x=2 (rows 0-3), gap at (2,4)
   - When: `FindPath(0, 0, 4, 0)`
   - Then: Path routes around wall through (2,4)

4. **No path exists**
   - Given: 5×5 grid, complete wall at x=2
   - When: `FindPath(0, 0, 4, 0)`
   - Then: Empty list returned, `LastFound == false`

5. **Start equals goal**
   - Given: Any grid
   - When: `FindPath(2, 2, 2, 2)`
   - Then: Single-element path [(2,2)]

6. **FromTilemap integration**
   - Given: Tilemap with known collision tiles
   - When: `Pathfinder.FromTilemap(tilemap)` then `FindPath(...)`
   - Then: Path avoids solid tiles

7. **Weighted costs**
   - Given: 5×5 grid, center cell cost=500 (5x normal)
   - When: `FindPath(0, 2, 4, 2)` with diagonal=true
   - Then: Path prefers going around the expensive cell

## 9. Documentation Deliverables

| Action | File |
|--------|------|
| CREATE | `docs/viperlib/game/pathfinding.md` — full Pathfinder API reference with examples |
| UPDATE | `docs/viperlib/game.md` — add `Viper.Game.Pathfinder` to contents |

## 10. Code References

| File | Role |
|------|------|
| `src/runtime/collections/rt_grid2d.c` | Grid2D integration (read cell values) |
| `src/runtime/graphics/rt_tilemap.c` | Tilemap integration (read collision data) |
| `src/runtime/collections/rt_pathfollow.c` | PathFollower consumes waypoint lists |
| `src/runtime/collections/rt_quadtree.c` | Pattern: spatial algorithm in collections |
| `src/runtime/core/rt_heap.h` | Existing heap (may reuse for open set) |
| `src/il/runtime/runtime.def` | Registration (add after Quadtree block) |
