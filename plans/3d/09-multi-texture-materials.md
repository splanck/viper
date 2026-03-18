# Phase 9: Multi-Texture Materials

## Goal

Extend Material3D from a single texture slot to 4 texture channels: diffuse, normal map, specular map, and emissive map. Add tangent vectors to the vertex format to enable tangent-space normal mapping. Modify all shaders (MSL/HLSL/GLSL) and the software rasterizer to support multi-texture lighting.

## Dependencies

- Phase 1 complete (vertex format already includes `tangent[3]` field)
- Phase 2 complete (backend abstraction)

## Vertex Format

Uses the `tangent[3]` field already present in `vgfx3d_vertex_t` (80 bytes, defined in Phase 1). The `CalcTangents` method populates tangent values — no vertex struct change is needed.

The tangent vector, combined with the normal and the computed bitangent (`B = cross(N, T)`), forms the **TBN matrix** that transforms normal map samples from tangent space to world space.

## Extended Draw Command

```c
typedef struct {
    // ... existing fields from Phase 1 ...
    const uint8_t *texture;         // diffuse/albedo texture (slot 0)
    int32_t tex_width, tex_height;

    // NEW — additional texture slots
    const uint8_t *normal_map;      // tangent-space normal map (slot 1, or NULL)
    int32_t nmap_width, nmap_height;
    const uint8_t *specular_map;    // specular intensity R channel (slot 2, or NULL)
    int32_t smap_width, smap_height;
    const uint8_t *emissive_map;    // emissive color RGB (slot 3, or NULL)
    int32_t emap_width, emap_height;
    float emissive_color[3];        // emissive color multiplier (NEW)
    int8_t has_normal_map;
    int8_t has_specular_map;
    int8_t has_emissive_map;
} vgfx3d_draw_cmd_t;
```

## Modified Files

#### Library Level (`src/lib/graphics/src/`)

**`vgfx3d.h`**
- Add `float tangent[3]` to `vgfx3d_vertex_t`
- Add 3 new texture slots + dimensions + flags to `vgfx3d_draw_cmd_t`

**`vgfx3d_raster.c`** (~+200 LOC)
- Interpolate tangent across triangle (same barycentric as normal/UV)
- Compute bitangent: `B = cross(N, T)`
- Construct TBN matrix per fragment
- Sample normal map → transform from tangent space to world space via TBN
- Use perturbed normal for diffuse/specular lighting
- Sample specular map → modulate specular intensity
- Sample emissive map → add to final color (after lighting)

**`vgfx3d_vertex.c`**
- Transform tangent by model matrix (same as normal, no inverse-transpose needed for tangent)

#### Runtime Level (`src/runtime/graphics/`)

**`rt_material3d.h` / `rt_material3d.c`** (~+100 LOC)

Extended struct:
```c
typedef struct {
    void *vptr;
    double diffuse[4];
    double specular[3];
    double shininess;
    void *texture;         // Pixels (diffuse, slot 0)
    void *normal_map;      // Pixels (normal, slot 1) — NEW
    void *specular_map;    // Pixels (specular, slot 2) — NEW
    void *emissive_map;    // Pixels (emissive, slot 3) — NEW
    double emissive[3];    // emissive color multiplier — NEW
    int8_t unlit;
} rt_material3d;
```

New API:
```c
void rt_material3d_set_normal_map(void *obj, void *pixels);
void rt_material3d_set_specular_map(void *obj, void *pixels);
void rt_material3d_set_emissive_map(void *obj, void *pixels);
void rt_material3d_set_emissive_color(void *obj, double r, double g, double b);
```

**`rt_mesh.c`** (~+150 LOC)

New API:
```c
// Compute tangent vectors from UV gradients (Lengyel's method)
// For each triangle: compute edge vectors and UV deltas, solve for T
// Accumulate per-vertex, then normalize
void rt_mesh3d_calc_tangents(void *obj);
```

## Backend Interface Additions

Add to `vgfx3d_backend_t`:

```c
void (*bind_texture_slot)(vgfx3d_context_t *ctx, int slot, void *tex);
// Slots: 0=diffuse, 1=normal, 2=specular, 3=emissive
```

## Shader Changes

All three shader languages get the same logical changes:

**Vertex shader:**
```
// Add tangent input attribute
in float3 aTangent;

// Pass tangent to fragment (transform by model matrix)
out.tangent = (modelMatrix * float4(aTangent, 0.0)).xyz;
```

