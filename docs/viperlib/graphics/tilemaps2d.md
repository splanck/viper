---
status: active
audience: public
last-verified: 2026-05-03
---

# 2D Tilemaps and Layers
> Tilesets, dense tile layers, object layers, autotile resolution, tilemap rendering helpers, and atlas import helpers.

**Part of [Graphics](README.md)**

Use this page for tile-oriented map data and editor/import helpers. The core `Tilemap` class is documented with [Images and Sprites](pixels.md).

## Classes

| Class | Purpose |
|-------|---------|
| `TileSet2D` | Uniform grid tileset over a `Pixels` image. |
| `TileLayer2D` | Dense tile ID layer with visibility and opacity. |
| `ObjectLayer2D` | Rect object layer for collision, triggers, spawn points, and editor metadata. |
| `AutoTile2D` | 16-mask autotile resolver that can apply resolved tile IDs to a `TileLayer2D`. |
| `TilemapRenderer2D` | Tilemap draw facade with draw-count tracking and optional chunk-cache association. |
| `TileChunkCache2D` | Chunk sizing and dirty-count tracking for tilemap renderers and editors. |
| `TexturePackerAtlas` | Texture-atlas authoring wrapper over `TextureAtlas` for named regions. |
| `AsepriteImporter` | Grid-to-atlas helper for Aseprite-style sprite sheets. |
| `TiledMapLoader` | Tile-size helper that creates `Tilemap` objects using Tiled-compatible dimensions. |

## Tilesets, Layers, And Atlas Helpers

```rust
var tiles = TileSet2D.New(Pixels.Load("assets/tiles.png"), 16, 16)
var ground = TileLayer2D.New(128, 64)
ground.Fill(0)
ground.Set(10, 12, 7)

var objects = ObjectLayer2D.New(32)
objects.AddRect(160, 192, 16, 16, 1)

var auto = AutoTile2D.New()
auto.SetVariant(5, 42)
auto.Apply(ground, 10, 12, 5)
```

`TileSet2D.New` requires a `Pixels` image whose dimensions are at least one full tile. The tileset retains the image and indexes tiles left-to-right, top-to-bottom from zero. `TileLayer2D.Get` returns `-1` for out-of-bounds coordinates.

`ObjectLayer2D.AddRect` normalizes negative width or height by moving the origin to the rectangle's top-left corner. Zero-size rectangles and dimensions that cannot be represented are rejected.

## Notes

- `TexturePackerAtlas`, `AsepriteImporter`, and `TiledMapLoader` are runtime-side helpers for common 2D asset layouts.
- Import helpers currently provide atlas/tilemap construction primitives rather than full external JSON or `.aseprite` file parsing.
- Atlas regions are validated against their backing `Pixels` buffer before registration.
- `AsepriteImporter.SetGrid(width, height)` treats non-positive dimensions as an unset grid. `ToAtlas(pixels)` returns `null` until both frame dimensions are positive.
