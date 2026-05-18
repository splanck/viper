---
status: active
audience: public
last-verified: 2026-05-17
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
| `PostProcess2D` | Distinct effect pass wrapper for applying `Shader2D`-style operations to a `Pixels` buffer. |
| `Sampler2D` | Reusable texture sampler state for `Filter` and `Wrap`. |
| `BlendState2D` | Reusable renderer blend state for blend mode, tint, and alpha. |
| `SpriteRenderer2D` | Draw helper that applies `Material2D`, `Sampler2D`, and `BlendState2D` before queueing pixels or textures. |
| `RenderPass2D` | Source-target postprocess pass using an optional `Shader2D` or `PostProcess2D`. |
| `RenderGraph2D` | Ordered collection of `RenderPass2D` objects for simple pass chains. |
| `Palette2D` | 256-entry palette that can recolor indexed `Pixels` buffers. |
| `Gradient2D` | RGBA gradient sampling and horizontal/vertical fills for `Pixels`. |

## Color And Blend Conventions

- `Pixels` storage is raw `0xRRGGBBAA`.
- `Palette2D` and `Gradient2D` store raw `0xRRGGBBAA` colors, and also accept tagged `Color.RGBA(...)` values by converting them into raw pixel storage. Public `GetColor` and `Sample` return `Color`-compatible values; use `GetRGBA` and `SampleRGBA` for raw storage integers.
- Renderer/material/blend-state tint uses `-1` for no tint. A tint value of `0` is black.
- Blend modes use `0 = alpha`, `1 = opaque`, `2 = additive`. Alpha mode uses straight-alpha source-over, matching `Pixels.BlendPixel` and `Canvas.BlitAlpha`; additive mode scales source RGB by source alpha, adds it to the destination, and clamps each channel.
- `Texture2D.Filter` uses `0 = nearest`, `1 = linear`. Linear sampling interpolates RGB in premultiplied-alpha space so transparent edge texels do not bleed black into partially transparent results.
- `Texture2D.Wrap` uses `0 = clamp`, `1 = repeat`.
- `Texture2D.New` requires a `Pixels` object and retains it for the texture lifetime. `Renderer2D` also retains queued sources before publishing commands, so queued draw calls keep their source objects alive until `Begin`, `End`, `FlushToTarget`, or object cleanup clears the queue.
- `RenderTarget2D.DrawRegion` supports drawing from its own `Pixels` buffer. Overlapping self-copies use a source snapshot so pixels are copied from the original region rather than from already-written output.
- Graphics2D handle validators check the runtime class and minimum object size. Passing the wrong object type to `Texture2D`, `TileSet2D`, `TileLayer2D`, `Viewport2D`, `Shader2D`, or `PostProcess2D` APIs returns safe defaults or no-ops instead of interpreting unrelated memory as that type.

## Render Targets, Textures, And Renderer

```rust
var target = RenderTarget2D.New(320, 180)
var spritePixels = Pixels.Load("assets/player.png")
var texture = Texture2D.New(spritePixels)

var renderer = Renderer2D.New(256)
renderer.Begin()
renderer.SetAlpha(255)
renderer.DrawTexture(texture, 32, 48)
renderer.DrawTextureScaled(texture, 96, 48, 64, 64)
renderer.DrawTextureRotatedAt(texture, 160, 48, 16, 16, 45.0)
renderer.FlushToTarget(target)

canvas.BlitAlpha(0, 0, target.Pixels)
```

`Renderer2D` keeps retained references to queued sources, so textures and pixels can be queued safely during a frame. Calling `Begin` clears the previous command list. `FlushToTarget(target)` draws to an offscreen target without ending the batch; `End(canvas)` draws to a Canvas with the queued blend modes, clears queued commands, and makes repeated `End` calls a no-op until the next `Begin`. `DrawTextureScaled(texture, x, y, width, height)` uses the texture's nearest or linear filter. `DrawTextureRegion(texture, x, y, sx, sy, width, height)` samples out-of-bounds source texels through the texture's clamp or repeat wrap mode. `DrawTextureRotated(texture, x, y, angleDegrees)` rotates around the texture center; `DrawTextureRotatedAt(texture, x, y, pivotX, pivotY, angleDegrees)` rotates around a source-local pivot. Additive `End(canvas)` clips correctly when a queued source is partially off-screen.

