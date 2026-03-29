# D3D-12: Persistent Dynamic VB/IB

## Status

Implemented in the D3D11 backend buffer upload path.

## Shipped

- regular mesh draws reuse persistent dynamic vertex and index buffers
- oversized uploads grow those buffers instead of recreating per-draw resources repeatedly
- the same dynamic-buffer helpers now support regular draws, instanced draws, and the shadow pass

## Notes

- the buffer strategy now extends beyond VB and IB to the instance stream and morph payload buffers, which keeps later DX11 plans from stacking more per-draw allocations onto the renderer

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
