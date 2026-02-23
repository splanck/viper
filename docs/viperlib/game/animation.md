# Animation & Movement
> Tween, SpriteAnimation, SpriteSheet, PathFollower, ButtonGroup

**Part of [Viper Runtime Library](../README.md) › [Game Utilities](README.md)**

---

## Viper.Game.Tween

Frame-based tweening with easing functions for smooth animations. Interpolates between values over time with various easing curves.

**Type:** Instance class (requires `New()`)

### Constructor

| Method  | Signature  | Description              |
|---------|------------|--------------------------|
| `New()` | `Tween()`  | Create a new tween       |

### Properties

| Property     | Type                  | Description                                |
|--------------|-----------------------|--------------------------------------------|
| `Value`      | `Double` (read-only)  | Current interpolated value                 |
| `ValueI64`   | `Integer` (read-only) | Current value as rounded integer           |
| `IsRunning`  | `Boolean` (read-only) | True if tween is active (not paused)       |
| `IsComplete` | `Boolean` (read-only) | True if tween has finished                 |
| `IsPaused`   | `Boolean` (read-only) | True if tween is paused                    |
| `Progress`   | `Integer` (read-only) | Completion percentage (0-100)              |
| `Elapsed`    | `Integer` (read-only) | Frames elapsed since start                 |
| `Duration`   | `Integer` (read-only) | Total duration in frames                   |

### Methods

| Method                          | Signature                     | Description                         |
|---------------------------------|-------------------------------|-------------------------------------|
| `Pause()`                       | `Void()`                      | Pause the tween                     |
| `Reset()`                       | `Void()`                      | Reset and restart from beginning    |
| `Resume()`                      | `Void()`                      | Resume a paused tween               |
| `Start(from, to, dur, ease)`    | `Void(Double,Double,Int,Int)` | Start tween with float values       |
| `StartI64(from, to, dur, ease)` | `Void(Int,Int,Int,Int)`       | Start tween with integer values     |
| `Stop()`                        | `Void()`                      | Stop the tween                      |
| `Update()`                      | `Boolean()`                   | Advance one frame; true if finished |

### Static Methods

| Method                   | Signature              | Description                                |
|--------------------------|------------------------|--------------------------------------------|
| `LerpI64(from, to, t)`   | `Integer(Int,Int,Dbl)` | Integer linear interpolation (0≤t≤1)       |
| `Ease(t, type)`          | `Double(Double,Int)`   | Apply easing function to progress value    |

### Easing Types

| Constant               | Value | Description                        |
|------------------------|-------|------------------------------------|
| `EASE_LINEAR`          | 0     | Linear interpolation (no easing)   |
| `EASE_IN_QUAD`         | 1     | Quadratic ease-in (accelerate)     |
| `EASE_OUT_QUAD`        | 2     | Quadratic ease-out (decelerate)    |
| `EASE_IN_OUT_QUAD`     | 3     | Quadratic ease-in-out              |
| `EASE_IN_CUBIC`        | 4     | Cubic ease-in                      |
| `EASE_OUT_CUBIC`       | 5     | Cubic ease-out                     |
| `EASE_IN_OUT_CUBIC`    | 6     | Cubic ease-in-out                  |
| `EASE_IN_SINE`         | 7     | Sinusoidal ease-in                 |
| `EASE_OUT_SINE`        | 8     | Sinusoidal ease-out                |
| `EASE_IN_OUT_SINE`     | 9     | Sinusoidal ease-in-out             |
| `EASE_IN_EXPO`         | 10    | Exponential ease-in                |
| `EASE_OUT_EXPO`        | 11    | Exponential ease-out               |
| `EASE_IN_OUT_EXPO`     | 12    | Exponential ease-in-out            |
| `EASE_IN_BACK`         | 13    | Back ease-in (overshoots start)    |
| `EASE_OUT_BACK`        | 14    | Back ease-out (overshoots end)     |
| `EASE_IN_OUT_BACK`     | 15    | Back ease-in-out                   |
| `EASE_IN_BOUNCE`       | 16    | Bounce ease-in                     |
| `EASE_OUT_BOUNCE`      | 17    | Bounce ease-out                    |
| `EASE_IN_OUT_BOUNCE`   | 18    | Bounce ease-in-out                 |

### Zia Example

