# Chapter 19: Graphics and Games

Text is powerful, but sometimes you need pictures. Games, visualizations, user interfaces — they all need graphics. This chapter introduces Viper's graphics system and the fundamental concepts of visual programming.

---

## The Canvas

Everything starts with a canvas — a rectangular area where you can draw:

```rust
import Viper.Graphics;

func start() {
    var canvas = Canvas(800, 600);  // 800 pixels wide, 600 tall
    canvas.setTitle("My First Window");

    // Draw something
    canvas.setColor(Color.RED);
    canvas.fillRect(100, 100, 200, 150);

    // Show the result
    canvas.show();

    // Keep window open until user closes it
    canvas.waitForClose();
}
```

This creates a window, draws a red rectangle, and waits for the user to close it.

---

## Coordinates

The canvas uses pixel coordinates:
- (0, 0) is the top-left corner
- X increases to the right
- Y increases downward (yes, down — this is standard for computer graphics)

```
(0,0) ────────────────────► X
  │
  │    Canvas
  │
  ▼
  Y
```

A point at (100, 50) is 100 pixels from the left edge and 50 pixels from the top.

---

## Colors

Colors are specified using RGB (red, green, blue) values from 0 to 255:

```rust
var red = Color(255, 0, 0);
var green = Color(0, 255, 0);
var blue = Color(0, 0, 255);
var white = Color(255, 255, 255);
var black = Color(0, 0, 0);
var purple = Color(128, 0, 255);
var gray = Color(128, 128, 128);
```

Common colors are predefined:

```rust
Color.RED
Color.GREEN
Color.BLUE
Color.WHITE
Color.BLACK
Color.YELLOW
Color.CYAN
Color.MAGENTA
```

---

## Drawing Shapes

### Rectangles
```rust
// Filled rectangle
canvas.setColor(Color.BLUE);
canvas.fillRect(x, y, width, height);

// Outline only
canvas.drawRect(x, y, width, height);
```

### Circles and Ellipses
```rust
// Filled circle
canvas.fillCircle(centerX, centerY, radius);

// Outline only
canvas.drawCircle(centerX, centerY, radius);

// Ellipse (oval)
canvas.fillEllipse(x, y, width, height);
```

### Lines
```rust
canvas.drawLine(x1, y1, x2, y2);
```

### Points
```rust
canvas.setPixel(x, y);
```

### Polygons
```rust
var points = [(100, 100), (150, 50), (200, 100)];  // Triangle
canvas.fillPolygon(points);
canvas.drawPolygon(points);
```

---

## Text

Drawing text on the canvas:

```rust
canvas.setColor(Color.BLACK);
canvas.setFont("Arial", 24);
canvas.drawText(100, 100, "Hello, Graphics!");
```

The coordinates specify where the text starts (left edge, baseline).

---

## A Simple Drawing Program

Let's put these together:

```rust
module DrawingDemo;

import Viper.Graphics;

func start() {
    var canvas = Canvas(640, 480);
    canvas.setTitle("Drawing Demo");

    // Background
    canvas.setColor(Color(135, 206, 235));  // Sky blue
    canvas.fillRect(0, 0, 640, 480);

    // Ground
    canvas.setColor(Color(34, 139, 34));  // Forest green
    canvas.fillRect(0, 350, 640, 130);

    // Sun
    canvas.setColor(Color.YELLOW);
    canvas.fillCircle(550, 80, 50);

    // House body
    canvas.setColor(Color(139, 69, 19));  // Brown
    canvas.fillRect(200, 250, 200, 150);

    // Roof
    canvas.setColor(Color(128, 0, 0));  // Dark red
    var roofPoints = [(180, 250), (300, 150), (420, 250)];
    canvas.fillPolygon(roofPoints);

    // Door
    canvas.setColor(Color(101, 67, 33));
    canvas.fillRect(270, 320, 60, 80);

    // Window
    canvas.setColor(Color(173, 216, 230));  // Light blue
    canvas.fillRect(320, 280, 50, 50);

    // Window frame
    canvas.setColor(Color.WHITE);
    canvas.drawRect(320, 280, 50, 50);
    canvas.drawLine(345, 280, 345, 330);
    canvas.drawLine(320, 305, 370, 305);

    // Text
    canvas.setColor(Color.WHITE);
    canvas.setFont("Arial", 20);
    canvas.drawText(250, 450, "Home Sweet Home");

    canvas.show();
    canvas.waitForClose();
}
```

---

## Animation: The Game Loop

Static pictures are nice, but games need animation. The key is the *game loop*:

```rust
while running {
    handleInput();   // Read keyboard/mouse
    updateState();   // Move things, check collisions
    render();        // Draw everything
    wait();          // Control frame rate
}
```

Here's a simple animation:

```rust
module BouncingBall;

import Viper.Graphics;

func start() {
    var canvas = Canvas(800, 600);
    canvas.setTitle("Bouncing Ball");

    var x = 400.0;
    var y = 300.0;
    var dx = 5.0;
    var dy = 3.0;
    var radius = 20.0;

    while canvas.isOpen() {
        // Clear screen
        canvas.setColor(Color.BLACK);
        canvas.fillRect(0, 0, 800, 600);

        // Update position
        x += dx;
        y += dy;

        // Bounce off walls
        if x - radius < 0 || x + radius > 800 {
            dx = -dx;
        }
        if y - radius < 0 || y + radius > 600 {
            dy = -dy;
        }

        // Draw ball
        canvas.setColor(Color.RED);
        canvas.fillCircle(x, y, radius);

        // Display and control frame rate
        canvas.show();
        Viper.Time.sleep(16);  // ~60 FPS
    }
}
```

