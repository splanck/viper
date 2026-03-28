# OGL-13: GPU Post-Processing Pipeline

## Context
Same as MTL-11 and D3D-11. Render to offscreen FBO, then draw fullscreen quad with post-process shader.

As with Metal/D3D, the backend should not inspect private `rt_postfx3d.c` internals directly. Export a compact backend-facing snapshot/helper API from [`src/runtime/graphics/rt_postfx3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.c) and [`src/runtime/graphics/rt_postfx3d.h`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.h) first.

## Implementation

### Offscreen FBO (reuse from OGL-10 infrastructure)
When post-processing enabled, render scene to offscreen FBO instead of default framebuffer.

### Fullscreen quad shader
```glsl
// Vertex shader — no VBO needed, use gl_VertexID
#version 330 core
out vec2 vUV;
void main() {
    vec2 pos[4] = vec2[](vec2(-1,-1), vec2(1,-1), vec2(-1,1), vec2(1,1));
    vec2 uv[4] = vec2[](vec2(0,1), vec2(1,1), vec2(0,0), vec2(1,0));
    gl_Position = vec4(pos[gl_VertexID], 0, 1);
    vUV = uv[gl_VertexID];
}

// Fragment shader
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uSceneColor;
uniform int uBloomEnabled;
uniform float uBloomThreshold, uBloomStrength;
uniform int uTonemapMode;
uniform float uTonemapExposure;
uniform int uColorGradeEnabled;
uniform vec3 uColorMult;
uniform int uVignetteEnabled;
uniform float uVignetteIntensity, uVignetteRadius;
uniform int uFxaaEnabled;

void main() {
    vec4 color = texture(uSceneColor, vUV);

    if (uBloomEnabled != 0) {
        float brightness = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        if (brightness > uBloomThreshold)
            color.rgb += (color.rgb - uBloomThreshold) * uBloomStrength;
    }
    if (uTonemapMode == 1) color.rgb = color.rgb / (color.rgb + 1.0);
    if (uTonemapMode == 2) {
        vec3 x = color.rgb * uTonemapExposure;
        color.rgb = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
    }
    if (uColorGradeEnabled != 0) color.rgb *= uColorMult;
    if (uVignetteEnabled != 0) {
        float dist = length(vUV - 0.5);
        float vig = 1.0 - smoothstep(uVignetteRadius, uVignetteRadius + 0.2, dist);
        color.rgb *= mix(1.0, vig, uVignetteIntensity);
    }
    FragColor = color;
}
```

### Compile + link second shader program in create_ctx

**Important:** GL 3.3 core profile requires a bound VAO for all draw calls, even when using `gl_VertexID` without vertex attributes. Create an empty VAO for the post-process draw:
```c
gl.GenVertexArrays(1, &ctx->postfx_vao); // empty — no attributes enabled
```

### Post-process pass in end_frame
```c
if (ctx->postfx_enabled) {
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0); // back to screen
    gl.UseProgram(ctx->postfx_program);
    gl.BindVertexArray(ctx->postfx_vao); // empty VAO required by core profile
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, ctx->postfx_color_tex);
    gl.Uniform1i(ctx->postfx_uSceneColor, 0);
    // Set all postfx uniforms...
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4); // fullscreen quad via gl_VertexID
    gl.UseProgram(ctx->program); // restore main program
}
```

## Depends On
- OGL-10 (FBO infrastructure)

## Scope Note
v1 should explicitly target bloom, tone mapping, FXAA, color grade, and vignette. Depth/history-driven effects such as SSAO, DOF, and motion blur should stay deferred unless this plan is expanded to include those resources.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — postfx FBO, second shader program, fullscreen quad draw, end_frame post-pass
- `src/runtime/graphics/rt_postfx3d.c` — export backend-facing PostFX snapshot/helper
- `src/runtime/graphics/rt_postfx3d.h` — declare backend-facing PostFX snapshot/helper

## Testing
- Same tests as MTL-11 and D3D-11
