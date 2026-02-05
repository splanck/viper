# Game Development Utilities

> Abstractions for common game development patterns.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Game.Grid2D](#vipergamegrid2d)
- [Viper.Game.Timer](#vipergametimer)
- [Viper.Game.StateMachine](#vipergamestatemachine)
- [Viper.Game.Tween](#vipergametween)
- [Viper.Game.ButtonGroup](#vipergamebuttongroup)
- [Viper.Game.SmoothValue](#vipergamesmoothvalue)
- [Viper.Game.ParticleEmitter](#vipergameparticleemitter)
- [Viper.Game.SpriteAnimation](#vipergamespriteanimation)
- [Viper.Game.CollisionRect](#vipergamecollisionrect)
- [Viper.Game.Collision](#vipergamecollision)
- [Viper.Game.ObjectPool](#vipergameobjectpool)
- [Viper.Game.ScreenFX](#vipergamescreenfx)
- [Viper.Game.PathFollower](#vipergamepathfollower)
- [Viper.Game.Quadtree](#vipergamequadtree)

---

## Viper.Game.Grid2D

A 2D array container optimized for tile maps, game boards, and grid-based data.

**Type:** Instance class (requires `New(width, height, defaultValue)`)

### Constructor

| Method                        | Signature                 | Description                                      |
|-------------------------------|---------------------------|--------------------------------------------------|
| `New(width, height, default)` | `Grid2D(Int, Int, Int)`   | Create a grid with dimensions and default value  |

### Properties

| Property | Type                  | Description                            |
|----------|-----------------------|----------------------------------------|
| `Width`  | `Integer` (read-only) | Width of the grid                      |
| `Height` | `Integer` (read-only) | Height of the grid                     |
| `Size`   | `Integer` (read-only) | Total number of cells (width × height) |

### Methods

| Method                | Signature                 | Description                                        |
|-----------------------|---------------------------|----------------------------------------------------|
| `Get(x, y)`           | `Integer(Int, Int)`       | Get value at coordinates                           |
| `Set(x, y, value)`    | `Void(Int, Int, Int)`     | Set value at coordinates                           |
| `Fill(value)`         | `Void(Integer)`           | Fill entire grid with value                        |
| `Clear()`             | `Void()`                  | Clear grid (fill with 0)                           |
| `InBounds(x, y)`      | `Boolean(Int, Int)`       | Check if coordinates are valid                     |
| `CopyFrom(other)`     | `Boolean(Grid2D)`         | Copy data from another grid (must match dimensions)|
| `Count(value)`        | `Integer(Integer)`        | Count cells with specified value                   |
| `Replace(old, new)`   | `Integer(Int, Int)`       | Replace all occurrences; returns count replaced    |

### Notes

- Coordinates are 0-based: (0,0) is top-left
- Out-of-bounds access traps with an error
- Use `InBounds()` to validate coordinates before access
- Grid stores integer values (use as tile IDs, flags, etc.)

### Zia Example

```zia
module Grid2DDemo;

bind Viper.Terminal;
bind Viper.Game.Grid2D as Grid;
bind Viper.Fmt as Fmt;

func start() {
    var g = Grid.New(10, 8, 0);
    Say("Size: " + Fmt.Int(g.get_Width()) + "x" + Fmt.Int(g.get_Height()));
    Say("Total: " + Fmt.Int(g.get_Size()));

    g.Fill(1);
    g.Set(5, 5, 42);
    Say("(5,5): " + Fmt.Int(g.Get(5, 5)));

    Say("InBounds(9,7): " + Fmt.Bool(g.InBounds(9, 7)));
    Say("Count(1): " + Fmt.Int(g.Count(1)));

    g.Replace(1, 2);
    g.Clear();
}
```

### Example: Tile Map

```basic
' Create a 20x15 tile map (defaults to 0 = empty)
DIM map AS OBJECT = Viper.Game.Grid2D.New(20, 15, 0)

' Set some tiles
CONST TILE_WALL = 1
CONST TILE_FLOOR = 2
CONST TILE_DOOR = 3

' Fill with floor
map.Fill(TILE_FLOOR)

' Add walls around the edges
FOR x = 0 TO map.Width - 1
    map.Set(x, 0, TILE_WALL)
    map.Set(x, map.Height - 1, TILE_WALL)
NEXT
FOR y = 0 TO map.Height - 1
    map.Set(0, y, TILE_WALL)
    map.Set(map.Width - 1, y, TILE_WALL)
NEXT

' Add a door
map.Set(10, 0, TILE_DOOR)

' Check a position before moving
DIM newX AS INTEGER = playerX + dx
DIM newY AS INTEGER = playerY + dy
IF map.InBounds(newX, newY) THEN
    DIM tile AS INTEGER = map.Get(newX, newY)
    IF tile <> TILE_WALL THEN
        playerX = newX
        playerY = newY
    END IF
END IF
```

### Example: Game of Life

```basic
DIM grid AS OBJECT = Viper.Game.Grid2D.New(50, 50, 0)
DIM next AS OBJECT = Viper.Game.Grid2D.New(50, 50, 0)

' Initialize with random cells
FOR y = 0 TO 49
    FOR x = 0 TO 49
        IF Viper.Random.Chance(0.3) THEN
            grid.Set(x, y, 1)
        END IF
    NEXT
NEXT

' Update step
FOR y = 0 TO 49
    FOR x = 0 TO 49
        DIM neighbors AS INTEGER = CountNeighbors(grid, x, y)
        DIM alive AS INTEGER = grid.Get(x, y)
        IF alive = 1 AND (neighbors = 2 OR neighbors = 3) THEN
            next.Set(x, y, 1)
        ELSEIF alive = 0 AND neighbors = 3 THEN
            next.Set(x, y, 1)
        ELSE
            next.Set(x, y, 0)
        END IF
    NEXT
NEXT
grid.CopyFrom(next)
```

### Use Cases

- **Tile maps:** 2D game levels, dungeon maps
- **Game boards:** Chess, checkers, puzzle games
- **Cellular automata:** Game of Life, simulation grids
- **Pathfinding:** Navigation grids, A* maps
- **Collision maps:** Simple tile-based collision detection

---

## Viper.Game.Timer

Frame-based timers for games and animations. Unlike wall-clock timers, Timer operates
in discrete frames, making it ideal for deterministic game logic.

**Type:** Instance class (requires `New()`)

### Constructor

| Method  | Signature  | Description                                 |
|---------|------------|---------------------------------------------|
| `New()` | `Timer()`  | Create a new timer (initially stopped)      |

### Properties

| Property      | Type                   | Description                                      |
|---------------|------------------------|--------------------------------------------------|
| `Duration`    | `Integer` (read/write) | Total frames for the countdown                   |
| `Elapsed`     | `Integer` (read-only)  | Frames elapsed since start                       |
| `Remaining`   | `Integer` (read-only)  | Frames remaining until expiration (0 if expired) |
| `Progress`    | `Integer` (read-only)  | Completion percentage (0-100)                    |
| `IsRunning`   | `Boolean` (read-only)  | True if the timer is currently running           |
| `IsExpired`   | `Boolean` (read-only)  | True if the timer has finished                   |
| `IsRepeating` | `Boolean` (read-only)  | True if the timer auto-restarts when expired     |

### Methods

| Method                   | Signature       | Description                                               |
|--------------------------|-----------------|-----------------------------------------------------------|
| `Start(frames)`          | `Void(Integer)` | Start a one-shot countdown for specified frames           |
| `StartRepeating(frames)` | `Void(Integer)` | Start a repeating timer that auto-restarts                |
| `Stop()`                 | `Void()`        | Pause the timer, preserving elapsed time                  |
| `Reset()`                | `Void()`        | Reset elapsed to 0 without stopping                       |
| `Update()`               | `Boolean()`     | Advance timer by one frame; returns true if just expired  |

### Notes

- Timer operates in frames, not milliseconds - call `Update()` once per frame
- `Update()` returns true exactly once when the timer expires
- One-shot timers stop when expired; repeating timers reset and continue
- `Progress` returns integer percentage (0-100), useful for animations
- Frame-based timing ensures deterministic game behavior across different hardware

### Zia Example

```zia
module TimerDemo;

bind Viper.Terminal;
bind Viper.Game.Timer as Timer;
bind Viper.Fmt as Fmt;

func start() {
    var t = Timer.New();
    t.Start(60);

    var i = 0;
    while i < 5 {
        t.Update();
        i = i + 1;
    }
    Say("Elapsed: " + Fmt.Int(t.get_Elapsed()));
    Say("Remaining: " + Fmt.Int(t.get_Remaining()));
    Say("Progress: " + Fmt.Int(t.get_Progress()) + "%");

    // Repeating timer
    var r = Timer.New();
    r.StartRepeating(5);
    i = 0;
    while i < 12 {
        if r.Update() { Say("Expired at frame " + Fmt.Int(i)); }
        i = i + 1;
    }
}
```

### Example: Animation Timer

```basic
DIM canvas AS OBJECT = Viper.Graphics.Canvas.New("Animation", 800, 600)

' Create a 60-frame animation timer (1 second at 60fps)
DIM anim AS OBJECT = Viper.Game.Timer.New()
anim.Start(60)

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    ' Update timer each frame
    IF anim.Update() THEN
        PRINT "Animation complete!"
    END IF

    ' Use progress for smooth interpolation
    DIM progress AS INTEGER = anim.Progress
    DIM x AS INTEGER = progress * 7  ' Move from 0 to 700

    canvas.Clear(&H00000000)
    canvas.Box(x, 280, 40, 40, &HFF0000)
    canvas.Flip()
LOOP
```

### Example: Repeating Timer (Game Events)

```basic
' Create a timer that fires every 180 frames (3 seconds at 60fps)
DIM spawnTimer AS OBJECT = Viper.Game.Timer.New()
spawnTimer.StartRepeating(180)

' Game loop
DO WHILE running
    canvas.Poll()

    ' Check if it's time to spawn an enemy
    IF spawnTimer.Update() THEN
        SpawnEnemy()  ' Called every 3 seconds
    END IF

    ' Rest of game logic...
LOOP
```

### Example: Power-Up Duration

```basic
DIM powerUpTimer AS OBJECT = Viper.Game.Timer.New()

' When player collects power-up
SUB OnPowerUpCollected()
    powerUpTimer.Start(600)  ' 10 seconds at 60fps
    playerSpeed = playerSpeed * 2
END SUB

' In game loop
IF powerUpTimer.IsRunning THEN
    ' Show remaining time
    IF powerUpTimer.Remaining < 120 THEN
        FlashPowerUpIndicator()  ' Warning: almost over
    END IF

    IF powerUpTimer.Update() THEN
        playerSpeed = playerSpeed / 2  ' Power-up expired
    END IF
END IF
```

### Comparison with Viper.Time.Countdown

| Feature             | Timer                          | Countdown                       |
|---------------------|--------------------------------|---------------------------------|
| Time unit           | Frames                         | Milliseconds                    |
| Determinism         | Fully deterministic            | Subject to system clock jitter  |
| Use case            | Game logic, animations         | Real-world timeouts             |
| Update model        | Manual `Update()` per frame    | Automatic based on clock        |
| Progress tracking   | 0-100 integer percentage       | Elapsed/remaining in ms         |
| Repeating mode      | Yes (`StartRepeating`)         | No (must manually restart)      |

### Use Cases

- **Animations:** Smooth interpolation with `Progress` property
- **Cooldowns:** Weapon fire rate, ability cooldowns
- **Spawn timers:** Periodic enemy spawning
- **Power-ups:** Duration-limited effects
- **Mode transitions:** Ghost AI state changes
- **Delays:** Wait N frames before action

---

## Viper.Game.StateMachine

A finite state machine for managing game/application states like menus, gameplay, pause screens, and transitions.

**Type:** Instance class (requires `New()`)

### Constructor

| Method  | Signature        | Description                           |
|---------|------------------|---------------------------------------|
| `New()` | `StateMachine()` | Create a new empty state machine      |

### Properties

| Property       | Type                  | Description                                     |
|----------------|-----------------------|-------------------------------------------------|
| `Current`      | `Integer` (read-only) | Current state ID (-1 if none set)               |
| `Previous`     | `Integer` (read-only) | Previous state ID (-1 if none)                  |
| `JustEntered`  | `Boolean` (read-only) | True if a transition just occurred this frame   |
| `JustExited`   | `Boolean` (read-only) | True if we just exited the previous state       |
| `FramesInState`| `Integer` (read-only) | Frames spent in current state                   |
| `StateCount`   | `Integer` (read-only) | Number of registered states                     |

### Methods

| Method              | Signature          | Description                                    |
|---------------------|--------------------|------------------------------------------------|
| `AddState(id)`      | `Boolean(Integer)` | Register a state ID; returns false if exists   |
| `SetInitial(id)`    | `Boolean(Integer)` | Set starting state before first update         |
| `Transition(id)`    | `Boolean(Integer)` | Transition to a new state                      |
| `IsState(id)`       | `Boolean(Integer)` | Check if currently in specified state          |
| `HasState(id)`      | `Boolean(Integer)` | Check if state ID is registered                |
| `Update()`          | `Void()`           | Increment frame counter (call once per frame)  |
| `ClearFlags()`      | `Void()`           | Clear JustEntered/JustExited flags             |

### Notes

- State IDs are integers (0 to 31); use constants for readability
- `JustEntered` and `JustExited` are true for one frame after transition
- Call `Update()` once per frame to track `FramesInState`
- Call `ClearFlags()` at end of frame if checking flags multiple times

### Zia Example

```zia
module StateMachineDemo;

bind Viper.Terminal;
bind Viper.Game.StateMachine as SM;
bind Viper.Fmt as Fmt;

func start() {
    var sm = SM.New();
    sm.AddState(0);  // MENU
    sm.AddState(1);  // PLAYING
    sm.AddState(2);  // PAUSED
    sm.SetInitial(0);

    Say("State: " + Fmt.Int(sm.get_Current()));
    sm.Transition(1);
    Say("After transition: " + Fmt.Int(sm.get_Current()));
    Say("Previous: " + Fmt.Int(sm.get_Previous()));

    sm.Update();
    sm.Update();
    Say("Frames in state: " + Fmt.Int(sm.get_FramesInState()));
    Say("IsState(1): " + Fmt.Bool(sm.IsState(1)));
}
```

### Example: Game States

```basic
' Define state constants
CONST STATE_MENU = 0
CONST STATE_PLAYING = 1
CONST STATE_PAUSED = 2
CONST STATE_GAMEOVER = 3

' Create and configure state machine
DIM sm AS OBJECT = Viper.Game.StateMachine.New()
sm.AddState(STATE_MENU)
sm.AddState(STATE_PLAYING)
sm.AddState(STATE_PAUSED)
sm.AddState(STATE_GAMEOVER)
sm.SetInitial(STATE_MENU)

' Game loop
DO WHILE running
    canvas.Poll()

    ' Handle state-specific logic
    SELECT CASE sm.Current
    CASE STATE_MENU
        IF sm.JustEntered THEN
            PRINT "Welcome! Press ENTER to start"
        END IF
        IF Viper.Input.Keyboard.Pressed(KEY_ENTER) THEN
            sm.Transition(STATE_PLAYING)
        END IF

    CASE STATE_PLAYING
        IF sm.JustEntered THEN
            InitGame()
        END IF
        UpdateGame()
        IF Viper.Input.Keyboard.Pressed(KEY_ESCAPE) THEN
            sm.Transition(STATE_PAUSED)
        END IF

    CASE STATE_PAUSED
        DrawPauseOverlay()
        IF Viper.Input.Keyboard.Pressed(KEY_ESCAPE) THEN
            sm.Transition(STATE_PLAYING)
        END IF

    CASE STATE_GAMEOVER
        IF sm.FramesInState > 180 THEN  ' 3 seconds at 60fps
            sm.Transition(STATE_MENU)
        END IF
    END SELECT

    sm.Update()
    sm.ClearFlags()
    canvas.Flip()
LOOP
```

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

| Method                            | Signature                       | Description                         |
|-----------------------------------|---------------------------------|-------------------------------------|
| `Start(from, to, dur, ease)`      | `Void(Double,Double,Int,Int)`   | Start tween with float values       |
| `StartI64(from, to, dur, ease)`   | `Void(Int,Int,Int,Int)`         | Start tween with integer values     |
| `Update()`                        | `Boolean()`                     | Advance one frame; true if finished |
| `Stop()`                          | `Void()`                        | Stop the tween                      |
| `Reset()`                         | `Void()`                        | Reset and restart from beginning    |
| `Pause()`                         | `Void()`                        | Pause the tween                     |
| `Resume()`                        | `Void()`                        | Resume a paused tween               |

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

```zia
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

| Method                | Signature           | Description                                    |
|-----------------------|---------------------|------------------------------------------------|
| `Add(id)`             | `Boolean(Integer)`  | Add button ID to group; false if exists        |
| `Remove(id)`          | `Boolean(Integer)`  | Remove button from group                       |
| `Has(id)`             | `Boolean(Integer)`  | Check if button ID is in group                 |
| `Select(id)`          | `Boolean(Integer)`  | Select a button (deselects others)             |
| `ClearSelection()`    | `Void()`            | Deselect all buttons                           |
| `IsSelected(id)`      | `Boolean(Integer)`  | Check if specific button is selected           |
| `ClearChangedFlag()`  | `Void()`            | Clear SelectionChanged flag                    |
| `GetAt(index)`        | `Integer(Integer)`  | Get button ID at index (for iteration)         |
| `SelectNext()`        | `Integer()`         | Select next button (wraps); returns new ID     |
| `SelectPrev()`        | `Integer()`         | Select previous button (wraps); returns new ID |

### Zia Example

```zia
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

## Viper.Game.SmoothValue

Smooth value interpolation for camera follow, UI animations, and other cases where instant changes would be jarring. Uses exponential smoothing.

**Type:** Instance class (requires `New(initial, smoothing)`)

### Constructor

| Method                    | Signature              | Description                                    |
|---------------------------|------------------------|------------------------------------------------|
| `New(initial, smoothing)` | `SmoothValue(Dbl,Dbl)` | Create with initial value and smoothing factor |

### Properties

| Property    | Type                   | Description                                      |
|-------------|------------------------|--------------------------------------------------|
| `Value`     | `Double` (read-only)   | Current smoothed value                           |
| `ValueI64`  | `Integer` (read-only)  | Current value as rounded integer                 |
| `Target`    | `Double` (read/write)  | Target value to approach                         |
| `Smoothing` | `Double` (read/write)  | Smoothing factor (0.0=instant, 0.95=slow)        |
| `AtTarget`  | `Boolean` (read-only)  | True if value has reached target                 |
| `Velocity`  | `Double` (read-only)   | Current rate of change per frame                 |

### Methods

| Method              | Signature      | Description                              |
|---------------------|----------------|------------------------------------------|
| `SetImmediate(val)` | `Void(Double)` | Set value and target immediately         |
| `Update()`          | `Void()`       | Advance interpolation (call each frame)  |
| `Impulse(amount)`   | `Void(Double)` | Add instant displacement to current value|

### Notes

- Smoothing factor of 0.9 gives smooth but responsive movement
- Higher values (0.95-0.99) give slower, more gradual movement
- Call `Update()` once per frame to advance the interpolation

### Zia Example

```zia
module SmoothValueDemo;

bind Viper.Terminal;
bind Viper.Game.SmoothValue as SV;
bind Viper.Fmt as Fmt;

func start() {
    var sv = SV.New(0.0, 0.9);
    sv.set_Target(100.0);

    var i = 0;
    while i < 10 {
        sv.Update();
        Say("Frame " + Fmt.Int(i) + ": " + Fmt.Int(sv.get_ValueI64()));
        i = i + 1;
    }

    sv.SetImmediate(50.0);
    Say("After SetImmediate: " + Fmt.Int(sv.get_ValueI64()));
}
```

### Example: Camera Follow

```basic
' Create smooth camera position
DIM camX AS OBJECT = Viper.Game.SmoothValue.New(400.0, 0.9)
DIM camY AS OBJECT = Viper.Game.SmoothValue.New(300.0, 0.9)

' In game loop
camX.Target = playerX  ' Camera follows player
camY.Target = playerY
camX.Update()
camY.Update()

' Use smoothed values for drawing offset
DIM offsetX AS INTEGER = camX.ValueI64 - 400
DIM offsetY AS INTEGER = camY.ValueI64 - 300
```

---

## Viper.Game.ParticleEmitter

Simple particle system for visual effects like explosions, sparks, smoke, and other game effects.

**Type:** Instance class (requires `New(maxParticles)`)

### Constructor

| Method              | Signature                | Description                        |
|---------------------|--------------------------|------------------------------------|
| `New(maxParticles)` | `ParticleEmitter(Int)`   | Create emitter (max 1024 particles)|

### Properties

| Property     | Type                   | Description                              |
|--------------|------------------------|------------------------------------------|
| `X`          | `Double` (read-only)   | Emitter X position                       |
| `Y`          | `Double` (read-only)   | Emitter Y position                       |
| `Rate`       | `Double` (read/write)  | Particles emitted per frame              |
| `IsEmitting` | `Boolean` (read-only)  | True if currently emitting               |
| `Count`      | `Integer` (read-only)  | Number of active particles               |
| `Color`      | `Integer` (write-only) | Particle color (0xAARRGGBB)              |
| `FadeOut`    | `Boolean` (write-only) | Enable alpha fade over lifetime          |
| `Shrink`     | `Boolean` (write-only) | Enable size reduction over lifetime      |

### Methods

| Method                               | Signature                  | Description                          |
|--------------------------------------|----------------------------|--------------------------------------|
| `SetPosition(x, y)`                  | `Void(Double,Double)`      | Set emitter position                 |
| `SetLifetime(min, max)`              | `Void(Int,Int)`            | Set particle lifetime range (frames) |
| `SetVelocity(minSpd,maxSpd,minAng,maxAng)` | `Void(Dbl,Dbl,Dbl,Dbl)` | Set speed and angle ranges         |
| `SetGravity(gx, gy)`                 | `Void(Double,Double)`      | Set gravity (per frame²)             |
| `SetSize(min, max)`                  | `Void(Double,Double)`      | Set particle size range              |
| `Start()`                            | `Void()`                   | Begin continuous emission            |
| `Stop()`                             | `Void()`                   | Stop emission (particles continue)   |
| `Burst(count)`                       | `Void(Integer)`            | Emit burst of particles instantly    |
| `Update()`                           | `Void()`                   | Update all particles (call per frame)|
| `Clear()`                            | `Void()`                   | Remove all particles                 |

### Zia Example

```zia
module ParticleDemo;

bind Viper.Terminal;
bind Viper.Game.ParticleEmitter as PE;
bind Viper.Fmt as Fmt;

func start() {
    var pe = PE.New(200);
    pe.SetPosition(400.0, 300.0);
    pe.SetLifetime(20, 40);
    pe.SetVelocity(2.0, 8.0, 0.0, 360.0);
    pe.SetGravity(0.0, 0.1);
    pe.SetSize(3.0, 6.0);

    // One-shot burst for explosion
    pe.Burst(50);
    pe.Update();
    Say("After burst: " + Fmt.Int(pe.get_Count()));

    // Continuous emission
    pe.set_Rate(5.0);
    pe.Start();
    var i = 0;
    while i < 10 {
        pe.Update();
        i = i + 1;
    }
    Say("After emitting: " + Fmt.Int(pe.get_Count()));

    pe.Stop();
    pe.Clear();
    Say("After clear: " + Fmt.Int(pe.get_Count()));
}
```

### Example: Explosion Effect

```basic
DIM explosion AS OBJECT = Viper.Game.ParticleEmitter.New(200)
explosion.SetPosition(400.0, 300.0)
explosion.SetLifetime(20, 40)
explosion.SetVelocity(2.0, 8.0, 0.0, 360.0)  ' All directions
explosion.SetGravity(0.0, 0.1)
explosion.Color = &HFFFF6600  ' Orange
explosion.SetSize(3.0, 6.0)
explosion.FadeOut = 1
explosion.Shrink = 1
explosion.Burst(100)  ' One-shot burst

' In game loop
explosion.Update()
' Render particles using Get() method
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
| `Loop`          | `Boolean` (write-only) | Enable looping                           |
| `PingPong`      | `Boolean` (write-only) | Enable ping-pong (forward/backward)      |
| `FrameChanged`  | `Boolean` (read-only)  | True if frame changed this update        |

### Methods

| Method                         | Signature         | Description                         |
|--------------------------------|-------------------|-------------------------------------|
| `Setup(start, end, duration)`  | `Void(Int,Int,Int)` | Configure frame range and timing  |
| `Play()`                       | `Void()`          | Start/restart from beginning        |
| `Stop()`                       | `Void()`          | Stop at current frame               |
| `Pause()`                      | `Void()`          | Pause (can resume)                  |
| `Resume()`                     | `Void()`          | Resume paused animation             |
| `Reset()`                      | `Void()`          | Reset to first frame                |
| `Update()`                     | `Boolean()`       | Advance animation; true if finished |

### Zia Example

```zia
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

## Viper.Game.CollisionRect

Axis-aligned bounding box (AABB) for collision detection between game objects.

**Type:** Instance class (requires `New(x, y, width, height)`)

### Constructor

| Method                    | Signature                    | Description                     |
|---------------------------|------------------------------|---------------------------------|
| `New(x, y, width, height)`| `CollisionRect(Dbl,Dbl,Dbl,Dbl)` | Create rectangle at position |

### Properties

| Property  | Type                  | Description                   |
|-----------|-----------------------|-------------------------------|
| `X`       | `Double` (read-only)  | Left edge                     |
| `Y`       | `Double` (read-only)  | Top edge                      |
| `Width`   | `Double` (read-only)  | Width                         |
| `Height`  | `Double` (read-only)  | Height                        |
| `Right`   | `Double` (read-only)  | Right edge (x + width)        |
| `Bottom`  | `Double` (read-only)  | Bottom edge (y + height)      |
| `CenterX` | `Double` (read-only)  | Center X coordinate           |
| `CenterY` | `Double` (read-only)  | Center Y coordinate           |

### Methods

| Method                          | Signature                   | Description                          |
|---------------------------------|-----------------------------|--------------------------------------|
| `SetPosition(x, y)`             | `Void(Double,Double)`       | Set top-left position                |
| `SetSize(w, h)`                 | `Void(Double,Double)`       | Set dimensions                       |
| `Set(x, y, w, h)`               | `Void(Dbl,Dbl,Dbl,Dbl)`     | Set position and size                |
| `SetCenter(cx, cy)`             | `Void(Double,Double)`       | Position by center point             |
| `Move(dx, dy)`                  | `Void(Double,Double)`       | Move by delta                        |
| `ContainsPoint(px, py)`         | `Boolean(Double,Double)`    | Test if point is inside              |
| `Overlaps(other)`               | `Boolean(CollisionRect)`    | Test overlap with another rect       |
| `OverlapsRect(x, y, w, h)`      | `Boolean(Dbl,Dbl,Dbl,Dbl)`  | Test overlap with raw coordinates    |
| `OverlapX(other)`               | `Double(CollisionRect)`     | Get X overlap depth                  |
| `OverlapY(other)`               | `Double(CollisionRect)`     | Get Y overlap depth                  |
| `Expand(margin)`                | `Void(Double)`              | Grow rect on all sides               |
| `ContainsRect(other)`           | `Boolean(CollisionRect)`    | Test if fully contains another       |

### Zia Example

```zia
module CollisionDemo;

bind Viper.Terminal;
bind Viper.Game.Collision as Coll;
bind Viper.Game.CollisionRect as CR;
bind Viper.Fmt as Fmt;

func start() {
    // Static collision checks
    Say("Rects overlap: " + Fmt.Bool(Coll.RectsOverlap(0.0, 0.0, 10.0, 10.0, 5.0, 5.0, 10.0, 10.0)));
    Say("Point in rect: " + Fmt.Bool(Coll.PointInRect(5.0, 5.0, 0.0, 0.0, 10.0, 10.0)));
    Say("Circles overlap: " + Fmt.Bool(Coll.CirclesOverlap(0.0, 0.0, 5.0, 8.0, 0.0, 5.0)));
    Say("Distance: " + Fmt.Num(Coll.Distance(0.0, 0.0, 3.0, 4.0)));

    // CollisionRect instance
    var r = CR.New(10.0, 20.0, 100.0, 50.0);
    Say("Contains(50,30): " + Fmt.Bool(r.ContainsPoint(50.0, 30.0)));
    r.Move(5.0, 10.0);
    Say("After move X: " + Fmt.Num(r.get_X()));
}
```

### Example: Player-Enemy Collision

```basic
DIM playerBox AS OBJECT = Viper.Game.CollisionRect.New(100.0, 100.0, 32.0, 48.0)
DIM enemyBox AS OBJECT = Viper.Game.CollisionRect.New(200.0, 100.0, 32.0, 32.0)

' Update positions
playerBox.SetPosition(playerX, playerY)
enemyBox.SetPosition(enemyX, enemyY)

' Check collision
IF playerBox.Overlaps(enemyBox) THEN
    TakeDamage()
END IF
```

---

## Viper.Game.Collision

Static utility class for collision detection without creating objects. Useful for one-off checks.

**Type:** Static class (no instantiation needed)

### Methods

| Method                              | Signature                        | Description                         |
|-------------------------------------|----------------------------------|-------------------------------------|
| `RectsOverlap(x1,y1,w1,h1,x2,y2,w2,h2)` | `Boolean(8×Double)`          | Test rectangle overlap              |
| `PointInRect(px,py,rx,ry,rw,rh)`    | `Boolean(6×Double)`              | Test if point in rectangle          |
| `CirclesOverlap(x1,y1,r1,x2,y2,r2)` | `Boolean(6×Double)`              | Test circle overlap                 |
| `PointInCircle(px,py,cx,cy,r)`      | `Boolean(5×Double)`              | Test if point in circle             |
| `CircleRect(cx,cy,r,rx,ry,rw,rh)`   | `Boolean(7×Double)`              | Test circle-rectangle overlap       |
| `Distance(x1,y1,x2,y2)`             | `Double(4×Double)`               | Distance between two points         |
| `DistanceSquared(x1,y1,x2,y2)`      | `Double(4×Double)`               | Squared distance (faster)           |

### Zia Example

> See the CollisionRect Zia example above for both static and instance collision API usage.

### Example: Quick Collision Checks

```basic
' Check if mouse click hits a button
IF Viper.Game.Collision.PointInRect(mouseX, mouseY, btnX, btnY, btnW, btnH) THEN
    OnButtonClick()
END IF

' Check if two circles overlap
IF Viper.Game.Collision.CirclesOverlap(p1X, p1Y, p1R, p2X, p2Y, p2R) THEN
    HandleCollision()
END IF

' Get distance for range checks
DIM dist AS DOUBLE = Viper.Game.Collision.Distance(playerX, playerY, enemyX, enemyY)
IF dist < attackRange THEN
    CanAttack = 1
END IF
```

---

## Viper.Game.ObjectPool

Efficient object pool for reusing slot indices, avoiding allocation churn for frequently created/destroyed game objects like bullets, enemies, and particles.

**Type:** Instance class (requires `New(capacity)`)

### Constructor

| Method           | Signature          | Description                              |
|------------------|--------------------|------------------------------------------|
| `New(capacity)`  | `ObjectPool(Int)`  | Create pool with max slots (up to 4096)  |

### Properties

| Property      | Type                  | Description                              |
|---------------|-----------------------|------------------------------------------|
| `Capacity`    | `Integer` (read-only) | Maximum number of slots                  |
| `ActiveCount` | `Integer` (read-only) | Number of currently acquired slots       |
| `FreeCount`   | `Integer` (read-only) | Number of available slots                |
| `IsFull`      | `Boolean` (read-only) | True if all slots are in use             |
| `IsEmpty`     | `Boolean` (read-only) | True if no slots are in use              |

### Methods

| Method               | Signature           | Description                                    |
|----------------------|---------------------|------------------------------------------------|
| `Acquire()`          | `Integer()`         | Get a free slot index (-1 if full)             |
| `Release(slot)`      | `Boolean(Integer)`  | Return slot to pool; false if invalid          |
| `IsActive(slot)`     | `Boolean(Integer)`  | Check if slot is currently acquired            |
| `Clear()`            | `Void()`            | Release all slots                              |
| `FirstActive()`      | `Integer()`         | Get first active slot (-1 if none)             |
| `NextActive(after)`  | `Integer(Integer)`  | Get next active slot after index               |
| `SetData(slot,data)` | `Boolean(Int,Int)`  | Associate user data with slot                  |
| `GetData(slot)`      | `Integer(Integer)`  | Get user data for slot                         |

### Zia Example

```zia
module ObjectPoolDemo;

bind Viper.Terminal;
bind Viper.Game.ObjectPool as Pool;
bind Viper.Fmt as Fmt;

func start() {
    var pool = Pool.New(10);
    Say("Capacity: " + Fmt.Int(pool.get_Capacity()));

    var s1 = pool.Acquire();
    var s2 = pool.Acquire();
    var s3 = pool.Acquire();
    Say("Active: " + Fmt.Int(pool.get_ActiveCount()));

    pool.SetData(s1, 100);
    Say("Data s1: " + Fmt.Int(pool.GetData(s1)));

    pool.Release(s2);
    Say("After release: " + Fmt.Int(pool.get_ActiveCount()));

    // Iterate active slots
    var slot = pool.FirstActive();
    while slot >= 0 {
        Say("Active: " + Fmt.Int(slot));
        slot = pool.NextActive(slot);
    }

    pool.Clear();
}
```

### Example: Bullet Pool

```basic
' Create bullet pool
DIM bullets AS OBJECT = Viper.Game.ObjectPool.New(100)
DIM bulletX(99) AS DOUBLE
DIM bulletY(99) AS DOUBLE
DIM bulletVX(99) AS DOUBLE
DIM bulletVY(99) AS DOUBLE

' Fire a bullet
SUB FireBullet(x, y, vx, vy)
    DIM slot AS INTEGER = bullets.Acquire()
    IF slot >= 0 THEN
        bulletX(slot) = x
        bulletY(slot) = y
        bulletVX(slot) = vx
        bulletVY(slot) = vy
    END IF
END SUB

' Update all bullets
SUB UpdateBullets()
    DIM slot AS INTEGER = bullets.FirstActive()
    DO WHILE slot >= 0
        bulletX(slot) = bulletX(slot) + bulletVX(slot)
        bulletY(slot) = bulletY(slot) + bulletVY(slot)

        ' Remove if off-screen
        IF bulletX(slot) < 0 OR bulletX(slot) > 800 THEN
            bullets.Release(slot)
        END IF

        slot = bullets.NextActive(slot)
    LOOP
END SUB
```

---

## Viper.Game.ScreenFX

Screen effects manager for camera shake, color flash, and fade effects.

**Type:** Instance class (requires `New()`)

### Constructor

| Method  | Signature    | Description                    |
|---------|--------------|--------------------------------|
| `New()` | `ScreenFX()` | Create a new effects manager   |

### Properties

| Property       | Type                  | Description                              |
|----------------|-----------------------|------------------------------------------|
| `IsActive`     | `Boolean` (read-only) | True if any effect is active             |
| `ShakeX`       | `Integer` (read-only) | Current X shake offset (fixed-point)     |
| `ShakeY`       | `Integer` (read-only) | Current Y shake offset (fixed-point)     |
| `OverlayColor` | `Integer` (read-only) | Current overlay color (RGB)              |
| `OverlayAlpha` | `Integer` (read-only) | Current overlay alpha (0-255)            |

### Methods

| Method                        | Signature                | Description                              |
|-------------------------------|--------------------------|------------------------------------------|
| `Update(dt)`                  | `Void(Integer)`          | Update effects (dt in milliseconds)      |
| `Shake(intensity, dur, decay)`| `Void(Int,Int,Int)`      | Start camera shake effect                |
| `Flash(color, duration)`      | `Void(Integer,Integer)`  | Start color flash effect                 |
| `FadeIn(color, duration)`     | `Void(Integer,Integer)`  | Fade from color to clear                 |
| `FadeOut(color, duration)`    | `Void(Integer,Integer)`  | Fade from clear to color                 |
| `CancelAll()`                 | `Void()`                 | Cancel all effects                       |
| `CancelType(type)`            | `Void(Integer)`          | Cancel effects of specific type          |
| `IsTypeActive(type)`          | `Boolean(Integer)`       | Check if effect type is active           |

### Effect Types

| Constant            | Value | Description              |
|---------------------|-------|--------------------------|
| `SCREENFX_NONE`     | 0     | No effect                |
| `SCREENFX_SHAKE`    | 1     | Camera shake             |
| `SCREENFX_FLASH`    | 2     | Color flash              |
| `SCREENFX_FADE_IN`  | 3     | Fade from color to clear |
| `SCREENFX_FADE_OUT` | 4     | Fade from clear to color |

### Zia Example

```zia
module ScreenFXDemo;

bind Viper.Terminal;
bind Viper.Game.ScreenFX as FX;
bind Viper.Fmt as Fmt;

func start() {
    var fx = FX.New();

    // Camera shake
    fx.Shake(5000, 300, 500);
    fx.Update(16);
    Say("Active after shake: " + Fmt.Bool(fx.get_IsActive()));
    Say("ShakeX: " + Fmt.Int(fx.get_ShakeX()));
    Say("ShakeY: " + Fmt.Int(fx.get_ShakeY()));

    // Flash effect
    fx.Flash(16711680, 200);     // Red flash
    fx.Update(16);
    Say("Overlay alpha: " + Fmt.Int(fx.get_OverlayAlpha()));

    // Cancel all
    fx.CancelAll();
    Say("After cancel: " + Fmt.Bool(fx.get_IsActive()));
}
```

### Example: Damage Effects

```basic
DIM fx AS OBJECT = Viper.Game.ScreenFX.New()

' On player damage
SUB OnDamage()
    fx.Shake(5000, 300, 500)        ' Shake for 300ms
    fx.Flash(&HFFFF0000, 200)       ' Red flash for 200ms
END SUB

' On level transition
SUB TransitionToLevel()
    fx.FadeOut(&HFF000000, 500)     ' Fade to black over 500ms
END SUB

' In game loop
fx.Update(16)  ' 16ms per frame at 60fps

' Apply to camera
DIM camOffsetX AS INTEGER = fx.ShakeX / 1000
DIM camOffsetY AS INTEGER = fx.ShakeY / 1000

' Draw overlay
IF fx.OverlayAlpha > 0 THEN
    canvas.BoxFilled(0, 0, 800, 600, fx.OverlayColor OR (fx.OverlayAlpha << 24))
END IF
```

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

| Method            | Signature           | Description                          |
|-------------------|---------------------|--------------------------------------|
| `Clear()`         | `Void()`            | Remove all waypoints                 |
| `AddPoint(x, y)`  | `Boolean(Int,Int)`  | Add waypoint (fixed-point coords)    |
| `Start()`         | `Void()`            | Begin following path                 |
| `Pause()`         | `Void()`            | Pause movement                       |
| `Stop()`          | `Void()`            | Stop and reset to start              |
| `Update(dt)`      | `Void(Integer)`     | Update position (dt in milliseconds) |

### Path Modes

| Constant              | Value | Description                |
|-----------------------|-------|----------------------------|
| `PATHFOLLOW_ONCE`     | 0     | Play once, stop at end     |
| `PATHFOLLOW_LOOP`     | 1     | Loop back to start         |
| `PATHFOLLOW_PINGPONG` | 2     | Reverse at endpoints       |

### Zia Example

```zia
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

## Viper.Game.Quadtree

Spatial partitioning data structure for efficient collision detection and spatial queries.

**Type:** Instance class (requires `New(x, y, width, height)`)

### Constructor

| Method                     | Signature                    | Description                         |
|----------------------------|------------------------------|-------------------------------------|
| `New(x, y, width, height)` | `Quadtree(Int,Int,Int,Int)`  | Create quadtree with bounds         |

### Properties

| Property      | Type                  | Description                          |
|---------------|-----------------------|--------------------------------------|
| `ItemCount`   | `Integer` (read-only) | Number of items in tree              |
| `ResultCount` | `Integer` (read-only) | Number of results from last query    |

### Methods

| Method                           | Signature                   | Description                              |
|----------------------------------|-----------------------------|------------------------------------------|
| `Clear()`                        | `Void()`                    | Remove all items                         |
| `Insert(id, x, y, w, h)`         | `Boolean(5×Int)`            | Add item with bounds                     |
| `Remove(id)`                     | `Boolean(Integer)`          | Remove item by ID                        |
| `Update(id, x, y, w, h)`         | `Boolean(5×Int)`            | Update item position/size                |
| `QueryRect(x, y, w, h)`          | `Integer(4×Int)`            | Find items in rectangle; returns count   |
| `QueryPoint(x, y, radius)`       | `Integer(3×Int)`            | Find items near point; returns count     |
| `GetResult(index)`               | `Integer(Integer)`          | Get item ID from query results           |
| `GetPairs()`                     | `Integer()`                 | Get potential collision pairs; returns count |
| `PairFirst(index)`               | `Integer(Integer)`          | Get first ID of collision pair           |
| `PairSecond(index)`              | `Integer(Integer)`          | Get second ID of collision pair          |

### Zia Example

```zia
module QuadtreeDemo;

bind Viper.Terminal;
bind Viper.Game.Quadtree as QT;
bind Viper.Fmt as Fmt;

func start() {
    var qt = QT.New(0, 0, 1000, 1000);
    qt.Insert(1, 100, 100, 50, 50);
    qt.Insert(2, 200, 200, 50, 50);
    qt.Insert(3, 800, 800, 50, 50);
    Say("Items: " + Fmt.Int(qt.get_ItemCount()));

    // Query for items in a region
    var count = qt.QueryRect(50, 50, 250, 250);
    Say("QueryRect found: " + Fmt.Int(count));
    var i = 0;
    while i < count {
        Say("  Result: " + Fmt.Int(qt.GetResult(i)));
        i = i + 1;
    }

    // Remove and re-query
    qt.Remove(2);
    Say("After remove: " + Fmt.Int(qt.get_ItemCount()));

    qt.Clear();
    Say("After clear: " + Fmt.Int(qt.get_ItemCount()));
}
```

### Example: Collision Detection

```basic
' Create quadtree for game world
DIM tree AS OBJECT = Viper.Game.Quadtree.New(0, 0, 800000, 600000)

' Add game objects (using fixed-point coordinates)
tree.Insert(1, playerX * 1000, playerY * 1000, 32000, 48000)
tree.Insert(2, enemy1X * 1000, enemy1Y * 1000, 32000, 32000)
tree.Insert(3, enemy2X * 1000, enemy2Y * 1000, 32000, 32000)

' Query for objects near player
DIM count AS INTEGER = tree.QueryPoint(playerX * 1000, playerY * 1000, 50000)
FOR i = 0 TO count - 1
    DIM id AS INTEGER = tree.GetResult(i)
    IF id <> 1 THEN  ' Not the player
        HandleCollision(1, id)
    END IF
NEXT

' Or get all potential collision pairs
DIM pairCount AS INTEGER = tree.GetPairs()
FOR i = 0 TO pairCount - 1
    DIM id1 AS INTEGER = tree.PairFirst(i)
    DIM id2 AS INTEGER = tree.PairSecond(i)
    CheckDetailedCollision(id1, id2)
NEXT
```

### Example: Bullet Optimization

```basic
' Instead of O(n²) collision checks
DIM tree AS OBJECT = Viper.Game.Quadtree.New(0, 0, 800000, 600000)

' Add all enemies to tree
FOR i = 0 TO enemyCount - 1
    tree.Insert(i, enemyX(i) * 1000, enemyY(i) * 1000, 32000, 32000)
NEXT

' For each bullet, only check nearby enemies
DIM slot AS INTEGER = bullets.FirstActive()
DO WHILE slot >= 0
    DIM bx AS INTEGER = bulletX(slot) * 1000
    DIM by AS INTEGER = bulletY(slot) * 1000

    ' Find enemies within bullet radius
    DIM hits AS INTEGER = tree.QueryPoint(bx, by, 20000)
    FOR i = 0 TO hits - 1
        DIM enemyId AS INTEGER = tree.GetResult(i)
        IF BulletHitsEnemy(slot, enemyId) THEN
            DamageEnemy(enemyId)
            bullets.Release(slot)
            EXIT FOR
        END IF
    NEXT

    slot = bullets.NextActive(slot)
LOOP
```

---

## See Also

- [Time & Timing](time.md) - `Timer`, `Countdown` for frame-based and wall-clock timing
- [Graphics](graphics.md) - `Canvas`, `Sprite` for rendering
- [Input](input.md) - `Keyboard`, `Mouse`, `Pad` for input handling
- [GUI](gui.md) - `Button`, `RadioButton` for GUI widgets

