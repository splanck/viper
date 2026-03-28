# OGL-12: Shadow Mapping

## Context
Same as SW-05, MTL-12, D3D-14. OpenGL implementation uses FBO with depth-only attachment for shadow pass, then samples depth texture in main pass.

Shared constraint: Canvas3D owns the deferred queue, so shadow mapping needs a scheduling change in [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c) in addition to backend work. Do not try to make `submit_draw()` secretly manage a complete prepass on its own.

## Implementation

### Shadow depth FBO
```c
GLuint shadow_fbo, shadow_depth_tex;
gl.GenFramebuffers(1, &shadow_fbo);
gl.BindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);

gl.GenTextures(1, &shadow_depth_tex);
gl.BindTexture(GL_TEXTURE_2D, shadow_depth_tex);
gl.TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
              resolution, resolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
float borderColor[] = {1,1,1,1};
gl.TexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_depth_tex, 0);
gl.DrawBuffer(GL_NONE); // no color attachment
gl.ReadBuffer(GL_NONE);
```

### Shadow pass
Separate shader program (depth-only VS, no FS) or minimal FS. Render all opaque geometry with light VP matrix.

### GLSL shadow comparison in main fragment
```glsl
uniform sampler2DShadow uShadowMap;
uniform mat4 uShadowVP;
uniform float uShadowBias;
uniform int uShadowEnabled;

// In fragment:
if (uShadowEnabled != 0) {
    vec4 lightClip = uShadowVP * vec4(vWorldPos, 1.0);
    vec3 shadowUV = lightClip.xyz / lightClip.w;
    shadowUV = shadowUV * 0.5 + 0.5;
    float shadow = texture(uShadowMap, vec3(shadowUV.xy, shadowUV.z - uShadowBias));
    atten *= mix(0.15, 1.0, shadow);
}
```

`sampler2DShadow` + `GL_COMPARE_REF_TO_TEXTURE` gives hardware PCF on supporting drivers.

## Depends On
- OGL-10 (FBO infrastructure)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — shadow FBO, shadow pass, GLSL shadow sampler, comparison
- `src/runtime/graphics/rt_canvas3d.c` — shared shadow-pass scheduling before normal opaque replay

## Testing
- Same tests as SW-05, MTL-12, D3D-14
