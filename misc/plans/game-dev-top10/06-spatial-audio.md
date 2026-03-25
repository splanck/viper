# Feature 6: Spatial Audio + Reverb Zones

## Current Reality

`src/runtime/graphics/rt_audio3d.c` is already implemented. Today it provides:
- single global listener
- `Audio3D.SetListener(position, forward)`
- `Audio3D.PlayAt(sound, position, maxDistance, volume)`
- `Audio3D.UpdateVoice(voice, position, maxDistance)`
- linear attenuation
- simple left/right pan

So this feature is an extension plan, not a stub replacement.

## Problem

The current helper is useful but limited:
- only one attenuation model
- no min-distance support
- no richer listener orientation
- no persistent source object
- no environmental effects

## Corrected Scope

### Phase 1 API: extend current Audio3D

Keep the existing API source-compatible and add richer helpers:

```text
Audio3D.SetListener(position, forward)
Audio3D.SetListenerFull(position, forward, up)
Audio3D.PlayAt(sound, position, maxDistance, volume)
Audio3D.PlayAtEx(sound, position, minDistance, maxDistance, model, volume) -> Integer
Audio3D.UpdateVoice(voice, position, maxDistance)
Audio3D.UpdateVoiceEx(voice, position, minDistance, maxDistance, model, baseVolume)
```

Optional wrapper object after the helper path is stable:

```text
AudioSource3D.New(sound) -> AudioSource3D
source.PlayAt(position)
source.Update(position)
source.Stop()
```

### Reverb Zones

Reverb zones are valid as a feature goal, but they require a mixer/voice effect hook.
That should be treated as an explicit prerequisite, not assumed to already exist.

## Implementation

### Phase 1: strengthen current Audio3D (2-3 days)

- Modify `src/runtime/graphics/rt_audio3d.c` + `rt_audio3d.h`
- Add:
  - min distance
  - multiple attenuation models
  - listener `up` support
  - explicit volume parameter on voice updates
- Keep the current `PlayAt` / `UpdateVoice` behavior working

### Phase 2: optional AudioSource3D object (1-2 days)

- Add a small runtime object that wraps:
  - retained sound reference
  - voice id
  - attenuation settings
  - world position
- This is convenience API only; it should sit on top of the existing helper path

### Phase 3: reverb zones, only after mixer support exists (2-4 days)

Prerequisite:
- per-voice DSP hook or effect-send path in the audio mixer / `vaud`

Without that prerequisite, a “reverb zone” plan is just design fiction.

## Viper-Specific Notes

- File location is `src/runtime/graphics/rt_audio3d.c`, not `src/runtime/audio/`
- Namespace is `Viper.Graphics3D.Audio3D`
- The current implementation stores a single listener and computes simple pan from the listener right vector
- `docs/graphics3d-guide.md` already documents the existing `Audio3D` helper and should be updated rather than replaced

## Runtime Registration

- Extend the existing `Viper.Graphics3D.Audio3D` static API
- Add `AudioSource3D` only if the wrapper object is actually implemented

## Files

| File | Action |
|------|--------|
| `src/runtime/graphics/rt_audio3d.c` | Modify |
| `src/runtime/graphics/rt_audio3d.h` | Modify |
| `src/runtime/graphics/rt_graphics_stubs.c` | Extend stubs if new API is added |
| `src/il/runtime/runtime.def` | Extend `Audio3D`; optionally add `AudioSource3D` |
| `src/tests/runtime/RTAudio3DTests.cpp` | New / extend |

## Documentation Updates

- Update `docs/graphics3d-guide.md`
- Update `docs/viperlib/audio.md`
- Update `docs/viperlib/README.md`

## Cross-Platform Requirements

- Audio backend behavior still flows through the existing `vaud` layer
- Reverb work must declare its mixer prerequisite explicitly
- Runtime wiring belongs in `src/runtime/CMakeLists.txt`
