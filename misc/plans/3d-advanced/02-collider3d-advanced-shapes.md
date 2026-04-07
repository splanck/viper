# Plan 02: Collider3D and Advanced Shape Support

## Goal

Split shape data out of `Physics3DBody` and add the collider types that modern 3D gameplay actually needs:

- box
- sphere
- capsule
- convex hull
- triangle mesh
- compound
- heightfield

The existing body constructors are fine as convenience entry points, but they are not a scalable physics API.

## Verified Current State

- `Physics3DBody` constructors currently bake shape choice into the body object itself.
- The only exposed shapes are AABB, sphere, and capsule.
- `3dbowling` approximates the lane and surrounding geometry with many simple bodies because there is no reusable static mesh or compound collider surface.

## Status of Older Plans

No older plan completed this split. The old angular/joints plan assumed bodies were also shapes. That is the main architectural limit that now needs to be removed.

## API Shape

Introduce `Viper.Graphics3D.Collider3D` as the reusable shape asset and keep the existing body constructors as wrappers.

### New class

`Collider3D` should be a runtime class with static-style constructors:

- `Collider3D.NewBox(hx, hy, hz)`
- `Collider3D.NewSphere(radius)`
- `Collider3D.NewCapsule(radius, height)`
- `Collider3D.NewConvexHull(mesh)`
- `Collider3D.NewMesh(mesh)`
- `Collider3D.NewHeightfield(heightmap, scaleX, scaleY, scaleZ)`
- `Collider3D.NewCompound()`

### Collider methods

- `AddChild(child, localTransform)` for compound colliders
- `GetType()`
- `GetLocalBoundsMin()`
- `GetLocalBoundsMax()`

### Body integration

Add to `Physics3DBody`:

- `New(mass)`
- `SetCollider(collider)`
- `GetCollider()`

Keep:

- `NewAABB`, `NewSphere`, `NewCapsule`

Those constructors become wrappers that allocate a body and assign a simple collider internally.

## Internal Design

Create a dedicated collider representation under `src/runtime/graphics/`.

Recommended layout:

- `rt_collider3d.h`
- `rt_collider3d.c`

The collider object should own:

- shape kind
- local transform
- shape-specific payload
- cached local bounds

The body should own:

- mass/inertia
- motion state
- active collider pointer

The world should query the collider for:

- world-space AABB
- ray intersection
- sweep support
- contact manifold generation

## Implementation Phases

### Phase 1: body/collider split

Files:

- new `src/runtime/graphics/rt_collider3d.h`
- new `src/runtime/graphics/rt_collider3d.c`
- `src/runtime/graphics/rt_physics3d.h`
- `src/runtime/graphics/rt_physics3d.c`
- `src/il/runtime/runtime.def`
- `src/il/runtime/classes/RuntimeClasses.hpp`
- `src/il/runtime/RuntimeSignatures.cpp`
- `src/runtime/CMakeLists.txt`
- `src/runtime/graphics/rt_graphics_stubs.c`

Work:

- add `Collider3D`
- update body to reference one collider
- reimplement old shape constructors as wrappers

### Phase 2: convex and mesh colliders

Work:

- support convex hull generation from `Mesh3D`
- support static triangle-mesh colliders
- explicitly reject dynamic triangle-mesh bodies in v1

### Phase 3: compound colliders

Work:

- allow child colliders with local transforms
- compute compound AABB and mass properties
- make this the preferred way to build complex dynamic bodies

### Phase 4: heightfield colliders

Work:

- integrate with terrain and heightmap-driven worlds
- add efficient bounds and cell lookup helpers

## Testing

### Runtime tests

- wrapper constructors still behave identically for box/sphere/capsule
- convex hull collider collides with sphere body
- static mesh collider blocks a rolling ball on a lane-shaped mesh
- compound collider child transforms affect contacts correctly
- heightfield collider supports slope/ground queries

### Export/link tests

- update `RTGraphicsSurfaceLinkTests.cpp` for all new `Collider3D` entry points

### Example coverage

- add an API-audit sample showing body/collider separation
- add one example showing a static lane mesh collider replacing a pile of AABBs

## Documentation

- add `Collider3D` to `docs/graphics3d-guide.md`
- mark `Physics3DBody.NewAABB/NewSphere/NewCapsule` as convenience factories, not the preferred authoring path
- document the static-only rule for triangle-mesh colliders

## Risks and Non-Goals

Risks:

- if `Collider3D` leaks shape-specific assumptions into bodies, later query/contact code will fragment
- mesh-collider support can become a performance trap without a better broadphase

Non-goals for this plan:

- world query APIs themselves
- structured contact events

Those are Plans `03` and `04`.
