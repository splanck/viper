# Phase 13: Frustum Culling

## Goal

Skip rendering objects whose bounding volumes are entirely outside the camera's view frustum. This is library-internal logic integrated with the scene graph traversal (Phase 12), providing significant performance improvement for scenes with many objects.

## Dependencies

- Phase 12 complete (scene graph with per-node bounding volumes)
- Phase 1 complete (camera with view/projection matrices)

## Architecture

```
Scene3D.Draw(canvas, camera)
  ↓ per node:
  1. Compute world matrix (from Phase 12 dirty-flag system)
  2. Transform node's object-space AABB to world space
  3. Test world-space AABB against camera frustum (6 planes)
  4. If OUTSIDE → skip this node and all children
  5. If INSIDE or INTERSECTING → submit draw + recurse children
```

## New Files

#### Library Level (`src/lib/graphics/src/`)

**`vgfx3d_frustum.h`** (~30 LOC)

```c
typedef struct {
    float planes[6][4];  // [A, B, C, D] for Ax + By + Cz + D = 0 (normalized)
    // Order: 0=left, 1=right, 2=bottom, 3=top, 4=near, 5=far
} vgfx3d_frustum_t;

// Extract frustum planes from combined View*Projection matrix
void vgfx3d_frustum_extract(vgfx3d_frustum_t *f, const float vp[16]);

// Test AABB against frustum
// Returns: 0 = fully outside, 1 = intersecting, 2 = fully inside
int vgfx3d_frustum_test_aabb(const vgfx3d_frustum_t *f,
                              const float min[3], const float max[3]);

// Test bounding sphere against frustum
int vgfx3d_frustum_test_sphere(const vgfx3d_frustum_t *f,
                                const float center[3], float radius);
```

**`vgfx3d_frustum.c`** (~150 LOC)

## Frustum Plane Extraction (Gribb-Hartmann Method)

Extract 6 planes directly from the combined VP matrix. For a row-major VP matrix `m`:

```c
void vgfx3d_frustum_extract(vgfx3d_frustum_t *f, const float vp[16]) {
    // Left:   row3 + row0
    f->planes[0][0] = vp[12] + vp[0];
    f->planes[0][1] = vp[13] + vp[1];
    f->planes[0][2] = vp[14] + vp[2];
    f->planes[0][3] = vp[15] + vp[3];

    // Right:  row3 - row0
    f->planes[1][0] = vp[12] - vp[0];
    f->planes[1][1] = vp[13] - vp[1];
    f->planes[1][2] = vp[14] - vp[2];
    f->planes[1][3] = vp[15] - vp[3];

    // Bottom: row3 + row1
    f->planes[2][0] = vp[12] + vp[4];
    f->planes[2][1] = vp[13] + vp[5];
    f->planes[2][2] = vp[14] + vp[6];
    f->planes[2][3] = vp[15] + vp[7];

    // Top:    row3 - row1
    f->planes[3][0] = vp[12] - vp[4];
    f->planes[3][1] = vp[13] - vp[5];
    f->planes[3][2] = vp[14] - vp[6];
    f->planes[3][3] = vp[15] - vp[7];

    // Near:   row3 + row2
    f->planes[4][0] = vp[12] + vp[8];
    f->planes[4][1] = vp[13] + vp[9];
    f->planes[4][2] = vp[14] + vp[10];
    f->planes[4][3] = vp[15] + vp[11];

    // Far:    row3 - row2
    f->planes[5][0] = vp[12] - vp[8];
    f->planes[5][1] = vp[13] - vp[9];
    f->planes[5][2] = vp[14] - vp[10];
    f->planes[5][3] = vp[15] - vp[11];

    // Normalize each plane
    for (int i = 0; i < 6; i++) {
        float len = sqrtf(f->planes[i][0]*f->planes[i][0] +
                          f->planes[i][1]*f->planes[i][1] +
                          f->planes[i][2]*f->planes[i][2]);
        if (len > 1e-8f) {
            float inv = 1.0f / len;
            f->planes[i][0] *= inv;
            f->planes[i][1] *= inv;
            f->planes[i][2] *= inv;
            f->planes[i][3] *= inv;
        }
    }
}
```

## AABB Test

For each of the 6 planes, find the **p-vertex** (corner of AABB most along the plane normal) and **n-vertex** (corner least along the plane normal). If the p-vertex is behind the plane, the AABB is outside. If the n-vertex is in front of all planes, the AABB is fully inside.

