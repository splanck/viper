---
status: active
audience: public
last-verified: 2026-04-20
---

# Production 2D Graphics
> Runtime classes for offscreen rendering, texture handles, command rendering, effects, layout scaling, tile/object layers, vector shapes, text, nine-slice UI, particles, and debug drawing.

**Part of [Graphics](README.md)**

These classes are CPU-backed in the current runtime and build on `Pixels`, `Canvas`, and the existing game particle system. They are intended to provide a stable production-facing 2D surface while the renderer can later gain GPU-specific backends without changing user code.

## Classes

| Class | Purpose |
|-------|---------|
| `RenderTarget2D` | Offscreen RGBA surface with alpha-aware `DrawPixels` and `DrawRegion`. |
| `Surface2D` | Alias for `RenderTarget2D`. |
| `Texture2D` | Retained `Pixels` texture handle with `Filter`, `Wrap`, `ClonePixels`, and `FromFile`. |
| `GpuTexture2D` | Alias for `Texture2D`; currently CPU-backed. |
| `Renderer2D` | Retained draw command stream for pixels/textures with tint, alpha, blend mode, and render target flushing. |
| `Material2D` | Tint, alpha, and blend-mode state that can produce processed pixel copies. |
| `Shader2D` | CPU image effect wrapper: none, invert, grayscale, tint, and blur. |
| `PostProcess2D` | Effect pass wrapper for applying `Shader2D`-style operations to a `Pixels` buffer. |
| `Viewport2D` | Fixed-point screen scaler with virtual size, screen size, offsets, and world/screen transforms. |
| `ScreenScaler` | Alias for `Viewport2D`. |
| `TileSet2D` | Uniform grid tileset over a `Pixels` image. |
| `TileLayer2D` | Dense tile ID layer with visibility and opacity. |
| `ObjectLayer2D` | Rect object layer for collision, triggers, spawn points, and editor metadata. |
| `AutoTile2D` | 16-mask autotile resolver that can apply resolved tile IDs to a `TileLayer2D`. |
| `ParticleSystem2D` | Graphics namespace alias for `Viper.Game.ParticleEmitter`. |
| `Emitter2D` | Short alias for `ParticleSystem2D`. |
| `Path2D` | Dynamic path of move/line points that can draw line segments to `Pixels`. |
| `ShapeRenderer2D` | Convenience renderer for lines, rectangles, circles, and paths on `Pixels`. |
| `TextRenderer2D` | Built-in or `BitmapFont` text measurement and Canvas drawing wrapper. |
| `SdfFont` | SDF-ready font wrapper around `BitmapFont`; the current backend uses bitmap font raster drawing. |
| `NineSlice2D` | Stretchable nine-slice drawing into a `Pixels` target. |
| `DebugDraw2D` | Retained debug line, rectangle, and circle draw queue for `Pixels`. |

## Color And Scale Conventions

- `Pixels` storage is `0xRRGGBBAA`.
- Shape and debug draw colors accept Canvas-style `0x00RRGGBB`; full RGBA input is also accepted by dropping alpha for the current CPU drawing primitives.
- `Viewport2D.Scale` is fixed-point with `1000` representing `1.0x`. For example, `4000` means `4.0x`.
- `Texture2D.Filter` uses `0 = nearest`, `1 = linear`.
- `Texture2D.Wrap` uses `0 = clamp`, `1 = repeat`.
- Blend modes use `0 = alpha`, `1 = opaque`, `2 = additive`.

## Render Target And Renderer

```viper
var target = RenderTarget2D.New(320, 180)
var spritePixels = Pixels.Load("assets/player.png")
var texture = Texture2D.New(spritePixels)

var renderer = Renderer2D.New(256)
renderer.Begin()
renderer.SetAlpha(255)
renderer.DrawTexture(texture, 32, 48)
renderer.FlushToTarget(target)

canvas.BlitAlpha(0, 0, target.Pixels)
```

`Renderer2D` keeps retained references to queued sources, so textures and pixels can be queued safely during a frame. Calling `Begin` clears the previous command list.

## Tile And Object Layers

```viper
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

`TileSet2D` indexes tiles left-to-right, top-to-bottom from zero. `TileLayer2D.Get` returns `-1` for out-of-bounds coordinates.

## UI And Debug Drawing

`NineSlice2D` preserves corner pixels and stretches edges and center regions into a target. It alpha-composites over the destination `Pixels`, making it suitable for UI panels generated from sprite assets.

`DebugDraw2D` stores transient diagnostics separately from the game scene. Clear it each frame after drawing if the overlays are frame-local.

## Notes

- `GpuTexture2D` is a compatibility alias today. It intentionally exposes the same behavior as `Texture2D` until a GPU-backed renderer is available.
- `SdfFont` is an SDF-ready API surface, not a full signed-distance-field rasterizer yet.
- `ParticleSystem2D` and `Emitter2D` share the same implementation as `Viper.Game.ParticleEmitter`, including `DrawToPixels`.
