# Canvas & Color
> Canvas drawing surface and Color utilities

**Part of [Viper Runtime Library](../README.md) › [Graphics](README.md)**

---

## Viper.Graphics.Canvas

2D graphics canvas for visual applications and games.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.Canvas(title, width, height)`

### Properties

| Property      | Type    | Description                                        |
|---------------|---------|----------------------------------------------------|
| `Height`      | Integer | Canvas height in pixels                            |
| `ShouldClose` | Integer | Non-zero if the user requested to close the canvas |
| `Width`       | Integer | Canvas width in pixels                             |

### Methods

| Method                                | Signature                             | Description                                                |
|---------------------------------------|---------------------------------------|------------------------------------------------------------|
| `Arc(cx, cy, radius, startAngle, endAngle, color)` | `Void(Integer...)`         | Draws a filled arc (pie slice)                             |
| `ArcFrame(cx, cy, radius, startAngle, endAngle, color)` | `Void(Integer...)`    | Draws an arc outline                                       |
| `Bezier(x1, y1, cx, cy, x2, y2, color)` | `Void(Integer...)`                  | Draws a quadratic Bezier curve                             |
| `Blit(x, y, pixels)`                  | `Void(Integer, Integer, Pixels)`      | Blits a Pixels buffer to the canvas at (x, y)              |
| `BlitAlpha(x, y, pixels)`             | `Void(Integer, Integer, Pixels)`      | Blits with alpha blending (respects alpha channel)         |
| `BlitRegion(dx, dy, pixels, sx, sy, w, h)` | `Void(Integer...)`               | Blits a region of a Pixels buffer to the canvas            |
| `Box(x, y, w, h, color)`              | `Void(Integer...)`                    | Draws a filled rectangle                                   |
| `Clear(color)`                        | `Void(Integer)`                       | Clears the canvas with a solid color                       |
| `ClearClipRect()`                     | `Void()`                              | Clears clipping rectangle; restores full canvas drawing    |
| `CopyRect(x, y, w, h)`                | `Pixels(Integer...)`                  | Copies canvas region to a Pixels buffer                    |
| `Disc(cx, cy, r, color)`              | `Void(Integer...)`                    | Draws a filled circle                                      |
| `Ellipse(cx, cy, rx, ry, color)`      | `Void(Integer...)`                    | Draws a filled ellipse                                     |
| `EllipseFrame(cx, cy, rx, ry, color)` | `Void(Integer...)`                    | Draws an ellipse outline                                   |
| `Flip()`                              | `Void()`                              | Presents the back buffer and displays drawn content        |
| `FloodFill(x, y, color)`              | `Void(Integer, Integer, Integer)`     | Flood fills connected area starting at (x, y)              |
| `Focus()`                             | `Void()`                              | Brings the window to the front and gives it focus          |
| `Frame(x, y, w, h, color)`            | `Void(Integer...)`                    | Draws a rectangle outline                                  |
| `Fullscreen()`                        | `Void()`                              | Enters fullscreen mode                                     |
| `GetFps()`                            | `Integer()`                           | Returns the current target FPS (-1 = unlimited)            |
| `GetPixel(x, y)`                      | `Integer(Integer, Integer)`           | Gets pixel color at (x, y)                                 |
| `GetScale()`                          | `Double()`                            | Returns the HiDPI display scale factor (1.0 normal, 2.0 on Retina) |
| `GradientH(x, y, w, h, c1, c2)`      | `Void(Integer...)`                    | Draws a horizontal gradient (left c1 to right c2)          |
| `GradientV(x, y, w, h, c1, c2)`      | `Void(Integer...)`                    | Draws a vertical gradient (top c1 to bottom c2)            |
| `IsFocused()`                         | `Integer()`                           | Returns 1 if the window has keyboard focus                 |
| `IsMaximized()`                       | `Integer()`                           | Returns 1 if the window is maximized                       |
| `IsMinimized()`                       | `Integer()`                           | Returns 1 if the window is minimized (iconified)           |
| `KeyHeld(keycode)`                    | `Integer(Integer)`                    | Returns non-zero if the specified key is held down         |
| `Line(x1, y1, x2, y2, color)`         | `Void(Integer...)`                    | Draws a line between two points                            |
| `Maximize()`                          | `Void()`                              | Maximizes the window                                       |
| `Minimize()`                          | `Void()`                              | Minimizes (iconifies) the window                           |
| `Plot(x, y, color)`                   | `Void(Integer, Integer, Integer)`     | Sets a single pixel                                        |
| `Poll()`                              | `Integer()`                           | Polls for input events; returns event type (0 = none)      |
| `PreventClose(prevent)`               | `Void(Integer)`                       | Blocks (1) or allows (0) the window close button           |
| `Polygon(points, count, color)`       | `Void(Pointer, Integer, Integer)`     | Draws a filled polygon                                     |
| `PolygonFrame(points, count, color)`  | `Void(Pointer, Integer, Integer)`     | Draws a polygon outline                                    |
| `Polyline(points, count, color)`      | `Void(Pointer, Integer, Integer)`     | Draws connected line segments                              |
| `Ring(cx, cy, r, color)`              | `Void(Integer...)`                    | Draws a circle outline                                     |
| `Restore()`                           | `Void()`                              | Restores the window after minimize or maximize             |
| `RoundBox(x, y, w, h, radius, color)` | `Void(Integer...)`                    | Draws a filled rectangle with rounded corners              |
| `RoundFrame(x, y, w, h, radius, color)` | `Void(Integer...)`                  | Draws a rectangle outline with rounded corners             |
| `SaveBmp(path)`                       | `Integer(String)`                     | Saves canvas to BMP file (returns 1 on success)            |
| `SavePng(path)`                       | `Integer(String)`                     | Saves canvas to PNG file (returns 1 on success)            |
| `Screenshot()`                        | `Pixels()`                            | Captures entire canvas contents to a Pixels buffer         |
| `SetClipRect(x, y, w, h)`             | `Void(Integer...)`                    | Sets clipping rectangle; all drawing is constrained to it  |
| `SetFps(fps)`                         | `Void(Integer)`                       | Set the target frame rate (-1 = unlimited)                 |
| `SetTitle(title)`                     | `Void(String)`                        | Changes the window title at runtime                        |
| `Text(x, y, text, color)`             | `Void(Integer, Integer, String, Integer)` | Draws text at (x, y) with the specified color          |
| `TextBg(x, y, text, fg, bg)`          | `Void(Integer, Integer, String, Integer, Integer)` | Draws text with foreground and background colors |
| `TextHeight()`                        | `Integer()`                           | Returns the height of rendered text in pixels (always 8)   |
| `TextWidth(text)`                     | `Integer(String)`                     | Returns the width of rendered text in pixels (8 per char)  |
| `ThickLine(x1, y1, x2, y2, thickness, color)` | `Void(Integer...)`            | Draws a line with specified thickness (parallelogram body + rounded endcap circles) |
| `Triangle(x1, y1, x2, y2, x3, y3, color)` | `Void(Integer...)`                 | Draws a filled triangle                                    |
| `TriangleFrame(x1, y1, x2, y2, x3, y3, color)` | `Void(Integer...)`            | Draws a triangle outline                                   |
| `Windowed()`                          | `Void()`                              | Exits fullscreen mode (returns to windowed)                |

### Color Format

Colors are specified as 32-bit integers in `0x00RRGGBB` format:

- Red: `0x00FF0000`
- Green: `0x0000FF00`
- Blue: `0x000000FF`
- White: `0x00FFFFFF`
- Black: `0x00000000`

Use `Viper.Graphics.Color.RGB()` or `Viper.Graphics.Color.RGBA()` to create colors from components.

### Zia Example

```rust
module GameDemo;

