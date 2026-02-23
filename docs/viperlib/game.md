# Game

> 2D game primitives: physics, scenes, cameras, tilemaps, particles, animations, pathfinding, and more.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Game.Camera](#vipergamecamera)
- [Viper.Game.Scene](#vipergamescene)
- [Viper.Game.Tilemap](#vipergametilemap)
- [Viper.Game.Physics2D](#vipergamephysics2d)
- [Viper.Game.Quadtree](#vipergamequadtree)
- [Viper.Game.Particle](#vipergameparticle)
- [Viper.Game.SpriteBatch](#vipergamespritebatch)
- [Viper.Game.SpriteAnim](#vipergamespriteanim)
- [Viper.Game.StateMachine](#vipergamestatemachine)
- [Viper.Game.ObjectPool](#vipergameobjectpool)
- [Viper.Game.PathFollower](#vipergamepathfollower)
- [Viper.Game.Timer](#vipergametimer)
- [Viper.Game.Tween / TweenChain](#vipergametween--tweenchain)
- [Viper.Game.SmoothValue](#vipergamesmoothvalue)
- [Viper.Game.ButtonGroup](#vipergamebuttongroup)
- [Viper.Game.ScreenFX](#vipergamescreenfx)
- [Viper.Game.Collision](#vipergamecollision)
- [Viper.Game.Grid2D](#vipergamegrid2d)
- [Current Limits](#current-limits)
- [Coordinate Systems](#coordinate-systems)

---

## Viper.Game.Camera

A 2D camera that maps a world-coordinate viewport to screen space. Supports position, zoom,
rotation, scroll bounds, visibility culling, and a dirty flag for skipping redundant redraws.

**Type:** Instance (obj)
**Constructor:** `Camera.New(width, height)`

### Properties

| Property   | Type    | Description                                              |
|------------|---------|----------------------------------------------------------|
| `X`        | Integer | Camera world X position (left edge of viewport)         |
| `Y`        | Integer | Camera world Y position (top edge of viewport)          |
| `Zoom`     | Integer | Zoom level (100 = 100%). Clamped to 10–1000             |
| `Rotation` | Integer | Rotation in degrees (currently informational)           |
| `Width`    | Integer | Viewport width in screen pixels (read-only)             |
| `Height`   | Integer | Viewport height in screen pixels (read-only)            |

### Methods

| Method | Signature | Description |
|---|---|---|
| `Follow(x, y)` | `none(Integer, Integer)` | Center camera on world position |
| `Move(dx, dy)` | `none(Integer, Integer)` | Translate camera by delta |
| `WorldToScreen(wx, wy, sx, sy)` | `none(Integer,Integer,Integer*,Integer*)` | Convert world → screen |
| `ScreenToWorld(sx, sy, wx, wy)` | `none(Integer,Integer,Integer*,Integer*)` | Convert screen → world |
| `ToScreenX(wx)` | `Integer(Integer)` | World X → screen X |
| `ToScreenY(wy)` | `Integer(Integer)` | World Y → screen Y |
| `ToWorldX(sx)` | `Integer(Integer)` | Screen X → world X |
| `ToWorldY(sy)` | `Integer(Integer)` | Screen Y → world Y |
| `SetBounds(minX, minY, maxX, maxY)` | `none(Integer,Integer,Integer,Integer)` | Clamp camera movement |
| `ClearBounds()` | `none()` | Remove movement clamping |
| `IsVisible(x, y, w, h)` | `Integer(Integer,Integer,Integer,Integer)` | AABB visibility test |
| `IsDirty()` | `Integer()` | 1 if camera changed since last `ClearDirty()` |
| `ClearDirty()` | `none()` | Reset dirty flag after consuming the change |

### IsVisible — Viewport Culling

`IsVisible(x, y, w, h)` converts the camera's screen viewport into world space and performs
an axis-aligned bounding box overlap test. Use this every frame to skip drawing off-screen
entities:

```rust
if cam.IsVisible(enemy.x, enemy.y, enemy.w, enemy.h) {
    enemy.Draw(cam);
}
```

- Passes `NULL` camera → conservatively returns 1 (visible).
- Accounts for zoom: a 200% zoom halves the world-space viewport; 50% zoom doubles it.
- `x, y` are the **left/top** edges of the AABB; `w, h` are its dimensions.

### Dirty Flag

Set automatically when `X`, `Y`, `Zoom`, or `Rotation` change. Clear it after consuming the
change (e.g., after re-rendering the scene):

```rust
if cam.IsDirty() {
    scene.Redraw(cam);
    cam.ClearDirty();
}
```

---

## Viper.Game.Scene

A hierarchical entity tree for organizing game objects. Entities can be nested into a
parent-child structure; transforms propagate to children.

**Type:** Instance (obj)
**Constructor:** `Scene.New()`

### Methods

| Method | Signature | Description |
|---|---|---|
| `Add(entity)` | `none(obj)` | Add an entity to the root |
| `Remove(entity)` | `none(obj)` | Remove an entity |
| `FindByName(name)` | `obj(String)` | Find first entity with exact name match |
| `Update(dt)` | `none(Integer)` | Propagate update tick to all entities |
| `Draw(canvas, camera)` | `none(obj, obj)` | Draw all entities using camera transform |

---

## Viper.Game.Tilemap

A tile-based 2D map renderer. Tiles are indexed from a spritesheet, and the map scrolls with
a camera.

**Type:** Instance (obj)
**Constructor:** `Tilemap.New(cols, rows, tileWidth, tileHeight, spritesheet)`

### Methods

| Method | Signature | Description |
|---|---|---|
| `SetTile(col, row, tileId)` | `none(Integer, Integer, Integer)` | Set tile at grid position |
| `GetTile(col, row)` | `Integer(Integer, Integer)` | Get tile ID at grid position |
| `Draw(canvas, camera)` | `none(obj, obj)` | Draw visible tiles using camera viewport |

---

## Viper.Game.Physics2D

A 2D rigid-body physics engine. Bodies can be static (immovable) or dynamic. Collision
detection uses an axis-aligned broad phase followed by AABB narrow-phase resolution.

**Type:** Instance (obj — world handle)
**Constructor:** `Physics2D.NewWorld(gravityX, gravityY)`

### World Methods

| Method | Signature | Description |
|---|---|---|
| `Step(dt)` | `none(Integer)` | Advance physics by `dt` milliseconds |
| `AddBody(x, y, w, h, mass)` | `Integer(Integer,Integer,Integer,Integer,Integer)` | Add a body; returns body handle |
| `RemoveBody(handle)` | `none(Integer)` | Remove a body from the world |
| `SetPosition(handle, x, y)` | `none(Integer, Integer, Integer)` | Teleport a body |
| `SetVelocity(handle, vx, vy)` | `none(Integer, Integer, Integer)` | Override velocity |
| `ApplyForce(handle, fx, fy)` | `none(Integer, Integer, Integer)` | Apply impulse force |
| `GetPosition(handle, x, y)` | `none(Integer, Integer*, Integer*)` | Read current position |
| `GetVelocity(handle, vx, vy)` | `none(Integer, Integer*, Integer*)` | Read current velocity |
| `SetCollisionLayer(handle, layer)` | `none(Integer, Integer)` | Set body's layer bit (0–31) |
| `SetCollisionMask(handle, mask)` | `none(Integer, Integer)` | Set layers this body collides with |

### Collision Layers and Masks

Physics2D uses a 32-bit layer/mask bitfield system:

- **`collision_layer`**: which bit (0–31) this body occupies. Default: 0.
- **`collision_mask`**: bitmask of layers this body will collide with. Default: `0xFFFFFFFF`
  (collides with all layers).

To create non-colliding groups:

```rust
// Player on layer 0, enemies on layer 1, terrain on layer 2
world.SetCollisionLayer(playerHandle, 0);
world.SetCollisionMask(playerHandle, 0b110);  // collides with enemies + terrain, not self

world.SetCollisionLayer(enemyHandle, 1);
world.SetCollisionMask(enemyHandle, 0b101);   // collides with player + terrain, not other enemies
```

> **Layer 31 note:** The default mask is `0xFFFFFFFF` (all bits set, including bit 31). A body
> placed on layer 31 will collide with any body whose default mask is used.

### Current Limits

| Limit | Value | Notes |
|---|---|---|
| `PH_MAX_BODIES` | 256 | Maximum bodies per world. Traps if exceeded |
| `BPG_DIM` | 8 | Broad-phase grid cells per axis (8×8 = 64 cells) |
| `BPG_CELL_MAX` | 32 | Maximum bodies per broad-phase cell |

> To increase `PH_MAX_BODIES`, edit the constant in `rt_physics2d.c` and recompile.

---

## Viper.Game.Quadtree

A spatial quadtree for O(log n) range queries. Insert entities by ID with their AABB, then
query a rectangle to find all overlapping IDs.

**Type:** Instance (obj)
**Constructor:** `Quadtree.New(x, y, w, h)` — defines the world bounds

### Methods

| Method | Signature | Description |
|---|---|---|
| `Insert(id, x, y, w, h)` | `Integer(Integer,Integer,Integer,Integer,Integer)` | Insert AABB; returns 1 on success, 0 if duplicate |
| `Remove(id)` | `Integer(Integer)` | Remove by ID; returns 1 on success |
| `QueryRect(x, y, w, h)` | `Integer(Integer,Integer,Integer,Integer)` | Query overlapping items; returns count |
| `GetResult(i)` | `Integer(Integer)` | Get the i-th result ID from last query |
| `GetResultCount()` | `Integer()` | Number of results from last query |
| `QueryWasTruncated()` | `Integer()` | 1 if last query exceeded the result cap |
| `ItemCount()` | `Integer()` | Total items in the tree |
| `Destroy()` | `none()` | Free the quadtree |

### Query Truncation

`QueryRect` returns at most **256** results (`RT_QUADTREE_MAX_RESULTS`). If more items
overlap the query rectangle, the results are truncated. Always check `QueryWasTruncated()`
when the result count might exceed 256:

```rust
var count = tree.QueryRect(x, y, w, h);
if tree.QueryWasTruncated() {
    // Handle dense region — results incomplete
}
```

### Duplicate ID Guard

Inserting the same ID twice returns 0 and leaves the tree unchanged. Use distinct IDs
(e.g., entity indices) for all inserts.

### Current Limits

| Limit | Value | Notes |
|---|---|---|
| `RT_QUADTREE_MAX_RESULTS` | 256 | Maximum results per query; detect overflow with `QueryWasTruncated()` |
| Max total items | 4096 | Items beyond this silently fail insertion |
| Max overlap pairs | 1024 | `GetPairCount()` stops at this value |

---

## Viper.Game.Particle

A particle emitter that manages a pool of particles with position, velocity, lifetime,
and color.

**Type:** Instance (obj)
**Constructor:** `Particle.NewEmitter(maxParticles)`

### Methods

| Method | Signature | Description |
|---|---|---|
| `Emit(x, y, vx, vy, life, color)` | `none(...)` | Spawn a particle |
| `Update(dt)` | `none(Integer)` | Advance all active particles |
| `Count()` | `Integer()` | Number of live particles |
| `Get(i)` | `obj(Integer)` | Get the i-th active particle |
| `DrawToPixels(canvas, xOff, yOff)` | `Integer(obj, Integer, Integer)` | Batch-render all particles to canvas |

### Batch Rendering

Use `DrawToPixels` for performance when rendering many particles. It renders all active
particles in a single pass instead of requiring per-particle `Get()` + draw calls:

```rust
emitter.Update(dt);
emitter.DrawToPixels(canvas, 0, 0);
```

---

## Viper.Game.SpriteBatch

Accumulates sprite draw calls for efficient batched rendering.

**Type:** Instance (obj)
**Constructor:** `SpriteBatch.New()`

### Methods

| Method | Signature | Description |
|---|---|---|
| `Add(sprite, x, y)` | `none(obj, Integer, Integer)` | Queue a sprite draw |
| `Draw(canvas, camera)` | `none(obj, obj)` | Flush all queued draws |

---

## Viper.Game.SpriteAnim

Frame-based sprite animation. Stores start/end frame indices and plays through them at a
configurable frame rate. There is **no upper limit** on the number of frames.

**Type:** Instance (obj)
**Constructor:** `SpriteAnim.New()`

### Methods

| Method | Signature | Description |
|---|---|---|
| `Setup(startFrame, endFrame, fps, loop)` | `none(Integer,Integer,Integer,Integer)` | Configure animation |
| `Update(dt)` | `none(Integer)` | Advance animation by `dt` milliseconds |
| `GetFrame()` | `Integer()` | Current frame index |
| `IsFinished()` | `Integer()` | 1 when non-looping animation completes |
| `Reset()` | `none()` | Restart from the first frame |

---

## Viper.Game.StateMachine

A finite state machine with up to **256 states**. Each state has optional `Enter`, `Update`,
and `Exit` callbacks. Traps if the state limit is exceeded.

**Type:** Instance (obj)
**Constructor:** `StateMachine.New()`

### Methods

| Method | Signature | Description |
|---|---|---|
| `AddState(id, enter, update, exit)` | `none(Integer, fn, fn, fn)` | Register a state |
| `SetState(id)` | `none(Integer)` | Transition to a state |
| `Update(dt)` | `none(Integer)` | Tick the active state |
| `GetState()` | `Integer()` | Current state ID |

### Current Limits

| Limit | Value | Notes |
|---|---|---|
| `RT_STATE_MAX` | 256 | Maximum states. Traps if exceeded |

---

## Viper.Game.ObjectPool

A fixed-capacity pool for reusing objects. Acquire an object from the pool; release it when
done. Iteration over active objects is O(1) using an intrusive linked list.

**Type:** Instance (obj)
**Constructor:** `ObjectPool.New(capacity)`

### Methods

| Method | Signature | Description |
|---|---|---|
| `Acquire()` | `obj()` | Get an inactive object (NULL if pool exhausted) |
| `Release(obj)` | `none(obj)` | Return an object to the pool |
| `FirstActive()` | `obj()` | First active object (O(1)) |
| `NextActive(obj)` | `obj(obj)` | Next active object after `obj` (O(1)) |
| `ActiveCount()` | `Integer()` | Number of active objects |

---

## Viper.Game.PathFollower

Moves an entity along a sequence of waypoints at a constant speed. Uses linear interpolation
between waypoints with a fixed-point coordinate scale of **1000 = 1 world unit**.

**Type:** Instance (obj)
**Constructor:** `PathFollower.New()`

> **Coordinate scale:** PathFollower uses fixed-point coordinates where 1000 = 1 unit.
> To place a waypoint at world position (200, 150), use `AddWaypoint(200000, 150000)`.

### Methods

| Method | Signature | Description |
|---|---|---|
| `AddWaypoint(x, y)` | `none(Integer, Integer)` | Append a waypoint (×1000 scale) |
| `SetSpeed(speed)` | `none(Integer)` | Movement speed in units/second (×1000 scale) |
| `Update(dt)` | `none(Integer)` | Advance along the path |
| `GetX()` | `Integer()` | Current X position (×1000 scale) |
| `GetY()` | `Integer()` | Current Y position (×1000 scale) |
| `IsFinished()` | `Integer()` | 1 when the end of the path is reached |
| `Reset()` | `none()` | Restart from the first waypoint |

---

## Viper.Game.Timer

A frame-based countdown timer. Each call to `Tick()` decrements the remaining count by 1.
At 60 fps a 1-second timer requires `SetDuration(60)`.

**Type:** Instance (obj)
**Constructor:** `Timer.New()`

### Methods

| Method | Signature | Description |
|---|---|---|
| `SetDuration(frames)` | `none(Integer)` | Set countdown in frames |
| `Tick()` | `none()` | Decrement by 1 |
| `IsFinished()` | `Integer()` | 1 when remaining reaches 0 |
| `Remaining()` | `Integer()` | Frames remaining |
| `Reset()` | `none()` | Restart from the original duration |

---

## Viper.Game.Tween / TweenChain

`Tween` interpolates a numeric value from `start` to `end` over a duration using a named
easing function. `TweenChain` sequences multiple tweens in order.

**Type:** Instance (obj)
**Constructors:** `Tween.New()`, `TweenChain.New()`

### Tween Methods

| Method | Signature | Description |
|---|---|---|
| `Setup(start, end, duration, easing)` | `none(Integer,Integer,Integer,String)` | Configure |
| `Update(dt)` | `none(Integer)` | Advance |
| `GetValue()` | `Integer()` | Current interpolated value |
| `IsFinished()` | `Integer()` | 1 when complete |
| `Reset()` | `none()` | Restart |

### Easing Functions

`"linear"`, `"ease-in"`, `"ease-out"`, `"ease-in-out"`, `"bounce"`, `"elastic"`

### TweenChain Methods

| Method | Signature | Description |
|---|---|---|
| `Add(tween)` | `none(obj)` | Append a tween to the chain |
| `Update(dt)` | `none(Integer)` | Advance current tween in chain |
| `GetValue()` | `Integer()` | Current value from active tween |
| `IsFinished()` | `Integer()` | 1 when all tweens complete |

---

## Viper.Game.SmoothValue

Lerps a value toward a target each frame using a configurable smoothing factor.

**Type:** Instance (obj)
**Constructor:** `SmoothValue.New(initial, smoothing)`

### Methods

| Method | Signature | Description |
|---|---|---|
| `SetTarget(value)` | `none(Integer)` | Set the target to lerp toward |
| `Update(dt)` | `none(Integer)` | Advance lerp |
| `GetValue()` | `Integer()` | Current smoothed value |

---

## Viper.Game.ButtonGroup

Manages a group of mutually exclusive button IDs (like radio buttons). At most one button
can be selected at a time. Traps if the button limit is exceeded.

**Type:** Instance (obj)
**Constructor:** `ButtonGroup.New()`

### Methods

| Method | Signature | Description |
|---|---|---|
| `Add(id)` | `Integer(Integer)` | Register a button. Returns 0 if duplicate |
| `Remove(id)` | `Integer(Integer)` | Unregister a button. Clears selection if selected |
| `Has(id)` | `Integer(Integer)` | 1 if button is registered |
| `Count()` | `Integer()` | Number of registered buttons |
| `Select(id)` | `Integer(Integer)` | Select a button (deselects all others) |
| `ClearSelection()` | `none()` | Deselect all buttons |
| `Selected()` | `Integer()` | Currently selected button ID, or -1 |
| `IsSelected(id)` | `Integer(Integer)` | 1 if `id` is the current selection |
| `HasSelection()` | `Integer()` | 1 if any button is selected |
| `SelectionChanged()` | `Integer()` | 1 if selection changed since last frame |
| `ClearChangedFlag()` | `none()` | Reset the changed flag (call each frame) |
| `GetAt(index)` | `Integer(Integer)` | Button ID at position `index` |
| `SelectNext()` | `Integer()` | Advance selection forward (wraps) |
| `SelectPrev()` | `Integer()` | Advance selection backward (wraps) |

### Current Limits

| Limit | Value | Notes |
|---|---|---|
| `RT_BUTTONGROUP_MAX` | 256 | Maximum buttons per group. Traps if exceeded |

---

## Viper.Game.ScreenFX

Post-process screen effects: camera shake, flash, fade-in, and fade-out.

**Type:** Instance (obj)
**Constructor:** `ScreenFX.New()`

> **Color format:** All color parameters use packed **`0xRRGGBBAA`** (alpha in the
> least-significant byte). This differs from the `0x00RRGGBB` format used by Canvas
> drawing methods.
> - Full-opacity white: `0xFFFFFFFF`
> - Half-opacity red: `0xFF000080`

### Methods

| Method | Signature | Description |
|---|---|---|
| `Shake(intensity, duration, decay)` | `none(Integer, Integer, Integer)` | Camera shake |
| `Flash(color, duration)` | `none(Integer, Integer)` | Flash overlay |
| `FadeIn(color, duration)` | `none(Integer, Integer)` | Fade from color to clear |
| `FadeOut(color, duration)` | `none(Integer, Integer)` | Fade from clear to color |
| `Update(dt)` | `none(Integer)` | Advance all active effects |
| `IsActive()` | `Integer()` | 1 if any effect is active |
| `IsTypeActive(type)` | `Integer(Integer)` | 1 if the given effect type is active |
| `GetShakeX()` | `Integer()` | Current horizontal shake offset |
| `GetShakeY()` | `Integer()` | Current vertical shake offset |
| `GetOverlayAlpha()` | `Integer()` | Current overlay alpha (0–255) |
| `GetOverlayColor()` | `Integer()` | Current overlay color (RRGGBB00) |
| `CancelAll()` | `none()` | Stop all effects immediately |
| `CancelType(type)` | `none(Integer)` | Stop effects of a specific type |

### Shake Decay Model

The `decay` parameter controls how quickly shake intensity falls off:

| `decay` value | Behavior |
|---|---|
| `0` | No decay — constant intensity throughout |
| `1000` | Linear decay — intensity falls proportionally to remaining time |
| `≥ 1500` | **Quadratic (trauma model)** — intensity falls as `(remaining_time)²`, producing a natural-feeling hard-stop near the end |

```rust
// Subtle hit feedback with quadratic falloff
fx.Shake(5000, 300, 2000);
// Earthquake: constant rumble for 3 seconds
fx.Shake(3000, 3000, 0);
```

---

## Viper.Game.Collision

Static helpers for geometric overlap tests.

**All methods are static** (`Viper.Game.Collision.*`)

| Method | Signature | Description |
|---|---|---|
| `AABBvsAABB(ax,ay,aw,ah, bx,by,bw,bh)` | `Integer(...)` | 1 if two AABBs overlap |
| `CircleVsCircle(ax,ay,ar, bx,by,br)` | `Integer(...)` | 1 if two circles overlap |
| `PointInAABB(px,py, ax,ay,aw,ah)` | `Integer(...)` | 1 if point inside AABB |

---

## Viper.Game.Grid2D

A 2D integer grid. Stores one integer value per cell with get/set/fill operations.

**Type:** Instance (obj)
**Constructor:** `Grid2D.New(cols, rows, defaultValue)`

### Methods

| Method | Signature | Description |
|---|---|---|
| `Get(col, row)` | `Integer(Integer, Integer)` | Read cell value |
| `Set(col, row, value)` | `none(Integer, Integer, Integer)` | Write cell value |
| `Fill(value)` | `none(Integer)` | Set all cells to `value` |
| `Cols()` | `Integer()` | Number of columns |
| `Rows()` | `Integer()` | Number of rows |
| `InBounds(col, row)` | `Integer(Integer, Integer)` | 1 if position is within grid bounds |

---

## Current Limits

| Class | Limit | Constant | Notes |
|---|---|---|---|
| Physics2D | 256 bodies/world | `PH_MAX_BODIES` | Traps on overflow |
| Physics2D | 8×8 broad-phase cells | `BPG_DIM = 8` | Per-world |
| Physics2D | 32 bodies/cell | `BPG_CELL_MAX = 32` | |
| Quadtree | 256 query results | `RT_QUADTREE_MAX_RESULTS` | Detect with `QueryWasTruncated()` |
| Quadtree | 4096 total items | Internal | |
| Quadtree | 1024 overlap pairs | Internal | |
| StateMachine | 256 states | `RT_STATE_MAX` | Traps on overflow |
| ButtonGroup | 256 buttons | `RT_BUTTONGROUP_MAX` | Traps on overflow |
| SpriteAnim | No limit | — | Start/end frame are plain integers |

---

## Coordinate Systems

Viper Game APIs use **integer world coordinates** with no fixed unit. Choose a coordinate
scale that suits your game and stay consistent:

| System | Scale | Used by |
|---|---|---|
| World pixels | 1 = 1 pixel | Camera, Physics2D, Quadtree, Tilemap, ScreenFX |
| PathFollower | 1000 = 1 world unit | PathFollower only |

When mixing PathFollower with Camera or Physics2D, convert between scales:

```rust
// PathFollower uses ×1000 scale; camera uses pixel scale
var camX = follower.GetX() / 1000;
var camY = follower.GetY() / 1000;
cam.Follow(camX, camY);
```

> **ScreenFX color** uses `0xRRGGBBAA`; **Canvas drawing** uses `0x00RRGGBB`.
> These formats are incompatible — always check which is expected.
