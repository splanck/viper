---
status: active
audience: public
last-verified: 2026-07-15
---

# Viper.Game.Raycast and line tests

These static helpers use pixel coordinates. Tile line-of-sight clips the segment to the finite map
and traverses every touched cell with grid DDA; it is not capped by a fixed number of pixel steps.

## API

- `Viper.Game.Raycast.HasLineOfSight(tilemap, x1, y1, x2, y2)` is false when any traversed tile is
  solid, including the starting/ending cell. A null map or a segment wholly outside the map is
  treated as clear. A segment exactly on the excluded right/bottom extent currently aliases the
  last tile and can report a false obstruction (VDOC-242).
- `Viper.Game.Collision.LineRect(x1, y1, x2, y2, rx, ry, width, height)` uses Liang-Barsky segment
  clipping. Negative dimensions or any non-finite input return false; zero dimensions are allowed.
- `Viper.Game.Collision.LineCircle(x1, y1, x2, y2, cx, cy, radius)` uses segment/circle
  intersection. Negative or non-finite radii/coordinates return false. A zero-length segment is a
  point-in-circle test.

## Example

```rust
module RaycastExample;

func start() {
    var map = Viper.Graphics2D.Tilemap.New(2, 1, 16, 16);
    map.SetCollision(1, 1);
    map.SetTile(1, 0, 1);

    var clear = Viper.Game.Raycast.HasLineOfSight(map, 0, 8, 31, 8);
    var crosses = Viper.Game.Collision.LineRect(0.0, 0.0, 10.0, 10.0,
                                                 4.0, 4.0, 2.0, 2.0);
    Viper.Terminal.SayBool(clear);
    Viper.Terminal.SayBool(crosses);
}
```
