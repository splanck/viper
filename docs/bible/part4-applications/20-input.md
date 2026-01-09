# Chapter 20: User Input

Graphics let you show things to users. But showing is only half of interactive software. The other half is listening. When the player presses a key, the character should jump. When they move the mouse, the cursor should follow. When they click a button, something should happen.

Without input handling, your program is like a movie — it plays, but the audience can't participate. With input handling, it becomes a conversation. The user speaks through the keyboard, mouse, and controller. Your program listens and responds.

This chapter teaches you how to hear what users are saying.

---

## Why Input Matters

Think about the programs you use every day. A word processor responds to every keystroke. A web browser follows your mouse as you scroll and click. A video game translates your button mashes into on-screen action. All of these programs share something fundamental: they react to you.

Input handling is what separates passive media from interactive software. A photograph doesn't change when you look at it. A book doesn't rewrite itself based on how fast you turn pages. But software can respond to your every action — if it knows how to listen.

This responsiveness creates engagement. When a character jumps the instant you press the button, the game feels good. When there's a delay, it feels sluggish. When the game ignores your input entirely, it feels broken. Great input handling is invisible; you don't notice it because the program simply does what you expect. Poor input handling is immediately obvious and frustrating.

---

## The Input Mental Model

Before we write any code, let's understand how input actually works. This mental model will help you debug problems and design better interactive programs.

### The Mail Carrier Analogy

Imagine you're waiting for an important letter. You have two ways to find out when it arrives:

**Approach 1: Constantly check the mailbox.** Every few minutes, you walk outside, open the mailbox, and look inside. This is *polling* — you repeatedly ask "is there anything new?" Most of the time the answer is no, but you keep checking anyway.

**Approach 2: Listen for the mail carrier.** You install a doorbell that rings when mail is delivered. You go about your business, and when you hear the bell, you know something arrived. This is *event-driven* input — the system notifies you when something happens.

Both approaches work. Polling is simpler to understand but can waste effort. Event-driven systems are more efficient but require you to set up the notification mechanism.

Most game loops use polling because games update every frame anyway — you're already checking everything 60 times per second, so checking input at the same time costs nothing extra. GUI applications often use events because they might sit idle for seconds or minutes between user actions.

### The Input Pipeline

When you press a key on your keyboard, quite a lot happens before your program can respond:

1. **Physical action**: Your finger pushes down a key
2. **Hardware detection**: The keyboard's internal circuitry detects which key moved
3. **Signal transmission**: The keyboard sends a signal to the computer (via USB, Bluetooth, etc.)
4. **Operating system processing**: The OS receives the signal and determines which key was pressed
5. **Event creation**: The OS creates an "input event" and places it in a queue
6. **Delivery to application**: The OS delivers the event to your program's input queue
7. **Program processing**: Your code reads the event and responds

This happens in milliseconds, fast enough to feel instantaneous. But understanding this pipeline helps you debug problems. If a keypress doesn't work, the issue could be anywhere along this chain — from a stuck key to a bug in your event-handling code.

### The Input Queue

When you press multiple keys quickly, the computer doesn't lose any of them. Instead, it stores them in an *input queue* — a waiting line of events that your program processes one at a time.

Think of it like a restaurant's order tickets. When customers order faster than the kitchen can cook, the tickets pile up in a queue. The kitchen works through them in order. Similarly, your program works through input events in the order they arrived.

This is why games feel responsive even during complex scenes. The input is captured immediately and queued; even if a frame takes a bit longer to render, the input events are waiting patiently to be processed.

---

## Input Devices

Before we write code, let's understand what we're working with. Each input device has its own characteristics and quirks.

### The Keyboard

A keyboard is essentially a grid of buttons. Each key is either *up* (not pressed) or *down* (pressed). When you press a key, it generates a "key down" event. When you release it, it generates a "key up" event. Some keys (like Shift or Ctrl) are "modifier" keys that change the meaning of other keys.

Keyboards also have *key repeat*. If you hold down a key, after a short delay, the computer starts generating repeated "key down" events, as if you were typing the key over and over. This is great for text editing (hold backspace to delete multiple characters) but can be confusing for games (you don't want your character to jump repeatedly just because you're holding the button).

### The Mouse

A mouse reports two kinds of information: *position* and *buttons*.

Position is straightforward — the mouse is at coordinates (x, y) on the screen. These coordinates update continuously as you move the mouse.

Mouse buttons work like keyboard keys — they can be up or down, and they generate press and release events. Most mice have at least left, right, and middle buttons. Many also have a scroll wheel, which generates scroll events when you roll it.

One subtlety: mouse position is in *screen coordinates*, but your game probably uses *canvas coordinates*. If your canvas is smaller than the full window, or if it's scaled, you'll need to convert between them.

### Game Controllers

Game controllers (gamepads) have both digital and analog inputs:

**Digital inputs** (buttons) work like keyboard keys — they're either pressed or not. Controllers typically have face buttons (A, B, X, Y), shoulder buttons (bumpers and triggers), and a D-pad (directional pad with up, down, left, right).

