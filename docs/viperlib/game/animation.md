---
status: active
audience: public
last-verified: 2026-07-15
---

# Animation & Movement

> `Tween`, `SpriteAnimation`, `AnimStateMachine`, `AnimTimeline`,
> `AnimationEventBatch`, `SpriteSheet`, `PathFollower`, and `ButtonGroup`

**Part of [Viper Runtime Library](../README.md) › [Game Utilities](README.md)**

These helpers are state containers. Except for `SpriteSheet`, none of them draws anything. Call
their update method from the game loop, then use the resulting value, frame, position, event batch,
or selection when updating the rest of the game.

`Tween`, `SpriteAnimation`, `AnimStateMachine`, and `AnimTimeline` measure time in integer update
frames. `PathFollower` is the exception: its `Update(dt)` argument is elapsed milliseconds.

---

## Viper.Game.Tween

`Tween` interpolates one number from a start value to an end value over a fixed number of calls to
`Update()`.

- **Type:** instance class
- **Constructor:** `Tween.New()`
- **Initial state:** value 0, duration 0, inactive, incomplete, and unpaused

### Properties

| Property | Type | Access | Meaning |
|---|---|---|---|
| `Value` | `Double` | read | Current interpolated value |
| `ValueI64` | `Integer` | read | `Value` rounded half away from zero and saturated to the integer range |
| `IsRunning` | `Boolean` | read | True only while active and not paused |
| `IsComplete` | `Boolean` | read | True after natural completion; `Stop()` does not set it |
| `IsPaused` | `Boolean` | read | True after a running tween is paused |
| `Progress` | `Integer` | read | Truncated percentage from 0 through 100 |
| `Elapsed` | `Integer` | read | Number of update frames consumed |
| `Duration` | `Integer` | read | Configured duration in update frames |

### Methods

| Method | Return | Behavior |
|---|---|---|
| `Start(from, to, duration, ease)` | `Void` | Start/restart a double tween |
| `StartI64(from, to, duration, ease)` | `Void` | Convert integer endpoints to double, then start |
| `Update()` | `Boolean` | Advance one frame; true only on the call that completes naturally |
| `Pause()` | `Void` | Pause a running, incomplete tween |
| `Resume()` | `Void` | Clear pause; it does not restart a stopped or completed tween |
| `Stop()` | `Void` | Stop at the current value and clear pause, without marking complete |
| `Reset()` | `Void` | Rewind to the start value, clear completion/pause, and restart if configured |
| `Destroy()` | `Void` | Release the handle; receiver form is frontend sugar for the static target |

`Start` clamps a non-positive duration to one frame and replaces an invalid easing code with
linear easing. A non-finite `from` becomes 0; a non-finite `to` becomes the sanitized `from`.
Every successful update increments `Elapsed` once. The completion update pins `Value` exactly to
`to`, clears `IsRunning`, sets `IsComplete`, and returns true. Later updates return false.

`StartI64` and `LerpI64` internally convert integer endpoints to binary64. Integers whose magnitude
exceeds 2^53 may therefore change before interpolation; use the double API only when that loss is
acceptable. `ValueI64` itself saturates at the integer limits and returns zero for a non-finite
intermediate result.

### Static methods and easing codes

| Method | Return | Behavior |
|---|---|---|
| `LerpI64(from, to, t)` | `Integer` | Clamp finite `t` to 0–1, interpolate through double, then round |
| `Ease(t, type)` | `Double` | Apply one curve; non-finite/`t <= 0` returns 0 and `t >= 1` returns 1 |

An unknown `Ease` type returns the already-clamped linear `t`. The `Start` methods instead replace
an unknown type with 0 before storing it.

| Code | Curve | Code | Curve |
|---:|---|---:|---|
| 0 | linear | 10 | in exponential |
| 1 | in quadratic | 11 | out exponential |
| 2 | out quadratic | 12 | in/out exponential |
| 3 | in/out quadratic | 13 | in back |
| 4 | in cubic | 14 | out back |
| 5 | out cubic | 15 | in/out back |
| 6 | in/out cubic | 16 | in bounce |
| 7 | in sine | 17 | out bounce |
| 8 | out sine | 18 | in/out bounce |
| 9 | in/out sine |  |  |

