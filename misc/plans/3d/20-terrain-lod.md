# Plan 20: Terrain LOD — Frustum Culling + Multi-Resolution Chunks

## Problem

Terrain3D draws ALL chunks every frame regardless of distance or visibility.
For a 512x512 terrain, that's 32x32 = 1024 chunks, each with a 16x16 quad
mesh (578 vertices, 512 triangles). Large terrains tank performance.

## Goal

Three phases:
- Phase A: Frustum culling (skip chunks outside camera view)
- Phase B: Distance-based LOD (coarser meshes at distance)
- Phase C: Crack stitching (hide seams between LOD levels)

## Zero External Dependencies

Frustum culling infrastructure already exists in `vgfx3d_frustum.h/c`
(Gribb-Hartmann plane extraction, AABB intersection). Canvas3D already
stores `cached_vp[16]` (the VP matrix). Only standard C math needed.

---

## Phase A: Frustum Culling (~100 LOC)

### Existing Infrastructure

- `vgfx3d_frustum_extract(frustum, vp)` — extracts 6 planes from VP matrix
- `vgfx3d_frustum_test_aabb(frustum, min, max)` — returns 0=outside, 1=intersecting, 2=inside
- `rt_canvas3d.cached_vp[16]` — VP matrix cached in `begin_frame`

### Implementation

#### Step 1: Store per-chunk AABB

Add to `rt_terrain3d` struct:
```c
float *chunk_aabbs;  // chunks_x * chunks_z * 6 floats (min[3] + max[3])
int8_t chunk_aabbs_valid;
```

Compute AABB when chunk is built (in `build_chunk`):
```c
// After building chunk vertices, compute AABB
float min[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
float max[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
// Iterate vertices: min/max of position components
// X range: [x0 * scale[0], xend * scale[0]]
// Y range: [min_height * scale[1], max_height * scale[1]]
// Z range: [z0 * scale[2], zend * scale[2]]
```

#### Step 2: Frustum test in draw loop

In `rt_canvas3d_draw_terrain()`, before drawing each chunk:
```c
// Extract frustum from cached VP matrix
vgfx3d_frustum_t frustum;
vgfx3d_frustum_extract(&frustum, c->cached_vp);

for (int32_t cz = 0; cz < t->chunks_z; cz++) {
    for (int32_t cx = 0; cx < t->chunks_x; cx++) {
        int32_t idx = cz * t->chunks_x + cx;

        // Frustum cull
        float *aabb = &t->chunk_aabbs[idx * 6];
        if (vgfx3d_frustum_test_aabb(&frustum, aabb, aabb + 3) == 0)
            continue;  // entirely outside — skip

        // Build and draw chunk as before
        ...
    }
}
```

### Performance Impact

For a typical FPS view with ~60-degree FOV, roughly 60-70% of chunks are
behind the camera or outside the view cone. This alone halves draw calls
on large terrains.

---

## Phase B: Multi-Resolution Chunks (~200 LOC)

### LOD Levels

```c
#define TERRAIN_LOD_LEVELS 3
// LOD 0: step=1  (16x16 quads = 578 vertices) — full detail
// LOD 1: step=2  (8x8 quads = 162 vertices)   — half detail
// LOD 2: step=4  (4x4 quads = 50 vertices)    — quarter detail
```

### Struct Changes

Add to `rt_terrain3d`:
```c
void **chunk_meshes_lod[TERRAIN_LOD_LEVELS]; // [lod][chunk_idx]
float lod_distances[TERRAIN_LOD_LEVELS];      // distance thresholds
```

Default LOD distances:
```c
lod_distances[0] = 0.0;     // LOD 0 from 0 to lod_distances[1]
lod_distances[1] = 200.0;   // LOD 1 from 200 to lod_distances[2]
lod_distances[2] = 500.0;   // LOD 2 beyond 500
```

### Zia API

```
Terrain3D.SetLODDistances(terrain, nearDist, farDist)
```

Two thresholds define 3 bands:
- `[0, nearDist)` = LOD 0 (full detail)
- `[nearDist, farDist)` = LOD 1 (half detail)
- `[farDist, ...)` = LOD 2 (quarter detail)

### Modified `build_chunk`

Accept a `step` parameter:
```c
static void *build_chunk(rt_terrain3d *t, int32_t cx, int32_t cz, int32_t step) {
    // ...
    for (int32_t dz = 0; dz <= rows; dz += step) {
        for (int32_t dx = 0; dx <= cols; dx += step) {
            // same vertex generation, but fewer vertices
        }
    }
    // Triangle generation uses (cols/step + 1) as row_verts
}
```