**Analog inputs** (sticks and triggers) report a range of values. An analog stick reports its position as two numbers (x and y), typically ranging from -1.0 to 1.0. A trigger reports how far it's pressed, typically 0.0 to 1.0.

Analog inputs introduce a complication: *dead zones*. When you're not touching an analog stick, it should report (0, 0). But physical sticks rarely rest exactly at center — they might report (0.03, -0.02) even when untouched. Without dead zone handling, your character would slowly drift even when you're not touching the controller.

---

## Event Types

Input events come in several flavors. Understanding the difference is crucial for handling input correctly.

### Press vs. Release Events

A key press event occurs once, at the instant a key goes from up to down. A key release event occurs once, at the instant a key goes from down to up.

This distinction matters enormously:

```rust
// This fires continuously while the key is held
if Input.isKeyDown(Key.SPACE) {
    // This code runs 60 times per second while space is held!
}

// This fires exactly once when the key is first pressed
if Input.wasKeyPressed(Key.SPACE) {
    // This code runs once, when space goes from up to down
}
```

For a jump action, you almost always want `wasKeyPressed`. If you use `isKeyDown`, the character will try to jump every single frame while you hold the button, which is almost never what you want.

### Continuous State vs. Discrete Events

Some actions need the key's current state (is it down right now?). Other actions need discrete events (was it just pressed?).

**Use state checks (`isKeyDown`) for:**
- Continuous movement: "move right while arrow is held"
- Holding actions: "aim while right-click is held"
- Any action that should continue as long as the button is held

**Use event checks (`wasKeyPressed`) for:**
- One-time actions: "jump", "shoot", "pause"
- Menu navigation: "select next item"
- Any action that should happen once per button press

### Text Input Events

Games typically use key codes (like `Key.A`), but text input is more complex. When the user presses Shift+A, they probably want an uppercase 'A', not separate events for Shift and A. When they're typing in a language that requires an input method editor (like Chinese or Japanese), a single character might require multiple key presses.