Back curves can leave the 0–1 range between their exact endpoints. That overshoot also affects
`Value`.

### Zia example

```rust
module TweenDemo;

bind Viper.Game.Tween as Tween;
bind Viper.Terminal;

func start() {
    var move = Tween.New();
    move.StartI64(0, 100, 4, 2); // out quadratic

    while move.IsRunning {
        var completedNow = move.Update();
        SayInt(move.ValueI64);
        if completedNow {
            Say("complete");
        }
    }

    move.Reset(); // rewinds and starts again
    move.Update();
    move.Stop();  // holds the current value; IsComplete remains false
    SayBool(move.IsComplete);
}
```

---

## Viper.Game.SpriteAnimation

`SpriteAnimation` selects an integer frame index. It does not own an image or extract a sprite.

- **Type:** instance class
- **Constructor:** `SpriteAnimation.New()`
- **Defaults:** range 0–0, six updates per displayed frame, speed 1.0, loop enabled, ping-pong
  disabled, and stopped

### Properties

| Property | Type | Access | Meaning |
|---|---|---|---|
| `Frame` | `Integer` | read/write | Current frame, clamped to the configured range |
| `FrameDuration` | `Integer` | read/write | Update ticks per displayed frame; values below one become one |
| `FrameCount` | `Integer` | read | Inclusive configured range size |
| `IsPlaying` | `Boolean` | read | True while playing and not paused |
| `IsPaused` | `Boolean` | read | Pause flag |
| `IsFinished` | `Boolean` | read | True after a non-looping traversal completes |
| `Progress` | `Integer` | read | Current frame's truncated 0–100 position in the forward range |
| `Speed` | `Double` | read/write | Playback multiplier, clamped to 0.0–10.0 |
| `Loop` | `Boolean` | read/write | Wrap or continue ping-pong traversal |
| `PingPong` | `Boolean` | read/write | Traverse forward and then backward |
| `FrameChanged` | `Boolean` | read | Whether the latest `Update()` changed the visible frame at least once |

### Methods

| Method | Return | Behavior |
|---|---|---|
| `Setup(start, end, frameDuration)` | `Void` | Configure an inclusive forward-only range and rewind |
| `Play()` | `Void` | Start or restart from the configured first frame |
| `Stop()` | `Void` | Stop, rewind, and clear pause/finish state |
| `Pause()` | `Void` | Pause only if currently playing |
| `Resume()` | `Void` | Clear pause; it does not start a stopped animation |
| `Reset()` | `Void` | Rewind counters/frame without changing play or pause state |
| `Update()` | `Boolean` | Advance timing; true only on the update that finishes a one-shot traversal |
| `Destroy()` | `Void` | Release the handle |

`Setup` clamps a negative start to zero, clamps the end up to the start, and clamps duration to at
least one. It resets the frame, counters, direction, and finish flag but deliberately preserves
the play and pause flags. Reverse ranges cannot be configured here; `AnimStateMachine` does support
them.

Each `Update()` adds `Speed` to a fractional accumulator. Speed 0 freezes the visible frame while
the object remains playing. Values above 1 can consume several timing ticks and may cross multiple
frames in one call. A non-finite or negative speed becomes zero; values above 10 become 10.

For a non-looping forward clip, the last frame remains visible for its complete duration. The next
frame-step attempt marks the animation finished and returns true. A non-looping ping-pong clip
travels from start to end and back to start, then finishes at the start. Its `Progress` consequently
rises to 100 and falls back to 0; it is not a completion percentage in ping-pong mode. A one-frame
ping-pong clip completes on its first timed step.

### BASIC example

```basic
DIM walk AS OBJECT = Viper.Game.SpriteAnimation.New()
walk.Setup(0, 7, 6)
walk.Loop = TRUE
walk.Speed = 1.0
walk.Play()

FOR tick = 1 TO 12
    IF walk.Update() THEN PRINT "one-shot completed"
    IF walk.FrameChanged THEN PRINT walk.Frame
NEXT

walk.Pause()
PRINT walk.IsPaused
walk.Resume()
END
```

---

