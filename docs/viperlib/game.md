# Game Development Utilities

> Abstractions for common game development patterns.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.StateMachine](#viperstatemachine)
- [Viper.Tween](#vipertween)
- [Viper.ButtonGroup](#viperbuttongroup)
- [Viper.SmoothValue](#vipersmoothvalue)
- [Viper.ParticleEmitter](#viperparticleemitter)
- [Viper.SpriteAnimation](#viperspriteanimation)
- [Viper.CollisionRect](#vipercollisionrect)
- [Viper.Collision](#vipercollision)
- [Viper.ObjectPool](#viperobjectpool)
- [Viper.ScreenFX](#viperscreenfx)
- [Viper.PathFollower](#viperpathfollower)
- [Viper.Quadtree](#viperquadtree)

---

## Viper.StateMachine

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

### Example: Game States

```basic
' Define state constants
CONST STATE_MENU = 0
CONST STATE_PLAYING = 1
CONST STATE_PAUSED = 2
CONST STATE_GAMEOVER = 3

' Create and configure state machine
DIM sm AS OBJECT = Viper.StateMachine.New()
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

## Viper.Tween

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

### Example: Smooth Movement

```basic
DIM canvas AS OBJECT = Viper.Graphics.Canvas.New("Tween Demo", 800, 600)
DIM moveTween AS OBJECT = Viper.Tween.New()

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
DIM fadeTween AS OBJECT = Viper.Tween.New()
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

## Viper.ButtonGroup

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

### Example: Tool Palette

```basic
' Tool IDs
CONST TOOL_PENCIL = 1
CONST TOOL_BRUSH = 2
CONST TOOL_ERASER = 3
CONST TOOL_FILL = 4

' Create tool group
DIM tools AS OBJECT = Viper.ButtonGroup.New()
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

DIM difficulty AS OBJECT = Viper.ButtonGroup.New()
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

## Viper.SmoothValue

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

### Example: Camera Follow

```basic
' Create smooth camera position
DIM camX AS OBJECT = Viper.SmoothValue.New(400.0, 0.9)
DIM camY AS OBJECT = Viper.SmoothValue.New(300.0, 0.9)

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

## Viper.ParticleEmitter

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

### Example: Explosion Effect

```basic
DIM explosion AS OBJECT = Viper.ParticleEmitter.New(200)
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

## Viper.SpriteAnimation

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

### Example: Character Walk Cycle

```basic
DIM walkAnim AS OBJECT = Viper.SpriteAnimation.New()
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
DIM attackAnim AS OBJECT = Viper.SpriteAnimation.New()
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

## Viper.CollisionRect

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

### Example: Player-Enemy Collision

```basic
DIM playerBox AS OBJECT = Viper.CollisionRect.New(100.0, 100.0, 32.0, 48.0)
DIM enemyBox AS OBJECT = Viper.CollisionRect.New(200.0, 100.0, 32.0, 32.0)

' Update positions
playerBox.SetPosition(playerX, playerY)
enemyBox.SetPosition(enemyX, enemyY)

' Check collision
IF playerBox.Overlaps(enemyBox) THEN
    TakeDamage()
END IF
```

---

## Viper.Collision

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

### Example: Quick Collision Checks

```basic
' Check if mouse click hits a button
IF Viper.Collision.PointInRect(mouseX, mouseY, btnX, btnY, btnW, btnH) THEN
    OnButtonClick()
END IF

' Check if two circles overlap
IF Viper.Collision.CirclesOverlap(p1X, p1Y, p1R, p2X, p2Y, p2R) THEN
    HandleCollision()
END IF

' Get distance for range checks
DIM dist AS DOUBLE = Viper.Collision.Distance(playerX, playerY, enemyX, enemyY)
IF dist < attackRange THEN
    CanAttack = 1
END IF
```

---

## Viper.ObjectPool

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

### Example: Bullet Pool

```basic
' Create bullet pool
DIM bullets AS OBJECT = Viper.ObjectPool.New(100)
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

## Viper.ScreenFX

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

### Example: Damage Effects

```basic
DIM fx AS OBJECT = Viper.ScreenFX.New()

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

## Viper.PathFollower

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

### Example: Patrol Route

```basic
DIM patrol AS OBJECT = Viper.PathFollower.New()
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
DIM cameraPath AS OBJECT = Viper.PathFollower.New()
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

## Viper.Quadtree

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

### Example: Collision Detection

```basic
' Create quadtree for game world
DIM tree AS OBJECT = Viper.Quadtree.New(0, 0, 800000, 600000)

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
DIM tree AS OBJECT = Viper.Quadtree.New(0, 0, 800000, 600000)

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

