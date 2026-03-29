# D3D-05: Wireframe Mode + Backface Culling Toggle

## Current State

The D3D11 backend still ignores both `wireframe` and `backface_cull`.

## Implementation

Pre-create four rasterizer states in `create_ctx()`:

- solid + back-cull
- solid + no-cull
- wireframe + back-cull
- wireframe + no-cull

Important: preserve the existing winding convention:

- `FrontCounterClockwise = FALSE`

That matches the backend's current clip-to-screen winding behavior.

## Draw Path

In `submit_draw()`:

- choose the correct rasterizer state from the two booleans
- bind it with `RSSetState`

Release all four rasterizer states in `destroy_ctx()`.

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- wireframe mode renders lines
- disabling backface cull makes planes visible from both sides
- both toggles work in all combinations
