# Scene/Physics Spatial Structure Decision

Decision: keep `Scene3D` and `Physics3D` on sibling spatial structures for now,
with shared query semantics but not a shared physical tree.

## Why Scene3D Keeps The BVH

- `Scene3D` indexes visible drawable `SceneNode3D` mesh entries only.
- Its bounds are render-facing: double-precision transformed draw AABBs, selected
  LOD/impostor candidates, hidden-subtree skipping, and flat-walk parity under
  the internal `use_spatial_index` flag.
- Transform-only dirties refit the existing BVH; hierarchy, visibility, mesh,
  LOD, and impostor changes rebuild it lazily.
- `Draw`, `QueryAABB`, `QuerySphere`, and `RaycastNodes` need deterministic
  scene traversal order after broad filtering.

## Why Physics3D Keeps Its Broadphase

- `Physics3D` indexes bodies with colliders, including bodies with no scene node
  and bodies that should never render.
- Pair generation is solver-facing: it must skip static-static pairs, honor
  bidirectional layer/mask filters, preserve trigger/contact-event identity, and
  hand candidate pairs to shape-specific narrow phase before warm-started solving.
- The step broadphase is rebuilt from body AABBs each simulation step; query
  broadphase entries are cached by body and collider bounds revisions.
- Physics already has the right fallback behavior: allocation failure falls back
  to brute-force pair testing, and per-mesh BVHs prune triangle candidates inside
  narrow phase.

## Rejected Shared-Tree Shape

Routing physics through the Scene3D BVH would either drop valid physics bodies
that are not visible drawable nodes, or force render-only state such as hidden
subtree and LOD/impostor rules into the solver. Routing Scene3D through the
physics broadphase would lose render traversal order, selected render bounds,
and scene visibility semantics.

The shared contract remains semantic: both systems use finite double-precision
world AABBs for broad filtering and keep exact narrow tests after candidate
selection. A physical shared tree should be reconsidered only if a future
physics broadphase grows beyond the sweep-and-prune/cache design and can prove
parity and lower step/query cost without changing either public contract.