## Viper.Game.AnimStateMachine

`AnimStateMachine` maps state IDs to frame clips and combines transitions, playback, and
frame-keyed event IDs. It holds at most 32 clips.

- **Type:** instance class
- **Constructor:** `AnimStateMachine.New()`
- **Initial state:** no current/previous state (`-1`), frame 0, stopped

### Properties

| Property | Type | Access | Meaning |
|---|---|---|---|
| `CurrentState` | `Integer` | read | Current state ID, or -1 |
| `PreviousState` | `Integer` | read | State before the latest real transition, or -1 |
| `JustEntered` | `Boolean` | read | Latched transition flag |
| `JustExited` | `Boolean` | read | Latched flag; false for the initial state |
| `FramesInState` | `Integer` | read | Number of `Update()` calls in the state, saturating at the integer maximum |
| `CurrentFrame` | `Integer` | read | Current clip frame |
| `IsAnimFinished` | `Boolean` | read | Whether the current non-looping clip has completed |
| `Progress` | `Integer` | read | Truncated 0–100 position through either a forward or reverse clip |
| `StateName` | `Object` | read | Runtime string for a named state, otherwise an empty string |

`StateName` is registered as bare `Object` even though the runtime returns a string. In Zia, assign
it to an explicitly typed string when string operations are needed.

### Methods

| Method | Return | Behavior |
|---|---|---|
| `AddState(id, start, end, duration, loop)` | `Void` | Add or replace a numeric clip |
| `AddNamed(name, start, end, duration, loop)` | `Void` | Add a clip with an automatically assigned ID and copied name |
| `SetInitial(id)` | `Boolean` | Select a registered starting state |
| `Transition(id)` | `Boolean` | Switch to a registered state |
| `Play(name)` | `Void` | Transition to the first registered clip with that name |
| `Update()` | `Void` | Advance the state counter and at most one animation frame |
| `ClearFlags()` | `Void` | Clear `JustEntered` and `JustExited` |
| `AddEvent(stateId, frame, eventId)` | `Boolean` | Add one event to a frame in that clip |
| `ClearEvents(stateId)` | `Void` | Remove all multi-events from one clip |
| `PollEvents()` | `AnimationEventBatch` | Snapshot IDs fired by the most recent update |
| `SetEventFrame(frame)` | `Void` | Configure the obsolete single-event path; see warning below |

### Registration and transition rules

- Numeric state IDs must be non-negative. A negative ID is ignored.
- Negative start/end frames are independently clamped to zero. `start > end` creates a supported
  reverse clip. Duration is clamped to at least one update per frame.
- Re-registering an ID overwrites its clip and clears that clip's events. If it is already active,
  the current playback fields are not reapplied until a later transition.
- Adding a 33rd distinct state traps. Each state holds at most eight events; a ninth returns false.
- `SetInitial` sets `JustEntered`, clears the previous state, and starts the clip. Unknown IDs
  return false.
- `Transition` to the current ID returns true but is a no-op: it neither restarts playback nor
  relatches flags. A real transition resets the frame and event masks.
- Transition flags remain true until `ClearFlags()`; reading them does not consume them.

`AddNamed` copies at most 31 bytes of the name. The current auto-ID algorithm uses the clip count,
not an unused-ID search. Mixing sparse numeric IDs with named registration can overwrite a numeric
clip and store the name into the wrong slot. Until that defect is fixed, use either sequential
numeric states starting at zero or named states on a fresh machine; do not mix the two styles.

### Playback and events

`Update()` does nothing before `SetInitial`/`Play`. Once active, `FramesInState` increases every
call, including while a one-shot is already finished. Animation playback advances at most one
frame per call. Like `SpriteAnimation`, a one-shot finishes only when the runtime attempts to step
past the terminal frame after that frame's configured duration.

`AddEvent` accepts only a frame inside the target clip, including reverse clips. Events are checked
when playback crosses a frame, returned in insertion order, and debounced for the current clip
cycle. A looping wrap clears the masks so they can fire next cycle. Merely entering a state does
not fire an event attached to its starting frame. Call `PollEvents()` after each `Update()` before
the next update replaces the producer's current event list.

