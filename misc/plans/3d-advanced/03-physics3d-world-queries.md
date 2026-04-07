# Plan 03: Physics3DWorld Queries

## Goal

Expose gameplay-grade world queries against `Physics3DWorld`:

- nearest raycast
- all-hit raycast
- sphere sweep
- capsule sweep
- sphere overlap
- AABB overlap

The runtime already has low-level shape tests and a private character sweep path. What is missing is an actual world-facing API that works against live bodies and colliders.

## Verified Current State

- `rt_raycast3d.h` exposes standalone mesh/shape intersection helpers, not world queries.
- `rt_physics3d.c` contains internal sweep logic for `Character3D`, but it is not reusable from game code.
- Gameplay code currently has no supported way to ask the world "what would I hit?" without duplicating physics internals.

## API Shape

Add query methods to `Viper.Graphics3D.Physics3DWorld` and introduce lightweight result objects.

### New result classes

- `PhysicsHit3D`
- `PhysicsHitList3D`

### `PhysicsHit3D` properties

- `Body`
- `Collider`
- `Point`
- `Normal`
- `Distance`
- `Fraction`
- `StartedPenetrating`
- `IsTrigger`

### `Physics3DWorld` methods

- `Raycast(origin, direction, maxDistance, mask) -> obj`
- `RaycastAll(origin, direction, maxDistance, mask) -> obj`
- `SweepSphere(center, radius, delta, mask) -> obj`
- `SweepCapsule(a, b, radius, delta, mask) -> obj`
- `OverlapSphere(center, radius, mask) -> obj`
- `OverlapAABB(min, max, mask) -> obj`

Compatibility note:

- returning `obj` should follow existing runtime behavior where a null object indicates "no hit"
- `mask` should use the same layer semantics already present on `Physics3DBody`

## Internal Design

Do not layer these APIs on the existing O(n^2) pair scan path.

Instead:

- build a reusable broadphase index for active body/collider AABBs
- let collision stepping and world queries share that index
- keep narrow-phase tests in one place so raycast, sweep, overlap, and normal collision resolution agree

Recommended implementation split:

- broadphase helpers remain in `rt_physics3d.c` unless they justify a dedicated module
- result-object wrappers can live in a new `rt_physics3d_query.c` if `rt_physics3d.c` gets too large

## Implementation Phases

### Phase 1: queryable broadphase

Files:

- `src/runtime/graphics/rt_physics3d.c`
- `src/runtime/graphics/rt_physics3d.h`

Work:

- introduce an explicit body/collider AABB cache
- add broadphase query helpers for ray, sweep candidate collection, and overlap candidate collection

### Phase 2: nearest-hit API

Files:

- new `src/runtime/graphics/rt_physics3d_query.h`
- new `src/runtime/graphics/rt_physics3d_query.c`
- `src/il/runtime/runtime.def`
- `src/il/runtime/classes/RuntimeClasses.hpp`
- `src/il/runtime/RuntimeSignatures.cpp`
- `src/runtime/CMakeLists.txt`
- `src/runtime/graphics/rt_graphics_stubs.c`

Work:

- implement `PhysicsHit3D`
- implement `Raycast`, `SweepSphere`, `SweepCapsule`

### Phase 3: all-hit and overlap APIs

Work:

- implement `PhysicsHitList3D`
- add sorted `RaycastAll`
- add `OverlapSphere` and `OverlapAABB`

### Phase 4: query policy polish

Work:

- add trigger inclusion/exclusion rules
- document backface handling for mesh colliders
- add deterministic hit ordering rules

## Testing

### Runtime tests

- raycast returns nearest hit body, point, and normal
- `RaycastAll` returns sorted hits
- sweep reports initial overlap correctly
- overlap queries honor collision masks
- trigger-only bodies can be included or excluded according to policy

### Regression tests

- `Character3D` internal sweep and public sweep agree on the same scene
- a static mesh lane collider is raycastable and sweepable

### Test files

- new `src/tests/runtime/RTPhysics3DQueryTests.cpp`
- update `src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp`

## Documentation

- add a dedicated "Physics Queries" section to `docs/graphics3d-guide.md`
- add one gameplay example for hitscan and one for ground probing

## Risks and Non-Goals

Risks:

- ambiguous result lifetimes if hit objects capture mutable internal buffers
- performance regressions if broadphase remains coupled to step-only data structures

Non-goals:

- per-contact event histories
- joint authoring

Those belong elsewhere.
