# Physics & Collision
> Grid2D, CollisionRect, Collision, Physics2D, Quadtree

**Part of [Viper Runtime Library](../README.md) › [Game Utilities](README.md)**

---

## Viper.Game.Grid2D

A 2D array container optimized for tile maps, game boards, and grid-based data.

**Type:** Instance class (requires `New(width, height, defaultValue)`)

### Constructor

| Method                        | Signature                 | Description                                      |
|-------------------------------|---------------------------|--------------------------------------------------|
| `New(width, height, default)` | `Grid2D(Int, Int, Int)`   | Create a grid with dimensions and default value  |

### Properties

| Property | Type                  | Description                            |
|----------|-----------------------|----------------------------------------|
| `Width`  | `Integer` (read-only) | Width of the grid                      |
| `Height` | `Integer` (read-only) | Height of the grid                     |
| `Size`   | `Integer` (read-only) | Total number of cells (width × height) |

### Methods

| Method              | Signature                 | Description                                        |
|---------------------|---------------------------|----------------------------------------------------|
| `Clear()`           | `Void()`                  | Clear grid (fill with 0)                           |
| `CopyFrom(other)`   | `Boolean(Grid2D)`         | Copy data from another grid (must match dimensions)|
| `Count(value)`      | `Integer(Integer)`        | Count cells with specified value                   |
| `Fill(value)`       | `Void(Integer)`           | Fill entire grid with value                        |
| `Get(x, y)`         | `Integer(Int, Int)`       | Get value at coordinates                           |
| `InBounds(x, y)`    | `Boolean(Int, Int)`       | Check if coordinates are valid                     |
| `Replace(old, new)` | `Integer(Int, Int)`       | Replace all occurrences; returns count replaced    |
| `Set(x, y, value)`  | `Void(Int, Int, Int)`     | Set value at coordinates                           |

### Notes

- Coordinates are 0-based: (0,0) is top-left
- Out-of-bounds access traps with an error
- Use `InBounds()` to validate coordinates before access
- Grid stores integer values (use as tile IDs, flags, etc.)

### Zia Example

```rust
module Grid2DDemo;

bind Viper.Terminal;
bind Viper.Game.Grid2D as Grid;
bind Viper.Fmt as Fmt;

func start() {
    var g = Grid.New(10, 8, 0);
    Say("Size: " + Fmt.Int(g.get_Width()) + "x" + Fmt.Int(g.get_Height()));
    Say("Total: " + Fmt.Int(g.get_Size()));

    g.Fill(1);
    g.Set(5, 5, 42);
    Say("(5,5): " + Fmt.Int(g.Get(5, 5)));

    Say("InBounds(9,7): " + Fmt.Bool(g.InBounds(9, 7)));
    Say("Count(1): " + Fmt.Int(g.Count(1)));

    g.Replace(1, 2);
    g.Clear();
}
```

### Example: Tile Map

```basic
' Create a 20x15 tile map (defaults to 0 = empty)
DIM map AS OBJECT = Viper.Game.Grid2D.New(20, 15, 0)

' Set some tiles
CONST TILE_WALL = 1
CONST TILE_FLOOR = 2
CONST TILE_DOOR = 3

' Fill with floor
map.Fill(TILE_FLOOR)

' Add walls around the edges
FOR x = 0 TO map.Width - 1
    map.Set(x, 0, TILE_WALL)
    map.Set(x, map.Height - 1, TILE_WALL)
NEXT
FOR y = 0 TO map.Height - 1
    map.Set(0, y, TILE_WALL)
    map.Set(map.Width - 1, y, TILE_WALL)
NEXT

' Add a door
map.Set(10, 0, TILE_DOOR)

' Check a position before moving
DIM newX AS INTEGER = playerX + dx
DIM newY AS INTEGER = playerY + dy
IF map.InBounds(newX, newY) THEN
    DIM tile AS INTEGER = map.Get(newX, newY)
    IF tile <> TILE_WALL THEN
        playerX = newX
        playerY = newY
    END IF
END IF
```

### Example: Game of Life