`SetEventFrame` remains in the public registry, but its only observer (`EventFired`) is no longer
registered. It has no usable public result. Use `AddEvent` and `PollEvents` instead. The former
`EventsFiredCount` and `EventFiredId` compatibility members are also no longer public.

### Viper.Game.AnimationEventBatch

The result of `PollEvents()` is an immutable copy that remains valid after later animation updates.

| Member | Return | Behavior |
|---|---|---|
| `Count` | `Integer` | Number of copied IDs |
| `GetId(index)` | `Integer` | ID at a valid zero-based index; otherwise 0 |
| `Contains(eventId)` | `Boolean` | Linear membership test |
| `Ids()` | `Object` | New owned `Seq` of boxed integers |

`Ids()` is registered as bare `Object`, not `Viper.Collections.Seq`. `GetId` is the most portable
way to iterate. The integer 0 is a valid event ID, so use `Count` to distinguish it from an invalid
index.

### Zia example

```rust
module AnimStateDemo;

bind Viper.Game.AnimStateMachine as AnimStateMachine;
bind Viper.Terminal;

final IDLE = 0;
final ATTACK = 1;
final HIT_EVENT = 40;

func start() {
    var machine = AnimStateMachine.New();
    machine.AddState(IDLE, 0, 3, 2, true);
    machine.AddState(ATTACK, 8, 10, 1, false);
    machine.AddEvent(ATTACK, 9, HIT_EVENT);
    machine.SetInitial(IDLE);
    machine.ClearFlags();

    machine.Transition(ATTACK);
    var tick = 0;
    while tick < 4 {
        machine.Update();
        var events = machine.PollEvents();
        if events.Contains(HIT_EVENT) {
            Say("apply hit");
        }
        tick = tick + 1;
    }

    SayBool(machine.IsAnimFinished);
    machine.ClearFlags();
}
```

---

## Viper.Game.AnimTimeline

`AnimTimeline` is a passive frame scheduler. It stores track spans and payload integers, advances a
playhead, and reports crossed marker IDs. It does **not** own or update a `Tween`,
`SpriteAnimation`, or `AnimStateMachine`; application code must query the timeline and drive those
objects itself.

- **Type:** instance class
- **Constructor:** `AnimTimeline.New(totalDurationFrames)`
- **Capacity:** 16 tracks and 32 markers
- **Initial state:** frame 0, stopped, non-looping, unfinished

The total duration is clamped to at least one. It is not exposed as a property.

### Properties

| Property | Type | Access | Meaning |
|---|---|---|---|
| `IsPlaying` | `Boolean` | read | Whether positive advances can move the playhead |
| `IsFinished` | `Boolean` | read | Whether non-looping playback reached the end |
| `CurrentFrame` | `Integer` | read | Current frame, 0 through total duration for non-looping playback |
| `Looping` | `Boolean` | write only | Enable modulo wrap at the total duration |

There is no public getter for `Looping`.

### Methods

| Method | Return | Behavior |
|---|---|---|
| `AddAnimTrack(name, start, duration, stateId)` | `Integer` | Add a span whose payload A is `stateId` |
| `AddTweenTrack(name, start, duration, from, to)` | `Integer` | Add a span with A=`from`, B=`to`, C=0 |
| `AddMarker(frame, eventId)` | `Integer` | Add a marker |
| `Play()` | `Void` | Start/resume at the current frame and clear finish state |
| `Pause()` | `Void` | Stop advancing without rewinding |
| `Stop()` | `Void` | Stop, rewind to zero, and reset marker fired flags |
| `Advance(deltaFrames)` | `Void` | Move forward by a positive integer amount |
| `PollEvents()` | `AnimationEventBatch` | Snapshot marker IDs from the latest advance |
| `TrackIsActive(index)` | `Boolean` | Test the track's half-open active span |
| `TrackProgress(index)` | `Double` | Return its clamped 0.0–1.0 playhead position |
| `TrackPayloadA/B/C(index)` | `Integer` | Read stored payload slots; invalid indices return 0 |

Track starts are clamped to zero, durations to at least one, and names are copied into 31-byte
internal fields. Names have no public query. The add methods return insertion-order indices or -1
at capacity. Track spans are half-open: `[start, start + duration)`. `TrackProgress` is zero at and
before the start and one at and after the end.

