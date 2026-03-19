# Phase E: AI + Navigation

## Goal

Add 3D navigation mesh pathfinding and animation blend trees — enabling enemies that can navigate around obstacles and smoothly blend locomotion animations.

## Dependencies

- Phase A complete (Physics3D for ground queries)
- 2D Pathfinder A* (`src/runtime/collections/rt_pathfinder.c` — binary min-heap A*)
- AnimPlayer3D (`rt_skeleton3d.c:580-692` — `compute_bone_palette` two-phase algorithm)
- Mesh3D vertex/index access (`rt_canvas3d_internal.h:47-56` — `rt_mesh3d` struct)
- `vgfx3d_skin_vertices` (`vgfx3d_skinning.c` — CPU skinning function)

---

## E1. NavMesh3D (~400 LOC)

### New Files

**`src/runtime/graphics/rt_navmesh3d.h`** (~25 LOC)
**`src/runtime/graphics/rt_navmesh3d.c`** (~375 LOC)

### Data Structures

```c
typedef struct {
    float position[3];       /* vertex position */
} nav_vertex_t;

typedef struct {
    int32_t v[3];            /* vertex indices (CCW winding) */
    int32_t neighbors[3];    /* adjacent triangle index per edge (-1 = boundary) */
    float centroid[3];       /* precomputed center of triangle */
    float normal[3];         /* face normal */
} nav_triangle_t;

typedef struct {
    void *vptr;
    nav_vertex_t *vertices;
    int32_t vertex_count;
    nav_triangle_t *triangles;
    int32_t triangle_count;
    double agent_radius;     /* agent clearance radius (default 0.4) */
    double agent_height;     /* agent standing height (default 1.8) */
    double max_slope;        /* max walkable slope in degrees (default 45) */
} rt_navmesh3d;
```

### Building NavMesh from Level Mesh

Simplified approach: extract walkable triangles directly from the level mesh by slope filtering, then build adjacency.

```c
void *rt_navmesh3d_build(void *mesh_obj, double agent_radius, double agent_height) {
    rt_mesh3d *m = (rt_mesh3d *)mesh_obj;
    rt_navmesh3d *nm = /* alloc + init */;
    nm->agent_radius = agent_radius;
    nm->agent_height = agent_height;
    double max_slope_cos = cos(nm->max_slope * M_PI / 180.0);

    /* Phase 1: Copy all vertices */
    nm->vertex_count = m->vertex_count;
    nm->vertices = malloc(nm->vertex_count * sizeof(nav_vertex_t));
    for (uint32_t i = 0; i < m->vertex_count; i++) {
        nm->vertices[i].position[0] = m->vertices[i].pos[0];
        nm->vertices[i].position[1] = m->vertices[i].pos[1];
        nm->vertices[i].position[2] = m->vertices[i].pos[2];
    }

    /* Phase 2: Filter triangles by slope (keep nearly-horizontal faces) */
    int32_t tri_cap = m->index_count / 3;
    nm->triangles = malloc(tri_cap * sizeof(nav_triangle_t));
    nm->triangle_count = 0;

    for (uint32_t i = 0; i + 2 < m->index_count; i += 3) {
        uint32_t i0 = m->indices[i], i1 = m->indices[i+1], i2 = m->indices[i+2];
        /* Compute face normal */
        float *p0 = nm->vertices[i0].position;
        float *p1 = nm->vertices[i1].position;
        float *p2 = nm->vertices[i2].position;
        float e1[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
        float e2[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};
        float nx = e1[1]*e2[2] - e1[2]*e2[1];
        float ny = e1[2]*e2[0] - e1[0]*e2[2];
        float nz = e1[0]*e2[1] - e1[1]*e2[0];
        float nlen = sqrtf(nx*nx + ny*ny + nz*nz);
        if (nlen < 1e-8f) continue;
        nx /= nlen; ny /= nlen; nz /= nlen;

        /* Check slope: normal.y > cos(max_slope) means walkable */
        if (ny < (float)max_slope_cos) continue;

        nav_triangle_t *tri = &nm->triangles[nm->triangle_count++];
        tri->v[0] = i0; tri->v[1] = i1; tri->v[2] = i2;
        tri->normal[0] = nx; tri->normal[1] = ny; tri->normal[2] = nz;
        tri->centroid[0] = (p0[0]+p1[0]+p2[0]) / 3.0f;
        tri->centroid[1] = (p0[1]+p1[1]+p2[1]) / 3.0f;
        tri->centroid[2] = (p0[2]+p1[2]+p2[2]) / 3.0f;
        tri->neighbors[0] = tri->neighbors[1] = tri->neighbors[2] = -1;
    }

    /* Phase 3: Build adjacency (triangles sharing 2 vertices are neighbors) */
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        for (int32_t j = i + 1; j < nm->triangle_count; j++) {
            int shared = count_shared_vertices(nm->triangles[i].v, nm->triangles[j].v);
            if (shared >= 2) {
                /* Find which edges are shared and set neighbor pointers */
                set_neighbor_edge(&nm->triangles[i], &nm->triangles[j], j);
                set_neighbor_edge(&nm->triangles[j], &nm->triangles[i], i);
            }
        }
    }

    return nm;
}
```

