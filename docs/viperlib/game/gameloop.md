---
status: active
audience: public
last-verified: 2026-07-15
---

# GameBase + IScene — Game Loop Framework
> Eliminates game loop boilerplate with a reusable base class and scene interface.

**Part of [Viper Runtime Library](../README.md) › [Game Utilities](README.md)**

---

## Overview

Many Viper games need the same canvas creation, frame loop, input polling,
DeltaTime clamping, and frame-pacing code. **GameBase** is an example-library
class that packages that loop, while **IScene** is its interface for organizing
menus, gameplay, game-over screens, and similar states. Neither is a registered
runtime class: import the source files from `examples/games/lib` into an
application.

**Location:** `examples/games/lib/gamebase.zia` and `examples/games/lib/iscene.zia`

---

## IScene Interface

Defines the lifecycle contract for a game scene.

```rust
interface IScene {
    func update(dt: Integer);   // Called every frame with delta time (ms)
    func draw(canvas: Canvas);  // Called every frame to render the scene
    func onEnter();             // Called once when this scene becomes active
    func onExit();              // Called once when transitioning away
}
```

### Scene Lifecycle

```text
setScene(newScene) called
        │
        ▼ (next loop boundary; first boundary if called before run)
  currentScene.onExit()      [only when a scene is already active]
        │
        ▼
  currentScene = newScene
        │
        ▼
  newScene.onEnter()
        │
        ▼ (every frame)
  newScene.update(dt)
  newScene.draw(canvas)
```

---

## GameBase Class

### Import

```rust
bind "../lib/gamebase";   // Adjust relative path for your game
bind "../lib/iscene";
```

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `initGame` | `(title: String, width: Integer, height: Integer)` | Initialize canvas, frame pacing, scene system. Call after `new`. |
| `run` | `()` | Start the main loop. Blocks until `quit()` or a window-close request ends it. |
| `setScene` | `(scene: IScene)` | Queue a scene transition (applied at next frame boundary). |
| `quit` | `()` | Signal the game to stop at end of current frame. |
| `getCanvas` | `() -> Canvas` | Access the canvas for drawing operations. |
| `getDT` | `() -> Integer` | Get current clamped delta time in milliseconds. |
| `getFX` | `() -> ScreenFX` | Access the owned screen-effects instance. |
| `getWidth` | `() -> Integer` | Get canvas width. |
| `getHeight` | `() -> Integer` | Get canvas height. |
| `setDTMax` | `(max: Integer)` | Set the GameBase delta clamp (default 50 ms); pass a positive value. |
| `setTargetFps` | `(fps: Integer)` | Forward the frame-rate target to Canvas (initially 60). `0` selects the graphics default and a negative value is unlimited. |
| `shake` | `(intensity, durationMs, decayMs)` | Start a screen shake. |
| `flash` | `(color, durationMs)` | Start a color flash. |
| `transitionTo` | `(scene, color, durationMs)` | Fade out, queue the scene, then fade in. |

### Override Hooks

| Hook | Signature | When Called |
|------|-----------|------------|
| `onInit` | `()` | Once, at end of `initGame()`. Set up your game here. |
| `onFrame` | `(dt: Integer)` | Every frame, after scene draw. Use for global overlays. |

### Frame Order And Edge Cases

Each loop iteration polls the canvas, clamps `Canvas.DeltaTime` first to at
least 1 ms and then to `dtMax`, updates ScreenFX, completes an orchestrated
fade transition if needed, applies one pending scene, updates and draws the
current scene, draws ScreenFX, calls `onFrame`, and flips the canvas.

`setScene()` called during a scene update is applied on the next iteration.
A scene queued by `onInit()` is applied on the first iteration. Multiple calls
before that boundary are last-write-wins. The framework clears to color `0`
immediately before drawing a real scene; when no scene is active, `onFrame()`
must clear the canvas itself if it does not want prior pixels retained.

The current loop sets `running = false` when it observes `Canvas.ShouldClose`
but still completes that iteration, including callbacks, effect drawing, and
`Flip()`. `quit()` similarly takes effect after the current iteration. A
nonpositive `dtMax` can override the 1 ms lower clamp and expose zero or
negative delta times; this is a known defect, so keep it positive.

---

## Usage Pattern

### Minimal Game (No Scenes)

```rust
bind "../lib/gamebase";
bind Viper.Graphics.Color;

class SimpleGame extends GameBase {
    override expose func onInit() {
        // Setup happens here
    }

    override expose func onFrame(dt: Integer) {
        var canvas = self.getCanvas();
        canvas.Clear(0);
        canvas.Text(20, 20, "Hello, Viper!", RGB(255, 255, 255));

        if canvas.KeyHeld(256) != 0 {   // ESC
            self.quit();
        }
    }
}

func start() {
    var game = new SimpleGame();
    game.initGame("My Game", 800, 600);
    game.run();
}
```

### Full Scene-Based Game

