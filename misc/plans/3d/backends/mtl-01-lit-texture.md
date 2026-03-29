# MTL-01: Fix Diffuse Texture Sampling in Lit Path — ✅ DONE

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

Replace `material.diffuseColor.rgb` with `baseColor` in exactly 2 places in the lighting loop:
1. Ambient: `scene.ambientColor.rgb * baseColor` (was `* material.diffuseColor.rgb`)
2. Diffuse accumulation: `NdotL * baseColor * atten` (was `* material.diffuseColor.rgb * atten`)

Also change the lit path return to include texture alpha:
```metal
return float4(result, material.alpha * texAlpha);
```

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — fragment_main body in the embedded MSL string

## Testing
- Textured box with directional light → texture visible with lighting (not solid color)
- Textured box unlit → still works as before
- Untextured box → renders with diffuse color as before