The current implementation never updates payload C. For tween tracks it remains zero at every
playhead position; callers must calculate the interpolated value from payload A, payload B, and
`TrackProgress`, or use a real `Tween`. Anim tracks similarly only expose a state ID; they do not
transition a state machine.

### Playback and marker rules

`Advance` first clears the latest marker list, even when its delta is non-positive or playback is
paused. A non-looping advance that reaches the duration leaves `CurrentFrame` equal to the duration,
sets finished, and stops. `Play()` at that terminal frame does not rewind, so the next advance
finishes immediately; call `Stop()` before replaying from the start.

Markers fire when crossed in `(oldFrame, newFrame]` and are returned in marker insertion order.
Consequences of the current implementation include:

- A marker at frame zero does not fire on initial playback. It can fire on a looping wrap.
- Markers beyond the total duration are accepted but cannot fire.
- A single looping `Advance` spanning several cycles reports each marker at most once.
- Loop wrap resets all marker flags, allowing markers to fire on later advances.

The former mutable `EventsFiredCount` and `EventFiredId` C entry points are not in the current
public registry. Use `PollEvents()`.

### Zia example

```rust
module TimelineDemo;

bind Viper.Game.AnimTimeline as AnimTimeline;
bind Viper.Game.Tween as Tween;
bind Viper.Terminal;

final SWAP_SCENE = 90;

func start() {
    var timeline = AnimTimeline.New(60);
    var track = timeline.AddTweenTrack("panel-x", 10, 20, 0, 300);
    timeline.AddMarker(30, SWAP_SCENE);
    timeline.Play();
    timeline.Advance(15);

    if timeline.TrackIsActive(track) {
        var x = Tween.LerpI64(
            timeline.TrackPayloadA(track),
            timeline.TrackPayloadB(track),
            timeline.TrackProgress(track));
        SayInt(x);
    }

    timeline.Advance(15);
    var events = timeline.PollEvents();
    SayBool(events.Contains(SWAP_SCENE));
}
```

---

## Viper.Graphics.SpriteSheet

`SpriteSheet` belongs to `Viper.Graphics`, not `Viper.Game`. It retains one `Pixels` atlas and maps
names to rectangular regions. Extraction creates a new independent `Pixels` buffer each time.

- **Type:** instance class
- **Constructors:** `SpriteSheet.New(atlas)` and `SpriteSheet.FromGrid(atlas, width, height)`
- **Initial region capacity:** 16, growing as needed

### Properties and methods

| Member | Return/access | Behavior |
|---|---|---|
| `RegionCount` | `Integer`, read | Number of named regions |
| `Width` | `Integer`, read | Atlas width |
| `Height` | `Integer`, read | Atlas height |
| `SetRegion(name, x, y, w, h)` | `Void` | Add or replace a valid in-bounds rectangle |
| `GetRegion(name)` | `Object` | New `Pixels` copy, or null when missing/allocation fails |
| `HasRegion(name)` | `Boolean` | Whether the name exists |
| `RegionNames()` | `Object` | New owned `Seq` of names in insertion order |
| `RemoveRegion(name)` | `Boolean` | Remove a region while preserving remaining order |

The two object-returning methods are not registered with their concrete `Pixels`/`Seq` result
types. In Zia, call an explicit class accessor such as `Pixels.get_Width(value)` or
`Seq.get_Count(value)` when member inference cannot recover the erased type.

Both constructors require an actual `Pixels` handle. `New` returns null for a null or wrong-class
atlas. `FromGrid` additionally requires positive cell dimensions and exact divisibility in both
directions. It names cells `"0"`, `"1"`, and so on in row-major order.

`SetRegion` silently ignores a null/empty name, non-positive dimensions, negative coordinates, or
a rectangle outside the atlas. Replacing a name keeps its insertion position. Adding copies the
name; it does not retain the caller's string. There is no Boolean success result, and name-allocation
failure is also silent.

`GetRegion` allocates and copies the rectangle on every call. The returned pixels do not share
storage with the atlas, so modifications in either object do not affect the other. Cache extracted
regions if they are used every frame.