For text input (like typing a player's name), Viper provides text input events that handle all this complexity:

```rust
if Input.hasTextInput() {
    var text = Input.getTextInput();  // Gets the actual characters typed
    playerName += text;
}
```

---

## Tracing a Key Press: What Really Happens

Let's trace exactly what happens when a player presses the Space bar to jump. Understanding this flow will help you debug input problems.

**Frame 0:** The player's finger pushes down the Space bar. The keyboard hardware detects this and sends a signal to the computer.

**Frame 1:** The operating system receives the signal and creates a key-down event for Space. This event goes into your program's input queue. At the start of this frame, Viper's input system reads all queued events. It notices that Space just went from "up" to "down", so it:
- Sets the internal state of Space to "down"
- Sets a flag indicating Space was "just pressed" this frame

Your game loop runs. When it calls `Input.wasKeyPressed(Key.SPACE)`, the function returns `true` because the "just pressed" flag is set. Your code calls `player.jump()`, and the character begins rising into the air.

**Frame 2:** No new Space events (the player is still holding the key). At the start of this frame, Viper clears all the "just pressed" flags from the previous frame. The state of Space is still "down", but it's no longer "just pressed."

Your game loop runs. `Input.wasKeyPressed(Key.SPACE)` now returns `false` because the flag was cleared. But `Input.isKeyDown(Key.SPACE)` still returns `true` because the key is still being held. Your physics code continues the jump — the character rises and then falls.

**Frame 10:** The player releases the Space bar. The OS creates a key-up event. Viper reads it and:
- Sets the internal state of Space to "up"
- Sets a flag indicating Space was "just released" this frame

**Frame 11:** The "just released" flag is cleared. Space is now fully up with no special flags.

This is why `wasKeyPressed` only returns true for one frame. The "just pressed" state is temporary — it exists only to let you detect the transition from up to down.

---

## Keyboard Input

Now let's write some code. We'll start with the keyboard, the most common input device for games and applications.

### Checking Key State

To check if a key is currently held down, use `isKeyDown`:

```rust
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

Let's trace through this code:

- `Input.isKeyDown(Key.LEFT)` returns `true` if the left arrow key is currently pressed, `false` otherwise.
- If it's pressed, we subtract from `player.x`, moving the player leftward.
- We multiply by `speed` (how fast to move) and `dt` (delta time — how many seconds passed since the last frame). This makes movement consistent regardless of frame rate.
- We check each direction independently, so the player can move diagonally by holding two arrows.

Notice we're checking every frame. This is polling in action — we're constantly asking "is this key down?" sixty times per second.

### Detecting Key Presses

For one-time actions, use `wasKeyPressed`:

```rust
if Input.wasKeyPressed(Key.SPACE) {
    player.jump();
}

if Input.wasKeyPressed(Key.ESCAPE) {
    pauseGame();
}

if Input.wasKeyPressed(Key.R) {
    restartLevel();
}
```

Why use `wasKeyPressed` instead of `isKeyDown`?

- `wasKeyPressed` returns `true` only on the frame the key went from up to down. The next frame, it returns `false` (even if the key is still held).
- `isKeyDown` returns `true` on every frame while the key is held.

For jumping, you want one jump per button press. If you used `isKeyDown`, the character would try to jump continuously while you hold the button — usually resulting in a single jump (since you can't jump while in the air), but the code would be wastefully checking every frame.

### Detecting Key Release

Sometimes you need to know when a key is released:

```rust
if Input.wasKeyReleased(Key.CTRL) {
    // Player released the aim button
    releaseArrow();  // Fire the arrow they were aiming
}
```

This is common in "charge and release" mechanics. The player holds a button to charge an attack, then releases to fire.

### Common Key Codes

Viper provides named constants for all standard keys:

```rust
// Arrow keys
Key.LEFT, Key.RIGHT, Key.UP, Key.DOWN

// Letters (uppercase names, but detect both cases)
Key.A, Key.B, Key.C, ... Key.Z

// Numbers (top row of keyboard)
Key.NUM_0, Key.NUM_1, ... Key.NUM_9

// Numpad numbers
Key.NUMPAD_0, Key.NUMPAD_1, ... Key.NUMPAD_9

// Function keys
Key.F1, Key.F2, ... Key.F12

// Special keys
Key.SPACE, Key.ENTER, Key.ESCAPE
Key.TAB, Key.BACKSPACE, Key.DELETE
Key.HOME, Key.END, Key.PAGE_UP, Key.PAGE_DOWN

// Modifier keys
Key.SHIFT, Key.CTRL, Key.ALT
Key.LEFT_SHIFT, Key.RIGHT_SHIFT  // Distinguish left from right
Key.LEFT_CTRL, Key.RIGHT_CTRL
Key.LEFT_ALT, Key.RIGHT_ALT
```

### Checking Modifier Keys

Modifier keys (Shift, Ctrl, Alt) are often used in combination with other keys:

```rust
if Input.wasKeyPressed(Key.S) && Input.isKeyDown(Key.CTRL) {
    saveGame();  // Ctrl+S to save
}

if Input.isKeyDown(Key.SHIFT) {
    speed = runSpeed;  // Hold shift to run
} else {
    speed = walkSpeed;
}
```

Notice the pattern: we check if S was *pressed* and if Ctrl is *down*. This correctly handles the timing — the action triggers when S is pressed, not when Ctrl is pressed.

---

## Mouse Input

The mouse provides position and button information. Let's explore both.

### Reading Mouse Position

```rust
var mouseX = Input.mouseX();
var mouseY = Input.mouseY();
```

These return the mouse position in canvas coordinates. The top-left corner of your canvas is (0, 0).

You can use mouse position for many things:

```rust
// Make something follow the mouse
cursor.x = Input.mouseX();
cursor.y = Input.mouseY();

// Check if mouse is over a button
var mx = Input.mouseX();
var my = Input.mouseY();
if mx >= button.x && mx < button.x + button.width &&
   my >= button.y && my < button.y + button.height {
    // Mouse is over the button
    button.highlighted = true;
}

// Point a turret at the mouse
var dx = Input.mouseX() - turret.x;
var dy = Input.mouseY() - turret.y;
turret.angle = Viper.Math.atan2(dy, dx);
```

### Mouse Buttons

Mouse buttons work like keyboard keys:

```rust
// Check if button is currently held
if Input.isMouseDown(MouseButton.LEFT) {
    // Left button is being held
}

// Check if button was just clicked
if Input.wasMousePressed(MouseButton.LEFT) {
    // Left button was just clicked
}

// Check if button was just released
if Input.wasMouseReleased(MouseButton.RIGHT) {
    // Right button was just released
}
```

The available buttons are:

```rust
MouseButton.LEFT    // Primary click
MouseButton.RIGHT   // Secondary click (context menus)
MouseButton.MIDDLE  // Usually clicking the scroll wheel
```

### The Scroll Wheel

The scroll wheel reports how much it moved:

```rust
var scroll = Input.mouseScroll();  // Positive = up, negative = down
if scroll != 0 {
    zoomLevel += scroll * 0.1;
}
```

Note that scroll values are typically small integers (-1, 0, or 1), though some mice report fractional values for smooth scrolling.

### Example: A Simple Drawing Program

Let's put mouse input together in a practical example:

```rust
module MouseDraw;

import Viper.Graphics;
import Viper.Input;

func start() {
    var canvas = Canvas(800, 600);
    canvas.setTitle("Draw with Mouse");

    // Start with a white background
    canvas.setColor(Color.WHITE);
    canvas.fillRect(0, 0, 800, 600);

    while canvas.isOpen() {
        // When left button is held, draw at mouse position
        if Input.isMouseDown(MouseButton.LEFT) {
            var x = Input.mouseX();
            var y = Input.mouseY();
            canvas.setColor(Color.BLACK);
            canvas.fillCircle(x, y, 5);
        }

        // Right click clears the canvas
        if Input.wasMousePressed(MouseButton.RIGHT) {
            canvas.setColor(Color.WHITE);
            canvas.fillRect(0, 0, 800, 600);
        }

        canvas.show();
        Viper.Time.sleep(16);
    }
}
```

Let's trace through this code:

1. We create an 800x600 canvas and fill it with white.
2. In our main loop, we check if the left mouse button is held (`isMouseDown`, not `wasMousePressed`).
3. While held, we draw a small black circle at the current mouse position. As the user drags, we draw many circles, creating a line.
4. If the right button is clicked, we clear the canvas back to white.
5. We show the result and wait 16 milliseconds (~60 FPS).

This demonstrates a key insight: use `isMouseDown` for continuous actions (drawing while dragging) and `wasMousePressed` for discrete actions (clearing on click).

---

## Game Controllers

Game controllers (gamepads) offer a different experience from keyboard and mouse. Their analog sticks provide smooth, proportional control that's hard to achieve with binary key presses.

### Checking Connection

Before reading from a controller, check if one is connected:

```rust
if Input.isControllerConnected(0) {  // Controller 0 (first controller)
    // Safe to read controller input
}
```

Controllers are numbered starting from 0. Most games support at least 4 controllers for local multiplayer.

### Reading Analog Sticks

Analog sticks report their position as two values (x and y), each ranging from -1.0 to 1.0:

```rust
if Input.isControllerConnected(0) {
    var leftX = Input.controllerAxis(0, Axis.LEFT_X);
    var leftY = Input.controllerAxis(0, Axis.LEFT_Y);

    // Move player based on stick position
    player.x += leftX * speed * dt;
    player.y += leftY * speed * dt;
}
```

The value represents how far the stick is pushed:
- 0.0 = centered
- 1.0 = fully pushed right (for X) or down (for Y)
- -1.0 = fully pushed left (for X) or up (for Y)

Values in between give you proportional control. Pushing the stick halfway right gives about 0.5, letting the player walk slowly. Pushing it fully gives 1.0, for full speed.

### Controller Buttons

Controller buttons work like keyboard keys:

```rust
if Input.isControllerButtonDown(0, ControllerButton.A) {
    // A button is held
}

if Input.wasControllerButtonPressed(0, ControllerButton.A) {
    player.jump();
}
```

### Controller Button Names

```rust
// Face buttons
ControllerButton.A, ControllerButton.B
ControllerButton.X, ControllerButton.Y

// Shoulder buttons
ControllerButton.LEFT_BUMPER, ControllerButton.RIGHT_BUMPER

// Stick clicks
ControllerButton.LEFT_STICK, ControllerButton.RIGHT_STICK

// Menu buttons
ControllerButton.START, ControllerButton.SELECT

// D-pad (digital directional pad)
ControllerButton.DPAD_UP, ControllerButton.DPAD_DOWN
ControllerButton.DPAD_LEFT, ControllerButton.DPAD_RIGHT
```

### Controller Axes

```rust
Axis.LEFT_X, Axis.LEFT_Y    // Left stick (-1 to 1)
Axis.RIGHT_X, Axis.RIGHT_Y  // Right stick (-1 to 1)
Axis.LEFT_TRIGGER           // Left trigger (0 to 1)
Axis.RIGHT_TRIGGER          // Right trigger (0 to 1)
```

Note that triggers are 0 to 1 (not -1 to 1) because they only go one direction — from released (0) to fully pressed (1).

---

## Input Patterns

Now that we understand the basics, let's explore patterns that make input handling robust and professional.

### Dead Zones

Remember that analog sticks rarely rest exactly at (0, 0). A dead zone ignores small values near the center:

```rust
func applyDeadZone(value: f64, threshold: f64) -> f64 {
    if Viper.Math.abs(value) < threshold {
        return 0.0;  // Treat small values as zero
    }
    return value;
}

// Usage
var rawX = Input.controllerAxis(0, Axis.LEFT_X);
var moveX = applyDeadZone(rawX, 0.15);  // Ignore values under 0.15
```

A threshold of 0.15 is typical. Too low, and the character drifts. Too high, and the stick feels unresponsive.

A more sophisticated dead zone smoothly scales the value to avoid a "jump" when crossing the threshold:

```rust
func applyDeadZoneSmooth(value: f64, threshold: f64) -> f64 {
    var absValue = Viper.Math.abs(value);
    if absValue < threshold {
        return 0.0;
    }
    // Scale remaining range to 0-1
    var sign = if value < 0 { -1.0 } else { 1.0 };
    return sign * (absValue - threshold) / (1.0 - threshold);
}
```

This makes the transition from dead zone to movement smooth rather than abrupt.

### Input Abstraction

Games should abstract input so the same action can come from different sources. This lets players use their preferred input device and makes your code cleaner:

```rust
entity InputManager {
    func getMoveX() -> f64 {
        // Check keyboard first
        if Input.isKeyDown(Key.LEFT) || Input.isKeyDown(Key.A) {
            return -1.0;
        }
        if Input.isKeyDown(Key.RIGHT) || Input.isKeyDown(Key.D) {
            return 1.0;
        }

        // Then check controller
        if Input.isControllerConnected(0) {
            var axis = Input.controllerAxis(0, Axis.LEFT_X);
            if Viper.Math.abs(axis) > 0.2 {  // Dead zone
                return axis;
            }
        }

        return 0.0;
    }

    func getMoveY() -> f64 {
        if Input.isKeyDown(Key.UP) || Input.isKeyDown(Key.W) {
            return -1.0;
        }
        if Input.isKeyDown(Key.DOWN) || Input.isKeyDown(Key.S) {
            return 1.0;
        }

        if Input.isControllerConnected(0) {
            var axis = Input.controllerAxis(0, Axis.LEFT_Y);
            if Viper.Math.abs(axis) > 0.2 {
                return axis;
            }
        }

        return 0.0;
    }

    func isJumpPressed() -> bool {
        return Input.wasKeyPressed(Key.SPACE) ||
               Input.wasKeyPressed(Key.W) ||
               Input.wasControllerButtonPressed(0, ControllerButton.A);
    }

    func isActionPressed() -> bool {
        return Input.wasKeyPressed(Key.E) ||
               Input.wasKeyPressed(Key.ENTER) ||
               Input.wasControllerButtonPressed(0, ControllerButton.X);
    }
}
```

Now your game code becomes clean and device-agnostic:

```rust
var input = InputManager();

while gameRunning {
    // Movement works with keyboard or controller
    player.x += input.getMoveX() * speed * dt;
    player.y += input.getMoveY() * speed * dt;

    // Actions work with any input device
    if input.isJumpPressed() {
        player.jump();
    }
    if input.isActionPressed() {
        player.interact();
    }
}
```

### Key Mapping (Rebindable Controls)

Players appreciate customizable controls. A key map stores the current bindings:

```rust
entity KeyMap {
    hide bindings: Map<string, i64>;

    expose func init() {
        self.bindings = Map.new();
        // Default bindings
        self.bindings.set("jump", Key.SPACE);
        self.bindings.set("left", Key.LEFT);
        self.bindings.set("right", Key.RIGHT);
        self.bindings.set("up", Key.UP);
        self.bindings.set("down", Key.DOWN);
        self.bindings.set("fire", Key.CTRL);
        self.bindings.set("pause", Key.ESCAPE);
    }

    func isActionDown(action: string) -> bool {
        var key = self.bindings.get(action);
        return Input.isKeyDown(key);
    }

    func wasActionPressed(action: string) -> bool {
        var key = self.bindings.get(action);
        return Input.wasKeyPressed(key);
    }

    func rebind(action: string, key: i64) {
        self.bindings.set(action, key);
    }

    func getBinding(action: string) -> i64 {
        return self.bindings.get(action);
    }
}
```

To let the player rebind a key:

```rust
func waitForKeyAndRebind(keyMap: KeyMap, action: string) {
    // Wait for any key press
    while true {
        for keyCode in 0..256 {
            if Input.wasKeyPressed(keyCode) {
                keyMap.rebind(action, keyCode);
                return;
            }
        }
        Viper.Time.sleep(16);
    }
}
```

### Input Buffering

Professional games use *input buffering* to feel responsive. The idea: remember recent inputs and use them when they become valid.

Imagine you're playing a platformer. Your character is falling toward the ground. You press jump slightly before landing. Without buffering, the jump is ignored because you weren't on the ground yet. With buffering, the game remembers you pressed jump and executes it the moment you land.

```rust
entity InputBuffer {
    hide jumpBufferTime: f64;
    hide jumpBufferDuration: f64 = 0.1;  // 100ms buffer window

    func update(dt: f64) {
        // Decrease buffer timer
        if self.jumpBufferTime > 0 {
            self.jumpBufferTime -= dt;
        }

        // When jump is pressed, start the buffer timer
        if Input.wasKeyPressed(Key.SPACE) {
            self.jumpBufferTime = self.jumpBufferDuration;
        }
    }

    func consumeJump() -> bool {
        // If there's a buffered jump, use it and clear the buffer
        if self.jumpBufferTime > 0 {
            self.jumpBufferTime = 0;
            return true;
        }
        return false;
    }
}
```

Usage in your game:

```rust
var inputBuffer = InputBuffer();

while gameRunning {
    inputBuffer.update(dt);

    // Only try to jump when on the ground
    if player.onGround && inputBuffer.consumeJump() {
        player.jump();
    }
}
```

This small addition makes games feel much more responsive. Players don't realize it's happening — they just feel like the game "gets" what they're trying to do.

### Coyote Time

A related technique is *coyote time* (named after cartoon coyotes who don't fall until they look down). It's the opposite of input buffering: instead of remembering inputs, you remember when the player was last grounded.

```rust
entity CoyoteTime {
    hide timeLeftGrounded: f64;
    hide coyoteDuration: f64 = 0.1;  // 100ms grace period
    hide wasGrounded: bool = false;

    func update(dt: f64, isGrounded: bool) {
        if isGrounded {
            self.timeLeftGrounded = self.coyoteDuration;
            self.wasGrounded = true;
        } else if self.wasGrounded {
            // Just left the ground, start counting
            self.timeLeftGrounded -= dt;
            if self.timeLeftGrounded <= 0 {
                self.wasGrounded = false;
            }
        }
    }

    func canJump() -> bool {
        return self.timeLeftGrounded > 0;
    }
}
```

Now the player can jump for a brief moment after walking off a ledge, which feels fair and responsive.

### Debouncing

Some inputs shouldn't repeat too quickly. *Debouncing* prevents rapid-fire activation:

```rust
entity Debouncer {
    hide cooldowns: Map<string, f64>;

    expose func init() {
        self.cooldowns = Map.new();
    }

    func update(dt: f64) {
        for action in self.cooldowns.keys() {
            var remaining = self.cooldowns.get(action);
            if remaining > 0 {
                self.cooldowns.set(action, remaining - dt);
            }
        }
    }

    func canActivate(action: string, cooldown: f64) -> bool {
        var remaining = self.cooldowns.get(action);
        if remaining == nil || remaining <= 0 {
            self.cooldowns.set(action, cooldown);
            return true;
        }
        return false;
    }
}
```

Use it for things like menu navigation:

```rust
var debouncer = Debouncer();

while inMenu {
    debouncer.update(dt);

    if Input.isKeyDown(Key.DOWN) && debouncer.canActivate("menuDown", 0.2) {
        selectedIndex += 1;
    }
    if Input.isKeyDown(Key.UP) && debouncer.canActivate("menuUp", 0.2) {
        selectedIndex -= 1;
    }
}
```

This lets the player hold the key to scroll through menu items at a reasonable pace, rather than instantly jumping to the end.

---

## A Complete Example: Controllable Character

Let's put everything together in a complete, playable example:

```rust
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
        // Calculate delta time
        var now = Viper.Time.millis();
        var dt = (now - lastTime) / 1000.0;
        lastTime = now;

        // --- INPUT ---
        // Horizontal movement (continuous - use isKeyDown)
        player.vx = 0.0;
        if Input.isKeyDown(Key.LEFT) || Input.isKeyDown(Key.A) {
            player.vx = -MOVE_SPEED;
        }
        if Input.isKeyDown(Key.RIGHT) || Input.isKeyDown(Key.D) {
            player.vx = MOVE_SPEED;
        }

        // Jump (one-time action - use wasKeyPressed)
        if Input.wasKeyPressed(Key.SPACE) && player.onGround {
            player.vy = JUMP_SPEED;
            player.onGround = false;
        }

        // --- PHYSICS ---
        // Apply gravity when in the air
        if !player.onGround {
            player.vy += GRAVITY * dt;
        }

        // Update position
        player.x += player.vx * dt;
        player.y += player.vy * dt;

        // Ground collision
        if player.y >= GROUND_Y {
            player.y = GROUND_Y;
            player.vy = 0.0;
            player.onGround = true;
        }

        // Keep player on screen (horizontal bounds)
        if player.x < 25 { player.x = 25; }
        if player.x > 775 { player.x = 775; }

        // --- RENDERING ---
        // Sky
        canvas.setColor(Color(100, 150, 255));
        canvas.fillRect(0, 0, 800, GROUND_Y + 50);

        // Ground
        canvas.setColor(Color(50, 150, 50));
        canvas.fillRect(0, GROUND_Y + 50, 800, 100);

        // Player (centered on position)
        canvas.setColor(Color.RED);
        canvas.fillRect(player.x - 25, player.y - 50, 50, 50);

        // Instructions
        canvas.setColor(Color.WHITE);
        canvas.setFont("Arial", 16);
        canvas.drawText(10, 25, "Arrow keys or A/D to move, Space to jump");

        // Debug info
        canvas.drawText(10, 50, "Position: (" + player.x + ", " + player.y + ")");
        canvas.drawText(10, 75, "On ground: " + player.onGround);

        canvas.show();
        Viper.Time.sleep(16);
    }
}
```

Let's trace what happens when you run this:

1. **Initialization**: We create a canvas, initialize the player at ground level, and record the current time.

2. **Main loop**: Each iteration represents one frame (~60 times per second).

3. **Delta time calculation**: We measure how much time passed since the last frame. This ensures consistent movement regardless of frame rate.

4. **Input handling**: We check keyboard state for movement (continuous) and key presses for jumping (one-time).

5. **Physics**: Gravity pulls the player down. Velocity updates position.

6. **Collision**: If the player falls below ground level, we snap them to the ground and reset vertical velocity.

7. **Rendering**: We draw everything in order (background, then foreground objects, then UI).

8. **Wait**: We sleep briefly to limit frame rate and avoid consuming 100% CPU.

---

## Common Mistakes

Learning from mistakes is efficient. Here are problems beginners often encounter with input handling.

### Mistake 1: Using isKeyDown for One-Time Actions

**Wrong:**
```rust
if Input.isKeyDown(Key.SPACE) {
    fireBullet();  // Fires 60 bullets per second!
}
```

**Right:**
```rust
if Input.wasKeyPressed(Key.SPACE) {
    fireBullet();  // Fires once per button press
}
```

When you hold the Space bar, `isKeyDown` returns `true` every frame. For actions that should happen once per press, use `wasKeyPressed`.

### Mistake 2: Forgetting to Handle Key Release

**Problem:**
```rust
if Input.wasKeyPressed(Key.SHIFT) {
    player.isRunning = true;
}
// Player runs forever after pressing shift once!
```

**Fix:**
```rust
player.isRunning = Input.isKeyDown(Key.SHIFT);
// Or:
if Input.wasKeyPressed(Key.SHIFT) {
    player.isRunning = true;
}
if Input.wasKeyReleased(Key.SHIFT) {
    player.isRunning = false;
}
```

For "hold to activate" mechanics, check the key state continuously, or handle both press and release.

### Mistake 3: Checking Input Outside the Game Loop

**Wrong:**
```rust
func checkJump() {
    // This might miss the key press!
    if Input.wasKeyPressed(Key.SPACE) {
        player.jump();
    }
}