```basic
DIM grid AS OBJECT = Viper.Game.Grid2D.New(50, 50, 0)
DIM next AS OBJECT = Viper.Game.Grid2D.New(50, 50, 0)

' Initialize with random cells
FOR y = 0 TO 49
    FOR x = 0 TO 49
        IF Viper.Random.Chance(0.3) THEN
            grid.Set(x, y, 1)
        END IF
    NEXT
NEXT

' Update step
FOR y = 0 TO 49
    FOR x = 0 TO 49
        DIM neighbors AS INTEGER = CountNeighbors(grid, x, y)
        DIM alive AS INTEGER = grid.Get(x, y)
        IF alive = 1 AND (neighbors = 2 OR neighbors = 3) THEN
            next.Set(x, y, 1)
        ELSEIF alive = 0 AND neighbors = 3 THEN
            next.Set(x, y, 1)
        ELSE
            next.Set(x, y, 0)
        END IF
    NEXT
NEXT
grid.CopyFrom(next)
```

### Use Cases

- **Tile maps:** 2D game levels, dungeon maps
- **Game boards:** Chess, checkers, puzzle games
- **Cellular automata:** Game of Life, simulation grids
- **Pathfinding:** Navigation grids, A* maps
- **Collision maps:** Simple tile-based collision detection

---

## Viper.Game.CollisionRect

Axis-aligned bounding box (AABB) for collision detection between game objects.

**Type:** Instance class (requires `New(x, y, width, height)`)

### Constructor

| Method                    | Signature                    | Description                     |
|---------------------------|------------------------------|---------------------------------|
| `New(x, y, width, height)`| `CollisionRect(Dbl,Dbl,Dbl,Dbl)` | Create rectangle at position |

### Properties

| Property  | Type                  | Description                   |
|-----------|-----------------------|-------------------------------|
| `X`       | `Double` (read-only)  | Left edge                     |
| `Y`       | `Double` (read-only)  | Top edge                      |
| `Width`   | `Double` (read-only)  | Width                         |
| `Height`  | `Double` (read-only)  | Height                        |
| `Right`   | `Double` (read-only)  | Right edge (x + width)        |
| `Bottom`  | `Double` (read-only)  | Bottom edge (y + height)      |
| `CenterX` | `Double` (read-only)  | Center X coordinate           |
| `CenterY` | `Double` (read-only)  | Center Y coordinate           |

### Methods

| Method                     | Signature                   | Description                          |
|----------------------------|-----------------------------|--------------------------------------|
| `ContainsPoint(px, py)`    | `Boolean(Double,Double)`    | Test if point is inside              |
| `ContainsRect(other)`      | `Boolean(CollisionRect)`    | Test if fully contains another       |
| `Expand(margin)`           | `Void(Double)`              | Grow rect on all sides               |
| `Move(dx, dy)`             | `Void(Double,Double)`       | Move by delta                        |
| `OverlapX(other)`          | `Double(CollisionRect)`     | Get X overlap depth                  |
| `OverlapY(other)`          | `Double(CollisionRect)`     | Get Y overlap depth                  |
| `Overlaps(other)`          | `Boolean(CollisionRect)`    | Test overlap with another rect       |
| `OverlapsRect(x, y, w, h)` | `Boolean(Dbl,Dbl,Dbl,Dbl)`  | Test overlap with raw coordinates    |
| `Set(x, y, w, h)`          | `Void(Dbl,Dbl,Dbl,Dbl)`     | Set position and size                |
| `SetCenter(cx, cy)`        | `Void(Double,Double)`       | Position by center point             |
| `SetPosition(x, y)`        | `Void(Double,Double)`       | Set top-left position                |
| `SetSize(w, h)`            | `Void(Double,Double)`       | Set dimensions                       |

### Zia Example

```rust
module CollisionDemo;

bind Viper.Terminal;
bind Viper.Game.Collision as Coll;
bind Viper.Game.CollisionRect as CR;
bind Viper.Fmt as Fmt;

func start() {
    // Static collision checks
    Say("Rects overlap: " + Fmt.Bool(Coll.RectsOverlap(0.0, 0.0, 10.0, 10.0, 5.0, 5.0, 10.0, 10.0)));
    Say("Point in rect: " + Fmt.Bool(Coll.PointInRect(5.0, 5.0, 0.0, 0.0, 10.0, 10.0)));
    Say("Circles overlap: " + Fmt.Bool(Coll.CirclesOverlap(0.0, 0.0, 5.0, 8.0, 0.0, 5.0)));
    Say("Distance: " + Fmt.Num(Coll.Distance(0.0, 0.0, 3.0, 4.0)));

    // CollisionRect instance
    var r = CR.New(10.0, 20.0, 100.0, 50.0);
    Say("Contains(50,30): " + Fmt.Bool(r.ContainsPoint(50.0, 30.0)));
    r.Move(5.0, 10.0);
    Say("After move X: " + Fmt.Num(r.get_X()));
}
```

