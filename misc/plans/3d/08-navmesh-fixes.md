# Plan: NavMesh3D Fixes — COMPLETE

## ~~1. A* Heap Overflow~~ — ALREADY SAFE (verified)
Heap capacity is `tc * 3` (line 333). Push checks capacity (line 256). Closed-set prevents re-processing (line 347-348). Mallocs have null checks (lines 134-136, 147-149).

## 2. O(n²) Adjacency Build — FIXED (2026-03-28)
**Was:** Nested `for (i=0; i<count) for (j=i+1; j<count)` with `count_shared` comparing vertex arrays. O(n²) — 10K triangles = 50M pair comparisons.

**Now:** Edge hash map with open-addressing linear probe:
1. For each triangle's 3 edges, compute `key = min(va,vb) * 1M + max(va,vb)`
2. Probe hash table (capacity = triangle_count * 4, load ~0.75)
3. Empty slot: insert edge + triangle index
4. Matching key: two triangles share that edge → set mutual adjacency
5. Total: 3 hash ops per triangle = O(n)

Removed `count_shared()` and `set_neighbor()` helper functions (no longer needed).

**Fallback:** If hash table malloc fails, adjacency is skipped gracefully — navmesh builds but has no connectivity (pathfinding returns NULL).

## 3. Null-Check Mallocs — ALREADY PRESENT (verified)
Lines 134-136 and 147-149 have null checks with trap messages.

### Files Changed
- `src/runtime/graphics/rt_navmesh3d.c` — Edge hash adjacency, removed old helpers

### Tests Added
2 new tests in `test_rt_navmesh_blend.cpp`:
- `test_navmesh_adjacency_edge_hash`: Plane (2 triangles) — pathfinding across shared edge verifies adjacency works
- `test_navmesh_large_mesh`: Box mesh — verifies edge hash handles >2 triangles

Total: 19/19 navmesh+blend, 1358/1358 full suite.