// Called from some other part of the code, not every frame
```

**Right:**
```rust
// In main game loop, called every frame
while gameRunning {
    if Input.wasKeyPressed(Key.SPACE) {
        player.jump();
    }
    // ...
}
```

`wasKeyPressed` only returns `true` for one frame. If you don't check it during that frame, you miss the input.

### Mistake 4: Not Applying Dead Zones

**Wrong:**
```rust
var stickX = Input.controllerAxis(0, Axis.LEFT_X);
player.x += stickX * speed * dt;  // Character slowly drifts even with stick centered
```

**Right:**
```rust
var stickX = Input.controllerAxis(0, Axis.LEFT_X);
if Viper.Math.abs(stickX) < 0.15 {
    stickX = 0.0;  // Dead zone
}
player.x += stickX * speed * dt;
```

Physical analog sticks almost never rest at exactly (0, 0). Always apply a dead zone.

### Mistake 5: Hardcoding Controls

**Problematic:**
```rust
// Scattered throughout your code
if Input.isKeyDown(Key.W) { moveUp(); }
if Input.isKeyDown(Key.A) { moveLeft(); }
if Input.wasKeyPressed(Key.SPACE) { jump(); }
```

**Better:**
```rust
// Centralized input handling
entity InputManager {
    func getMoveDirection() -> Vec2 { ... }
    func isJumpPressed() -> bool { ... }
}

