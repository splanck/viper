# Core Utilities
> Timer, StateMachine, SmoothValue, ObjectPool

**Part of [Viper Runtime Library](../README.md) › [Game Utilities](README.md)**

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
| `Reset()`                | `Void()`        | Reset elapsed to 0 without stopping                       |
| `Start(frames)`          | `Void(Integer)` | Start a one-shot countdown for specified frames           |
| `StartRepeating(frames)` | `Void(Integer)` | Start a repeating timer that auto-restarts                |
| `Stop()`                 | `Void()`        | Pause the timer, preserving elapsed time                  |
| `Update()`               | `Boolean()`     | Advance timer by one frame; returns true if just expired  |

### Notes

- Timer operates in frames, not milliseconds - call `Update()` once per frame
- `Update()` returns true exactly once when the timer expires
- One-shot timers stop when expired; repeating timers reset and continue
- `Progress` returns integer percentage (0-100), useful for animations
- Frame-based timing ensures deterministic game behavior across different hardware

### Zia Example

```rust
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

| Method           | Signature          | Description                                    |
|------------------|--------------------|------------------------------------------------|
| `AddState(id)`   | `Boolean(Integer)` | Register a state ID; returns false if exists   |
| `ClearFlags()`   | `Void()`           | Clear JustEntered/JustExited flags             |
| `HasState(id)`   | `Boolean(Integer)` | Check if state ID is registered                |
| `IsState(id)`    | `Boolean(Integer)` | Check if currently in specified state          |
| `SetInitial(id)` | `Boolean(Integer)` | Set starting state before first update         |
| `Transition(id)` | `Boolean(Integer)` | Transition to a new state                      |
| `Update()`       | `Void()`           | Increment frame counter (call once per frame)  |

### Notes

- State IDs are integers (0 to 31); use constants for readability
- `JustEntered` and `JustExited` are true for one frame after transition
- Call `Update()` once per frame to track `FramesInState`
- Call `ClearFlags()` at end of frame if checking flags multiple times

### Zia Example

```rust
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
| `Impulse(amount)`   | `Void(Double)` | Add instant displacement to current value|
| `SetImmediate(val)` | `Void(Double)` | Set value and target immediately         |
| `Update()`          | `Void()`       | Advance interpolation (call each frame)  |

### Notes

- Smoothing factor of 0.9 gives smooth but responsive movement
- Higher values (0.95-0.99) give slower, more gradual movement
- Call `Update()` once per frame to advance the interpolation

### Zia Example

```rust
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
| `Clear()`            | `Void()`            | Release all slots                              |
| `FirstActive()`      | `Integer()`         | Get first active slot (-1 if none)             |
| `GetData(slot)`      | `Integer(Integer)`  | Get user data for slot                         |
| `IsActive(slot)`     | `Boolean(Integer)`  | Check if slot is currently acquired            |
| `NextActive(after)`  | `Integer(Integer)`  | Get next active slot after index               |
| `Release(slot)`      | `Boolean(Integer)`  | Return slot to pool; false if invalid          |
| `SetData(slot,data)` | `Boolean(Int,Int)`  | Associate user data with slot                  |

### Zia Example

```rust
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


## See Also

- [Physics & Collision](physics.md)
- [Animation & Movement](animation.md)
- [Visual Effects](effects.md)
- [Game Utilities Overview](README.md)
- [Viper Runtime Library](../README.md)
