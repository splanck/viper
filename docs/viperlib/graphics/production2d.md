---
status: active
audience: public
last-verified: 2026-04-21
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
| `Transform2D` | Integer 2D transform with position, percent scale, rotation, origin, and point transforms. |
| `Sampler2D` | Reusable texture sampler state for `Filter` and `Wrap`. |
| `BlendState2D` | Reusable renderer blend state for blend mode, tint, and alpha. |
| `SpriteRenderer2D` | Draw helper that applies `Material2D`, `Sampler2D`, and `BlendState2D` before queueing pixels or textures. |
| `TilemapRenderer2D` | Tilemap draw facade with draw-count tracking and optional chunk-cache association. |
| `TileChunkCache2D` | Chunk sizing and dirty-count tracking for tilemap renderers and editors. |
| `AnimationClip2D` | Frame range, frame delay, and loop metadata for 2D sprite animation. |
| `AnimatedSprite2D` | Runtime clip player that advances a `Sprite` frame from elapsed milliseconds. |
| `TextLayout2D` | Text measurement helper with scale, wrap width, alignment metadata, and optional font. |
| `SpriteFont` | Game-facing alias for `BitmapFont` loading and measurement. |
| `RenderPass2D` | Source-target postprocess pass using an optional `Shader2D`. |
| `RenderGraph2D` | Ordered collection of `RenderPass2D` objects for simple pass chains. |
| `CollisionMask2D` | Dense per-pixel solid mask with alpha-threshold construction and mask overlap tests. |
| `Hitbox2D` | Axis-aligned rectangle hitbox with containment and intersection tests. |
| `Palette2D` | 256-entry palette that can recolor indexed `Pixels` buffers. |
| `Gradient2D` | RGBA gradient sampling and horizontal/vertical fills for `Pixels`. |
| `CameraRig2D` | Follow-target camera controller with smoothing, deadzone forwarding, and render shake offsets. |
| `TexturePackerAtlas` | Texture-atlas authoring wrapper over `TextureAtlas` for named regions. |
| `AsepriteImporter` | Grid-to-atlas helper for Aseprite-style sprite sheets. |
| `TiledMapLoader` | Tile-size helper that creates `Tilemap` objects using Tiled-compatible dimensions. |
| `Lighting2D` | Graphics namespace alias for `Viper.Game.Lighting2D`. |

## Color And Scale Conventions

- `Pixels` storage is `0xRRGGBBAA`.
- Shape, path, and debug draw colors accept Canvas-style `0x00RRGGBB` and `Color.RGBA(...)` / `0xAARRGGBB`; alpha is ignored by these RGB-only CPU drawing primitives.
- Renderer/material/blend-state tint uses `-1` for no tint. A tint value of `0` is black.
- `Viewport2D.Scale` is fixed-point with `1000` representing `1.0x`. For example, `4000` means `4.0x`.
- `Texture2D.Filter` uses `0 = nearest`, `1 = linear`.
- `Texture2D.Wrap` uses `0 = clamp`, `1 = repeat`.
- Blend modes use `0 = alpha`, `1 = opaque`, `2 = additive`. Alpha mode uses straight-alpha source-over, matching `Pixels.BlendPixel` and `Canvas.BlitAlpha`.

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

`Renderer2D` keeps retained references to queued sources, so textures and pixels can be queued safely during a frame. Calling `Begin` clears the previous command list. `FlushToTarget(target)` draws to an offscreen target without ending the batch; `End(canvas)` draws to a Canvas, clears queued commands, and makes repeated `End` calls a no-op until the next `Begin`.

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

## State, Animation, And Passes

```viper
var sampler = Sampler2D.New()
sampler.Filter = 1
sampler.ApplyToTexture(texture)

var blend = BlendState2D.New()
blend.BlendMode = 0
blend.Alpha = 192

var spriteDraw = SpriteRenderer2D.New()
spriteDraw.SetBlendState(blend)
spriteDraw.SetSampler(sampler)
spriteDraw.DrawTexture(renderer, texture, 32, 48)

var clip = AnimationClip2D.New(0, 4, 80, 1)
var animated = AnimatedSprite2D.New(sprite)
animated.SetClip(clip)
animated.Update(deltaMs)
```

`RenderPass2D` expects `RenderTarget2D` source and target objects. When a shader is attached, the pass applies it to the source pixels and draws the result into the target. `RenderGraph2D` executes passes in insertion order.

## Collision, Color, And Camera Helpers

```viper
var mask = CollisionMask2D.FromPixels(playerPixels, 1)
var hurt = Hitbox2D.New(4, 4, 8, 8)

var palette = Palette2D.New()
palette.SetColor(3, 0xFF0000FF)
var recolored = palette.Apply(indexedPixels)

var rig = CameraRig2D.New(camera)
rig.SetTarget(playerX, playerY)
rig.SetSmoothing(160)
rig.Update()
```

`CollisionMask2D.FromPixels` marks pixels solid when alpha is greater than or equal to the threshold. `Palette2D.Apply` treats the source pixel red channel as the palette index and writes `0xRRGGBBAA` colors to a new buffer.

## UI And Debug Drawing

`NineSlice2D` preserves corner pixels and stretches edges and center regions into a target. It alpha-composites over the destination `Pixels`, making it suitable for UI panels generated from sprite assets.

`DebugDraw2D` stores transient diagnostics separately from the game scene. Clear it each frame after drawing if the overlays are frame-local.

## Notes

- `GpuTexture2D` is a compatibility alias today. It intentionally exposes the same behavior as `Texture2D` until a GPU-backed renderer is available.
- `SdfFont` is an SDF-ready API surface, not a full signed-distance-field rasterizer yet.
- `ParticleSystem2D` and `Emitter2D` share the same implementation as `Viper.Game.ParticleEmitter`, including `DrawToPixels`.
- `TexturePackerAtlas`, `AsepriteImporter`, and `TiledMapLoader` are runtime-side helpers for common 2D asset layouts. They currently provide atlas/tilemap construction primitives rather than full external JSON or `.aseprite` file parsing. Atlas regions are validated against their backing `Pixels` buffer before registration.
