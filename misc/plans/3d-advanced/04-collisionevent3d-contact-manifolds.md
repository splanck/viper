# Plan 04: CollisionEvent3D and Contact Manifolds

## Goal

Replace the current flat collision pair accessors with structured per-step collision events that expose the data gameplay code actually needs:

- enter/stay/exit
- trigger versus solid classification
- contact points
- normals
- penetration or separation
- relative velocity
- impulse magnitude

The existing world collision surface is enough for debugging, not for production gameplay.

## Verified Current State

- `Physics3DWorld` currently exposes only:
  - collision count
  - body A
  - body B
  - contact normal
  - penetration depth
- `Trigger3D` exists as a separate AABB zone API with enter/exit counts.
- `3dbowling` iterates raw contacts and infers impact meaning manually.

## Status of Older Plans

The older angular/joints plan proposed a richer system but only the joint subset shipped. Structured collision events did not land.

## API Shape

Introduce event objects while keeping the old accessors as compatibility wrappers.

### New classes

- `CollisionEvent3D`
- `ContactPoint3D`

### `Physics3DWorld` additions

- `get_CollisionEventCount`
- `GetCollisionEvent(index)`
- `get_EnterEventCount`
- `GetEnterEvent(index)`
- `get_StayEventCount`
- `GetStayEvent(index)`
- `get_ExitEventCount`
- `GetExitEvent(index)`

### `CollisionEvent3D` properties

- `BodyA`
- `BodyB`
- `ColliderA`
- `ColliderB`
- `IsTrigger`
- `ContactCount`
- `RelativeSpeed`
- `NormalImpulse`

### `CollisionEvent3D` methods

- `GetContactPoint(index)`
- `GetContactNormal(index)`
- `GetContactSeparation(index)`

Compatibility policy:

- keep `GetCollisionBodyA/B`, `GetCollisionNormal`, `GetCollisionDepth`
- implement them as views over the first manifold point in the new event buffer

## Internal Design

The world step should produce a stable per-frame contact buffer:

- contact manifolds built during narrow phase
- pair tracking table from previous step to compute enter/stay/exit
- event objects treated as frame-local snapshots

Recommended split:

- keep manifold generation in the narrow phase
- add a small event/cache layer after solver resolution
- do not make event objects own the bodies; they should hold raw references only for the frame

This plan should also decide how `Trigger3D` relates to body triggers:

- standalone `Trigger3D` stays for authored zones
- trigger bodies participate in the same enter/stay/exit event stream as solid bodies

## Implementation Phases

### Phase 1: internal manifold representation

Files:

- `src/runtime/graphics/rt_physics3d.c`
- `src/runtime/graphics/rt_physics3d.h`

Work:

- store one or more contact points per collision pair
- capture impulse and relative-velocity information

### Phase 2: event diffing

Work:

- track pair keys across steps
- derive enter/stay/exit buckets
- keep the frame-local buffers compact and deterministic

### Phase 3: public object wrappers

Files:

- new `src/runtime/graphics/rt_collision3d.h`
- new `src/runtime/graphics/rt_collision3d.c`
- `src/il/runtime/runtime.def`
- `src/il/runtime/classes/RuntimeClasses.hpp`
- `src/il/runtime/RuntimeSignatures.cpp`
- `src/runtime/CMakeLists.txt`
- `src/runtime/graphics/rt_graphics_stubs.c`

Work:

- expose `CollisionEvent3D` and `ContactPoint3D`
- preserve old query methods as compatibility wrappers

### Phase 4: body-filtered convenience views

Work:

- optionally add `Physics3DBody.GetContactCount` and `GetContact`
- only add this if the world-level API proves too noisy in examples

## Testing

### Runtime tests

- collision enters once, stays while overlapping, exits once
- trigger contacts produce trigger events without solver impulses
- manifold points and normals remain stable across repeated steps
- old `GetCollision*` accessors still behave consistently

### Example-driven tests

- bowling-ball collision can distinguish impact strength via `NormalImpulse`
- trigger zone entry/exit can be implemented without bespoke polling state

### Test files

- new `src/tests/runtime/RTCollision3DTests.cpp`
- update `src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp`

## Documentation

- add a `CollisionEvent3D` section to `docs/graphics3d-guide.md`
- update trigger and physics sections to explain the difference between authored zones and body triggers

## Risks and Non-Goals

Risks:

- unstable pair ordering will make events noisy and hard to test
- exposing pointers into internal buffers will create lifetime bugs

Non-goals:

- script callbacks or event subscriptions

Polling fits the current runtime style better and avoids binding complexity.
