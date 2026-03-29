# D3D-04: Spot Light Cone Attenuation

## Current State

The D3D11 light buffer handles directional and point lights, but spot lights still fall through the ambient branch.

## Shared State

No new shared runtime work is required:

- `vgfx3d_light_params_t` already carries `inner_cos` and `outer_cos`
- Canvas3D already fills those fields

## HLSL Changes

Extend the `Light` struct to include:

```hlsl
float inner_cos;
float outer_cos;
```

Then add a `type == 3` branch in the pixel shader:

- point-light style distance attenuation
- cone attenuation using `inner_cos` / `outer_cos`
- smooth interpolation between inner and outer cone

Use the same Hermite blend already used by the software and Metal paths.

## C-Side Changes

Replace the trailing padding in `d3d_light_t` with explicit `inner_cos` / `outer_cos` fields and copy those values into the light constant buffer.

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- spot lights brighten inside the inner cone
- fade smoothly to zero between the inner and outer cone
- contribute nothing outside the outer cone
