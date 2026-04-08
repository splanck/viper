# Plan 09: NavAgent3D

## Status in Tree

Implemented.

## Goal

Add the gameplay-facing agent layer on top of `NavMesh3D`.

The existing navmesh surface is a good geometry/pathfinding primitive. What it does not provide is an actor that can:

- own a goal
- maintain and follow a path
- repath when needed
- produce desired movement
- bind to a `Character3D` or scene node

## Verified Current State

- `NavMesh3D` supports build, pathfinding, point sampling, walkability, slope tuning, and debug draw.
- `NavAgent3D` now exists as the gameplay-facing layer above it, with target ownership, path-follow state, periodic repath, and bindings for `Character3D` and `SceneNode3D`.
- lightweight local avoidance and off-mesh links are still intentionally absent.

## Status of Older Plans

`misc/plans/3d/08-navmesh-fixes.md` landed its performance/safety fix scope, but it did not add any higher-level navigation API. This plan is the next layer, not a replacement.

## API Shape

Introduce `Viper.Graphics3D.NavAgent3D`.

### Core API

- `NavAgent3D.New(navmesh, radius, height)`
- `SetTarget(position)`
- `ClearTarget()`
- `Update(dt)`
- `Warp(position)`

### Properties

- `Position`
- `Velocity`
- `DesiredVelocity`
- `HasPath`
- `RemainingDistance`
- `StoppingDistance`
- `DesiredSpeed`
- `AutoRepath`

### Integration helpers

- `BindCharacter(controller)`
- `BindNode(node)`

The initial goal is not to solve crowd simulation perfectly. The goal is to make one actor move robustly through authored space with minimal boilerplate.

## Internal Design

Recommended layers:

- `NavMesh3D` stays responsible for topology and pathfinding
- `NavAgent3D` owns path-follow state and steering
- bindings optionally push the result into `Character3D` or `SceneNode3D`

The agent should internally use:

- sampled path corners
- path index / current segment
- repath timer or dirty flag
- simple local steering and stopping logic

## Implementation Phases

### Phase 1: single-agent path following

Files:

- new `src/runtime/graphics/rt_navagent3d.h`
- new `src/runtime/graphics/rt_navagent3d.c`
- `src/runtime/graphics/rt_navmesh3d.h`
- `src/runtime/graphics/rt_navmesh3d.c`
- `src/il/runtime/runtime.def`
- `src/il/runtime/classes/RuntimeClasses.hpp`
- `src/il/runtime/RuntimeSignatures.cpp`
- `src/runtime/CMakeLists.txt`
- `src/runtime/graphics/rt_graphics_stubs.c`

Work:

- add agent object
- compute desired velocity along path corners
- expose remaining distance and stop tolerance

### Phase 2: character and scene bindings

Work:

- push desired movement into `Character3D`
- optionally update a bound scene node when no character exists

### Phase 3: repath and dynamic goals

Work:

- add auto-repath
- add path invalidation if the agent strays too far or the target moves

### Phase 4: local avoidance and links

Work:

- add lightweight agent-agent separation first
- add off-mesh links only after core movement is stable

## Testing

### Runtime tests

- agent follows a simple path and stops near the target
- moving target triggers repath when enabled
- bound `Character3D` consumes agent motion
- agent warps cleanly without stale path state

### Example coverage

- add an API-audit scene with one agent bound to a character controller

### Test files

- new `src/tests/runtime/RTNavAgent3DTests.cpp`
- update `src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp`

## Documentation

- add `NavAgent3D` to `docs/graphics3d-guide.md`
- show the recommended pattern: `NavMesh3D.Build -> NavAgent3D.New -> BindCharacter -> Update`

## Risks and Non-Goals

Risks:

- if agent movement and character controller collision resolution fight each other, navigation will jitter
- local avoidance can expand scope quickly

Non-goals:

- full RTS-scale crowd simulation in v1
- dynamic navmesh carving
