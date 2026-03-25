# Feature 10: Animation Events / Notifies

## Current Reality

The runtime already has:
- `Viper.Game.SpriteAnimation` with `FrameChanged`
- `Viper.Graphics3D.AnimPlayer3D` with `Update(dt)` and `Time`

So this feature should extend those existing classes using their current timing models:
- 2D: frame-based
- 3D: time-based (`f64`, same units as `AnimPlayer3D.Time`)

## Problem

Gameplay often needs animation-driven hooks:
- footsteps
- attack windows
- projectile spawn moments
- landing events

Without events, every game recreates the same timing logic externally.

## Corrected Scope

### SpriteAnimation Extensions

```text
anim.AddEvent(frame, name)
anim.RemoveEvent(frame, name)
anim.ClearEvents()
anim.LastEvent -> String
anim.EventFired -> Boolean
```

### AnimPlayer3D Extensions

```text
player.AddEvent(time, name)
player.RemoveEvent(time, name)
player.ClearEvents()
player.LastEvent -> String
player.EventFired -> Boolean
```

v1 should keep a simple “last fired event” edge-flag model that matches the current
runtime style.

## Implementation

### Phase 1: SpriteAnimation events (1 day)

- Modify `src/runtime/collections/rt_spriteanim.c` + `rt_spriteanim.h`
- Store a compact list of frame events
- Clear `EventFired` at the start of `Update()`
- Fire when `Update()` crosses an event frame

### Phase 2: AnimPlayer3D events (1 day)

- Modify `src/runtime/graphics/rt_skeleton3d.c` + `rt_skeleton3d.h`
- Use event times in the same units as `AnimPlayer3D.Time`
- Fire when playback crosses an event time between updates

### Phase 3: tests and examples (1-2 days)

- Verify no duplicate firing without a crossing
- Verify skipped-frame / skipped-time detection
- Add a footstep or attack-window example

## Viper-Specific Notes

- `SpriteAnimation.Update()` takes no `dt`; examples must show `Update()`, not `Update(dt)`
- `SpriteAnimation.Setup(start, end, frameDuration)` uses frame duration, not milliseconds
- `AnimPlayer3D` uses `f64` time values and `Update(dt)`
- Follow the current edge-flag style already used by `FrameChanged`

## Runtime Registration

Extend:
- `Viper.Game.SpriteAnimation`
- `Viper.Graphics3D.AnimPlayer3D`

Use current `runtime.def` style.

## Files

| File | Action |
|------|--------|
| `src/runtime/collections/rt_spriteanim.c` | Modify |
| `src/runtime/collections/rt_spriteanim.h` | Modify |
| `src/runtime/graphics/rt_skeleton3d.c` | Modify |
| `src/runtime/graphics/rt_skeleton3d.h` | Modify |
| `src/il/runtime/runtime.def` | Add methods/properties |
| `src/tests/runtime/RTAnimEventTests.cpp` | New |

## Documentation Updates

- Update `docs/viperlib/game/animation.md`
- Update `docs/graphics3d-guide.md`
- Update `docs/viperlib/README.md`

## Cross-Platform Requirements

- Pure computation
- No backend-specific code
- Runtime wiring belongs in `src/runtime/CMakeLists.txt`
