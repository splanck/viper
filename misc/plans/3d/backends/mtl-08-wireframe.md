# MTL-08: Wireframe Mode

## Context
The `wireframe` parameter is accepted by `metal_submit_draw()` but void-cast (line 534). Metal supports wireframe via `MTLTriangleFillModeLines`.

## Current Code
```objc
(void)wireframe;
```

## Fix
Replace the void cast with fill mode selection. Metal requires a separate render pipeline state with a different triangle fill mode, OR we can set it on the encoder directly:

```objc
// In metal_submit_draw, after setCullMode:
if (wireframe) {
    [ctx.encoder setTriangleFillMode:MTLTriangleFillModeLines];
} else {
    [ctx.encoder setTriangleFillMode:MTLTriangleFillModeFill];
}
```

That's it. Metal's `setTriangleFillMode` is a per-draw encoder setting — no pipeline state change needed.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — replace `(void)wireframe` with `setTriangleFillMode`

## Testing
- Set wireframe=true → mesh renders as wireframe edges only
- Set wireframe=false → normal filled rendering
- Toggle per-draw → some objects wireframe, others solid