bind Viper.Graphics.Canvas as Canvas;
bind Viper.Graphics.Color as Color;

func start() {
    var c = Canvas.New("My Game", 800, 600);

    // Main loop
    while c.get_ShouldClose() == 0 {
        c.Poll();
        c.Clear(Color.RGB(0, 0, 0));

        // Draw shapes
        c.Box(100, 100, 200, 150, Color.RGB(255, 0, 0));
        c.Disc(400, 300, 50, Color.RGB(0, 0, 255));
        c.Line(0, 0, 800, 600, Color.RGB(0, 255, 0));
        c.Frame(50, 50, 100, 100, Color.RGB(255, 255, 255));
        c.Ring(600, 200, 40, Color.RGB(255, 255, 0));
        c.Text(10, 10, "Hello Zia!", Color.RGB(255, 255, 255));

        c.Flip();
    }
}
```

### Example

```basic
' Create a canvas
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("My Game", 800, 600)

' Main loop
DO WHILE canvas.ShouldClose = 0
    ' Poll events
    canvas.Poll()

    ' Clear to black
    canvas.Clear(&H00000000)

    ' Draw a red filled rectangle
    canvas.Box(100, 100, 200, 150, &H00FF0000)

    ' Draw a blue filled circle
    canvas.Disc(400, 300, 50, &H000000FF)

    ' Draw a green line
    canvas.Line(0, 0, 800, 600, &H0000FF00)

    ' Draw a white rectangle outline
    canvas.Frame(50, 50, 100, 100, &H00FFFFFF)

    ' Draw a yellow circle outline
    canvas.Ring(600, 200, 40, &H00FFFF00)

    ' Draw text with transparent background
    canvas.Text(10, 10, "Hello World!", &H00FFFFFF)

    ' Draw text with solid background (useful for HUDs)
    canvas.TextBg(10, 30, "Score: 1000", &H00FFFF00, &H00000080)

    ' Present
    canvas.Flip()
