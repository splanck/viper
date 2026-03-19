# Phase D: Rendering Power

## Goal

Add instanced rendering and heightmap terrain — enabling large-scale scenes with thousands of objects and open-world terrain.

## Dependencies

- Backend vtable (`vgfx3d_backend.h:82-106`)
- Mesh3D vertex format (`vgfx3d_vertex_t`, 80 bytes, `rt_canvas3d_internal.h:32-41`)
- Material3D system
- Frustum culling (`vgfx3d_frustum.c:104` — `vgfx3d_frustum_test_aabb`)
- AABB transform (`vgfx3d_frustum.c:160` — `vgfx3d_transform_aabb` — ALREADY EXISTS)
- Canvas3D deferred draw queue (`rt_canvas3d.c:278-346`)

---

## D1. Instanced Rendering (~400 LOC)

### New Files

**`src/runtime/graphics/rt_instbatch3d.h`** (~20 LOC)
**`src/runtime/graphics/rt_instbatch3d.c`** (~380 LOC)

### Data Structure

```c
typedef struct {
    void *vptr;              /* GC dispatch */
    void *mesh;              /* shared Mesh3D (borrowed, not owned) */
    void *material;          /* shared Material3D (borrowed) */
    float *transforms;       /* N * 16 floats (row-major Mat4 per instance) */
    int32_t instance_count;
    int32_t instance_capacity;
} rt_instbatch3d;
```

### Backend Vtable Extension (`vgfx3d_backend.h`)

Add new optional function pointer:
```c
/* Instanced rendering (NULL = fall back to N individual submit_draw calls) */
void (*submit_instanced)(void *ctx, vgfx_window_t win,
                          const vgfx3d_draw_cmd_t *cmd,
                          const float *transforms, int32_t instance_count,
                          const vgfx3d_light_params_t *lights, int32_t light_count,
                          const float *ambient, int8_t wireframe, int8_t backface_cull);
```

### Software Backend (`vgfx3d_backend_sw.c`)

Fallback: loop over instances, call existing `sw_submit_draw` with each transform:
```c
static void sw_submit_instanced(void *ctx_ptr, vgfx_window_t win,
                                  const vgfx3d_draw_cmd_t *cmd,
                                  const float *transforms, int32_t count, ...) {
    for (int i = 0; i < count; i++) {
        vgfx3d_draw_cmd_t instance_cmd = *cmd;
        memcpy(instance_cmd.model_matrix, &transforms[i * 16], 16 * sizeof(float));
        sw_submit_draw(ctx_ptr, win, &instance_cmd, lights, light_count, ambient, wireframe, backface_cull);
    }
}
```

### Metal Backend (`vgfx3d_backend_metal.m`)

True GPU instancing using `drawIndexedPrimitives:instanceCount:`:

**Buffer index 2** is available (currently: buffer 0 = vertices, buffer 1 = per-object uniforms).

```objc
static void metal_submit_instanced(void *ctx_ptr, vgfx_window_t win,
                                     const vgfx3d_draw_cmd_t *cmd,
                                     const float *transforms, int32_t count, ...) {
    /* Create instance buffer with all model matrices */
    id<MTLBuffer> instanceBuf = [ctx.device newBufferWithBytes:transforms
        length:(NSUInteger)(count * 16 * sizeof(float))
        options:MTLResourceStorageModeShared];
    [ctx.encoder setVertexBuffer:instanceBuf offset:0 atIndex:2];

    /* ... set up vertex/index buffers, materials, lights as in submit_draw ... */

    /* Single draw call for all instances */
    [ctx.encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
        indexCount:cmd->index_count indexType:MTLIndexTypeUInt32
        indexBuffer:ib indexBufferOffset:0
        instanceCount:(NSUInteger)count];
}
```

**Metal vertex shader change:**
```metal
struct InstanceData {
    float4x4 modelMatrix;  // per-instance model matrix
};

vertex VertexOut vertex_main(
    VertexIn in [[stage_in]],
    constant PerObject &obj [[buffer(1)]],
    constant InstanceData *instances [[buffer(2)]],
    uint instanceID [[instance_id]]
) {
    float4x4 model = instances[instanceID].modelMatrix;
    float4 wp = model * float4(in.position, 1.0);
    out.position = obj.viewProjection * wp;
    /* ... normal matrix = transpose(model) for instances ... */
}
```

**Note:** When `submit_instanced` is NULL (D3D11, OpenGL not yet implemented), the Canvas3D draw function falls back to N individual `submit_draw` calls.

### Per-Instance Frustum Culling