```rust
module TweenDemo;

bind Viper.Terminal;
bind Viper.Game.Tween as Tween;
bind Viper.Fmt as Fmt;

func start() {
    var tw = Tween.New();
    tw.Start(0.0, 100.0, 10, 0);  // Linear, 10 frames

    var i = 0;
    while i < 11 {
        var done = tw.Update();
        Say("Frame " + Fmt.Int(i) + ": " + Fmt.Int(tw.get_ValueI64()));
        if done { Say("  Complete!"); }
        i = i + 1;
    }

    // Integer tween with easing
    var tw2 = Tween.New();
    tw2.StartI64(0, 200, 5, 2);  // EASE_OUT_QUAD

    // Static lerp
    Say("Lerp(0,100,0.5): " + Fmt.Int(Tween.LerpI64(0, 100, 0.5)));
}
```

### Example: Smooth Movement

```basic
DIM canvas AS OBJECT = Viper.Graphics.Canvas.New("Tween Demo", 800, 600)
DIM moveTween AS OBJECT = Viper.Game.Tween.New()

' Move from x=100 to x=600 over 60 frames with ease-out
moveTween.Start(100.0, 600.0, 60, 2)  ' EASE_OUT_QUAD = 2

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()
    canvas.Clear(&H000020)

    ' Update tween
    IF moveTween.Update() THEN
        PRINT "Animation complete!"
    END IF

    ' Draw at tweened position
    DIM x AS INTEGER = moveTween.ValueI64
    canvas.Box(x, 280, 40, 40, &HFF0000)

    ' Show progress
    canvas.Text(10, 10, "Progress: " & moveTween.Progress & "%", &HFFFFFF)

    canvas.Flip()
LOOP
```

### Example: UI Transitions

```basic
' Fade-in effect using opacity tween
DIM fadeTween AS OBJECT = Viper.Game.Tween.New()
fadeTween.Start(0.0, 255.0, 30, 8)  ' EASE_OUT_SINE

DO WHILE fadeTween.IsRunning
    canvas.Poll()
    fadeTween.Update()

    DIM alpha AS INTEGER = fadeTween.ValueI64
    DIM color AS INTEGER = &HFF0000 OR (alpha << 24)
    canvas.Clear(&H000000)
    canvas.BoxFilled(100, 100, 200, 150, color)
    canvas.Flip()
LOOP
```

---

## Viper.Game.SpriteAnimation

Frame-based sprite animation controller for animated characters, effects, and UI elements.

**Type:** Instance class (requires `New()`)

### Constructor

| Method  | Signature           | Description               |
|---------|---------------------|---------------------------|
| `New()` | `SpriteAnimation()` | Create new animation      |

### Properties

| Property        | Type                   | Description                              |
|-----------------|------------------------|------------------------------------------|
| `Frame`         | `Integer` (read/write) | Current frame index                      |
| `FrameDuration` | `Integer` (read/write) | Frames to display each animation frame   |
| `FrameCount`    | `Integer` (read-only)  | Total frames in animation                |
| `IsPlaying`     | `Boolean` (read-only)  | True if playing (not paused/stopped)     |
| `IsPaused`      | `Boolean` (read-only)  | True if paused                           |
| `IsFinished`    | `Boolean` (read-only)  | True if one-shot animation completed     |
| `Progress`      | `Integer` (read-only)  | Completion percentage (0-100)            |
| `Speed`         | `Double` (read/write)  | Playback speed multiplier (1.0 = normal) |
| `Loop`          | `Boolean` (read/write) | Enable looping                           |
| `PingPong`      | `Boolean` (read/write) | Enable ping-pong (forward/backward)      |
| `FrameChanged`  | `Boolean` (read-only)  | True if frame changed this update        |

### Methods

| Method                        | Signature           | Description                         |
|-------------------------------|---------------------|-------------------------------------|
| `Pause()`                     | `Void()`            | Pause (can resume)                  |
| `Play()`                      | `Void()`            | Start/restart from beginning        |
| `Reset()`                     | `Void()`            | Reset to first frame                |
| `Resume()`                    | `Void()`            | Resume paused animation             |
| `Setup(start, end, duration)` | `Void(Int,Int,Int)` | Configure frame range and timing    |
| `Stop()`                      | `Void()`            | Stop at current frame               |
| `Update()`                    | `Boolean()`         | Advance animation; true if finished |

### Zia Example