// Game code uses abstraction
player.move(input.getMoveDirection());
if input.isJumpPressed() { player.jump(); }
```

Centralizing input handling makes it easy to add controller support, rebindable keys, and different control schemes.

### Mistake 6: Checking Input for Unfocused Windows

When your game window isn't focused (the player clicked on another window), you might still receive input events in some situations, or the input state might be stale. Good practice:

```rust
while canvas.isOpen() {
    if !canvas.hasFocus() {
        // Window not focused, skip input processing
        // Maybe also pause the game
        Viper.Time.sleep(100);  // Don't burn CPU while unfocused
        continue;
    }

    // Normal input handling here
}
```

### Mistake 7: Not Clearing Input Between States

When transitioning between game states (menu to gameplay, for example), leftover input can cause problems:

```rust
func startGame() {
    // The player pressed Enter to start, but Enter might still register
    // as a "just pressed" key this frame
    if Input.wasKeyPressed(Key.ENTER) {
        // This triggers immediately, maybe pausing the game!
        togglePause();
    }
}
```

**Fix:**
```rust
func startGame() {
    Input.clearPressedKeys();  // Clear the "just pressed" flags
    // Now Enter won't trigger anything this frame
}
```

Or simply wait a frame before processing input in the new state.

---

## Debugging Input Problems

When input doesn't work as expected, here's how to find the problem.

### Print Input State

The simplest debugging technique — see what the input system is actually reporting:

```rust
// Add to your game loop temporarily
canvas.drawText(10, 50, "Space down: " + Input.isKeyDown(Key.SPACE));
canvas.drawText(10, 70, "Space pressed: " + Input.wasKeyPressed(Key.SPACE));
canvas.drawText(10, 90, "Mouse: " + Input.mouseX() + ", " + Input.mouseY());
```

If the display shows the input is detected but your game doesn't respond, the bug is in your game logic. If the display doesn't show the input, the problem is earlier in the pipeline.

### Check Event Order

Sometimes the order of operations matters:

```rust
// Bug: wasKeyPressed is checked after the action already happened
player.update();  // This might call Input functions internally
if Input.wasKeyPressed(Key.SPACE) {
    // This never triggers because wasKeyPressed was already
    // consumed (or cleared) during player.update()
}
```

Make sure input is checked before it's used anywhere else in the frame.

### Verify Focus

Is your game window actually focused?

```rust
canvas.drawText(10, 110, "Has focus: " + canvas.hasFocus());
```

Some input might not register if the window isn't focused.

### Test with Different Input Devices

If keyboard works but controller doesn't:
- Is the controller actually connected? (`Input.isControllerConnected(0)`)
- Is it the right controller number? (Maybe it's controller 1, not 0)
- Are you using the right button names? (Controllers vary in layout)

### Log Timing Issues

For input buffering and timing-sensitive code:

```rust
if Input.wasKeyPressed(Key.SPACE) {
    Viper.Terminal.Say("Jump pressed at time: " + Viper.Time.millis());
}

