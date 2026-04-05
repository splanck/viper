---
status: active
audience: public
last-verified: 2026-04-05
---

# DebugOverlay
> Real-time debug information overlay for game development.

**Part of [Viper Runtime Library](../README.md) › [Game Utilities](README.md)**

---

## Overview

`DebugOverlay` renders a semi-transparent panel showing FPS, delta time, and custom watch variables. Toggle it with a key binding during development — no code removal needed for release.

**Type:** Instance (obj)
**Constructor:** `DebugOverlay.New()`

---

## Properties

| Property    | Type    | Access | Description                              |
|-------------|---------|--------|------------------------------------------|
| `IsEnabled` | Boolean | Read   | Whether the overlay is currently visible |
| `Fps`       | Integer | Read   | Current frames per second (rolling avg)  |

---

## Methods

| Method              | Signature                   | Description                                     |
|---------------------|-----------------------------|-------------------------------------------------|
| `Enable()`          | `Void()`                    | Show the overlay                                |
| `Disable()`         | `Void()`                    | Hide the overlay                                |
| `Toggle()`          | `Void()`                    | Toggle visibility                               |
| `Update(dt)`        | `Void(Integer)`             | Update FPS calculation. Call once per frame with delta time (ms) |
| `Watch(name, value)`| `Void(String, Integer)`     | Add or update a named watch variable            |
| `Unwatch(name)`     | `Void(String)`              | Remove a watch variable                         |
| `Clear()`           | `Void()`                    | Remove all watch variables                      |
| `Draw(canvas)`      | `Void(Canvas)`              | Render the overlay. Call after all other drawing |

---

## FPS Calculation

FPS is computed as a **rolling average** over the last 16 frames using a ring buffer of frame times. This provides smooth, readable values without the jitter of per-frame calculations.

The FPS number is color-coded:
- **Green** (≥ 55 FPS) — Running well
- **Yellow** (30–54 FPS) — Below target
- **Red** (< 30 FPS) — Performance problem

---

## Limits

| Limit | Value |
|-------|-------|
| Max watch variables | 16 |
| Watch name length | 31 characters |
| FPS history depth | 16 frames |

---

## Example

```zia
module DebugDemo;

bind Viper.Game;
bind Viper.Graphics;
bind Viper.Input;

class MyGame extends GameBase {
    hide DebugOverlay debug;
    hide Integer score;

    override expose func onInit() {
        debug = DebugOverlay.New();
        score = 0;

        // Bind F3 to toggle debug overlay
        Action.Define("debug_toggle");
        Action.BindKey("debug_toggle", 292);  // F3
    }

    override expose func onFrame(dt: Integer) {
        // Toggle with F3
        if Action.Pressed("debug_toggle") {
            debug.Toggle();
        }

        // Update FPS tracking
        debug.Update(dt);

        // Add custom watches
        debug.Watch("Score", score);
        debug.Watch("Entities", 42);

        // Draw overlay last (renders on top of everything)
        debug.Draw(self.getCanvas());
    }
}

func start() {
    var game = new MyGame();
    game.initGame("Debug Demo", 800, 600);
    game.run();
}
```

---

## Integration with GameBase

DebugOverlay works naturally in the `onFrame()` hook — it draws after the current scene's `draw()` has completed, so it renders on top of all game content.

```
GameBase.run() loop:
  1. scene.update(dt)
  2. scene.draw(canvas)
  3. onFrame(dt)          ← Update and draw DebugOverlay here
  4. canvas.Flip()
```

---

## See Also

- [Game Loop Framework](gameloop.md) — GameBase and IScene
- [Visual Effects](effects.md) — ScreenFX for fade/shake/flash
