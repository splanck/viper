# OGL-11: GPU Skeletal Skinning + Morph Targets

## Context
Same gap as MTL-09/10 and D3D-10. Bone inputs defined but unused. OpenGL uses UBOs or SSBOs for bone palette.

Producer-side integration belongs in [`src/runtime/graphics/rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c) and [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c). The backend should consume flattened draw-command payloads rather than reading private runtime objects.

## Implementation

### GPU Skinning via SSBO (Shader Storage Buffer)
OpenGL 4.3+ supports SSBOs. For GL 3.3 compatibility, use a Uniform Buffer Object (UBO) or texture buffer.

**UBO approach (GL 3.3 compatible):**
```glsl
// Max 128 bones × 64 bytes = 8KB (well within UBO limit)
uniform mat4 uBonePalette[128];
uniform int uHasSkinning;

// In vertex shader:
if (uHasSkinning != 0) {
    vec4 skinnedPos = vec4(0);
    vec3 skinnedNorm = vec3(0);
    for (int i = 0; i < 4; i++) {
        uint boneIdx = aBoneIdx[i];
        float weight = aBoneWt[i];
        if (weight > 0.001) {
            mat4 bm = uBonePalette[boneIdx];
            skinnedPos += bm * vec4(aPosition, 1.0) * weight;
            skinnedNorm += (bm * vec4(aNormal, 0.0)).xyz * weight;
        }
    }
    // Use skinnedPos/skinnedNorm instead of aPosition/aNormal
}
```

C-side upload:
```c
if (cmd->bone_palette && cmd->bone_count > 0) {
    GLint loc = gl.GetUniformLocation(ctx->program, "uBonePalette");
    gl.UniformMatrix4fv(loc, cmd->bone_count, GL_FALSE, cmd->bone_palette);
    gl.Uniform1i(ctx->uHasSkinning, 1);
}
```

Note: coordinate this with the existing row-major upload conventions already used elsewhere in the backend.

### GPU Morph Targets via Texture Buffer
For morph deltas, use a texture buffer (GL 3.1+):
```glsl
uniform samplerBuffer uMorphDeltas;
uniform int uMorphShapeCount;
uniform float uMorphWeights[16]; // max 16 shapes

// In vertex shader:
if (uMorphShapeCount > 0) {
    for (int s = 0; s < uMorphShapeCount; s++) {
        float w = uMorphWeights[s];
        if (w > 0.001) {
            int offset = s * vertexCount + gl_VertexID;
            vec3 delta = vec3(
                texelFetch(uMorphDeltas, offset * 3 + 0).r,
                texelFetch(uMorphDeltas, offset * 3 + 1).r,
                texelFetch(uMorphDeltas, offset * 3 + 2).r
            );
            pos.xyz += delta * w;
        }
    }
}
```

C-side: create buffer texture from morph delta float array:
```c
GLuint morphBuf, morphTex;
gl.GenBuffers(1, &morphBuf);
gl.BindBuffer(GL_TEXTURE_BUFFER, morphBuf);
gl.BufferData(GL_TEXTURE_BUFFER, size, cmd->morph_deltas, GL_DYNAMIC_DRAW);
gl.GenTextures(1, &morphTex);
gl.BindTexture(GL_TEXTURE_BUFFER, morphTex);
gl.TexBuffer(GL_TEXTURE_BUFFER, GL_R32F, morphBuf);
```

## Depends On
- OGL-03 (texture infrastructure)
- Shared draw command bone_palette/morph fields (from MTL-09/D3D-10)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — GLSL bone palette UBO, morph texture buffer, vertex shader skinning/morph, C upload
- `src/runtime/graphics/vgfx3d_backend.h` — bone_palette/morph fields in draw command (shared)
- `src/runtime/graphics/rt_skeleton3d.c` — GPU-skinning producer path + CPU fallback
- `src/runtime/graphics/rt_morphtarget3d.c` — GPU-morph producer path + CPU fallback

## Testing
- Same tests as MTL-09/10 and D3D-10