```rust
module SpriteAnimDemo;

bind Viper.Terminal;
bind Viper.Game.SpriteAnimation as SA;
bind Viper.Fmt as Fmt;

func start() {
    var anim = SA.New();
    anim.Setup(0, 7, 6);       // Frames 0-7, 6 game frames each
    anim.set_Loop(true);       // Note: Loop takes bool, not int
    anim.Play();

    Say("FrameCount: " + Fmt.Int(anim.get_FrameCount()));

    var i = 0;
    while i < 20 {
        anim.Update();
        if anim.get_FrameChanged() {
            Say("Frame " + Fmt.Int(i) + ": " + Fmt.Int(anim.get_Frame()));
        }
        i = i + 1;
    }

    anim.Pause();
    Say("Paused at frame: " + Fmt.Int(anim.get_Frame()));
    anim.Resume();
    anim.Update();
    Say("Resumed, frame: " + Fmt.Int(anim.get_Frame()));
}
```

### Example: Character Walk Cycle

```basic
DIM walkAnim AS OBJECT = Viper.Game.SpriteAnimation.New()
walkAnim.Setup(0, 7, 6)  ' Frames 0-7, 6 game frames each
walkAnim.Loop = 1
walkAnim.Play()

' In game loop
walkAnim.Update()
DIM frameIndex AS INTEGER = walkAnim.Frame
' Use frameIndex to select sprite region
```

### Example: Attack Animation (One-Shot)

```basic
DIM attackAnim AS OBJECT = Viper.Game.SpriteAnimation.New()
attackAnim.Setup(8, 15, 4)  ' Frames 8-15, faster playback
attackAnim.Loop = 0  ' One-shot

' Trigger attack
attackAnim.Play()

' In game loop
IF attackAnim.Update() THEN
    ' Animation finished, return to idle
    idleAnim.Play()
END IF
```

---

## Viper.Graphics.SpriteSheet

> **Note:** SpriteSheet is in the `Viper.Graphics` namespace, not `Viper.Game`.

Sprite sheet/atlas for named region extraction from a single texture. Defines named rectangular regions within an atlas image and extracts them as individual `Pixels` buffers.

**Type:** Instance class (requires `New(atlas)` or `FromGrid(atlas, frameW, frameH)`)

### Constructors

| Method                          | Signature                         | Description                                     |
|---------------------------------|-----------------------------------|-------------------------------------------------|
| `New(atlas)`                    | `SpriteSheet(Pixels)`             | Create from atlas Pixels buffer                 |
| `FromGrid(atlas, frameW, frameH)` | `SpriteSheet(Pixels, Int, Int)` | Create with uniform grid (auto-named "0", "1", ...) |

### Properties

| Property      | Type                  | Description                              |
|---------------|-----------------------|------------------------------------------|
| `RegionCount` | `Integer` (read-only) | Number of defined regions                |
| `Width`       | `Integer` (read-only) | Width of the underlying atlas            |
| `Height`      | `Integer` (read-only) | Height of the underlying atlas           |

### Methods

| Method                         | Signature                      | Description                                   |
|--------------------------------|--------------------------------|-----------------------------------------------|
| `GetRegion(name)`              | `Pixels(String)`               | Extract region as new Pixels (NULL if missing) |
| `HasRegion(name)`              | `Boolean(String)`              | Check if region name exists                   |
| `RegionNames()`                | `Seq()`                        | Get all region names as a Seq                 |
| `RemoveRegion(name)`           | `Boolean(String)`              | Remove a region; false if not found           |
| `SetRegion(name, x, y, w, h)` | `Void(String,Int,Int,Int,Int)` | Define a named region                         |

### Notes

- `FromGrid()` automatically slices the atlas into equal cells and names them `"0"`, `"1"`, etc. (left-to-right, top-to-bottom)
- `GetRegion()` returns a new Pixels object each call — cache results for repeated use
- Region coordinates are in pixels, relative to the atlas top-left corner
- Backed by a single atlas Pixels object — regions share the source data

### Zia Example