### LOD Selection in Draw Loop

```c
// Compute chunk center
float cx_world = (x0 + xend) * 0.5f * (float)t->scale[0];
float cz_world = (z0 + zend) * 0.5f * (float)t->scale[2];

// Distance to camera
float dx = cx_world - c->cached_cam_pos[0];
float dz = cz_world - c->cached_cam_pos[2];
float dist = sqrtf(dx*dx + dz*dz);

// Select LOD
int lod = 0;
if (dist >= t->lod_distances[2]) lod = 2;
else if (dist >= t->lod_distances[1]) lod = 1;

// Get or build mesh at this LOD
void *mesh = t->chunk_meshes_lod[lod][idx];
if (!mesh) {
    int step = 1 << lod;  // 1, 2, or 4
    mesh = build_chunk(t, cx, cz, step);
    t->chunk_meshes_lod[lod][idx] = mesh;
}
```

### Cache Management

When heightmap changes (`set_heightmap`, `set_scale`), invalidate ALL LOD
levels:
```c
for (int lod = 0; lod < TERRAIN_LOD_LEVELS; lod++)
    for (int32_t i = 0; i < t->chunks_x * t->chunks_z; i++)
        t->chunk_meshes_lod[lod][i] = NULL;
```

---

## Phase C: Crack Stitching (~100 LOC)

### The Problem

When adjacent chunks have different LOD levels, their shared edge has
different vertex counts, creating T-junctions that appear as visible cracks.

### Solution: Skirt Geometry

Add a downward-facing "skirt" along all four edges of each chunk. The skirt
extends below the terrain surface by a configurable depth, hiding gaps.

```c
static void add_chunk_skirt(void *mesh, rt_terrain3d *t,
                             int32_t x0, int32_t z0,
                             int32_t xend, int32_t zend,
                             int32_t step, float skirt_depth) {
    // For each edge (top, bottom, left, right):
    // For each vertex along the edge:
    //   Add a duplicate vertex at (same X, Y - skirt_depth, same Z)
    //   Add two triangles connecting original vertex to skirt vertex
}
```

### Zia API

```
Terrain3D.SetSkirtDepth(terrain, depth)
```

Default: `skirt_depth = 2.0` (relative to scale). Set to 0 to disable.

### Alternative Considered: Geomorphing

Geomorphing smoothly transitions vertices between LOD levels based on
distance, eliminating cracks entirely. However:
- Requires per-vertex LOD factor in the vertex shader
- Needs shader changes in all 4 backends
- Higher complexity for marginal visual improvement

Skirt geometry is simpler, requires no shader changes, and hides cracks
effectively in practice (widely used in commercial engines).

---

## Files Modified

| File | Change | Phase |
|------|--------|-------|
| `src/runtime/graphics/rt_terrain3d.c` | All three phases | A,B,C |
| `src/runtime/graphics/rt_terrain3d.h` | New function declarations | B,C |
| `src/runtime/graphics/rt_graphics_stubs.c` | Stubs for new API | B,C |
| `src/il/runtime/runtime.def` | Register SetLODDistances, SetSkirtDepth | B,C |

No new files. No shader changes. No backend modifications.

## LOC Estimate

Phase A: ~100 LOC (AABB computation + frustum test wiring)
Phase B: ~200 LOC (LOD mesh cache + distance selection + modified build_chunk)
Phase C: ~100 LOC (skirt geometry generation)
Total: ~400 LOC, all in `rt_terrain3d.c`

## Testing

1. Build: `./scripts/build_viper.sh`
2. `ctest --test-dir build --output-on-failure`
3. Write `examples/apiaudit/graphics3d/terrain_lod_demo.zia`:
   - Create 512x512 terrain with procedural heightmap
   - Enable wireframe to visually confirm LOD transitions
   - Move camera to verify frustum culling (chunks behind camera not drawn)
   - Verify no visible cracks at LOD boundaries
4. Performance comparison: FPS before/after with wireframe stats output

## Performance Expectations

For a 512x512 terrain (1024 chunks):
- **Before**: 1024 draw calls per frame, ~590K vertices
- **After Phase A**: ~350-400 draw calls (frustum culled)
- **After Phase B**: ~400 draw calls, ~150K vertices (LOD 1+2 reduce verts 4-16x)
- Combined: ~3-4x fewer vertices processed per frame
