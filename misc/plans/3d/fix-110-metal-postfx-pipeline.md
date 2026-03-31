# Fix 110: Metal PostFX Pipeline — Dead Code / Missing Vtable Entry

## Severity: P1 — High

## Problem

The Metal backend has PostFX shader code (MSL for bloom, FXAA, tonemap, vignette, color
grading at lines 617-685) and creates a `postfxPipeline` (line 699), but:

1. The pipeline is never used — no `submit_postfx` function exists in the vtable
2. The vtable (lines 1552-1566) has no post-processing entry
3. D3D11 has a full PostFX pipeline with DOF, SSAO, and motion blur
4. OpenGL has PostFX but missing DOF

This means Metal renders have no post-processing effects despite the shader code existing.

## Fix

### Option A: Wire Existing Shader (Recommended, ~100 LOC)

1. Add `metal_submit_postfx()` function that:
   - Creates an offscreen render target from the current frame
   - Renders a fullscreen quad with the PostFX shader
   - Applies bloom, FXAA, tonemap, vignette, color grading
   - Copies result back to the main framebuffer

2. Add the function to the Metal vtable:
   ```objc
   backend.submit_postfx = metal_submit_postfx;
   ```

3. The existing MSL shader code handles the effects — just needs to be called.

### Option B: Implement Missing Effects Too (~250 LOC)

In addition to wiring the existing pipeline, implement the effects that Metal is missing
compared to D3D11:
- **DOF (Depth of Field):** Circle-of-confusion blur based on depth
- **SSAO:** Screen-space ambient occlusion with noise sampling
- **Motion Blur:** Velocity-based blur using previous frame data

These require additional MSL shader code and render target management.

## Backend PostFX Parity After Fix

| Effect | Metal (before) | Metal (after A) | Metal (after B) | D3D11 | OpenGL |
|--------|---------------|-----------------|-----------------|-------|--------|
| Bloom | Dead code | Working | Working | Yes | Yes |
| FXAA | Dead code | Working | Working | Yes | Yes |
| Tonemap | Dead code | Working | Working | Yes | Yes |
| Vignette | Dead code | Working | Working | Yes | Yes |
| Color Grade | Dead code | Working | Working | Yes | Yes |
| DOF | Missing | Missing | Working | Yes | Missing |
| SSAO | Missing | Missing | Working | Yes | Yes |
| Motion Blur | Missing | Missing | Working | Yes | Yes |

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/vgfx3d_backend_metal.m` | Add `metal_submit_postfx()`, wire to vtable (~100-250 LOC) |

## Test

- Enable PostFX on a Metal-rendered scene
- Verify bloom threshold produces glow on bright areas
- Verify FXAA smooths edges
- Compare visual output with D3D11 PostFX for parity