```rust
module SpriteSheetDemo;

bind Viper.Graphics;
bind Viper.Terminal;
bind Viper.Collections;

func start() {
    var atlas = Pixels.New(128, 128);
    atlas.Fill(Color.RGB(255, 0, 0));

    var sheet = SpriteSheet.New(atlas);
    Say("Regions: " + Fmt.Int(sheet.RegionCount));  // 0

    // Define named regions
    sheet.SetRegion("idle", 0, 0, 32, 32);
    sheet.SetRegion("walk1", 32, 0, 32, 32);
    sheet.SetRegion("walk2", 64, 0, 32, 32);
    sheet.SetRegion("jump", 96, 0, 32, 32);

    // Query regions
    SayBool(sheet.HasRegion("idle"));     // true
    SayBool(sheet.HasRegion("attack"));   // false

    // Get region as Pixels
    var region = sheet.GetRegion("idle");
    SayInt(Pixels.get_Width(region));   // 32

    // Region names list
    var names = sheet.RegionNames();
    SayInt(Seq.get_Len(names));  // 4

    // Remove a region
    sheet.RemoveRegion("jump");
    SayInt(sheet.RegionCount);  // 3

    // Auto-slice from grid
    var gridSheet = SpriteSheet.FromGrid(atlas, 32, 32);
    SayInt(gridSheet.RegionCount);  // 16
}
```

### BASIC Example

```basic
' Load an atlas image
DIM atlas AS OBJECT = Viper.Graphics.Pixels.Load("sprites.png")

' Method 1: Manual region definition
DIM sheet AS OBJECT = Viper.Graphics.SpriteSheet.New(atlas)
sheet.SetRegion("player_idle", 0, 0, 32, 48)
sheet.SetRegion("player_walk1", 32, 0, 32, 48)
sheet.SetRegion("player_walk2", 64, 0, 32, 48)
sheet.SetRegion("enemy", 0, 48, 32, 32)

' Extract a region
DIM idle AS OBJECT = sheet.GetRegion("player_idle")

' Check what's available
PRINT sheet.RegionCount  ' Output: 4
IF sheet.HasRegion("enemy") THEN
    DIM enemy AS OBJECT = sheet.GetRegion("enemy")
END IF

' Method 2: Uniform grid layout
DIM gridSheet AS OBJECT = Viper.Graphics.SpriteSheet.FromGrid(atlas, 32, 32)
' Regions auto-named "0", "1", "2", ... (left-to-right, top-to-bottom)
DIM frame0 AS OBJECT = gridSheet.GetRegion("0")
DIM frame1 AS OBJECT = gridSheet.GetRegion("1")

' List all region names
DIM names AS OBJECT = gridSheet.RegionNames()
FOR i = 0 TO names.Len - 1
    PRINT names.Get(i)
NEXT
```

### Use Cases

- **Character animation:** Define walk, idle, attack frames from a sprite sheet
- **Tile sets:** Extract tiles from a uniform grid atlas
- **UI elements:** Store buttons, icons, and decorations in a single texture
- **Game objects:** Keep all enemy sprites in one atlas for efficient loading

---

## Viper.Game.PathFollower

Path following for moving objects along predefined waypoint paths.

**Type:** Instance class (requires `New()`)

### Constructor

| Method  | Signature        | Description                |
|---------|------------------|----------------------------|
| `New()` | `PathFollower()` | Create a new path follower |

### Properties

| Property     | Type                   | Description                                  |
|--------------|------------------------|----------------------------------------------|
| `PointCount` | `Integer` (read-only)  | Number of waypoints                          |
| `Mode`       | `Integer` (read/write) | Path mode (0=once, 1=loop, 2=pingpong)       |
| `Speed`      | `Integer` (read/write) | Speed in units/sec (fixed-point: 1000=1)     |
| `IsActive`   | `Boolean` (read-only)  | True if following path                       |
| `IsFinished` | `Boolean` (read-only)  | True if reached end (once mode)              |
| `X`          | `Integer` (read-only)  | Current X position (fixed-point)             |
| `Y`          | `Integer` (read-only)  | Current Y position (fixed-point)             |
| `Progress`   | `Integer` (read/write) | Overall progress (0-1000)                    |
| `Segment`    | `Integer` (read-only)  | Current segment index                        |
| `Angle`      | `Integer` (read-only)  | Movement direction (degrees × 1000)          |

### Methods

