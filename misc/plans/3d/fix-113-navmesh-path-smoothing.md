# Fix 113: NavMesh Path Smoothing (Funnel Algorithm)

## Severity: P2 — Medium

## Problem

A* pathfinding in `rt_navmesh3d.c:316-407` returns raw triangle centroid waypoints.
Agents walk from centroid to centroid, producing jagged zigzag paths even when a
straight line would be valid. This is the #1 visual quality issue in NavMesh pathfinding.

```c
// Current: raw centroids as waypoints
for (int32_t i = 0; i < count; i++) {
    float *cen = nm->triangles[seq[i]].centroid;
    rt_path3d_add_point(path, rt_vec3_new(cen[0], cen[1], cen[2]));
}
```

## Fix: Simple String-Pulling (Funnel Algorithm)

The funnel algorithm post-processes A* output to find the shortest path through the
portal edges (shared edges between adjacent triangles). It produces smooth, direct paths.

### Algorithm (~80 LOC)

```
Input: triangle sequence from A*, start point, end point
Output: smoothed waypoint list

1. Build portal list: for each pair of adjacent triangles, find the shared edge
   (the "portal" — two vertices defining the passage between triangles)

2. Initialize funnel:
   - apex = start point
   - left boundary = start point
   - right boundary = start point

3. For each portal (left_vertex, right_vertex):
   - If right_vertex is inside the current funnel:
     - Tighten right boundary
   - Else:
     - If right_vertex crosses left boundary:
       - Add left boundary to path
       - Reset funnel with left boundary as new apex
     - Else:
       - Tighten right boundary
   - (Mirror logic for left_vertex)

4. Add end point to path
```

### Key Functions

```c
/// @brief Find the shared edge between two adjacent triangles.
static int find_portal(const nav_triangle_t *tri_a, const nav_triangle_t *tri_b,
                        const nav_vertex_t *verts,
                        float left[3], float right[3]);

/// @brief Smooth an A* path using the simple funnel algorithm.
static void smooth_path(const rt_navmesh3d *nm,
                         const int32_t *tri_seq, int32_t tri_count,
                         const float *start, const float *end,
                         void *path); // rt_path3d*
```

### Cross Product for Side Test

```c
static float cross2d(float ax, float az, float bx, float bz) {
    return ax * bz - az * bx;
}
```

The funnel operates in 2D (XZ plane) since navigation meshes are typically planar.

## File to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_navmesh3d.c` | Add funnel algorithm, replace centroid waypoints (~80 LOC) |

## Test

- Existing NavMesh tests pass
- Create a corridor NavMesh (straight hallway with many triangles)
- Verify path has only start + end (2 waypoints, straight line) instead of N centroid zigzags
- Create an L-shaped NavMesh — verify path has 3 waypoints (start, corner, end)