LOOP
```

### Text Rendering

The canvas includes a built-in 8x8 pixel bitmap font for rendering text:

- **Font size:** 8x8 pixels per character
- **Character set:** ASCII 32-126 (printable characters)
- Characters are drawn pixel-by-pixel to the canvas
- Use `Text` for text with a transparent background
- Use `TextBg` for text with a solid background (useful for HUDs/overlays)
- Use `TextWidth(text)` to measure text width in pixels (8 per character)
- Use `TextHeight()` to get font height in pixels (always 8)

**Note:** `TextWidth` and `TextHeight` are static methods -- they do not require a canvas instance.

### Blitting Pixels Buffers

Use `Blit`, `BlitRegion`, and `BlitAlpha` to copy Pixels buffers to the canvas:

```basic
' Load an image
DIM sprite AS Viper.Graphics.Pixels
sprite = Viper.Graphics.Pixels.LoadBmp("player.bmp")

' Draw the sprite (opaque blit)
canvas.Blit(playerX, playerY, sprite)

' Draw with alpha blending (for transparent sprites)
canvas.BlitAlpha(playerX, playerY, sprite)

' Draw only a portion of the sprite (for sprite sheets)
canvas.BlitRegion(screenX, screenY, spriteSheet, frameX, frameY, 32, 32)
```

### Extended Drawing Primitives

The canvas supports additional drawing primitives for more complex shapes:

```basic
' Draw a thick line (5 pixels wide, rounded caps)
canvas.ThickLine(10, 10, 200, 150, 5, &H00FF0000)

' Draw rounded rectangles
canvas.RoundBox(50, 50, 150, 80, 15, &H0000FF00)   ' Filled with 15px radius corners
canvas.RoundFrame(50, 150, 150, 80, 15, &H000000FF) ' Outline only

' Flood fill an area (like paint bucket tool)
canvas.FloodFill(100, 100, &H00FFFF00)

' Draw triangles
canvas.Triangle(100, 50, 50, 150, 150, 150, &H00FF00FF)      ' Filled triangle
canvas.TriangleFrame(200, 50, 150, 150, 250, 150, &H0000FFFF) ' Triangle outline