### Example: Player-Enemy Collision

```basic
DIM playerBox AS OBJECT = Viper.Game.CollisionRect.New(100.0, 100.0, 32.0, 48.0)
DIM enemyBox AS OBJECT = Viper.Game.CollisionRect.New(200.0, 100.0, 32.0, 32.0)

' Update positions
playerBox.SetPosition(playerX, playerY)
enemyBox.SetPosition(enemyX, enemyY)

' Check collision
IF playerBox.Overlaps(enemyBox) THEN
    TakeDamage()
END IF
```

---

## Viper.Game.Collision

Static utility class for collision detection without creating objects. Useful for one-off checks.

**Type:** Static class (no instantiation needed)

### Methods

| Method                              | Signature                        | Description                         |
|-------------------------------------|----------------------------------|-------------------------------------|
| `RectsOverlap(x1,y1,w1,h1,x2,y2,w2,h2)` | `Boolean(8×Double)`          | Test rectangle overlap              |
| `PointInRect(px,py,rx,ry,rw,rh)`    | `Boolean(6×Double)`              | Test if point in rectangle          |
| `CirclesOverlap(x1,y1,r1,x2,y2,r2)` | `Boolean(6×Double)`              | Test circle overlap                 |
| `PointInCircle(px,py,cx,cy,r)`      | `Boolean(5×Double)`              | Test if point in circle             |
| `CircleRect(cx,cy,r,rx,ry,rw,rh)`   | `Boolean(7×Double)`              | Test circle-rectangle overlap       |
| `Distance(x1,y1,x2,y2)`             | `Double(4×Double)`               | Distance between two points         |
| `DistanceSquared(x1,y1,x2,y2)`      | `Double(4×Double)`               | Squared distance (faster)           |

### Zia Example

> See the CollisionRect Zia example above for both static and instance collision API usage.

### Example: Quick Collision Checks

```basic
' Check if mouse click hits a button
IF Viper.Game.Collision.PointInRect(mouseX, mouseY, btnX, btnY, btnW, btnH) THEN
    OnButtonClick()
END IF

' Check if two circles overlap
IF Viper.Game.Collision.CirclesOverlap(p1X, p1Y, p1R, p2X, p2Y, p2R) THEN
    HandleCollision()
END IF

' Get distance for range checks
DIM dist AS DOUBLE = Viper.Game.Collision.Distance(playerX, playerY, enemyX, enemyY)
IF dist < attackRange THEN
    CanAttack = 1
END IF
```

---

## Viper.Game.Physics2D

Simple 2D physics engine with rigid body dynamics, gravity, and AABB collision detection. Uses fixed-timestep Euler integration and impulse-based collision resolution.

**Type:** Compound — `Physics2D.World` (instance) + `Physics2D.Body` (instance)

### World Constructor

| Method              | Signature                  | Description                                    |
|---------------------|----------------------------|------------------------------------------------|
| `World.New(gx, gy)` | `World(Double, Double)`    | Create world with gravity vector               |

### World Properties

| Property    | Type                  | Description                  |
|-------------|-----------------------|------------------------------|
| `BodyCount` | `Integer` (read-only) | Number of bodies in the world|

### World Methods

| Method               | Signature             | Description                             |
|----------------------|-----------------------|-----------------------------------------|
| `Add(body)`          | `Void(Body)`          | Add a body to the world                 |
| `Remove(body)`       | `Void(Body)`          | Remove a body from the world            |
| `SetGravity(gx, gy)` | `Void(Double,Double)` | Change gravity vector                   |
| `Step(dt)`           | `Void(Double)`        | Advance simulation by dt seconds        |

### Body Constructor

| Method                        | Signature                          | Description                              |
|-------------------------------|------------------------------------|------------------------------------------|
| `Body.New(x, y, w, h, mass)` | `Body(Dbl,Dbl,Dbl,Dbl,Dbl)`       | Create body at position with size/mass   |

### Body Properties

| Property      | Type                   | Description                              |
|---------------|------------------------|------------------------------------------|
| `X`           | `Double` (read-only)   | X position                               |
| `Y`           | `Double` (read-only)   | Y position                               |
| `Width`       | `Double` (read-only)   | Body width                               |
| `Height`      | `Double` (read-only)   | Body height                              |
| `VX`          | `Double` (read-only)   | X velocity                               |
| `VY`          | `Double` (read-only)   | Y velocity                               |
| `Mass`        | `Double` (read-only)   | Body mass (0 = static)                   |
| `IsStatic`    | `Boolean` (read-only)  | True if mass is 0 (immovable)            |
| `Restitution`    | `Double` (read/write)  | Bounciness (0-1)                         |
| `Friction`       | `Double` (read/write)  | Surface friction (0-1)                   |
| `CollisionLayer` | `Integer` (read/write) | Collision layer bitmask                  |
| `CollisionMask`  | `Integer` (read/write) | Collision mask bitmask                   |

