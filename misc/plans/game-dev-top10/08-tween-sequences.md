# Feature 8: Tween Sequence Chaining

## Current Reality

`Viper.Game.Tween` already exists and is frame-based:
- `Start(...)`
- `StartI64(...)`
- `Update()`
- `Value`, `ValueI64`, `Progress`, `IsRunning`, `IsComplete`

The original plan mixed millisecond timing into a frame-counted system. That would be a
bad fit for the current runtime.

## Problem

Games need chained transitions such as:
- fade in -> hold -> fade out
- move -> scale -> settle
- intro staging for UI or enemies

One `Tween` object cannot represent that sequence cleanly.

## Corrected Scope

### New Class

`Viper.Game.TweenSeq`

```text
TweenSeq.New() -> TweenSeq

seq.Then(from, to, frames, easing)
seq.ThenI64(from, to, frames, easing)
seq.Wait(frames)
seq.Play()
seq.Stop()
seq.Reset()
seq.SetLoop(count)
seq.Update()

seq.Value -> Number
seq.ValueI64 -> Integer
seq.IsRunning -> Boolean
seq.IsComplete -> Boolean
seq.Progress -> Integer
```

v1 is sequential only.

Not v1:
- `Update(dt)`
- millisecond timing
- callbacks
- unconstrained `Together(...)` / parallel graphs

Those can be added later once the simple sequence object is proven.

## Implementation

### Phase 1: sequential steps (1-2 days)

- Add `src/runtime/collections/rt_tweenseq.c` + `rt_tweenseq.h`
- Store a fixed or growable list of steps:
  - tween step
  - wait step
- Reuse the current easing helpers from `rt_tween.c`
- Keep naming aligned with `Tween`:
  - `Then`
  - `ThenI64`
  - `Value`
  - `ValueI64`

### Phase 2: tests and docs (1 day)

- Verify exact frame progression
- Verify loop behavior
- Verify integer rounding matches `Tween`

## Viper-Specific Notes

- Keep this in `src/runtime/collections`
- Use frame counting, not milliseconds
- Runtime wiring belongs in `src/runtime/CMakeLists.txt`

## Runtime Registration

Add `Viper.Game.TweenSeq` using the current `runtime.def` style.

## Files

| File | Action |
|------|--------|
| `src/runtime/collections/rt_tweenseq.c` | New |
| `src/runtime/collections/rt_tweenseq.h` | New |
| `src/il/runtime/runtime.def` | Add class |
| `src/tests/runtime/RTTweenSeqTests.cpp` | New |

## Documentation Updates

- Update `docs/viperlib/game/animation.md`
- Update `docs/viperlib/game/README.md`
- Update `docs/viperlib/README.md`

## Cross-Platform Requirements

- Pure computation
- No platform guards beyond normal build inclusion