| Method           | Signature           | Description                          |
|------------------|---------------------|--------------------------------------|
| `AddPoint(x, y)` | `Boolean(Int,Int)`  | Add waypoint (fixed-point coords)    |
| `Clear()`        | `Void()`            | Remove all waypoints                 |
| `Pause()`        | `Void()`            | Pause movement                       |
| `Start()`        | `Void()`            | Begin following path                 |
| `Stop()`         | `Void()`            | Stop and reset to start              |
| `Update(dt)`     | `Void(Integer)`     | Update position (dt in milliseconds) |

### Path Modes

| Constant              | Value | Description                |
|-----------------------|-------|----------------------------|
| `PATHFOLLOW_ONCE`     | 0     | Play once, stop at end     |
| `PATHFOLLOW_LOOP`     | 1     | Loop back to start         |
| `PATHFOLLOW_PINGPONG` | 2     | Reverse at endpoints       |

### Zia Example

```rust
module PathFollowerDemo;

bind Viper.Terminal;
bind Viper.Game.PathFollower as PF;
bind Viper.Fmt as Fmt;

func start() {
    var pf = PF.New();
    pf.AddPoint(0, 0);
    pf.AddPoint(100000, 0);       // 100 units right
    pf.AddPoint(100000, 100000);   // 100 units down
    pf.set_Speed(50000);           // 50 units/sec
    pf.set_Mode(1);                // PATHFOLLOW_LOOP

    pf.Start();
    Say("Points: " + Fmt.Int(pf.get_PointCount()));
    Say("Active: " + Fmt.Bool(pf.get_IsActive()));

    // Simulate a few frames
    var i = 0;
    while i < 5 {
        pf.Update(16);
        Say("Pos: " + Fmt.Int(pf.get_X()) + ", " + Fmt.Int(pf.get_Y()));
        i = i + 1;
    }

    pf.Pause();
    Say("Paused, segment: " + Fmt.Int(pf.get_Segment()));
}
```

### Example: Patrol Route

```basic
DIM patrol AS OBJECT = Viper.Game.PathFollower.New()
patrol.AddPoint(100000, 100000)   ' 100, 100
patrol.AddPoint(400000, 100000)   ' 400, 100
patrol.AddPoint(400000, 400000)   ' 400, 400
patrol.AddPoint(100000, 400000)   ' 100, 400
patrol.Mode = 1                    ' PATHFOLLOW_LOOP
patrol.Speed = 50000              ' 50 units/sec
patrol.Start()

' In game loop
patrol.Update(16)
DIM enemyX AS INTEGER = patrol.X / 1000
DIM enemyY AS INTEGER = patrol.Y / 1000
```

### Example: Cutscene Camera

```basic
DIM cameraPath AS OBJECT = Viper.Game.PathFollower.New()
cameraPath.AddPoint(0, 0)
cameraPath.AddPoint(200000, 100000)
cameraPath.AddPoint(500000, 300000)
cameraPath.Mode = 0  ' PATHFOLLOW_ONCE
cameraPath.Speed = 30000
cameraPath.Start()

DO WHILE NOT cameraPath.IsFinished
    cameraPath.Update(16)
    SetCameraPosition(cameraPath.X / 1000, cameraPath.Y / 1000)
    RenderScene()
LOOP
```

---

## Viper.Game.ButtonGroup

Manages mutually exclusive button selection, like radio buttons or tool palettes. Only one button can be selected at a time.

**Type:** Instance class (requires `New()`)

### Constructor

| Method  | Signature       | Description                    |
|---------|-----------------|--------------------------------|
| `New()` | `ButtonGroup()` | Create a new empty button group|

### Properties

| Property           | Type                  | Description                                    |
|--------------------|-----------------------|------------------------------------------------|
| `Count`            | `Integer` (read-only) | Number of buttons in the group                 |
| `Selected`         | `Integer` (read-only) | Currently selected button ID (-1 if none)      |
| `HasSelection`     | `Boolean` (read-only) | True if any button is selected                 |
| `SelectionChanged` | `Boolean` (read-only) | True if selection just changed this frame      |

### Methods

