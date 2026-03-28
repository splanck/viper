# OGL-03: Diffuse Texture Sampling

## Context
OpenGL backend has zero texture infrastructure — no glActiveTexture, glBindTexture, glTexParameteri, no sampler uniform. Every textured mesh renders as solid color on Linux GPU.

## Implementation

### Step 1: Add sampler uniform to GLSL fragment shader
```glsl
uniform sampler2D uDiffuseTex;
uniform int uHasTexture;
```

### Step 2: Sample texture in fragment shader
After computing `baseColor` from `uDiffuseColor`:
```glsl
float3 baseColor = uDiffuseColor.rgb;
float texAlpha = 1.0;
if (uHasTexture != 0) {
    vec4 texSample = texture(uDiffuseTex, vUV);
    baseColor *= texSample.rgb;
    texAlpha = texSample.a;
}
// Unlit path uses baseColor
if (uUnlit != 0) { FragColor = vec4(baseColor, uAlpha * texAlpha); return; }
// Lit path uses baseColor instead of uDiffuseColor.rgb throughout
```

### Step 3: Create GL texture from Pixels in submit_draw
```c
if (cmd->texture) {
    typedef struct { int64_t w, h; uint32_t *data; } px_view;
    const px_view *pv = (const px_view *)cmd->texture;
    if (pv->data && pv->w > 0 && pv->h > 0) {
        GLuint tex;
        gl.GenTextures(1, &tex);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, tex);

        // Convert RGBA (Viper) to RGBA (OpenGL) — channel order may differ
        // Viper pixel format: 0xRRGGBBAA
        // OpenGL expects: R at byte 0, G at byte 1, B at byte 2, A at byte 3
        // Need to swizzle from packed uint32 to byte-ordered RGBA
        size_t count = (size_t)(pv->w * pv->h);
        uint8_t *rgba = (uint8_t *)malloc(count * 4);
        for (size_t i = 0; i < count; i++) {
            uint32_t px = pv->data[i];
            rgba[i*4+0] = (uint8_t)((px >> 24) & 0xFF); // R
            rgba[i*4+1] = (uint8_t)((px >> 16) & 0xFF); // G
            rgba[i*4+2] = (uint8_t)((px >> 8) & 0xFF);  // B
            rgba[i*4+3] = (uint8_t)(px & 0xFF);          // A
        }

        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                      (GLsizei)pv->w, (GLsizei)pv->h, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        gl.Uniform1i(ctx->uDiffuseTex, 0); // texture unit 0
        gl.Uniform1i(ctx->uHasTexture, 1);

        free(rgba);
        // IMPORTANT: Do NOT delete texture here — it must remain bound through the draw call.
        // Store the texture name for deletion AFTER glDrawElements:
        ctx->pending_tex = tex;
    }
} else {
    gl.Uniform1i(ctx->uHasTexture, 0);
    ctx->pending_tex = 0;
}
```

After the `glDrawElements` call (line 729), clean up:
```c
if (ctx->pending_tex) {
    gl.DeleteTextures(1, &ctx->pending_tex);
    ctx->pending_tex = 0;
}
```

### Step 4: Load GL texture functions in gl_load_functions
Add to the function pointer loading:
```c
LOAD(GenTextures);
LOAD(DeleteTextures);
LOAD(BindTexture);
LOAD(TexImage2D);
LOAD(TexParameteri);
LOAD(ActiveTexture);
```

These are core GL 3.3 functions — no extension needed.

### Step 5: Get uniform locations in create_ctx
```c
ctx->uDiffuseTex = gl.GetUniformLocation(ctx->program, "uDiffuseTex");
ctx->uHasTexture = gl.GetUniformLocation(ctx->program, "uHasTexture");
```

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — GLSL sampler uniform, fragment texture sampling, GL texture creation + binding, function loading, uniform locations

## Testing
- Textured box → texture visible (was solid color)
- Untextured box → still renders with diffuse color
- Textured + lit → texture modulated by lighting
