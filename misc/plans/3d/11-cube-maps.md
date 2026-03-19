# Phase 11: Cube Map Textures

## Goal

Support 6-face cube map textures for skybox rendering (environmental backdrop) and environment reflections on shiny surfaces.

## Dependencies

- Phase 9 complete (multi-texture material slots)
- Phase 2 complete (backend abstraction)

## Architecture

```
CubeMap3D.New(+X, -X, +Y, -Y, +Z, -Z)     ← 6 Pixels objects
  ↓
Canvas3D.SetSkybox(cubeMap)                  ← render at infinity
  ↓ (in Begin/End):
  1. Disable depth write
  2. Strip translation from view matrix (rotation only)
  3. Render inverted cube with cube map texture
  4. Re-enable depth write
  5. Render scene geometry normally

Material3D.SetEnvMap(cubeMap)                ← reflection on surface
Material3D.SetReflectivity(0.5)
  ↓ (in fragment shader):
  1. Compute reflection vector: R = reflect(-V, N)
  2. Sample cube map at R
  3. Mix with diffuse: final = lerp(diffuse, envSample, reflectivity)
```

## New Files

#### Runtime Level (`src/runtime/graphics/`)

**`rt_cubemap3d.h` / `rt_cubemap3d.c`** (~230 LOC)

```c
typedef struct {
    void *vptr;
    void *faces[6];        // Pixels objects: +X, -X, +Y, -Y, +Z, -Z
    void *gpu_handle;      // backend-specific cube map texture
    int64_t face_size;     // width = height per face (must be square)
} rt_cubemap3d;

// Construction — all 6 faces must be square and same dimensions
void *rt_cubemap3d_new(void *px, void *nx, void *py, void *ny, void *pz, void *nz);

// Canvas3D integration
void rt_canvas3d_set_skybox(void *canvas, void *cubemap);     // NULL = no skybox
void rt_canvas3d_clear_skybox(void *canvas);

// Material3D integration
void rt_material3d_set_env_map(void *obj, void *cubemap);
void rt_material3d_set_reflectivity(void *obj, double r);     // [0.0, 1.0]
double rt_material3d_get_reflectivity(void *obj);
```

## Software Cube Map Sampling

For the software rasterizer, cube map sampling given a 3D direction vector:

```c
static void cubemap_sample(const rt_cubemap3d *cm, float dx, float dy, float dz,
                           uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
    // Find dominant axis → select face
    float ax = fabsf(dx), ay = fabsf(dy), az = fabsf(dz);
    int face;
    float u, v;
    if (ax >= ay && ax >= az) {
        face = dx > 0 ? 0 : 1;  // +X or -X
        u = dx > 0 ? -dz : dz;
        v = -dy;
        float ma = ax;
        u = (u / ma + 1.0f) * 0.5f;
        v = (v / ma + 1.0f) * 0.5f;
    } else if (ay >= ax && ay >= az) {
        // ... +Y / -Y
    } else {
        // ... +Z / -Z
    }
    // Sample face texture at (u, v)
    // Bilinear filter from faces[face] Pixels data
}
```

## Skybox Rendering

The skybox is a unit cube rendered at infinity. In the vertex shader:

```
// Strip translation from view matrix (keep only rotation)
mat4 skyboxView = viewMatrix;
skyboxView[3][0] = 0;  // clear translation
skyboxView[3][1] = 0;
skyboxView[3][2] = 0;

// Vertex position IS the cube map sample direction
out.texCoord3D = vertexPosition;  // 3D direction

// Set depth to maximum (far plane) — always behind everything
out.position = (projectionMatrix * skyboxView * float4(vertexPosition, 1.0)).xyww;
// .xyww trick: sets z=w so after w-divide z=1.0 (far plane)

// Depth function: the .xyww trick yields z/w=1.0 (far plane).
// Default depth test LESS would fail if the depth clear value is 1.0.
// Must use LEQUAL depth test so skybox passes at z=1.0:
//   Software: z <= zbuf (not z < zbuf)
//   Metal:    MTLCompareFunctionLessEqual
//   D3D11:    D3D11_COMPARISON_LESS_EQUAL
//   OpenGL:   glDepthFunc(GL_LEQUAL)
// After skybox, restore LESS for scene geometry — or use LEQUAL throughout
// (equally correct and simpler to manage).
```

Software path: for each pixel in the framebuffer, compute the 3D direction by unprojecting through the rotation-only VP matrix, sample the cube map.

## Backend Interface Additions

Add to `vgfx3d_backend_t`:

```c
void *(*create_cubemap)(vgfx3d_context_t *ctx, const uint8_t *faces[6],
                        int32_t face_size);
void  (*destroy_cubemap)(vgfx3d_context_t *ctx, void *cm);
void  (*bind_cubemap)(vgfx3d_context_t *ctx, int slot, void *cm);
```