if player.onGround {
    Viper.Terminal.Say("On ground at time: " + Viper.Time.millis());
    if inputBuffer.consumeJump() {
        Viper.Terminal.Say("Jump executed!");
    }
}
```

This helps you see if inputs are arriving at the right times.

---

## The Three Languages

**ViperLang**
```rust
import Viper.Input;

// Keyboard
if Input.isKeyDown(Key.SPACE) {
    player.charging = true;
}
if Input.wasKeyPressed(Key.ESCAPE) {
    pauseGame();
}

// Mouse
var mx = Input.mouseX();
var my = Input.mouseY();
if Input.wasMousePressed(MouseButton.LEFT) {
    handleClick(mx, my);
}

// Controller
if Input.isControllerConnected(0) {
    var moveX = Input.controllerAxis(0, Axis.LEFT_X);
    player.x += moveX * speed * dt;
}
```

**BASIC**
```basic
' Keyboard
IF KEYDOWN(KEY_SPACE) THEN
    player.charging = TRUE
END IF
IF KEYPRESSED(KEY_ESCAPE) THEN
    CALL PauseGame()
END IF

' Mouse
DIM mx AS INTEGER, my AS INTEGER
mx = MOUSEX
my = MOUSEY
IF MOUSEPRESSED(BUTTON_LEFT) THEN
    CALL HandleClick(mx, my)
