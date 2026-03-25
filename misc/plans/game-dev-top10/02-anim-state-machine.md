# Feature 2: Animation State Machine

## Current Reality

Both pieces already exist:
- `Viper.Game.StateMachine`
- `Viper.Game.SpriteAnimation`

They are frame-based, not millisecond-based:
- `StateMachine.Update()` advances one frame
- `SpriteAnimation.Update()` advances one frame and uses `FrameDuration` internally

So this feature should be a thin gameplay helper that preserves those semantics.

## Problem

Games currently hand-wire the same boilerplate repeatedly:
- transition state
- change animation clip
- reset clip timing
- inspect `JustEntered`, `JustExited`, and `FrameChanged`

That logic belongs in a reusable helper.

## Corrected Scope

### New Class

`Viper.Game.AnimStateMachine`

```text
AnimStateMachine.New() -> AnimStateMachine
AnimStateMachine.AddState(stateId, startFrame, endFrame, frameDuration, loop)
AnimStateMachine.SetInitial(stateId)
AnimStateMachine.Transition(stateId) -> Boolean
AnimStateMachine.Update()
AnimStateMachine.ClearFlags()

anim.CurrentState -> Integer
anim.PreviousState -> Integer
anim.JustEntered -> Boolean
anim.JustExited -> Boolean
anim.FramesInState -> Integer
anim.CurrentFrame -> Integer
anim.IsAnimFinished -> Boolean
anim.Progress -> Integer
```

Not in v1:
- `Update(dtMs)`
- rule language / condition variables
- crossfades

Those can be phase-2 work after the simple wrapper exists.

## Implementation

### Phase 1: Wrapper around existing runtime classes (1-2 days)

- Add `src/runtime/collections/rt_animstate.c` + `rt_animstate.h`
- Internally own:
  - a retained `StateMachine`
  - a retained `SpriteAnimation`
- Store a compact clip table keyed by `stateId`
- On `Transition(stateId)`:
  - call the internal state machine
  - reconfigure/reset the internal sprite animation to the mapped clip
- On `Update()`:
  - call `StateMachine.Update()`
  - call `SpriteAnimation.Update()`

### Phase 2: polish and validation (1-2 days)

- Surface the wrapper properties
- Add integration coverage against a simple idle/walk/jump controller
- Decide later whether auto-rules are worth adding or whether users should keep that logic in gameplay code

## Viper-Specific Notes

- Keep this feature in `src/runtime/collections`, not graphics
- Match the current edge-flag conventions:
  - `JustEntered` / `JustExited` stay latched until `ClearFlags()`
  - animation frame changes remain frame-based
- Do not introduce `dtMs` into an otherwise frame-counted subsystem

## Runtime Registration

Register under `Viper.Game.AnimStateMachine` using current `RT_FUNC` plus
`RT_CLASS_BEGIN` style in `runtime.def`.

## Files

| File | Action |
|------|--------|
| `src/runtime/collections/rt_animstate.c` | New |
| `src/runtime/collections/rt_animstate.h` | New |
| `src/il/runtime/runtime.def` | Add class |
| `src/tests/runtime/RTAnimStateTests.cpp` | New |

## Documentation Updates

- Update `docs/viperlib/game/animation.md`
- Update `docs/viperlib/game/README.md`
- Update `docs/viperlib/README.md`
- Update `docs/codemap/runtime-library-c.md`

## Cross-Platform Requirements

- Pure computation
- No platform guards beyond normal runtime build inclusion
- Runtime wiring belongs in `src/runtime/CMakeLists.txt`
