# Plan 06: AnimController3D

## Goal

Add a high-level character animation controller on top of `Skeleton3D`, `Animation3D`, `AnimPlayer3D`, and `AnimBlend3D`.

The current runtime can sample and blend clips, but it does not yet provide the stateful control layer that gameplay code expects:

- state transitions
- clip events
- root motion
- layered playback
- optional bone masks

## Verified Current State

- `AnimPlayer3D` supports play, stop, update, speed, time, and crossfade.
- `AnimBlend3D` supports manual weight-based blending of named states.
- There is no 3D-specific state graph, event system, root-motion extraction, or layer/mask API.
- `Viper.Game.AnimStateMachine` exists, but it is a separate generic game runtime surface, not a 3D skeleton controller.

## Status of Older Plans

`misc/plans/3d/10-animation-state-machine.md` did not land. This plan supersedes it, but the naming should integrate with the existing runtime better:

- do not introduce an isolated 3D FSM that ignores `AnimPlayer3D` and `AnimBlend3D`
- reuse those internals and mirror existing Viper state-machine naming where it helps

## API Shape

Introduce `Viper.Graphics3D.AnimController3D`.

### Core methods

- `AnimController3D.New(skeleton)`
- `AddState(name, animation)`
- `AddTransition(fromState, toState, blendSeconds)`
- `Play(stateName)`
- `Crossfade(stateName, blendSeconds)`
- `Update(dt)`
- `Stop()`

### State/query properties

- `CurrentState`
- `PreviousState`
- `IsTransitioning`
- `StateCount`

### Playback controls

- `SetStateSpeed(name, speed)`
- `SetStateLooping(name, loop)`
- `SetLayerWeight(layer, weight)`
- `SetLayerMask(layer, rootBone)`

### Events and root motion

- `AddEvent(stateName, timeSeconds, eventName)`
- `PollEvent()`
- `get_RootMotionDelta`
- `ConsumeRootMotion()`

Compatibility note:

- `AnimPlayer3D` remains the low-level clip player
- `AnimBlend3D` remains the low-level manual blender
- `AnimController3D` becomes the recommended gameplay-facing API

## Internal Design

Recommended structure:

- `AnimController3D` owns one skeleton binding
- internally it uses existing animation sampling code from `AnimPlayer3D`
- transitions are evaluated into one or more blend layers
- root motion is extracted from a designated bone, usually the skeleton root

Do not duplicate sampling math. The controller should be policy and orchestration, not a second animation engine.

## Implementation Phases

### Phase 1: controller wrapper over current sampling

Files:

- new `src/runtime/graphics/rt_animcontroller3d.h`
- new `src/runtime/graphics/rt_animcontroller3d.c`
- `src/runtime/graphics/rt_skeleton3d.h`
- `src/runtime/graphics/rt_skeleton3d.c`
- `src/il/runtime/runtime.def`
- `src/il/runtime/classes/RuntimeClasses.hpp`
- `src/il/runtime/RuntimeSignatures.cpp`
- `src/runtime/CMakeLists.txt`
- `src/runtime/graphics/rt_graphics_stubs.c`

Work:

- add controller object
- reuse clip sampling and blend logic already present in `rt_skeleton3d.c`
- support imperative play/crossfade/state lookup

### Phase 2: events and root motion

Work:

- store per-state event tables
- expose polling API instead of callbacks
- extract root motion delta each update

### Phase 3: layers and masks

Work:

- add upper-body/lower-body blending support
- define one simple bone-mask selection rule first, then generalize if needed

### Phase 4: optional IK hook points

Work:

- add extension hooks for two-bone IK or look-at constraints
- keep IK as a follow-on feature, not a blocker for the controller itself

## Testing

### Runtime tests

- transition timing and state changes are deterministic
- events fire once at the correct time
- root motion delta accumulates and clears correctly
- masked layers affect only the targeted subtree

### Integration tests

- imported animated model can be driven via `AnimController3D`
- root motion can drive a scene-node or physics binding path without double application

### Test files

- new `src/tests/runtime/RTAnimController3DTests.cpp`
- extend existing skeleton/animation tests
- update `src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp`

## Documentation

- add an `AnimController3D` section to `docs/graphics3d-guide.md`
- explain when to use `AnimPlayer3D`, `AnimBlend3D`, and `AnimController3D`

## Risks and Non-Goals

Risks:

- root motion can become wrong if the same motion is also applied through scene bindings
- layered blending can balloon complexity if masks are too flexible too early

Non-goals:

- editor-authored animation graphs
- full retargeting in v1
- a separate "AnimStateMachine3D" type with redundant sampling code
