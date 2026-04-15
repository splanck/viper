---
status: active
audience: public
last-verified: 2026-04-09
---

# GameBase + IScene — Game Loop Framework
> Eliminates game loop boilerplate with a reusable base class and scene interface.

**Part of [Viper Runtime Library](../README.md) › [Game Utilities](README.md)**

---

## Overview

Every Viper game needs the same ~100 lines of boilerplate: canvas creation, frame loop, input polling, DeltaTime clamping, and frame pacing. **GameBase** encapsulates all of this into a single class you extend, while **IScene** provides a clean interface for organizing game states (menus, gameplay, game over, etc.).

**Location:** `examples/games/lib/gamebase.zia` and `examples/games/lib/iscene.zia`

---

## IScene Interface

Defines the lifecycle contract for a game scene.

```zia
interface IScene {
    func update(dt: Integer);   // Called every frame with delta time (ms)
    func draw(canvas: Canvas);  // Called every frame to render the scene
    func onEnter();             // Called once when this scene becomes active
    func onExit();              // Called once when transitioning away
}
```

### Scene Lifecycle

```
setScene(newScene) called
        │
        ▼ (next frame boundary)
  currentScene.onExit()
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

## GameBase Entity

### Import

```zia
bind "../lib/gamebase";   // Adjust relative path for your game
bind "../lib/iscene";
```

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `initGame` | `(title: String, width: Integer, height: Integer)` | Initialize canvas, frame pacing, scene system. Call after `new`. |
| `run` | `()` | Start the main game loop. Blocks until `quit()` is called. |
| `setScene` | `(scene: IScene)` | Queue a scene transition (applied at next frame boundary). |
| `quit` | `()` | Signal the game to stop at end of current frame. |
| `getCanvas` | `() -> Canvas` | Access the canvas for drawing operations. |
| `getDT` | `() -> Integer` | Get current clamped delta time in milliseconds. |
| `getWidth` | `() -> Integer` | Get canvas width. |
| `getHeight` | `() -> Integer` | Get canvas height. |
| `setDTMax` | `(max: Integer)` | Set maximum delta time clamp (default 50ms). |
| `setTargetFps` | `(fps: Integer)` | Set target frames per second (default 60). |

### Override Hooks

| Hook | Signature | When Called |
|------|-----------|------------|
| `onInit` | `()` | Once, at end of `initGame()`. Set up your game here. |
| `onFrame` | `(dt: Integer)` | Every frame, after scene draw. Use for global overlays. |

---

## Usage Pattern

### Minimal Game (No Scenes)

```zia
bind "../lib/gamebase";
bind Viper.Graphics.Color;

class SimpleGame extends GameBase {
    override expose func onInit() {
        // Setup happens here
    }

    override expose func onFrame(dt: Integer) {
        var canvas = self.getCanvas();
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

```zia
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

Zia's inherited `init()` with arguments has a known code generation limitation. The two-step pattern (`new` + `initGame`) works reliably across modules:

```zia
var game = new MyGame();              // Zero-arg implicit constructor
game.initGame("Title", 800, 600);    // Setup method (inherited from GameBase)
game.run();                           // Start game loop
```

### Why Scenes Use `GameBase` References

Scenes store `GameBase` (the parent type), not the concrete game type. This follows the Dependency Inversion Principle and avoids circular analysis ordering issues:

```zia
class MenuScene implements IScene {
    hide GameBase game;        // Abstract reference — not `hide MyGame game;`
    // ...
}
```

### Why NullScene Instead of `IScene?`

Zia's optional chaining (`?.`) supports field access but not method calls on interface types. Instead of `IScene?` with null checks, GameBase uses a **Null Object** sentinel — a `NullScene` class with empty methods. This eliminates all null-checking complexity.

### Deferred Scene Transitions

`setScene()` doesn't switch immediately — it queues the transition for the start of the next frame. This prevents mid-frame state corruption (e.g., a scene switching during its own `update()`).

---

## Screen Effects (ScreenFX Integration)

GameBase includes ScreenFX for visual effects — shake, flash, and fade.

### Direct Effect Methods

```zia
game.shake(4000, 200, 500);        // intensity, duration, decay (ms)
game.flash(0xFF000088, 160);        // RGBA color, duration (ms)
```

### Orchestrated Transitions

`transitionTo()` provides smooth fade-out → scene switch → fade-in:

```zia
// In a scene's update():
if playerWon {
    game.transitionTo(game.victoryScene, 0x000000FF, 500);  // Black fade, 500ms
}
```

The transition is fully automated:
1. `FadeOut` starts with the specified color/duration
2. When fade-out completes, the scene switches (via `setScene`)
3. `FadeIn` starts automatically

### Custom FX Access

For advanced effects, access ScreenFX directly:

```zia
var fx = game.getFX();
fx.FadeOut(0xFF000080, 300);   // Manual fade control
```

The overlay is rendered automatically — GameBase delegates to `ScreenFX.Draw()` each frame.

---

## Files

| File | Description |
|------|-------------|
| `examples/games/lib/iscene.zia` | IScene interface definition |
| `examples/games/lib/gamebase.zia` | GameBase class + NullScene sentinel |
| `examples/games/lib/test_gamebase.zia` | Validation test (Red/Blue scene switching) |

## See Also

- [Scene Manager](scenemanager.md) — Multi-scene transitions with crossfade
- [Game Utilities](core.md) — Timer, StateMachine, ObjectPool
- [Visual Effects](effects.md) — ScreenFX for shake, fade, flash
- [Viper Runtime Library](../README.md)
