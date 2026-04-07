# Plan 07: SceneNode3D Bindings and Runtime Composition

## Goal

Remove the manual per-frame glue that currently ties together scene nodes, physics bodies, animation controllers, nav agents, and spatial audio.

This plan is intentionally not a full ECS. Viper already has a scene graph. The missing piece is a lightweight binding layer that lets a `SceneNode3D` stay connected to the systems that actually drive it.

## Verified Current State

- `SceneNode3D` currently owns transform, mesh, material, visibility, naming, and LOD data.
- There is no built-in binding to `Physics3DBody`, animation, nav, or audio.
- `3dbowling` manually reads body positions and rebuilds draw transforms for pins.

## API Shape

Extend `SceneNode3D` with explicit typed bindings and give `Scene3D` a single sync step.

### `SceneNode3D` additions

- `BindBody(body, syncMode)`
- `BindAnimator(controller)`
- `BindAudioSource(source)`
- `BindNavAgent(agent)`
- `ClearBodyBinding()`
- `ClearAnimatorBinding()`
- `ClearAudioBinding()`
- `ClearNavBinding()`
- `get_Body`
- `get_Animator`
- `get_AudioSource`
- `get_NavAgent`

### `Scene3D` addition

- `SyncBindings(dt)`

### Sync modes

Start with one simple enum:

- `0 = NodeFromBody`
- `1 = BodyFromNode`
- `2 = NodeFromAnimatorRootMotion`
- `3 = TwoWayKinematic`

The sync order must be documented and deterministic.

## Internal Design

Bindings should be weak references, not ownership transfers.

Recommended rules:

- `SceneNode3D` remains the transform owner for rendering
- bindings describe how external systems push or pull transforms
- `Scene3D.SyncBindings(dt)` is the explicit integration point before draw
- `Scene3D.Draw` should not secretly mutate simulation state

This keeps the frame pipeline understandable:

1. simulation updates
2. binding sync
3. rendering

## Implementation Phases

### Phase 1: body binding

Files:

- `src/runtime/graphics/rt_scene3d.h`
- `src/runtime/graphics/rt_scene3d.c`
- `src/il/runtime/runtime.def`
- `src/runtime/graphics/rt_graphics_stubs.c`

Work:

- add body binding fields to scene nodes
- implement `SyncBindings(dt)` for body/node sync
- handle parented nodes carefully so world/local transforms do not fight

### Phase 2: animation binding

Work:

- support animator attachment
- optionally consume root motion into node transform
- expose current controller pointer for gameplay access

### Phase 3: nav and audio bindings

Work:

- allow a nav agent or audio source to follow node/world transforms
- avoid duplicate transform pushes when body/nav/audio all exist

### Phase 4: prefab integration

Work:

- let `Model3D.Instantiate()` prewire known bindings where the source asset encodes them
- keep this opt-in and explicit in metadata

## Testing

### Runtime tests

- `NodeFromBody` updates node transform after physics step
- `BodyFromNode` updates a kinematic body correctly
- parent-child transforms remain correct when the child is body-bound
- audio/nav bindings do not create update cycles

### Example coverage

- update or add a small API-audit sample that binds a body and animator to a node

### Test files

- new `src/tests/runtime/RTScene3DBindingTests.cpp`
- update `src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp` if new exports are added

## Documentation

- expand the `Scene3D` / `SceneNode3D` section in `docs/graphics3d-guide.md`
- include a frame-order example showing `Physics -> SyncBindings -> Draw`

## Risks and Non-Goals

Risks:

- update-order bugs if `SyncBindings` is implicit
- parent-space versus world-space transform confusion

Non-goals:

- a generalized ECS
- script callback systems inside the scene graph
