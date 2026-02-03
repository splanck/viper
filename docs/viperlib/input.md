# Input

> Keyboard, mouse, and gamepad input handling for interactive applications.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Input.Keyboard](#viperinputkeyboard)
- [Viper.Input.Mouse](#viperinputmouse)
- [Viper.Input.Pad](#viperinputpad)
- [Viper.Input.Action](#viperinputaction)
- [Viper.Input.Manager](#viperinputmanager)

---

## Viper.Input.Keyboard

Comprehensive keyboard input handling for games and interactive applications.

**Type:** Static utility class

The Keyboard class provides two complementary input models:

- **Polling**: Check the current state of any key at any time
- **Event-based**: Query which keys were pressed or released since the last frame

Keyboard state is updated automatically when you call `Canvas.Poll()`.

### Polling Methods (Current State)

| Method        | Signature           | Description                                                |
|---------------|---------------------|------------------------------------------------------------|
| `IsDown(key)` | `Boolean(Integer)`  | Returns true if the specified key is currently held down   |
| `IsUp(key)`   | `Boolean(Integer)`  | Returns true if the specified key is currently released    |
| `AnyDown()`   | `Boolean()`         | Returns true if any key is currently pressed               |
| `GetDown()`   | `Integer()`         | Returns the key code of the first pressed key, or 0        |

### Event Methods (Since Last Poll)

| Method          | Signature          | Description                                          |
|-----------------|--------------------|------------------------------------------------------|
| `WasPressed(key)` | `Boolean(Integer)` | Returns true if the key was pressed this frame     |
| `WasReleased(key)` | `Boolean(Integer)` | Returns true if the key was released this frame   |
| `GetPressed()`  | `Seq()`            | Returns a Seq of all key codes pressed this frame   |
| `GetReleased()` | `Seq()`            | Returns a Seq of all key codes released this frame  |

### Text Input Methods

| Method              | Signature  | Description                                     |
|---------------------|------------|-------------------------------------------------|
| `GetText()`         | `String()` | Returns text typed since last poll              |
| `EnableTextInput()` | `Void()`   | Enable text input mode (for text fields)        |
| `DisableTextInput()` | `Void()`  | Disable text input mode                         |

### Modifier State Methods

| Method      | Signature   | Description                              |
|-------------|-------------|------------------------------------------|
| `Shift()`   | `Boolean()` | Returns true if Shift is held            |
| `Ctrl()`    | `Boolean()` | Returns true if Ctrl is held             |
| `Alt()`     | `Boolean()` | Returns true if Alt is held              |
| `CapsLock()` | `Boolean()` | Returns true if Caps Lock is on         |

### Helper Methods

| Method         | Signature          | Description                                        |
|----------------|--------------------|----------------------------------------------------|
| `KeyName(key)` | `String(Integer)`  | Returns human-readable name for a key code         |

### Key Code Constants

All key codes are accessed as read-only properties on the Keyboard class.

#### Letters

| Property | Value | Property | Value | Property | Value |
|----------|-------|----------|-------|----------|-------|
| `KEY_A`  | 65    | `KEY_J`  | 74    | `KEY_S`  | 83    |
| `KEY_B`  | 66    | `KEY_K`  | 75    | `KEY_T`  | 84    |
| `KEY_C`  | 67    | `KEY_L`  | 76    | `KEY_U`  | 85    |
| `KEY_D`  | 68    | `KEY_M`  | 77    | `KEY_V`  | 86    |
| `KEY_E`  | 69    | `KEY_N`  | 78    | `KEY_W`  | 87    |
| `KEY_F`  | 70    | `KEY_O`  | 79    | `KEY_X`  | 88    |
| `KEY_G`  | 71    | `KEY_P`  | 80    | `KEY_Y`  | 89    |
| `KEY_H`  | 72    | `KEY_Q`  | 81    | `KEY_Z`  | 90    |
| `KEY_I`  | 73    | `KEY_R`  | 82    |          |       |

#### Numbers

| Property  | Value | Property  | Value |
|-----------|-------|-----------|-------|
| `KEY_0`   | 48    | `KEY_5`   | 53    |
| `KEY_1`   | 49    | `KEY_6`   | 54    |
| `KEY_2`   | 50    | `KEY_7`   | 55    |
| `KEY_3`   | 51    | `KEY_8`   | 56    |
| `KEY_4`   | 52    | `KEY_9`   | 57    |

#### Function Keys

| Property   | Value | Property   | Value |
|------------|-------|------------|-------|
| `KEY_F1`   | 290   | `KEY_F7`   | 296   |
| `KEY_F2`   | 291   | `KEY_F8`   | 297   |
| `KEY_F3`   | 292   | `KEY_F9`   | 298   |
| `KEY_F4`   | 293   | `KEY_F10`  | 299   |
| `KEY_F5`   | 294   | `KEY_F11`  | 300   |
| `KEY_F6`   | 295   | `KEY_F12`  | 301   |

#### Navigation

| Property       | Value | Property       | Value |
|----------------|-------|----------------|-------|
| `KEY_UP`       | 265   | `KEY_HOME`     | 268   |
| `KEY_DOWN`     | 264   | `KEY_END`      | 269   |
| `KEY_LEFT`     | 263   | `KEY_PAGEUP`   | 266   |
| `KEY_RIGHT`    | 262   | `KEY_PAGEDOWN` | 267   |
| `KEY_INSERT`   | 260   | `KEY_DELETE`   | 261   |

#### Special Keys

| Property        | Value | Property       | Value |
|-----------------|-------|----------------|-------|
| `KEY_UNKNOWN`   | 0     | `KEY_SPACE`    | 32    |
| `KEY_TAB`       | 258   | `KEY_ENTER`    | 257   |
| `KEY_BACKSPACE` | 259   | `KEY_ESCAPE`   | 256   |

#### Modifier Keys

| Property      | Value | Property      | Value |
|---------------|-------|---------------|-------|
| `KEY_SHIFT`   | 340   | `KEY_LSHIFT`  | 340   |
| `KEY_CTRL`    | 341   | `KEY_RSHIFT`  | 344   |
| `KEY_ALT`     | 342   | `KEY_LCTRL`   | 341   |
|               |       | `KEY_RCTRL`   | 345   |
|               |       | `KEY_LALT`    | 342   |
|               |       | `KEY_RALT`    | 346   |

#### Punctuation

| Property         | Value | Property         | Value |
|------------------|-------|------------------|-------|
| `KEY_MINUS`      | 45    | `KEY_SEMICOLON`  | 59    |
| `KEY_EQUALS`     | 61    | `KEY_QUOTE`      | 39    |
| `KEY_LBRACKET`   | 91    | `KEY_COMMA`      | 44    |
| `KEY_RBRACKET`   | 93    | `KEY_PERIOD`     | 46    |
| `KEY_BACKSLASH`  | 92    | `KEY_SLASH`      | 47    |
| `KEY_GRAVE`      | 96    |                  |       |

#### Numpad

| Property       | Value | Property       | Value |
|----------------|-------|----------------|-------|
| `KEY_NUM0`     | 320   | `KEY_NUM6`     | 326   |
| `KEY_NUM1`     | 321   | `KEY_NUM7`     | 327   |
| `KEY_NUM2`     | 322   | `KEY_NUM8`     | 328   |
| `KEY_NUM3`     | 323   | `KEY_NUM9`     | 329   |
| `KEY_NUM4`     | 324   | `KEY_NUMADD`   | 334   |
| `KEY_NUM5`     | 325   | `KEY_NUMSUB`   | 333   |
| `KEY_NUMDOT`   | 330   | `KEY_NUMMUL`   | 332   |
| `KEY_NUMENTER` | 335   | `KEY_NUMDIV`   | 331   |

### Example: Basic Game Input

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Game", 800, 600)

DIM playerX AS INTEGER = 400
DIM playerY AS INTEGER = 300
DIM speed AS INTEGER = 5

DO WHILE canvas.ShouldClose = 0
    ' Poll events and update keyboard state
    canvas.Poll()

    ' Movement using polling (smooth, held keys)
    IF Viper.Input.Keyboard.IsDown(Viper.Input.Keyboard.KEY_W) THEN
        playerY = playerY - speed
    END IF
    IF Viper.Input.Keyboard.IsDown(Viper.Input.Keyboard.KEY_S) THEN
        playerY = playerY + speed
    END IF
    IF Viper.Input.Keyboard.IsDown(Viper.Input.Keyboard.KEY_A) THEN
        playerX = playerX - speed
    END IF
    IF Viper.Input.Keyboard.IsDown(Viper.Input.Keyboard.KEY_D) THEN
        playerX = playerX + speed
    END IF

    ' Action using event (single press)
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Keyboard.KEY_SPACE) THEN
        ' Fire weapon or jump - only triggers once per press
        PRINT "Action!"
    END IF

    ' Escape to quit
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Keyboard.KEY_ESCAPE) THEN
        EXIT DO
    END IF

    ' Draw
    canvas.Clear(&H00000000)
    canvas.Box(playerX - 10, playerY - 10, 20, 20, &H00FF0000)
    canvas.Flip()
LOOP
```

### Example: Displaying Pressed Keys

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Key Test", 400, 300)

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    ' Get all keys pressed this frame
    DIM pressed AS Viper.Collections.Seq
    pressed = Viper.Input.Keyboard.GetPressed()

    DIM i AS INTEGER
    FOR i = 0 TO pressed.Len() - 1
        DIM key AS INTEGER
        key = pressed.Get(i)
        PRINT "Pressed: "; Viper.Input.Keyboard.KeyName(key)
    NEXT i

    canvas.Clear(&H00000000)
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

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    ' Get typed text
    DIM typed AS STRING
    typed = Viper.Input.Keyboard.GetText()
    inputText = inputText + typed

    ' Handle backspace
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Keyboard.KEY_BACKSPACE) THEN
        IF LEN(inputText) > 0 THEN
            inputText = LEFT$(inputText, LEN(inputText) - 1)
        END IF
    END IF

    ' Submit on Enter
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Keyboard.KEY_ENTER) THEN
        PRINT "Submitted: "; inputText
        inputText = ""
    END IF

    canvas.Clear(&H00000000)
    ' Draw text field (you'd use a text rendering method)
    canvas.Flip()
LOOP

Viper.Input.Keyboard.DisableTextInput()
```

### Notes

- Keyboard state is only updated when `Canvas.Poll()` is called
- `WasPressed()` and `WasReleased()` only return true for one frame after the event
- Use polling (`IsDown()`) for continuous input like movement
- Use events (`WasPressed()`) for discrete actions like menu selection or jumping
- Key codes are GLFW-compatible values for portability
- Text input mode should be enabled for text fields and disabled otherwise

### Integration with Canvas

The Keyboard class automatically integrates with Canvas. When you call `Canvas.Poll()`:

1. Platform keyboard events are collected
2. Key state arrays are updated
3. Pressed/released event lists are populated
4. Text input buffer is updated

You don't need to explicitly initialize the keyboard - it's ready when you create a Canvas.

---

## Viper.Input.Mouse

Comprehensive mouse input handling for games and interactive applications.

**Type:** Static utility class

The Mouse class provides:

- **Position tracking**: Current position and movement delta
- **Button state polling**: Check if buttons are currently pressed
- **Button events**: Query presses, releases, clicks, and double-clicks since last frame
- **Scroll wheel**: Horizontal and vertical scroll amounts
- **Cursor control**: Show, hide, capture, and position the cursor

Mouse state is updated automatically when you call `Canvas.Poll()`.

### Position Methods

| Method     | Signature   | Description                                            |
|------------|-------------|--------------------------------------------------------|
| `X()`      | `Integer()` | Current X position relative to the canvas              |
| `Y()`      | `Integer()` | Current Y position relative to the canvas              |
| `DeltaX()` | `Integer()` | Horizontal movement since last frame                   |
| `DeltaY()` | `Integer()` | Vertical movement since last frame                     |

### Button State Methods (Polling)

| Method            | Signature          | Description                                        |
|-------------------|--------------------|----------------------------------------------------|
| `IsDown(button)`  | `Boolean(Integer)` | Returns true if the button is currently held down  |
| `IsUp(button)`    | `Boolean(Integer)` | Returns true if the button is currently released   |
| `Left()`          | `Boolean()`        | Returns true if the left button is held            |
| `Right()`         | `Boolean()`        | Returns true if the right button is held           |
| `Middle()`        | `Boolean()`        | Returns true if the middle button is held          |

### Button Event Methods (Since Last Poll)

| Method                     | Signature          | Description                                           |
|----------------------------|--------------------|-------------------------------------------------------|
| `WasPressed(button)`       | `Boolean(Integer)` | Returns true if the button was pressed this frame     |
| `WasReleased(button)`      | `Boolean(Integer)` | Returns true if the button was released this frame    |
| `WasClicked(button)`       | `Boolean(Integer)` | Returns true if a quick press-release occurred        |
| `WasDoubleClicked(button)` | `Boolean(Integer)` | Returns true if a double-click was detected           |

### Scroll Wheel Methods

| Method     | Signature   | Description                                      |
|------------|-------------|--------------------------------------------------|
| `WheelX()` | `Integer()` | Horizontal scroll amount this frame              |
| `WheelY()` | `Integer()` | Vertical scroll amount this frame (+ = up)       |

### Cursor Control Methods

| Method           | Signature                 | Description                                    |
|------------------|---------------------------|------------------------------------------------|
| `Show()`         | `Void()`                  | Show the cursor                                |
| `Hide()`         | `Void()`                  | Hide the cursor                                |
| `IsHidden()`     | `Boolean()`               | Returns true if the cursor is hidden           |
| `Capture()`      | `Void()`                  | Lock the cursor to the window (for FPS games)  |
| `Release()`      | `Void()`                  | Release the cursor lock                        |
| `IsCaptured()`   | `Boolean()`               | Returns true if the cursor is captured         |
| `SetPos(x, y)`   | `Void(Integer, Integer)`  | Move the cursor to a specific position         |

### Button Constants

| Property        | Value | Description              |
|-----------------|-------|--------------------------|
| `BUTTON_LEFT`   | 0     | Left mouse button        |
| `BUTTON_RIGHT`  | 1     | Right mouse button       |
| `BUTTON_MIDDLE` | 2     | Middle mouse button      |
| `BUTTON_X1`     | 3     | Extra button 1 (back)    |
| `BUTTON_X2`     | 4     | Extra button 2 (forward) |

### Example: Drawing with Mouse

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Draw", 800, 600)

DIM drawing AS INTEGER = 0

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    ' Start drawing when left button pressed
    IF Viper.Input.Mouse.WasPressed(Viper.Input.Mouse.BUTTON_LEFT) THEN
        drawing = 1
    END IF

    ' Stop drawing when released
    IF Viper.Input.Mouse.WasReleased(Viper.Input.Mouse.BUTTON_LEFT) THEN
        drawing = 0
    END IF

    ' Draw at mouse position while button held
    IF drawing = 1 THEN
        DIM mx AS INTEGER = Viper.Input.Mouse.X()
        DIM my AS INTEGER = Viper.Input.Mouse.Y()
        canvas.Disc(mx, my, 5, &H00FF0000)
    END IF

    ' Clear on right click
    IF Viper.Input.Mouse.WasClicked(Viper.Input.Mouse.BUTTON_RIGHT) THEN
        canvas.Clear(&H00000000)
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

DO WHILE canvas.ShouldClose = 0
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
    IF Viper.Input.Mouse.WasPressed(Viper.Input.Mouse.BUTTON_LEFT) THEN
        IF overBox = 1 THEN
            dragging = 1
            offsetX = mx - boxX
            offsetY = my - boxY
        END IF
    END IF

    ' Stop dragging on release
    IF Viper.Input.Mouse.WasReleased(Viper.Input.Mouse.BUTTON_LEFT) THEN
        dragging = 0
    END IF

    ' Update position while dragging
    IF dragging = 1 THEN
        boxX = mx - offsetX
        boxY = my - offsetY
    END IF

    ' Draw
    canvas.Clear(&H00222222)

    ' Draw box (highlight if hovering or dragging)
    DIM color AS INTEGER = &H00444444
    IF overBox = 1 OR dragging = 1 THEN
        color = &H00666666
    END IF
    canvas.Box(boxX, boxY, boxW, boxH, color)
    canvas.Frame(boxX, boxY, boxW, boxH, &H00FFFFFF)

    canvas.Flip()
LOOP
```

### Example: First-Person Camera

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("FPS Camera", 800, 600)

DIM cameraYaw AS INTEGER = 0
DIM cameraPitch AS INTEGER = 0
DIM sensitivity AS INTEGER = 1

' Capture and hide cursor for FPS mode
Viper.Input.Mouse.Capture()
Viper.Input.Mouse.Hide()

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    ' Use mouse delta for camera rotation
    DIM dx AS INTEGER = Viper.Input.Mouse.DeltaX()
    DIM dy AS INTEGER = Viper.Input.Mouse.DeltaY()

    cameraYaw = cameraYaw + dx * sensitivity
    cameraPitch = cameraPitch - dy * sensitivity

    ' Clamp pitch
    IF cameraPitch > 90 THEN cameraPitch = 90
    IF cameraPitch < -90 THEN cameraPitch = -90

    ' Escape to release cursor and exit
    IF Viper.Input.Keyboard.WasPressed(Viper.Input.Keyboard.KEY_ESCAPE) THEN
        Viper.Input.Mouse.Release()
        Viper.Input.Mouse.Show()
        EXIT DO
    END IF

    canvas.Clear(&H00000000)
    ' Render scene based on cameraYaw and cameraPitch...
    canvas.Flip()
LOOP
```

### Example: Scroll Zoom

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Zoom", 800, 600)

DIM zoom AS INTEGER = 100  ' percentage

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    ' Scroll wheel to zoom
    DIM scroll AS INTEGER = Viper.Input.Mouse.WheelY()
    zoom = zoom + scroll * 10

    ' Clamp zoom level
    IF zoom < 10 THEN zoom = 10
    IF zoom > 500 THEN zoom = 500

    canvas.Clear(&H00000000)

    ' Draw something at zoom level
    DIM size AS INTEGER = zoom / 2
    DIM cx AS INTEGER = 400 - size / 2
    DIM cy AS INTEGER = 300 - size / 2
    canvas.Box(cx, cy, size, size, &H0000FF00)

    canvas.Flip()
LOOP
```

### Notes

- Mouse state is only updated when `Canvas.Poll()` is called
- `WasPressed()`, `WasReleased()`, and `WasClicked()` only return true for one frame
- Delta values (DeltaX/DeltaY) are reset at the start of each frame
- Click detection uses a timing threshold (quick press-release)
- Double-click detection requires two clicks within a short time window
- Use `Left()`, `Right()`, `Middle()` as shortcuts for common button checks
- Cursor capture is useful for FPS-style camera controls

### Integration with Canvas

The Mouse class automatically integrates with Canvas. When you call `Canvas.Poll()`:

1. Platform mouse events are collected
2. Position and delta are updated
3. Button state arrays are updated
4. Click/double-click detection is processed
5. Scroll wheel values are accumulated

You don't need to explicitly initialize the mouse - it's ready when you create a Canvas.

---

## Viper.Input.Pad

Gamepad/controller input handling for games and interactive applications.

**Type:** Static utility class

The Pad class provides unified gamepad input across Xbox, PlayStation, and generic controllers:

- **Controller enumeration**: Detect connected controllers (0-3)
- **Button state polling**: Check if buttons are currently pressed
- **Button events**: Query presses and releases since last frame
- **Analog sticks**: Left and right stick X/Y axes (-1.0 to 1.0)
- **Triggers**: Left and right trigger values (0.0 to 1.0)
- **Deadzone handling**: Configurable stick deadzone
- **Vibration/rumble**: Control haptic feedback motors

Gamepad state is updated automatically when you call `Canvas.Poll()`.

### Controller Enumeration Methods

| Method              | Signature          | Description                                     |
|---------------------|--------------------|-------------------------------------------------|
| `Count()`           | `Integer()`        | Number of connected controllers (0-4)           |
| `IsConnected(index)`| `Boolean(Integer)` | Returns true if controller is connected         |
| `Name(index)`       | `String(Integer)`  | Controller name/description (empty if invalid)  |

### Button State Methods (Polling)

| Method                 | Signature                   | Description                                        |
|------------------------|-----------------------------|---------------------------------------------------|
| `IsDown(index, button)`| `Boolean(Integer, Integer)` | Returns true if the button is currently held down  |
| `IsUp(index, button)`  | `Boolean(Integer, Integer)` | Returns true if the button is currently released   |

### Button Event Methods (Since Last Poll)

| Method                       | Signature                   | Description                                        |
|------------------------------|-----------------------------|----------------------------------------------------|
| `WasPressed(index, button)`  | `Boolean(Integer, Integer)` | Returns true if button was pressed this frame      |
| `WasReleased(index, button)` | `Boolean(Integer, Integer)` | Returns true if button was released this frame     |

### Analog Input Methods

| Method               | Signature         | Description                                            |
|----------------------|-------------------|--------------------------------------------------------|
| `LeftX(index)`       | `Double(Integer)` | Left stick X axis (-1.0 to 1.0, left to right)         |
| `LeftY(index)`       | `Double(Integer)` | Left stick Y axis (-1.0 to 1.0, up to down)            |
| `RightX(index)`      | `Double(Integer)` | Right stick X axis (-1.0 to 1.0, left to right)        |
| `RightY(index)`      | `Double(Integer)` | Right stick Y axis (-1.0 to 1.0, up to down)           |
| `LeftTrigger(index)` | `Double(Integer)` | Left trigger (0.0 to 1.0, released to fully pressed)   |
| `RightTrigger(index)`| `Double(Integer)` | Right trigger (0.0 to 1.0, released to fully pressed)  |

### Deadzone Methods

| Method                | Signature       | Description                                      |
|-----------------------|-----------------|--------------------------------------------------|
| `SetDeadzone(radius)` | `Void(Double)`  | Set stick deadzone radius (0.0 to 1.0)           |
| `GetDeadzone()`       | `Double()`      | Get current deadzone radius (default 0.1)        |

### Vibration Methods

| Method                             | Signature                      | Description                              |
|------------------------------------|--------------------------------|------------------------------------------|
| `Vibrate(index, left, right)`      | `Void(Integer, Double, Double)`| Set motor intensities (0.0 to 1.0)       |
| `StopVibration(index)`             | `Void(Integer)`                | Stop vibration on controller             |

### Button Constants

Standard gamepad layout compatible with Xbox and PlayStation controllers:

| Property        | Value | Xbox          | PlayStation    |
|-----------------|-------|---------------|----------------|
| `PAD_A`         | 0     | A             | Cross (X)      |
| `PAD_B`         | 1     | B             | Circle (O)     |
| `PAD_X`         | 2     | X             | Square         |
| `PAD_Y`         | 3     | Y             | Triangle       |
| `PAD_LB`        | 4     | Left Bumper   | L1             |
| `PAD_RB`        | 5     | Right Bumper  | R1             |
| `PAD_BACK`      | 6     | Back/View     | Share          |
| `PAD_START`     | 7     | Start/Menu    | Options        |
| `PAD_LSTICK`    | 8     | Left Stick    | L3             |
| `PAD_RSTICK`    | 9     | Right Stick   | R3             |
| `PAD_UP`        | 10    | D-pad Up      | D-pad Up       |
| `PAD_DOWN`      | 11    | D-pad Down    | D-pad Down     |
| `PAD_LEFT`      | 12    | D-pad Left    | D-pad Left     |
| `PAD_RIGHT`     | 13    | D-pad Right   | D-pad Right    |
| `PAD_GUIDE`     | 14    | Xbox Button   | PS Button      |

### Example: Basic Controller Movement

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Controller Demo", 800, 600)

DIM playerX AS DOUBLE = 400.0
DIM playerY AS DOUBLE = 300.0
DIM speed AS DOUBLE = 5.0

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    ' Check if controller 0 is connected
    IF Viper.Input.Pad.IsConnected(0) THEN
        ' Movement with left stick
        DIM lx AS DOUBLE = Viper.Input.Pad.LeftX(0)
        DIM ly AS DOUBLE = Viper.Input.Pad.LeftY(0)

        playerX = playerX + lx * speed
        playerY = playerY + ly * speed

        ' Action on A button press
        IF Viper.Input.Pad.WasPressed(0, Viper.Input.Pad.PAD_A) THEN
            PRINT "Jump!"
        END IF

        ' Shoot while holding right trigger
        DIM rt AS DOUBLE = Viper.Input.Pad.RightTrigger(0)
        IF rt > 0.5 THEN
            PRINT "Shooting! Power: "; rt
        END IF
    END IF

    canvas.Clear(&H00000000)
    canvas.Disc(INT(playerX), INT(playerY), 20, &H00FF0000)
    canvas.Flip()
LOOP
```

### Example: Controller Vibration

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Rumble Test", 400, 300)

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    IF Viper.Input.Pad.IsConnected(0) THEN
        ' Strong rumble on left bumper
        IF Viper.Input.Pad.WasPressed(0, Viper.Input.Pad.PAD_LB) THEN
            Viper.Input.Pad.Vibrate(0, 1.0, 0.3)
        END IF

        ' Light rumble on right bumper
        IF Viper.Input.Pad.WasPressed(0, Viper.Input.Pad.PAD_RB) THEN
            Viper.Input.Pad.Vibrate(0, 0.3, 1.0)
        END IF

        ' Stop on B button
        IF Viper.Input.Pad.WasPressed(0, Viper.Input.Pad.PAD_B) THEN
            Viper.Input.Pad.StopVibration(0)
        END IF
    END IF

    canvas.Clear(&H00000000)
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

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    IF Viper.Input.Pad.IsConnected(0) THEN
        ' Movement with left stick
        posX = posX + Viper.Input.Pad.LeftX(0) * 3.0
        posY = posY + Viper.Input.Pad.LeftY(0) * 3.0

        ' Aiming with right stick
        DIM rx AS DOUBLE = Viper.Input.Pad.RightX(0)
        DIM ry AS DOUBLE = Viper.Input.Pad.RightY(0)
        IF ABS(rx) > 0.1 OR ABS(ry) > 0.1 THEN
            aimAngle = ATN2(ry, rx)
        END IF
    END IF

    canvas.Clear(&H00000000)

    ' Draw player
    canvas.Disc(INT(posX), INT(posY), 15, &H0000FF00)

    ' Draw aim direction
    DIM aimX AS INTEGER = INT(posX + COS(aimAngle) * 30)
    DIM aimY AS INTEGER = INT(posY + SIN(aimAngle) * 30)
    canvas.Line(INT(posX), INT(posY), aimX, aimY, &H00FF0000)

    canvas.Flip()
LOOP
```

### Example: Multi-Controller Support

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Multiplayer", 800, 600)

' Colors for each player
DIM colors(3) AS INTEGER
colors(0) = &H00FF0000  ' Red
colors(1) = &H000000FF  ' Blue
colors(2) = &H0000FF00  ' Green
colors(3) = &H00FFFF00  ' Yellow

DIM x(3) AS DOUBLE
DIM y(3) AS DOUBLE

' Initial positions
x(0) = 200 : y(0) = 300
x(1) = 600 : y(1) = 300
x(2) = 400 : y(2) = 150
x(3) = 400 : y(3) = 450

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    DIM i AS INTEGER
    FOR i = 0 TO 3
        IF Viper.Input.Pad.IsConnected(i) THEN
            x(i) = x(i) + Viper.Input.Pad.LeftX(i) * 4.0
            y(i) = y(i) + Viper.Input.Pad.LeftY(i) * 4.0
        END IF
    NEXT i

    canvas.Clear(&H00222222)

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

- Gamepad state is only updated when `Canvas.Poll()` is called
- `WasPressed()` and `WasReleased()` only return true for one frame
- Deadzone is applied to stick values, not triggers
- Values below the deadzone are rescaled (not clamped) for smooth movement
- Controller indices 0-3 are valid; invalid indices return 0/false
- Disconnected controllers return 0/false for all queries (no errors)
- Vibration intensity may vary by controller model
- macOS rumble requests are ignored (no HID rumble support)

### Platform Support

| Platform | API Used                    | Notes                                              |
|----------|-----------------------------|----------------------------------------------------|
| Windows  | XInput                      | Xbox-compatible controllers; rumble supported      |
| Linux    | evdev (`/dev/input/event*`) | Requires input permissions; rumble when supported  |
| macOS    | IOHIDManager (HID)          | Generic HID gamepads; rumble not available         |

### Integration with Canvas

The Pad class automatically integrates with Canvas. When you call `Canvas.Poll()`:

1. Platform gamepad APIs are queried
2. Controller connection state is updated
3. Button state arrays are updated
4. Analog values are read and deadzone applied
5. Press/release events are detected

You don't need to explicitly initialize gamepads - they're ready when you create a Canvas.

---

## Viper.Input.Action

High-level action mapping system for device-agnostic input handling.

**Type:** Static utility class

The Action class provides an abstraction layer over raw input devices. Instead of checking individual keys, mouse buttons, or gamepad buttons, you define named "actions" and bind them to multiple input sources. This enables:

- **Input remapping** without code changes
- **Multi-device support** - query a single action that works across keyboard, mouse, and gamepad
- **Axis actions** for analog input (movement, camera control)
- **Consistent state tracking** - pressed, released, and held states

Action state is updated automatically when you call `Canvas.Poll()`.

### Action System Lifecycle

| Method        | Signature | Description                                     |
|---------------|-----------|------------------------------------------------|
| `Clear()`     | `Void()`  | Remove all defined actions and bindings         |

### Action Definition Methods

| Method                | Signature          | Description                                                |
|-----------------------|--------------------|------------------------------------------------------------|
| `Define(name)`        | `Boolean(String)`  | Define a new button action; returns false if already exists|
| `DefineAxis(name)`    | `Boolean(String)`  | Define a new axis action; returns false if already exists  |
| `Exists(name)`        | `Boolean(String)`  | Check if an action is defined                              |
| `IsAxis(name)`        | `Boolean(String)`  | Check if an action is an axis action                       |
| `Remove(name)`        | `Boolean(String)`  | Remove an action and all its bindings                      |

### Keyboard Binding Methods

| Method                          | Signature                      | Description                                      |
|---------------------------------|--------------------------------|--------------------------------------------------|
| `BindKey(action, key)`          | `Boolean(String, Integer)`     | Bind a key to a button action                    |
| `BindKeyAxis(action, key, value)` | `Boolean(String, Integer, Double)` | Bind a key to an axis action with value     |
| `UnbindKey(action, key)`        | `Boolean(String, Integer)`     | Remove a key binding from an action              |

### Mouse Binding Methods

| Method                             | Signature                      | Description                                      |
|------------------------------------|--------------------------------|--------------------------------------------------|
| `BindMouse(action, button)`        | `Boolean(String, Integer)`     | Bind a mouse button to a button action           |
| `UnbindMouse(action, button)`      | `Boolean(String, Integer)`     | Remove a mouse button binding                    |
| `BindMouseX(action, sensitivity)`  | `Boolean(String, Double)`      | Bind mouse X delta to an axis action             |
| `BindMouseY(action, sensitivity)`  | `Boolean(String, Double)`      | Bind mouse Y delta to an axis action             |
| `BindScrollX(action, sensitivity)` | `Boolean(String, Double)`      | Bind scroll wheel X to an axis action            |
| `BindScrollY(action, sensitivity)` | `Boolean(String, Double)`      | Bind scroll wheel Y to an axis action            |

### Gamepad Binding Methods

| Method                                         | Signature                               | Description                                      |
|------------------------------------------------|-----------------------------------------|--------------------------------------------------|
| `BindPadButton(action, pad, button)`           | `Boolean(String, Integer, Integer)`     | Bind a gamepad button to a button action         |
| `UnbindPadButton(action, pad, button)`         | `Boolean(String, Integer, Integer)`     | Remove a gamepad button binding                  |
| `BindPadAxis(action, pad, axis, scale)`        | `Boolean(String, Integer, Integer, Double)` | Bind a gamepad axis to an axis action        |
| `UnbindPadAxis(action, pad, axis)`             | `Boolean(String, Integer, Integer)`     | Remove a gamepad axis binding                    |
| `BindPadButtonAxis(action, pad, button, value)`| `Boolean(String, Integer, Integer, Double)` | Bind a gamepad button to an axis action      |

**Note:** Use pad index `-1` to match any connected controller.

### Button Action Query Methods

| Method             | Signature          | Description                                                  |
|--------------------|--------------------|------------------------------------------------------------- |
| `Pressed(action)`  | `Boolean(String)`  | Returns true if any bound input was pressed this frame       |
| `Released(action)` | `Boolean(String)`  | Returns true if any bound input was released this frame      |
| `Held(action)`     | `Boolean(String)`  | Returns true if any bound input is currently held            |
| `Strength(action)` | `Double(String)`   | Returns 1.0 if held, 0.0 otherwise                           |

### Axis Action Query Methods

| Method             | Signature         | Description                                                   |
|--------------------|-------------------|---------------------------------------------------------------|
| `Axis(action)`     | `Double(String)`  | Returns combined axis value, clamped to -1.0 to 1.0           |
| `AxisRaw(action)`  | `Double(String)`  | Returns combined axis value, not clamped                      |

### Introspection Methods

| Method                  | Signature          | Description                                              |
|-------------------------|--------------------|---------------------------------------------------------|
| `List()`                | `Seq()`            | Returns a Seq of all defined action names                |
| `BindingsStr(action)`   | `String(String)`   | Returns human-readable description of bindings           |
| `BindingCount(action)`  | `Integer(String)`  | Returns the number of bindings for an action             |

### Conflict Detection Methods

| Method                              | Signature                      | Description                                    |
|-------------------------------------|--------------------------------|------------------------------------------------|
| `KeyBoundTo(key)`                   | `String(Integer)`              | Returns action name if key is bound, else ""   |
| `MouseBoundTo(button)`              | `String(Integer)`              | Returns action name if button is bound, else ""|
| `PadButtonBoundTo(pad, button)`     | `String(Integer, Integer)`     | Returns action name if bound, else ""          |

### Axis Constants

| Property            | Value | Description                     |
|---------------------|-------|---------------------------------|
| `AXIS_LEFT_X`       | 0     | Left stick horizontal           |
| `AXIS_LEFT_Y`       | 1     | Left stick vertical             |
| `AXIS_RIGHT_X`      | 2     | Right stick horizontal          |
| `AXIS_RIGHT_Y`      | 3     | Right stick vertical            |
| `AXIS_LEFT_TRIGGER` | 4     | Left trigger (0.0 to 1.0)       |
| `AXIS_RIGHT_TRIGGER`| 5     | Right trigger (0.0 to 1.0)      |

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
Viper.Input.Action.BindKey("jump", Viper.Input.Keyboard.KEY_SPACE)
Viper.Input.Action.BindKey("fire", Viper.Input.Keyboard.KEY_Z)
Viper.Input.Action.BindKeyAxis("move_x", Viper.Input.Keyboard.KEY_LEFT, -1.0)
Viper.Input.Action.BindKeyAxis("move_x", Viper.Input.Keyboard.KEY_RIGHT, 1.0)
Viper.Input.Action.BindKeyAxis("move_y", Viper.Input.Keyboard.KEY_UP, -1.0)
Viper.Input.Action.BindKeyAxis("move_y", Viper.Input.Keyboard.KEY_DOWN, 1.0)

' Bind gamepad (any controller)
Viper.Input.Action.BindPadButton("jump", -1, Viper.Input.Pad.PAD_A)
Viper.Input.Action.BindPadButton("fire", -1, Viper.Input.Pad.PAD_X)
Viper.Input.Action.BindPadAxis("move_x", -1, Viper.Input.Action.AXIS_LEFT_X, 1.0)
Viper.Input.Action.BindPadAxis("move_y", -1, Viper.Input.Action.AXIS_LEFT_Y, 1.0)

DIM playerX AS DOUBLE = 400.0
DIM playerY AS DOUBLE = 300.0
DIM speed AS DOUBLE = 5.0

DO WHILE canvas.ShouldClose = 0
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
        PRINT "Firing..."
    END IF

    canvas.Clear(&H00000000)
    canvas.Disc(INT(playerX), INT(playerY), 20, &H00FF0000)
    canvas.Flip()
LOOP
```

### Example: Mouse Look with Action Mapping

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("FPS Camera", 800, 600)

' Define look actions
Viper.Input.Action.DefineAxis("look_x")
Viper.Input.Action.DefineAxis("look_y")

' Bind mouse delta with sensitivity
Viper.Input.Action.BindMouseX("look_x", 0.002)
Viper.Input.Action.BindMouseY("look_y", 0.002)

' Also allow gamepad right stick
Viper.Input.Action.BindPadAxis("look_x", -1, Viper.Input.Action.AXIS_RIGHT_X, 0.05)
Viper.Input.Action.BindPadAxis("look_y", -1, Viper.Input.Action.AXIS_RIGHT_Y, 0.05)

DIM yaw AS DOUBLE = 0.0
DIM pitch AS DOUBLE = 0.0

Viper.Input.Mouse.Capture()

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    yaw = yaw + Viper.Input.Action.Axis("look_x")
    pitch = pitch + Viper.Input.Action.Axis("look_y")

    ' Clamp pitch
    IF pitch > 1.57 THEN pitch = 1.57
    IF pitch < -1.57 THEN pitch = -1.57

    canvas.Clear(&H00000000)
    ' Render scene using yaw/pitch...
    canvas.Flip()
LOOP

Viper.Input.Mouse.Release()
```

### Example: Rebindable Controls

```basic
' Query current bindings
DIM jumpBindings AS STRING
jumpBindings = Viper.Input.Action.BindingsStr("jump")
PRINT "Jump is bound to: "; jumpBindings

' Check for conflicts
DIM conflict AS STRING
conflict = Viper.Input.Action.KeyBoundTo(Viper.Input.Keyboard.KEY_SPACE)
IF conflict <> "" THEN
    PRINT "Space is already bound to: "; conflict
END IF

' Rebind at runtime
Viper.Input.Action.UnbindKey("jump", Viper.Input.Keyboard.KEY_SPACE)
Viper.Input.Action.BindKey("jump", Viper.Input.Keyboard.KEY_W)
```

### Notes

- Action state is updated when `Canvas.Poll()` is called
- `Pressed()` and `Released()` only return true for one frame
- Multiple bindings on a button action trigger if ANY binding is active
- Axis bindings are combined (summed) and clamped to -1.0 to 1.0
- Use `AxisRaw()` to get the unclamped sum for advanced use cases
- Binding the same input to multiple actions is allowed
- Use pad index `-1` for "any controller" in local multiplayer setups

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

High-level input manager with debouncing and unified direction input. Simplifies input handling for games by providing
device-agnostic direction queries and built-in debouncing for menu navigation.

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
| `DebounceDelay`| Integer | Frames to wait before allowing repeat (r/w)    |

### Core Methods

| Method     | Signature | Description                                                      |
|------------|-----------|------------------------------------------------------------------|
| `Update()` | `Void()`  | Update input state; call once per frame after `Canvas.Poll()`    |

### Unified Direction Methods

These methods check ALL input sources (keyboard, D-pad, analog sticks) and return true if ANY is active:

| Method      | Signature   | Description                                              |
|-------------|-------------|----------------------------------------------------------|
| `Up()`      | `Boolean()` | Arrow Up, W, D-pad Up, or left stick up                  |
| `Down()`    | `Boolean()` | Arrow Down, S, D-pad Down, or left stick down            |
| `Left()`    | `Boolean()` | Arrow Left, A, D-pad Left, or left stick left            |
| `Right()`   | `Boolean()` | Arrow Right, D, D-pad Right, or left stick right         |
| `Confirm()` | `Boolean()` | Enter, Space, or gamepad A button                        |
| `Cancel()`  | `Boolean()` | Escape or gamepad B button                               |
| `AxisX()`   | `Double()`  | Combined horizontal axis (-1.0 to 1.0)                   |
| `AxisY()`   | `Double()`  | Combined vertical axis (-1.0 to 1.0)                     |

### Keyboard Methods

| Method                    | Signature           | Description                                          |
|---------------------------|---------------------|------------------------------------------------------|
| `KeyPressed(key)`         | `Boolean(Integer)`  | True if key was pressed this frame (edge detection)  |
| `KeyReleased(key)`        | `Boolean(Integer)`  | True if key was released this frame                  |
| `KeyHeld(key)`            | `Boolean(Integer)`  | True if key is currently held down                   |
| `KeyPressedDebounced(key)`| `Boolean(Integer)`  | True if pressed with debouncing (for menus)          |

### Mouse Methods

| Method                 | Signature           | Description                                     |
|------------------------|---------------------|-------------------------------------------------|
| `MousePressed(button)` | `Boolean(Integer)`  | True if button was pressed this frame           |
| `MouseReleased(button)`| `Boolean(Integer)`  | True if button was released this frame          |
| `MouseHeld(button)`    | `Boolean(Integer)`  | True if button is currently held                |
| `MouseX()`             | `Integer()`         | Current mouse X position                        |
| `MouseY()`             | `Integer()`         | Current mouse Y position                        |
| `MouseDeltaX()`        | `Integer()`         | Mouse X movement since last frame               |
| `MouseDeltaY()`        | `Integer()`         | Mouse Y movement since last frame               |
| `ScrollX()`            | `Integer()`         | Horizontal scroll delta                         |
| `ScrollY()`            | `Integer()`         | Vertical scroll delta                           |

### Gamepad Methods

| Method                        | Signature                   | Description                           |
|-------------------------------|-----------------------------|---------------------------------------|
| `PadPressed(pad, button)`     | `Boolean(Integer, Integer)` | True if button was pressed this frame |
| `PadReleased(pad, button)`    | `Boolean(Integer, Integer)` | True if button was released this frame|
| `PadHeld(pad, button)`        | `Boolean(Integer, Integer)` | True if button is currently held      |
| `PadLeftX(pad)`               | `Double(Integer)`           | Left stick X (-1.0 to 1.0)            |
| `PadLeftY(pad)`               | `Double(Integer)`           | Left stick Y (-1.0 to 1.0)            |
| `PadRightX(pad)`              | `Double(Integer)`           | Right stick X (-1.0 to 1.0)           |
| `PadRightY(pad)`              | `Double(Integer)`           | Right stick Y (-1.0 to 1.0)           |
| `PadLeftTrigger(pad)`         | `Double(Integer)`           | Left trigger (0.0 to 1.0)             |
| `PadRightTrigger(pad)`        | `Double(Integer)`           | Right trigger (0.0 to 1.0)            |

**Note:** Use pad index `-1` to check any connected controller.

### Example: Menu Navigation

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Menu Demo", 800, 600)

DIM input AS OBJECT = Viper.Input.Manager.New()
input.DebounceDelay = 12  ' Wait 12 frames between repeat inputs

DIM selectedItem AS INTEGER = 0
DIM menuItems(3) AS STRING
menuItems(0) = "New Game"
menuItems(1) = "Continue"
menuItems(2) = "Options"
menuItems(3) = "Exit"

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()
    input.Update()

    ' Navigation with unified direction (works with keyboard AND gamepad)
    IF input.Up() THEN
        selectedItem = selectedItem - 1
        IF selectedItem < 0 THEN selectedItem = 3
    END IF

    IF input.Down() THEN
        selectedItem = selectedItem + 1
        IF selectedItem > 3 THEN selectedItem = 0
    END IF

    ' Confirm selection
    IF input.Confirm() THEN
        SELECT CASE selectedItem
            CASE 0: StartNewGame()
            CASE 1: ContinueGame()
            CASE 2: ShowOptions()
            CASE 3: EXIT DO
        END SELECT
    END IF

    ' Back/Cancel
    IF input.Cancel() THEN
        EXIT DO
    END IF

    ' Render menu...
    canvas.Clear(&H00000000)
    FOR i = 0 TO 3
        DIM color AS INTEGER = &H00FFFFFF
        IF i = selectedItem THEN color = &H00FFFF00
        ' Draw menu item at y position...
    NEXT i
    canvas.Flip()
LOOP
```

### Example: Game Movement

```basic
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("Game", 800, 600)

DIM input AS OBJECT = Viper.Input.Manager.New()

DIM playerX AS DOUBLE = 400.0
DIM playerY AS DOUBLE = 300.0
DIM speed AS DOUBLE = 5.0

DO WHILE canvas.ShouldClose = 0
    canvas.Poll()
    input.Update()

    ' Smooth movement using axis values (works with WASD, arrows, AND analog stick)
    playerX = playerX + input.AxisX() * speed
    playerY = playerY + input.AxisY() * speed

    ' Or use digital direction for grid-based movement
    IF input.Left() THEN playerX = playerX - speed
    IF input.Right() THEN playerX = playerX + speed
    IF input.Up() THEN playerY = playerY - speed
    IF input.Down() THEN playerY = playerY + speed

    canvas.Clear(&H00000000)
    canvas.Disc(INT(playerX), INT(playerY), 20, &H00FF0000)
    canvas.Flip()
LOOP
```

### Example: Debounced Key Input

```basic
DIM input AS OBJECT = Viper.Input.Manager.New()
input.DebounceDelay = 15  ' About 250ms at 60fps

DO WHILE running
    canvas.Poll()
    input.Update()

    ' Non-debounced: fires every frame while held
    IF input.KeyHeld(Viper.Input.Keyboard.KEY_SPACE) THEN
        FireWeapon()  ' Continuous fire
    END IF

    ' Debounced: fires once, then requires release or wait
    IF input.KeyPressedDebounced(Viper.Input.Keyboard.KEY_P) THEN
        TogglePause()  ' Won't rapid-toggle
    END IF
LOOP
```

### Notes

- Call `Update()` once per frame after `Canvas.Poll()`
- Unified direction methods combine all input sources automatically
- Debouncing prevents rapid repeated inputs from held keys
- AxisX/AxisY return smooth analog values from sticks, or -1/0/1 from digital inputs
- Use pad index `-1` to accept input from any connected gamepad

### InputManager vs Low-Level Classes

| Feature                | InputManager                    | Keyboard/Mouse/Pad            |
|------------------------|---------------------------------|-------------------------------|
| Unified directions     | Yes (Up/Down/Left/Right)        | No (check each device)        |
| Built-in debouncing    | Yes (`KeyPressedDebounced`)     | No (must implement manually)  |
| Device-agnostic axes   | Yes (`AxisX`, `AxisY`)          | No                            |
| Confirm/Cancel actions | Yes (built-in mappings)         | No                            |
| Per-key control        | Yes (falls through to low-level)| Yes                           |
| State tracking         | Automatic                       | Must track manually           |

### Use Cases

- **Menu navigation:** Unified Up/Down/Confirm/Cancel
- **Character movement:** Combined axis input from all devices
- **Dialog systems:** Debounced input to prevent skipping
- **Inventory screens:** Device-agnostic selection
- **Quick prototyping:** Less boilerplate than raw input classes

---

## See Also

- [Graphics](graphics.md) - `Canvas` class for windowing and rendering that drives input polling
- [Collections](collections.md) - `Seq` type returned by `GetPressed()` and `GetReleased()` methods
- [Time](time.md) - `Timer` class for frame-based timing that pairs well with InputManager
