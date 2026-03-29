# D3D-11: GPU Post-Processing Pipeline

## Status

Implemented in the D3D11 offscreen scene path and `present_postfx` fullscreen composite.

## Shipped

- Canvas3D hands presentation to the backend through `present_postfx`
- D3D11 preserves offscreen scene color for the fullscreen composite instead of presenting the scene directly
- the fullscreen pass now owns bloom, tone mapping, FXAA, color grade, and vignette for the onscreen path
- `RenderTarget3D` readback remains scene-color output rather than an implicit postfx composite

## Notes

- the D3D11 postfx constant buffer now carries pass-through defaults for fields such as exposure, contrast, saturation, and vignette radius so partially populated snapshots do not black out the frame
- D3D-20 extends this same pipeline with depth and history inputs rather than replacing it

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
- [`misc/plans/3d/backends/d3d-20-advanced-postfx.md`](/Users/stephen/git/viper/misc/plans/3d/backends/d3d-20-advanced-postfx.md)