### A* Pathfinding

Reuses the A* heap concept from `rt_pathfinder.c`:

```c
void *rt_navmesh3d_find_path(void *obj, void *from_vec, void *to_vec) {
    rt_navmesh3d *nm = (rt_navmesh3d *)obj;

    /* Find start/end triangles */
    int start = find_containing_triangle(nm, from_vec);
    int goal = find_containing_triangle(nm, to_vec);
    if (start < 0 || goal < 0) return NULL;
    if (start == goal) {
        /* Already in same triangle — direct path */
        return build_path_single(from_vec, to_vec);
    }

    /* A* on triangle adjacency graph */
    float *g_cost = calloc(nm->triangle_count, sizeof(float));
    int32_t *parent = malloc(nm->triangle_count * sizeof(int32_t));
    int8_t *closed = calloc(nm->triangle_count, sizeof(int8_t));
    memset(parent, -1, nm->triangle_count * sizeof(int32_t));

    /* Min-heap open set (binary heap by f = g + h) */
    typedef struct { int32_t tri; float f; } heap_entry;
    heap_entry *heap = malloc(nm->triangle_count * sizeof(heap_entry));
    int32_t heap_size = 0;

    /* Initialize with start */
    g_cost[start] = 0;
    heap_push(heap, &heap_size, start, heuristic(nm, start, goal));

    while (heap_size > 0) {
        int32_t current = heap_pop(heap, &heap_size);
        if (current == goal) break;
        if (closed[current]) continue;
        closed[current] = 1;

        /* Expand neighbors */
        for (int e = 0; e < 3; e++) {
            int32_t next = nm->triangles[current].neighbors[e];
            if (next < 0 || closed[next]) continue;

            float edge_cost = centroid_distance(nm, current, next);
            float new_g = g_cost[current] + edge_cost;
            if (new_g < g_cost[next] || !closed[next]) {
                g_cost[next] = new_g;
                parent[next] = current;
                heap_push(heap, &heap_size, next, new_g + heuristic(nm, next, goal));
            }
        }
    }

    /* Reconstruct path: triangle centroids */
    void *path = build_path_from_parents(nm, parent, start, goal, from_vec, to_vec);

    free(g_cost); free(parent); free(closed); free(heap);
    return path; /* returns Seq[Vec3] or NULL */
}
```

### Point Queries

```c
int32_t find_containing_triangle(const rt_navmesh3d *nm, void *point) {
    float px = (float)rt_vec3_x(point);
    float py = (float)rt_vec3_y(point);
    float pz = (float)rt_vec3_z(point);

    float best_dist = FLT_MAX;
    int32_t best_tri = -1;
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        /* Project point onto triangle plane, check if inside */
        float *v0 = nm->vertices[nm->triangles[i].v[0]].position;
        float *v1 = nm->vertices[nm->triangles[i].v[1]].position;
        float *v2 = nm->vertices[nm->triangles[i].v[2]].position;
        /* Barycentric test (ignoring Y, project onto XZ) */
        if (point_in_triangle_xz(px, pz, v0, v1, v2)) {
            float dy = fabsf(py - nm->triangles[i].centroid[1]);
            if (dy < best_dist) { best_dist = dy; best_tri = i; }
        }
    }
    return best_tri;
}

void *rt_navmesh3d_sample_position(void *obj, void *point) {
    rt_navmesh3d *nm = (rt_navmesh3d *)obj;
    int32_t tri = find_containing_triangle(nm, point);
    if (tri >= 0) {
        /* Project point onto triangle surface */
        /* ... */
        return rt_vec3_new(snapped_x, snapped_y, snapped_z);
    }
    /* Not on navmesh — find nearest triangle centroid */
    /* ... brute force closest centroid ... */
    return rt_vec3_new(nearest_x, nearest_y, nearest_z);
}
```

### Debug Visualization

```c
void rt_navmesh3d_debug_draw(void *obj, void *canvas) {
    rt_navmesh3d *nm = (rt_navmesh3d *)obj;
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        nav_triangle_t *tri = &nm->triangles[i];
        float *v0 = nm->vertices[tri->v[0]].position;
        float *v1 = nm->vertices[tri->v[1]].position;
        float *v2 = nm->vertices[tri->v[2]].position;
        /* Draw 3 edges in green (DrawLine3D) */
        int64_t color = 0x00FF00;
        rt_canvas3d_draw_line3d(canvas, vec3_f(v0), vec3_f(v1), color);
        rt_canvas3d_draw_line3d(canvas, vec3_f(v1), vec3_f(v2), color);
        rt_canvas3d_draw_line3d(canvas, vec3_f(v2), vec3_f(v0), color);
    }
}
```