| Method               | Signature           | Description                                    |
|----------------------|---------------------|------------------------------------------------|
| `Add(id)`            | `Boolean(Integer)`  | Add button ID to group; false if exists        |
| `ClearChangedFlag()` | `Void()`            | Clear SelectionChanged flag                    |
| `ClearSelection()`   | `Void()`            | Deselect all buttons                           |
| `GetAt(index)`       | `Integer(Integer)`  | Get button ID at index (for iteration)         |
| `Has(id)`            | `Boolean(Integer)`  | Check if button ID is in group                 |
| `IsSelected(id)`     | `Boolean(Integer)`  | Check if specific button is selected           |
| `Remove(id)`         | `Boolean(Integer)`  | Remove button from group                       |
| `Select(id)`         | `Boolean(Integer)`  | Select a button (deselects others)             |
| `SelectNext()`       | `Integer()`         | Select next button (wraps); returns new ID     |
| `SelectPrev()`       | `Integer()`         | Select previous button (wraps); returns new ID |

### Zia Example

```rust
module ButtonGroupDemo;

bind Viper.Terminal;
bind Viper.Game.ButtonGroup as BG;
bind Viper.Fmt as Fmt;

func start() {
    var bg = BG.New();
    bg.Add(0);  // Pencil
    bg.Add(1);  // Brush
    bg.Add(2);  // Eraser
    bg.Select(0);

    Say("Selected: " + Fmt.Int(bg.get_Selected()));
    Say("Count: " + Fmt.Int(bg.get_Count()));

    bg.SelectNext();
    Say("After next: " + Fmt.Int(bg.get_Selected()));

    bg.SelectPrev();
    Say("After prev: " + Fmt.Int(bg.get_Selected()));

    Say("IsSelected(0): " + Fmt.Bool(bg.IsSelected(0)));
}
```

### Example: Tool Palette

```basic
' Tool IDs
CONST TOOL_PENCIL = 1
CONST TOOL_BRUSH = 2
CONST TOOL_ERASER = 3
CONST TOOL_FILL = 4

' Create tool group
DIM tools AS OBJECT = Viper.Game.ButtonGroup.New()
tools.Add(TOOL_PENCIL)
tools.Add(TOOL_BRUSH)
tools.Add(TOOL_ERASER)
tools.Add(TOOL_FILL)
tools.Select(TOOL_PENCIL)  ' Default tool

' Handle keyboard shortcuts
IF Viper.Input.Keyboard.Pressed(KEY_P) THEN tools.Select(TOOL_PENCIL)
IF Viper.Input.Keyboard.Pressed(KEY_B) THEN tools.Select(TOOL_BRUSH)
IF Viper.Input.Keyboard.Pressed(KEY_E) THEN tools.Select(TOOL_ERASER)
IF Viper.Input.Keyboard.Pressed(KEY_F) THEN tools.Select(TOOL_FILL)

' Cycle through tools with Tab
IF Viper.Input.Keyboard.Pressed(KEY_TAB) THEN
    tools.SelectNext()
END IF

' React to selection change
IF tools.SelectionChanged THEN
    SELECT CASE tools.Selected
    CASE TOOL_PENCIL: SetCursor("pencil")
    CASE TOOL_BRUSH: SetCursor("brush")
    CASE TOOL_ERASER: SetCursor("eraser")
    CASE TOOL_FILL: SetCursor("bucket")
    END SELECT
    tools.ClearChangedFlag()
END IF

' Draw tool palette with selection highlighting
FOR i = 0 TO tools.Count - 1
    DIM toolId AS INTEGER = tools.GetAt(i)
    DIM highlight AS INTEGER = 0
    IF tools.IsSelected(toolId) THEN highlight = 1
    DrawToolButton(i, toolId, highlight)
NEXT
```

### Example: Radio Buttons

```basic
' Difficulty options
CONST DIFF_EASY = 0
CONST DIFF_NORMAL = 1
CONST DIFF_HARD = 2

DIM difficulty AS OBJECT = Viper.Game.ButtonGroup.New()
difficulty.Add(DIFF_EASY)
difficulty.Add(DIFF_NORMAL)
difficulty.Add(DIFF_HARD)
difficulty.Select(DIFF_NORMAL)  ' Default

' In menu update
IF Viper.Input.Keyboard.Pressed(KEY_UP) THEN
    difficulty.SelectPrev()
END IF
IF Viper.Input.Keyboard.Pressed(KEY_DOWN) THEN
    difficulty.SelectNext()
END IF

' Get selected difficulty for game settings
DIM selectedDifficulty AS INTEGER = difficulty.Selected
```

---


## See Also

- [Core Utilities](core.md)
- [Physics & Collision](physics.md)
- [Visual Effects](effects.md)
- [Game Utilities Overview](README.md)
- [Viper Runtime Library](../README.md)