### Body Methods

| Method                  | Signature             | Description                         |
|-------------------------|-----------------------|-------------------------------------|
| `ApplyForce(fx, fy)`    | `Void(Double,Double)` | Apply force (accumulated until step)|
| `ApplyImpulse(ix, iy)`  | `Void(Double,Double)` | Apply instant velocity change       |
| `SetPos(x, y)`          | `Void(Double,Double)` | Set body position                   |
| `SetVel(vx, vy)`        | `Void(Double,Double)` | Set body velocity                   |

### Notes

- **Static bodies** (mass = 0) are immovable — use for floors, walls, platforms
- **Dynamic bodies** (mass > 0) are affected by gravity, forces, and collisions
- `Step(dt)` performs integration and AABB collision detection/resolution
- Collision response uses impulse-based resolution with restitution and friction
- Fixed timestep recommended (e.g., `Step(1.0 / 60.0)` for 60 FPS)

### Zia Example

```rust
module Physics2DDemo;

bind Viper.Game;
bind Viper.Terminal;

func start() {
    // Create world with gravity
    var world = Physics2D.World.New(0.0, 9.8);

    // Static floor (mass=0)
    var floor = Physics2D.Body.New(0.0, 500.0, 800.0, 50.0, 0.0);
    SayBool(floor.IsStatic);  // true

    // Dynamic ball (mass=1)
    var ball = Physics2D.Body.New(400.0, 50.0, 20.0, 20.0, 1.0);
    Physics2D.Body.set_Restitution(ball, 0.8);
    Physics2D.Body.set_Friction(ball, 0.3);

    // Add to world
    world.Add(floor);
    world.Add(ball);
    SayInt(world.BodyCount);  // 2

    // Apply impulse and simulate
    ball.ApplyImpulse(10.0, 0.0);
    var i = 0;
    while i < 10 {
        world.Step(0.016);
        i = i + 1;
    }

    // Direct position/velocity control
    ball.SetPos(100.0, 100.0);
    ball.SetVel(0.0, 0.0);

    // Change gravity
    world.SetGravity(0.0, 0.0);

    // Remove body
    world.Remove(ball);
    SayInt(world.BodyCount);  // 1
}
```

### BASIC Example

```basic
' Create a physics world with downward gravity
DIM world AS OBJECT = Viper.Game.Physics2D.World.New(0.0, 9.8)

' Create a static floor
DIM floor AS OBJECT = Viper.Game.Physics2D.Body.New(0.0, 500.0, 800.0, 50.0, 0.0)
world.Add(floor)

' Create a dynamic ball
DIM ball AS OBJECT = Viper.Game.Physics2D.Body.New(400.0, 50.0, 20.0, 20.0, 1.0)
ball.Restitution = 0.8  ' Bouncy
world.Add(ball)

' Apply an initial impulse
ball.ApplyImpulse(50.0, 0.0)

' Game loop
DO WHILE canvas.ShouldClose = 0
    canvas.Poll()
    world.Step(1.0 / 60.0)

    canvas.Clear(&H000020)
    canvas.BoxFilled(floor.X, floor.Y, floor.Width, floor.Height, &H888888)
    canvas.BoxFilled(ball.X, ball.Y, ball.Width, ball.Height, &HFF0000)
    canvas.Flip()
LOOP
```

---

## Viper.Game.Quadtree

Spatial partitioning data structure for efficient collision detection and spatial queries.

**Type:** Instance class (requires `New(x, y, width, height)`)

### Constructor

| Method                     | Signature                    | Description                         |
|----------------------------|------------------------------|-------------------------------------|
| `New(x, y, width, height)` | `Quadtree(Int,Int,Int,Int)`  | Create quadtree with bounds         |

### Properties

| Property      | Type                  | Description                          |
|---------------|-----------------------|--------------------------------------|
| `ItemCount`   | `Integer` (read-only) | Number of items in tree              |
| `ResultCount` | `Integer` (read-only) | Number of results from last query    |

### Methods

