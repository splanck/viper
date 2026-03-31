# Fix 111: Scene Graph Dirty Flag — Redundant O(n) Propagation

## Severity: P2 — Medium

## Problem

`mark_dirty()` in `rt_scene3d.c:185` recursively visits all descendants without
checking if a child is already dirty. In scenes with 1000+ nodes, moving the root
triggers 1000 recursive calls even if the entire tree is already dirty.

```c
static void mark_dirty(rt_scene_node3d *node) {
    node->world_dirty = 1;
    for (int32_t i = 0; i < node->child_count; i++)
        mark_dirty(node->children[i]);  // No early exit
}
```

## Fix

Add early-exit when a child is already dirty (its subtree must also be dirty):

```c
static void mark_dirty(rt_scene_node3d *node) {
    if (node->world_dirty)
        return;  // Already dirty — subtree must be too
    node->world_dirty = 1;
    for (int32_t i = 0; i < node->child_count; i++)
        mark_dirty(node->children[i]);
}
```

## File to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_scene3d.c` | Add early return in `mark_dirty()` (1 line) |

## Test

- Existing scene graph tests pass
- Performance: time 1000 iterations of moving root in a 1000-node scene — should be significantly faster
