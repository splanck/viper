---
status: active
audience: public
last-verified: 2026-07-14
---

# Input

> Keyboard, mouse, and gamepad input handling for interactive applications.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Input.Key](#viperinputkey)
- [Viper.Input.Keyboard](#viperinputkeyboard)
- [Viper.Input.KeyChord](#viperinputkeychord)
- [Viper.Input.Mouse](#viperinputmouse)
- [Viper.Input.Pad](#viperinputpad)
- [Viper.Input.Action](#viperinputaction)
- [Viper.Input.Manager](#viperinputmanager)

---

## Viper.Input.Key

Canonical keyboard key-code constants for input APIs.

**Type:** Static constants class

Use `Viper.Input.Key` anywhere an API expects an integer key code, including
`Keyboard.IsDown`, `Keyboard.WasPressed`, `Action.BindKey`, and Game3D input
helpers. `Keyboard.Key*` and `Game3D.Keys` remain available as compatibility
mirrors, but new code should import `Key` for constants and keep `Keyboard` for
state/query behavior.

| Group | Properties |
|---|---|
| Sentinel | `Unknown` (value 0) |
| Letters | `A`-`Z` |
| Digits | `Digit0`-`Digit9` |
| Function keys | `F1`-`F12` |
| Arrows | `Up`, `Down`, `Left`, `Right` |
| Navigation | `Home`, `End`, `PageUp`, `PageDown`, `Insert`, `Delete` |
| Editing | `Backspace`, `Tab`, `Enter`, `Space`, `Escape` |
| Modifiers | `LeftShift`, `RightShift`, `LeftControl`, `RightControl`, `LeftAlt`, `RightAlt` |
| Punctuation | `Minus`, `Equals`, `LeftBracket`, `RightBracket`, `Backslash`, `Semicolon`, `Quote`, `Grave`, `Comma`, `Period`, `Slash` |
| Numpad | `Numpad0`-`Numpad9`, `NumpadAdd`, `NumpadSubtract`, `NumpadMultiply`, `NumpadDivide`, `NumpadEnter`, `NumpadDecimal` |

---

## Viper.Input.Keyboard

Comprehensive keyboard input handling for games and interactive applications.

**Type:** Static utility class

The Keyboard class provides two complementary input models:

- **Polling**: Check the current state of any key at any time
- **Event-based**: Query which keys were pressed or released since the last frame

Keyboard state is updated by the active window event poll (`Canvas.Poll()`,
`Canvas3D.Poll()`, or GUI application polling).

### Polling Methods (Current State)

| Method        | Signature           | Description                                                |
|---------------|---------------------|------------------------------------------------------------|
| `AnyDown()`   | `Boolean()`         | Returns true if any key is currently pressed               |
| `GetDown()`   | `Integer()`         | Returns the key code of the first pressed key, or 0        |
| `IsDown(key)` | `Boolean(Integer)`  | Returns true if the specified key is currently held down   |
| `IsUp(key)`   | `Boolean(Integer)`  | Returns true if the specified key is currently released    |

### Event Methods (Since Last Poll)

| Method              | Signature          | Description                                          |
|---------------------|--------------------|------------------------------------------------------|
| `GetPressed()`      | `Seq()`            | Returns boxed integer key codes pressed this frame   |
| `GetReleased()`     | `Seq()`            | Returns boxed integer key codes released this frame  |
| `WasPressed(key)`   | `Boolean(Integer)` | Returns true if the key was pressed this frame       |
| `WasReleased(key)`  | `Boolean(Integer)` | Returns true if the key was released this frame      |

### Text Input Methods

| Method               | Signature  | Description                                     |
|----------------------|------------|-------------------------------------------------|
| `DisableTextInput()` | `Void()`   | Disable text input mode                         |
| `EnableTextInput()`  | `Void()`   | Enable text input mode (for text fields)        |
| `GetText()`          | `String()` | Returns UTF-8 text received during the last poll|

### Modifier State Methods

| Method       | Signature   | Description                              |
|--------------|-------------|------------------------------------------|
| `Alt()`      | `Boolean()` | Returns true if Alt is held              |
| `CapsLock()` | `Boolean()` | Returns true if Caps Lock is on          |
| `Ctrl()`     | `Boolean()` | Returns true if Ctrl is held             |
| `Shift()`    | `Boolean()` | Returns true if Shift is held            |

### Helper Methods

| Method         | Signature          | Description                                        |
|----------------|--------------------|----------------------------------------------------|
| `KeyName(key)` | `String(Integer)`  | Returns human-readable name for a key code         |

### Key Code Constants

Prefer `Viper.Input.Key` for key constants in new code. The `Keyboard.Key*`
properties below remain as compatibility aliases for existing programs.

#### Letters

| Property | Value | Property | Value | Property | Value |
|----------|-------|----------|-------|----------|-------|
| `KeyA`  | 65    | `KeyJ`  | 74    | `KeyS`  | 83    |
| `KeyB`  | 66    | `KeyK`  | 75    | `KeyT`  | 84    |
| `KeyC`  | 67    | `KeyL`  | 76    | `KeyU`  | 85    |
| `KeyD`  | 68    | `KeyM`  | 77    | `KeyV`  | 86    |
| `KeyE`  | 69    | `KeyN`  | 78    | `KeyW`  | 87    |
| `KeyF`  | 70    | `KeyO`  | 79    | `KeyX`  | 88    |
| `KeyG`  | 71    | `KeyP`  | 80    | `KeyY`  | 89    |
| `KeyH`  | 72    | `KeyQ`  | 81    | `KeyZ`  | 90    |
| `KeyI`  | 73    | `KeyR`  | 82    |          |       |

#### Numbers

| Property  | Value | Property  | Value |
|-----------|-------|-----------|-------|
| `Key0`   | 48    | `Key5`   | 53    |
| `Key1`   | 49    | `Key6`   | 54    |
| `Key2`   | 50    | `Key7`   | 55    |
| `Key3`   | 51    | `Key8`   | 56    |
| `Key4`   | 52    | `Key9`   | 57    |

#### Function Keys

| Property   | Value | Property   | Value |
|------------|-------|------------|-------|
| `KeyF1`   | 290   | `KeyF7`   | 296   |
| `KeyF2`   | 291   | `KeyF8`   | 297   |
| `KeyF3`   | 292   | `KeyF9`   | 298   |
| `KeyF4`   | 293   | `KeyF10`  | 299   |
| `KeyF5`   | 294   | `KeyF11`  | 300   |
| `KeyF6`   | 295   | `KeyF12`  | 301   |

#### Navigation

| Property       | Value | Property       | Value |
|----------------|-------|----------------|-------|
| `KeyUp`       | 265   | `KeyHome`     | 268   |
| `KeyDown`     | 264   | `KeyEnd`      | 269   |
| `KeyLeft`     | 263   | `KeyPageUp`   | 266   |
| `KeyRight`    | 262   | `KeyPageDown` | 267   |
| `KeyInsert`   | 260   | `KeyDelete`   | 261   |

#### Special Keys

| Property        | Value | Property       | Value |
|-----------------|-------|----------------|-------|
| `KeyUnknown`   | 0     | `KeySpace`    | 32    |
| `KeyTab`       | 258   | `KeyEnter`    | 257   |
| `KeyBackspace` | 259   | `KeyEscape`   | 256   |

#### Modifier Keys

| Property      | Value | Property      | Value |
|---------------|-------|---------------|-------|
| `KeyLeftShift`   | 340   | `KeyRightShift`  | 344   |
| `KeyLeftControl`    | 341   | `KeyRightControl`   | 345   |
| `KeyLeftAlt`     | 342   | `KeyRightAlt`    | 346   |

#### Punctuation

| Property         | Value | Property         | Value |
|------------------|-------|------------------|-------|
| `KeyMinus`      | 45    | `KeySemicolon`  | 59    |
| `KeyEquals`     | 61    | `KeyQuote`      | 39    |
| `KeyLeftBracket`   | 91    | `KeyComma`      | 44    |
| `KeyRightBracket`   | 93    | `KeyPeriod`     | 46    |
| `KeyBackslash`  | 92    | `KeySlash`      | 47    |
| `KeyGrave`      | 96    |                  |       |

#### Numpad

| Property       | Value | Property       | Value |
|----------------|-------|----------------|-------|
| `KeyNum0`     | 320   | `KeyNum6`     | 326   |
| `KeyNum1`     | 321   | `KeyNum7`     | 327   |
| `KeyNum2`     | 322   | `KeyNum8`     | 328   |
| `KeyNum3`     | 323   | `KeyNum9`     | 329   |
| `KeyNum4`     | 324   | `KeyNumAdd`   | 334   |
| `KeyNum5`     | 325   | `KeyNumSub`   | 333   |
| `KeyNumDot`   | 330   | `KeyNumMul`   | 332   |
| `KeyNumEnter` | 335   | `KeyNumDiv`   | 331   |

### Zia Example: Basic Game Input

```rust
module GameInput;

bind Viper.Terminal;
bind Viper.Graphics.Canvas as Canvas;
bind Viper.Graphics.Color as Color;
bind Viper.Input.Keyboard as KB;
bind Viper.Input.Key as Key;

func start() {
    var c = Canvas.New("Game", 800, 600);
    var px = 400;
    var py = 300;

    while !c.get_ShouldClose() {
        c.Poll();

        // Movement using polling (smooth, held keys)
        if KB.IsDown(Key.W) { py = py - 5; }
        if KB.IsDown(Key.S) { py = py + 5; }
        if KB.IsDown(Key.A) { px = px - 5; }
        if KB.IsDown(Key.D) { px = px + 5; }

        // Action on single press
        if KB.WasPressed(Key.Space) { Say("Action!"); }

        // Escape to quit
        if KB.WasPressed(Key.Escape) { return; }

        c.Clear(Color.RGB(0, 0, 0));
        c.Box(px - 10, py - 10, 20, 20, Color.RGB(255, 0, 0));
        c.Flip();
    }
}
```

### Example: Basic Game Input

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Game", 800, 600)

DIM playerX AS INTEGER = 400
DIM playerY AS INTEGER = 300
DIM speed AS INTEGER = 5

DO WHILE NOT canvas.ShouldClose
    ' Poll events and update keyboard state
    canvas.Poll()

    ' Movement using polling (smooth, held keys)
    IF Viper.Input.Keyboard.IsDown(Viper.Input.Key.W) THEN
        playerY = playerY - speed
    END IF
    IF Viper.Input.Keyboard.IsDown(Viper.Input.Key.S) THEN
        playerY = playerY + speed
    END IF
    IF Viper.Input.Keyboard.IsDown(Viper.Input.Key.A) THEN
        playerX = playerX - speed
    END IF
    IF Viper.Input.Keyboard.IsDown(Viper.Input.Key.D) THEN
        playerX = playerX + speed
    END IF

    ' Action using event (single press)
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Key.Space) THEN
        ' Fire weapon or jump - only triggers once per press
        PRINT "Action!"
    END IF

    ' Escape to quit
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Key.Escape) THEN
        EXIT DO
    END IF

    ' Draw
    canvas.Clear(0)
    canvas.Box(playerX - 10, playerY - 10, 20, 20, 16711680)
    canvas.Flip()
LOOP
```

### Example: Displaying Pressed Keys

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Key Test", 400, 300)

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    ' Get all keys pressed this frame
    DIM pressed AS Viper.Collections.Seq
    pressed = Viper.Input.Keyboard.GetPressed()

    DIM i AS INTEGER
    FOR i = 0 TO Viper.Collections.Seq.get_Count(pressed) - 1
        DIM key AS INTEGER
        key = Viper.Core.Box.ToI64(pressed.Get(i))
        PRINT "Pressed: "; Viper.Input.Keyboard.KeyName(key)
    NEXT i

    canvas.Clear(0)
    canvas.Flip()
LOOP
```

### Example: Text Input Field

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Text Input", 400, 100)

DIM inputText AS STRING = ""

' Enable text input mode
Viper.Input.Keyboard.EnableTextInput()

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    ' Get typed text
    DIM typed AS STRING
    typed = Viper.Input.Keyboard.GetText()
    inputText = inputText + typed

    ' Handle backspace
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Key.Backspace) THEN
        IF LEN(inputText) > 0 THEN
            inputText = LEFT$(inputText, LEN(inputText) - 1)
        END IF
    END IF

    ' Submit on Enter
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Key.Enter) THEN
        PRINT "Submitted: "; inputText
        inputText = ""
    END IF

    canvas.Clear(0)
    ' Draw text field (you'd use a text rendering method)
    canvas.Flip()
LOOP

Viper.Input.Keyboard.DisableTextInput()
```

### Notes

- Keyboard state advances when the active Canvas, Canvas3D, or GUI application polls events
- `WasPressed()` and `WasReleased()` only return true for the poll that receives the event
- Use polling (`IsDown()`) for continuous input like movement
- Use events (`WasPressed()`) for discrete actions like menu selection or jumping
- Key codes are GLFW-compatible values for portability
- `GetPressed()` and `GetReleased()` contain boxed integers; BASIC callers must use
  `Viper.Core.Box.ToI64()` when reading a `Seq` element
- Text input mode should be enabled for text fields and disabled otherwise. `GetText()` contains
  UTF-8 text only while the mode is enabled, and its per-poll buffer is cleared before the next
  event collection

### Integration with Canvas

The Keyboard class automatically integrates with each windowing frontend. During an event poll:

1. Platform keyboard events are collected
2. Key state arrays are updated
3. Pressed/released event lists are populated
4. Text input buffer is updated

You do not initialize the keyboard separately; creating a Canvas, Canvas3D, or GUI application
provides the event source.

---

## Viper.Input.KeyChord

Key chord (simultaneous) and combo (sequential) detection for complex input patterns. Supports named chords with
configurable timing windows.

**Type:** Instance class
**Constructor:** `Viper.Input.KeyChord.New()`

### Properties

| Property | Type    | Access | Description                             |
|----------|---------|--------|-----------------------------------------|
| `Count`  | Integer | Read   | Number of registered chords and combos  |

### Methods

| Method                             | Signature                          | Description                                           |
|------------------------------------|------------------------------------|-------------------------------------------------------|
| `Active(name)`                      | `Boolean(String)`                  | Check if a chord is currently active (all keys held)  |
| `Clear()`                           | `Void()`                           | Remove all registered chords and combos               |
| `Define(name, keys)`                | `Void(String, Seq)`                | Register a named simultaneous chord (1-16 keys)       |
| `DefineCombo(name, keys, frames)`   | `Void(String, Seq, Integer)`       | Register a named sequence (1-16 keys; maximum gap)    |
| `Progress(name)`                    | `Integer(String)`                  | Get combo progress (number of keys matched so far)    |
| `Remove(name)`                      | `Boolean(String)`                  | Remove a named chord or combo; returns true if found  |
| `Triggered(name)`                   | `Boolean(String)`                  | Check if a chord/combo was triggered this frame       |
| `Update()`                          | `Void()`                           | Update detection state (call once per frame)          |

### Notes

- **Chords** detect simultaneous key presses (e.g., Ctrl+Shift+S). `Active` remains true while
  every key is held; `Triggered` is true only when the chord transitions from inactive to active.
- **Combos** detect ordered key presses (e.g., up-up-down-down). `frames` is the maximum gap
  between consecutive matches, not a limit on the entire sequence. Values less than or equal to
  zero select the 15-frame default. Pressing another key that also occurs in the sequence but is
  not the next expected key resets progress.
- Each definition must contain 1-16 boxed integer key codes. Defining an existing name replaces
  its prior chord or combo.
- Call `Update()` once per frame after `Canvas.Poll()` to refresh detection state.
- A completed combo has `Active` and `Triggered` set only until the next `Update()`; its progress
  resets to zero. For a chord, `Progress` is its key count while active and zero otherwise.

### Zia Example

```rust
module KeyChordDemo;

bind Viper.Input;
bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Input.Key as Key;

func start() {
    var kc = KeyChord.New();

    // Define a chord (Ctrl+S)
    var saveKeys = Seq.New();
    saveKeys.Push(Key.LeftControl);
    saveKeys.Push(Key.S);
    kc.Define("save", saveKeys);

    // Define a combo (sequential keys with frame window)
    var konamiKeys = Seq.New();
    konamiKeys.Push(Key.Up);
    konamiKeys.Push(Key.Up);
    konamiKeys.Push(Key.Down);
    konamiKeys.Push(Key.Down);
    kc.DefineCombo("konami", konamiKeys, 60);

    SayInt(kc.Count);  // 2

    // Check state (no keys pressed headlessly)
    kc.Update();
    SayBool(kc.Active("save"));      // false
    SayBool(kc.Triggered("save"));   // false
    SayInt(kc.Progress("konami"));   // 0

    // Manage chords
    kc.Remove("save");
    SayInt(kc.Count);  // 1
    kc.Clear();
    SayInt(kc.Count);  // 0
}
```

### BASIC Example

```basic
DIM detector AS OBJECT = Viper.Input.KeyChord.New()
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Key Chords", 400, 300)

' Register Ctrl+S chord
DIM saveKeys AS OBJECT = NEW Viper.Collections.Seq()
saveKeys.Push(Viper.Core.Box.I64(Viper.Input.Key.LeftControl))
saveKeys.Push(Viper.Core.Box.I64(Viper.Input.Key.S))
detector.Define("save", saveKeys)

' Register Konami code combo (timing window: 30 frames)
DIM konamiKeys AS OBJECT = NEW Viper.Collections.Seq()
' Up, Up, Down, Down
konamiKeys.Push(Viper.Core.Box.I64(Viper.Input.Key.Up))
konamiKeys.Push(Viper.Core.Box.I64(Viper.Input.Key.Up))
konamiKeys.Push(Viper.Core.Box.I64(Viper.Input.Key.Down))
konamiKeys.Push(Viper.Core.Box.I64(Viper.Input.Key.Down))
detector.DefineCombo("konami", konamiKeys, 30)

' In game loop
DO WHILE NOT canvas.ShouldClose
    canvas.Poll()
    detector.Update()

    IF detector.Triggered("save") THEN
        PRINT "Save chord triggered"
    END IF
    IF detector.Triggered("konami") THEN
        PRINT "Cheat activated!"
    END IF

    ' Show combo progress
    DIM prog AS INTEGER = detector.Progress("konami")
    IF prog > 0 THEN
        PRINT "Konami progress: "; prog; " / 4"
    END IF

    canvas.Flip()
LOOP
```

---

## Viper.Input.Mouse

Comprehensive mouse input handling for games and interactive applications.

**Type:** Static utility class

The Mouse class provides:

- **Position tracking**: Current position and movement delta
- **Button state polling**: Check if buttons are currently pressed
- **Button events**: Query presses, releases, clicks, and double-clicks since last frame
- **Scroll wheel**: Horizontal and vertical scroll amounts
- **Cursor control**: Show, hide, mark as captured, and position the cursor

Mouse state is updated by the active window event poll. Coordinates use the canvas's logical
coordinate system: the origin is at the top left and positive Y points down.

### Position Methods

| Method      | Signature   | Description                                           |
|-------------|-------------|-------------------------------------------------------|
| `DeltaX()`  | `Integer()` | Horizontal movement since last frame                  |
| `DeltaY()`  | `Integer()` | Vertical movement since last frame                    |
| `DeltaXF()` | `Float()`   | Sub-pixel horizontal movement (relative mouse mode)   |
| `DeltaYF()` | `Float()`   | Sub-pixel vertical movement (relative mouse mode)     |
| `X()`       | `Integer()` | Current X position relative to the canvas             |
| `Y()`       | `Integer()` | Current Y position relative to the canvas             |

### Button State Methods (Polling)

| Method            | Signature          | Description                                        |
|-------------------|--------------------|----------------------------------------------------|
| `IsDown(button)`  | `Boolean(Integer)` | Returns true if the button is currently held down  |
| `IsUp(button)`    | `Boolean(Integer)` | Returns true if the button is currently released   |
| `Left()`          | `Boolean()`        | Returns true if the left button is held            |
| `Middle()`        | `Boolean()`        | Returns true if the middle button is held          |
| `Right()`         | `Boolean()`        | Returns true if the right button is held           |

### Button Event Methods (Since Last Poll)

| Method                     | Signature          | Description                                           |
|----------------------------|--------------------|-------------------------------------------------------|
| `WasClicked(button)`       | `Boolean(Integer)` | True on release after a press lasting at most 300 ms  |
| `WasDoubleClicked(button)` | `Boolean(Integer)` | True when that click follows one within 400 ms        |
| `WasPressed(button)`       | `Boolean(Integer)` | Returns true if the button was pressed this frame     |
| `WasReleased(button)`      | `Boolean(Integer)` | Returns true if the button was released this frame    |

### Scroll Wheel Methods

| Method      | Signature   | Description                                           |
|-------------|-------------|-------------------------------------------------------|
| `WheelX()`  | `Integer()` | Horizontal scroll sum, truncated toward zero          |
| `WheelY()`  | `Integer()` | Vertical scroll sum, truncated toward zero (+ = up)   |
| `WheelXF()` | `Double()`  | Horizontal scroll sum with fractional precision       |
| `WheelYF()` | `Double()`  | Vertical scroll sum with fractional precision (+ = up)|

### Cursor Control Methods

| Method           | Signature                 | Description                                         |
|------------------|---------------------------|-----------------------------------------------------|
| `Capture()`      | `Void()`                  | Set the capture flag and hide the cursor             |
| `Hide()`         | `Void()`                  | Hide the cursor                                     |
| `IsCaptured()`   | `Boolean()`               | Return the runtime capture flag                      |
| `IsHidden()`     | `Boolean()`               | Return whether the cursor is hidden                  |
| `Release()`      | `Void()`                  | Clear the capture flag and show the cursor           |
| `SetPos(x, y)`   | `Void(Integer, Integer)`  | Update the stored position and warp the bound cursor |
| `Show()`         | `Void()`                  | Show the cursor                                     |

`Capture()` is not an operating-system pointer lock or confinement API. By itself it leaves
absolute motion bounded by the desktop/window. Use Canvas3D relative mode for unbounded camera
input.

### Relative (Raw) Mouse Mode — FPS Mouse-Look

`SetRelativeMode(true)` requests unbounded mouse motion and implies `Capture()`;
`SetRelativeMode(false)` releases capture. The request is applied only while `Canvas3D.Poll()`
drives input. A 2D Canvas or GUI poll records the request and hides the cursor but does not engage
native relative input or the center-warp fallback.

With Canvas3D polling, the absolute position freezes and motion is exposed through
`DeltaXF()`/`DeltaYF()`; `DeltaX()`/`DeltaY()` return rounded integer deltas.

Canvas3D uses Windows raw input, macOS cursor dissociation, or Linux XInput2 when available. If
native relative input cannot be enabled, it falls back to warping to the window center, with
integer precision. `RelativeModeNative` distinguishes the native path from the fallback.

| Method / Property         | Signature        | Description                                       |
|---------------------------|------------------|---------------------------------------------------|
| `SetRelativeMode(on)`     | `Void(Boolean)`  | Enable/disable relative mode (implies Capture)    |
| `RelativeMode`            | `Boolean`        | True while relative mode is requested             |
| `RelativeModeNative`      | `Boolean`        | True when native raw deltas are active            |

```zia
module RelativeMouseRequest;

bind Viper.Input.Mouse as Mouse;

func start() {
    Mouse.SetRelativeMode(true);  // request Canvas3D mouse-look
    // Each frame after Canvas3D.Poll():
    var lookX = Mouse.DeltaXF();  // sub-pixel, unbounded
    var lookY = Mouse.DeltaYF();
    Mouse.SetRelativeMode(false); // back to a normal cursor
}
```

In `Viper.Game3D`, prefer `Input3D.SetRelativeLook(true)` — it enables the
same mode and feeds `Input3D.LookAxis()`/`MouseDelta()` with the sub-pixel
deltas automatically. Focus loss suspends relative mode (the desktop gets a
normal cursor back); it resumes when the window regains focus.

### Button Constants

| Property        | Value | Description              |
|-----------------|-------|--------------------------|
| `ButtonLeft`   | 0     | Left mouse button        |
| `ButtonMiddle` | 2     | Middle mouse button      |
| `ButtonRight`  | 1     | Right mouse button       |
| `ButtonX1`     | 3     | Extra button 1 (back)    |
| `ButtonX2`     | 4     | Extra button 2 (forward) |

### Zia Example: Mouse Drawing

```rust
module MouseDraw;

bind Viper.Graphics.Canvas as Canvas;
bind Viper.Graphics.Color as Color;
bind Viper.Input.Mouse as Mouse;

func start() {
    var c = Canvas.New("Draw", 800, 600);
    c.Clear(Color.RGB(0, 0, 0));

    while !c.get_ShouldClose() {
        c.Poll();

        // Draw while left button held
        if Mouse.Left() {
            c.Disc(Mouse.X(), Mouse.Y(), 5, Color.RGB(255, 0, 0));
        }

        // Clear on right click
        if Mouse.WasClicked(Mouse.get_ButtonRight()) {
            c.Clear(Color.RGB(0, 0, 0));
        }

        c.Flip();
    }
}
```

### Example: Drawing with Mouse

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Draw", 800, 600)

DIM drawing AS INTEGER = 0

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    ' Start drawing when left button pressed
    IF Viper.Input.Mouse.WasPressed(Viper.Input.Mouse.ButtonLeft) THEN
        drawing = 1
    END IF

    ' Stop drawing when released
    IF Viper.Input.Mouse.WasReleased(Viper.Input.Mouse.ButtonLeft) THEN
        drawing = 0
    END IF

    ' Draw at mouse position while button held
    IF drawing = 1 THEN
        DIM mx AS INTEGER = Viper.Input.Mouse.X()
        DIM my AS INTEGER = Viper.Input.Mouse.Y()
        canvas.Disc(mx, my, 5, 16711680)
    END IF

    ' Clear on right click
    IF Viper.Input.Mouse.WasClicked(Viper.Input.Mouse.ButtonRight) THEN
        canvas.Clear(0)
    END IF

    canvas.Flip()
LOOP
```

### Example: Drag and Drop

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Drag Box", 800, 600)

DIM boxX AS INTEGER = 350
DIM boxY AS INTEGER = 250
DIM boxW AS INTEGER = 100
DIM boxH AS INTEGER = 100
DIM dragging AS INTEGER = 0
DIM offsetX AS INTEGER = 0
DIM offsetY AS INTEGER = 0

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    DIM mx AS INTEGER = Viper.Input.Mouse.X()
    DIM my AS INTEGER = Viper.Input.Mouse.Y()

    ' Check if mouse is over the box
    DIM overBox AS INTEGER = 0
    IF mx >= boxX AND mx < boxX + boxW THEN
        IF my >= boxY AND my < boxY + boxH THEN
            overBox = 1
        END IF
    END IF

    ' Start dragging on press
    IF Viper.Input.Mouse.WasPressed(Viper.Input.Mouse.ButtonLeft) THEN
        IF overBox = 1 THEN
            dragging = 1
            offsetX = mx - boxX
            offsetY = my - boxY
        END IF
    END IF

    ' Stop dragging on release
    IF Viper.Input.Mouse.WasReleased(Viper.Input.Mouse.ButtonLeft) THEN
        dragging = 0
    END IF

    ' Update position while dragging
    IF dragging = 1 THEN
        boxX = mx - offsetX
        boxY = my - offsetY
    END IF

    ' Draw
    canvas.Clear(2236962)

    ' Draw box (highlight if hovering or dragging)
    DIM color AS INTEGER = 4473924
    IF overBox = 1 OR dragging = 1 THEN
        color = 6710886
    END IF
    canvas.Box(boxX, boxY, boxW, boxH, color)
    canvas.Frame(boxX, boxY, boxW, boxH, 16777215)

    canvas.Flip()
LOOP
```

### Example: First-Person Camera Input

```basic
DIM canvas AS Viper.Graphics3D.Canvas3D
canvas = Viper.Graphics3D.Canvas3D.New("FPS Camera", 800, 600)

DIM cameraYaw AS DOUBLE = 0.0
DIM cameraPitch AS DOUBLE = 0.0
DIM sensitivity AS DOUBLE = 0.002

' Canvas3D.Poll applies native relative mode or its center-warp fallback
Viper.Input.Mouse.SetRelativeMode(TRUE)

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    ' Use mouse delta for camera rotation
    DIM dx AS DOUBLE = Viper.Input.Mouse.DeltaXF()
    DIM dy AS DOUBLE = Viper.Input.Mouse.DeltaYF()

    cameraYaw = cameraYaw + dx * sensitivity
    cameraPitch = cameraPitch - dy * sensitivity

    ' Clamp pitch
    IF cameraPitch > 1.55 THEN cameraPitch = 1.55
    IF cameraPitch < -1.55 THEN cameraPitch = -1.55

    ' Escape to release cursor and exit
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Key.Escape) THEN
        Viper.Input.Mouse.SetRelativeMode(FALSE)
        EXIT DO
    END IF

    canvas.Clear(0.0, 0.0, 0.0)
    ' Render the scene using cameraYaw and cameraPitch here
    canvas.Flip()
LOOP
```

### Example: Scroll Zoom

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Zoom", 800, 600)

DIM zoom AS INTEGER = 100  ' percentage

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    ' Scroll wheel to zoom
    DIM scroll AS INTEGER = Viper.Input.Mouse.WheelY()
    zoom = zoom + scroll * 10

    ' Clamp zoom level
    IF zoom < 10 THEN zoom = 10
    IF zoom > 500 THEN zoom = 500

    canvas.Clear(0)

    ' Draw something at zoom level
    DIM size AS INTEGER = zoom / 2
    DIM cx AS INTEGER = 400 - size / 2
    DIM cy AS INTEGER = 300 - size / 2
    canvas.Box(cx, cy, size, size, 65280)

    canvas.Flip()
LOOP
```

### Notes

- Mouse state advances when the active Canvas, Canvas3D, or GUI application polls events
- `WasPressed()`, `WasReleased()`, `WasClicked()`, and `WasDoubleClicked()` are per-poll flags
- Delta and wheel values are reset at the start of each poll
- A click is a press/release of at most 300 ms; a second click within 400 ms also sets the
  double-click flag
- Use `Left()`, `Right()`, `Middle()` as shortcuts for common button checks
- `Capture()` only hides and records state; Canvas3D relative mode provides FPS-style motion
- **Known issue:** in ordinary absolute mode, `DeltaX()`/`DeltaY()` are calculated before current
  events are pumped and therefore trail `X()`/`Y()` by one poll (see
  [VDOC-006](../documentation-review-findings.md#vdoc-006--absolute-mouse-deltas-lag-one-poll-behind))

### Integration with Canvas

The Mouse class automatically integrates with each windowing frontend. During an event poll:

1. Per-poll edge, click, delta, and wheel state is reset
2. Platform events update position, buttons, click detection, and scroll accumulators
3. A Canvas3D poll applies requested native or fallback relative motion

You do not initialize the mouse separately; creating a Canvas, Canvas3D, or GUI application
provides the event source.

---

## Viper.Input.Pad

Gamepad/controller input handling for games and interactive applications.

**Type:** Static utility class

The Pad class maps supported controllers to one standard logical layout:

- **Controller enumeration**: Four fixed slots with indices 0-3
- **Button state polling**: Check if buttons are currently pressed
- **Button events**: Query presses and releases since last frame
- **Analog sticks**: Left and right stick X/Y axes (-1.0 to 1.0)
- **Triggers**: Left and right trigger values (0.0 to 1.0)
- **Deadzone handling**: Configurable stick deadzone
- **Vibration/rumble**: Control haptic feedback motors

Gamepad state is updated by Canvas or Canvas3D polling.

### Controller Enumeration Methods

| Method               | Signature          | Description                                     |
|----------------------|--------------------|------------------------------------------------------|
| `Count()`            | `Integer()`        | Number of connected slots (0-4)                 |
| `IsConnected(index)` | `Boolean(Integer)` | Returns true if controller is connected         |
| `Name(index)`        | `String(Integer)`  | Controller name/description (empty if invalid)  |

### Button State Methods (Polling)

| Method                  | Signature                   | Description                                        |
|-------------------------|-----------------------------|-----------------------------------------------------|
| `IsDown(index, button)` | `Boolean(Integer, Integer)` | Returns true if the button is currently held down  |
| `IsUp(index, button)`   | `Boolean(Integer, Integer)` | Returns true if the button is currently released   |

### Button Event Methods (Since Last Poll)

| Method                       | Signature                   | Description                                        |
|------------------------------|-----------------------------|----------------------------------------------------|
| `WasPressed(index, button)`  | `Boolean(Integer, Integer)` | Returns true if button was pressed this frame      |
| `WasReleased(index, button)` | `Boolean(Integer, Integer)` | Returns true if button was released this frame     |

### Analog Input Methods

| Method                | Signature         | Description                                            |
|-----------------------|-------------------|--------------------------------------------------------|
| `LeftTrigger(index)`  | `Double(Integer)` | Left trigger (0.0 to 1.0, released to fully pressed)   |
| `LeftX(index)`        | `Double(Integer)` | Left stick X axis (-1.0 to 1.0, left to right)         |
| `LeftY(index)`        | `Double(Integer)` | Left stick Y axis (-1.0 to 1.0, up to down)            |
| `RightTrigger(index)` | `Double(Integer)` | Right trigger (0.0 to 1.0, released to fully pressed)  |
| `RightX(index)`       | `Double(Integer)` | Right stick X axis (-1.0 to 1.0, left to right)        |
| `RightY(index)`       | `Double(Integer)` | Right stick Y axis (-1.0 to 1.0, up to down)           |

### Deadzone Methods

| Method                | Signature       | Description                                      |
|-----------------------|-----------------|--------------------------------------------------|
| `GetDeadzone()`       | `Double()`      | Get current deadzone radius (default 0.1)        |
| `SetDeadzone(radius)` | `Void(Double)`  | Set stick deadzone radius (0.0 to 1.0)           |

The runtime clamps the configured radius to `[0.0, 1.0]`. It applies a radial deadzone to each
stick: values inside the radius become zero and values outside it are rescaled over the remaining
range. Triggers are clamped to `[0.0, 1.0]` but do not use this deadzone.

### Vibration Methods

| Method                        | Signature                       | Description                              |
|-------------------------------|---------------------------------|------------------------------------------|
| `StopVibration(index)`        | `Void(Integer)`                 | Stop vibration on controller             |
| `Vibrate(index, left, right)` | `Void(Integer, Double, Double)` | Set motor intensities (0.0 to 1.0)       |

Finite vibration values are clamped to `[0.0, 1.0]`. Unsupported devices/platforms ignore the
request.

### Button Constants

Standard gamepad layout compatible with Xbox and PlayStation controllers:

| Property        | Value | Xbox          | PlayStation    |
|-----------------|-------|---------------|----------------|
| `ButtonA`         | 0     | A             | Cross (X)      |
| `ButtonB`         | 1     | B             | Circle (O)     |
| `ButtonX`         | 2     | X             | Square         |
| `ButtonY`         | 3     | Y             | Triangle       |
| `ButtonLeftBumper`        | 4     | Left Bumper   | L1             |
| `ButtonRightBumper`        | 5     | Right Bumper  | R1             |
| `ButtonBack`      | 6     | Back/View     | Share          |
| `ButtonStart`     | 7     | Start/Menu    | Options        |
| `ButtonLeftStick`    | 8     | Left Stick    | L3             |
| `ButtonRightStick`    | 9     | Right Stick   | R3             |
| `ButtonUp`        | 10    | D-pad Up      | D-pad Up       |
| `ButtonDown`      | 11    | D-pad Down    | D-pad Down     |
| `ButtonLeft`      | 12    | D-pad Left    | D-pad Left     |
| `ButtonRight`     | 13    | D-pad Right   | D-pad Right    |
| `ButtonGuide`     | 14    | Xbox Button   | PS Button      |

### Zia Example: Controller Movement

```rust
module PadDemo;

bind Viper.Terminal;
bind Viper.Input.Pad as Pad;
bind Viper.Graphics.Canvas as Canvas;
bind Viper.Graphics.Color as Color;
bind Viper.Text.Fmt as Fmt;

func start() {
    var c = Canvas.New("Controller", 800, 600);
    var px = 400.0;
    var py = 300.0;

    while !c.get_ShouldClose() {
        c.Poll();

        if Pad.IsConnected(0) {
            // Left stick movement
            px = px + Pad.LeftX(0) * 5.0;
            py = py + Pad.LeftY(0) * 5.0;

            if Pad.WasPressed(0, Pad.get_ButtonA()) { Say("Jump!"); }

            // Trigger for shooting
            if Pad.RightTrigger(0) > 0.5 { Say("Shooting!"); }
        }

        c.Clear(Color.RGB(0, 0, 0));
        c.Flip();
    }
}
```

### Example: Basic Controller Movement

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Controller Demo", 800, 600)

DIM playerX AS DOUBLE = 400.0
DIM playerY AS DOUBLE = 300.0
DIM speed AS DOUBLE = 5.0

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    ' Check if controller 0 is connected
    IF Viper.Input.Pad.IsConnected(0) THEN
        ' Movement with left stick
        DIM lx AS DOUBLE = Viper.Input.Pad.LeftX(0)
        DIM ly AS DOUBLE = Viper.Input.Pad.LeftY(0)

        playerX = playerX + lx * speed
        playerY = playerY + ly * speed

        ' Action on A button press
        IF Viper.Input.Pad.WasPressed(0, Viper.Input.Pad.ButtonA) THEN
            PRINT "Jump!"
        END IF

        ' Shoot while holding right trigger
        DIM rt AS DOUBLE = Viper.Input.Pad.RightTrigger(0)
        IF rt > 0.5 THEN
            PRINT "Shooting! Power: "; rt
        END IF
    END IF

    canvas.Clear(0)
    canvas.Disc(INT(playerX), INT(playerY), 20, 16711680)
    canvas.Flip()
LOOP
```

### Example: Controller Vibration

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Rumble Test", 400, 300)

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    IF Viper.Input.Pad.IsConnected(0) THEN
        ' Strong rumble on left bumper
        IF Viper.Input.Pad.WasPressed(0, Viper.Input.Pad.ButtonLeftBumper) THEN
            Viper.Input.Pad.Vibrate(0, 1.0, 0.3)
        END IF

        ' Light rumble on right bumper
        IF Viper.Input.Pad.WasPressed(0, Viper.Input.Pad.ButtonRightBumper) THEN
            Viper.Input.Pad.Vibrate(0, 0.3, 1.0)
        END IF

        ' Stop on B button
        IF Viper.Input.Pad.WasPressed(0, Viper.Input.Pad.ButtonB) THEN
            Viper.Input.Pad.StopVibration(0)
        END IF
    END IF

    canvas.Clear(0)
    canvas.Flip()
LOOP
```

### Example: Dual Stick Camera Control

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Twin Stick", 800, 600)

DIM posX AS DOUBLE = 400.0
DIM posY AS DOUBLE = 300.0
DIM aimAngle AS DOUBLE = 0.0

' Adjust deadzone for precision
Viper.Input.Pad.SetDeadzone(0.15)

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    IF Viper.Input.Pad.IsConnected(0) THEN
        ' Movement with left stick
        posX = posX + Viper.Input.Pad.LeftX(0) * 3.0
        posY = posY + Viper.Input.Pad.LeftY(0) * 3.0

        ' Aiming with right stick
        DIM rx AS DOUBLE = Viper.Input.Pad.RightX(0)
        DIM ry AS DOUBLE = Viper.Input.Pad.RightY(0)
        IF ABS(rx) > 0.1 OR ABS(ry) > 0.1 THEN
            aimAngle = Viper.Math.Atan2(ry, rx)
        END IF
    END IF

    canvas.Clear(0)

    ' Draw player
    canvas.Disc(INT(posX), INT(posY), 15, 65280)

    ' Draw aim direction
    DIM aimX AS INTEGER = INT(posX + COS(aimAngle) * 30)
    DIM aimY AS INTEGER = INT(posY + SIN(aimAngle) * 30)
    canvas.Line(INT(posX), INT(posY), aimX, aimY, 16711680)

    canvas.Flip()
LOOP
```

### Example: Multi-Controller Support

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Multiplayer", 800, 600)

' Colors for each player
DIM colors(3) AS INTEGER
colors(0) = 16711680  ' Red
colors(1) = 255  ' Blue
colors(2) = 65280  ' Green
colors(3) = 16776960  ' Yellow

DIM x(3) AS DOUBLE
DIM y(3) AS DOUBLE

' Initial positions
x(0) = 200 : y(0) = 300
x(1) = 600 : y(1) = 300
x(2) = 400 : y(2) = 150
x(3) = 400 : y(3) = 450

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    DIM i AS INTEGER
    FOR i = 0 TO 3
        IF Viper.Input.Pad.IsConnected(i) THEN
            x(i) = x(i) + Viper.Input.Pad.LeftX(i) * 4.0
            y(i) = y(i) + Viper.Input.Pad.LeftY(i) * 4.0
        END IF
    NEXT i

    canvas.Clear(2236962)

    ' Draw status text
    PRINT "Controllers: "; Viper.Input.Pad.Count()

    ' Draw each player
    FOR i = 0 TO 3
        IF Viper.Input.Pad.IsConnected(i) THEN
            canvas.Disc(INT(x(i)), INT(y(i)), 20, colors(i))
        END IF
    NEXT i

    canvas.Flip()
LOOP
```

### Notes

- Gamepad state advances when Canvas or Canvas3D polls; button edges last for that poll
- The four valid slot indices are 0-3. `Count()` counts connected slots, but slots need not be
  contiguous, so enumerate 0-3 and call `IsConnected()`
- Invalid or disconnected slots return an empty name and zero/false for most queries;
  `IsUp()` returns true
- Raw `Pad` methods do not treat `-1` as “any controller”; that convention belongs to selected
  `Action` and `Manager` button methods
- Vibration intensity may vary by controller model
- macOS rumble requests are ignored

### Platform Support

| Platform | API Used                    | Notes                                              |
|----------|-----------------------------|----------------------------------------------------|
| Windows  | XInput                      | Xbox-compatible controllers; rumble supported             |
| Linux    | evdev (`/dev/input/event*`) | Requires read permission; rumble also needs writable FF support |
| macOS    | IOHIDManager (HID)          | Generic HID gamepads; rumble is a no-op                   |

### Integration with Canvas

The Pad class automatically integrates with Canvas and Canvas3D. During polling:

1. Platform gamepad APIs are queried
2. Controller connection state is updated
3. Button state arrays are updated
4. Analog values are read and deadzone applied
5. Press/release events are detected

You do not initialize gamepads separately.

---

## Viper.Input.Action

High-level action mapping system for device-agnostic input handling.

**Type:** Static utility class

The Action class provides an abstraction layer over raw input devices. Instead of checking individual keys, mouse buttons, or gamepad buttons, you define named "actions" and bind them to multiple input sources. This enables:

- **Input remapping** without code changes
- **Multi-device support** - query a single action that works across keyboard, mouse, and gamepad
- **Axis actions** for analog input (movement, camera control)
- **Consistent state tracking** - pressed, released, and held states

Action state is updated automatically after Canvas or Canvas3D polling.

### Action System Lifecycle

| Method        | Signature | Description                                     |
|---------------|-----------|------------------------------------------------|
| `Clear()`     | `Void()`  | Remove all defined actions and bindings         |
| `LoadPreset(name)` | `Boolean(String)` | Load a predefined set of actions with standard key bindings |

### Action Definition Methods

| Method             | Signature          | Description                                                |
|--------------------|--------------------|------------------------------------------------------------|
| `Define(name)`     | `Boolean(String)`  | Define a new button action; returns false if already exists|
| `DefineAxis(name)` | `Boolean(String)`  | Define a new axis action; returns false if already exists  |
| `Exists(name)`     | `Boolean(String)`  | Check if an action is defined                              |
| `IsAxis(name)`     | `Boolean(String)`  | Check if an action is an axis action                       |
| `Remove(name)`     | `Boolean(String)`  | Remove an action and all its bindings                      |

### Keyboard Binding Methods

| Method                              | Signature                          | Description                                      |
|-------------------------------------|------------------------------------|--------------------------------------------------|
| `BindKey(action, key)`              | `Boolean(String, Integer)`         | Bind a key to a button action                    |
| `BindKeyAxis(action, key, value)`   | `Boolean(String, Integer, Double)` | Bind a key to an axis action with value          |
| `UnbindKey(action, key)`            | `Boolean(String, Integer)`         | Remove a key binding from an action              |

### Key Chord Binding Methods

| Method                        | Signature                 | Description                                           |
|-------------------------------|---------------------------|-------------------------------------------------------|
| `BindChord(action, keys)`     | `Boolean(String, Seq)`    | Bind a simultaneous 2-8-key chord to a button action  |
| `ChordCount(action)`          | `Integer(String)`         | Get the number of chord bindings for an action        |
| `UnbindChord(action, keys)`   | `Boolean(String, Seq)`    | Remove an exact ordered chord definition              |

Action chords are simultaneous only; use `KeyChord` when you need a sequential combo. Detection
does not care which order chord keys are pressed, but `UnbindChord()` matches the same keys in the
same sequence order supplied to `BindChord()`.

### Mouse Binding Methods

| Method                             | Signature                 | Description                                      |
|------------------------------------|---------------------------|--------------------------------------------------|
| `BindMouse(action, button)`        | `Boolean(String, Integer)` | Bind a mouse button to a button action          |
| `BindMouseX(action, sensitivity)`  | `Boolean(String, Double)`  | Bind mouse X delta to an axis action            |
| `BindMouseY(action, sensitivity)`  | `Boolean(String, Double)`  | Bind mouse Y delta to an axis action            |
| `BindScrollX(action, sensitivity)` | `Boolean(String, Double)`  | Bind scroll wheel X to an axis action           |
| `BindScrollY(action, sensitivity)` | `Boolean(String, Double)`  | Bind scroll wheel Y to an axis action           |
| `UnbindMouse(action, button)`      | `Boolean(String, Integer)` | Remove a mouse button binding                   |

### Gamepad Binding Methods

| Method                                          | Signature                                   | Description                                      |
|-------------------------------------------------|---------------------------------------------|--------------------------------------------------|
| `BindPadAxis(action, pad, axis, scale)`         | `Boolean(String, Integer, Integer, Double)` | Bind a gamepad axis to an axis action            |
| `BindPadButton(action, pad, button)`            | `Boolean(String, Integer, Integer)`         | Bind a gamepad button to a button action         |
| `BindPadButtonAxis(action, pad, button, value)` | `Boolean(String, Integer, Integer, Double)` | Bind a gamepad button to an axis action          |
| `UnbindPadAxis(action, pad, axis)`              | `Boolean(String, Integer, Integer)`         | Remove a gamepad axis binding                    |
| `UnbindPadButton(action, pad, button)`          | `Boolean(String, Integer, Integer)`         | Remove a gamepad button binding                  |

**Note:** Use pad index `-1` to match any connected controller. An any-pad axis binding uses the
first nonzero value found in slot order; it does not sum the same axis across controllers.

### Button Action Query Methods

| Method             | Signature          | Description                                                  |
|--------------------|--------------------|------------------------------------------------------------- |
| `Held(action)`     | `Boolean(String)`  | Returns true if any bound input is currently held            |
| `Pressed(action)`  | `Boolean(String)`  | Returns true if any bound input was pressed this frame       |
| `Released(action)` | `Boolean(String)`  | Returns true if any bound input was released this frame      |
| `Strength(action)` | `Double(String)`   | Returns 1.0 if held, 0.0 otherwise                           |

### Axis Action Query Methods

| Method             | Signature         | Description                                                   |
|--------------------|-------------------|---------------------------------------------------------------|
| `Axis(action)`     | `Double(String)`  | Returns combined axis value, clamped to -1.0 to 1.0           |
| `AxisRaw(action)`  | `Double(String)`  | Returns combined axis value, not clamped                      |

### Introspection Methods

| Method                  | Signature          | Description                                              |
|-------------------------|--------------------|---------------------------------------------------------|
| `BindingCount(action)`  | `Integer(String)`  | Returns the number of bindings for an action             |
| `BindingsStr(action)`   | `String(String)`   | Returns human-readable description of bindings           |
| `List()`                | `Seq()`            | Returns action-name strings in newest-defined-first order|

### Conflict Detection Methods

| Method                          | Signature                  | Description                                    |
|---------------------------------|----------------------------|------------------------------------------------|
| `KeyBoundTo(key)`               | `String(Integer)`          | Returns action name if key is bound, else ""   |
| `MouseBoundTo(button)`          | `String(Integer)`          | Returns action name if button is bound, else ""|
| `PadButtonBoundTo(pad, button)` | `String(Integer, Integer)` | Returns action name if bound, else ""          |

### Persistence Methods

| Method        | Signature          | Description                                                           |
|---------------|--------------------|-----------------------------------------------------------------------|
| `Load(json)`  | `Boolean(String)`  | Atomically replace all actions from JSON; false preserves current state |
| `Save()`      | `String()`         | Serialize all actions and bindings to a JSON string                   |

### Action Presets

`LoadPreset()` loads a predefined action set and returns false for an unknown preset name. Existing
actions are retained; compatible preset bindings are added to them instead of replacing them.

#### Available Presets

| Preset Name | Actions Defined | Principal Bindings |
|-------------|-----------------|--------------------|
| `"standard_movement"` | Buttons `move_up/down/left/right`; axes `move_x/y` | WASD, arrows, D-pad, left stick |
| `"menu_navigation"` | `menu_up/down/left/right`, `confirm`, `back` | WASD/arrows/D-pad; Enter or Space/A; Escape/B |
| `"platformer"` | `move_left/right`, `jump`, `shoot`, `pause`; axis `move_x` | A/D or arrows/D-pad/stick; Space/W/Up or A; J/X or pad X; Escape/Start |
| `"topdown"` | `move_up/down/left/right`, `fire`, `pause`; axes `move_x/y` | WASD/arrows/D-pad/stick; Space/J or A; Escape/Start |
| `"fps3d"` | `jump`, `sprint`, `crouch`, `interact`, `fire`, `aim`, `pause`; axes `move_x/y` | Space/Shift/Ctrl/E, mouse buttons, WASD; A/LB/B/X/Start, left stick |

#### Example
```rust
module PresetDemo;

bind Viper.Input.Action as Action;
bind Viper.Input.Key as Key;

func start() {
    Action.Clear();
    Action.LoadPreset("platformer");
    Action.LoadPreset("menu_navigation");

    // Compatible preset bindings are added to existing actions.
    Action.Define("special_attack");
    Action.BindKey("special_attack", Key.Q);
    Action.Clear();
}
```

### Axis Constants

| Property            | Value | Description                     |
|---------------------|-------|---------------------------------|
| `AxisLeftX`       | 0     | Left stick horizontal           |
| `AxisLeftY`       | 1     | Left stick vertical             |
| `AxisRightX`      | 2     | Right stick horizontal          |
| `AxisRightY`      | 3     | Right stick vertical            |
| `AxisLeftTrigger` | 4     | Left trigger (0.0 to 1.0)       |
| `AxisRightTrigger`| 5     | Right trigger (0.0 to 1.0)      |

### Zia Example: Action Mapping

```rust
module ActionDemo;

bind Viper.Terminal;
bind Viper.Input.Action as Action;
bind Viper.Input.Key as Key;
bind Viper.Input.Pad as Pad;
bind Viper.Graphics.Canvas as Canvas;
bind Viper.Graphics.Color as Color;
bind Viper.Text.Fmt as Fmt;

func start() {
    var c = Canvas.New("Action Demo", 800, 600);

    // Define actions
    Action.Define("jump");
    Action.Define("fire");
    Action.DefineAxis("move_x");
    Action.DefineAxis("move_y");

    // Bind keyboard
    Action.BindKey("jump", Key.Space);
    Action.BindKey("fire", Key.Z);
    Action.BindKeyAxis("move_x", Key.Left, -1.0);
    Action.BindKeyAxis("move_x", Key.Right, 1.0);
    Action.BindKeyAxis("move_y", Key.Up, -1.0);
    Action.BindKeyAxis("move_y", Key.Down, 1.0);

    // Bind gamepad (any controller)
    Action.BindPadButton("jump", -1, Pad.get_ButtonA());
    Action.BindPadButton("fire", -1, Pad.get_ButtonX());

    Say("Jump bindings: " + Action.BindingsStr("jump"));

    var px = 400.0;
    var py = 300.0;

    while !c.get_ShouldClose() {
        c.Poll();

        // Device-agnostic movement
        px = px + Action.Axis("move_x") * 5.0;
        py = py + Action.Axis("move_y") * 5.0;

        if Action.Pressed("jump") { Say("Jump!"); }
        if Action.Held("fire") { Say("Firing"); }

        c.Clear(Color.RGB(0, 0, 0));
        c.Flip();
    }

    Action.Clear();
}
```

### Example: Basic Game Actions

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Action Demo", 800, 600)

' Define actions
Viper.Input.Action.Define("jump")
Viper.Input.Action.Define("fire")
Viper.Input.Action.DefineAxis("move_x")
Viper.Input.Action.DefineAxis("move_y")

' Bind keyboard
Viper.Input.Action.BindKey("jump", Viper.Input.Key.Space)
Viper.Input.Action.BindKey("fire", Viper.Input.Key.Z)
Viper.Input.Action.BindKeyAxis("move_x", Viper.Input.Key.Left, -1.0)
Viper.Input.Action.BindKeyAxis("move_x", Viper.Input.Key.Right, 1.0)
Viper.Input.Action.BindKeyAxis("move_y", Viper.Input.Key.Up, -1.0)
Viper.Input.Action.BindKeyAxis("move_y", Viper.Input.Key.Down, 1.0)

' Bind gamepad (any controller)
Viper.Input.Action.BindPadButton("jump", -1, Viper.Input.Pad.ButtonA)
Viper.Input.Action.BindPadButton("fire", -1, Viper.Input.Pad.ButtonX)
Viper.Input.Action.BindPadAxis("move_x", -1, Viper.Input.Action.AxisLeftX, 1.0)
Viper.Input.Action.BindPadAxis("move_y", -1, Viper.Input.Action.AxisLeftY, 1.0)

DIM playerX AS DOUBLE = 400.0
DIM playerY AS DOUBLE = 300.0
DIM speed AS DOUBLE = 5.0

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    ' Movement using axis actions (works with keyboard OR gamepad)
    playerX = playerX + Viper.Input.Action.Axis("move_x") * speed
    playerY = playerY + Viper.Input.Action.Axis("move_y") * speed

    ' Jump action (works with Space OR gamepad A)
    IF Viper.Input.Action.Pressed("jump") THEN
        PRINT "Jump!"
    END IF

    ' Fire action (works with Z OR gamepad X)
    IF Viper.Input.Action.Held("fire") THEN
        PRINT "Firing"
    END IF

    canvas.Clear(0)
    canvas.Disc(INT(playerX), INT(playerY), 20, 16711680)
    canvas.Flip()
LOOP
```

### Example: Mouse Look with Action Mapping

```basic
DIM canvas AS Viper.Graphics3D.Canvas3D
canvas = Viper.Graphics3D.Canvas3D.New("FPS Camera", 800, 600)

' Define look actions
Viper.Input.Action.DefineAxis("look_x")
Viper.Input.Action.DefineAxis("look_y")

' Bind mouse delta with sensitivity
Viper.Input.Action.BindMouseX("look_x", 0.002)
Viper.Input.Action.BindMouseY("look_y", 0.002)

' Also allow gamepad right stick
Viper.Input.Action.BindPadAxis("look_x", -1, Viper.Input.Action.AxisRightX, 0.05)
Viper.Input.Action.BindPadAxis("look_y", -1, Viper.Input.Action.AxisRightY, 0.05)

DIM yaw AS DOUBLE = 0.0
DIM pitch AS DOUBLE = 0.0

' Relative mode is applied by Canvas3D.Poll()
Viper.Input.Mouse.SetRelativeMode(TRUE)

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()

    yaw = yaw + Viper.Input.Action.Axis("look_x")
    pitch = pitch + Viper.Input.Action.Axis("look_y")

    ' Clamp pitch
    IF pitch > 1.57 THEN pitch = 1.57
    IF pitch < -1.57 THEN pitch = -1.57

    canvas.Clear(0.0, 0.0, 0.0)
    ' Render the scene using yaw and pitch here
    canvas.Flip()
LOOP

Viper.Input.Mouse.SetRelativeMode(FALSE)
```

### Example: Rebindable Controls

```basic
' Query current bindings
DIM jumpBindings AS STRING
jumpBindings = Viper.Input.Action.BindingsStr("jump")
PRINT "Jump is bound to: "; jumpBindings

' Check for conflicts
DIM conflict AS STRING
conflict = Viper.Input.Action.KeyBoundTo(Viper.Input.Key.Space)
IF conflict <> "" THEN
    PRINT "Space is already bound to: "; conflict
END IF

' Rebind at runtime
Viper.Input.Action.UnbindKey("jump", Viper.Input.Key.Space)
Viper.Input.Action.BindKey("jump", Viper.Input.Key.W)
```

### Notes

- Action state is updated after Canvas or Canvas3D polling
- `Pressed()`/`Released()` report any bound input edge received during that poll
- Multiple bindings on a button action trigger if ANY binding is active
- Axis bindings are combined (summed) and clamped to -1.0 to 1.0
- Use `AxisRaw()` to get the unclamped sum for advanced use cases
- Mouse-axis bindings use rounded integer mouse deltas; scroll-axis bindings preserve fractional
  wheel input
- Binding the same input to multiple actions is allowed
- Use pad index `-1` on Action gamepad bindings for “any controller”

### Design Philosophy

The Action system follows the principle of "define once, query everywhere":

1. **Define** your game's actions at startup (jump, fire, move_x, etc.)
2. **Bind** multiple inputs to each action (keyboard + mouse + gamepad)
3. **Query** actions in your game logic, not raw inputs

This separation means:
- Players can rebind controls without code changes
- Supporting new input devices only requires adding bindings
- Game logic remains clean and device-agnostic

---

## Viper.Input.Manager

High-level input wrapper with a per-key edge gate and unified direction input.

**Type:** Instance class (requires `New()`)

Unlike the low-level Keyboard, Mouse, and Pad classes that require checking specific keys/buttons, InputManager provides
unified "direction" input that automatically combines keyboard arrows, WASD, D-pad, and analog sticks.

### Constructor

| Method  | Signature        | Description                      |
|---------|------------------|----------------------------------|
| `New()` | `InputManager()` | Create a new input manager       |

### Properties

| Property       | Type    | Description                                    |
|----------------|---------|------------------------------------------------|
| `DebounceDelay`| Integer | Edge-gate timer in frames (r/w; default 12)    |

### Core Methods

| Method     | Signature | Description                                                      |
|------------|-----------|------------------------------------------------------------------|
| `Update()` | `Void()`  | Decrement edge-gate timers; call after the window poll          |

### Unified Direction Properties

These properties check ALL input sources (keyboard, D-pad, analog sticks) and return true if ANY is active:

| Property  | Type    | Access | Description                                              |
|-----------|---------|--------|----------------------------------------------------------|
| `Up`      | Boolean | Read   | Held: Arrow Up, W, D-pad Up, or left stick below -0.5    |
| `Down`    | Boolean | Read   | Held: Arrow Down, S, D-pad Down, or left stick above 0.5 |
| `Left`    | Boolean | Read   | Held: Arrow Left, A, D-pad Left, or left stick below -0.5|
| `Right`   | Boolean | Read   | Held: Arrow Right, D, D-pad Right, or left stick above 0.5|
| `Confirm` | Boolean | Read   | Press edge: Enter, Space, or gamepad A                    |
| `Cancel`  | Boolean | Read   | Press edge: Escape or gamepad B                           |
| `AxisX`   | Double  | Read   | Unified horizontal axis, clamped to -1.0 through 1.0     |
| `AxisY`   | Double  | Read   | Unified vertical axis, clamped to -1.0 through 1.0       |

The direction properties are level-triggered and therefore remain true every poll while held.
`AxisX`/`AxisY` combine keyboard, D-pad, and all four left sticks by selection rather than by
summing their values. Digital input can force `-1.0` or `1.0`; vertical negative is up.

### Keyboard Methods

| Method                     | Signature           | Description                                          |
|----------------------------|---------------------|------------------------------------------------------|
| `KeyHeld(key)`             | `Boolean(Integer)`  | True if key is currently held down                   |
| `KeyPressed(key)`          | `Boolean(Integer)`  | True if key was pressed this frame (edge detection)  |
| `KeyPressedDebounced(key)` | `Boolean(Integer)`  | Accept a down edge only while that key's timer is zero|
| `KeyReleased(key)`         | `Boolean(Integer)`  | True if key was released this frame                  |

### Mouse Methods

| Method                  | Signature           | Description                                     |
|-------------------------|---------------------|-------------------------------------------------|
| `MouseHeld(button)`     | `Boolean(Integer)`  | True if button is currently held                |
| `MousePressed(button)`  | `Boolean(Integer)`  | True if button was pressed this frame           |
| `MouseReleased(button)` | `Boolean(Integer)`  | True if button was released this frame          |

### Mouse Properties

| Property     | Type    | Access | Description                              |
|--------------|---------|--------|------------------------------------------|
| `MouseX`     | Integer | Read   | Current mouse X position                 |
| `MouseY`     | Integer | Read   | Current mouse Y position                 |
| `MouseDeltaX`| Integer | Read   | Mouse X movement since last frame        |
| `MouseDeltaY`| Integer | Read   | Mouse Y movement since last frame        |
| `ScrollX`    | Integer | Read   | Horizontal scroll delta                  |
| `ScrollY`    | Integer | Read   | Vertical scroll delta                    |
| `ScrollHorizontalFloat` | Double | Read | Horizontal scroll delta with fractional precision |
| `ScrollVerticalFloat`   | Double | Read | Vertical scroll delta with fractional precision   |

### Gamepad Methods

| Method                     | Signature                   | Description                           |
|----------------------------|-----------------------------|---------------------------------------|
| `PadHeld(pad, button)`     | `Boolean(Integer, Integer)` | True if button is currently held      |
| `PadLeftTrigger(pad)`      | `Double(Integer)`           | Left trigger (0.0 to 1.0)             |
| `PadLeftX(pad)`            | `Double(Integer)`           | Left stick X (-1.0 to 1.0)            |
| `PadLeftY(pad)`            | `Double(Integer)`           | Left stick Y (-1.0 to 1.0)            |
| `PadPressed(pad, button)`  | `Boolean(Integer, Integer)` | True if button was pressed this frame |
| `PadReleased(pad, button)` | `Boolean(Integer, Integer)` | True if button was released this frame|
| `PadRightTrigger(pad)`     | `Double(Integer)`           | Right trigger (0.0 to 1.0)            |
| `PadRightX(pad)`           | `Double(Integer)`           | Right stick X (-1.0 to 1.0)           |
| `PadRightY(pad)`           | `Double(Integer)`           | Right stick Y (-1.0 to 1.0)           |

**Note:** `PadPressed`, `PadReleased`, and `PadHeld` accept exactly `-1` for any connected
controller. Axis and trigger methods pass the index directly to `Pad`; `-1` returns zero.

### Zia Example: Menu Navigation

```rust
module MenuDemo;

bind Viper.Terminal;
bind Viper.Input.Manager as IM;
bind Viper.Graphics.Canvas as Canvas;
bind Viper.Graphics.Color as Color;
bind Viper.Text.Fmt as Fmt;

func start() {
    var c = Canvas.New("Menu", 800, 600);
    var input = IM.New();
    input.set_DebounceDelay(12);

    var selected = 0;
    var directionReady = true;

    while !c.get_ShouldClose() {
        c.Poll();
        input.Update();

        // Unified directions are held-state properties, so latch one move per press.
        var up = input.get_Up();
        var down = input.get_Down();
        if directionReady && up {
            selected = selected - 1;
            directionReady = false;
        }
        if directionReady && down {
            selected = selected + 1;
            directionReady = false;
        }
        if !up && !down { directionReady = true; }
        if selected < 0 { selected = 3; }
        if selected > 3 { selected = 0; }

        if input.get_Confirm() {
            Say("Selected item: " + Fmt.Int(selected));
        }
        if input.get_Cancel() { return; }

        c.Clear(Color.RGB(0, 0, 0));
        c.Flip();
    }
}
```

### Example: Menu Navigation

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Menu Demo", 800, 600)

DIM mgr AS OBJECT = Viper.Input.Manager.New()

DIM selectedItem AS INTEGER = 0
DIM directionReady AS INTEGER = 1
DIM menuItems(3) AS STRING
menuItems(0) = "New Game"
menuItems(1) = "Continue"
menuItems(2) = "Options"
menuItems(3) = "Exit"

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()
    mgr.Update()

    ' Unified directions are properties and stay true while held.
    IF directionReady = 1 AND mgr.Up THEN
        selectedItem = selectedItem - 1
        IF selectedItem < 0 THEN selectedItem = 3
        directionReady = 0
    END IF

    IF directionReady = 1 AND mgr.Down THEN
        selectedItem = selectedItem + 1
        IF selectedItem > 3 THEN selectedItem = 0
        directionReady = 0
    END IF

    IF NOT mgr.Up AND NOT mgr.Down THEN directionReady = 1

    ' Confirm selection
    IF mgr.Confirm THEN
        SELECT CASE selectedItem
            CASE 0: PRINT "New Game"
            CASE 1: PRINT "Continue"
            CASE 2: PRINT "Options"
            CASE 3: EXIT DO
        END SELECT
    END IF

    ' Back/Cancel
    IF mgr.Cancel THEN
        EXIT DO
    END IF

    ' Render the menu background
    canvas.Clear(0)
    DIM i AS INTEGER
    FOR i = 0 TO 3
        DIM color AS INTEGER = 16777215
        IF i = selectedItem THEN color = 16776960
        ' Draw menuItems(i) at its row here
    NEXT i
    canvas.Flip()
LOOP
```

### Example: Game Movement

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Game", 800, 600)

DIM mgr AS OBJECT = Viper.Input.Manager.New()

DIM playerX AS DOUBLE = 400.0
DIM playerY AS DOUBLE = 300.0
DIM speed AS DOUBLE = 5.0

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()
    mgr.Update()

    ' Smooth movement using axis values (works with WASD, arrows, AND analog stick)
    playerX = playerX + mgr.AxisX * speed
    playerY = playerY + mgr.AxisY * speed

    ' Or use digital direction for grid-based movement
    IF mgr.Left THEN playerX = playerX - speed
    IF mgr.Right THEN playerX = playerX + speed
    IF mgr.Up THEN playerY = playerY - speed
    IF mgr.Down THEN playerY = playerY + speed

    canvas.Clear(0)
    canvas.Disc(INT(playerX), INT(playerY), 20, 16711680)
    canvas.Flip()
LOOP
```

### Example: Debounced Key Input

```basic
DIM mgr AS OBJECT = Viper.Input.Manager.New()
mgr.DebounceDelay = 15
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Input Gate", 400, 300)

DO WHILE NOT canvas.ShouldClose
    canvas.Poll()
    mgr.Update()

    ' Held is level-triggered and fires every poll while held.
    IF mgr.KeyHeld(Viper.Input.Key.Space) THEN
        PRINT "Fire"
    END IF

    ' This accepts only a down edge while the per-key timer is zero.
    ' Calling it while P is up resets the timer; holding P never repeats.
    IF mgr.KeyPressedDebounced(Viper.Input.Key.P) THEN
        PRINT "Toggle pause"
    END IF

    canvas.Clear(0)
    canvas.Flip()
LOOP
```

### Notes

- Call `Update()` after each window poll when using `KeyPressedDebounced`; all other members read
  global input state directly
- Negative `DebounceDelay` assignments are ignored. The manager tracks at most 32 key codes and
  reuses the slot with the shortest remaining timer when saturated
- `KeyPressedDebounced` still requires a `WasPressed` down edge; it does not generate held-key
  repeat. Calling it while the key is up clears that key's timer
- Unified directions inspect keyboard, D-pad, and all four gamepad slots automatically
- `AxisX`/`AxisY` select among digital and analog contributions and clamp the result; they do not
  sum all devices
- Use `-1` for any gamepad only with the three Manager button methods
- **Known issue:** the edge gate does not implement the commonly expected repeat-delay behavior;
  see [VDOC-010](../documentation-review-findings.md#vdoc-010--inputmanager-debounce-delay-does-not-implement-held-key-repeat)

### InputManager vs Low-Level Classes

| Feature                | InputManager                    | Keyboard/Mouse/Pad            |
|------------------------|---------------------------------|-------------------------------|
| Unified directions     | Yes (Up/Down/Left/Right)        | No (check each device)        |
| Per-key edge gate      | Yes (`KeyPressedDebounced`)     | No                            |
| Device-agnostic axes   | Yes (`AxisX`, `AxisY`)          | No                            |
| Confirm/Cancel actions | Yes (built-in mappings)         | No                            |
| Per-key control        | Yes (falls through to low-level)| Yes                           |
| Input state ownership  | Delegates to global low-level state | Global low-level state    |

### Use Cases

- **Menu navigation:** Unified held directions plus edge-triggered Confirm/Cancel; add a latch or
  repeat policy for one-step selection
- **Character movement:** Combined axis input from all devices
- **Dialog systems:** Per-key edge gating to prevent duplicate consumption
- **Inventory screens:** Device-agnostic selection
- **Quick prototyping:** Less boilerplate than raw input classes

---

## See Also

- [Graphics](graphics/README.md) - `Canvas` class for windowing and rendering that drives input polling
- [Collections](collections/README.md) - `Seq` type returned by `GetPressed()` and `GetReleased()` methods
- [Time](time.md) - `Timer` class for frame-based timing that pairs well with InputManager
