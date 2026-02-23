# Images & Sprites
> Pixels, Sprite, SpriteSheet, Tilemap

**Part of [Viper Runtime Library](../README.md) › [Graphics](README.md)**

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
| `Blur(radius)`                    | `Pixels(Integer)`                                                    | Return a box-blurred copy (radius 1-10, separable horizontal+vertical passes)     |
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
| `DrawThickLine(x1, y1, x2, y2, thickness, color)` | `Void(Integer...)` | Line with thickness (parallelogram body + rounded endcap circles) |
| `DrawTriangle(x1, y1, x2, y2, x3, y3, color)` | `Void(Integer...)` | Filled triangle |
| `DrawBezier(x1, y1, cx, cy, x2, y2, color)` | `Void(Integer...)` | Quadratic Bezier curve |

> **Color format note:** `Pixels.Set(x, y, color)` and `Pixels.Get(x, y)` use `0xRRGGBBAA` (packed RGBA
> with explicit alpha). Drawing primitives and `SetRGB`/`GetRGB` use `0x00RRGGBB` — the same format as
> `Canvas` drawing calls and `Color.RGB()`. This allows the same color constants to be used when drawing
> to both a Canvas and an off-screen Pixels buffer without any format conversion.

#### Zia Example — Drawing into an off-screen buffer

```rust
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

```rust
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

```rust
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

```rust
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
| `CollideBody(body)`                            | `Integer(Object)`                            | Resolve a Physics2D.Body against solid/one-way tiles (returns 1 on collision). One-way platform detection is frame-rate independent. |
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

```rust
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

### Collision Types

`SetCollision` assigns physics behaviour to a tile ID:

| Value | Constant     | Behaviour                                                                      |
|-------|--------------|--------------------------------------------------------------------------------|
| `0`   | none         | Passthrough — no collision                                                     |
| `1`   | solid        | Full AABB collision on all four sides                                          |
| `2`   | one\_way\_up | Body lands on top only. A body coming from below passes through. Detection is frame-rate independent: collision fires only when the body's top edge is at or above the tile surface at the moment of contact. |

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


## See Also

- [Canvas & Color](canvas.md)
- [Scene Graph](scene.md)
- [Graphics Overview](README.md)
- [Viper Runtime Library](../README.md)