### Zia example

```rust
module SpriteSheetDemo;

bind Viper.Graphics;
bind Viper.Collections.Seq as Seq;
bind Viper.Terminal;

func start() {
    var atlas = Pixels.New(64, 32);
    atlas.Fill(Color.RGB(255, 0, 0));

    var sheet = SpriteSheet.FromGrid(atlas, 16, 16);
    SayInt(sheet.RegionCount); // 8

    var frame = sheet.GetRegion("0");
    SayInt(Pixels.get_Width(frame));

    var names = sheet.RegionNames();
    SayInt(Seq.get_Count(names));
    sheet.RemoveRegion("7");
    SayBool(sheet.HasRegion("7"));
}
```

---

## Viper.Game.PathFollower

`PathFollower` moves a fixed-point position along up to 64 waypoints.

- **Type:** instance class
- **Constructor:** `PathFollower.New()`
- **Defaults:** no points, once mode, stopped, speed 100000 (100 world units/second)
- **Scale:** coordinates and speed use 1000 units per world unit; `Progress` uses 0–1000 per mille

### Properties

| Property | Type | Access | Meaning |
|---|---|---|---|
| `PointCount` | `Integer` | read | Waypoint count, 0–64 |
| `Mode` | `Integer` | read/write | 0 once, 1 loop, 2 ping-pong |
| `Speed` | `Integer` | read/write | Positive fixed-point world units per second |
| `IsActive` | `Boolean` | read | Whether updates are enabled |
| `IsFinished` | `Boolean` | read | True only after once-mode completion |
| `X`, `Y` | `Integer` | read | Current fixed-point coordinates |
| `Progress` | `Integer` | read/write | Path-distance position from 0 through 1000 |
| `Segment` | `Integer` | read | Current zero-based segment index |
| `Angle` | `Integer` | read | Quantized direction in degrees × 1000 |

### Methods

| Method | Return | Behavior |
|---|---|---|
| `AddPoint(x, y)` | `Boolean` | Append one waypoint; false at 64 |
| `Clear()` | `Void` | Remove all waypoints and reset state |
| `Start()` | `Void` | Start/resume when at least two points exist |
| `Pause()` | `Void` | Disable updates without moving |
| `Stop()` | `Void` | Disable updates and rewind, preserving points |
| `Update(dtMilliseconds)` | `Void` | Spend the distance implied by a positive delta |
| `Destroy()` | `Void` | Release the handle and its length cache |

Invalid mode values and non-positive speed assignments are ignored, preserving the current value.
Use `Pause()` rather than speed zero. The first point immediately becomes `X`/`Y`. Adding points
marks a lazily rebuilt segment-length cache dirty.

`Start()` is a no-op with fewer than two points. It resumes a paused path from its current position;
after once-mode completion it rewinds first. `Stop()` always rewinds and clears completion. `Clear()`
also discards the waypoints.

Each positive update computes `floor(Speed * dt / 1000)` fixed-point distance and can cross several
segments. Once mode stops at the final point. Loop mode jumps from the final point back to the first
segment; it does not add a closing last-to-first segment. Ping-pong reverses over the same segments.
Setting `Progress` clamps to 0–1000 and seeks by path length, but does not change active, finished,
or traversal-direction flags.

`Angle` is only an eight-direction approximation: 0, 45, 90, …, 315 degrees, multiplied by 1000.
Positive Y is treated as down. It reflects reverse traversal in ping-pong mode. It is not an
`atan2` result for the actual segment slope.

### Current precision and degenerate-path limitations

Adjacent duplicate waypoints are skipped when positive movement reaches them, but a path whose
total length is zero remains active forever and never becomes finished. Do not start an all-equal
path.

Movement has no fractional-distance remainder, and segment position is stored in only 1001 steps.
If `Speed * dt / 1000` is below one fixed-point unit, the entire update is lost. Even above that,
movement smaller than one-thousandth of the current segment produces a zero progress delta and is
also lost. Repeated small updates can therefore stall permanently instead of accumulating. Choose
a timestep/speed for which each update advances at least one per-mille step of the longest segment,
or perform the accumulation in application code until this defect is fixed.

