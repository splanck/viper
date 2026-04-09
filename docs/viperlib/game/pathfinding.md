---
status: active
audience: public
last-verified: 2026-04-09
---

# Pathfinding

> A* grid pathfinding for 2D games.

**Part of [Viper Runtime Library — Game](../game.md)**

## Contents

- [Viper.Game.Pathfinder](#vipergamepathfinder)
- [Movement Modes](#movement-modes)
- [Cost Weights](#cost-weights)
- [Integration](#integration-with-tilemap-and-grid2d)
- [Usage Example](#usage-example)

---

## Viper.Game.Pathfinder

A* pathfinding on uniform-cost 2D grids. Supports 4-way (cardinal) and 8-way (cardinal + diagonal) movement with per-cell movement cost weights.

**Type:** Instance (obj)
**Constructor:** `Pathfinder.New(width, height)`

### Static Factory Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `New(width, height)` | `Pathfinder(Integer, Integer)` | Blank walkable grid |
| `FromTilemap(tilemap)` | `Pathfinder(Tilemap)` | Import collision data (collision != 0 → wall) |
| `FromGrid2D(grid)` | `Pathfinder(Grid2D)` | Import cell values (non-zero → wall) |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Width` | Integer | Read | Grid width |
| `Height` | Integer | Read | Grid height |
| `Diagonal` | Boolean | Write | Enable 8-way movement (default: false = 4-way) |
| `MaxSteps` | Integer | Write | Max nodes to expand (0 = unlimited) |
| `LastSteps` | Integer | Read | Nodes expanded in last search |
| `LastFound` | Boolean | Read | True if last search found a path |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetWalkable(x, y, walkable)` | `void(Integer, Integer, Boolean)` | Mark cell as walkable/blocked |
| `IsWalkable(x, y)` | `Boolean(Integer, Integer)` | Check if cell is walkable |
| `SetCost(x, y, cost)` | `void(Integer, Integer, Integer)` | Set movement cost (100 = normal) |
| `GetCost(x, y)` | `Integer(Integer, Integer)` | Get movement cost |
| `FindPath(sx, sy, gx, gy)` | `List[Integer](Integer×4)` | Find path (returns interleaved x,y pairs) |
| `FindPathLength(sx, sy, gx, gy)` | `Integer(Integer×4)` | Get path cost (-1 if no path) |
| `FindNearest(sx, sy, value)` | `List[Integer](Integer×3)` | Find nearest cell with value (placeholder) |

---

## Movement Modes

### 4-Way (default)
- Cardinal directions only: up, right, down, left
- Heuristic: Manhattan distance
- Base step cost: 100

### 8-Way (Diagonal = true)
- Cardinal + diagonal directions (8 neighbors)
- Heuristic: octile distance
- Cardinal step cost: 100, diagonal step cost: 141 (~sqrt(2) x 100)
- Corner-cutting prevention: diagonal moves require both adjacent cardinal cells to be walkable

---

## Cost Weights

Each cell has a movement cost multiplier (default 100 = 1x).

| Cost | Meaning |
|------|---------|
| 100 | Normal terrain (1x) |
| 200 | Difficult terrain (2x cost) |
| 50 | Easy terrain (0.5x cost) |
| 0 | Impassable (same as not walkable) |

The actual movement cost for a step is: `base_cost × cell_cost / 100`

---

## Integration with Tilemap and Grid2D

### FromTilemap
Reads the tilemap's collision types. Tiles with `SetCollision(tile, type)` where `type != 0` are marked as non-walkable.

```zia
var tilemap = Tilemap.New(20, 15, 16, 16);
tilemap.SetCollision(1, 1); // tile 1 = solid
// ... set tiles ...

var pf = Pathfinder.FromTilemap(tilemap);
var path = pf.FindPath(0, 0, 19, 14);
```

### FromGrid2D
Reads cell values. Non-zero cells are treated as walls.

```zia
var grid = Grid2D.New(20, 15, 0); // 0 = walkable
grid.Set(5, 5, 1);                // 1 = wall

var pf = Pathfinder.FromGrid2D(grid);
```

---

## Usage Example

```zia
bind Viper.Game;

// Create pathfinder from tilemap
var pf = Pathfinder.FromTilemap(levelTilemap);
pf.Diagonal = true; // Allow diagonal movement

// Find path for enemy AI
var path = pf.FindPath(enemyTileX, enemyTileY, playerTileX, playerTileY);

if pf.LastFound {
    // Path is List[Integer] of interleaved x,y pairs
    // Feed to PathFollower for smooth movement
    var i = 0;
    while i < path.Length {
        var wx = path.Get(i);
        var wy = path.Get(i + 1);
        pathFollower.AddPoint(wx * TILE_SIZE + TILE_SIZE / 2,
                              wy * TILE_SIZE + TILE_SIZE / 2);
        i = i + 2;
    }
    pathFollower.Start();
}
```

---

## Limits

| Limit | Value |
|-------|-------|
| Max grid dimension | 4096 × 4096 |
| Default step cost | 100 (fixed-point) |
| Diagonal step cost | 141 (~sqrt(2) × 100) |
| Cost range | 0–30000 |
| Max steps | 0 = unlimited |

---

## See Also

- [PathFollower](../game.md#vipergamepathfollower) — Smooth movement along waypoint paths
- [Tilemap](../graphics/pixels.md) — Tile-based level with collision data
- [Grid2D](../game.md#vipergamegrid2d) — 2D integer grid for game data
