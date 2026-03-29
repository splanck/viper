# OGL-03: Diffuse Texture Sampling

## Current State

The OpenGL backend has no texture pipeline. Every mesh renders with solid material color even when `cmd->texture` is populated.

## Scope

This plan establishes the reusable OpenGL texture path that later plans build on, and it also fixes the currently-missing GPU vertex-color modulation:

- diffuse texture sampling in GLSL
- vertex color contribution to base color / alpha
- `Pixels` to GL texture upload
- texture unit binding
- per-draw lifetime rules

Texture caching belongs to OGL-04. OGL-03 may still upload temporary textures per draw, but the upload helper should be structured so OGL-04 can reuse it directly.

## GLSL Changes

Add fragment uniforms:

```glsl
uniform sampler2D uDiffuseTex;
uniform int uHasTexture;
```

Restructure fragment color setup:

```glsl
vec3 baseColor = uDiffuseColor.rgb * vColor.rgb;
float texAlpha = 1.0;
float finalAlpha = uAlpha * vColor.a;
if (uHasTexture != 0) {
    vec4 texSample = texture(uDiffuseTex, vUV);
    baseColor *= texSample.rgb;
    texAlpha = texSample.a;
}
finalAlpha *= texAlpha;
```

Then:

- use `baseColor` for both unlit and lit paths
- use `finalAlpha` for the final alpha

This layout is intentionally compatible with OGL-08, OGL-09, and OGL-16.

## GL Upload Path

Add a helper that converts a `Pixels` object to a `GL_TEXTURE_2D`:

1. Treat the runtime object as:
   - `int64_t w`
   - `int64_t h`
   - `uint32_t *data`
2. Convert from packed `0xRRGGBBAA` to byte-addressed RGBA.
3. Create and bind a texture.
4. Upload with `glTexImage2D(..., GL_RGBA8, ..., GL_RGBA, GL_UNSIGNED_BYTE, ...)`.
5. Set:
   - `GL_TEXTURE_WRAP_S/T = GL_REPEAT`
   - `GL_TEXTURE_MIN_FILTER = GL_LINEAR`
   - `GL_TEXTURE_MAG_FILTER = GL_LINEAR`

Until OGL-04 lands, a temporary texture created for a draw should be deleted after `glDrawElements`.

## Function Loader Additions

Load the GL entry points needed for the reusable texture path:

- `GenTextures`
- `DeleteTextures`
- `BindTexture`
- `TexImage2D`
- `TexParameteri`
- `ActiveTexture`

Constants needed in the backend:

- `GL_REPEAT`
- `GL_LINEAR`
- `GL_TEXTURE_WRAP_S`
- `GL_TEXTURE_WRAP_T`
- `GL_TEXTURE_MIN_FILTER`
- `GL_TEXTURE_MAG_FILTER`

## Draw Path Rules

- Bind diffuse texture on unit `0`
- Set `uDiffuseTex = 0`
- Set `uHasTexture = 1` only when a valid texture was uploaded
- When no texture is present:
  - set `uHasTexture = 0`
  - bind texture `0` on unit `0` to avoid stale bindings from earlier draws

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- Textured meshes render with lighting
- Untextured meshes remain unchanged
- Vertex color visibly modulates the material color
- Transparent texels modulate final alpha correctly