If allocation of the internal segment-length cache fails, length becomes zero and the follower can
likewise remain active without motion; no public error is reported and the cache is not retried
until another point is added.

### Zia example

```rust
module PathFollowerDemo;

bind Viper.Game.PathFollower as PathFollower;
bind Viper.Terminal;

func start() {
    var path = PathFollower.New();
    path.AddPoint(0, 0);
    path.AddPoint(100000, 0);      // 100 world units
    path.AddPoint(100000, 50000); // then 50 down
    path.Speed = 50000;           // 50 world units/second
    path.Mode = 0;                // once
    path.Start();

    path.Update(100);             // five world units
    SayInt(path.X / 1000);
    SayInt(path.Y / 1000);
    SayInt(path.Angle);

    path.Pause();
    path.Start();                 // resume
}
```

---

## Viper.Game.ButtonGroup

`ButtonGroup` stores integer IDs in insertion order and allows at most one selection. IDs are
application values; the group does not retain Button objects or draw UI.

- **Type:** instance class
- **Constructor:** `ButtonGroup.New()`
- **Capacity:** 256 IDs

### Properties

| Property | Type | Access | Meaning |
|---|---|---|---|
| `Count` | `Integer` | read | Registered ID count |
| `Selected` | `Integer` | read | Selected ID, or -1 when none |
| `HasSelection` | `Boolean` | read | Distinguishes no selection from selecting ID -1 |
| `SelectionChanged` | `Boolean` | read | Latched until `ClearChangedFlag()` |

### Methods

| Method | Return | Behavior |
|---|---|---|
| `Add(id)` | `Boolean` | Append a unique ID |
| `Remove(id)` | `Boolean` | Remove an existing ID |
| `Has(id)` | `Boolean` | Test registration |
| `Select(id)` | `Boolean` | Select a registered ID |
| `ClearSelection()` | `Void` | Deselect, if selected |
| `IsSelected(id)` | `Boolean` | Test the active selection |
| `ClearChangedFlag()` | `Void` | Clear the change latch |
| `GetAt(index)` | `Integer` | Read an insertion-order ID; invalid index returns -1 |
| `SelectNext()` | `Integer` | Cycle forward with wrap |
| `SelectPrevious()` | `Integer` | Cycle backward with wrap |
| `Destroy()` | `Void` | Release the handle |

`Add` normally returns false for a duplicate. The current implementation checks capacity before
checking duplicates, so *any* `Add` call after the group reaches 256 IDs traps—even an attempt to
re-add an existing ID. `GetAt` returning -1 is also ambiguous because -1 is a valid registered ID;
iterate only from zero to `Count - 1`.

Selecting an unknown ID returns false. Selecting the already selected ID returns true without
setting the change latch. Removing the selected ID or clearing a real selection sets the latch;
removing another ID does not. Reading `SelectionChanged` never clears it.

With no selection, `SelectNext()` chooses the first ID and `SelectPrevious()` chooses the last.
Both return -1 for an empty group, which is again ambiguous with a valid ID. Cycling a one-item
group that is already selected does not set `SelectionChanged`.

### BASIC example

```basic
CONST TOOL_PENCIL = 1
CONST TOOL_BRUSH = 2
CONST TOOL_ERASER = 3

DIM tools AS OBJECT = Viper.Game.ButtonGroup.New()
tools.Add(TOOL_PENCIL)
tools.Add(TOOL_BRUSH)
tools.Add(TOOL_ERASER)
tools.Select(TOOL_PENCIL)

PRINT tools.Selected
tools.ClearChangedFlag()
tools.SelectNext()

IF tools.SelectionChanged THEN
    PRINT tools.Selected
    tools.ClearChangedFlag()
END IF

tools.SelectPrevious()
PRINT tools.IsSelected(TOOL_PENCIL)
END
```

---

## See also

- [Core Game Utilities](core.md)
- [Physics & Collision](physics.md)
- [Visual Effects](effects.md)
- [Graphics](../graphics/README.md)
- [Generated Game API](../../generated/runtime/game.md)
- [Generated Graphics API](../../generated/runtime/graphics.md)