The ball bounces around the screen, reversing direction when it hits walls.

---

## Frame Rate and Delta Time

Games should run at a consistent speed regardless of computer speed. Use *delta time*:

```rust
var lastTime = Viper.Time.millis();

while canvas.isOpen() {
    var now = Viper.Time.millis();
    var dt = (now - lastTime) / 1000.0;  // Seconds since last frame
    lastTime = now;

    // Update using dt
    x += speed * dt;  // Moves same distance regardless of frame rate

    render();
    canvas.show();
}
```

With delta time, `speed` is in "units per second" rather than "units per frame."

---

## Sprites and Images

Games often use pre-drawn images (sprites):

```rust
var playerSprite = Image.load("player.png");
var enemySprite = Image.load("enemy.png");

// In render loop:
canvas.drawImage(playerSprite, playerX, playerY);
canvas.drawImage(enemySprite, enemyX, enemyY);
```

You can scale and flip images:

```rust
canvas.drawImageScaled(sprite, x, y, width, height);
canvas.drawImageFlipped(sprite, x, y, flipX, flipY);
```

---

## Double Buffering

To avoid flickering, draw to a back buffer and flip:

```rust
var canvas = Canvas(800, 600, { doubleBuffered: true });

while running {
    canvas.clear();       // Clear back buffer
    drawEverything();     // Draw to back buffer
    canvas.flip();        // Swap buffers — display the result
}
```

Most canvas implementations handle this automatically.

---

## A Complete Example: Simple Game Framework

Here's a framework you can use for games:

```rust
module GameFramework;

import Viper.Graphics;

value Vec2 {
    x: f64;
    y: f64;
}

entity GameObject {
    position: Vec2;
    size: Vec2;
    color: Color;

    expose func init(x: f64, y: f64, w: f64, h: f64, color: Color) {
        self.position = Vec2 { x: x, y: y };
        self.size = Vec2 { x: w, y: h };
        self.color = color;
    }

    func update(dt: f64) {
        // Override in subclasses
    }

    func draw(canvas: Canvas) {
        canvas.setColor(self.color);
        canvas.fillRect(self.position.x, self.position.y,
                       self.size.x, self.size.y);
    }

    func collidesWith(other: GameObject) -> bool {
        return self.position.x < other.position.x + other.size.x &&
               self.position.x + self.size.x > other.position.x &&
               self.position.y < other.position.y + other.size.y &&
               self.position.y + self.size.y > other.position.y;
    }
}

entity Game {
    canvas: Canvas;
    objects: [GameObject];
    running: bool;
    lastTime: i64;

    expose func init(width: i64, height: i64, title: string) {
        self.canvas = Canvas(width, height);
        self.canvas.setTitle(title);
        self.objects = [];
        self.running = true;
        self.lastTime = Viper.Time.millis();
    }

    func add(obj: GameObject) {
        self.objects.push(obj);
    }

    func run() {
        while self.running && self.canvas.isOpen() {
            var now = Viper.Time.millis();
            var dt = (now - self.lastTime) / 1000.0;
            self.lastTime = now;

            self.handleInput();
            self.update(dt);
            self.render();

            Viper.Time.sleep(16);
        }
    }

    func handleInput() {
        // Override for game-specific input
    }

    func update(dt: f64) {
        for obj in self.objects {
            obj.update(dt);
        }
    }

    func render() {
        self.canvas.setColor(Color.BLACK);
        self.canvas.fillRect(0, 0, 800, 600);

        for obj in self.objects {
            obj.draw(self.canvas);
        }

        self.canvas.show();
    }
}

// Usage example
func start() {
    var game = Game(800, 600, "My Game");

    game.add(GameObject(100, 100, 50, 50, Color.RED));
    game.add(GameObject(300, 200, 30, 30, Color.BLUE));

    game.run();
}
```

This provides a foundation to build upon.

---

## The Three Languages

**ViperLang**
```rust
import Viper.Graphics;

var canvas = Canvas(800, 600);
canvas.setColor(Color.RED);
canvas.fillRect(100, 100, 200, 150);
canvas.show();
```

**BASIC**
```basic
SCREEN 800, 600
COLOR RED
FILL RECT 100, 100, 200, 150
REFRESH
```

**Pascal**
```pascal
uses ViperGraphics;
var canvas: TCanvas;
begin
    canvas := TCanvas.Create(800, 600);
    canvas.SetColor(clRed);
    canvas.FillRect(100, 100, 200, 150);
    canvas.Show;
end.
```

---

## Summary

- The *canvas* is your drawing surface with pixel coordinates
- (0,0) is top-left; Y increases downward
- Colors use RGB values (0-255)
- Draw shapes with `fillRect`, `fillCircle`, `drawLine`, etc.
- Animation uses a game loop: input → update → render
- Use *delta time* for consistent speed across machines
- *Sprites* are pre-made images for characters and objects
- *Double buffering* prevents flicker

---

## Exercises

**Exercise 19.1**: Draw a smiley face using circles for the head and eyes, and an arc for the mouth.

**Exercise 19.2**: Create an animation of a square moving across the screen and wrapping around when it exits.

**Exercise 19.3**: Draw a starfield — many random white dots on a black background.

**Exercise 19.4**: Animate a simple sunset: the sun slowly descends, the sky color gradually changes from blue to orange to dark.

**Exercise 19.5**: Create a simple "screensaver" with multiple bouncing balls of different colors.

**Exercise 19.6** (Challenge): Draw a simple bar chart from an array of numbers.

---

*We can draw graphics. But games need input — knowing when the player presses keys or moves the mouse. Next, we learn about handling user input.*

*[Continue to Chapter 20: User Input →](20-input.md)*