```rust
bind "../lib/gamebase";
bind "../lib/iscene";
bind Viper.Graphics.Canvas;
bind Viper.Graphics.Color;
bind Viper.Input;

// --- Scenes reference GameBase (not the concrete game type) ---

class MenuScene implements IScene {
    hide GameBase game;
    hide IScene playTarget;
    hide Boolean hasTarget;

    expose func init(g: GameBase) {
        game = g;
        hasTarget = false;
    }

    expose func setPlayScene(scene: IScene) {
        playTarget = scene;
        hasTarget = true;
    }

    expose func update(dt: Integer) {
        if Action.Pressed("confirm") {
            if hasTarget {
                game.setScene(playTarget);
            }
        }
    }

    expose func draw(canvas: Canvas) {
        canvas.Text(100, 100, "PRESS ENTER TO PLAY", RGB(255, 255, 255));
    }

    expose func onEnter() { }
    expose func onExit() { }
}

class PlayScene implements IScene {
    hide GameBase game;
    hide IScene menuTarget;
    hide Boolean hasTarget;

    expose func init(g: GameBase) {
        game = g;
        hasTarget = false;
    }

    expose func setMenuScene(scene: IScene) {
        menuTarget = scene;
        hasTarget = true;
    }

    expose func update(dt: Integer) {
        if Action.Pressed("back") {
            if hasTarget {
                game.setScene(menuTarget);
            }
        }
    }

    expose func draw(canvas: Canvas) {
        canvas.Text(100, 100, "PLAYING! Press ESC to return", RGB(0, 255, 0));
    }

    expose func onEnter() { }
    expose func onExit() { }
}

// --- Concrete game wires scenes together ---

class MyGame extends GameBase {
    expose MenuScene menuScene;
    expose PlayScene playScene;

    override expose func onInit() {
        Action.Define("confirm");
        Action.BindKey("confirm", 257);    // Enter

        Action.Define("back");
        Action.BindKey("back", 256);       // Escape

        // Create scenes — pass `self` as GameBase reference
        menuScene = new MenuScene(self);
        playScene = new PlayScene(self);

        // Late-bind scene transitions (breaks circular dependency)
        menuScene.setPlayScene(playScene);
        playScene.setMenuScene(menuScene);

        // Start with menu
        self.setScene(menuScene);
    }
}

func start() {
    var game = new MyGame();
    game.initGame("My Scene Game", 800, 600);
    game.run();
}
```

---

## Design Notes

### Why `initGame()` Instead of `init()`

`initGame()` is this example library's explicit setup contract, not a workaround
for a current inherited-initializer limitation. It lets the subclass be created
first and then initializes the canvas, NullScene sentinels, effects, and finally
the dynamically dispatched `onInit()` hook:

```rust
var game = new MyGame();              // Zero-arg implicit constructor
game.initGame("Title", 800, 600);    // Setup method (inherited from GameBase)
game.run();                           // Start game loop
```

### Why Scenes Use `GameBase` References

Scenes store `GameBase` (the parent type), not the concrete game type. This follows the Dependency Inversion Principle and avoids circular analysis ordering issues:

```rust
class MenuScene implements IScene {
    hide GameBase game;        // Abstract reference — not `hide MyGame game;`
    // ...
}
```

### Why NullScene Instead of Nullable State

GameBase uses a **Null Object** sentinel—a `NullScene` with empty methods—so
all interface fields contain a concrete object before a real scene is installed.
The separate `hasScene` and `hasPending` flags still decide whether lifecycle
methods and rendering run.

### Deferred Scene Transitions

`setScene()` does not switch immediately. It queues one transition for the
scene-transition phase of a later loop iteration. This prevents a scene from
being replaced in the middle of its own `update()` or `draw()`.

---

## Screen Effects (ScreenFX Integration)

GameBase includes ScreenFX for visual effects — shake, flash, and fade.

### Direct Effect Methods

```rust
game.shake(4000, 200, 500);        // intensity, duration, decay (ms)
game.flash(0xFF000088, 160);        // RGBA color, duration (ms)
```

### Orchestrated Transitions

`transitionTo()` provides smooth fade-out → scene switch → fade-in:

```rust
// In a scene's update():
if playerWon {
    game.transitionTo(game.victoryScene, 0x000000FF, 500);  // Black fade, 500ms
}
```

The transition is fully automated:
1. `FadeOut` starts with the specified color/duration
2. On the first later iteration with no active fade-out, `setScene` queues the target
3. That queued scene is applied in the same iteration and `FadeIn` starts automatically

A duration at or below zero creates no fade and therefore becomes a deferred
scene switch on the next loop iteration. Calling `transitionTo()` again while a
transition is pending replaces its target, color, and duration and restarts the
fade-out.

### Custom FX Access

For advanced effects, access ScreenFX directly:

```rust
var fx = game.getFX();
fx.FadeOut(0xFF000080, 300);   // Manual fade control
```

The effect overlay is rendered automatically after the scene's `draw()` and
before `onFrame()`. Global overlay drawing in `onFrame()` therefore appears
above ScreenFX.

---

## Files

| File | Description |
|------|-------------|
| `examples/games/lib/iscene.zia` | IScene interface definition |
| `examples/games/lib/gamebase.zia` | GameBase class + NullScene sentinel |
| `examples/games/lib/test_gamebase.zia` | Validation test (Red/Blue scene switching) |

## See Also

- [Scene Manager](scenemanager.md) — Bounded scene-name registry and transition timer
- [Game Utilities](core.md) — Timer, StateMachine, ObjectPool
- [Visual Effects](effects.md) — ScreenFX for shake, fade, flash
- [Viper Runtime Library](../README.md)