' Draw ellipses (horizontal and vertical radii)
canvas.Ellipse(400, 300, 80, 50, &H00808080)      ' Filled ellipse
canvas.EllipseFrame(400, 400, 80, 50, &H00FFFFFF) ' Ellipse outline
```

### Advanced Curves & Shapes

The canvas supports arcs, Bezier curves, and general polygons:

```basic
' Draw arcs (pie slices) - angles in degrees, 0 = right, 90 = up
canvas.Arc(200, 200, 50, 0, 90, &H00FF0000)       ' Filled quarter-circle (top-right)
canvas.ArcFrame(300, 200, 50, 45, 135, &H0000FF00) ' Arc outline

' Draw a quadratic Bezier curve (start, control point, end)
canvas.Bezier(10, 100, 100, 10, 190, 100, &H000000FF)

' Polyline and polygon require an array of points [x1, y1, x2, y2, ...]
' Note: In BASIC, use an array and pass count of points
DIM points(5) AS INTEGER
points(0) = 50 : points(1) = 10   ' Point 1
points(2) = 10 : points(3) = 90   ' Point 2
points(4) = 90 : points(5) = 90   ' Point 3

canvas.Polygon(points, 3, &H00FF00FF)      ' Filled polygon (3 points = triangle)
canvas.PolygonFrame(points, 3, &H0000FFFF) ' Polygon outline
```

### Canvas Utilities

Read pixels, copy regions, take screenshots, and draw gradients:

```basic
' Get a pixel color from the canvas
DIM color AS INTEGER
color = canvas.GetPixel(100, 100)

' Copy a rectangular region from canvas to a Pixels buffer
DIM region AS Viper.Graphics.Pixels
region = canvas.CopyRect(0, 0, 200, 200)

' Capture the entire canvas to a Pixels buffer
DIM screenshot AS Viper.Graphics.Pixels
screenshot = canvas.Screenshot()

' Save the entire canvas to a BMP or PNG file
DIM success AS INTEGER
success = canvas.SaveBmp("screenshot.bmp")  ' BMP (no compression, universally supported)
success = canvas.SavePng("screenshot.png")  ' PNG (lossless, smaller than BMP)
IF success = 1 THEN
    PRINT "Screenshot saved!"
END IF

' Draw gradient backgrounds
canvas.GradientH(0, 0, 800, 600, &H00FF0000, &H000000FF)  ' Red to blue (horizontal)
canvas.GradientV(0, 0, 800, 600, &H00000000, &H00FFFFFF)  ' Black to white (vertical)
```

### Canvas Clipping

Restrict drawing to a rectangular region. All drawing operations will be clipped to the
specified bounds until `ClearClipRect()` is called.

```basic
' Set a clipping region (x=100, y=100, width=200, height=150)
canvas.SetClipRect(100, 100, 200, 150)

' This circle will only appear within the clip region
canvas.Disc(150, 150, 100, &H00FF0000)

' Drawing outside the clip region is ignored
canvas.Box(0, 0, 50, 50, &H0000FF00)  ' Not visible (outside clip)

' Restore full canvas drawing
canvas.ClearClipRect()

' Now drawing works across the entire canvas again
canvas.Box(0, 0, 50, 50, &H0000FF00)  ' Visible
```

**Use Cases for Clipping:**
- **UI panels:** Clip content to panel boundaries
- **Scrollable regions:** Clip to viewport during scrolling
- **Minimap rendering:** Clip map to minimap area
- **Text overflow:** Prevent text from drawing outside containers

### Window Controls

Change the window title, toggle fullscreen, manage window state, and query display information at runtime:

```basic
' Create a canvas
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("My Game", 800, 600)

' Update window title dynamically (e.g., show FPS or game state)
canvas.SetTitle("My Game - Level 1")

' HiDPI / Retina: multiply logical coords by scale for sharp rendering
DIM scale AS DOUBLE = canvas.GetScale()   ' 2.0 on Retina, 1.0 otherwise

