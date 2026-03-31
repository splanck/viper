# Fix 103: Metal Backend Missing Bone Count Limit

## Severity: P0 — Critical

## Problem

The Metal backend does not enforce the 128-bone limit for GPU skinning. D3D11 and OpenGL
both cap `bone_count` at 128. Metal passes the count unchecked, which can cause GPU buffer
overflow, Metal validation errors, or system hang.

## Prerequisites

None — the constant `128` matches `VGFX3D_MAX_BONES`. Fix only modifies local variables.

## Fix

In `vgfx3d_backend_metal.m` around line 951:

```objc
int bone_count = cmd->bone_count;
if (bone_count > 128) bone_count = 128;
obj.hasSkinning = (cmd->bone_palette && bone_count > 0) ? 1 : 0;
// ... use bone_count (capped) for buffer size computation
```

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/vgfx3d_backend_metal.m` | Add bone count cap (~3 LOC) |

## Documentation Update

None — internal safety fix.

## Test

- Existing 3D canvas tests pass
- Skeleton with 128 bones — no Metal validation errors
- Skeleton with 200 bones — capped to 128, no crash