In `rt_canvas3d_draw_instanced` (the Canvas3D-level function):
```c
void rt_canvas3d_draw_instanced(void *canvas, void *batch) {
    rt_instbatch3d *b = (rt_instbatch3d *)batch;
    rt_mesh3d *m = (rt_mesh3d *)b->mesh;

    /* Compute mesh AABB once */
    float aabb_min[3], aabb_max[3];
    vgfx3d_compute_mesh_aabb(m->vertices, m->vertex_count, sizeof(vgfx3d_vertex_t), aabb_min, aabb_max);

    /* Extract frustum from cached VP */
    vgfx3d_frustum_t frustum;
    vgfx3d_frustum_extract(&frustum, c->cached_vp);

    /* Filter visible instances */
    float *visible_transforms = malloc(b->instance_count * 16 * sizeof(float));
    int32_t visible_count = 0;
    for (int32_t i = 0; i < b->instance_count; i++) {
        float world_min[3], world_max[3];
        /* vgfx3d_transform_aabb ALREADY EXISTS in vgfx3d_frustum.c:160 */
        vgfx3d_transform_aabb(aabb_min, aabb_max, /* cast to double */ ...,
                               world_min, world_max);
        if (vgfx3d_frustum_test_aabb(&frustum, world_min, world_max) != 0) {
            memcpy(&visible_transforms[visible_count * 16], &b->transforms[i * 16], 64);
            visible_count++;
        }
    }

    /* Submit visible instances */
    if (visible_count > 0) {
        /* ... build draw cmd from batch mesh/material ... */
        backend->submit_instanced(ctx, win, &cmd, visible_transforms, visible_count, ...);
    }
    free(visible_transforms);
}
```

**Note:** `vgfx3d_transform_aabb` in `vgfx3d_frustum.c:160` takes `const double world_matrix[16]` but instance transforms are `float[16]`. Need a float variant or temporary double conversion.

### Public API

```c
void   *rt_instbatch3d_new(void *mesh, void *material);
void    rt_instbatch3d_add(void *batch, void *transform);      /* Mat4 */
void    rt_instbatch3d_remove(void *batch, int64_t index);
void    rt_instbatch3d_set(void *batch, int64_t index, void *transform);
void    rt_instbatch3d_clear(void *batch);
int64_t rt_instbatch3d_count(void *batch);
void    rt_canvas3d_draw_instanced(void *canvas, void *batch);
```

### Namespace: `Viper.Graphics3D.InstanceBatch3D`
### RuntimeClasses.hpp: add `RTCLS_InstanceBatch3D`
### runtime.def: 7 RT_FUNC + 1 RT_CLASS
### Stubs: 7 functions
### CMakeLists: add `graphics/rt_instbatch3d.c`

---

## D2. Terrain3D (~400 LOC)

### New Files

**`src/runtime/graphics/rt_terrain3d.h`** (~25 LOC)
**`src/runtime/graphics/rt_terrain3d.c`** (~375 LOC)

### Data Structure

```c
#define TERRAIN_CHUNK_SIZE 16  /* vertices per chunk edge */

typedef struct {
    void *vptr;
    float *heights;              /* width * depth floats */
    int32_t width, depth;        /* heightmap dimensions */
    double scale[3];             /* (x_spacing, y_scale, z_spacing) */
    void **chunk_meshes;         /* Mesh3D per chunk (lazy-built, freed in finalizer) */
    int32_t chunks_x, chunks_z;  /* chunk grid dimensions */
    void *material;              /* shared Material3D (borrowed) */
    int8_t dirty;                /* 1 = chunks need rebuild */
} rt_terrain3d;
```

### Heightmap Loading from Pixels

Access Pixels data via internal cast (same pattern as `rt_rendertarget3d.c:152-157`):
```c
void rt_terrain3d_set_heightmap(void *obj, void *pixels) {
    rt_terrain3d *t = (rt_terrain3d *)obj;

    /* Access Pixels internal data */
    typedef struct { int64_t w; int64_t h; uint32_t *data; } px_view;
    px_view *pv = (px_view *)pixels;
    if (!pv || !pv->data) return;

    /* Sample heights from red channel (0xRRGGBBAA format) */
    int32_t src_w = (int32_t)pv->w, src_h = (int32_t)pv->h;
    for (int32_t z = 0; z < t->depth; z++) {
        for (int32_t x = 0; x < t->width; x++) {
            /* Bilinear sample from source if sizes differ */
            int sx = x * src_w / t->width;
            int sz = z * src_h / t->depth;
            if (sx >= src_w) sx = src_w - 1;
            if (sz >= src_h) sz = src_h - 1;
            uint32_t pixel = pv->data[sz * src_w + sx]; /* 0xRRGGBBAA */
            float r = (float)((pixel >> 24) & 0xFF) / 255.0f;
            t->heights[z * t->width + x] = r;
        }
    }

    /* Invalidate all chunks */
    t->dirty = 1;
    for (int32_t i = 0; i < t->chunks_x * t->chunks_z; i++) {
        t->chunk_meshes[i] = NULL; /* GC will collect old meshes */
    }
}
```