' Window state management
canvas.Minimize()            ' Minimize (iconify) the window
canvas.Maximize()            ' Maximize the window
canvas.Restore()             ' Restore from minimize or maximize
IF canvas.IsMinimized() = 1 THEN PRINT "minimized"
IF canvas.IsMaximized() = 1 THEN PRINT "maximized"

' Focus management
canvas.Focus()               ' Bring window to front
IF canvas.IsFocused() = 1 THEN PRINT "has focus"

' Frame rate control
canvas.SetFps(60)            ' Limit to 60 fps
PRINT "FPS: "; canvas.GetFps()

' Prevent accidental close (e.g., while saving)
canvas.PreventClose(1)       ' Block the close button
' ... save work ...
canvas.PreventClose(0)       ' Re-enable close button

' Toggle fullscreen with F11 key
DIM isFullscreen AS INTEGER = 0
DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    IF Viper.Input.Keyboard.Pressed(300) THEN  ' 300 = F11
        IF isFullscreen = 1 THEN
            canvas.Windowed()
            isFullscreen = 0
        ELSE
            canvas.Fullscreen()
            isFullscreen = 1
        END IF
    END IF

    canvas.Flip()
LOOP
```

**Platform Behavior:**
- **macOS:** Uses native Cocoa fullscreen (menu bar hidden, dock accessible via mouse)
- **Linux:** Uses X11 EWMH protocol (_NET_WM_STATE_FULLSCREEN)
- **Windows:** Uses Win32 API (removes decorations, covers taskbar)

**Notes:**
- Fullscreen mode uses the display's native resolution
- Window size (Width/Height) remains unchanged; content is scaled
- Use `Fullscreen()` to enter and `Windowed()` to exit fullscreen mode
- `GetScale()` returns 2.0 on HiDPI (Retina) displays — multiply pixel dimensions by this factor for sharp rendering
- `SetFps(-1)` disables frame rate limiting (default); `GetFps()` returns the configured target
- `PreventClose(1)` blocks the OS close button; the `ShouldClose` property will not become true until you call `PreventClose(0)`

---

## Viper.Graphics.Color

Color utility functions for graphics operations.

**Type:** Static utility class

### Methods

| Method                   | Signature                                     | Description                                                                     |
|--------------------------|-----------------------------------------------|---------------------------------------------------------------------------------|
| `Brighten(color, amount)` | `Integer(Integer, Integer)`                  | Brightens a color by the given amount (0-100)                                   |
| `Complement(color)`      | `Integer(Integer)`                            | Returns the complementary color (opposite on color wheel)                       |
| `Darken(color, amount)`  | `Integer(Integer, Integer)`                   | Darkens a color by the given amount (0-100)                                     |
| `Desaturate(color, amount)` | `Integer(Integer, Integer)`               | Decreases saturation of a color (0-100)                                         |
| `FromHex(hex)`           | `Integer(String)`                             | Parses a hex color string (e.g., "#FF0000" or "#FF000080")                      |
| `FromHSL(h, s, l)`       | `Integer(Integer, Integer, Integer)`          | Creates a color from hue (0-360), saturation (0-100), lightness (0-100)         |
| `GetA(color)`            | `Integer(Integer)`                            | Extracts alpha component (0-255) from a packed color                            |
| `GetB(color)`            | `Integer(Integer)`                            | Extracts blue component (0-255) from a packed color                             |
| `GetG(color)`            | `Integer(Integer)`                            | Extracts green component (0-255) from a packed color                            |
| `GetH(color)`            | `Integer(Integer)`                            | Extracts hue (0-360) from a packed color                                        |
| `GetL(color)`            | `Integer(Integer)`                            | Extracts lightness (0-100) from a packed color                                  |
| `GetR(color)`            | `Integer(Integer)`                            | Extracts red component (0-255) from a packed color                              |
| `GetS(color)`            | `Integer(Integer)`                            | Extracts saturation (0-100) from a packed color                                 |
| `Grayscale(color)`       | `Integer(Integer)`                            | Converts a color to grayscale                                                   |
| `Invert(color)`          | `Integer(Integer)`                            | Inverts a color (255 minus each channel)                                        |
| `Lerp(c1, c2, t)`        | `Integer(Integer, Integer, Integer)`          | Linearly interpolates between two colors (t: 0-100, where 0=c1, 100=c2)        |
| `RGB(r, g, b)`           | `Integer(Integer, Integer, Integer)`          | Creates a color value from red, green, blue components (0-255 each)             |
| `RGBA(r, g, b, a)`       | `Integer(Integer, Integer, Integer, Integer)` | Creates a color with alpha from red, green, blue, alpha components (0-255 each) |
| `Saturate(color, amount)` | `Integer(Integer, Integer)`                  | Increases saturation of a color (0-100)                                         |
| `ToHex(color)`           | `String(Integer)`                             | Converts a color to hex string (e.g., "#RRGGBB" or "#RRGGBBAA")                |

### Zia Example

```rust
module ColorDemo;