| Backend | Cube Map Texture Type | Sampler |
|---------|-----------------------|---------|
| Metal | `MTLTextureTypeCube`, `newTextureWithDescriptor:` with `textureType = .typeCube`, upload 6 slices | `samplerCube` in MSL |
| D3D11 | `D3D11_TEXTURE2D_DESC` with `ArraySize = 6`, `MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE`, create SRV with `ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE` | `TextureCube` in HLSL |
| OpenGL | `GL_TEXTURE_CUBE_MAP`, `glTexImage2D` for each of 6 `GL_TEXTURE_CUBE_MAP_POSITIVE_X` etc. | `samplerCube` in GLSL |

## Shader Changes (Fragment)

```
// Environment reflection (when envMap is bound)
float3 V = normalize(cameraPosition - worldPos);
float3 R = reflect(-V, N);
float3 envColor = textureCube(envMap, R).rgb;
float3 final = mix(litColor, envColor, reflectivity);
```

## runtime.def Additions

```c
RT_FUNC(CubeMap3DNew,              rt_cubemap3d_new,              "Viper.Graphics3D.CubeMap3D.New",              "obj(obj,obj,obj,obj,obj,obj)")
RT_FUNC(Canvas3DSetSkybox,         rt_canvas3d_set_skybox,        "Viper.Graphics3D.Canvas3D.SetSkybox",         "void(obj,obj)")
RT_FUNC(Canvas3DClearSkybox,       rt_canvas3d_clear_skybox,      "Viper.Graphics3D.Canvas3D.ClearSkybox",       "void(obj)")
RT_FUNC(Material3DSetEnvMap,       rt_material3d_set_env_map,     "Viper.Graphics3D.Material3D.SetEnvMap",       "void(obj,obj)")
RT_FUNC(Material3DSetReflectivity, rt_material3d_set_reflectivity,"Viper.Graphics3D.Material3D.set_Reflectivity","void(obj,f64)")
RT_FUNC(Material3DGetReflectivity, rt_material3d_get_reflectivity,"Viper.Graphics3D.Material3D.get_Reflectivity","f64(obj)")

RT_CLASS_BEGIN("Viper.Graphics3D.CubeMap3D", CubeMap3D, "obj", CubeMap3DNew)
RT_CLASS_END()

// Add to Canvas3D class:
//   RT_METHOD("SetSkybox", "void(obj)", Canvas3DSetSkybox)
//   RT_METHOD("ClearSkybox", "void()", Canvas3DClearSkybox)
// Add to Material3D class:
//   RT_METHOD("SetEnvMap", "void(obj)", Material3DSetEnvMap)
//   RT_PROP("Reflectivity", "f64", Material3DGetReflectivity, Material3DSetReflectivity)
```

## GC Finalizer

```c
static void rt_cubemap3d_finalize(void *obj) {
    rt_cubemap3d *cm = (rt_cubemap3d *)obj;
    // faces[] are GC-managed Pixels — do NOT free them
    // gpu_handle needs backend cleanup
    if (cm->gpu_handle) {
        // backend->destroy_cubemap(ctx, cm->gpu_handle);
        cm->gpu_handle = NULL;
    }
}
```

## Stubs

```c
void *rt_cubemap3d_new(void *px, void *nx, void *py, void *ny, void *pz, void *nz) {
    (void)px; (void)nx; (void)py; (void)ny; (void)pz; (void)nz;
    rt_trap("CubeMap3D.New: graphics support not compiled in");
    return NULL;
}
void rt_canvas3d_set_skybox(void *c, void *cm) { (void)c; (void)cm; }
void rt_canvas3d_clear_skybox(void *c) { (void)c; }
void rt_material3d_set_env_map(void *o, void *cm) { (void)o; (void)cm; }
void rt_material3d_set_reflectivity(void *o, double r) { (void)o; (void)r; }
double rt_material3d_get_reflectivity(void *o) { (void)o; return 0.0; }
```

## Tests (10)

| Test | Description |
|------|-------------|
| Create (6 faces) | 6 same-size square Pixels → valid CubeMap3D |
| Non-square rejection | Non-square face → trap |
| Mismatched sizes | Faces with different dimensions → trap |
| Skybox renders | SetSkybox → visible behind geometry, fills background |
| Skybox at infinity | Camera translation doesn't move skybox |
| Reflection on sphere | Shiny sphere reflects cube map faces correctly |
| Reflectivity 0.0 | No reflection visible (pure diffuse) |
| Reflectivity 1.0 | Pure mirror (only cube map color) |
| ClearSkybox | After clear, background returns to clear color |
| Software vs GPU parity | Same skybox scene, compare outputs |
