# Fix 107: FBX Loader Unchecked Array Accesses

## Severity: P1 — High

## Problem

The FBX loader accesses `objects->children[i]` in multiple loops without validating that
the `children` pointer is non-NULL. A malformed FBX file with corrupted node structure
can cause out-of-bounds reads or NULL dereference crashes.

## Prerequisites

None — adds defensive NULL checks using standard C patterns. No new includes needed.

## Fix

Add NULL checks before each child iteration loop in `rt_fbx_loader.c`. There are ~10
sites that need this pattern:

```c
// Before:
for (int32_t i = 0; i < objects->child_count; i++) {
    fbx_node_t *obj = &objects->children[i];

// After:
if (!objects->children) goto cleanup;  // or continue/break as appropriate
for (int32_t i = 0; i < objects->child_count; i++) {
    fbx_node_t *obj = &objects->children[i];
```

### Sites to check (all in `rt_fbx_loader.c`):

1. Geometry extraction loop (~line 1412)
2. Material extraction loop (~line 1425)
3. Texture extraction loop (~line 1467)
4. Skeleton extraction — Model scan (~line 840)
5. Skeleton extraction — Connection lookup (~line 892)
6. Animation extraction — AnimationStack scan (~line 1084)
7. Morph target extraction — Shape geometry scan (~line 1570)
8. Properties70 iteration in `fbx_extract_material` (~line 792)
9. Connection parsing loop (~line 551)
10. Node parsing in `fbx_parse_node` (~line 406)

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_fbx_loader.c` | Add NULL checks at ~10 sites (~20 LOC) |

## Documentation Update

None — internal robustness fix.

## Test

- Existing FBX tests pass (regression)
- Create a truncated FBX file (cut at 50% of size) — verify graceful NULL return
- Create a zero-length file with valid FBX magic — verify NULL return
