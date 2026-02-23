# Scene Graph
> SceneNode, Scene, SpriteBatch, Camera, SpriteAnimation

**Part of [Viper Runtime Library](../README.md) › [Graphics](README.md)**

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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
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

- [Canvas & Color](canvas.md)
- [Images & Sprites](pixels.md)
- [Graphics Overview](README.md)
- [Viper Runtime Library](../README.md)