### Chunk Mesh Generation

Each chunk is a `TERRAIN_CHUNK_SIZE × TERRAIN_CHUNK_SIZE` quad grid:
```c
static void *build_chunk(rt_terrain3d *t, int cx, int cz) {
    void *mesh = rt_mesh3d_new();
    int x0 = cx * TERRAIN_CHUNK_SIZE;
    int z0 = cz * TERRAIN_CHUNK_SIZE;

    /* Vertices: (CHUNK_SIZE+1)² per chunk */
    for (int dz = 0; dz <= TERRAIN_CHUNK_SIZE; dz++) {
        for (int dx = 0; dx <= TERRAIN_CHUNK_SIZE; dx++) {
            int ix = x0 + dx, iz = z0 + dz;
            if (ix >= t->width) ix = t->width - 1;
            if (iz >= t->depth) iz = t->depth - 1;

            float wx = (float)ix * (float)t->scale[0];
            float wy = t->heights[iz * t->width + ix] * (float)t->scale[1];
            float wz = (float)iz * (float)t->scale[2];

            /* Normal from central difference */
            float hL = (ix > 0) ? t->heights[iz*t->width + ix-1] : t->heights[iz*t->width + ix];
            float hR = (ix < t->width-1) ? t->heights[iz*t->width + ix+1] : t->heights[iz*t->width + ix];
            float hD = (iz > 0) ? t->heights[(iz-1)*t->width + ix] : t->heights[iz*t->width + ix];
            float hU = (iz < t->depth-1) ? t->heights[(iz+1)*t->width + ix] : t->heights[iz*t->width + ix];
            float nx = (hL - hR) * (float)t->scale[1];
            float nz = (hD - hU) * (float)t->scale[1];
            float ny = 2.0f * (float)t->scale[0]; /* approximate */
            float nlen = sqrtf(nx*nx + ny*ny + nz*nz);
            if (nlen > 1e-6f) { nx/=nlen; ny/=nlen; nz/=nlen; }

            float u = (float)ix / (float)t->width;
            float v = (float)iz / (float)t->depth;

            rt_mesh3d_add_vertex(mesh, wx, wy, wz, nx, ny, nz, u, v);
        }
    }

    /* Indices: 2 triangles per quad (CCW winding) */
    int row = TERRAIN_CHUNK_SIZE + 1;
    for (int dz = 0; dz < TERRAIN_CHUNK_SIZE; dz++) {
        for (int dx = 0; dx < TERRAIN_CHUNK_SIZE; dx++) {
            int base = dz * row + dx;
            rt_mesh3d_add_triangle(mesh, base, base + row, base + 1);
            rt_mesh3d_add_triangle(mesh, base + 1, base + row, base + row + 1);
        }
    }

    return mesh;
}
```

### Height/Normal Queries

```c
double rt_terrain3d_get_height_at(void *obj, double x, double z) {
    rt_terrain3d *t = (rt_terrain3d *)obj;
    /* Convert world coords to heightmap coords */
    double hx = x / t->scale[0], hz = z / t->scale[2];
    /* Bilinear interpolation */
    int ix = (int)hx, iz = (int)hz;
    float fx = (float)(hx - ix), fz = (float)(hz - iz);
    /* Clamp */
    if (ix < 0) { ix = 0; fx = 0; }
    if (iz < 0) { iz = 0; fz = 0; }
    if (ix >= t->width-1) { ix = t->width-2; fx = 1; }
    if (iz >= t->depth-1) { iz = t->depth-2; fz = 1; }
    float h00 = t->heights[iz * t->width + ix];
    float h10 = t->heights[iz * t->width + ix + 1];
    float h01 = t->heights[(iz+1) * t->width + ix];
    float h11 = t->heights[(iz+1) * t->width + ix + 1];
    float h = h00*(1-fx)*(1-fz) + h10*fx*(1-fz) + h01*(1-fx)*fz + h11*fx*fz;
    return (double)(h * (float)t->scale[1]);
}
```

### Drawing