`SpriteRenderer2D` snapshots `Sampler2D` state when queuing a texture draw. It does not mutate the `Texture2D`; call `Sampler2D.ApplyToTexture(texture)` only when you explicitly want to change the texture's stored filter and wrap properties. Material and blend overrides are scoped to that draw call, so later direct `Renderer2D` draws keep the renderer state you set.

## Passes And Color Helpers

```rust
var palette = Palette2D.New()
palette.SetColor(3, 0xFF0000FF)
palette.SetColor(4, Color.RGBA(0, 0, 255, 128))
var recolored = palette.Apply(indexedPixels)
var legacyRecolored = palette.ApplyLegacy(oldAlphaByteIndexedPixels)

var gradient = Gradient2D.New(0x000000FF, Color.RGBA(255, 255, 255, 192), 16)
gradient.FillHorizontal(pixels)
```

`Palette2D.Apply` treats the source pixel red byte as the palette index and writes `0xRRGGBBAA` colors to a new buffer. Only palette entries set with `SetColor` are remapped; unset entries and out-of-range indices are copied unchanged. `ApplyLegacy` keeps the older `0x000000II` alpha-byte index convention for assets that used fully transparent pixels as palette indices.

`Palette2D.GetColor(index)` and `Gradient2D.Sample(t)` return values that work with `Color.GetR/G/B/A`, preserving alpha from raw pixel storage. `Palette2D.GetRGBA(index)` and `Gradient2D.SampleRGBA(t)` return raw `0xRRGGBBAA`.

`Gradient2D` uses `Steps <= 2` as smooth interpolation. Larger `Steps` values quantize into that many discrete levels, including both endpoints. For example, a three-step gradient samples start, midpoint, and end colors; horizontal and vertical fills use the same sampling as `SampleRGBA`.

## Video Playback

### Viper.Graphics.VideoPlayer

Software video decoder that exposes each decoded frame as a `Pixels` object for display on a `Canvas` or as a `Texture2D`. Suitable for cutscenes, loading screens, and in-game video surfaces.

**Type:** Instance (obj)
**Constructor:** `VideoPlayer.Open(path)`

#### Properties

| Property    | Type    | Access | Description |
|-------------|---------|--------|-------------|
| `Width`     | Integer | Read   | Frame width in pixels |
| `Height`    | Integer | Read   | Frame height in pixels |
| `Duration`  | Double  | Read   | Total video duration in seconds |
| `Position`  | Double  | Read   | Current playback position in seconds |
| `IsPlaying` | Integer | Read   | Non-zero while the video is playing |
| `Frame`     | Object  | Read   | The most recently decoded frame as `Pixels` |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Play()` | `Void()` | Start or resume playback |
| `Pause()` | `Void()` | Pause at the current frame |
| `Stop()` | `Void()` | Stop and reset to the start |
| `Seek(time)` | `Void(Double)` | Seek to a position in seconds |
| `Update(deltaSeconds)` | `Void(Double)` | Advance the decoder by the elapsed time |
| `SetVolume(volume)` | `Void(Double)` | Set playback volume `[0.0–1.0]` |

```rust
bind Viper.Graphics.VideoPlayer as VideoPlayer;

var video = VideoPlayer.Open("assets/intro.ogv")
video.SetVolume(0.8)
video.Play()

// per frame
video.Update(deltaSeconds)
canvas.Blit(0, 0, video.Frame)
```

```basic
DIM video AS Viper.Graphics.VideoPlayer
video = Viper.Graphics.VideoPlayer.Open("assets/intro.ogv")
video.Play()

' per frame
video.Update(deltaSeconds)
canvas.Blit(0, 0, video.Frame)
```

`VideoPlayer.Frame` returns the same `Pixels` object each frame until the video ends; copy if you need to retain a specific frame. Calling `Update` more than once per logical frame skips decoding for the excess calls.

---

## Notes

- `GpuTexture2D` is a compatibility alias today. It intentionally exposes the same behavior as `Texture2D` until a GPU-backed renderer is available.
- `PostProcess2D` has its own runtime class. It is accepted wherever a render pass expects an effect, but it is not treated as a `Shader2D` by direct shader APIs.
- `RenderPass2D` requires `RenderTarget2D` source and target objects. Invalid source or target handles make the pass a no-op instead of interpreting the handle as a render target. `SetShader` accepts either a `Shader2D` or `PostProcess2D`; other handles clear the pass effect.
- `VideoPlayer` decodes in software on the calling thread. For best results call `Update` once per frame with the actual elapsed seconds rather than a fixed timestep.
