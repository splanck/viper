# Chapter 19: Graphics and Games

Close your eyes and imagine the first video game you ever played. Maybe it was a plumber jumping between platforms. Maybe it was a yellow circle eating dots in a maze. Maybe it was blocks falling from the sky that you had to arrange into rows. Whatever it was, you probably remember not just playing it, but *feeling* something — excitement, challenge, the satisfaction of mastering a new skill.

Now imagine creating that experience for someone else.

That's what this chapter is about. We're going to learn how to make pictures appear on screen, how to animate them, and how to build the foundation for interactive visual programs. By the end, you'll understand how every game, every animation, every visual interface you've ever used actually works under the hood.

But this chapter isn't just about games. Graphics programming opens doors everywhere: data visualization that makes complex information understandable at a glance, user interfaces that feel intuitive and responsive, simulations that model real-world phenomena, educational tools that make learning visual and interactive. The skills you learn here transfer to all of these domains.

Let's start painting.

---

## How Computer Graphics Work: The Mental Model

Before we write any code, let's understand what we're actually doing when we "draw" on a computer screen.

### Pixels: The Atoms of Digital Images

Look closely at your screen — really closely. If you could zoom in far enough, you'd see that everything you're looking at is made of tiny squares of light called *pixels* (short for "picture elements"). Your screen might have over two million of these tiny squares, each one capable of displaying a single color at any moment.

Think of your screen like a giant mosaic. A mosaic artist creates images by placing individual colored tiles — they can't paint smooth curves or gradients directly. Instead, they arrange many small, solid-colored squares in patterns that *appear* to form shapes and images when viewed from a distance. Computer graphics work exactly the same way.

When we "draw a circle" on screen, we're not actually creating a perfect mathematical circle. We're choosing which pixels to light up and what color to make each one, in a pattern that looks like a circle to human eyes. The more pixels we have, the smoother curves appear — but at the fundamental level, it's always a mosaic of tiny squares.

```
What a "circle" really looks like at the pixel level:

      # # #
    # # # # #
  # #       # #
  #           #
  #           #
  #           #
  # #       # #
    # # # # #
      # # #
```

### The Frame Buffer: The Canvas in Memory

Your graphics card maintains a region of memory called the *frame buffer*. This is essentially a giant array where each element represents one pixel on screen. When you draw something, you're really just changing values in this array.

The frame buffer is like a painter's canvas, but with an important difference: you can erase and redraw it many times per second. This is how animation works — by rapidly updating the frame buffer with slightly different images, we create the illusion of movement, just like how a flipbook creates animation from a stack of drawings.

```
Frame buffer in memory (simplified):

Address     Color Value
[0000]  ->  (255, 0, 0)     <- Pixel at (0,0) is red
[0001]  ->  (255, 0, 0)     <- Pixel at (1,0) is red
[0002]  ->  (0, 0, 255)     <- Pixel at (2,0) is blue
...
[0800]  ->  (0, 255, 0)     <- Pixel at (0,1) is green (new row)
...
```

### Refresh Rates: The Flipbook Speed

Your monitor doesn't display a static image — it redraws the entire screen many times per second. This rate is measured in Hertz (Hz). A 60Hz monitor redraws 60 times per second, a 144Hz gaming monitor redraws 144 times per second.

This is like a flipbook: the faster you flip the pages, the smoother the animation appears. At 60 flips (frames) per second, motion looks smooth to the human eye. Below about 24 frames per second, we start perceiving individual images rather than fluid motion — that's why old movies sometimes look "choppy."

When we write graphics programs, we need to be mindful of this refresh cycle. Our code needs to update the frame buffer fast enough to keep up with the monitor's refresh rate, or our animations will stutter.

---

## The Coordinate System: Finding Your Way

Every drawing system needs a way to specify *where* things go. We use a coordinate system — two numbers (x, y) that identify each pixel on the canvas.

### The Origin and Axes

In computer graphics, we have a convention that might surprise you if you remember graphing in math class:

- **The origin (0, 0) is at the top-left corner**, not the bottom-left
- **X increases to the right** (same as in math)
- **Y increases downward** (opposite of math!)

```
(0,0) ─────────────────────────────────► X (increases right)
  │
  │
  │        Your Canvas
  │
  │
  │
  ▼
  Y (increases down)
```

Why is Y flipped? Historical reasons. Early computers displayed text from top to bottom, line by line. The first line was at y=0, the second at y=1, and so on. When graphics capabilities were added, this convention stuck. Every major graphics system uses it, so you'll encounter it everywhere.

### Understanding Positions

A point at (100, 50) means:
- 100 pixels from the left edge
- 50 pixels from the top edge

Let's visualize this:

```
(0,0)
  ┌──────────────────────────────────────┐
  │                                      │
  │     100 pixels right                 │
  │  ─────────────►                      │
  │  │             ● (100, 50)           │
  │  │ 50 pixels                         │
  │  │ down                              │
  │  ▼                                   │
  │                                      │
  │                                      │
  └──────────────────────────────────────┘
```

