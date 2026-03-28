# MTL-01: Fix Diffuse Texture Sampling in Lit Path

## Context
The Metal fragment shader only samples the diffuse texture in the `unlit` branch (line 161). Lit textured meshes render as flat solid color. This is the most critical Metal bug.

## Current Shader (broken)
```metal
if (material.unlit) {
    float3 c = material.diffuseColor.rgb;
    if (material.hasTexture) c *= diffuseTex.sample(texSampler, in.uv).rgb;
    return float4(c, material.alpha);
}
// Lit path — NO texture sampling, uses material.diffuseColor only
float3 result = scene.ambientColor.rgb * material.diffuseColor.rgb;
```

## Fix
Move texture sampling outside the unlit check, compute `baseColor` once:
```metal
float3 baseColor = material.diffuseColor.rgb;
float texAlpha = 1.0;
if (material.hasTexture) {
    float4 texSample = diffuseTex.sample(texSampler, in.uv);
    baseColor *= texSample.rgb;
    texAlpha = texSample.a;
}

if (material.unlit) {
    return float4(baseColor, material.alpha * texAlpha);
}

float3 N = normalize(in.normal);
float3 V = normalize(scene.cameraPosition.xyz - in.worldPos);
float3 result = scene.ambientColor.rgb * baseColor;
// ... use baseColor instead of material.diffuseColor.rgb throughout ...
```

Replace all `material.diffuseColor.rgb` in the lighting loop with `baseColor`.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — shader source string (lines 159-191)

## Testing
- Textured box with directional light → texture visible with lighting (not solid color)
- Textured box unlit → still works as before
- Untextured box → renders with diffuse color as before