### Public API

```c
void   *rt_navmesh3d_build(void *mesh, double agent_radius, double agent_height);
void   *rt_navmesh3d_find_path(void *navmesh, void *from, void *to);
void   *rt_navmesh3d_sample_position(void *navmesh, void *point);
int8_t  rt_navmesh3d_is_walkable(void *navmesh, void *point);
int64_t rt_navmesh3d_get_triangle_count(void *navmesh);
void    rt_navmesh3d_set_max_slope(void *navmesh, double degrees);
void    rt_navmesh3d_debug_draw(void *navmesh, void *canvas);
```

### Namespace: `Viper.Graphics3D.NavMesh3D`
### RuntimeClasses.hpp: add `RTCLS_NavMesh3D`
### runtime.def: 7 RT_FUNC + 1 RT_CLASS
### Stubs: 7 functions
### CMakeLists: add `graphics/rt_navmesh3d.c`

---

## E2. Animation Blending (~200 LOC)

### Modified/New Files

**`src/runtime/graphics/rt_skeleton3d.h`** (+declarations)
**`src/runtime/graphics/rt_skeleton3d.c`** (+200 LOC)

### Data Structure

```c
#define MAX_BLEND_STATES 8

typedef struct {
    char name[64];
    rt_animation3d *animation;  /* borrowed pointer */
    float weight;               /* blend weight [0, 1] */
    float time;                 /* current playback time */
    float speed;                /* playback speed (default 1.0) */
    int8_t looping;             /* per-state looping */
} anim_blend_state_t;

typedef struct {
    void *vptr;
    rt_skeleton3d *skeleton;    /* borrowed — NOT owned */
    anim_blend_state_t states[MAX_BLEND_STATES];
    int32_t state_count;
    float *bone_palette;        /* output: bone_count * 16 floats (same format as AnimPlayer3D) */
    float *local_transforms;    /* workspace: bone_count * 16 floats */
    float *temp_state_local;    /* per-state sampling workspace: bone_count * 16 floats */
} rt_anim_blend3d;
```

### Bone Palette Computation

Follows the SAME two-phase algorithm as `compute_bone_palette` in `rt_skeleton3d.c:664-692`, but with multi-state blending:

```c
static void blend_compute_palette(rt_anim_blend3d *blend) {
    rt_skeleton3d *skel = blend->skeleton;
    if (!skel || skel->bone_count == 0) return;
    int32_t bc = skel->bone_count;

    /* Start with bind pose for all bones */
    for (int32_t b = 0; b < bc; b++)
        memcpy(&blend->local_transforms[b * 16], skel->bones[b].bind_pose_local, 64);

    /* For each active state: sample + accumulate weighted contribution */
    float total_weight = 0.0f;
    for (int32_t s = 0; s < blend->state_count; s++) {
        anim_blend_state_t *st = &blend->states[s];
        if (st->weight < 1e-6f || !st->animation) continue;
        total_weight += st->weight;

        /* Sample this state's animation into temp_state_local */
        for (int32_t c = 0; c < st->animation->channel_count; c++) {
            int32_t bone = st->animation->channels[c].bone_index;
            if (bone < 0 || bone >= bc) continue;
            /* sample_channel from existing AnimPlayer3D code */
            sample_channel(&st->animation->channels[c], st->time,
                           &blend->temp_state_local[bone * 16]);
        }

        /* Blend into local_transforms: weighted lerp of matrix elements */
        /* First state sets values; subsequent states blend additively */
        if (s == 0) {
            for (int32_t b = 0; b < bc; b++)
                for (int i = 0; i < 16; i++)
                    blend->local_transforms[b*16+i] =
                        skel->bones[b].bind_pose_local[i] * (1.0f - st->weight) +
                        blend->temp_state_local[b*16+i] * st->weight;
        } else {
            float w = st->weight / total_weight; /* normalized weight */
            for (int32_t b = 0; b < bc; b++)
                for (int i = 0; i < 16; i++)
                    blend->local_transforms[b*16+i] +=
                        (blend->temp_state_local[b*16+i] - blend->local_transforms[b*16+i]) * w;
        }
    }

    /* Two-phase globals + palette (identical to AnimPlayer3D:671-692) */
    float *globals = malloc((size_t)bc * 16 * sizeof(float));
    if (!globals) return;
    for (int32_t i = 0; i < bc; i++) {
        if (skel->bones[i].parent_index >= 0)
            mat4f_mul_local(&globals[skel->bones[i].parent_index * 16],
                            &blend->local_transforms[i * 16], &globals[i * 16]);
        else
            memcpy(&globals[i * 16], &blend->local_transforms[i * 16], 64);
    }
    for (int32_t i = 0; i < bc; i++)
        mat4f_mul_local(&globals[i * 16], skel->bones[i].inverse_bind,
                        &blend->bone_palette[i * 16]);
    free(globals);
}
```

