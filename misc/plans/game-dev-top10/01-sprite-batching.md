# Feature 1: 2D Texture Atlas + SpriteBatch Enhancements

## Current Reality

`Viper.Graphics.SpriteBatch` already exists:
- `SpriteBatch.New(capacity)`
- `Begin()`
- `Draw(...)`, `DrawScaled(...)`, `DrawEx(...)`
- `DrawPixels(...)`, `DrawRegion(...)`
- `SetSortByDepth(...)`, `SetTint(...)`, `SetAlpha(...)`, `ResetSettings()`
- `End(canvas)`

So the missing feature is not batching itself. The missing pieces are:
- a 2D texture-atlas class with named regions
- a convenient atlas-to-batch workflow
- optional batch-level blend-mode support if it can be added without regressing the current software path

## Problem

Games can already batch sprites, but content pipelines still require manual region
coordinates. There is no 2D equivalent of a named atlas workflow, and there is no
descriptor-driven path for tools like Aseprite or JSON atlas exporters.

## Corrected Scope

### New Class

`Viper.Graphics.TextureAtlas`

```text
TextureAtlas.New(pixels) -> TextureAtlas
TextureAtlas.LoadGrid(pixels, frameW, frameH) -> TextureAtlas
TextureAtlas.Add(name, x, y, w, h)
TextureAtlas.Has(name) -> Boolean
TextureAtlas.GetX(name) -> Integer
TextureAtlas.GetY(name) -> Integer
TextureAtlas.GetWidth(name) -> Integer
TextureAtlas.GetHeight(name) -> Integer
TextureAtlas.GetPixels() -> Pixels
```

### Extensions to Existing SpriteBatch

```text
batch.DrawAtlas(atlas, name, x, y)
batch.DrawAtlasScaled(atlas, name, x, y, scale)
batch.DrawAtlasEx(atlas, name, x, y, scale, rotation, depth)
```

Optional phase 2 only:

```text
batch.SetBlendMode(mode)   // Alpha first; additive/multiply only if validated
```

## Implementation

### Phase 1: 2D TextureAtlas (1-2 days)

- Add `src/runtime/graphics/rt_texatlas.c` + `rt_texatlas.h`
- Model storage after the existing `TextureAtlas3D` naming/style, but back it with a retained `Pixels`
- Store named regions in a compact array or small map
- Support:
  - grid slicing for sprite sheets
- Keep v1 scope to rectangular named regions only

### Phase 2: Extend existing SpriteBatch (1-2 days)

- Modify `src/runtime/graphics/rt_spritebatch.c` + `rt_spritebatch.h`
- Add atlas-specific draw helpers that resolve a region and internally route to the existing region draw path
- Do not replace the current batch implementation
- Keep `SpriteBatch.New(capacity)` and `End(canvas)` unchanged

### Phase 3: Blend modes if safe (1-2 days)

- Add blend-mode state only after validating the current software raster path
- Start with `Alpha` and `Additive`
- Treat `Multiply` and `Screen` as follow-up work, not guaranteed v1

## Viper-Specific Notes

- `SpriteBatch` already lives in:
  - `src/runtime/graphics/rt_spritebatch.c`
  - `src/runtime/graphics/rt_spritebatch.h`
  - `src/il/runtime/runtime.def`
- Region blits should use the existing canvas/drawing helpers behind `DrawRegion`, not invent a second region renderer
- `Canvas.Screenshot()` already returns a `Pixels` object and is useful for validation
- `Pixels.Blur(radius)` already exists and can be used in atlas/debug examples, but it is not part of this feature

## Runtime Registration

Add a new `RT_CLASS_BEGIN("Viper.Graphics.TextureAtlas", ...)` block in `runtime.def`,
and add new `SpriteBatch` methods using the current `RT_FUNC` / `RT_CLASS_BEGIN`
style. Do not use legacy macro examples.

## Files

| File | Action |
|------|--------|
| `src/runtime/graphics/rt_texatlas.c` | New |
| `src/runtime/graphics/rt_texatlas.h` | New |
| `src/runtime/graphics/rt_spritebatch.c` | Modify |
| `src/runtime/graphics/rt_spritebatch.h` | Modify |
| `src/il/runtime/runtime.def` | Add `TextureAtlas`, extend `SpriteBatch` |
| `src/tests/runtime/RTSpriteBatchTests.cpp` | New or extend existing coverage |

## Documentation Updates

- Add atlas coverage to `docs/viperlib/graphics/scene.md` or split out a new atlas page if the scene doc gets too large
- Update `docs/viperlib/graphics/README.md`
- Update `docs/viperlib/README.md`
- Update `docs/codemap/graphics.md`

## Cross-Platform Requirements

- Pure software-side feature; no backend-specific code required
- Runtime wiring belongs in `src/runtime/CMakeLists.txt`
- Guard with `#ifdef VIPER_ENABLE_GRAPHICS` and provide stubs in `rt_graphics_stubs.c`
