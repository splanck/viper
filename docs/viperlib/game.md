---
status: active
audience: public
last-verified: 2026-04-09
---

# Game

> 2D game primitives: physics, scenes, cameras, tilemaps, particles, animations, pathfinding, and more.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Game.Camera](#vipergamecamera)
- [Viper.Game.Scene](#vipergamescene)
- [Viper.Game.Tilemap](#vipergametilemap)
- [Viper.Game.Physics2D](#vipergamephysics2d)
- [Viper.Game.Quadtree](#vipergamequadtree)
- [Viper.Game.ParticleEmitter](#vipergameparticleemitter)
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
- [Viper.Game.DebugOverlay](#vipergamedebugoverlay)
- [Viper.Game.Collision](#vipergamecollision)
- [Viper.Game.Grid2D](#vipergamegrid2d)
- [Viper.Game.Lighting2D](#vipergamelighting2d)
- [Viper.Game.PlatformerController](#vipergameplatformercontroller)
- [Viper.Game.AchievementTracker](#vipergameachievementtracker)
- [Viper.Game.Typewriter](#vipergametypewriter)
- [Viper.Game.UI.*](game/ui.md) — Label, Bar, Panel, NineSlice, MenuList (in-game UI widgets)
- [Viper.Game.Pathfinder](game/pathfinding.md) — A* grid pathfinding for AI navigation
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

A hierarchical object tree for organizing game objects. Objects can be nested into a
parent-child structure; transforms propagate to children.

**Type:** Instance (obj)
**Constructor:** `Scene.New()`

### Methods

| Method | Signature | Description |
|---|---|---|
| `Add(obj)` | `none(obj)` | Add an object to the root |
| `Remove(obj)` | `none(obj)` | Remove an object |
| `FindByName(name)` | `obj(String)` | Find first object with exact name match |
| `Update(dt)` | `none(Integer)` | Propagate update tick to all objects |
| `Draw(canvas, camera)` | `none(obj, obj)` | Draw all objects using camera transform |

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
**Constructor:** `Physics2D.World.New(gravityX, gravityY)`

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

## Viper.Game.ParticleEmitter

A particle emitter that manages a pool of particles with position, velocity, lifetime,
and color.

**Type:** Instance (obj)
**Constructor:** `ParticleEmitter.NewEmitter(maxParticles)`

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

A finite state machine with up to **256 numeric states**. Supports one-frame edge flags
(`JustEntered`/`JustExited`) for triggering enter/exit logic, a per-state frame counter,
and transition validation. Traps if the state limit is exceeded.

**Type:** Instance (obj)
**Constructor:** `StateMachine.New()`

### Properties

| Property | Type | Description |
|---|---|---|
| `Current` | Integer | Current state ID (-1 if none set) |
| `Previous` | Integer | Previous state ID (-1 if none) |
| `JustEntered` | Boolean | True on the frame a state was entered |
| `JustExited` | Boolean | True on the frame a state was exited |
| `FramesInState` | Integer | Frames since entering current state (incremented by `Update()`) |
| `StateCount` | Integer | Number of registered states |

### Methods

| Method | Signature | Description |
|---|---|---|
| `AddState(id)` | `Boolean(Integer)` | Register a state ID. Returns false if duplicate |
| `SetInitial(id)` | `Boolean(Integer)` | Set initial state (sets `JustEntered` flag) |
| `Transition(id)` | `Boolean(Integer)` | Transition to a registered state. Sets edge flags, resets frame counter |
| `IsState(id)` | `Boolean(Integer)` | True if current state equals `id` |
| `HasState(id)` | `Boolean(Integer)` | True if state `id` is registered |
| `Update()` | `Void()` | Increment `FramesInState` counter (call once per frame) |
| `ClearFlags()` | `Void()` | Clear `JustEntered` and `JustExited` flags |

### Example

```zia
bind Viper.Game;

// Register states and set initial
var sm = StateMachine.New();
sm.AddState(0);  // MENU
sm.AddState(1);  // PLAYING
sm.AddState(2);  // PAUSED
sm.SetInitial(0);

// In game loop:
sm.Update();
sm.ClearFlags();

if sm.IsState(0) {
    // Menu logic...
    if startPressed { sm.Transition(1); }
} else if sm.IsState(1) {
    // Gameplay logic...
    if pausePressed { sm.Transition(2); }
}
```

**Reference implementation:** [Pac-Man](../../examples/games/pacman/game.zia) — uses StateMachine for 8 game states.

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
> To place a waypoint at world position (200, 150), use `AddPoint(200000, 150000)`.

### Methods

| Method | Signature | Description |
|---|---|---|
| `AddPoint(x, y)` | `none(Integer, Integer)` | Append a waypoint (×1000 scale) |
| `SetSpeed(speed)` | `none(Integer)` | Movement speed in units/second (×1000 scale) |
| `Update(dt)` | `none(Integer)` | Advance along the path |
| `GetX()` | `Integer()` | Current X position (×1000 scale) |
| `GetY()` | `Integer()` | Current Y position (×1000 scale) |
| `IsFinished()` | `Integer()` | 1 when the end of the path is reached |
| `Reset()` | `none()` | Restart from the first waypoint |

---

## Viper.Game.Timer

A countdown timer supporting both frame-based and millisecond-based modes. Frame mode counts
frames (call `Update()` once per frame). Ms mode counts delta time (call `UpdateMs(dt)` with
the frame's delta time in ms) for frame-rate-independent timing.

**Type:** Instance (obj)
**Constructor:** `Timer.New()`

### Frame-Based Methods

| Method | Signature | Description |
|---|---|---|
| `Start(frames)` | `none(Integer)` | Start one-shot countdown in frames |
| `StartRepeating(frames)` | `none(Integer)` | Start repeating timer in frames |
| `Update()` | `Boolean()` | Advance by 1 frame; returns true on expiry |
| `Stop()` | `none()` | Stop the timer |
| `Reset()` | `none()` | Reset elapsed to 0 without changing state |

### Millisecond-Based Methods

| Method | Signature | Description |
|---|---|---|
| `StartMs(durationMs)` | `none(Integer)` | Start one-shot countdown in milliseconds |
| `StartRepeatingMs(intervalMs)` | `none(Integer)` | Start repeating timer in milliseconds |
| `UpdateMs(dt)` | `Boolean(Integer)` | Advance by dt ms; returns true on expiry |

### Properties

| Property | Type | Description |
|---|---|---|
| `IsRunning` | `Boolean` | True if timer is counting |
| `IsExpired` | `Boolean` | True if timer finished |
| `IsRepeating` | `Boolean` | True if auto-restarting |
| `Elapsed` | `Integer` | Frames elapsed (frame mode) |
| `Remaining` | `Integer` | Frames remaining (frame mode) |
| `ElapsedMs` | `Integer` | Milliseconds elapsed (ms mode) |
| `RemainingMs` | `Integer` | Milliseconds remaining (ms mode) |
| `Progress` | `Integer` | 0-100 completion percentage (both modes) |
| `Duration` | `Integer` | Total duration (read/write) |

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

**Reference implementation:** [Pac-Man](../../examples/games/pacman/game.zia) — uses ButtonGroup for menu navigation with `SelectNext()`/`SelectPrev()` wraparound.

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

## Viper.Game.DebugOverlay

Real-time debug information overlay. Shows FPS, delta time, and custom watch variables in a semi-transparent panel. Toggle with a key binding during development.

**Type:** Instance (obj)
**Constructor:** `DebugOverlay.New()`

### Properties

| Property    | Type    | Access | Description                              |
|-------------|---------|--------|------------------------------------------|
| `IsEnabled` | Boolean | Read   | Whether the overlay is currently visible |
| `Fps`       | Integer | Read   | Current FPS (rolling 16-frame average)   |

### Methods

| Method              | Signature                   | Description                                     |
|---------------------|-----------------------------|-------------------------------------------------|
| `Enable()`          | `Void()`                    | Show the overlay                                |
| `Disable()`         | `Void()`                    | Hide the overlay                                |
| `Toggle()`          | `Void()`                    | Toggle visibility                               |
| `Update(dt)`        | `Void(Integer)`             | Update FPS tracking (call once per frame with dt in ms) |
| `Watch(name, value)`| `Void(String, Integer)`     | Add/update a named watch variable (max 16)      |
| `Unwatch(name)`     | `Void(String)`              | Remove a watch variable                         |
| `Clear()`           | `Void()`                    | Remove all watch variables                      |
| `Draw(canvas)`      | `Void(Canvas)`              | Render the overlay on top of everything         |

> FPS is color-coded: **green** (≥55), **yellow** (30–54), **red** (<30).

For full documentation see [Debug Overlay](game/debug.md).

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

## Viper.Game.Lighting2D

A 2D darkness overlay system with a pulsing player light and pooled dynamic point lights.
Renders a full-screen tinted overlay, then punches bright "holes" for light sources. Suitable
for cave, dungeon, and horror game atmospheres.

**Type:** Instance (obj)
**Constructor:** `Lighting2D.New(maxDynamicLights)`

### Properties

| Property | Type | Description |
|---|---|---|
| `Darkness` | `Integer` | Overlay alpha (0 = fully lit, 255 = pitch black) |
| `TintColor` | `Integer` | Darkness tint color (0xRRGGBB) — e.g., blue for underwater |
| `LightCount` | `Integer` | Number of active dynamic lights |

### Methods

| Method | Signature | Description |
|---|---|---|
| `SetPlayerLight(radius, color)` | `none(Integer, Integer)` | Configure the player's light radius and color |
| `AddLight(x, y, radius, color, lifetime)` | `none(Integer, Integer, Integer, Integer, Integer)` | Add a dynamic light at world position with frame lifetime |
| `AddTileLight(screenX, screenY, radius, color)` | `none(Integer, Integer, Integer, Integer)` | Add a single-frame tile glow at screen position |
| `ClearLights()` | `none()` | Remove all dynamic lights |
| `Update()` | `none()` | Tick light lifetimes, advance player light pulse |
| `Draw(canvas, camX, camY, playerScreenX, playerScreenY)` | `none(Canvas, Integer, Integer, Integer, Integer)` | Render darkness overlay + all lights |

---

## Viper.Game.PlatformerController

Encapsulates standard 2D platformer input mechanics: jump buffering, coyote time, variable
jump height, and separate ground/air acceleration curves with apex gravity bonus. All timing
is ms-based for frame-rate independence.

**Type:** Instance (obj)
**Constructor:** `PlatformerController.New()`

### Configuration Methods

| Method | Signature | Description |
|---|---|---|
| `SetJumpBuffer(ms)` | `none(Integer)` | Grace window for early jump press (default: 100ms) |
| `SetCoyoteTime(ms)` | `none(Integer)` | Grace window after leaving ledge (default: 80ms) |
| `SetAcceleration(ground, air, decel)` | `none(Integer, Integer, Integer)` | Horizontal acceleration rates (x100 units) |
| `SetJumpForce(full, cut)` | `none(Integer, Integer)` | Full jump and early-release cut force (negative = up) |
| `SetMaxSpeed(normal, sprint)` | `none(Integer, Integer)` | Speed caps for walk and sprint (x100 units) |
| `SetGravity(gravity, maxFall)` | `none(Integer, Integer)` | Gravity strength and terminal velocity |
| `SetApexBonus(threshold, gravityPct)` | `none(Integer, Integer)` | Apex hang: reduce gravity when |vy| < threshold |

### Per-Frame Update

| Method | Signature | Description |
|---|---|---|
| `Update(dt, left, right, jumpPressed, jumpHeld, onGround, sprint)` | `none(Integer, Boolean, Boolean, Boolean, Boolean, Boolean, Boolean)` | Process input and compute velocities |

### Output Properties

| Property | Type | Description |
|---|---|---|
| `VX` | `Integer` | Computed horizontal velocity (x100, read/write) |
| `VY` | `Integer` | Computed vertical velocity (x100, read/write) |
| `ShouldJump` | `Boolean` | True when jump buffer + coyote resolve (one-shot, consumed on read) |
| `JumpForce` | `Integer` | Configured full jump force |
| `Facing` | `Integer` | 1 = right, -1 = left |
| `IsMoving` | `Boolean` | True when VX != 0 |

---

## Viper.Game.AchievementTracker

Achievement system with bitmask-based unlock tracking, stat counters, and animated slide-in
notification popups. Supports up to 64 achievements and 32 stat counters. The unlock mask is
a single Integer suitable for save/load via SaveData.

**Type:** Instance (obj)
**Constructor:** `AchievementTracker.New(maxAchievements)`

### Properties

| Property | Type | Description |
|---|---|---|
| `Mask` | `Integer` | Bitmask of unlocked achievements (read/write for save/load) |
| `UnlockedCount` | `Integer` | Number of achievements unlocked |
| `TotalCount` | `Integer` | Number of achievements defined |
| `NotifyDuration` | `Integer` | Notification display time in ms (write-only, default: 3000) |
| `HasNotification` | `Boolean` | True when a notification popup is visible |

### Methods

| Method | Signature | Description |
|---|---|---|
| `Add(id, name, description)` | `none(Integer, String, String)` | Define an achievement |
| `Unlock(id)` | `Boolean(Integer)` | Unlock achievement; returns true if newly unlocked |
| `IsUnlocked(id)` | `Boolean(Integer)` | Check if achievement is unlocked |
| `IncrementStat(statId, amount)` | `none(Integer, Integer)` | Add to a stat counter |
| `GetStat(statId)` | `Integer(Integer)` | Read a stat counter |
| `SetStat(statId, value)` | `none(Integer, Integer)` | Set a stat counter directly |
| `Update(dt)` | `none(Integer)` | Tick notification timer |
| `Draw(canvas)` | `none(Canvas)` | Render notification popup (top-right corner) |

---

## Viper.Game.Typewriter

Character-by-character text reveal effect for dialogue, lore terminals, tutorials, and
narrative sequences. Accumulates milliseconds and reveals one character per configured
interval. `Skip()` instantly reveals all remaining text.

**Type:** Instance (obj)
**Constructor:** `Typewriter.New()`

### Properties

| Property | Type | Description |
|---|---|---|
| `VisibleText` | `String` | Text revealed so far |
| `FullText` | `String` | Complete source text |
| `IsActive` | `Boolean` | True while revealing |
| `IsComplete` | `Boolean` | True when fully revealed |
| `Progress` | `Integer` | 0-100 reveal percentage |
| `CharCount` | `Integer` | Characters revealed |
| `TotalChars` | `Integer` | Total characters in source |

### Methods

| Method | Signature | Description |
|---|---|---|
| `Say(text, rateMs)` | `none(String, Integer)` | Start revealing text at rateMs per character |
| `Update(dt)` | `Boolean(Integer)` | Advance reveal; returns true on completion |
| `Skip()` | `none()` | Reveal all remaining text instantly |
| `Reset()` | `none()` | Clear text and state |

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

## See Also

- [Game Engine Documentation](../gameengine/README.md) — Guides, tutorials, example games
- [Game Module Index](game/README.md) — Per-topic documentation pages
- [Graphics](graphics/README.md) — Canvas, Sprites, Tilemap
- [Input](input.md) — Keyboard, Mouse, Gamepad
- [Audio](audio.md) — Sound effects and music
- [Viper Runtime Library](README.md)
