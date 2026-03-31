# Fix 102: Water3D and Decal3D Finalizer Memory Leaks

## Severity: P0 — Critical

## Problem

Both `rt_water3d.c:59` and `rt_decal3d.c:58` have empty finalizers. Mesh and material
objects created during lifetime are never freed when the parent is garbage collected.

## Prerequisites

Need to verify that `rt_obj_release_check0` and `rt_obj_free` are accessible. These
functions are declared in `rt_object.h`. Check if Water3D and Decal3D files already
include it (or include a header that transitively provides it).

**If not included:** Add `#include "rt_object.h"` or use extern declarations:
```c
extern int32_t rt_obj_release_check0(void *);
extern void rt_obj_free(void *);
```

**NULL safety:** Both functions handle NULL pointers gracefully (verified from other
finalizers in the codebase that use the same pattern).

## Fix

### Water3D (`rt_water3d.c:59-61`)
```c
static void water3d_finalizer(void *obj) {
    rt_water3d *w = (rt_water3d *)obj;
    if (w->mesh) {
        if (rt_obj_release_check0(w->mesh))
            rt_obj_free(w->mesh);
        w->mesh = NULL;
    }
    if (w->material) {
        if (rt_obj_release_check0(w->material))
            rt_obj_free(w->material);
        w->material = NULL;
    }
}
```

### Decal3D (`rt_decal3d.c:58-60`)
```c
static void decal3d_finalizer(void *obj) {
    rt_decal3d *d = (rt_decal3d *)obj;
    if (d->mesh) {
        if (rt_obj_release_check0(d->mesh))
            rt_obj_free(d->mesh);
        d->mesh = NULL;
    }
    if (d->material) {
        if (rt_obj_release_check0(d->material))
            rt_obj_free(d->material);
        d->material = NULL;
    }
}
```

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_water3d.c` | Implement finalizer (~10 LOC) |
| `src/runtime/graphics/rt_decal3d.c` | Implement finalizer (~10 LOC) |

## Documentation Update

None — internal fix, no API change.

## Test

- Create Water3D object, verify mesh/material are non-NULL during lifetime
- Let Water3D go out of scope (GC collect) — verify no memory growth
- Same for Decal3D
- Existing 3D tests pass (regression)