This means the top of your canvas has *smaller* Y values than the bottom. A common source of confusion is expecting a larger Y to mean "higher up" — in graphics, it means the opposite.

### Canvas Size and Boundaries

When you create a canvas of 800 pixels wide by 600 pixels tall:
- Valid X coordinates range from 0 to 799 (that's 800 pixels: 0, 1, 2, ... 799)
- Valid Y coordinates range from 0 to 599 (that's 600 pixels: 0, 1, 2, ... 599)

Notice that the maximum valid coordinate is always *one less* than the dimension. This is because we start counting at 0, not 1. If your canvas is 800 pixels wide, the rightmost column of pixels is at x=799, not x=800. Attempting to draw at x=800 would be drawing off the edge of the canvas.

---

## Colors: The RGB Model

Every pixel needs a color. Computers represent colors using the RGB model — mixing Red, Green, and Blue light in different intensities.

### How RGB Works

Think of three spotlights: one red, one green, one blue. By adjusting the brightness of each spotlight, you can create any color visible on a screen.

Each color channel has 256 possible intensities, from 0 (off) to 255 (full brightness):
- Red: 0 to 255
- Green: 0 to 255
- Blue: 0 to 255

This gives us 256 * 256 * 256 = 16,777,216 possible colors. That's over 16 million colors — far more than the human eye can distinguish.

### Building Colors

```rust
// Primary colors: one channel at maximum, others off
var red = Color(255, 0, 0);      // Full red, no green, no blue
var green = Color(0, 255, 0);    // No red, full green, no blue
var blue = Color(0, 0, 255);     // No red, no green, full blue

// Secondary colors: mixing two primaries
var yellow = Color(255, 255, 0);   // Red + Green = Yellow
var cyan = Color(0, 255, 255);     // Green + Blue = Cyan
var magenta = Color(255, 0, 255);  // Red + Blue = Magenta

// Neutrals: all channels equal
var white = Color(255, 255, 255);  // All at maximum
var black = Color(0, 0, 0);        // All off
var gray = Color(128, 128, 128);   // All at half

// Custom colors
var orange = Color(255, 165, 0);   // Lots of red, some green
var purple = Color(128, 0, 255);   // Some red, full blue
var pink = Color(255, 192, 203);   // High red, medium green and blue
var brown = Color(139, 69, 19);    // More red than green, less blue
```

### The Color Mixing Intuition

If you've mixed paint before, this might seem backwards. With paint (subtractive color), mixing red and green gives you a muddy brown. With light (additive color), mixing red and green gives you yellow.

Here's the intuition: paint works by *absorbing* light — red paint absorbs everything except red. Light works by *adding* — red light plus green light is more light than either alone.

Think of it this way:
- **Black is the absence of light** — all channels at 0
- **White is all colors of light combined** — all channels at 255
- **Adding colors makes things brighter** — higher numbers mean more light

### Using Predefined Colors

The graphics library provides common colors so you don't have to remember RGB values:

```rust
Color.RED       // (255, 0, 0)
Color.GREEN     // (0, 255, 0) — note: this is pure green, not forest green
Color.BLUE      // (0, 0, 255)
Color.WHITE     // (255, 255, 255)
Color.BLACK     // (0, 0, 0)
Color.YELLOW    // (255, 255, 0)
Color.CYAN      // (0, 255, 255)
Color.MAGENTA   // (255, 0, 255)
```

---

## Your First Canvas

Now that we understand the concepts, let's write code. Everything starts with creating a canvas — a window where we can draw:

```rust
bind Viper.Graphics;

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

Let's trace through what happens when this program runs:

1. **`Canvas(800, 600)`** — Create a window 800 pixels wide and 600 pixels tall. At this point, the window exists but nothing is drawn on it yet. The frame buffer is allocated in memory, initially filled with black (or whatever the default background is).

2. **`canvas.setTitle("My First Window")`** — Set the text that appears in the window's title bar. This doesn't affect the drawing area.

3. **`canvas.setColor(Color.RED)`** — Set the "current drawing color" to red. This is like dipping your paintbrush in red paint. It doesn't draw anything yet — it just prepares for subsequent drawing operations.

4. **`canvas.fillRect(100, 100, 200, 150)`** — Draw a filled rectangle. The parameters mean:
   - Start at x=100, y=100 (the top-left corner of the rectangle)
   - Width of 200 pixels
   - Height of 150 pixels

   After this call, the frame buffer contains a red rectangle, but the screen hasn't been updated yet.

5. **`canvas.show()`** — Copy the frame buffer to the screen. Now you can see the rectangle!

6. **`canvas.waitForClose()`** — Pause the program and wait until the user closes the window. Without this, the program would end immediately and the window would disappear.

### Visualizing the Rectangle

Here's what we drew:

```
(0,0)
  ┌──────────────────────────────────────────────────┐
  │                                                  │
  │     (100,100)                                    │
  │        ┌──────────────────────┐                  │
  │        │                      │                  │
  │        │   RED RECTANGLE      │ 150 pixels      │
  │        │   (filled)           │ high             │
  │        │                      │                  │
  │        └──────────────────────┘                  │
  │              200 pixels wide          (100+200=300, 100+150=250)
  │                                                  │
  │                                                  │
  └──────────────────────────────────────────────────┘
                                                (799, 599)
```

The rectangle's corners are at:
- Top-left: (100, 100)
- Top-right: (299, 100) — that's 100 + 200 - 1
- Bottom-left: (100, 249) — that's 100 + 150 - 1
- Bottom-right: (299, 249)

---

## Drawing Primitives: The Building Blocks

Graphics libraries provide basic shapes called *primitives*. Everything more complex is built from these fundamentals.

### Rectangles

Rectangles are the workhorse of graphics — fast to draw and useful for backgrounds, UI elements, platforms, and countless other purposes.

```rust
// Filled rectangle (solid color)
canvas.setColor(Color.BLUE);
canvas.fillRect(x, y, width, height);

// Outline only (just the border)
canvas.setColor(Color.WHITE);
canvas.drawRect(x, y, width, height);
```

The difference between `fillRect` and `drawRect`:

```
fillRect(50, 50, 100, 80)         drawRect(50, 50, 100, 80)
  ┌───────────────┐                  ┌───────────────┐
  │███████████████│                  │               │
  │███████████████│                  │               │
  │███████████████│                  │               │
  │███████████████│                  │               │
  └───────────────┘                  └───────────────┘
   (filled inside)                    (outline only)
```

### Circles and Ellipses

```rust
// Circle: specify center and radius
canvas.setColor(Color.YELLOW);
canvas.fillCircle(centerX, centerY, radius);
canvas.drawCircle(centerX, centerY, radius);

// Ellipse (oval): specify bounding box
canvas.fillEllipse(x, y, width, height);
canvas.drawEllipse(x, y, width, height);
```

For circles, you specify where the *center* is and how big the radius is. For ellipses, you specify a bounding rectangle, and the ellipse fills it.

```
fillCircle(200, 150, 50)          fillEllipse(100, 100, 120, 60)

         ●●●●●●                        ●●●●●●●●●●●●●●
       ●●●●●●●●●●                    ●●●●●●●●●●●●●●●●●●
      ●●●●●●●●●●●●                  ●●●●●●●●●●●●●●●●●●●●
      ●●●●●●●●●●●●                   ●●●●●●●●●●●●●●●●●●
       ●●●●●●●●●●                      ●●●●●●●●●●●●●●
         ●●●●●●                           ●●●●●●●●

    center: (200, 150)              bounds: (100,100) to (220,160)
    radius: 50                      width: 120, height: 60
```

### Lines

```rust
// Draw a line from point 1 to point 2
canvas.setColor(Color.GREEN);
canvas.drawLine(x1, y1, x2, y2);
```

Lines connect two points. There's no "filled" version — a line is inherently just a path.

```
drawLine(50, 50, 200, 150)

  (50,50)
     \
      \
       \
        \
         \
          \
           (200,150)
```

### Individual Pixels

Sometimes you need to color a single pixel:

```rust
canvas.setColor(Color.WHITE);
canvas.setPixel(x, y);
```

This is the most fundamental drawing operation — everything else is built from it. Drawing a line is really just setting many pixels in a row; drawing a rectangle is setting pixels in a grid pattern.

### Polygons

For arbitrary shapes, define a series of points and connect them:

```rust
// Triangle
var triangle = [(100, 200), (150, 100), (200, 200)];
canvas.setColor(Color.RED);
canvas.fillPolygon(triangle);
canvas.drawPolygon(triangle);  // Outline version

// Pentagon
var pentagon = [
    (200, 100),
    (250, 140),
    (230, 200),
    (170, 200),
    (150, 140)
];
canvas.fillPolygon(pentagon);
```

The points define the vertices (corners) of the shape. The graphics system connects them in order, and the last point connects back to the first.

```
Triangle from [(100,200), (150,100), (200,200)]:

         (150,100)
            /\
           /  \
          /    \
         /      \
        /________\
   (100,200)    (200,200)
```

---

## Drawing Text

Text is rendered as graphics too — each character is drawn as a pattern of pixels in the font's defined shape.

```rust
canvas.setColor(Color.BLACK);
canvas.setFont("Arial", 24);  // Font name and size in pixels
canvas.drawText(100, 100, "Hello, Graphics!");
```

The coordinates (100, 100) specify where the text starts — specifically, the left edge at the text's *baseline*. The baseline is the line that letters sit on (think of the bottom of letters like 'a' or 'x', but letters like 'g' and 'y' extend below it).

```
canvas.drawText(100, 100, "Hello, y")

         ___         ___  ___
        |   |       |   ||   |
        |___|  ___  |   ||   |  ___       __
        |   | /   \ |   ||   | /   \     |  |
        |   ||  ___||   ||   ||  _  |    |  |
        |   ||   __||   ||   || / \ | __ |__|
        |   | \___/ |___||___| \___/  \/  __
                                         |  |
(100,100) ─────────────────────────────────│───── baseline
                                         \/
                                    (descender below baseline)
```

---

## A Complete Drawing: Step by Step

Let's draw a simple scene — a house with the sun — and trace through every step:

```rust
module DrawingDemo;

bind Viper.Graphics;

func start() {
    var canvas = Canvas(640, 480);
    canvas.setTitle("Drawing Demo");

    // Step 1: Sky background
    canvas.setColor(Color(135, 206, 235));  // Sky blue
    canvas.fillRect(0, 0, 640, 480);

    // Step 2: Ground
    canvas.setColor(Color(34, 139, 34));  // Forest green
    canvas.fillRect(0, 350, 640, 130);

    // Step 3: Sun
    canvas.setColor(Color.YELLOW);
    canvas.fillCircle(550, 80, 50);

    // Step 4: House body
    canvas.setColor(Color(139, 69, 19));  // Brown
    canvas.fillRect(200, 250, 200, 150);

    // Step 5: Roof
    canvas.setColor(Color(128, 0, 0));  // Dark red
    var roofPoints = [(180, 250), (300, 150), (420, 250)];
    canvas.fillPolygon(roofPoints);

    // Step 6: Door
    canvas.setColor(Color(101, 67, 33));  // Dark brown
    canvas.fillRect(270, 320, 60, 80);

    // Step 7: Window
    canvas.setColor(Color(173, 216, 230));  // Light blue
    canvas.fillRect(320, 280, 50, 50);

    // Step 8: Window frame
    canvas.setColor(Color.WHITE);
    canvas.drawRect(320, 280, 50, 50);
    canvas.drawLine(345, 280, 345, 330);
    canvas.drawLine(320, 305, 370, 305);

    // Step 9: Label
    canvas.setColor(Color.WHITE);
    canvas.setFont("Arial", 20);
    canvas.drawText(250, 450, "Home Sweet Home");

    canvas.show();
    canvas.waitForClose();
}
```

### Tracing the Drawing Order

The order we draw things matters enormously. Later drawings cover earlier ones. Let's trace through:

**Step 1: Sky background**
We fill the entire canvas with sky blue. This is our base layer — everything else will be drawn on top of it.

**Step 2: Ground**
We draw green over the bottom portion of the sky. The green covers the sky blue in that region.

**Step 3: Sun**
The yellow circle is drawn on top of the sky. Since it's in the upper right (center at 550, 80), it doesn't overlap the ground.

**Step 4: House body**
The brown rectangle covers part of the ground and sky. Notice we're building up layers.

**Step 5: Roof**
The dark red triangle is drawn on top of the house body and sky. It overlaps the top edge of the house.

**Step 6: Door**
The darker brown door is drawn on top of the house body.

**Step 7 & 8: Window**
First the light blue window glass, then white lines for the frame. The frame is drawn last so it appears on top of the glass.

**Step 9: Label**
Text at the bottom, on top of the ground.

Think of it like painting: you paint the background first, then things farther away, then things closer to the viewer. Each layer can cover parts of previous layers.

---

## Animation: Making Things Move

A static picture is nice, but games need movement. Animation is really just an illusion — we show a series of slightly different images so quickly that the brain perceives motion.

Remember flipbooks? Each page has a drawing that's slightly different from the last. Flip through quickly, and the drawings appear to move. Computer animation works identically, but instead of physical pages, we redraw the screen many times per second.

### The Game Loop

At the heart of every game and animation is the *game loop* — a continuous cycle of three steps:

```
┌──────────────────────────────────────────────────────────────┐
│                                                              │
│    ┌───────────┐     ┌────────────┐     ┌────────────┐      │
│    │  HANDLE   │     │   UPDATE   │     │   RENDER   │      │
│    │   INPUT   │────►│   STATE    │────►│  (DRAW)    │      │
│    └───────────┘     └────────────┘     └────────────┘      │
│         ▲                                      │             │
│         │                                      │             │
│         └──────────────────────────────────────┘             │
│                                                              │
│                    Repeat Forever                            │
│                 (or until player quits)                      │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

**1. Handle Input**: Check what the player is doing — are any keys pressed? Is the mouse moving?

**2. Update State**: Move things, check collisions, apply physics, update scores. This is where the game logic lives.

**3. Render**: Draw everything in its new position to the screen.

Then repeat, dozens of times per second.

### The Loop in Code

```rust
while running {
    handleInput();   // Read keyboard/mouse
    updateState();   // Move things, check collisions
    render();        // Draw everything
    wait();          // Control frame rate
}
```

The `wait()` at the end is important. Without it, the loop would run as fast as possible — potentially thousands of times per second. But your monitor only updates 60 times per second, so anything faster is wasted work (and drains battery/runs your CPU hot). We typically wait about 16 milliseconds between frames, giving us approximately 60 frames per second.

### A Bouncing Ball Example

Let's see the game loop in action:

```rust
module BouncingBall;

bind Viper.Graphics;
bind Viper.Time;

func start() {
    var canvas = Canvas(800, 600);
    canvas.setTitle("Bouncing Ball");

    // Ball state: position and velocity
    var x = 400.0;     // Start in center
    var y = 300.0;
    var dx = 5.0;      // Moving right 5 pixels per frame
    var dy = 3.0;      // Moving down 3 pixels per frame
    var radius = 20.0;

    // Game loop
    while canvas.isOpen() {
        // === UPDATE STATE ===

        // Move the ball
        x += dx;
        y += dy;

        // Bounce off left and right walls
        if x - radius < 0 {      // Hit left wall
            x = radius;          // Push back inside
            dx = -dx;            // Reverse horizontal direction
        }
        if x + radius > 800 {    // Hit right wall
            x = 800 - radius;    // Push back inside
            dx = -dx;            // Reverse horizontal direction
        }

        // Bounce off top and bottom walls
        if y - radius < 0 {      // Hit top wall
            y = radius;
            dy = -dy;
        }
        if y + radius > 600 {    // Hit bottom wall
            y = 600 - radius;
            dy = -dy;
        }

        // === RENDER ===

        // Clear screen (draw black background)
        canvas.setColor(Color.BLACK);
        canvas.fillRect(0, 0, 800, 600);

        // Draw ball at new position
        canvas.setColor(Color.RED);
        canvas.fillCircle(x, y, radius);

        // Show the result
        canvas.show();

        // === WAIT ===
        sleep(16);  // ~60 FPS (1000ms / 60 = ~16ms)
    }
}
```

### Tracing Through Several Frames

Let's trace the first few frames:

**Frame 1:**
- x = 400, y = 300 (initial position)
- Update: x = 405, y = 303 (add velocity)
- No wall collisions
- Render: Ball drawn at (405, 303)

**Frame 2:**
- x = 405, y = 303
- Update: x = 410, y = 306
- No wall collisions
- Render: Ball drawn at (410, 306)

**Frame 3:**
- x = 410, y = 306
- Update: x = 415, y = 309
- No wall collisions
- Render: Ball drawn at (415, 309)

The ball moves 5 pixels right and 3 pixels down each frame. Since we're running at 60 FPS, it travels 300 pixels right and 180 pixels down per second.

**Many frames later, when x reaches 780:**
- Update: x = 785, y = something
- Check: x + radius (785 + 20 = 805) > 800? Yes!
- Bounce: x = 800 - 20 = 780, dx = -5 (reversed)
- Now moving left instead of right

### Why Clear the Screen Each Frame?

Notice we fill the entire screen with black at the start of each frame. Why?

If we didn't clear, the previous frame's drawing would still be there. The ball would leave a trail of red circles across the screen:

```
Without clearing:           With clearing:

Frame 1:   ●                Frame 1:   ●
Frame 2:   ●●               Frame 2:    ●
Frame 3:   ●●●              Frame 3:     ●
Frame 4:   ●●●●             Frame 4:      ●
          (trail)                   (no trail)
```

Clearing the screen is like erasing a whiteboard before drawing a new picture. We redraw *everything* each frame, in its new position.

---

## Smooth Motion: Frame Rate and Delta Time

Our bouncing ball has a problem: it moves a fixed number of pixels per frame. If the computer runs at 60 FPS, it moves 300 pixels per second. But if the computer is slow and only manages 30 FPS, it moves only 150 pixels per second. The game runs in slow motion!

The solution is *delta time* — the time elapsed since the last frame.

### The Problem

```
60 FPS computer:
  Frame 1: x = 100
  Frame 2: x = 105  (moved 5)
  Frame 3: x = 110  (moved 5)
  ...after 60 frames (1 second): x = 400 (moved 300)

30 FPS computer:
  Frame 1: x = 100
  Frame 2: x = 105  (moved 5)
  Frame 3: x = 110  (moved 5)
  ...after 30 frames (1 second): x = 250 (moved 150)
```

Same code, different results!

### The Solution: Delta Time

Instead of moving a fixed amount per frame, we move based on how much time has passed:

```rust
bind Viper.Time;

var lastTime = millis();

while canvas.isOpen() {
    // Calculate delta time
    var now = millis();
    var dt = (now - lastTime) / 1000.0;  // Convert to seconds
    lastTime = now;

    // Update using dt
    x += speed * dt;  // speed is now "pixels per second"

    render();
    canvas.show();
}
```

Now `speed` means "pixels per second" rather than "pixels per frame":

```
60 FPS computer:
  dt = 1/60 = 0.0167 seconds per frame
  Movement per frame = 300 * 0.0167 = 5 pixels
  After 1 second (60 frames): moved 300 pixels

30 FPS computer:
  dt = 1/30 = 0.0333 seconds per frame
  Movement per frame = 300 * 0.0333 = 10 pixels
  After 1 second (30 frames): moved 300 pixels
```

Same distance covered in the same real-world time, regardless of frame rate!

### Delta Time in Practice

```rust
bind Viper.Time;

var x = 400.0;
var y = 300.0;
var speedX = 200.0;  // 200 pixels per second
var speedY = 150.0;  // 150 pixels per second

var lastTime = millis();

while canvas.isOpen() {
    var now = millis();
    var dt = (now - lastTime) / 1000.0;
    lastTime = now;

    // Movement is consistent regardless of frame rate
    x += speedX * dt;
    y += speedY * dt;

    // ... collision checks using the same dt logic ...

    // Render
    canvas.setColor(Color.BLACK);
    canvas.fillRect(0, 0, 800, 600);
    canvas.setColor(Color.RED);
    canvas.fillCircle(x, y, 20);
    canvas.show();

    sleep(16);
}
```

---

## Double Buffering: Eliminating Flicker

You might notice that naive graphics code can produce flickering — momentary visual glitches where the screen appears to flash. This happens when the monitor reads from the frame buffer while we're in the middle of drawing.

Imagine someone watching you paint. If they look while you're half-done erasing the old picture and drawing the new one, they see a mess. The solution is *double buffering* — using two canvases.

### How Double Buffering Works

```
┌─────────────────┐      ┌─────────────────┐
│  BACK BUFFER    │      │  FRONT BUFFER   │
│                 │      │                 │
│  (We draw here) │      │  (Monitor shows │
│                 │      │   this one)     │
└─────────────────┘      └─────────────────┘
        │                         │
        │                         │
        ▼                         ▼
    Drawing                  On Screen
    happens here             displayed here
```

1. While the monitor displays the *front buffer*, we draw to the hidden *back buffer*
2. When our drawing is complete, we *swap* the buffers instantly
3. The old back buffer becomes the new front buffer (displayed)
4. The old front buffer becomes the new back buffer (we draw on it next)

The swap is nearly instantaneous, so the monitor never sees a half-finished drawing.

### Using Double Buffering

```rust
var canvas = Canvas(800, 600, { doubleBuffered: true });

while running {
    canvas.clear();       // Clear the back buffer
    drawEverything();     // Draw to back buffer
    canvas.flip();        // Swap buffers - back becomes front
}
```

Most modern graphics systems handle double buffering automatically. The `canvas.show()` function in our examples typically does this swap for you. But understanding the concept helps you debug visual glitches if they occur.

---

## Sprites and Images

While drawing shapes works for simple graphics, most games use pre-made images called *sprites*. A sprite is just an image file (PNG, JPG, etc.) loaded into memory.

```rust
var playerSprite = Image.load("player.png");
var enemySprite = Image.load("enemy.png");
var backgroundSprite = Image.load("background.png");

// In the render loop:
canvas.drawImage(backgroundSprite, 0, 0);
canvas.drawImage(playerSprite, playerX, playerY);
canvas.drawImage(enemySprite, enemyX, enemyY);
```

### Sprite Operations

```rust
// Basic drawing
canvas.drawImage(sprite, x, y);

// Scaled (stretched or shrunk)
canvas.drawImageScaled(sprite, x, y, width, height);

// Flipped (for facing different directions)
canvas.drawImageFlipped(sprite, x, y, flipX, flipY);
```

A common technique: create a sprite facing right, then flip it horizontally when the character needs to face left. This saves you from needing separate images for each direction.

### Transparency

PNG images can have transparent areas. This lets you draw a character that isn't a rectangle:

```
Without transparency:          With transparency:

┌─────────────────┐
│█████████████████│            █████████████████
│███████████ █████│            ███████████ █████
│██████████ ██████│            ██████████ ██████
│█████████████████│            █████████████████
│█████████████████│            █████████████████
│███████ █████████│            ███████ █████████
└─────────────────┘

(White box around              (Only the character
 the character)                 is visible)
```

The areas that should be "see-through" are stored with zero opacity (alpha = 0) in the PNG file.

---

## Building a Game Framework

Let's put everything together into a reusable structure:

```rust
module GameFramework;

bind Viper.Graphics;
bind Viper.Time;

value Vec2 {
    x: Number;
    y: Number;
}

entity GameObject {
    position: Vec2;
    size: Vec2;
    color: Color;

    expose func init(x: Number, y: Number, w: Number, h: Number, c: Color) {
        self.position = Vec2 { x: x, y: y };
        self.size = Vec2 { x: w, y: h };
        self.color = c;
    }

    func update(dt: Number) {
        // Override in child entities for custom behavior
    }

    func draw(canvas: Canvas) {
        canvas.setColor(self.color);
        canvas.fillRect(self.position.x, self.position.y,
                       self.size.x, self.size.y);
    }

    func collidesWith(other: GameObject) -> Boolean {
        // Axis-Aligned Bounding Box (AABB) collision
        return self.position.x < other.position.x + other.size.x &&
               self.position.x + self.size.x > other.position.x &&
               self.position.y < other.position.y + other.size.y &&
               self.position.y + self.size.y > other.position.y;
    }
}

entity Game {
    canvas: Canvas;
    objects: [GameObject];
    running: Boolean;
    lastTime: Integer;

    expose func init(width: Integer, height: Integer, title: String) {
        self.canvas = Canvas(width, height);
        self.canvas.setTitle(title);
        self.objects = [];
        self.running = true;
        self.lastTime = millis();
    }

    func add(obj: GameObject) {
        self.objects.push(obj);
    }

    func run() {
        while self.running && self.canvas.isOpen() {
            // Calculate delta time
            var now = millis();
            var dt = (now - self.lastTime) / 1000.0;
            self.lastTime = now;

            // Game loop phases
            self.handleInput();
            self.update(dt);
            self.render();

            sleep(16);
        }
    }

    func handleInput() {
        // Override for game-specific input handling
    }

    func update(dt: Number) {
        for obj in self.objects {
            obj.update(dt);
        }
    }

    func render() {
        // Clear screen
        self.canvas.setColor(Color.BLACK);
        self.canvas.fillRect(0, 0, 800, 600);

        // Draw all objects
        for obj in self.objects {
            obj.draw(self.canvas);
        }

        self.canvas.show();
    }
}

// Example usage
func start() {
    var game = Game(800, 600, "My Game");

    game.add(GameObject(100, 100, 50, 50, Color.RED));
    game.add(GameObject(300, 200, 30, 30, Color.BLUE));

    game.run();
}
```

This framework provides:
- **Automatic timing** with delta time
- **Object management** for game entities
- **Collision detection** between objects
- **Structured game loop** separating concerns

---

## Common Mistakes and How to Avoid Them

Graphics programming has some classic pitfalls. Learn from others' mistakes!

### Mistake 1: Off-by-One Coordinate Errors

**The bug:**
```rust
var canvas = Canvas(800, 600);
// Later...
canvas.fillRect(0, 0, 800, 600);  // This is correct
canvas.fillCircle(800, 600, 10);  // Bug! (800, 600) is outside the canvas
```

**Why it's wrong:** A 800x600 canvas has coordinates from (0,0) to (799, 599). Position (800, 600) is one pixel beyond the right and bottom edges.

**The fix:**
```rust
// The bottom-right corner is at (width-1, height-1)
canvas.fillCircle(799, 599, 10);

// Or calculate from canvas size:
canvas.fillCircle(width - 1, height - 1, 10);
```

### Mistake 2: Forgetting to Clear the Screen

**The bug:**
```rust
while running {
    x += 5;
    canvas.setColor(Color.RED);
    canvas.fillCircle(x, 100, 20);  // No clear before this!
    canvas.show();
}
```

**What happens:** The ball leaves a trail of circles because previous frames aren't erased.

**The fix:**
```rust
while running {
    x += 5;

    // Clear first!
    canvas.setColor(Color.BLACK);
    canvas.fillRect(0, 0, 800, 600);

    canvas.setColor(Color.RED);
    canvas.fillCircle(x, 100, 20);
    canvas.show();
}
```

### Mistake 3: Drawing in Wrong Order (Z-Order Issues)

**The bug:**
```rust
canvas.setColor(Color.RED);
canvas.fillCircle(100, 100, 50);  // Draw player
canvas.setColor(Color.BLUE);
canvas.fillRect(0, 0, 800, 600);  // Draw background
```

**What happens:** The background covers the player because it's drawn second.

**The fix:** Draw back-to-front (painter's algorithm):
```rust
// Background first (farthest back)
canvas.setColor(Color.BLUE);
canvas.fillRect(0, 0, 800, 600);

// Then foreground elements (closer to viewer)
canvas.setColor(Color.RED);
canvas.fillCircle(100, 100, 50);
```

### Mistake 4: Integer vs. Float Precision

**The bug:**
```rust
var x = 100;  // Integer
x = x + 0.5;  // Want to move half a pixel per frame
// But x is still 100! Integer truncation lost the 0.5
```

**What happens:** Slow movements don't work. The object either doesn't move at all (velocity < 1) or moves in jerky steps.

**The fix:**
```rust
var x = 100.0;  // Float
x = x + 0.5;    // Now x = 100.5

// When drawing, convert to integer:
canvas.fillCircle(x as Integer, y as Integer, 20);
```

### Mistake 5: Forgetting show() or flip()

**The bug:**
```rust
while running {
    canvas.setColor(Color.BLACK);
    canvas.fillRect(0, 0, 800, 600);
    canvas.setColor(Color.RED);
    canvas.fillCircle(x, y, 20);
    // Forgot canvas.show()!
}
```

**What happens:** You draw to the back buffer but never swap it to the front. The window stays blank or frozen.

**The fix:** Always call `canvas.show()` (or `canvas.flip()`) after you're done drawing each frame.

### Mistake 6: Not Accounting for Object Size in Collisions

**The bug:**
```rust
// Check if ball hit the right wall
if x > 800 {  // Wrong! This checks the center
    // bounce
}
```

**What happens:** The ball's center has to pass the wall before bouncing, so half the ball goes through the wall before reversing.

**The fix:**
```rust
// Account for the ball's radius
if x + radius > 800 {
    x = 800 - radius;  // Push back so edge touches wall
    dx = -dx;
}
```

---

## Debugging Graphics Issues

When your graphics don't look right, here's a systematic approach:

### Step 1: Add Diagnostic Output

Print key values to the console:

```rust
bind Viper.Terminal;

Say("x=" + x + ", y=" + y + ", dx=" + dx);
```

Check: Are the values what you expect? Are they changing each frame?

### Step 2: Draw Debug Visualizations

Make the invisible visible:

```rust
// Draw bounding boxes around objects
canvas.setColor(Color.WHITE);
canvas.drawRect(player.x, player.y, player.width, player.height);

// Draw collision points
canvas.setColor(Color.YELLOW);
canvas.fillCircle(player.x, player.y, 3);  // Top-left corner
canvas.fillCircle(player.x + player.width, player.y + player.height, 3);  // Bottom-right

// Draw velocity vectors
canvas.setColor(Color.GREEN);
canvas.drawLine(player.x, player.y, player.x + player.dx * 10, player.y + player.dy * 10);
```

### Step 3: Slow Down Time

Make things move slowly so you can watch:

```rust
bind Viper.Time;

sleep(500);  // Half second between frames
```

Or reduce velocities temporarily:

```rust
var speedMultiplier = 0.1;  // 10% speed for debugging
x += dx * speedMultiplier;
```

### Step 4: Check One Thing at a Time

Simplify until it works:

```rust
// Remove the game loop - just draw once
canvas.setColor(Color.RED);
canvas.fillRect(100, 100, 50, 50);
canvas.show();
canvas.waitForClose();
```

Does a simple rectangle appear? Yes? The basic setup works. Add complexity back piece by piece until you find what breaks.

### Step 5: Verify Coordinates Visually

Draw a grid to understand your coordinate space:

```rust
canvas.setColor(Color(50, 50, 50));  // Dark gray
for i in 0..=8 {
    var x = i * 100;
    canvas.drawLine(x, 0, x, 600);  // Vertical lines
}
for i in 0..=6 {
    var y = i * 100;
    canvas.drawLine(0, y, 800, y);  // Horizontal lines
}

// Label some coordinates
canvas.setColor(Color.WHITE);
canvas.setFont("Arial", 12);
canvas.drawText(5, 15, "(0,0)");
canvas.drawText(405, 315, "(400,300)");
```

---

## The Three Languages

**Zia**
```rust
bind Viper.Graphics;

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

The concepts are identical across all three languages — only the syntax differs. A canvas is a canvas. A rectangle is a rectangle. RGB colors work the same way. Once you understand graphics in one language, you understand them in all.

---

## Summary

- The **canvas** is your drawing surface — a grid of pixels addressable by (x, y) coordinates
- **(0,0) is the top-left corner**; Y increases *downward* (opposite of math class)
- **Colors use RGB** — three values from 0-255 for red, green, and blue light
- **Draw primitives** with functions like `fillRect`, `fillCircle`, `drawLine`, `fillPolygon`
- **Drawing order matters** — later drawings cover earlier ones (painter's algorithm)
- **Animation uses a game loop**: input -> update -> render, repeated many times per second
- **Delta time** ensures consistent speed regardless of frame rate
- **Double buffering** prevents flicker by drawing to a hidden buffer and swapping
- **Sprites** are pre-made images loaded from files
- **Common bugs**: forgetting to clear, off-by-one coordinates, wrong drawing order, ignoring object size in collisions

---

## Exercises

**Exercise 19.1 (Mimic)**: Modify the bouncing ball example to use a different color and starting position. Make it bounce in a square that's only 400x400 pixels in the center of the screen.

**Exercise 19.2 (Shapes)**: Draw a smiley face using circles for the head, eyes, and mouth. The eyes should be smaller circles inside the face circle. (Hint: draw in the right order!)

**Exercise 19.3 (Starfield)**: Create a starfield — fill the screen with 100 random white dots of varying sizes (1-3 pixel radius) on a black background.

**Exercise 19.4 (Animation)**: Animate a square moving across the screen from left to right. When it goes off the right edge, have it reappear on the left (wrapping around).

**Exercise 19.5 (Multiple Objects)**: Create an animation with five bouncing balls of different colors. Each should start at a random position with a random velocity. They should all bounce off the walls but don't need to bounce off each other (that's harder!).

**Exercise 19.6 (Sunset)**: Animate a simple sunset: the sun (yellow/orange circle) slowly descends from the top of the screen. As it moves down, gradually change the sky color from blue at the top to orange at the bottom.

**Exercise 19.7 (Bar Chart)**: Write a program that draws a bar chart from an array of numbers. Given `[30, 85, 45, 60, 90, 25, 70]`, draw seven vertical bars with heights proportional to the values. Add labels and axes.

**Exercise 19.8 (Clock)**: Draw an analog clock face with hour and minute hands. Make it update in real-time (use `Viper.Time.now()` to get the current time). The hands should rotate smoothly around the center.

**Exercise 19.9 (Screensaver)**: Create a "DVD logo" style screensaver: a colored rectangle bounces around the screen, changing color each time it hits a wall.

**Exercise 19.10 (Challenge - Pong)**: Create a simple one-player Pong game:
- A ball bounces around the screen
- A paddle at the bottom can move left and right
- The ball bounces off the paddle
- Score a point each time you hit the ball
- Lose if the ball passes the paddle

This requires combining graphics (this chapter) with input (next chapter), so you may want to return to this after Chapter 20.

---

*We can draw graphics and create animations. But games need to respond to the player — knowing when keys are pressed and where the mouse is. Next, we learn about handling user input.*

*[Continue to Chapter 20: User Input](20-input.md)*