### Integration with DrawMeshSkinned

`rt_canvas3d_draw_mesh_skinned` currently takes `void *anim_player` and casts to `rt_anim_player3d`. For AnimBlend3D compatibility:

**Option 1 (simplest):** Add `rt_canvas3d_draw_mesh_blended`:
```c
void rt_canvas3d_draw_mesh_blended(void *canvas, void *mesh, void *transform,
                                     void *material, void *blend) {
    /* Same as DrawMeshSkinned but accesses blend->bone_palette */
    rt_anim_blend3d *b = (rt_anim_blend3d *)blend;
    /* ... allocate skinned buffer, call vgfx3d_skin_vertices with b->bone_palette ... */
}
```

**Option 2 (cleaner):** Both AnimPlayer3D and AnimBlend3D have `bone_palette` at the same struct offset. Add a common accessor:
```c
float *get_bone_palette(void *anim_obj) {
    /* Both types start with vptr, then have bone_palette at a known offset */
    /* This requires careful struct layout alignment */
}
```

**Recommended: Option 1** — explicit separate function, no fragile struct aliasing.

### Public API

```c
void   *rt_anim_blend3d_new(void *skeleton);
int64_t rt_anim_blend3d_add_state(void *blend, rt_string name, void *animation);
void    rt_anim_blend3d_set_weight(void *blend, int64_t state, double weight);
void    rt_anim_blend3d_set_weight_by_name(void *blend, rt_string name, double weight);
double  rt_anim_blend3d_get_weight(void *blend, int64_t state);
void    rt_anim_blend3d_set_speed(void *blend, int64_t state, double speed);
void    rt_anim_blend3d_update(void *blend, double dt);
int64_t rt_anim_blend3d_state_count(void *blend);
void    rt_canvas3d_draw_mesh_blended(void *canvas, void *mesh, void *transform,
                                       void *material, void *blend);
```

### Namespace: `Viper.Graphics3D.AnimBlend3D`
### RuntimeClasses.hpp: add `RTCLS_AnimBlend3D`
### runtime.def: 9 RT_FUNC + 1 RT_CLASS
### Stubs: 9 functions
### Header: add declarations to `rt_skeleton3d.h`

---

## Files Modified/Created Summary

| Action | File | Est. LOC |
|--------|------|----------|
| NEW | `src/runtime/graphics/rt_navmesh3d.h` | ~25 |
| NEW | `src/runtime/graphics/rt_navmesh3d.c` | ~375 |
| MOD | `src/runtime/graphics/rt_skeleton3d.h` | +15 |
| MOD | `src/runtime/graphics/rt_skeleton3d.c` | +200 |
| MOD | `src/runtime/graphics/rt_canvas3d.h` | +3 |
| MOD | `src/runtime/graphics/rt_graphics_stubs.c` | +20 |
| MOD | `src/il/runtime/runtime.def` | +16 entries |
| MOD | `src/il/runtime/classes/RuntimeClasses.hpp` | +2 class IDs |
| MOD | `src/il/runtime/RuntimeSignatures.cpp` | +1 include |
| MOD | `src/runtime/CMakeLists.txt` | +1 source |
| MOD | `src/tests/unit/CMakeLists.txt` | +1 test |
| NEW | `src/tests/unit/test_rt_navmesh_blend.cpp` | ~200 |

---

## Tests

### NavMesh Tests (8)
- Build from flat plane: all triangles walkable
- Build from box room: walls excluded by slope filter
- Find path: straight line when unobstructed
- Find path: around obstacle (L-shaped path)
- Sample position: snaps to nearest walkable surface
- Is walkable: on mesh → true, off edge → false
- Max slope: set to 30° → steeper triangles excluded
- Triangle count matches expected

### Animation Blend Tests (6)
- Single state weight=1.0: identical output to AnimPlayer3D
- Two states 50/50: midpoint of both poses
- Weight=0: contributes nothing
- Speed 2x: animation advances twice as fast
- Looping: time wraps correctly
- DrawMeshBlended produces correct skinned output

## Verification

1. Build clean (zero warnings)
2. 1334+ ctest pass (new add ~14)
3. Demo: enemy patrolling via NavMesh waypoints
4. Demo: character with idle/walk/run blend based on velocity
5. NavMesh debug visualization (green triangle outlines)
6. Native compilation verified
7. `./scripts/check_runtime_completeness.sh` passes
