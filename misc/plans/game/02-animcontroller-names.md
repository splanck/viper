# Plan 02: AnimStateMachine String Names + Frame Events

## Context

AnimStateMachine (rt_animstate.c) exists with AddState(int_id, start, end, dur, loop),
Transition(int_id), Update(), JustEntered/JustExited. Max 32 states.
But states are integer-only — the XENOSCAPE demo maps constants manually and doesn't
use AnimStateMachine because the int-only API is awkward. Adding string names makes it
usable.

## Changes

### rt_animstate.c — extend clip struct + add name lookup (~80 LOC)

**Add name field to anim_clip_t (line 42):**
```c
typedef struct {
    int64_t state_id;
    int64_t start_frame;
    int64_t end_frame;
    int64_t frame_duration;
    int8_t loop;
    int8_t valid;
    char name[32];  // NEW: string name (null-terminated, max 31 chars)
} anim_clip_t;
```

**New function: AddNamedState**
```c
void rt_animstate_add_named(animstate_impl *sm, rt_string name,
                            int64_t start, int64_t end, int64_t dur, int8_t loop) {
    int64_t id = sm->clip_count; // auto-assign ID
    rt_animstate_add_state(sm, id, start, end, dur, loop);
    strncpy(sm->clips[id].name, rt_string_cstr(name), 31);
}
```

**New function: TransitionByName**
```c
void rt_animstate_transition_named(animstate_impl *sm, rt_string name) {
    const char *cname = rt_string_cstr(name);
    for (int i = 0; i < sm->clip_count; i++) {
        if (strcmp(sm->clips[i].name, cname) == 0) {
            rt_animstate_transition(sm, sm->clips[i].state_id);
            return;
        }
    }
}
```

**New function: GetStateName**
```c
rt_string rt_animstate_current_name(animstate_impl *sm);
```

**New function: SetFrameCallback (event system)**

Add a lightweight per-clip event: when animation reaches a specific frame, a flag is set.
```c
// New struct field in animstate_impl:
int64_t event_frame;     // Frame to trigger event on (-1 = none)
int8_t event_triggered;  // Flag set when event_frame is reached

void rt_animstate_set_event_frame(animstate_impl *sm, int64_t frame);
int8_t rt_animstate_event_fired(animstate_impl *sm); // check + auto-clear
```

### runtime.def — new entries
```
RT_FUNC(AnimStateAddNamed,      rt_animstate_add_named,        "Viper.Game.AnimStateMachine.AddNamed",      "void(obj,str,i64,i64,i64,i1)")
RT_FUNC(AnimStateTransNamed,    rt_animstate_transition_named, "Viper.Game.AnimStateMachine.Play",          "void(obj,str)")
RT_FUNC(AnimStateCurrentName,   rt_animstate_current_name,     "Viper.Game.AnimStateMachine.get_StateName", "str(obj)")
RT_FUNC(AnimStateSetEventFrame, rt_animstate_set_event_frame,  "Viper.Game.AnimStateMachine.SetEventFrame", "void(obj,i64)")
RT_FUNC(AnimStateEventFired,    rt_animstate_event_fired,      "Viper.Game.AnimStateMachine.EventFired",    "i1(obj)")
```

### Zia usage after change
```zia
var anim = AnimStateMachine.New()
anim.AddNamed("idle", 0, 3, 8, true)
anim.AddNamed("run", 4, 7, 6, true)
anim.AddNamed("jump", 8, 10, 4, false)
anim.Play("run")
anim.Update()
var frame = anim.get_CurrentFrame()
```

### Files to modify
- `src/runtime/game/rt_animstate.c` — extend clip struct, add 4 functions
- `src/runtime/game/rt_animstate.h` — add declarations
- `src/il/runtime/runtime.def` — register 5 new entries

### Tests

**File:** `src/tests/unit/runtime/TestAnimStateNamed.cpp`
```
TEST(AnimStateNamed, AddAndPlayByName)
  — AddNamed "walk" (0,3,8,true), Play("walk"), verify CurrentFrame starts at 0

TEST(AnimStateNamed, TransitionByName)
  — Add "idle" and "run", Play("idle"), transition to "run", verify JustEntered

TEST(AnimStateNamed, GetStateName)
  — Play("jump"), verify get_StateName returns "jump"

TEST(AnimStateNamed, UnknownNameNoOp)
  — Play("nonexistent"), verify no crash, state unchanged

TEST(AnimStateNamed, EventFrameFires)
  — AddNamed "attack" (0,5,4,false), SetEventFrame(3), update until frame 3, verify EventFired

TEST(AnimStateNamed, EventAutoClears)
  — Fire event, call EventFired (returns true), call again (returns false)
```

### Doc update
- `docs/viperlib/game/animstatemachine.md` — add AddNamed, Play, StateName, EventFrame sections