END IF

' Controller
IF CONTROLLERCONNECTED(0) THEN
    DIM moveX AS SINGLE
    moveX = CONTROLLERAXIS(0, AXIS_LEFT_X)
    player.x = player.x + moveX * speed * dt
END IF
```

**Pascal**
```pascal
uses ViperInput;

{ Keyboard }
if IsKeyDown(VK_SPACE) then
    player.charging := True;
if WasKeyPressed(VK_ESCAPE) then
    PauseGame;

{ Mouse }
var
    mx, my: Integer;
begin
    mx := MouseX;
    my := MouseY;
    if WasMousePressed(mbLeft) then
        HandleClick(mx, my);
end;

{ Controller }
if IsControllerConnected(0) then
begin
    moveX := ControllerAxis(0, axLeftX);
    player.x := player.x + moveX * speed * dt;
end;
```

---

## Summary

- **Input is communication** — it's how users talk to your program
- **The input pipeline** goes from physical device through OS to your code
- **Input queues** store events so nothing is lost
- **Key state** (`isKeyDown`) checks if a key is currently held
- **Key events** (`wasKeyPressed`, `wasKeyReleased`) detect transitions
- Use **state** for continuous actions, **events** for one-time actions
- **Mouse** provides position and button state
- **Controllers** have analog sticks (need dead zones) and digital buttons
- **Abstract input** to support multiple control schemes
- **Input buffering** and **coyote time** make games feel responsive
- **Debouncing** prevents unwanted rapid-fire activation
- **Common mistakes**: wrong function for the action type, missing key releases, no dead zones

---

## Exercises

**Exercise 20.1** (Mimic): Create a cursor that follows the mouse position. Draw a small crosshair or circle at the mouse coordinates.

**Exercise 20.2** (Extend): Modify the drawing program to support multiple colors. Use number keys 1-5 to select different colors.

**Exercise 20.3** (Create): Build a simple "avoid the obstacles" game where the player character follows the mouse and must avoid randomly moving rectangles. Display a score that increases over time, and end the game when the player touches an obstacle.

**Exercise 20.4** (Create): Create a color picker with three sliders for R, G, B values. Click and drag each slider to adjust. Display the resulting color in a preview box.

**Exercise 20.5** (Create): Implement a simple text input field. Show characters as the user types, handle backspace to delete, and Enter to submit. Display what was typed.

**Exercise 20.6** (Create): Create a simple drawing program with:
- Different brush sizes (number keys 1-5)
- Different colors (letter keys R, G, B, Y, W)
- Clear canvas (C key)
- Undo last stroke (Ctrl+Z) — this requires storing strokes, not just pixels

**Exercise 20.7** (Challenge): Create a two-player game on one keyboard. Player 1 uses WASD and Space. Player 2 uses arrow keys and Enter. Both players control separate characters that can move and jump. Add a simple competition element (race to a goal, collect coins, etc.).

**Exercise 20.8** (Challenge): Implement a complete input system with:
- Support for keyboard, mouse, and controller
- Rebindable keys (press a key to assign it to an action)
- Dead zone handling for controller sticks
- Input buffering for jump actions
- Save and load control bindings to a file

**Exercise 20.9** (Challenge): Create a rhythm game where notes scroll down the screen and the player must press the correct key (D, F, J, K) when notes reach a target line. Score based on timing accuracy. This requires precise input timing and visual feedback.

**Exercise 20.10** (Challenge): Build a simple virtual keyboard on screen. The player uses the mouse to click keys, and what they type appears in a text display. Support shift for uppercase letters.

---

*We can draw graphics and handle input. Now let's build a complete game from scratch, putting together everything we've learned.*

*[Continue to Chapter 21: Building a Game](21-game-project.md)*
