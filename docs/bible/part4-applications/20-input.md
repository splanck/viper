# Chapter 20: User Input

Graphics let you show things. But games and interactive applications need to respond to the player. Press a key, the character jumps. Move the mouse, the cursor follows. Click a button, something happens.

This chapter covers handling user input: keyboard, mouse, and game controllers.

---

## Keyboard Input

There are two ways to think about keyboard input:

**Key events**: A key was just pressed or released. Good for actions: "jump when space is pressed."

**Key state**: Is a key currently held down? Good for continuous actions: "move right while the right arrow is held."

### Checking Key State

```viper
import Viper.Input;

while gameRunning {
    if Input.isKeyDown(Key.LEFT) {
        player.x -= speed * dt;
    }
    if Input.isKeyDown(Key.RIGHT) {
        player.x += speed * dt;
    }
    if Input.isKeyDown(Key.UP) {
        player.y -= speed * dt;
    }
    if Input.isKeyDown(Key.DOWN) {
        player.y += speed * dt;
    }
}
```

This checks every frame whether each key is currently pressed.

### Key Events

For one-time actions, check if a key was just pressed this frame:

```viper
if Input.wasKeyPressed(Key.SPACE) {
    player.jump();
}

if Input.wasKeyPressed(Key.ESCAPE) {
    pauseGame();
}
```

`wasKeyPressed` returns true only on the frame the key went from up to down.

### Common Key Codes

```viper
// Arrow keys
Key.LEFT, Key.RIGHT, Key.UP, Key.DOWN

// Letters (uppercase)
Key.A, Key.B, Key.C, ... Key.Z

// Numbers
Key.NUM_0, Key.NUM_1, ... Key.NUM_9

// Function keys
Key.F1, Key.F2, ... Key.F12

// Special keys
Key.SPACE, Key.ENTER, Key.ESCAPE
Key.TAB, Key.BACKSPACE, Key.DELETE
Key.SHIFT, Key.CTRL, Key.ALT
```

---

## Mouse Input

### Position

```viper
var mouseX = Input.mouseX();
var mouseY = Input.mouseY();
```

This gives you the mouse position in canvas coordinates.

### Buttons

```viper
if Input.isMouseDown(MouseButton.LEFT) {
    // Left button is held
}

if Input.wasMousePressed(MouseButton.LEFT) {
    // Left button was just clicked
}

if Input.wasMouseReleased(MouseButton.RIGHT) {
    // Right button was just released
}
```

Mouse buttons:
```viper
MouseButton.LEFT
MouseButton.RIGHT
MouseButton.MIDDLE
```

### Mouse Wheel

```viper
var scroll = Input.mouseScroll();  // Positive = up, negative = down
if scroll != 0 {
    zoomLevel += scroll * 0.1;
}
```

### Example: Drawing with Mouse

```viper
module MouseDraw;

import Viper.Graphics;
import Viper.Input;

func start() {
    var canvas = Canvas(800, 600);
    canvas.setTitle("Draw with Mouse");

    canvas.setColor(Color.WHITE);
    canvas.fillRect(0, 0, 800, 600);

    while canvas.isOpen() {
        if Input.isMouseDown(MouseButton.LEFT) {
            var x = Input.mouseX();
            var y = Input.mouseY();
            canvas.setColor(Color.BLACK);
            canvas.fillCircle(x, y, 5);
        }

        canvas.show();
        Viper.Time.sleep(16);
    }
}
```

Hold the left mouse button and drag to draw!

---

## Game Controllers

Many games support controllers (gamepads):

```viper
if Input.isControllerConnected(0) {  // First controller
    var leftX = Input.controllerAxis(0, Axis.LEFT_X);
    var leftY = Input.controllerAxis(0, Axis.LEFT_Y);

    // Axis values are -1.0 to 1.0
    player.x += leftX * speed * dt;
    player.y += leftY * speed * dt;

    if Input.isControllerButtonDown(0, ControllerButton.A) {
        player.jump();
    }
}
```

### Controller Buttons

```viper
ControllerButton.A, .B, .X, .Y
ControllerButton.LEFT_BUMPER, .RIGHT_BUMPER
ControllerButton.START, .SELECT
ControllerButton.DPAD_UP, .DPAD_DOWN, .DPAD_LEFT, .DPAD_RIGHT
```

### Controller Axes

```viper
Axis.LEFT_X, Axis.LEFT_Y    // Left stick
Axis.RIGHT_X, Axis.RIGHT_Y  // Right stick
Axis.LEFT_TRIGGER           // Left trigger (0 to 1)
Axis.RIGHT_TRIGGER          // Right trigger (0 to 1)
```

---

## Input Abstraction

Games often abstract input so the same action can come from different sources:

```viper
entity InputManager {
    func getMoveX() -> f64 {
        // Keyboard
        if Input.isKeyDown(Key.LEFT) {
            return -1.0;
        }
        if Input.isKeyDown(Key.RIGHT) {
            return 1.0;
        }

        // Controller
        if Input.isControllerConnected(0) {
            var axis = Input.controllerAxis(0, Axis.LEFT_X);
            if Viper.Math.abs(axis) > 0.2 {  // Dead zone
                return axis;
            }
        }

        return 0.0;
    }

    func isJumpPressed() -> bool {
        return Input.wasKeyPressed(Key.SPACE) ||
               Input.wasControllerButtonPressed(0, ControllerButton.A);
    }
}
```

Now the game code uses `input.getMoveX()` instead of checking specific keys.

---

## Input Handling Patterns

### Dead Zones

Controller sticks rarely rest at exactly (0, 0). Use a dead zone:

