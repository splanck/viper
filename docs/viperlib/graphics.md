# Graphics

> 2D graphics, colors, and image manipulation.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Graphics.Canvas](#vipergraphicscanvas)
- [Viper.Graphics.Color](#vipergraphicscolor)
- [Viper.Graphics.Pixels](#vipergraphicspixels)
- [Viper.Graphics.Sprite](#vipergraphicssprite)
- [Viper.Graphics.SpriteSheet](#vipergraphicsspritesheet)
- [Viper.Graphics.Tilemap](#vipergraphicstilemap)
- [Viper.Graphics.Camera](#vipergraphicscamera)
- [Viper.Graphics.SceneNode](#vipergraphicsscenenode)
- [Viper.Graphics.Scene](#vipergraphicsscene)
- [Viper.Graphics.SpriteBatch](#vipergraphicsspritebatch)

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
| `Screenshot()`                        | `Pixels()`                            | Captures entire canvas contents to a Pixels buffer         |
| `SetClipRect(x, y, w, h)`             | `Void(Integer...)`                    | Sets clipping rectangle; all drawing is constrained to it  |
| `SetFps(fps)`                         | `Void(Integer)`                       | Set the target frame rate (-1 = unlimited)                 |
| `SetTitle(title)`                     | `Void(String)`                        | Changes the window title at runtime                        |
| `Text(x, y, text, color)`             | `Void(Integer, Integer, String, Integer)` | Draws text at (x, y) with the specified color          |
| `TextBg(x, y, text, fg, bg)`          | `Void(Integer, Integer, String, Integer, Integer)` | Draws text with foreground and background colors |
| `TextHeight()`                        | `Integer()`                           | Returns the height of rendered text in pixels (always 8)   |
| `TextWidth(text)`                     | `Integer(String)`                     | Returns the width of rendered text in pixels (8 per char)  |
| `ThickLine(x1, y1, x2, y2, thickness, color)` | `Void(Integer...)`            | Draws a line with specified thickness (rounded caps)       |
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

```zia
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

' Save the entire canvas to a BMP file
DIM success AS INTEGER
success = canvas.SaveBmp("screenshot.bmp")
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

```zia
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

## Viper.Graphics.Pixels

Software image buffer for direct pixel manipulation. Use for procedural texture generation, image processing, or custom
rendering.

**Type:** Instance class

**Constructor:** `NEW Viper.Graphics.Pixels(width, height)`

Creates a new pixel buffer initialized to transparent black (0x00000000).

### Properties

| Property | Type    | Access | Description                    |
|----------|---------|--------|--------------------------------|
| `Width`  | Integer | Read   | Width of the buffer in pixels  |
| `Height` | Integer | Read   | Height of the buffer in pixels |

### Methods

| Method                            | Signature                                                            | Description                                                                       |
|-----------------------------------|----------------------------------------------------------------------|-----------------------------------------------------------------------------------|
| `Blur(radius)`                    | `Pixels(Integer)`                                                    | Return a box-blurred copy (radius 1-10)                                           |
| `Clear()`                         | `Void()`                                                             | Clear buffer to transparent black (0x00000000)                                    |
| `Clone()`                         | `Pixels()`                                                           | Create a deep copy of this buffer                                                 |
| `Copy(dx, dy, src, sx, sy, w, h)` | `Void(Integer, Integer, Pixels, Integer, Integer, Integer, Integer)` | Copy a rectangle from source to this buffer                                       |
| `Fill(color)`                     | `Void(Integer)`                                                      | Fill entire buffer with a color                                                   |
| `FlipH()`                         | `Pixels()`                                                           | Return a horizontally flipped copy (mirror left-right)                            |
| `FlipV()`                         | `Pixels()`                                                           | Return a vertically flipped copy (mirror top-bottom)                              |
| `Get(x, y)`                       | `Integer(Integer, Integer)`                                          | Get pixel color at (x, y) as packed RGBA (0xRRGGBBAA). Returns 0 if out of bounds |
| `Grayscale()`                     | `Pixels()`                                                           | Return a grayscale copy of the image                                              |
| `Invert()`                        | `Pixels()`                                                           | Return a copy with all colors inverted (255 minus each channel)                   |
| `Resize(width, height)`           | `Pixels(Integer, Integer)`                                           | Return a scaled copy using bilinear interpolation (smoother than Scale)           |
| `Rotate180()`                     | `Pixels()`                                                           | Return a 180-degree rotated copy                                                  |
| `RotateCCW()`                     | `Pixels()`                                                           | Return a 90-degree counter-clockwise rotated copy (swaps dimensions)              |
| `RotateCW()`                      | `Pixels()`                                                           | Return a 90-degree clockwise rotated copy (swaps dimensions)                      |
| `SaveBmp(path)`                   | `Integer(String)`                                                    | Save to a BMP file. Returns 1 on success, 0 on failure                            |
| `SavePng(path)`                   | `Integer(String)`                                                    | Save to a PNG file. Returns 1 on success, 0 on failure                            |
| `Scale(width, height)`            | `Pixels(Integer, Integer)`                                           | Return a scaled copy using nearest-neighbor interpolation                         |
| `Set(x, y, color)`                | `Void(Integer, Integer, Integer)`                                    | Set pixel color at (x, y). Silently ignores out of bounds                         |
| `Tint(color)`                     | `Pixels(Integer)`                                                    | Return a copy with a color tint applied (0x00RRGGBB)                              |
| `ToBytes()`                       | `Bytes()`                                                            | Convert to raw bytes (RGBA, row-major)                                            |

### Static Methods

| Method                            | Signature                         | Description                                           |
|-----------------------------------|-----------------------------------|-------------------------------------------------------|
| `FromBytes(width, height, bytes)` | `Pixels(Integer, Integer, Bytes)` | Create from raw bytes (RGBA, row-major)               |
| `LoadBmp(path)`                   | `Pixels(String)`                  | Load from a 24-bit BMP file. Returns null on failure  |
| `LoadPng(path)`                   | `Pixels(String)`                  | Load from a PNG file. Returns null on failure          |

### Drawing Primitives

Drawing primitives use the `0x00RRGGBB` color format (compatible with `Canvas` and `Color.RGB()`).
Alpha is always 255 (fully opaque). Coordinates outside the pixel buffer are silently clipped.

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetRGB(x, y, color)` | `Void(Integer, Integer, Integer)` | Set pixel using `0x00RRGGBB` format (alpha = 255) |
| `GetRGB(x, y)` | `Integer(Integer, Integer)` | Get pixel as `0x00RRGGBB` (alpha discarded) |
| `DrawLine(x1, y1, x2, y2, color)` | `Void(Integer...)` | Bresenham line between two points |
| `DrawBox(x, y, w, h, color)` | `Void(Integer...)` | Filled rectangle |
| `DrawFrame(x, y, w, h, color)` | `Void(Integer...)` | Rectangle outline |
| `DrawDisc(cx, cy, r, color)` | `Void(Integer...)` | Filled circle |
| `DrawRing(cx, cy, r, color)` | `Void(Integer...)` | Circle outline |
| `DrawEllipse(cx, cy, rx, ry, color)` | `Void(Integer...)` | Filled ellipse |
| `DrawEllipseFrame(cx, cy, rx, ry, color)` | `Void(Integer...)` | Ellipse outline |
| `FloodFill(x, y, color)` | `Void(Integer...)` | Iterative scanline flood fill (no recursion depth limit) |
| `DrawThickLine(x1, y1, x2, y2, thickness, color)` | `Void(Integer...)` | Line with thickness (pen-radius circles at each point) |
| `DrawTriangle(x1, y1, x2, y2, x3, y3, color)` | `Void(Integer...)` | Filled triangle |
| `DrawBezier(x1, y1, cx, cy, x2, y2, color)` | `Void(Integer...)` | Quadratic Bezier curve |

> **Color format note:** `Pixels.Set(x, y, color)` and `Pixels.Get(x, y)` use `0xRRGGBBAA` (packed RGBA
> with explicit alpha). Drawing primitives and `SetRGB`/`GetRGB` use `0x00RRGGBB` — the same format as
> `Canvas` drawing calls and `Color.RGB()`. This allows the same color constants to be used when drawing
> to both a Canvas and an off-screen Pixels buffer without any format conversion.

#### Zia Example — Drawing into an off-screen buffer

```zia
module PixelsDrawDemo;

bind Viper.Graphics;
bind Viper.Graphics.Color as Color;

func start() {
    // Create an off-screen pixel buffer and draw into it
    var buf = Pixels.New(320, 240);

    // All drawing uses 0x00RRGGBB — same as Canvas and Color.RGB()
    buf.DrawBox(0, 0, 320, 240, Color.RGB(30, 30, 30));       // dark background
    buf.DrawDisc(160, 120, 80, Color.RGB(0, 120, 220));        // blue filled circle
    buf.DrawRing(160, 120, 80, Color.RGB(255, 255, 255));      // white outline
    buf.DrawLine(0, 0, 319, 239, Color.RGB(255, 80, 0));       // orange diagonal
    buf.DrawEllipse(160, 60, 60, 25, Color.RGB(200, 0, 200)); // purple ellipse
    buf.FloodFill(5, 5, Color.RGB(10, 10, 50));                // flood fill corner

    // Blit the finished buffer to a canvas for display
    var c = Canvas.New("Pixels Draw Demo", 320, 240);
    while c.ShouldClose == 0 {
        c.Poll();
        c.Blit(0, 0, buf);
        c.Flip();
    }
}
```

### Color Format

`Pixels.Set` and `Pixels.Get` use packed 32-bit RGBA in the format `0xRRGGBBAA`:

- `RR` - Red component (0-255)
- `GG` - Green component (0-255)
- `BB` - Blue component (0-255)
- `AA` - Alpha component (0-255, where 255 = opaque)

**Drawing primitives** (`SetRGB`, `GetRGB`, `DrawLine`, `DrawBox`, etc.) use `0x00RRGGBB` — the same
format as `Canvas` methods and `Color.RGB()`. This makes it straightforward to share color constants
between on-screen canvas drawing and off-screen pixel buffer operations.

Use `Viper.Graphics.Color.RGBA()` to create `0xRRGGBBAA` colors for `Set`/`Get`, and
`Viper.Graphics.Color.RGB()` to create `0x00RRGGBB` colors for drawing primitives.

### Zia Example

```zia
module PixelsDemo;

bind Viper.Terminal;
bind Viper.Graphics.Pixels as Pixels;
bind Viper.Graphics.Color as Color;
bind Viper.Fmt as Fmt;

func start() {
    var p = Pixels.New(64, 64);
    Say("Size: " + Fmt.Int(p.get_Width()) + "x" + Fmt.Int(p.get_Height()));

    // Set and get pixels
    p.Set(0, 0, Color.RGB(255, 0, 0));
    var c = p.Get(0, 0);
    Say("Pixel(0,0): " + Fmt.Int(c));

    // Fill and clear
    p.Fill(Color.RGB(0, 255, 0));
    p.Clear();

    // Clone
    var clone = p.Clone();
    Say("Clone: " + Fmt.Int(clone.get_Width()) + "x" + Fmt.Int(clone.get_Height()));

    // Transform operations (return new Pixels)
    var flipped = p.FlipH();
    var rotated = p.RotateCW();
    var scaled = p.Scale(128, 128);
    Say("Scaled: " + Fmt.Int(scaled.get_Width()) + "x" + Fmt.Int(scaled.get_Height()));
}
```

### Example

```basic
DIM pixels AS Viper.Graphics.Pixels
pixels = NEW Viper.Graphics.Pixels(256, 256)

' Create a gradient
DIM x AS INTEGER
DIM y AS INTEGER
FOR y = 0 TO 255
    FOR x = 0 TO 255
        ' Red increases left-to-right, green increases top-to-bottom
        DIM r AS INTEGER = x
        DIM g AS INTEGER = y
        DIM color AS INTEGER = Viper.Graphics.Color.RGB(r, g, 0)
        pixels.Set(x, y, color)
    NEXT x
NEXT y

' Copy a region
DIM copy AS Viper.Graphics.Pixels
copy = NEW Viper.Graphics.Pixels(64, 64)
copy.Copy(0, 0, pixels, 100, 100, 64, 64)

' Clone the entire buffer
DIM backup AS Viper.Graphics.Pixels
backup = pixels.Clone()

' Convert to bytes for serialization
DIM data AS Viper.Collections.Bytes
data = pixels.ToBytes()

' Save to BMP file
pixels.SaveBmp("output.bmp")

' Load from BMP file
DIM loaded AS Viper.Graphics.Pixels
loaded = Viper.Graphics.Pixels.LoadBmp("input.bmp")
IF loaded <> NULL THEN
    PRINT "Loaded image: "; loaded.Width; "x"; loaded.Height
END IF

' Transform operations (all return new Pixels objects)
DIM flipped AS Viper.Graphics.Pixels
flipped = pixels.FlipH()     ' Mirror horizontally
flipped = pixels.FlipV()     ' Mirror vertically

DIM rotated AS Viper.Graphics.Pixels
rotated = pixels.RotateCW()  ' Rotate 90 degrees clockwise
rotated = pixels.RotateCCW() ' Rotate 90 degrees counter-clockwise
rotated = pixels.Rotate180() ' Rotate 180 degrees

' Scale to new dimensions (nearest-neighbor interpolation)
DIM scaled AS Viper.Graphics.Pixels
scaled = pixels.Scale(128, 128)  ' Scale to 128x128
scaled = pixels.Scale(pixels.Width * 2, pixels.Height * 2)  ' Double size

' Resize with bilinear interpolation (smoother)
DIM resized AS Viper.Graphics.Pixels
resized = pixels.Resize(128, 128)

' Image processing (all return new Pixels objects)
DIM inverted AS Viper.Graphics.Pixels
inverted = pixels.Invert()      ' Invert all colors
DIM gray AS Viper.Graphics.Pixels
gray = pixels.Grayscale()       ' Convert to grayscale
DIM tinted AS Viper.Graphics.Pixels
tinted = pixels.Tint(&H00FF0000) ' Apply red tint
DIM blurred AS Viper.Graphics.Pixels
blurred = pixels.Blur(3)        ' Box blur with radius 3

' PNG support
DIM pngImg AS Viper.Graphics.Pixels
pngImg = Viper.Graphics.Pixels.LoadPng("image.png")
pixels.SavePng("output.png")
```

### Notes

- Pixel data is stored in row-major order (row 0 first, then row 1, etc.)
- Out-of-bounds reads return 0 (transparent black)
- Out-of-bounds writes are silently ignored
- The `Copy` method automatically clips to buffer boundaries
- `ToBytes` returns 4 bytes per pixel (width × height × 4 total bytes)
- BMP support is limited to 24-bit uncompressed format (most common)
- When loading BMP files, alpha is set to 255 (opaque) for all pixels
- All transform operations (flip, rotate, scale) return new Pixels objects
- RotateCW and RotateCCW swap width and height dimensions
- Scale uses nearest-neighbor interpolation (fast, no blending)
- Resize uses bilinear interpolation (smoother, better for non-integer scale factors)
- Image processing methods (Invert, Grayscale, Tint, Blur) return new Pixels objects
- PNG support handles 8-bit RGB and RGBA files

---

## Viper.Graphics.Sprite

2D sprite with transform, animation, and collision detection for game development.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.Sprite(pixels)` or `Viper.Graphics.Sprite.FromFile(path)`

### Static Methods

| Method           | Signature         | Description                                          |
|------------------|-------------------|------------------------------------------------------|
| `FromFile(path)` | `Sprite(String)`  | Load sprite from a BMP file. Returns NULL on failure |

### Properties

| Property     | Type    | Access | Description                                  |
|--------------|---------|--------|----------------------------------------------|
| `FlipX`      | Integer | R/W    | Horizontal flip (1 = flipped, 0 = normal)    |
| `FlipY`      | Integer | R/W    | Vertical flip (1 = flipped, 0 = normal)      |
| `Frame`      | Integer | R/W    | Current animation frame index                |
| `FrameCount` | Integer | Read   | Total number of animation frames             |
| `Height`     | Integer | Read   | Height of current frame in pixels            |
| `Rotation`   | Integer | R/W    | Rotation in degrees                          |
| `ScaleX`     | Integer | R/W    | Horizontal scale (100 = 100%)                |
| `ScaleY`     | Integer | R/W    | Vertical scale (100 = 100%)                  |
| `Visible`    | Integer | R/W    | Visibility (1 = visible, 0 = hidden)         |
| `Width`      | Integer | Read   | Width of current frame in pixels             |
| `X`          | Integer | R/W    | X position in pixels                         |
| `Y`          | Integer | R/W    | Y position in pixels                         |

### Methods

| Method                      | Signature                          | Description                                           |
|-----------------------------|------------------------------------|-------------------------------------------------------|
| `AddFrame(pixels)`          | `Void(Pixels)`                     | Add an animation frame                                |
| `Contains(x, y)`            | `Integer(Integer, Integer)`        | Check if point is inside sprite (returns 1 or 0)      |
| `Draw(canvas)`              | `Void(Canvas)`                     | Draw the sprite to a canvas                           |
| `Move(dx, dy)`              | `Void(Integer, Integer)`           | Move sprite by delta amounts                          |
| `Overlaps(other)`           | `Integer(Sprite)`                  | Check bounding box overlap (returns 1 or 0)           |
| `SetFrameDelay(ms)`         | `Void(Integer)`                    | Set delay between animation frames (milliseconds)     |
| `SetOrigin(x, y)`           | `Void(Integer, Integer)`           | Set origin point for rotation/scaling                 |
| `Update()`                  | `Void()`                           | Advance animation (call each frame)                   |

### Zia Example

```zia
module SpriteDemo;

bind Viper.Terminal;
bind Viper.Graphics.Sprite as Sprite;
bind Viper.Graphics.Pixels as Pixels;
bind Viper.Graphics.Color as Color;
bind Viper.Fmt as Fmt;

func start() {
    // Create sprite from pixels
    var px = Pixels.New(32, 32);
    px.Fill(Color.RGB(255, 0, 0));
    var s = Sprite.New(px);

    Say("Size: " + Fmt.Int(s.get_Width()) + "x" + Fmt.Int(s.get_Height()));

    // Position and transform
    s.set_X(100);
    s.set_Y(200);
    s.set_ScaleX(150);
    s.set_ScaleY(150);
    s.set_Rotation(45);
    Say("Pos: " + Fmt.Int(s.get_X()) + "," + Fmt.Int(s.get_Y()));

    // Movement
    s.Move(10, 20);
    Say("After move: " + Fmt.Int(s.get_X()) + "," + Fmt.Int(s.get_Y()));
}
```

### Example

```basic
' Create a sprite from file
DIM player AS Viper.Graphics.Sprite
player = Viper.Graphics.Sprite.FromFile("player.bmp")

IF player <> NULL THEN
    ' Set position
    player.X = 100
    player.Y = 200

    ' Set origin to center for rotation
    player.SetOrigin(player.Width / 2, player.Height / 2)

    ' Scale to 150%
    player.ScaleX = 150
    player.ScaleY = 150

    ' Add animation frames
    DIM frame2 AS Viper.Graphics.Pixels
    frame2 = Viper.Graphics.Pixels.LoadBmp("player2.bmp")
    player.AddFrame(frame2)
    player.SetFrameDelay(100)  ' 100ms between frames

    ' Game loop
    DO WHILE canvas.ShouldClose = 0
        canvas.Poll()
        canvas.Clear(&H000000)

        ' Update animation
        player.Update()

        ' Move with arrow keys
        IF Viper.Input.Keyboard.Held(262) THEN player.Move(5, 0)   ' Right
        IF Viper.Input.Keyboard.Held(263) THEN player.Move(-5, 0)  ' Left

        ' Rotate with Q/E
        IF Viper.Input.Keyboard.Held(81) THEN player.Rotation = player.Rotation - 2
        IF Viper.Input.Keyboard.Held(69) THEN player.Rotation = player.Rotation + 2

        ' Draw sprite
        player.Draw(canvas)

        canvas.Flip()
    LOOP
END IF
```

### Collision Detection

```basic
DIM player AS Viper.Graphics.Sprite
DIM enemy AS Viper.Graphics.Sprite

' ... initialize sprites ...

' Check bounding box collision
IF player.Overlaps(enemy) = 1 THEN
    PRINT "Collision detected!"
END IF

' Check if mouse is over sprite
DIM mx AS INTEGER = Viper.Input.Mouse.X
DIM my AS INTEGER = Viper.Input.Mouse.Y
IF player.Contains(mx, my) = 1 THEN
    PRINT "Mouse over player!"
END IF
```

---

## Viper.Graphics.SpriteSheet

Sprite sheet/atlas for named region extraction from a single texture. Defines named rectangular regions within an atlas image and extracts them as individual `Pixels` buffers.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.SpriteSheet(atlas)` or `Viper.Graphics.SpriteSheet.FromGrid(atlas, frameW, frameH)`

### Static Methods (Constructors)

| Method                                 | Signature                       | Description                                                             |
|----------------------------------------|---------------------------------|-------------------------------------------------------------------------|
| `New(atlas)`                           | `SpriteSheet(Pixels)`           | Create from atlas Pixels buffer                                         |
| `FromGrid(atlas, frameW, frameH)`      | `SpriteSheet(Pixels, Int, Int)` | Auto-slice atlas into uniform cells; regions named `"0"`, `"1"`, etc.  |

### Properties

| Property      | Type    | Access | Description                              |
|---------------|---------|--------|------------------------------------------|
| `RegionCount` | Integer | Read   | Number of defined named regions          |
| `Width`       | Integer | Read   | Width of the underlying atlas in pixels  |
| `Height`      | Integer | Read   | Height of the underlying atlas in pixels |

### Methods

| Method                              | Signature                          | Description                                            |
|-------------------------------------|------------------------------------|--------------------------------------------------------|
| `GetRegion(name)`                   | `Pixels(String)`                   | Extract region as a new Pixels buffer; NULL if missing |
| `HasRegion(name)`                   | `Boolean(String)`                  | Check if a region name exists                          |
| `RegionNames()`                     | `Seq()`                            | Return all region names as a Seq of strings            |
| `RemoveRegion(name)`                | `Boolean(String)`                  | Remove a named region; returns false if not found      |
| `SetRegion(name, x, y, w, h)`       | `Void(String, Int, Int, Int, Int)` | Define a named rectangular region within the atlas     |

### Notes

- `FromGrid()` automatically slices the atlas into equal cells, named `"0"`, `"1"`, etc. (left-to-right, top-to-bottom)
- `GetRegion()` returns a new Pixels object on each call — cache results for repeated use
- Region coordinates are in pixels, relative to the atlas top-left corner
- All regions share the underlying atlas Pixels object

### Zia Example

```zia
module SpriteSheetDemo;

bind Viper.Graphics;
bind Viper.Terminal;
bind Viper.Fmt as Fmt;

func start() {
    var atlas = Pixels.New(128, 128);
    atlas.Fill(Color.RGB(200, 100, 50));

    var sheet = SpriteSheet.New(atlas);
    Say("Regions: " + Fmt.Int(sheet.RegionCount));  // 0

    // Define named regions
    sheet.SetRegion("idle", 0, 0, 32, 32);
    sheet.SetRegion("walk1", 32, 0, 32, 32);
    sheet.SetRegion("walk2", 64, 0, 32, 32);

    SayBool(sheet.HasRegion("idle"));    // true
    SayBool(sheet.HasRegion("jump"));    // false
    Say("Regions: " + Fmt.Int(sheet.RegionCount));  // 3

    // Extract region as Pixels
    var idleFrame = sheet.GetRegion("idle");
    Say("Frame: " + Fmt.Int(idleFrame.Width) + "x" + Fmt.Int(idleFrame.Height));  // 32x32

    // List all region names
    var names = sheet.RegionNames();

    // Remove a region
    SayBool(sheet.RemoveRegion("walk2"));  // true
    Say("Regions: " + Fmt.Int(sheet.RegionCount));  // 2

    // Auto-slice from uniform grid
    var gridSheet = SpriteSheet.FromGrid(atlas, 32, 32);
    Say("Grid regions: " + Fmt.Int(gridSheet.RegionCount));  // 16
}
```

### Example

```basic
' Load atlas image
DIM atlas AS Viper.Graphics.Pixels
atlas = Viper.Graphics.Pixels.LoadBmp("sprites.bmp")

' Method 1: Manual region definition
DIM sheet AS Viper.Graphics.SpriteSheet
sheet = Viper.Graphics.SpriteSheet.New(atlas)

sheet.SetRegion("player_idle",  0,  0, 32, 48)
sheet.SetRegion("player_walk1", 32, 0, 32, 48)
sheet.SetRegion("player_walk2", 64, 0, 32, 48)
sheet.SetRegion("enemy",         0, 48, 32, 32)

PRINT "Region count: "; sheet.RegionCount   ' Output: 4

' Check existence and extract
IF sheet.HasRegion("player_idle") THEN
    DIM frame AS Viper.Graphics.Pixels
    frame = sheet.GetRegion("player_idle")
    PRINT "Frame size: "; frame.Width; "x"; frame.Height
END IF

' Method 2: Uniform grid layout
DIM gridSheet AS Viper.Graphics.SpriteSheet
gridSheet = Viper.Graphics.SpriteSheet.FromGrid(atlas, 32, 32)
' Regions auto-named "0", "1", "2", ... (left-to-right, top-to-bottom)
DIM frame0 AS Viper.Graphics.Pixels
frame0 = gridSheet.GetRegion("0")

' List all region names
DIM names AS OBJECT
names = sheet.RegionNames()

' Remove a region
IF sheet.RemoveRegion("enemy") THEN
    PRINT "Region removed"
END IF
```

---

## Viper.Graphics.Tilemap

Efficient tile-based 2D map rendering for platformers, RPGs, and strategy games.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.Tilemap(width, height, tileWidth, tileHeight)`

### Properties

| Property     | Type    | Access | Description                    |
|--------------|---------|--------|--------------------------------|
| `Width`      | Integer | Read   | Map width in tiles             |
| `Height`     | Integer | Read   | Map height in tiles            |
| `TileWidth`  | Integer | Read   | Width of each tile in pixels   |
| `TileHeight` | Integer | Read   | Height of each tile in pixels  |
| `TileCount`  | Integer | Read   | Number of tiles in tileset     |

### Methods

| Method                                         | Signature                                    | Description                                           |
|------------------------------------------------|----------------------------------------------|-------------------------------------------------------|
| `Clear()`                                      | `Void()`                                     | Clear map (set all to 0)                              |
| `CollideBody(body)`                            | `Integer(Object)`                            | Resolve a Physics2D.Body against solid/one-way tiles (returns 1 on collision) |
| `Draw(canvas, offsetX, offsetY)`               | `Void(Canvas, Integer, Integer)`             | Draw tilemap with scroll offset                       |
| `DrawRegion(canvas, ox, oy, vx, vy, vw, vh)`   | `Void(Canvas, Integer...)`                   | Draw only visible region (for optimization)           |
| `Fill(index)`                                  | `Void(Integer)`                              | Fill entire map with a tile                           |
| `FillRect(x, y, w, h, index)`                  | `Void(Integer...)`                           | Fill rectangular region                               |
| `GetCollision(tileId)`                         | `Integer(Integer)`                           | Get collision type for a tile ID                      |
| `GetTile(x, y)`                                | `Integer(Integer, Integer)`                  | Get tile index at position                            |
| `IsSolidAt(pixelX, pixelY)`                    | `Integer(Integer, Integer)`                  | Check if a pixel position is on a solid tile          |
| `SetCollision(tileId, collType)`               | `Void(Integer, Integer)`                     | Set collision type for a tile ID (0=none, 1=solid, 2=one_way_up) |
| `SetTile(x, y, index)`                         | `Void(Integer, Integer, Integer)`            | Set tile at position (0 = empty)                      |
| `SetTileset(pixels)`                           | `Void(Pixels)`                               | Set the tileset image (tiles arranged in grid)        |
| `ToPixelX(tileX)`                              | `Integer(Integer)`                           | Convert tile X to pixel X                             |
| `ToPixelY(tileY)`                              | `Integer(Integer)`                           | Convert tile Y to pixel Y                             |
| `ToTileX(pixelX)`                              | `Integer(Integer)`                           | Convert pixel X to tile X                             |
| `ToTileY(pixelY)`                              | `Integer(Integer)`                           | Convert pixel Y to tile Y                             |

### Zia Example

```zia
module TilemapDemo;

bind Viper.Terminal;
bind Viper.Graphics.Tilemap as TM;
bind Viper.Fmt as Fmt;

func start() {
    var map = TM.New(10, 10, 16, 16);
    Say("Map: " + Fmt.Int(map.get_Width()) + "x" + Fmt.Int(map.get_Height()));
    Say("Tile size: " + Fmt.Int(map.get_TileWidth()) + "x" + Fmt.Int(map.get_TileHeight()));

    // Fill with grass (tile 1)
    map.Fill(1);
    Say("Tile(0,0): " + Fmt.Int(map.GetTile(0, 0)));

    // Place individual tiles
    map.SetTile(5, 5, 3);
    Say("Tile(5,5): " + Fmt.Int(map.GetTile(5, 5)));

    // Fill a rectangle region
    map.FillRect(2, 2, 3, 3, 7);

    // Coordinate conversion
    Say("ToTileX(32): " + Fmt.Int(map.ToTileX(32)));
    Say("ToPixelX(2): " + Fmt.Int(map.ToPixelX(2)));

    map.Clear();
}
```

### Example

```basic
' Create a 100x100 tilemap with 16x16 pixel tiles
DIM map AS Viper.Graphics.Tilemap
map = NEW Viper.Graphics.Tilemap(100, 100, 16, 16)

' Load tileset (tiles arranged in a grid)
DIM tileset AS Viper.Graphics.Pixels
tileset = Viper.Graphics.Pixels.LoadBmp("tileset.bmp")
map.SetTileset(tileset)

' Fill with grass (tile index 1)
map.Fill(1)

' Add some walls (tile index 2)
map.FillRect(10, 10, 5, 5, 2)

' Set individual tiles
map.SetTile(20, 20, 3)  ' Place a tree

' Scroll position
DIM scrollX AS INTEGER = 0
DIM scrollY AS INTEGER = 0

' Game loop
DO WHILE canvas.ShouldClose = 0
    canvas.Poll()
    canvas.Clear(&H000000)

    ' Scroll with arrow keys
    IF Viper.Input.Keyboard.Held(262) THEN scrollX = scrollX + 4
    IF Viper.Input.Keyboard.Held(263) THEN scrollX = scrollX - 4
    IF Viper.Input.Keyboard.Held(265) THEN scrollY = scrollY - 4
    IF Viper.Input.Keyboard.Held(264) THEN scrollY = scrollY + 4

    ' Draw tilemap
    map.Draw(canvas, -scrollX, -scrollY)

    ' Convert mouse position to tile coordinates
    DIM mx AS INTEGER = Viper.Input.Mouse.X + scrollX
    DIM my AS INTEGER = Viper.Input.Mouse.Y + scrollY
    DIM tileX AS INTEGER = map.ToTileX(mx)
    DIM tileY AS INTEGER = map.ToTileY(my)

    canvas.Text(10, 10, "Tile: " + STR$(tileX) + "," + STR$(tileY), &HFFFFFF)

    canvas.Flip()
LOOP
```

### Tileset Layout

Tiles in the tileset image are arranged left-to-right, top-to-bottom:

```
+---+---+---+---+
| 0 | 1 | 2 | 3 |
+---+---+---+---+
| 4 | 5 | 6 | 7 |
+---+---+---+---+
```

- **Index 0** is typically empty/transparent
- Indices start at 0, counted left-to-right, then top-to-bottom

---

## Viper.Graphics.Camera

2D camera for viewport management, scrolling, and coordinate transformation.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.Camera(width, height)`

### Properties

| Property   | Type    | Access | Description                         |
|------------|---------|--------|-------------------------------------|
| `X`        | Integer | R/W    | Camera X position (world coords)    |
| `Y`        | Integer | R/W    | Camera Y position (world coords)    |
| `Zoom`     | Integer | R/W    | Zoom level (100 = 100%)             |
| `Rotation` | Integer | R/W    | Rotation in degrees                 |
| `Width`    | Integer | Read   | Viewport width                      |
| `Height`   | Integer | Read   | Viewport height                     |

### Methods

| Method                           | Signature                              | Description                                      |
|----------------------------------|----------------------------------------|--------------------------------------------------|
| `ClearBounds()`                  | `Void()`                               | Remove camera bounds                             |
| `Follow(x, y)`                   | `Void(Integer, Integer)`               | Center camera on world position                  |
| `Move(dx, dy)`                   | `Void(Integer, Integer)`               | Move camera by delta amounts                     |
| `SetBounds(minX, minY, maxX, maxY)` | `Void(Integer, Integer, Integer, Integer)` | Limit camera movement range         |
| `ToScreenX(worldX)`              | `Integer(Integer)`                     | Convert world X to screen X                      |
| `ToScreenY(worldY)`              | `Integer(Integer)`                     | Convert world Y to screen Y                      |
| `ToWorldX(screenX)`              | `Integer(Integer)`                     | Convert screen X to world X                      |
| `ToWorldY(screenY)`              | `Integer(Integer)`                     | Convert screen Y to world Y                      |
| `IsDirty()`                      | `Integer()`                            | Returns 1 if position/zoom/rotation changed since last `ClearDirty` |
| `ClearDirty()`                   | `Void()`                               | Reset the dirty flag (call after re-rendering)   |

### Zia Example

```zia
module CameraDemo;

bind Viper.Terminal;
bind Viper.Graphics.Camera as Camera;
bind Viper.Fmt as Fmt;

func start() {
    var cam = Camera.New(800, 600);
    Say("Viewport: " + Fmt.Int(cam.get_Width()) + "x" + Fmt.Int(cam.get_Height()));

    // Position camera
    cam.set_X(100);
    cam.set_Y(200);
    cam.Follow(500, 400);
    Say("Pos: " + Fmt.Int(cam.get_X()) + "," + Fmt.Int(cam.get_Y()));

    // Coordinate conversion
    var sx = cam.ToScreenX(500);
    var sy = cam.ToScreenY(400);
    Say("Screen: " + Fmt.Int(sx) + "," + Fmt.Int(sy));

    // Movement and bounds
    cam.Move(10, 20);
    cam.SetBounds(0, 0, 2000, 1500);
    cam.ClearBounds();
}
```

### Example

```basic
' Create camera matching screen size
DIM camera AS Viper.Graphics.Camera
camera = NEW Viper.Graphics.Camera(800, 600)

' Player position (world coordinates)
DIM playerX AS INTEGER = 400
DIM playerY AS INTEGER = 300

' Set camera bounds to prevent showing outside world
camera.SetBounds(0, 0, 2000, 1500)  ' World is 2000x1500

' Game loop
DO WHILE canvas.ShouldClose = 0
    canvas.Poll()
    canvas.Clear(&H000000)

    ' Move player
    IF Viper.Input.Keyboard.Held(262) THEN playerX = playerX + 5
    IF Viper.Input.Keyboard.Held(263) THEN playerX = playerX - 5
    IF Viper.Input.Keyboard.Held(265) THEN playerY = playerY - 5
    IF Viper.Input.Keyboard.Held(264) THEN playerY = playerY + 5

    ' Camera follows player
    camera.Follow(playerX, playerY)

    ' Draw tilemap using camera offset
    map.Draw(canvas, -camera.X + camera.Width / 2, -camera.Y + camera.Height / 2)

    ' Draw player at screen position
    DIM screenX AS INTEGER = camera.ToScreenX(playerX)
    DIM screenY AS INTEGER = camera.ToScreenY(playerY)
    canvas.Box(screenX - 16, screenY - 16, 32, 32, &H00FF00)

    ' Convert mouse to world coordinates for clicking
    DIM mx AS INTEGER = Viper.Input.Mouse.X
    DIM my AS INTEGER = Viper.Input.Mouse.Y
    DIM worldX AS INTEGER = camera.ToWorldX(mx)
    DIM worldY AS INTEGER = camera.ToWorldY(my)

    canvas.Text(10, 10, "World: " + STR$(worldX) + "," + STR$(worldY), &HFFFFFF)

    ' Zoom with +/-
    IF Viper.Input.Keyboard.Pressed(61) THEN camera.Zoom = camera.Zoom + 10
    IF Viper.Input.Keyboard.Pressed(45) THEN camera.Zoom = camera.Zoom - 10

    canvas.Flip()
LOOP
```

### Camera + Tilemap + Sprite Integration

```basic
' Full game rendering pipeline
DIM camera AS Viper.Graphics.Camera
DIM map AS Viper.Graphics.Tilemap
DIM player AS Viper.Graphics.Sprite

' ... initialize all objects ...

' Render in correct order
SUB Render()
    ' 1. Clear screen
    canvas.Clear(&H000000)

    ' 2. Draw tilemap with camera offset
    map.Draw(canvas, -camera.X + 400, -camera.Y + 300)

    ' 3. Draw sprites at screen positions
    player.X = camera.ToScreenX(playerWorldX)
    player.Y = camera.ToScreenY(playerWorldY)
    player.Draw(canvas)

    ' 4. Draw UI (not affected by camera)
    canvas.Text(10, 10, "Score: " + STR$(score), &HFFFFFF)

    canvas.Flip()
END SUB
```

---

## Viper.Graphics.SceneNode

Hierarchical scene node for building scene graphs with transform inheritance.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.SceneNode()` (empty node) or `Viper.Graphics.SceneNode.FromSprite(sprite)` (with sprite)

Creates a scene node. Scene nodes support parent-child hierarchies where child transforms are relative to their parent.

### Static Methods

| Method               | Signature         | Description                                          |
|----------------------|-------------------|------------------------------------------------------|
| `FromSprite(sprite)` | `SceneNode(Sprite)` | Create a scene node with a sprite attached         |

### Properties

| Property        | Type    | Access | Description                                    |
|-----------------|---------|--------|------------------------------------------------|
| `X`             | Integer | R/W    | Local X position (relative to parent)          |
| `Y`             | Integer | R/W    | Local Y position (relative to parent)          |
| `ScaleX`        | Integer | R/W    | Local X scale (100 = 100%)                     |
| `ScaleY`        | Integer | R/W    | Local Y scale (100 = 100%)                     |
| `Rotation`      | Integer | R/W    | Local rotation in degrees                      |
| `Visible`       | Integer | R/W    | Visibility (1=visible, 0=hidden)               |
| `Depth`         | Integer | R/W    | Z-order for depth sorting (higher = on top)    |
| `Name`          | String  | R/W    | Name/tag for identification and search         |
| `Sprite`        | Object  | R/W    | Sprite attached to this node                   |
| `Parent`        | Object  | Read   | Parent scene node (NULL if root)               |
| `ChildCount`    | Integer | Read   | Number of direct children                      |
| `WorldX`        | Integer | Read   | Computed world X position                      |
| `WorldY`        | Integer | Read   | Computed world Y position                      |
| `WorldScaleX`   | Integer | Read   | Computed world X scale                         |
| `WorldScaleY`   | Integer | Read   | Computed world Y scale                         |
| `WorldRotation` | Integer | Read   | Computed world rotation                        |

### Methods

| Method                            | Signature                      | Description                                    |
|-----------------------------------|--------------------------------|------------------------------------------------|
| `AddChild(child)`                 | `Void(SceneNode)`              | Add a child node                               |
| `Detach()`                        | `Void()`                       | Remove this node from its parent               |
| `Draw(canvas)`                    | `Void(Canvas)`                 | Draw this node and all children to a canvas    |
| `DrawWithCamera(canvas, camera)`  | `Void(Canvas, Camera)`         | Draw with camera transform applied             |
| `Find(name)`                      | `SceneNode(String)`            | Find a descendant node by name                 |
| `GetChild(index)`                 | `SceneNode(Integer)`           | Get child by index                             |
| `Move(dx, dy)`                    | `Void(Integer, Integer)`       | Move the node by delta amounts                 |
| `RemoveChild(child)`              | `Void(SceneNode)`              | Remove a child node                            |
| `SetPosition(x, y)`              | `Void(Integer, Integer)`       | Set both X and Y position at once              |
| `SetScale(scale)`                 | `Void(Integer)`                | Set both ScaleX and ScaleY to the same value   |
| `Update()`                        | `Void()`                       | Update node and all children (for animations)  |

### Zia Example

```zia
module SceneNodeDemo;

bind Viper.Graphics;
bind Viper.Terminal;

func start() {
    var root = SceneNode.New();
    SceneNode.set_Name(root, "root");
    SceneNode.set_X(root, 100);
    SceneNode.set_Y(root, 200);

    // Add children
    var child1 = SceneNode.New();
    SceneNode.set_Name(child1, "child1");
    SceneNode.set_X(child1, 10);
    SceneNode.set_Y(child1, 20);
    root.AddChild(child1);

    var child2 = SceneNode.New();
    SceneNode.set_Name(child2, "child2");
    SceneNode.set_X(child2, 50);
    SceneNode.set_Y(child2, 60);
    root.AddChild(child2);

    // World coordinates (parent + child)
    SayInt(child1.WorldX);  // 110
    SayInt(child1.WorldY);  // 220

    // Find by name
    var found = root.Find("child2");
    Say(SceneNode.get_Name(found));  // child2

    // Transform inheritance
    root.SetScale(200);
    SayInt(child1.WorldScaleX);  // 200

    // Hierarchy management
    root.RemoveChild(child2);
    SayInt(root.ChildCount);  // 1
    child1.Detach();
    SayInt(root.ChildCount);  // 0
}
```

### Example

```basic
' Create sprites
DIM bodySprite AS Viper.Graphics.Sprite
DIM armSprite AS Viper.Graphics.Sprite
bodySprite = Viper.Graphics.Sprite.FromFile("body.bmp")
armSprite = Viper.Graphics.Sprite.FromFile("arm.bmp")

' Create scene nodes
DIM body AS Viper.Graphics.SceneNode
DIM arm AS Viper.Graphics.SceneNode
body = Viper.Graphics.SceneNode.FromSprite(bodySprite)
arm = Viper.Graphics.SceneNode.FromSprite(armSprite)

' Build hierarchy - arm is child of body
body.AddChild(arm)

' Name nodes for later lookup
body.Name = "body"
arm.Name = "arm"

' Position arm relative to body
arm.X = 20  ' 20 pixels right of body origin
arm.Y = -10 ' 10 pixels above body origin

' When body moves/rotates, arm follows automatically
body.X = 100
body.Y = 200
body.Rotation = 45  ' Arm rotates with body

' Arm inherits transforms - its world position is computed automatically
PRINT "Arm world position: "; arm.WorldX; ", "; arm.WorldY

' Find a descendant by name
DIM found AS Viper.Graphics.SceneNode
found = body.Find("arm")

' Draw entire hierarchy to canvas
body.Draw(canvas)

' Detach arm from body
arm.Detach()
```

---

## Viper.Graphics.Scene

Root container for a scene graph. Manages rendering order and provides scene-level operations.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.Scene()`

### Properties

| Property    | Type    | Access | Description                                |
|-------------|---------|--------|--------------------------------------------|
| `Root`      | Object  | Read   | The root SceneNode of the scene            |
| `NodeCount` | Integer | Read   | Number of root-level nodes                 |

### Methods

| Method                           | Signature                      | Description                                    |
|----------------------------------|--------------------------------|------------------------------------------------|
| `Add(node)`                      | `Void(SceneNode)`              | Add a root-level node to the scene             |
| `Clear()`                        | `Void()`                       | Remove all nodes                               |
| `Draw(canvas)`                   | `Void(Canvas)`                 | Render all visible nodes to canvas             |
| `DrawWithCamera(canvas, camera)` | `Void(Canvas, Camera)`         | Render all visible nodes with camera transform |
| `Find(name)`                     | `SceneNode(String)`            | Find a node by name                            |
| `Remove(node)`                   | `Void(SceneNode)`              | Remove a node from the scene                   |
| `Update()`                       | `Void()`                       | Update all nodes (advances animations)         |

### Zia Example

```zia
module SceneDemo;

bind Viper.Graphics;
bind Viper.Terminal;

func start() {
    var scene = Scene.New();

    // Add nodes
    var player = SceneNode.New();
    SceneNode.set_Name(player, "player");
    SceneNode.set_X(player, 100);
    SceneNode.set_Depth(player, 50);

    var bg = SceneNode.New();
    SceneNode.set_Name(bg, "background");
    SceneNode.set_Depth(bg, 0);

    scene.Add(player);
    scene.Add(bg);

    // Access root
    var root = scene.Root;
    SayInt(SceneNode.get_ChildCount(root));  // 2

    // Find by name
    var found = scene.Find("player");
    SayInt(SceneNode.get_X(found));  // 100

    // Update and manage
    scene.Update();
    scene.Remove(bg);
    scene.Clear();
}
```

### Example

```basic
' Create a scene
DIM scene AS Viper.Graphics.Scene
scene = NEW Viper.Graphics.Scene()

' Create game objects as scene nodes
DIM background AS Viper.Graphics.SceneNode
DIM player AS Viper.Graphics.SceneNode
DIM foreground AS Viper.Graphics.SceneNode

background = Viper.Graphics.SceneNode.FromSprite(bgSprite)
player = Viper.Graphics.SceneNode.FromSprite(playerSprite)
foreground = Viper.Graphics.SceneNode.FromSprite(fgSprite)

' Set depth (lower = rendered first/behind)
background.Depth = 0
player.Depth = 50
foreground.Depth = 100

' Add to scene
scene.Add(background)
scene.Add(player)
scene.Add(foreground)

' Game loop
DO WHILE canvas.ShouldClose = 0
    canvas.Poll()
    canvas.Clear(&H000000)

    ' Update player position
    player.X = playerX
    player.Y = playerY

    ' Update animations
    scene.Update()

    ' Render entire scene
    scene.Draw(canvas)

    canvas.Flip()
LOOP
```

### Hierarchical Character Example

```basic
' Build a character with multiple parts
DIM character AS Viper.Graphics.SceneNode
DIM head AS Viper.Graphics.SceneNode
DIM body AS Viper.Graphics.SceneNode
DIM leftArm AS Viper.Graphics.SceneNode
DIM rightArm AS Viper.Graphics.SceneNode

' Create nodes
character = NEW Viper.Graphics.SceneNode()  ' Empty parent node
body = Viper.Graphics.SceneNode.FromSprite(bodySprite)
head = Viper.Graphics.SceneNode.FromSprite(headSprite)
leftArm = Viper.Graphics.SceneNode.FromSprite(armSprite)
rightArm = Viper.Graphics.SceneNode.FromSprite(armSprite)

' Build hierarchy
character.AddChild(body)
body.AddChild(head)
body.AddChild(leftArm)
body.AddChild(rightArm)

' Position parts relative to body
head.Y = -40
leftArm.X = -25
leftArm.Y = -10
rightArm.X = 25
rightArm.Y = -10

' Add character to scene
scene.Add(character)

' Moving/rotating the character moves all parts
character.X = 400
character.Y = 300
character.Rotation = 15  ' Entire character tilts
```

---

## Viper.Graphics.SpriteBatch

Batched sprite rendering for improved performance when drawing many sprites.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.SpriteBatch(capacity)`

Creates a sprite batch with the given initial capacity (use 0 for default). SpriteBatch collects draw calls and renders them efficiently in a single batch. Use this when drawing many sprites (particles, bullets, tiles) for better performance.

### Properties

| Property    | Type    | Access | Description                                      |
|-------------|---------|--------|--------------------------------------------------|
| `Count`     | Integer | Read   | Number of sprites currently in the batch         |
| `Capacity`  | Integer | Read   | Current capacity of the batch                    |
| `IsActive`  | Integer | Read   | Non-zero if the batch is currently recording     |

### Methods

| Method                                          | Signature                                              | Description                                    |
|-------------------------------------------------|--------------------------------------------------------|------------------------------------------------|
| `Begin()`                                       | `Void()`                                               | Begin batch - call before drawing              |
| `Draw(sprite, x, y)`                            | `Void(Sprite, Integer, Integer)`                       | Draw sprite at position                        |
| `DrawEx(sprite, x, y, scaleX, scaleY, rotation)`| `Void(Sprite, Integer, Integer, Integer, Integer, Integer)` | Draw with full transform              |
| `DrawPixels(pixels, x, y)`                      | `Void(Pixels, Integer, Integer)`                       | Draw pixels buffer at position                 |
| `DrawRegion(pixels, dx, dy, sx, sy, sw, sh)`    | `Void(Pixels, Integer...)`                             | Draw a sub-region of a Pixels buffer           |
| `DrawScaled(sprite, x, y, scale)`               | `Void(Sprite, Integer, Integer, Integer)`              | Draw sprite with uniform scale (100 = 100%)    |
| `End(canvas)`                                   | `Void(Canvas)`                                         | End batch and flush all draws to the canvas    |
| `ResetSettings()`                               | `Void()`                                               | Clear all settings to defaults                 |
| `SetAlpha(alpha)`                               | `Void(Integer)`                                        | Set global alpha (0-255) for all sprites       |
| `SetSortByDepth(enabled)`                       | `Void(Integer)`                                        | Enable/disable depth sorting (1=on, 0=off)     |
| `SetTint(color)`                                | `Void(Integer)`                                        | Set tint color (ARGB) for all sprites          |

### Zia Example

```zia
module SpriteBatchDemo;

bind Viper.Graphics;
bind Viper.Terminal;

func start() {
    var batch = SpriteBatch.New(64);
    SayInt(batch.Count);       // 0
    SayInt(batch.Capacity);    // 64
    SayBool(batch.IsActive);   // false

    // Begin a batch
    batch.Begin();
    SayBool(batch.IsActive);  // true

    // Draw sprites
    var px = Pixels.New(16, 16);
    px.Fill(Color.RGB(255, 0, 0));
    batch.DrawPixels(px, 10, 20);
    batch.DrawPixels(px, 30, 40);
    batch.DrawPixels(px, 50, 60);
    SayInt(batch.Count);  // 3

    // Rendering settings
    batch.SetSortByDepth(true);
    batch.SetTint(Color.RGBA(255, 0, 0, 128));
    batch.SetAlpha(200);
    batch.ResetSettings();
}
```

### Example

```basic
' Create sprite batch with default capacity
DIM batch AS Viper.Graphics.SpriteBatch
batch = NEW Viper.Graphics.SpriteBatch(0)

' Load sprites
DIM bulletSprite AS Viper.Graphics.Sprite
bulletSprite = Viper.Graphics.Sprite.FromFile("bullet.bmp")

' Array of bullet positions
DIM bulletsX(50) AS INTEGER
DIM bulletsY(50) AS INTEGER

' Game loop
DO WHILE canvas.ShouldClose = 0
    canvas.Poll()
    canvas.Clear(&H000000)

    ' Begin batched rendering
    batch.Begin()

    ' Draw all bullets efficiently
    FOR i = 0 TO 49
        batch.Draw(bulletSprite, bulletsX(i), bulletsY(i))
    NEXT i

    ' End batch - all sprites rendered to canvas in one go
    batch.End(canvas)

    canvas.Flip()
LOOP
```

### Particle System Example

```basic
' Create a simple particle system using SpriteBatch
DIM batch AS Viper.Graphics.SpriteBatch
batch = NEW Viper.Graphics.SpriteBatch(512)  ' Pre-allocate for 512 sprites
batch.SetSortByDepth(1)  ' Sort particles by depth

DIM particleSprite AS Viper.Graphics.Sprite
particleSprite = Viper.Graphics.Sprite.FromFile("particle.bmp")

' Particle data arrays
DIM px(500) AS INTEGER   ' X positions
DIM py(500) AS INTEGER   ' Y positions
DIM pa(500) AS INTEGER   ' Alpha (0-255)

' Render particles
SUB RenderParticles()
    batch.Begin()

    FOR i = 0 TO 499
        IF pa(i) > 0 THEN
            ' Set alpha for this particle
            batch.SetAlpha(pa(i))
            batch.Draw(particleSprite, px(i), py(i))
        END IF
    NEXT i

    batch.End(canvas)
END SUB
```

### Tinting Example

```basic
' Create batch
DIM batch AS Viper.Graphics.SpriteBatch
batch = NEW Viper.Graphics.SpriteBatch(0)

DIM sprite AS Viper.Graphics.Sprite
sprite = Viper.Graphics.Sprite.FromFile("enemy.bmp")

batch.Begin()

' Draw normal sprite
batch.Draw(sprite, 100, 100)

' Draw with red tint (damaged enemy)
batch.SetTint(&HFFFF0000)
batch.Draw(sprite, 200, 100)

' Draw with blue tint
batch.SetTint(&HFF0000FF)
batch.Draw(sprite, 300, 100)

' Reset all settings to defaults
batch.ResetSettings()
batch.Draw(sprite, 400, 100)

batch.End(canvas)
```

### Performance Tips

- **Batch similar sprites:** Draw sprites that share the same texture together
- **Minimize Begin/End calls:** Each Begin/End pair flushes the batch
- **Use depth sorting wisely:** Disable `SetSortByDepth` when not needed for faster rendering
- **Pre-allocate batches:** Create SpriteBatch once with sufficient capacity, reuse each frame

---

## Viper.Game.SpriteAnimation

Frame-based sprite animation controller. Use alongside `Viper.Graphics.Sprite` to drive animated sprites with configurable speed, looping, and ping-pong modes.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Game.SpriteAnimation()`

### Properties

| Property       | Type    | Access | Description                                              |
|----------------|---------|--------|----------------------------------------------------------|
| `Frame`        | Integer | R/W    | Current frame index                                      |
| `FrameCount`   | Integer | Read   | Total number of frames (set via `Setup`)                 |
| `FrameDuration`| Integer | R/W    | Duration of each frame in milliseconds                   |
| `IsPlaying`    | Boolean | Read   | True if animation is currently playing                   |
| `IsPaused`     | Boolean | Read   | True if animation is paused                              |
| `IsFinished`   | Boolean | Read   | True if non-looping animation reached the last frame     |
| `Progress`     | Integer | Read   | Playback progress 0–100 (percent complete)               |
| `Speed`        | Double  | R/W    | Playback speed multiplier (1.0 = normal, 2.0 = double)   |
| `Loop`         | Boolean | R/W    | Loop when last frame is reached (default: true)          |
| `PingPong`     | Boolean | R/W    | Reverse direction at end instead of restarting           |
| `FrameChanged` | Boolean | Read   | True if frame advanced on the last `Update()` call       |

### Methods

| Method                            | Signature                               | Description                                        |
|-----------------------------------|-----------------------------------------|----------------------------------------------------|
| `Setup(startFrame, endFrame, fps)` | `Void(Integer, Integer, Integer)`      | Configure frame range and playback speed            |
| `Play()`                          | `Void()`                                | Start or resume playback                           |
| `Stop()`                          | `Void()`                                | Stop and reset to first frame                      |
| `Pause()`                         | `Void()`                                | Pause at the current frame                         |
| `Resume()`                        | `Void()`                                | Resume from the paused frame                       |
| `Reset()`                         | `Void()`                                | Reset to first frame without stopping              |
| `Update()`                        | `Boolean()`                             | Advance animation by one frame tick; returns true if frame changed |
| `Destroy()`                       | `Void()`                                | Free the animator                                  |

### Zia Example

```zia
module AnimDemo;

bind Viper.Terminal;
bind Viper.Graphics.Sprite as Sprite;
bind Viper.Graphics.Canvas as Canvas;
bind Viper.Game.SpriteAnimation as Anim;

func start() {
    var canvas = Canvas.New("Sprite Animation", 400, 300);
    var hero = Sprite.FromFile("hero_sheet.bmp");

    var walk = Anim.New();
    walk.Setup(0, 7, 12);  // frames 0-7 at 12 FPS
    walk.set_Loop(true);
    walk.Play();

    while canvas.get_ShouldClose() == 0 {
        canvas.Poll();
        canvas.Clear(0x000000);

        if walk.Update() == true {
            hero.set_Frame(walk.get_Frame());
        }
        hero.Draw(canvas);
        canvas.Flip();
    }
}
```

### Notes

- Call `Update()` once per frame in your game loop; it advances the frame timer and returns `true` when the visible frame changes.
- `FrameChanged` is a convenience flag — equivalent to the `Update()` return value — useful when calling `Update()` elsewhere but checking in a different location.
- `PingPong` and `Loop` are independent: setting both causes the animation to reverse at the end and loop indefinitely; `PingPong` alone (no `Loop`) plays forward then backward once.

---

## See Also

- [Input](input.md) - `Keyboard`, `Mouse`, and `Pad` for interactive input with Canvas
- [Audio](audio.md) - Sound effects and music for games
- [Mathematics](math.md) - `Vec2` and `Vec3` for 2D/3D graphics calculations
- [Collections](collections.md) - `Bytes` for pixel data serialization

