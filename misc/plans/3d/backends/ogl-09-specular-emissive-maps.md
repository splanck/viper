# OGL-09: Specular + Emissive Map Sampling

## Context
Same gap as MTL-05/06 and D3D-08. Uniforms only, no texture maps.

## Implementation

### Specular map on texture unit 2
```glsl
uniform sampler2D uSpecularTex;
uniform int uHasSpecularMap;
// In fragment:
vec3 specColor = uSpecularColor.rgb;
float shine = uSpecularColor.w;
if (uHasSpecularMap != 0) {
    vec4 specSample = texture(uSpecularTex, vUV);
    specColor *= specSample.rgb;
}
```

Keep `shine` uniform in v1 unless the engine adopts one shared gloss-map convention for all backends.

C binding:
```c
if (cmd->specular_map) {
    GLuint specTex = get_or_create_texture(ctx, cmd->specular_map);
    gl.ActiveTexture(GL_TEXTURE2);
    gl.BindTexture(GL_TEXTURE_2D, specTex);
    gl.Uniform1i(ctx->uSpecularTex, 2);
    gl.Uniform1i(ctx->uHasSpecularMap, 1);
} else {
    gl.Uniform1i(ctx->uHasSpecularMap, 0); // must clear — stale value from prior draw would be wrong
}
```

### Emissive map on texture unit 3
```glsl
uniform sampler2D uEmissiveTex;
uniform int uHasEmissiveMap;
// In fragment:
vec3 emissive = uEmissiveColor;
if (uHasEmissiveMap != 0) {
    emissive *= texture(uEmissiveTex, vUV).rgb;
}
result += emissive;
```

C binding:
```c
if (cmd->emissive_map) {
    GLuint emisTex = get_or_create_texture(ctx, cmd->emissive_map);
    gl.ActiveTexture(GL_TEXTURE3);
    gl.BindTexture(GL_TEXTURE_2D, emisTex);
    gl.Uniform1i(ctx->uEmissiveTex, 3);
    gl.Uniform1i(ctx->uHasEmissiveMap, 1);
} else {
    gl.Uniform1i(ctx->uHasEmissiveMap, 0);
}
```

## Depends On
- OGL-03 (texture infrastructure)
- OGL-04 (texture cache)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — GLSL sampler uniforms, fragment sampling, C binding on units 2-3

## Testing
- Same tests as MTL-05/06 and D3D-08