**Fragment shader:**
```
// Construct TBN matrix
float3 T = normalize(in.tangent);
float3 N = normalize(in.normal);
float3 B = cross(N, T);
mat3 TBN = mat3(T, B, N);

// Sample normal map (if bound), remap [0,1] → [-1,1]
if (hasNormalMap) {
    float3 mapNormal = normalMapSample.xyz * 2.0 - 1.0;
    N = normalize(TBN * mapNormal);
}

// ... existing Blinn-Phong lighting using perturbed N ...

// Specular map modulation (if bound)
if (hasSpecularMap) {
    specular *= specularMapSample.r;
}

// Emissive (if bound)
float3 emissive = emissiveColor;
if (hasEmissiveMap) {
    emissive *= emissiveMapSample.rgb;
}
result += emissive;
```

## runtime.def Additions

```c
RT_FUNC(Material3DSetNormalMap,    rt_material3d_set_normal_map,    "Viper.Graphics3D.Material3D.SetNormalMap",    "void(obj,obj)")
RT_FUNC(Material3DSetSpecularMap,  rt_material3d_set_specular_map,  "Viper.Graphics3D.Material3D.SetSpecularMap",  "void(obj,obj)")
RT_FUNC(Material3DSetEmissiveMap,  rt_material3d_set_emissive_map,  "Viper.Graphics3D.Material3D.SetEmissiveMap",  "void(obj,obj)")
RT_FUNC(Material3DSetEmissiveColor,rt_material3d_set_emissive_color,"Viper.Graphics3D.Material3D.SetEmissiveColor","void(obj,f64,f64,f64)")
RT_FUNC(Mesh3DCalcTangents,       rt_mesh3d_calc_tangents,         "Viper.Graphics3D.Mesh3D.CalcTangents",        "void(obj)")

// Add to Material3D class:
//   RT_METHOD("SetNormalMap", "void(obj)", Material3DSetNormalMap)
//   RT_METHOD("SetSpecularMap", "void(obj)", Material3DSetSpecularMap)
//   RT_METHOD("SetEmissiveMap", "void(obj)", Material3DSetEmissiveMap)
//   RT_METHOD("SetEmissiveColor", "void(f64,f64,f64)", Material3DSetEmissiveColor)

// Add to Mesh3D class:
//   RT_METHOD("CalcTangents", "void()", Mesh3DCalcTangents)
```

## Tangent Calculation Algorithm (Lengyel's Method)

For each triangle with vertices (v0, v1, v2) and UVs (uv0, uv1, uv2):

```c
float3 edge1 = v1.pos - v0.pos;
float3 edge2 = v2.pos - v0.pos;
float2 duv1 = uv1 - uv0;
float2 duv2 = uv2 - uv0;

float det = duv1.x * duv2.y - duv1.y * duv2.x;
if (fabsf(det) < 1e-8f) continue;  // degenerate UV
float inv_det = 1.0f / det;

float3 tangent = (edge1 * duv2.y - edge2 * duv1.y) * inv_det;

// Accumulate to all 3 vertices (area-weighted by default)
v0.tangent += tangent;
v1.tangent += tangent;
v2.tangent += tangent;
```

After all triangles: normalize each vertex's tangent and orthogonalize against normal via Gram-Schmidt: `T = normalize(T - N * dot(N, T))`.

## Stubs

```c
void rt_material3d_set_normal_map(void *obj, void *pixels) { (void)obj; (void)pixels; }
void rt_material3d_set_specular_map(void *obj, void *pixels) { (void)obj; (void)pixels; }
void rt_material3d_set_emissive_map(void *obj, void *pixels) { (void)obj; (void)pixels; }
void rt_material3d_set_emissive_color(void *obj, double r, double g, double b) {
    (void)obj; (void)r; (void)g; (void)b;
}
void rt_mesh3d_calc_tangents(void *obj) { (void)obj; }
```

## Tests (15)

| Test | Description |
|------|-------------|
| Normal map flat | Normal map all (0.5, 0.5, 1.0) = no perturbation, matches unlit normal |
| Normal map tilted | Known normal map → verify lighting direction shifts |
| Specular map modulation | Specular map R=1.0 → full specular; R=0.0 → no specular |
| Emissive glow | Emissive map adds color even with no lights |
| Emissive color multiplier | Emissive color (1,0,0) × white emissive map = red glow |
| Tangent calc (plane) | Flat plane with known UVs → tangent = (1,0,0) |
| Tangent calc (sphere) | Sphere → tangents orthogonal to normals |
| Gram-Schmidt orthogonalization | Verify dot(T, N) ≈ 0 after calc |
| Partial slots (normal only) | Only normal map bound, specular/emissive NULL |
| Partial slots (emissive only) | Only emissive bound |
| All 4 slots bound | Diffuse + normal + specular + emissive simultaneously |
| Degenerate UV tangent | Triangle with zero-area UV → tangent defaults to (1,0,0) |
| Software vs GPU parity | Same scene, compare normal-mapped output |
| No tangent data | CalcTangents not called → normal map ignored gracefully |
| Large texture | 2048×2048 normal map renders correctly |