bind Viper.Terminal;
bind Viper.Graphics.Color as Color;
bind Viper.Fmt as Fmt;

func start() {
    var red = Color.RGB(255, 0, 0);
    var green = Color.RGB(0, 255, 0);
    var blue = Color.RGB(0, 0, 255);
    var semi = Color.RGBA(255, 0, 0, 128);

    Say("Red: " + Fmt.Int(red));
    Say("Green: " + Fmt.Int(green));
    Say("Blue: " + Fmt.Int(blue));
    Say("Semi-transparent: " + Fmt.Int(semi));

    // Extract components
    Say("R: " + Fmt.Int(Color.GetR(red)));
    Say("H: " + Fmt.Int(Color.GetH(red)));

    // Color manipulation
    var bright = Color.Brighten(blue, 50);
    var lerped = Color.Lerp(red, blue, 50);
    var gray = Color.Grayscale(green);
    Say("Bright blue: " + Fmt.Int(bright));

    // HSL and hex
    var hsl = Color.FromHSL(120, 100, 50);
    var hex = Color.ToHex(red);
    Say("Hex: " + hex);
}
```

### Example

```basic
DIM red AS INTEGER
red = Viper.Graphics.Color.RGB(255, 0, 0)

DIM green AS INTEGER
green = Viper.Graphics.Color.RGB(0, 255, 0)

DIM blue AS INTEGER
blue = Viper.Graphics.Color.RGB(0, 0, 255)

DIM purple AS INTEGER
purple = Viper.Graphics.Color.RGB(128, 0, 128)

DIM semiTransparent AS INTEGER
semiTransparent = Viper.Graphics.Color.RGBA(255, 0, 0, 128)  ' 50% transparent red

' Extract individual components
DIM r AS INTEGER = Viper.Graphics.Color.GetR(purple)   ' 128
DIM g AS INTEGER = Viper.Graphics.Color.GetG(purple)   ' 0

' Create from HSL
DIM orange AS INTEGER
orange = Viper.Graphics.Color.FromHSL(30, 100, 50)

' Parse hex strings
DIM fromHex AS INTEGER
fromHex = Viper.Graphics.Color.FromHex("#FF8000")

' Color manipulation
DIM bright AS INTEGER = Viper.Graphics.Color.Brighten(blue, 30)
DIM dark AS INTEGER = Viper.Graphics.Color.Darken(red, 20)
DIM mixed AS INTEGER = Viper.Graphics.Color.Lerp(red, blue, 50)  ' 50% blend
DIM gray AS INTEGER = Viper.Graphics.Color.Grayscale(green)
DIM inv AS INTEGER = Viper.Graphics.Color.Invert(red)
DIM comp AS INTEGER = Viper.Graphics.Color.Complement(red)

' Use with graphics canvas
canvas.Box(10, 10, 100, 100, red)
canvas.Disc(200, 200, 50, purple)
```

---


## See Also

- [Images & Sprites](pixels.md)
- [Scene Graph](scene.md)
- [Graphics Overview](README.md)
- [Viper Runtime Library](../README.md)
