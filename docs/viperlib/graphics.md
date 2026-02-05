# Graphics

> 2D graphics, colors, and image manipulation.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Graphics.Canvas](#vipergraphicscanvas)
- [Viper.Graphics.Color](#vipergraphicscolor)
- [Viper.Graphics.Pixels](#vipergraphicspixels)
- [Viper.Graphics.Sprite](#vipergraphicssprite)
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
| `Width`       | Integer | Canvas width in pixels                             |
| `Height`      | Integer | Canvas height in pixels                            |
| `ShouldClose` | Integer | Non-zero if the user requested to close the canvas |
| `IsFullscreen`| Integer | Non-zero if canvas is in fullscreen mode (read-only) |

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
| `SetClipRect(x, y, w, h)`             | `Void(Integer...)`                    | Sets clipping rectangle; all drawing is constrained to it  |
| `ClearClipRect()`                     | `Void()`                              | Clears clipping rectangle; restores full canvas drawing    |
| `SetTitle(title)`                     | `Void(String)`                        | Changes the window title at runtime                        |
| `SetFullscreen(enable)`               | `Void(Integer)`                       | Toggles fullscreen mode (1 = fullscreen, 0 = windowed)     |

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

Change the window title or toggle fullscreen mode at runtime:

```basic
' Create a canvas
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("My Game", 800, 600)

' Update window title dynamically (e.g., show FPS or game state)
canvas.SetTitle("My Game - Level 1")
canvas.SetTitle("My Game - Score: 1000")

' Toggle fullscreen with F11 key
DO WHILE canvas.ShouldClose = 0
    canvas.Poll()

    ' Toggle fullscreen on F11 press
    IF Viper.Input.Keyboard.Pressed(300) THEN  ' 300 = F11
        IF canvas.IsFullscreen THEN
            canvas.SetFullscreen(0)  ' Exit fullscreen
        ELSE
            canvas.SetFullscreen(1)  ' Enter fullscreen
        END IF
    END IF

    ' ... render game ...

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
- `IsFullscreen` property reflects current state (read-only)
- Calling `SetFullscreen` with current state has no effect

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
| `Rotate(angle)`                   | `Pixels(Float)`                                                      | Return a rotated copy at arbitrary angle (bilinear interpolation)                 |
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

' Arbitrary angle rotation (uses bilinear interpolation for smooth results)
rotated = pixels.Rotate(45.0)   ' Rotate 45 degrees clockwise
rotated = pixels.Rotate(-30.0)  ' Rotate 30 degrees counter-clockwise
' Note: Output dimensions expand to fit the rotated image without clipping

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
- Rotate(angle) uses bilinear interpolation for smooth results at any angle
- Rotate(angle) expands output dimensions to fit rotated image without clipping
- Scale uses nearest-neighbor interpolation (fast, no blending)

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
| `X`          | Integer | R/W    | X position in pixels                         |
| `Y`          | Integer | R/W    | Y position in pixels                         |
| `Width`      | Integer | Read   | Width of current frame in pixels             |
| `Height`     | Integer | Read   | Height of current frame in pixels            |
| `ScaleX`     | Integer | R/W    | Horizontal scale (100 = 100%)                |
| `ScaleY`     | Integer | R/W    | Vertical scale (100 = 100%)                  |
| `Rotation`   | Integer | R/W    | Rotation in degrees                          |
| `Visible`    | Integer | R/W    | Visibility (1 = visible, 0 = hidden)         |
| `Frame`      | Integer | R/W    | Current animation frame index                |
| `FrameCount` | Integer | Read   | Total number of animation frames             |

### Methods

| Method                      | Signature                          | Description                                           |
|-----------------------------|------------------------------------|-------------------------------------------------------|
| `Draw(canvas)`              | `Void(Canvas)`                     | Draw the sprite to a canvas                           |
| `SetOrigin(x, y)`           | `Void(Integer, Integer)`           | Set origin point for rotation/scaling                 |
| `AddFrame(pixels)`          | `Void(Pixels)`                     | Add an animation frame                                |
| `SetFrameDelay(ms)`         | `Void(Integer)`                    | Set delay between animation frames (milliseconds)     |
| `Update()`                  | `Void()`                           | Advance animation (call each frame)                   |
| `Overlaps(other)`           | `Integer(Sprite)`                  | Check bounding box overlap (returns 1 or 0)           |
| `Contains(x, y)`            | `Integer(Integer, Integer)`        | Check if point is inside sprite (returns 1 or 0)      |
| `Move(dx, dy)`              | `Void(Integer, Integer)`           | Move sprite by delta amounts                          |

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
| `SetTileset(pixels)`                           | `Void(Pixels)`                               | Set the tileset image (tiles arranged in grid)        |
| `SetTile(x, y, index)`                         | `Void(Integer, Integer, Integer)`            | Set tile at position (0 = empty)                      |
| `GetTile(x, y)`                                | `Integer(Integer, Integer)`                  | Get tile index at position                            |
| `Fill(index)`                                  | `Void(Integer)`                              | Fill entire map with a tile                           |
| `Clear()`                                      | `Void()`                                     | Clear map (set all to 0)                              |
| `FillRect(x, y, w, h, index)`                  | `Void(Integer...)`                           | Fill rectangular region                               |
| `Draw(canvas, offsetX, offsetY)`               | `Void(Canvas, Integer, Integer)`             | Draw tilemap with scroll offset                       |
| `DrawRegion(canvas, ox, oy, vx, vy, vw, vh)`   | `Void(Canvas, Integer...)`                   | Draw only visible region (for optimization)           |
| `ToTileX(pixelX)`                              | `Integer(Integer)`                           | Convert pixel X to tile X                             |
| `ToTileY(pixelY)`                              | `Integer(Integer)`                           | Convert pixel Y to tile Y                             |
| `ToPixelX(tileX)`                              | `Integer(Integer)`                           | Convert tile X to pixel X                             |
| `ToPixelY(tileY)`                              | `Integer(Integer)`                           | Convert tile Y to pixel Y                             |

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
| `Follow(x, y)`                   | `Void(Integer, Integer)`               | Center camera on world position                  |
| `ToScreenX(worldX)`              | `Integer(Integer)`                     | Convert world X to screen X                      |
| `ToScreenY(worldY)`              | `Integer(Integer)`                     | Convert world Y to screen Y                      |
| `ToWorldX(screenX)`              | `Integer(Integer)`                     | Convert screen X to world X                      |
| `ToWorldY(screenY)`              | `Integer(Integer)`                     | Convert screen Y to world Y                      |
| `Move(dx, dy)`                   | `Void(Integer, Integer)`               | Move camera by delta amounts                     |
| `SetBounds(minX, minY, maxX, maxY)` | `Void(Integer, Integer, Integer, Integer)` | Limit camera movement range         |
| `ClearBounds()`                  | `Void()`                               | Remove camera bounds                             |

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
**Constructor:** `NEW Viper.Graphics.SceneNode(sprite)`

Creates a scene node wrapping a sprite. Scene nodes support parent-child hierarchies where child transforms are relative to their parent.

### Properties

| Property   | Type    | Access | Description                                    |
|------------|---------|--------|------------------------------------------------|
| `X`        | Double  | R/W    | Local X position (relative to parent)          |
| `Y`        | Double  | R/W    | Local Y position (relative to parent)          |
| `ScaleX`   | Double  | R/W    | Local X scale (1.0 = 100%)                     |
| `ScaleY`   | Double  | R/W    | Local Y scale (1.0 = 100%)                     |
| `Rotation` | Double  | R/W    | Local rotation in degrees                      |
| `Visible`  | Integer | R/W    | Visibility (1=visible, 0=hidden)               |
| `Depth`    | Double  | R/W    | Z-order for depth sorting (higher = on top)    |
| `Parent`   | Object  | Read   | Parent scene node (NULL if root)               |

### Methods

| Method                     | Signature                      | Description                                    |
|----------------------------|--------------------------------|------------------------------------------------|
| `AddChild(child)`          | `Void(SceneNode)`              | Add a child node                               |
| `RemoveChild(child)`       | `Void(SceneNode)`              | Remove a child node                            |
| `GetChildCount()`          | `Integer()`                    | Get number of direct children                  |
| `GetChild(index)`          | `SceneNode(Integer)`           | Get child by index                             |
| `WorldX()`                 | `Double()`                     | Get computed world X position                  |
| `WorldY()`                 | `Double()`                     | Get computed world Y position                  |
| `WorldScaleX()`            | `Double()`                     | Get computed world X scale                     |
| `WorldScaleY()`            | `Double()`                     | Get computed world Y scale                     |
| `WorldRotation()`          | `Double()`                     | Get computed world rotation                    |
| `SetSprite(sprite)`        | `Void(Sprite)`                 | Set the sprite for this node                   |
| `GetSprite()`              | `Sprite()`                     | Get the sprite                                 |

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
body = NEW Viper.Graphics.SceneNode(bodySprite)
arm = NEW Viper.Graphics.SceneNode(armSprite)

' Build hierarchy - arm is child of body
body.AddChild(arm)

' Position arm relative to body
arm.X = 20  ' 20 pixels right of body origin
arm.Y = -10 ' 10 pixels above body origin

' When body moves/rotates, arm follows automatically
body.X = 100
body.Y = 200
body.Rotation = 45  ' Arm rotates with body

' Arm inherits transforms - its world position is computed automatically
PRINT "Arm world position: "; arm.WorldX(); ", "; arm.WorldY()
```

---

## Viper.Graphics.Scene

Root container for a scene graph. Manages rendering order and provides scene-level operations.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.Scene()`

### Properties

| Property       | Type    | Access | Description                                |
|----------------|---------|--------|--------------------------------------------|
| `DepthSort`    | Integer | R/W    | Enable depth sorting (1=enabled, 0=disabled) |

### Methods

| Method                     | Signature                      | Description                                    |
|----------------------------|--------------------------------|------------------------------------------------|
| `AddNode(node)`            | `Void(SceneNode)`              | Add a root-level node to the scene             |
| `RemoveNode(node)`         | `Void(SceneNode)`              | Remove a node from the scene                   |
| `Clear()`                  | `Void()`                       | Remove all nodes                               |
| `GetNodeCount()`           | `Integer()`                    | Get number of root nodes                       |
| `Draw(canvas)`             | `Void(Canvas)`                 | Render all visible nodes to canvas             |
| `FindNode(name)`           | `SceneNode(String)`            | Find a node by name (if named)                 |

### Example

```basic
' Create a scene
DIM scene AS Viper.Graphics.Scene
scene = NEW Viper.Graphics.Scene()
scene.DepthSort = 1  ' Enable depth sorting

' Create game objects as scene nodes
DIM background AS Viper.Graphics.SceneNode
DIM player AS Viper.Graphics.SceneNode
DIM foreground AS Viper.Graphics.SceneNode

background = NEW Viper.Graphics.SceneNode(bgSprite)
player = NEW Viper.Graphics.SceneNode(playerSprite)
foreground = NEW Viper.Graphics.SceneNode(fgSprite)

' Set depth (lower = rendered first/behind)
background.Depth = 0
player.Depth = 50
foreground.Depth = 100

' Add to scene
scene.AddNode(background)
scene.AddNode(player)
scene.AddNode(foreground)

' Game loop
DO WHILE canvas.ShouldClose = 0
    canvas.Poll()
    canvas.Clear(&H000000)

    ' Update player position
    player.X = playerX
    player.Y = playerY

    ' Render entire scene (depth-sorted)
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
character = NEW Viper.Graphics.SceneNode(NULL)  ' Empty parent node
body = NEW Viper.Graphics.SceneNode(bodySprite)
head = NEW Viper.Graphics.SceneNode(headSprite)
leftArm = NEW Viper.Graphics.SceneNode(armSprite)
rightArm = NEW Viper.Graphics.SceneNode(armSprite)

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
scene.AddNode(character)

' Moving/rotating the character moves all parts
character.X = 400
character.Y = 300
character.Rotation = 15  ' Entire character tilts
```

---

## Viper.Graphics.SpriteBatch

Batched sprite rendering for improved performance when drawing many sprites.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.SpriteBatch(canvas)`

SpriteBatch collects draw calls and renders them efficiently in a single batch. Use this when drawing many sprites (particles, bullets, tiles) for better performance.

### Properties

| Property   | Type    | Access | Description                                |
|------------|---------|--------|--------------------------------------------|
| `DepthSort`| Integer | R/W    | Enable depth sorting (1=enabled, 0=disabled) |
| `Tint`     | Integer | R/W    | Tint color applied to all sprites (ARGB)   |
| `Alpha`    | Double  | R/W    | Global alpha multiplier (0.0-1.0)          |

### Methods

| Method                                          | Signature                                    | Description                                    |
|-------------------------------------------------|----------------------------------------------|------------------------------------------------|
| `Begin()`                                       | `Void()`                                     | Begin batch - call before drawing              |
| `End()`                                         | `Void()`                                     | End batch and flush all draws to canvas        |
| `Draw(sprite, x, y)`                            | `Void(Sprite, Double, Double)`               | Draw sprite at position                        |
| `DrawEx(sprite, x, y, scaleX, scaleY, rotation)`| `Void(Sprite, Double, Double, Double, Double, Double)` | Draw with transform             |
| `DrawPixels(pixels, x, y)`                      | `Void(Pixels, Double, Double)`               | Draw pixels buffer at position                 |
| `DrawPixelsEx(pixels, x, y, scaleX, scaleY, rotation)` | `Void(Pixels, Double...)`             | Draw pixels with transform                     |
| `DrawDepth(sprite, x, y, depth)`                | `Void(Sprite, Double, Double, Double)`       | Draw with explicit depth value                 |
| `SetTint(color)`                                | `Void(Integer)`                              | Set tint color (ARGB)                          |
| `SetAlpha(alpha)`                               | `Void(Double)`                               | Set global alpha (0.0-1.0)                     |
| `Clear()`                                       | `Void()`                                     | Clear batch without rendering                  |
| `GetCount()`                                    | `Integer()`                                  | Get number of pending draws                    |

### Example

```basic
' Create sprite batch for the canvas
DIM batch AS Viper.Graphics.SpriteBatch
batch = NEW Viper.Graphics.SpriteBatch(canvas)

' Load sprites
DIM bulletSprite AS Viper.Graphics.Sprite
bulletSprite = Viper.Graphics.Sprite.FromFile("bullet.bmp")

' Array of bullet positions
DIM bullets(100) AS DOUBLE  ' x, y pairs

' Game loop
DO WHILE canvas.ShouldClose = 0
    canvas.Poll()
    canvas.Clear(&H000000)

    ' Begin batched rendering
    batch.Begin()

    ' Draw all bullets efficiently
    FOR i = 0 TO 49
        batch.Draw(bulletSprite, bullets(i * 2), bullets(i * 2 + 1))
    NEXT i

    ' End batch - all sprites rendered in one go
    batch.End()

    canvas.Flip()
LOOP
```

### Particle System Example

```basic
' Create a simple particle system using SpriteBatch
DIM batch AS Viper.Graphics.SpriteBatch
batch = NEW Viper.Graphics.SpriteBatch(canvas)
batch.DepthSort = 1  ' Sort particles by depth

DIM particleSprite AS Viper.Graphics.Sprite
particleSprite = Viper.Graphics.Sprite.FromFile("particle.bmp")

' Particle data arrays
DIM px(500) AS DOUBLE   ' X positions
DIM py(500) AS DOUBLE   ' Y positions
DIM pz(500) AS DOUBLE   ' Depth
DIM pa(500) AS DOUBLE   ' Alpha

' Render particles
SUB RenderParticles()
    batch.Begin()

    FOR i = 0 TO 499
        IF pa(i) > 0 THEN
            ' Set alpha for this particle
            batch.SetAlpha(pa(i))
            batch.DrawDepth(particleSprite, px(i), py(i), pz(i))
        END IF
    NEXT i

    batch.End()
END SUB
```

### Tinting Example

```basic
' Create batch
DIM batch AS Viper.Graphics.SpriteBatch
batch = NEW Viper.Graphics.SpriteBatch(canvas)

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

' Reset to no tint
batch.SetTint(&HFFFFFFFF)
batch.Draw(sprite, 400, 100)

batch.End()
```

### Performance Tips

- **Batch similar sprites:** Draw sprites that share the same texture together
- **Minimize Begin/End calls:** Each Begin/End pair flushes the batch
- **Use depth sorting wisely:** Disable when not needed for faster rendering
- **Pre-allocate batches:** Create SpriteBatch once, reuse each frame

---

## See Also

- [Input](input.md) - `Keyboard`, `Mouse`, and `Pad` for interactive input with Canvas
- [Audio](audio.md) - Sound effects and music for games
- [Mathematics](math.md) - `Vec2` and `Vec3` for 2D/3D graphics calculations
- [Collections](collections.md) - `Bytes` for pixel data serialization