```c
int vgfx3d_frustum_test_aabb(const vgfx3d_frustum_t *f,
                              const float min[3], const float max[3]) {
    int result = 2;  // assume fully inside
    for (int i = 0; i < 6; i++) {
        float px = f->planes[i][0] >= 0 ? max[0] : min[0];
        float py = f->planes[i][1] >= 0 ? max[1] : min[1];
        float pz = f->planes[i][2] >= 0 ? max[2] : min[2];
        float dist = f->planes[i][0]*px + f->planes[i][1]*py +
                     f->planes[i][2]*pz + f->planes[i][3];
        if (dist < 0) return 0;  // p-vertex behind plane → outside

        float nx = f->planes[i][0] >= 0 ? min[0] : max[0];
        float ny = f->planes[i][1] >= 0 ? min[1] : max[1];
        float nz = f->planes[i][2] >= 0 ? min[2] : max[2];
        float ndist = f->planes[i][0]*nx + f->planes[i][1]*ny +
                      f->planes[i][2]*nz + f->planes[i][3];
        if (ndist < 0) result = 1;  // n-vertex behind → intersecting
    }
    return result;
}
```

## Integration with Scene Graph (Phase 12)

Modify `draw_node()` in `rt_scene3d.c`:

```c
static void draw_node(rt_scene_node3d *node, void *canvas3d,
                       const vgfx3d_frustum_t *frustum) {
    if (!node->visible) return;
    recompute_world_matrix(node);

    int draw_self = 1;

    // Frustum cull this node's own mesh (if it has one)
    if (node->mesh) {
        float world_min[3], world_max[3];
        transform_aabb(node->aabb_min, node->aabb_max,
                       node->world_matrix, world_min, world_max);
        if (vgfx3d_frustum_test_aabb(frustum, world_min, world_max) == 0)
            draw_self = 0;  // skip own draw, but STILL recurse children
    }

    // Draw this node if visible and has mesh+material
    if (draw_self && node->mesh && node->material) {
        void *mat4 = mat4_from_doubles(node->world_matrix);
        rt_canvas3d_draw_mesh(canvas3d, node->mesh, mat4, node->material);
    }

    // ALWAYS recurse into children — node AABBs are per-mesh only, not hierarchical.
    // A parent's mesh may be outside the frustum while children (offset via local
    // transforms) are visible. Skipping children here would be a correctness bug.
    // Future optimization: compute hierarchical AABBs that enclose all descendants.
    for (int32_t i = 0; i < node->child_count; i++) {
        draw_node(node->children[i], canvas3d, frustum);
    }
}
```

## AABB Computation

When a mesh is assigned to a node (`SetMesh`), compute the object-space AABB:

```c
void compute_mesh_aabb(const vgfx3d_vertex_t *verts, uint32_t count,
                       float min[3], float max[3]) {
    min[0] = min[1] = min[2] = FLT_MAX;
    max[0] = max[1] = max[2] = -FLT_MAX;
    for (uint32_t i = 0; i < count; i++) {
        for (int j = 0; j < 3; j++) {
            if (verts[i].pos[j] < min[j]) min[j] = verts[i].pos[j];
            if (verts[i].pos[j] > max[j]) max[j] = verts[i].pos[j];
        }
    }
}
```

## Debug Runtime API

```c
// How many objects were culled in the last Draw call
int64_t rt_canvas3d_get_culled_count(void *canvas);

// Manual frustum test for a scene node
int8_t rt_canvas3d_is_visible(void *canvas, void *node);
```

## runtime.def Additions

```c
RT_FUNC(Canvas3DGetCulledCount, rt_canvas3d_get_culled_count, "Viper.Graphics3D.Canvas3D.get_CulledCount", "i64(obj)")
RT_FUNC(Canvas3DIsVisible,      rt_canvas3d_is_visible,       "Viper.Graphics3D.Canvas3D.IsVisible",       "i1(obj,obj)")

// Add to Canvas3D class:
//   RT_PROP("CulledCount", "i64", Canvas3DGetCulledCount, none)
//   RT_METHOD("IsVisible", "i1(obj)", Canvas3DIsVisible)
```

## Stubs

```c
int64_t rt_canvas3d_get_culled_count(void *c) { (void)c; return 0; }
int8_t  rt_canvas3d_is_visible(void *c, void *n) { (void)c; (void)n; return 1; }
```

## Tests (15)

| Test | Description |
|------|-------------|
| Fully inside | Object centered at origin, camera looking at it → visible |
| Fully outside left | Object far left → culled |
| Fully outside right | Object far right → culled |
| Fully outside top | Object far above → culled |
| Fully outside bottom | Object far below → culled |
| Fully outside behind | Object behind camera → culled |
| Fully outside beyond far | Object beyond far plane → culled |
| Partially intersecting | Object straddles left plane → visible (intersecting) |
| Sphere test | Bounding sphere fully outside → culled |
| Sphere intersecting | Sphere straddles plane → visible |
| Large object spanning frustum | Huge AABB containing entire frustum → visible |
| Multiple objects mixed | 10 objects, verify correct cull count |
| CulledCount property | After Draw, CulledCount reflects actual culled objects |
| IsVisible manual test | Check specific node visibility |
| Hierarchical independence | Parent mesh culled but offset child still drawn |

## Build Changes

- Add `vgfx3d_frustum.c` to `src/lib/graphics/CMakeLists.txt`
