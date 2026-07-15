---
status: active
audience: public
last-verified: 2026-07-15
---

# Pathfinding
> Bounded A* and breadth-first grid searches for 2D games.

**Part of [Viper Runtime Library](../README.md) › [Game Utilities](README.md)**

`Viper.Game.Pathfinder` owns a snapshot of grid walkability, per-cell movement
costs, and source values. `FindPath` performs weighted A* between two cells;
`FindNearest` performs an unweighted breadth-first search for the nearest
reachable cell with a stored value. Both return an immutable
`Viper.Game.PathResult` snapshot.

## Construction

| Factory | Current behavior |
|---|---|
| `Pathfinder.New(width, height)` | Creates an all-walkable grid with cost `100`; returns null for nonpositive dimensions, either dimension above 4096, or allocation failure. |
| `Pathfinder.FromTilemap(tilemap)` | Copies base-layer tile IDs and treats every tile whose registered collision type is nonzero as blocked. |
| `Pathfinder.FromGrid2D(grid)` | Copies cell values and initially treats every nonzero value as blocked. |

Factories make one-time snapshots; later Tilemap/Grid2D changes do not update
the Pathfinder. The two import function rows currently return untyped `obj`
rather than `obj<Viper.Game.Pathfinder>`. Current Zia recovers Pathfinder
identity from the fully qualified owner call, but the raw API inventory and
consumers without that provenance do not receive the concrete return type.

`FromTilemap` currently samples Tilemap layer zero even when another layer is
designated as the collision layer. Until that defect is fixed, copy or mirror
collision tiles into the base layer before importing.

## Properties And Configuration

| Member | Access | Behavior |
|---|---|---|
| `Width`, `Height` | Read | Snapshot dimensions. |
| `Diagonal` | Write | False selects four cardinal neighbors; true adds diagonals. |
| `MaxSteps` | Write | Maximum popped/expanded cells per search. `0` is unlimited; negatives become `0`, and values above `2,147,483,647` clamp. |
| `SetWalkable(x, y, value)` | Method | Change walkability; out-of-range writes do nothing. |
| `IsWalkable(x, y)` | Method | Read walkability; out-of-range cells are false. |
| `SetCost(x, y, cost)` | Method | Set destination-cell cost, clamped to `[1, 30000]`; out-of-range writes do nothing. |
| `GetCost(x, y)` | Method | Return the cost, or `0` out of range. |
| `Destroy()` | Method | Explicitly release this handle; do not use it afterward. |

`Diagonal` and `MaxSteps` are setter-only properties. The old mutable
last-search properties are no longer public; read each operation's result
instead.

## `FindPath`

```zia
module PathfinderDemo;

bind Viper.Terminal;

func start() {
    var pf = Viper.Game.Pathfinder.New(8, 6);
    pf.SetWalkable(3, 2, false);
    pf.SetWalkable(3, 3, false);
    pf.Diagonal = true;

    var result = pf.FindPath(0, 0, 7, 5);
    if result.Found {
        Say("steps=" + result.StepCount + ", cost=" + result.Cost);
    }
}
```

Four-way movement uses cardinal steps with base cost `100` and a Manhattan
heuristic. Eight-way movement uses cardinal cost `100`, diagonal cost `141`,
and an octile heuristic. A diagonal is permitted only when both adjacent
cardinal cells are walkable, preventing corner cutting.

The cost of entering a cell is `baseCost * cellCost / 100` with integer
truncation. The heuristic is scaled by the lowest walkable cost anywhere in
the grid, so costs below `100` remain admissible. The start and goal must both
be in bounds and walkable. A successful path includes both endpoints; start
equal to goal produces one point, zero steps, and zero cost.

`MaxSteps` counts nodes removed from the open set, including the goal when it
is reached. Exhausting that budget is reported the same way as an unreachable
goal.

## `FindNearest`

`FindNearest(startX, startY, value)` explores reachable cells in breadth-first
order using the selected four- or eight-neighbor rules. It ignores movement
cost weights and returns `Cost == -1`. It can match the start cell, and its
path also includes both endpoints.

`FromGrid2D` marks every nonzero source value blocked. To search for a nonzero
marker copied from a Grid2D, first call `SetWalkable` on candidate marker cells.
`FromTilemap` retains tile IDs separately from walkability, so a tile with
collision type zero can be found directly.

## PathResult

| Property | Meaning |
|---|---|
| `Found` | Whether the goal or matching value was reached. |
| `Steps` | Number of cells expanded by the search. |
| `StepCount` | Cell-to-cell movement count, or `-1` when not found. |
| `Cost` | Weighted A* cost; `-1` for misses and all `FindNearest` results. |
| `Path` | Retained runtime `Viper.Collections.List` of two-element `Viper.Collections.Seq` points. |

The public `Path` property is currently registered only as `obj`, not as a
typed List. Zia treats it as `Any` and currently rejects both assignment and an
`as` cast to `Viper.Collections.List`. The low-level static collection calls
are the available workaround:

```zia
var points = result.Path;
var i = 0;
while i < Viper.Collections.List.get_Count(points) {
    var point = Viper.Collections.List.Get(points, i);
    var x = Viper.Core.Box.ToI64(Viper.Collections.Seq.Get(point, 0));
    var y = Viper.Core.Box.ToI64(Viper.Collections.Seq.Get(point, 1));
    // Convert cell coordinates to world coordinates here.
    i = i + 1;
}
```

Allocation failure during a search is not distinguished from an unreachable
path. A later allocation failure while assembling point objects can also
produce a shortened/empty `Path` even though `Found` was already set true;
callers processing untrusted grid sizes should sanity-check
`Path.Count == StepCount + 1` on successful results.

## Current Surface Migration

The authoritative registry now exposes `FindPath(...) -> PathResult` and
`FindNearest(...) -> PathResult`. It no longer exposes the earlier list-returning
`FindPath`, `FindPathLength`, `FindPathResult`, `FindNearestResult`,
`LastFound`, `LastSteps`, or `PathResult.Length` names. Existing generated docs,
tests, ADRs, and built binaries may still show that older surface while the
repository-wide compatibility cleanup is in progress; this uncoordinated drift
is recorded in the documentation findings log.

## Limits

| Limit | Value |
|---|---:|
| Maximum width or height | 4096 cells |
| Default cell cost | 100 |
| Cardinal/diagonal base costs | 100 / 141 |
| Stored cost range | 1–30000 |
| Search budget | `0` unlimited; otherwise up to `INT32_MAX` expansions |

The grid cells are allocated at construction, while A* allocates node and heap
arrays proportional to `width * height` for each search. A nominal 4096×4096
grid can therefore require hundreds of MiB during a search.

## See Also

- [Animation and movement](animation.md) — `PathFollower` consumes world-space waypoints
- [Physics and collision](physics.md) — Grid2D
- [Graphics2D](../graphics/README.md) — Tilemap layers and collision types