```c
void rt_canvas3d_draw_terrain(void *canvas, void *terrain, void *camera) {
    rt_terrain3d *t = (rt_terrain3d *)terrain;

    /* Build frustum from cached VP */
    vgfx3d_frustum_t frustum;
    vgfx3d_frustum_extract(&frustum, c->cached_vp);

    for (int32_t cz = 0; cz < t->chunks_z; cz++) {
        for (int32_t cx = 0; cx < t->chunks_x; cx++) {
            /* Chunk AABB for frustum test */
            float cmin[3] = {cx*TERRAIN_CHUNK_SIZE*(float)t->scale[0], 0,
                              cz*TERRAIN_CHUNK_SIZE*(float)t->scale[2]};
            float cmax[3] = {(cx+1)*TERRAIN_CHUNK_SIZE*(float)t->scale[0],
                              (float)t->scale[1],
                              (cz+1)*TERRAIN_CHUNK_SIZE*(float)t->scale[2]};
            if (vgfx3d_frustum_test_aabb(&frustum, cmin, cmax) == 0) continue;

            /* Lazy chunk build */
            int idx = cz * t->chunks_x + cx;
            if (!t->chunk_meshes[idx])
                t->chunk_meshes[idx] = build_chunk(t, cx, cz);

            /* Draw with identity transform (vertices are in world space) */
            extern void *rt_mat4_identity(void);
            rt_canvas3d_draw_mesh(canvas, t->chunk_meshes[idx], rt_mat4_identity(), t->material);
        }
    }
}
```

### Public API

```c
void   *rt_terrain3d_new(int64_t width, int64_t depth);
void    rt_terrain3d_set_heightmap(void *terrain, void *pixels);
void    rt_terrain3d_set_material(void *terrain, void *material);
void    rt_terrain3d_set_scale(void *terrain, double sx, double sy, double sz);
double  rt_terrain3d_get_height_at(void *terrain, double x, double z);
void   *rt_terrain3d_get_normal_at(void *terrain, double x, double z);
void    rt_canvas3d_draw_terrain(void *canvas, void *terrain, void *camera);
```

### Namespace: `Viper.Graphics3D.Terrain3D`
### RuntimeClasses.hpp: add `RTCLS_Terrain3D`
### runtime.def: 7 RT_FUNC + 1 RT_CLASS
### Stubs: 7 functions
### CMakeLists: add `graphics/rt_terrain3d.c`

---

## Files Modified/Created Summary

| Action | File | Est. LOC |
|--------|------|----------|
| NEW | `src/runtime/graphics/rt_instbatch3d.h` | ~20 |
| NEW | `src/runtime/graphics/rt_instbatch3d.c` | ~380 |
| NEW | `src/runtime/graphics/rt_terrain3d.h` | ~25 |
| NEW | `src/runtime/graphics/rt_terrain3d.c` | ~375 |
| MOD | `src/runtime/graphics/vgfx3d_backend.h` | +5 (submit_instanced) |
| MOD | `src/runtime/graphics/vgfx3d_backend_sw.c` | +30 (sw_submit_instanced) |
| MOD | `src/runtime/graphics/vgfx3d_backend_metal.m` | +60 (instanced draw + shader) |
| MOD | `src/runtime/graphics/rt_canvas3d.c` | +40 (draw_instanced, draw_terrain) |
| MOD | `src/runtime/graphics/rt_canvas3d.h` | +5 |
| MOD | `src/runtime/graphics/rt_graphics_stubs.c` | +15 |
| MOD | `src/il/runtime/runtime.def` | +14 entries |
| MOD | `src/il/runtime/classes/RuntimeClasses.hpp` | +2 class IDs |
| MOD | `src/il/runtime/RuntimeSignatures.cpp` | +2 includes |
| MOD | `src/runtime/CMakeLists.txt` | +2 sources |
| NEW | `src/tests/unit/test_rt_instterrain.cpp` | ~150 |

---

## Tests

### Instancing Tests (5)
- Batch create: non-null, count=0
- Add transforms: count increases
- Remove: count decreases
- Clear: count=0
- Set: transform at index updated

### Terrain Tests (6)
- Flat heightmap: all heights=0
- Heightmap load: values match pixel data
- Height query: bilinear interpolation correct at non-integer coords
- Normal query: perpendicular to flat surface = (0,1,0)
- Scale: world coordinates match scale factors
- Chunk count: ceil(width/16) * ceil(depth/16)

## Verification

1. Build clean (zero warnings)
2. 1334+ ctest pass
3. Demo: forest with 500 instanced trees (single draw call on Metal)
4. Demo: heightmap terrain with player walking on surface (using GetHeightAt for Y position)
5. Frustum culling works for both instanced and terrain chunks
6. Native compilation verified
