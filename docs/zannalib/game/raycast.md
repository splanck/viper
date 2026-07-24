---
status: active
audience: public
last-verified: 2026-07-15
---

# Zanna.Game.Raycast and line tests

These static helpers use pixel coordinates. Tile line-of-sight clips the segment to the finite map
and traverses every touched cell with grid DDA; it is not capped by a fixed number of pixel steps.

## API

- `Zanna.Game.Raycast.HasLineOfSight(tilemap, x1, y1, x2, y2)` is false when any traversed tile is
  solid, including the starting/ending cell. A null map or a segment wholly outside the map is
  treated as clear. The tile grid is half-open `[0, mapWidth) x [0, mapHeight)`: a segment lying
  exactly on the excluded right (`x == mapWidth`) or bottom (`y == mapHeight`) extent is tangent to
  the outside and reads as clear, matching a segment one pixel farther out — it no longer aliases
  the last in-bounds tile (VDOC-242).
- `Zanna.Game.Collision.LineRect(x1, y1, x2, y2, rx, ry, width, height)` uses Liang-Barsky segment
  clipping. Negative dimensions or any non-finite input return false; zero dimensions are allowed.
- `Zanna.Game.Collision.LineCircle(x1, y1, x2, y2, cx, cy, radius)` uses segment/circle
  intersection. Negative or non-finite radii/coordinates return false. A zero-length segment is a
  point-in-circle test.

## Example

```zia
module RaycastExample;

func start() {
    var map = Zanna.Graphics2D.Tilemap.New(2, 1, 16, 16);
    map.SetCollision(1, 1);
    map.SetTile(1, 0, 1);

    var clear = Zanna.Game.Raycast.HasLineOfSight(map, 0, 8, 31, 8);
    var crosses = Zanna.Game.Collision.LineRect(0.0, 0.0, 10.0, 10.0,
                                                 4.0, 4.0, 2.0, 2.0);
    Zanna.Terminal.SayBool(clear);
    Zanna.Terminal.SayBool(crosses);
}
```
