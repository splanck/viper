# Viper.Game.Entity

Lightweight 2D game object combining position, velocity, size, health, and built-in tilemap collision.

## Overview

Entity replaces the common pattern of parallel arrays + manual gravity/moveAndCollide code. One `UpdatePhysics()` call replaces 13+ lines of gravity + collision + flag checking.

## API

### Entity.New(x, y, width, height) -> Entity
Create entity at position (x, y) in centipixels (x100) with size in pixels.

### Properties
- `X`, `Y` — Position in centipixels (get/set)
- `VX`, `VY` — Velocity in centipixels per frame (get/set)
- `Width`, `Height` — Size in pixels (read-only)
- `Dir` — Facing direction: 1=right, -1=left (get/set)
- `HP`, `MaxHP` — Health points (get/set)
- `Type` — User-defined type ID (get/set)
- `Active` — Active flag (get/set)
- `OnGround`, `HitLeft`, `HitRight`, `HitCeiling` — Collision flags (read-only, set by MoveAndCollide)

### Methods
- `ApplyGravity(gravity, maxFall, dt)` — Apply gravity to VY, cap at maxFall
- `MoveAndCollide(tilemap, dt)` — Move by velocity, resolve against tilemap, set collision flags
- `UpdatePhysics(tilemap, gravity, maxFall, dt)` — Combined ApplyGravity + MoveAndCollide
- `AtEdge(tilemap)` — Returns true if no solid tile below leading edge (for patrol AI)
- `PatrolReverse(speed)` — Reverse direction on wall hit, set VX to ±speed
- `Overlaps(other)` — AABB overlap test with another Entity

## Example
```zia
var enemy = Entity.New(10000, 5000, 24, 16)
enemy.set_HP(3)
enemy.set_VX(100)

// Per frame:
enemy.UpdatePhysics(tilemap, 78, 1350, dt)
if enemy.get_OnGround() { /* can jump */ }
enemy.PatrolReverse(100)
if enemy.AtEdge(tilemap) { enemy.set_Dir(0 - enemy.get_Dir()) }
```
