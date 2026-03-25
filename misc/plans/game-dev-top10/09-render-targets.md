# Feature 9: 2D Render Targets (Off-Screen Rendering)

## Current Reality

`Viper.Graphics3D.RenderTarget3D` already exists, but there is no equivalent 2D runtime
class. The current `Canvas` does not expose a direct framebuffer pointer that can simply
be swapped, so the original “just replace the active pixel pointer” design was not
compatible with the actual runtime.

## Problem

Without 2D off-screen rendering, users cannot build common workflows like:
- minimaps
- layer compositing
- glow / blur passes
- reflections
- cached UI surfaces

## Corrected Scope

### New Class

`Viper.Graphics.RenderTarget`

```text
RenderTarget.New(width, height) -> RenderTarget
target.Width -> Integer
target.Height -> Integer
target.AsPixels() -> Pixels
target.Clear(color)
```

### Canvas Extensions

Use naming parallel to `RenderTarget3D`:

```text
canvas.SetRenderTarget(target)
canvas.ResetRenderTarget()
canvas.DrawRenderTarget(target, x, y)
canvas.DrawRenderTargetScaled(target, x, y, scale)
canvas.DrawRenderTargetAlpha(target, x, y, alpha)
```

Optional later phase:

```text
canvas.DrawRenderTargetBlend(target, x, y, blendMode)
```

## Implementation

### Phase 1: off-screen surface abstraction (2-3 days)

- Add `src/runtime/graphics/rt_rendertarget.c` + `rt_rendertarget.h`
- Back the render target with a retained `Pixels`
- Introduce a small internal draw-surface abstraction so 2D raster ops can target:
  - the window-backed canvas surface
  - a `RenderTarget` surface

That is the key correction. Do not pretend the current `Canvas` already owns a swappable
pixel pointer.

### Phase 2: canvas routing and blit helpers (1-2 days)

- Modify `rt_canvas.c`
- Route existing software drawing calls through the shared draw-surface path
- Add render-target blit helpers using existing pixel operations

### Phase 3: validation and examples (1-2 days)

- Add unit tests for off-screen draw + readback
- Add a minimap or glow example

## Viper-Specific Notes

- Prefer `AsPixels()` over `GetPixels()` for symmetry with `RenderTarget3D`
- `Pixels.Blur(radius)` already exists and is a natural post-process example once readback is available
- Nested render targets should be either unsupported in v1 or explicitly stack-based; do not leave that ambiguous

## Runtime Registration

- Add `Viper.Graphics.RenderTarget`
- Extend `Viper.Graphics.Canvas` with render-target methods

## Files

| File | Action |
|------|--------|
| `src/runtime/graphics/rt_rendertarget.c` | New |
| `src/runtime/graphics/rt_rendertarget.h` | New |
| `src/runtime/graphics/rt_canvas.c` | Modify |
| `src/il/runtime/runtime.def` | Add class and canvas methods |
| `src/tests/runtime/RTRenderTargetTests.cpp` | New |

## Documentation Updates

- Update `docs/viperlib/graphics/canvas.md`
- Update `docs/viperlib/graphics/README.md`
- Update `docs/viperlib/README.md`

## Cross-Platform Requirements

- CPU/software-side feature built on `Pixels` and the existing 2D draw path
- Guard with `#ifdef VIPER_ENABLE_GRAPHICS`
- Runtime wiring belongs in `src/runtime/CMakeLists.txt`
