# D3D-12: Persistent Dynamic VB/IB

## Current State

The D3D11 backend creates and releases a vertex buffer and index buffer for every draw. That is needlessly expensive and makes later features pile onto an already wasteful path.

## Implementation

Add persistent dynamic buffers to `d3d11_context_t`:

```c
ID3D11Buffer *dynamic_vb;
ID3D11Buffer *dynamic_ib;
size_t dynamic_vb_size;
size_t dynamic_ib_size;
```

Recommended initial sizes:

- VB: 4 MiB
- IB: 1 MiB

## Upload Strategy

For meshes that fit:

1. map the persistent buffer with `D3D11_MAP_WRITE_DISCARD`
2. copy the new data
3. unmap
4. bind the persistent buffers

For oversized meshes:

- either grow the persistent buffers, or
- fall back to temporary per-draw buffers

The earlier plan left the oversize behavior ambiguous. This needs to be explicit.

Recommended behavior:

- grow the persistent buffers to the largest seen size and keep them

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- regular draws no longer create/release VB/IB objects per draw
- output is unchanged
- oversized meshes still render correctly
