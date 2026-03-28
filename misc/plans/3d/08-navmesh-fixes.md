# Plan: NavMesh3D Fixes

## Overview
After code verification: heap overflow risk is lower than originally reported but the O(n²) adjacency build is confirmed.

## ~~1. A* Heap Overflow~~ — LOWER RISK THAN REPORTED
**Verified:** Heap implementation at lines 300-321 has proper size management. However, stack-allocated heap with fixed capacity could still overflow on very large meshes. Add a capacity check before pushing.

## 2. O(n²) Adjacency Build (CONFIRMED)
**File:** `src/runtime/graphics/rt_navmesh3d.c:215-225`
**Verified:** Nested loop `for (i=0; i<count) for (j=i+1; j<count)` comparing every triangle pair.
**Fix:** Build edge hash map:
1. For each triangle, generate 3 edge keys: `edge_key = min(v_idx_a, v_idx_b) * MAX_VERTS + max(v_idx_a, v_idx_b)`
2. Insert into hash map: `edge_key → triangle_index`
3. When a key already exists: the two triangles share that edge → mark adjacent
4. O(n) with hash table (6 hash operations per triangle)

**Implementation detail:** Use a simple open-addressing hash table with `triangle_count * 4` capacity (load factor 0.75). Keys are int64, values are int32 triangle indices.

## 3. Null-Check Mallocs
**File:** `src/runtime/graphics/rt_navmesh3d.c:133,143`
**Fix:** Add null checks after malloc for `nm->vertices` and `nm->triangles`. Return NULL on failure.

## Files Modified
- `src/runtime/graphics/rt_navmesh3d.c` — Edge hash adjacency + null checks

## Verification
- Build navmesh from 10K triangle mesh — should complete in <100ms (was potentially multi-second)
- Pathfinding still produces correct paths after adjacency algorithm change
