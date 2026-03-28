# OGL-16: Per-Pixel Terrain Splatting

## Context
Same as MTL-14, D3D-16, SW-07. Part of Plan 15. OpenGL needs splat map + 4 layer samplers on texture units 5-9.

Producer-side integration belongs in [`src/runtime/graphics/rt_terrain3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_terrain3d.c). Keep the baked terrain-texture fallback until the shared splat payload is wired and the intended backends consume it.

## Depends On
- OGL-03 (texture infrastructure)
- OGL-04 (texture cache)
- Plan 15 Phase 1 (splat fields in draw command)

## Implementation

### GLSL declarations
```glsl
uniform sampler2D uSplatTex;
uniform sampler2D uSplatLayer0;
uniform sampler2D uSplatLayer1;
uniform sampler2D uSplatLayer2;
uniform sampler2D uSplatLayer3;
uniform int uHasSplat;
uniform vec4 uSplatScales;
```

### Fragment shader splat blending
```glsl
if (uHasSplat != 0) {
    vec4 sp = texture(uSplatTex, vUV);
    float wsum = sp.r + sp.g + sp.b + sp.a;
    if (wsum > 0.001) sp /= wsum;
    vec3 blended = vec3(0);
    if (sp.r > 0.001) blended += texture(uSplatLayer0, vUV * uSplatScales.x).rgb * sp.r;
    if (sp.g > 0.001) blended += texture(uSplatLayer1, vUV * uSplatScales.y).rgb * sp.g;
    if (sp.b > 0.001) blended += texture(uSplatLayer2, vUV * uSplatScales.z).rgb * sp.b;
    if (sp.a > 0.001) blended += texture(uSplatLayer3, vUV * uSplatScales.w).rgb * sp.a;
    baseColor = blended * uDiffuseColor.rgb;
}
```

### C-side texture binding
```c
if (cmd->has_splat && cmd->splat_map) {
    GLuint splatTex = get_or_create_texture(ctx, cmd->splat_map);
    gl.ActiveTexture(GL_TEXTURE5);
    gl.BindTexture(GL_TEXTURE_2D, splatTex);
    gl.Uniform1i(ctx->uSplatTex, 5);

    for (int i = 0; i < 4; i++) {
        if (cmd->splat_layers[i]) {
            GLuint layerTex = get_or_create_texture(ctx, cmd->splat_layers[i]);
            gl.ActiveTexture(GL_TEXTURE6 + i);
            gl.BindTexture(GL_TEXTURE_2D, layerTex);
            gl.Uniform1i(ctx->uSplatLayer[i], 6 + i);
        }
    }
    gl.Uniform1i(ctx->uHasSplat, 1);
    gl.Uniform4f(ctx->uSplatScales,
                 cmd->splat_layer_scales[0], cmd->splat_layer_scales[1],
                 cmd->splat_layer_scales[2], cmd->splat_layer_scales[3]);
    gl.ActiveTexture(GL_TEXTURE0); // restore
} else {
    gl.Uniform1i(ctx->uHasSplat, 0);
}
```

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — GLSL splat samplers, fragment blending, C texture unit binding
- `src/runtime/graphics/rt_terrain3d.c` — populate shared splat payload while preserving fallback path

## Testing
- Same tests as MTL-14, D3D-16, SW-07
