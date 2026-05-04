---
status: active
audience: public
last-verified: 2026-05-03
---

# 2D Rendering and Effects
> Offscreen surfaces, texture handles, draw command queues, effects, post-processing, blend state, and color helpers.

**Part of [Graphics](README.md)**

These classes sit directly on top of `Pixels` and `Canvas`. They cover rendering state and image-processing workflows; tilemaps, UI helpers, animation, and collision live on separate pages.

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
| `Sampler2D` | Reusable texture sampler state for `Filter` and `Wrap`. |
| `BlendState2D` | Reusable renderer blend state for blend mode, tint, and alpha. |
| `SpriteRenderer2D` | Draw helper that applies `Material2D`, `Sampler2D`, and `BlendState2D` before queueing pixels or textures. |
| `RenderPass2D` | Source-target postprocess pass using an optional `Shader2D`. |
| `RenderGraph2D` | Ordered collection of `RenderPass2D` objects for simple pass chains. |
| `Palette2D` | 256-entry palette that can recolor indexed `Pixels` buffers. |
| `Gradient2D` | RGBA gradient sampling and horizontal/vertical fills for `Pixels`. |

## Color And Blend Conventions

- `Pixels` storage is raw `0xRRGGBBAA`.
- `Palette2D` and `Gradient2D` store raw `0xRRGGBBAA` colors, and also accept tagged `Color.RGBA(...)` values by converting them into raw pixel storage.
- Renderer/material/blend-state tint uses `-1` for no tint. A tint value of `0` is black.
- Blend modes use `0 = alpha`, `1 = opaque`, `2 = additive`. Alpha mode uses straight-alpha source-over, matching `Pixels.BlendPixel` and `Canvas.BlitAlpha`; additive mode scales source RGB by source alpha, adds it to the destination, and clamps each channel.
- `Texture2D.Filter` uses `0 = nearest`, `1 = linear`.
- `Texture2D.Wrap` uses `0 = clamp`, `1 = repeat`.

## Render Targets, Textures, And Renderer

```viper
var target = RenderTarget2D.New(320, 180)
var spritePixels = Pixels.Load("assets/player.png")
var texture = Texture2D.New(spritePixels)

var renderer = Renderer2D.New(256)
renderer.Begin()
renderer.SetAlpha(255)
renderer.DrawTexture(texture, 32, 48)
renderer.DrawTextureScaled(texture, 96, 48, 64, 64)
renderer.FlushToTarget(target)

canvas.BlitAlpha(0, 0, target.Pixels)
```

`Renderer2D` keeps retained references to queued sources, so textures and pixels can be queued safely during a frame. Calling `Begin` clears the previous command list. `FlushToTarget(target)` draws to an offscreen target without ending the batch; `End(canvas)` draws to a Canvas with the queued blend modes, clears queued commands, and makes repeated `End` calls a no-op until the next `Begin`. `DrawTextureScaled(texture, x, y, width, height)` uses the texture's nearest or linear filter. `DrawTextureRegion(texture, x, y, sx, sy, width, height)` samples out-of-bounds source texels through the texture's clamp or repeat wrap mode.

`SpriteRenderer2D` snapshots `Sampler2D` state when queuing a texture draw. It does not mutate the `Texture2D`; call `Sampler2D.ApplyToTexture(texture)` only when you explicitly want to change the texture's stored filter and wrap properties.

## Passes And Color Helpers

```viper
var palette = Palette2D.New()
palette.SetColor(3, 0xFF0000FF)
palette.SetColor(4, Color.RGBA(0, 0, 255, 128))
var recolored = palette.Apply(indexedPixels)

var gradient = Gradient2D.New(0x000000FF, Color.RGBA(255, 255, 255, 192), 16)
gradient.FillHorizontal(pixels)
```

`Palette2D.Apply` treats the source pixel red byte as the palette index and writes `0xRRGGBBAA` colors to a new buffer. Fully transparent legacy index pixels in `0x000000II` form are still accepted. Pixels whose index is beyond the palette count are copied unchanged.

`Gradient2D` uses `Steps <= 2` as smooth interpolation. Larger `Steps` values quantize into that many discrete levels, including both endpoints. For example, a three-step gradient samples start, midpoint, and end colors; horizontal and vertical fills use the same sampling as `Sample`.

## Notes

- `GpuTexture2D` is a compatibility alias today. It intentionally exposes the same behavior as `Texture2D` until a GPU-backed renderer is available.
- `RenderPass2D` requires `RenderTarget2D` source and target objects. Invalid source or target handles make the pass a no-op instead of interpreting the handle as a render target.
