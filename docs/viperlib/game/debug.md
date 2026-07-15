---
status: active
audience: public
last-verified: 2026-07-15
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
| `Destroy()`         | `Void()`                    | Release this handle's reference; do not use it afterward |
| `Enable()`          | `Void()`                    | Show the overlay                                |
| `Disable()`         | `Void()`                    | Hide the overlay                                |
| `Toggle()`          | `Void()`                    | Toggle visibility                               |
| `Update(dt)`        | `Void(Integer)`             | Update FPS calculation. Call once per frame with delta time (ms) |
| `Watch(name, value)`| `Void(String, Integer)`     | Add or update a named watch variable            |
| `Unwatch(name)`     | `Boolean(String)`           | Remove a watch variable; true when it existed   |
| `Clear()`           | `Void()`                    | Remove all watch variables                      |
| `Draw(canvas)`      | `Void(Canvas)`              | Render the overlay. Call after all other drawing |

---

## FPS Calculation

FPS is computed as a **rolling average** over the last 16 frames using a ring buffer of frame times. This provides smooth, readable values without the jitter of per-frame calculations.

`Update()` records samples even while the overlay is disabled. Negative delta
times become zero; zero-time samples still count toward the 16-sample average,
and an all-zero history reports `0` FPS.

The FPS number is color-coded:
- **Green** (≥ 55 FPS) — Running well
- **Yellow** (30–54 FPS) — Below target
- **Red** (< 30 FPS) — Performance problem

---

## Limits

| Limit | Value |
|-------|-------|
| Max watch variables | 16 |
| Stored watch name | 31 bytes; drawing shows at most 28 bytes |
| FPS history depth | 16 frames |

`Watch` updates an existing exact stored name, fills the first free slot, and
silently ignores a new name when all 16 slots are occupied. Names longer than
31 bytes are silently truncated, potentially through a UTF-8 sequence. Because
lookup currently compares the untruncated argument first, repeatedly watching
the same long name can consume multiple slots and `Unwatch` with that long name
cannot find the stored entry; keep names at or below 31 ASCII bytes.

---

## Example

```rust
module DebugDemo;

bind Viper.Game.DebugOverlay as DebugOverlay;
bind Viper.Graphics.Canvas as Canvas;
bind Viper.Input.Action as Action;

func start() {
    var canvas = Canvas.New("Debug Demo", 800, 600);
    var debug = DebugOverlay.New();
    var score = 0;

    // Bind F3 to toggle debug overlay
    Action.Define("debug_toggle");
    Action.BindKey("debug_toggle", 292);  // F3

    debug.Enable();

    canvas.Poll();
    if Action.Pressed("debug_toggle") {
        debug.Toggle();
    }

    // Update FPS tracking and custom watches
    debug.Update(16);
    debug.Watch("Score", score);
    debug.Watch("Entities", 42);

    // Draw overlay last so it renders on top of the frame
    debug.Draw(canvas);
    canvas.Flip();
}
```

---

## Integration with GameBase

DebugOverlay works naturally in the `onFrame()` hook — it draws after the current scene's `draw()` has completed, so it renders on top of all game content.

```
GameBase.run() loop:
  1. scene.update(dt)
  2. scene.draw(canvas)
  3. ScreenFX.Draw(...)
  4. onFrame(dt)          ← Update and draw DebugOverlay here
  5. canvas.Flip()
```

---

## See Also

- [Game Loop Framework](gameloop.md) — GameBase and IScene
- [Visual Effects](effects.md) — ScreenFX for fade/shake/flash
