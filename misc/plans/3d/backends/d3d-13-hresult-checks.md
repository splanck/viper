# D3D-13: HRESULT Error Handling

## Status

Implemented as a consistent logging and failure-handling pattern across the D3D11 backend.

## Shipped

- resource creation and update calls are checked for `HRESULT` failure instead of assuming success
- shader compilation failures preserve and log the compiler error blob text
- draw-path updates bail out cleanly when constant-buffer or buffer-map operations fail
- later DX11 features such as RTT, shadows, skyboxes, cubemaps, and postfx use the same checked pattern

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
