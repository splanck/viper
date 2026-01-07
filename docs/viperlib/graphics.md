# Graphics

> 2D graphics, colors, and image manipulation.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Graphics.Canvas](#vipergraphicscanvas)
- [Viper.Graphics.Color](#vipergraphicscolor)
- [Viper.Graphics.Pixels](#vipergraphicspixels)

---

## Viper.Graphics.Canvas

2D graphics canvas for visual applications and games.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.Canvas(title, width, height)`

### Properties

| Property      | Type    | Description                                        |
|---------------|---------|----------------------------------------------------|
| `Width`       | Integer | Canvas width in pixels                             |
| `Height`      | Integer | Canvas height in pixels                            |
| `ShouldClose` | Integer | Non-zero if the user requested to close the canvas |

### Methods

| Method                                | Signature                             | Description                                                |
|---------------------------------------|---------------------------------------|------------------------------------------------------------|
| `Flip()`                              | `Void()`                              | Presents the back buffer and displays drawn content        |
| `Clear(color)`                        | `Void(Integer)`                       | Clears the canvas with a solid color                       |
| `Line(x1, y1, x2, y2, color)`         | `Void(Integer...)`                    | Draws a line between two points                            |
| `Box(x, y, w, h, color)`              | `Void(Integer...)`                    | Draws a filled rectangle                                   |
| `Frame(x, y, w, h, color)`            | `Void(Integer...)`                    | Draws a rectangle outline                                  |
| `Disc(cx, cy, r, color)`              | `Void(Integer...)`                    | Draws a filled circle                                      |
| `Ring(cx, cy, r, color)`              | `Void(Integer...)`                    | Draws a circle outline                                     |
| `Plot(x, y, color)`                   | `Void(Integer, Integer, Integer)`     | Sets a single pixel                                        |
| `Poll()`                              | `Integer()`                           | Polls for input events; returns event type (0 = none)      |
| `KeyHeld(keycode)`                    | `Integer(Integer)`                    | Returns non-zero if the specified key is held down         |
| `Text(x, y, text, color)`             | `Void(Integer, Integer, String, Integer)` | Draws text at (x, y) with the specified color          |
| `TextBg(x, y, text, fg, bg)`          | `Void(Integer, Integer, String, Integer, Integer)` | Draws text with foreground and background colors |
| `Blit(x, y, pixels)`                  | `Void(Integer, Integer, Pixels)`      | Blits a Pixels buffer to the canvas at (x, y)              |
| `BlitRegion(dx, dy, pixels, sx, sy, w, h)` | `Void(Integer...)`               | Blits a region of a Pixels buffer to the canvas            |
| `BlitAlpha(x, y, pixels)`             | `Void(Integer, Integer, Pixels)`      | Blits with alpha blending (respects alpha channel)         |
| `ThickLine(x1, y1, x2, y2, thickness, color)` | `Void(Integer...)`            | Draws a line with specified thickness (rounded caps)       |
| `RoundBox(x, y, w, h, radius, color)` | `Void(Integer...)`                    | Draws a filled rectangle with rounded corners              |
| `RoundFrame(x, y, w, h, radius, color)` | `Void(Integer...)`                  | Draws a rectangle outline with rounded corners             |
| `FloodFill(x, y, color)`              | `Void(Integer, Integer, Integer)`     | Flood fills connected area starting at (x, y)              |
| `Triangle(x1, y1, x2, y2, x3, y3, color)` | `Void(Integer...)`                 | Draws a filled triangle                                    |
| `TriangleFrame(x1, y1, x2, y2, x3, y3, color)` | `Void(Integer...)`            | Draws a triangle outline                                   |
| `Ellipse(cx, cy, rx, ry, color)`      | `Void(Integer...)`                    | Draws a filled ellipse                                     |
| `EllipseFrame(cx, cy, rx, ry, color)` | `Void(Integer...)`                    | Draws an ellipse outline                                   |
| `Arc(cx, cy, radius, startAngle, endAngle, color)` | `Void(Integer...)`         | Draws a filled arc (pie slice)                             |
| `ArcFrame(cx, cy, radius, startAngle, endAngle, color)` | `Void(Integer...)`    | Draws an arc outline                                       |
| `Bezier(x1, y1, cx, cy, x2, y2, color)` | `Void(Integer...)`                  | Draws a quadratic Bezier curve                             |
| `Polyline(points, count, color)`      | `Void(Pointer, Integer, Integer)`     | Draws connected line segments                              |
| `Polygon(points, count, color)`       | `Void(Pointer, Integer, Integer)`     | Draws a filled polygon                                     |
| `PolygonFrame(points, count, color)`  | `Void(Pointer, Integer, Integer)`     | Draws a polygon outline                                    |
| `GetPixel(x, y)`                      | `Integer(Integer, Integer)`           | Gets pixel color at (x, y)                                 |
| `CopyRect(x, y, w, h)`                | `Pixels(Integer...)`                  | Copies canvas region to a Pixels buffer                    |
| `SaveBmp(path)`                       | `Integer(String)`                     | Saves canvas to BMP file (returns 1 on success)            |

### Color Format

Colors are specified as 32-bit integers in `0x00RRGGBB` format:

- Red: `0x00FF0000`
- Green: `0x0000FF00`
- Blue: `0x000000FF`
- White: `0x00FFFFFF`
- Black: `0x00000000`

Use `Viper.Graphics.Color.RGB()` or `Viper.Graphics.Color.RGBA()` to create colors from components.

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

Read pixels, copy regions, and save screenshots:

```basic
' Get a pixel color from the canvas
DIM color AS INTEGER
color = canvas.GetPixel(100, 100)

' Copy a rectangular region from canvas to a Pixels buffer
DIM screenshot AS Viper.Graphics.Pixels
screenshot = canvas.CopyRect(0, 0, 800, 600)

' Save the entire canvas to a BMP file
DIM success AS INTEGER
success = canvas.SaveBmp("screenshot.bmp")
IF success = 1 THEN
    PRINT "Screenshot saved!"
END IF
```

---

## Viper.Graphics.Color

Color utility functions for graphics operations.

**Type:** Static utility class

### Methods

| Method             | Signature                                     | Description                                                                     |
|--------------------|-----------------------------------------------|---------------------------------------------------------------------------------|
| `RGB(r, g, b)`     | `Integer(Integer, Integer, Integer)`          | Creates a color value from red, green, blue components (0-255 each)             |
| `RGBA(r, g, b, a)` | `Integer(Integer, Integer, Integer, Integer)` | Creates a color with alpha from red, green, blue, alpha components (0-255 each) |

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
| `Get(x, y)`                       | `Integer(Integer, Integer)`                                          | Get pixel color at (x, y) as packed RGBA (0xRRGGBBAA). Returns 0 if out of bounds |
| `Set(x, y, color)`                | `Void(Integer, Integer, Integer)`                                    | Set pixel color at (x, y). Silently ignores out of bounds                         |
| `Fill(color)`                     | `Void(Integer)`                                                      | Fill entire buffer with a color                                                   |
| `Clear()`                         | `Void()`                                                             | Clear buffer to transparent black (0x00000000)                                    |
| `Copy(dx, dy, src, sx, sy, w, h)` | `Void(Integer, Integer, Pixels, Integer, Integer, Integer, Integer)` | Copy a rectangle from source to this buffer                                       |
| `Clone()`                         | `Pixels()`                                                           | Create a deep copy of this buffer                                                 |
| `ToBytes()`                       | `Bytes()`                                                            | Convert to raw bytes (RGBA, row-major)                                            |
| `SaveBmp(path)`                   | `Integer(String)`                                                    | Save to a BMP file. Returns 1 on success, 0 on failure                            |
| `FlipH()`                         | `Pixels()`                                                           | Return a horizontally flipped copy (mirror left-right)                            |
| `FlipV()`                         | `Pixels()`                                                           | Return a vertically flipped copy (mirror top-bottom)                              |
| `RotateCW()`                      | `Pixels()`                                                           | Return a 90-degree clockwise rotated copy (swaps dimensions)                      |
| `RotateCCW()`                     | `Pixels()`                                                           | Return a 90-degree counter-clockwise rotated copy (swaps dimensions)              |
| `Rotate180()`                     | `Pixels()`                                                           | Return a 180-degree rotated copy                                                  |
| `Scale(width, height)`            | `Pixels(Integer, Integer)`                                           | Return a scaled copy using nearest-neighbor interpolation                         |

### Static Methods

| Method                            | Signature                         | Description                                           |
|-----------------------------------|-----------------------------------|-------------------------------------------------------|
| `FromBytes(width, height, bytes)` | `Pixels(Integer, Integer, Bytes)` | Create from raw bytes (RGBA, row-major)               |
| `LoadBmp(path)`                   | `Pixels(String)`                  | Load from a 24-bit BMP file. Returns null on failure  |

### Color Format

Colors are stored as packed 32-bit RGBA integers in the format `0xRRGGBBAA`:

- `RR` - Red component (0-255)
- `GG` - Green component (0-255)
- `BB` - Blue component (0-255)
- `AA` - Alpha component (0-255, where 255 = opaque)

Use `Viper.Graphics.Color.RGBA()` to create colors.

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

---

## See Also

- [Input](input.md) - `Keyboard`, `Mouse`, and `Pad` for interactive input with Canvas
- [Mathematics](math.md) - `Vec2` and `Vec3` for 2D/3D graphics calculations
- [Collections](collections.md) - `Bytes` for pixel data serialization

