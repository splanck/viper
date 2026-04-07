# Plan 10: AudioSource3D and AudioListener3D

## Goal

Replace the current manual spatial-audio workflow with object-based APIs that fit the rest of the 3D runtime.

The low-level `Audio3D.SetListener`, `PlayAt`, and `UpdateVoice` functions are useful building blocks, but they are too thin for sustained game use.

## Verified Current State

- `rt_audio3d.h` exposes only three functions.
- the runtime keeps one global listener state.
- the earlier per-voice max-distance bug was fixed, but the public API is still static and manual.
- `3dbowling` pushes listener state from the camera every frame itself.

## Status of Older Plans

`misc/plans/3d/07-audio3d-fixes.md` is complete for the bug it targeted. It did not add the missing game-facing object model. This plan is the next step.

## API Shape

Add two new runtime classes while preserving the existing static functions as wrappers.

### `AudioListener3D`

- `AudioListener3D.New()`
- `SetPosition(position)`
- `SetForward(forward)`
- `SetVelocity(velocity)`
- `BindNode(node)`
- `BindCamera(camera)`

Properties:

- `Position`
- `Forward`
- `Velocity`
- `IsActive`

### `AudioSource3D`

- `AudioSource3D.New(sound)`
- `Play()`
- `Stop()`
- `Pause()`
- `Resume()`
- `BindNode(node)`
- `SetPosition(position)`
- `SetVelocity(velocity)`

Properties:

- `Position`
- `Velocity`
- `MaxDistance`
- `Volume`
- `Pitch`
- `Looping`
- `IsPlaying`
- `VoiceId`

### `Audio3D` compatibility

- keep `SetListener`, `PlayAt`, and `UpdateVoice`
- implement them internally in terms of the new object model where practical

## Internal Design

Recommended approach:

- keep one active listener globally, but make it an actual `AudioListener3D` object
- let each `AudioSource3D` manage one runtime voice handle
- factor attenuation/panning math into pure helpers so it can be tested headlessly

This is the right integration point for later features like:

- doppler
- rolloff curves
- cones
- occlusion
- reverb sends

## Implementation Phases

### Phase 1: object wrappers

Files:

- new `src/runtime/graphics/rt_audio3d_listener.h`
- new `src/runtime/graphics/rt_audio3d_listener.c`
- new `src/runtime/graphics/rt_audio3d_source.h`
- new `src/runtime/graphics/rt_audio3d_source.c`
- `src/runtime/graphics/rt_audio3d.h`
- `src/runtime/graphics/rt_audio3d.c`
- `src/il/runtime/runtime.def`
- `src/il/runtime/classes/RuntimeClasses.hpp`
- `src/il/runtime/RuntimeSignatures.cpp`
- `src/runtime/CMakeLists.txt`
- `src/runtime/graphics/rt_graphics_stubs.c`

Work:

- add listener/source objects
- keep one active listener
- bridge the old static calls through the new helpers

### Phase 2: scene bindings

Work:

- support node/camera following
- integrate with `Scene3D.SyncBindings(dt)` when available

### Phase 3: motion-aware audio

Work:

- add velocity-driven doppler support
- add configurable rolloff curves
- add cones for directional sources

### Phase 4: environment polish

Work:

- add occlusion queries against the physics world
- add optional reverb-send routing if the audio runtime supports it cleanly

## Testing

### Runtime tests

- listener/source position updates feed attenuation and pan helpers correctly
- binding updates follow node/camera transforms
- old static API still routes through valid code paths

### Headless strategy

Because CI audio hardware may be unavailable:

- isolate spatial math into pure functions with ordinary unit tests
- keep link-surface tests for exported entry points
- reserve live-audio smoke tests for environments where audio init is available

### Test files

- new `src/tests/runtime/RTAudio3DTests.cpp`
- update `src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp`

## Documentation

- add `AudioListener3D` and `AudioSource3D` sections to `docs/graphics3d-guide.md`
- update the existing `Audio3D` section to describe it as the low-level compatibility layer

## Risks and Non-Goals

Risks:

- hidden voice-handle lifetime bugs if source objects and low-level calls both mutate the same handles
- listener-follow integration can become order-dependent if mixed with scene bindings carelessly

Non-goals:

- full DAW-style mixer graph redesign
- HRTF or platform-specific binaural pipelines in v1