```viper
func applyDeadZone(value: f64, threshold: f64) -> f64 {
    if Viper.Math.abs(value) < threshold {
        return 0.0;
    }
    return value;
}

var moveX = applyDeadZone(Input.controllerAxis(0, Axis.LEFT_X), 0.15);
```

### Key Mapping

Let players customize controls:

```viper
entity KeyMap {
    hide bindings: Map<string, i64>;

    expose func init() {
        // Default bindings
        self.bindings = Map.new();
        self.bindings.set("jump", Key.SPACE);
        self.bindings.set("left", Key.LEFT);
        self.bindings.set("right", Key.RIGHT);
        self.bindings.set("fire", Key.CTRL);
    }

    func isActionDown(action: string) -> bool {
        var key = self.bindings.get(action);
        return Input.isKeyDown(key);
    }

    func rebind(action: string, key: i64) {
        self.bindings.set(action, key);
    }
}
```

### Buffering Inputs

Some games accept inputs slightly before they're valid (e.g., pressing jump just before landing):

```viper
entity InputBuffer {
    hide jumpBufferTime: f64;
    hide jumpBufferDuration: f64 = 0.1;

    func update(dt: f64) {
        if self.jumpBufferTime > 0 {
            self.jumpBufferTime -= dt;
        }

        if Input.wasKeyPressed(Key.SPACE) {
            self.jumpBufferTime = self.jumpBufferDuration;
        }
    }

    func consumeJump() -> bool {
        if self.jumpBufferTime > 0 {
            self.jumpBufferTime = 0;
            return true;
        }
        return false;
    }
}
```

---

## A Complete Example: Controllable Character

```viper
module CharacterDemo;

import Viper.Graphics;
import Viper.Input;

value Player {
    x: f64;
    y: f64;
    vx: f64;
    vy: f64;
    onGround: bool;
}

final GRAVITY = 800.0;
final JUMP_SPEED = -400.0;
final MOVE_SPEED = 200.0;
final GROUND_Y = 500.0;

func start() {
    var canvas = Canvas(800, 600);
    canvas.setTitle("Character Control");

    var player = Player {
        x: 400.0,
        y: GROUND_Y,
        vx: 0.0,
        vy: 0.0,
        onGround: true
    };

    var lastTime = Viper.Time.millis();

    while canvas.isOpen() {
        var now = Viper.Time.millis();
        var dt = (now - lastTime) / 1000.0;
        lastTime = now;

        // Input
        player.vx = 0.0;
        if Input.isKeyDown(Key.LEFT) || Input.isKeyDown(Key.A) {
            player.vx = -MOVE_SPEED;
        }
        if Input.isKeyDown(Key.RIGHT) || Input.isKeyDown(Key.D) {
            player.vx = MOVE_SPEED;
        }

        if Input.wasKeyPressed(Key.SPACE) && player.onGround {
            player.vy = JUMP_SPEED;
            player.onGround = false;
        }

        // Physics
        if !player.onGround {
            player.vy += GRAVITY * dt;
        }

        player.x += player.vx * dt;
        player.y += player.vy * dt;

        // Ground collision
        if player.y >= GROUND_Y {
            player.y = GROUND_Y;
            player.vy = 0.0;
            player.onGround = true;
        }

        // Keep on screen
        if player.x < 25 { player.x = 25; }
        if player.x > 775 { player.x = 775; }

        // Render
        canvas.setColor(Color(100, 150, 255));  // Sky
        canvas.fillRect(0, 0, 800, GROUND_Y + 50);

        canvas.setColor(Color(50, 150, 50));  // Ground
        canvas.fillRect(0, GROUND_Y + 50, 800, 100);

        canvas.setColor(Color.RED);  // Player
        canvas.fillRect(player.x - 25, player.y - 50, 50, 50);

        canvas.setColor(Color.WHITE);
        canvas.setFont("Arial", 16);
        canvas.drawText(10, 25, "Arrow keys or A/D to move, Space to jump");

        canvas.show();
        Viper.Time.sleep(16);
    }
}
```

---

## The Three Languages

**ViperLang**
```viper
import Viper.Input;

if Input.isKeyDown(Key.SPACE) {
    player.jump();
}

var mx = Input.mouseX();
var my = Input.mouseY();
```

**BASIC**
```basic
IF KEYDOWN(KEY_SPACE) THEN
    CALL Jump()
END IF

DIM mx AS INTEGER, my AS INTEGER
mx = MOUSEX
my = MOUSEY
```

**Pascal**
```pascal
uses ViperInput;

if IsKeyDown(VK_SPACE) then
    Jump;

mx := MouseX;
my := MouseY;
```

---

## Summary

- *Key state* checks if a key is currently held down
- *Key events* detect when a key is pressed or released
- Mouse provides position and button state
- Game controllers have sticks (axes) and buttons
- Abstract input to support multiple control schemes
- Use *dead zones* for controller sticks
- Consider *input buffering* for responsive controls

---

## Exercises

**Exercise 20.1**: Create a cursor that follows the mouse position.

**Exercise 20.2**: Make a simple "avoid the obstacles" game where the player character follows the mouse and must avoid randomly moving rectangles.

**Exercise 20.3**: Create a color picker: three sliders (controlled by clicking/dragging) for R, G, B, showing the resulting color.

**Exercise 20.4**: Implement a simple text input field that shows characters as you type.

**Exercise 20.5**: Create a simple drawing program with different brush sizes (use number keys to change size) and colors (use letter keys).

**Exercise 20.6** (Challenge): Create a two-player game where one player uses WASD and another uses arrow keys.

---

*We can draw graphics and handle input. Now let's build a complete game from scratch, putting together everything we've learned.*

*[Continue to Chapter 21: Building a Game →](21-game-project.md)*