| Method                     | Signature          | Description                                      |
|----------------------------|--------------------|--------------------------------------------------|
| `Clear()`                  | `Void()`           | Remove all items                                 |
| `GetPairs()`               | `Integer()`        | Get potential collision pairs; returns count     |
| `GetResult(index)`         | `Integer(Integer)` | Get item ID from query results                   |
| `Insert(id, x, y, w, h)`  | `Boolean(5×Int)`   | Add item with bounds                             |
| `PairFirst(index)`         | `Integer(Integer)` | Get first ID of collision pair                   |
| `PairSecond(index)`        | `Integer(Integer)` | Get second ID of collision pair                  |
| `QueryPoint(x, y, radius)` | `Integer(3×Int)`   | Find items near point; returns count             |
| `QueryRect(x, y, w, h)`   | `Integer(4×Int)`   | Find items in rectangle; returns count           |
| `Remove(id)`               | `Boolean(Integer)` | Remove item by ID                                |
| `Update(id, x, y, w, h)`  | `Boolean(5×Int)`   | Update item position/size                        |

### Zia Example

```rust
module QuadtreeDemo;

bind Viper.Terminal;
bind Viper.Game.Quadtree as QT;
bind Viper.Fmt as Fmt;

func start() {
    var qt = QT.New(0, 0, 1000, 1000);
    qt.Insert(1, 100, 100, 50, 50);
    qt.Insert(2, 200, 200, 50, 50);
    qt.Insert(3, 800, 800, 50, 50);
    Say("Items: " + Fmt.Int(qt.get_ItemCount()));

    // Query for items in a region
    var count = qt.QueryRect(50, 50, 250, 250);
    Say("QueryRect found: " + Fmt.Int(count));
    var i = 0;
    while i < count {
        Say("  Result: " + Fmt.Int(qt.GetResult(i)));
        i = i + 1;
    }

    // Remove and re-query
    qt.Remove(2);
    Say("After remove: " + Fmt.Int(qt.get_ItemCount()));

    qt.Clear();
    Say("After clear: " + Fmt.Int(qt.get_ItemCount()));
}
```

### Example: Collision Detection

```basic
' Create quadtree for game world
DIM tree AS OBJECT = Viper.Game.Quadtree.New(0, 0, 800000, 600000)

' Add game objects (using fixed-point coordinates)
tree.Insert(1, playerX * 1000, playerY * 1000, 32000, 48000)
tree.Insert(2, enemy1X * 1000, enemy1Y * 1000, 32000, 32000)
tree.Insert(3, enemy2X * 1000, enemy2Y * 1000, 32000, 32000)

' Query for objects near player
DIM count AS INTEGER = tree.QueryPoint(playerX * 1000, playerY * 1000, 50000)
FOR i = 0 TO count - 1
    DIM id AS INTEGER = tree.GetResult(i)
    IF id <> 1 THEN  ' Not the player
        HandleCollision(1, id)
    END IF
NEXT

' Or get all potential collision pairs
DIM pairCount AS INTEGER = tree.GetPairs()
FOR i = 0 TO pairCount - 1
    DIM id1 AS INTEGER = tree.PairFirst(i)
    DIM id2 AS INTEGER = tree.PairSecond(i)
    CheckDetailedCollision(id1, id2)
NEXT
```

### Example: Bullet Optimization

```basic
' Instead of O(n²) collision checks
DIM tree AS OBJECT = Viper.Game.Quadtree.New(0, 0, 800000, 600000)

' Add all enemies to tree
FOR i = 0 TO enemyCount - 1
    tree.Insert(i, enemyX(i) * 1000, enemyY(i) * 1000, 32000, 32000)
NEXT

' For each bullet, only check nearby enemies
DIM slot AS INTEGER = bullets.FirstActive()
DO WHILE slot >= 0
    DIM bx AS INTEGER = bulletX(slot) * 1000
    DIM by AS INTEGER = bulletY(slot) * 1000

    ' Find enemies within bullet radius
    DIM hits AS INTEGER = tree.QueryPoint(bx, by, 20000)
    FOR i = 0 TO hits - 1
        DIM enemyId AS INTEGER = tree.GetResult(i)
        IF BulletHitsEnemy(slot, enemyId) THEN
            DamageEnemy(enemyId)
            bullets.Release(slot)
            EXIT FOR
        END IF
    NEXT

    slot = bullets.NextActive(slot)
LOOP
```

---


## See Also

- [Core Utilities](core.md)
- [Animation & Movement](animation.md)
- [Visual Effects](effects.md)
- [Game Utilities Overview](README.md)
- [Viper Runtime Library](../README.md)
