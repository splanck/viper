# Plan 03: Entity Base Class

## Context

Every game object in XENOSCAPE (player, 29 enemy types, pickups, projectiles) manually
manages parallel `List[Integer]` arrays for x, y, vx, vy, hp, direction, animFrame,
animTimer, state — and repeats a 13-line gravity+moveAndCollide+collision-response
pattern per entity type. PlatformerController, CollisionRect, and AnimStateMachine all
exist but are disconnected. Entity unifies them.

## Design

`Entity` is a lightweight game object that bundles:
- Position (x, y) in centipixels (x100 for subpixel precision)
- Velocity (vx, vy)
- Size (width, height) in pixels
- Direction (1 or -1)
- HP, MaxHP
- Active flag
- Built-in gravity + tilemap collision resolution

NOT an ECS. NOT a scene node. Just a data bundle with common physics methods.

## Changes

### New file: `src/runtime/game/rt_entity.c` (~350 LOC)

**Entity struct:**
```c
typedef struct rt_entity_impl {
    int64_t x, y;           // Position (centipixels, x100)
    int64_t vx, vy;         // Velocity (centipixels per 16ms frame)
    int64_t width, height;  // Size (pixels)
    int64_t dir;            // Facing: 1 = right, -1 = left
    int64_t hp, max_hp;
    int64_t type;           // User-defined type ID
    int8_t active;
    int8_t on_ground;       // Set by MoveAndCollide
    int8_t hit_left, hit_right, hit_ceiling; // Set by MoveAndCollide
} rt_entity_impl;
```

**Core methods:**
```c
void *rt_entity_new(int64_t x, int64_t y, int64_t w, int64_t h);
void rt_entity_set_pos(void *ent, int64_t x, int64_t y);
void rt_entity_set_velocity(void *ent, int64_t vx, int64_t vy);

// Apply gravity and cap fall speed. Call once per frame.
void rt_entity_apply_gravity(void *ent, int64_t gravity, int64_t max_fall, int64_t dt);

// Move entity by velocity*dt, resolve against tilemap. Sets on_ground/hit_* flags.
// This is the 13-line pattern extracted into one call.
void rt_entity_move_and_collide(void *ent, void *tilemap, int64_t dt);

// Convenience: apply gravity + move + collide in one call.
void rt_entity_update_physics(void *ent, void *tilemap,
                              int64_t gravity, int64_t max_fall, int64_t dt);

// Edge detection: returns true if no solid tile below the entity's leading edge.
int8_t rt_entity_at_edge(void *ent, void *tilemap);

// Reverse direction on wall hit (common patrol pattern).
void rt_entity_patrol_reverse(void *ent, int64_t speed);

// AABB overlap with another entity.
int8_t rt_entity_overlaps(void *ent, void *other);

// Property accessors
int64_t rt_entity_get_x(void *ent);
// ... (standard get/set for all fields)
```

**MoveAndCollide implementation** (the key function):
```c
void rt_entity_move_and_collide(void *ent, void *tilemap, int64_t dt) {
    rt_entity_impl *e = (rt_entity_impl *)ent;
    int64_t dispX = e->vx * dt / 16; // DT_BASE = 16
    int64_t dispY = e->vy * dt / 16;

    // Reuse existing physics.zia algorithm: resolve X then Y
    // Check tile solidity via tilemap collision layer
    // Set on_ground, hit_left, hit_right, hit_ceiling flags
    // Zero velocity component on collision
    // ... (~60 lines, matching physics.zia:50-115)
}
```

### New file: `src/runtime/game/rt_entity.h` (~50 LOC)

### runtime.def entries
```
RT_CLASS_BEGIN("Viper.Game.Entity", Entity, "obj", EntityNew)
    RT_PROP("X", "i64", EntityGetX, EntitySetX)
    RT_PROP("Y", "i64", EntityGetY, EntitySetY)
    RT_PROP("VX", "i64", EntityGetVX, EntitySetVX)
    RT_PROP("VY", "i64", EntityGetVY, EntitySetVY)
    RT_PROP("Width", "i64", EntityGetWidth, none)
    RT_PROP("Height", "i64", EntityGetHeight, none)
    RT_PROP("Dir", "i64", EntityGetDir, EntitySetDir)
    RT_PROP("HP", "i64", EntityGetHP, EntitySetHP)
    RT_PROP("MaxHP", "i64", EntityGetMaxHP, EntitySetMaxHP)
    RT_PROP("Type", "i64", EntityGetType, EntitySetType)
    RT_PROP("Active", "i1", EntityGetActive, EntitySetActive)
    RT_PROP("OnGround", "i1", EntityOnGround, none)
    RT_PROP("HitLeft", "i1", EntityHitLeft, none)
    RT_PROP("HitRight", "i1", EntityHitRight, none)
    RT_PROP("HitCeiling", "i1", EntityHitCeiling, none)
    RT_METHOD("ApplyGravity", "void(i64,i64,i64)", EntityApplyGravity)
    RT_METHOD("MoveAndCollide", "void(obj,i64)", EntityMoveAndCollide)
    RT_METHOD("UpdatePhysics", "void(obj,i64,i64,i64)", EntityUpdatePhysics)
    RT_METHOD("AtEdge", "i1(obj)", EntityAtEdge)
    RT_METHOD("PatrolReverse", "void(i64)", EntityPatrolReverse)
    RT_METHOD("Overlaps", "i1(obj)", EntityOverlaps)
RT_CLASS_END()
```

Plus corresponding RT_FUNC entries for each method.

### Zia usage after change
```zia
// Before (13 lines per enemy per frame):
var curVy = evy.get(i) + GRAVITY * dt / DT_BASE;
if curVy > MAX_FALL { curVy = MAX_FALL; }
evy.set(i, curVy);
var dispX = evx.get(i) * dt / DT_BASE;
var dispY = curVy * dt / DT_BASE;
var result = phys.moveAndCollide(ex.get(i), ey.get(i), dispX, dispY, W, H, level);
ex.set(i, result.x); ey.set(i, result.y);
if result.hitLeft { evx.set(i, 0); } ...

// After (1 line):
enemy.UpdatePhysics(tilemap, GRAVITY, MAX_FALL, dt)
```

### Files to modify
- New: `src/runtime/game/rt_entity.c` (~350 LOC)
- New: `src/runtime/game/rt_entity.h` (~50 LOC)
- `src/il/runtime/runtime.def` — ~25 new entries (class + props + methods)
- `src/il/runtime/RuntimeSignatures.cpp` — include rt_entity.h
- `src/il/runtime/classes/RuntimeClasses.hpp` — add RTCLS_Entity
- `src/runtime/CMakeLists.txt` — add source file

### Tests

**File:** `src/tests/unit/runtime/TestEntity.cpp`
```
TEST(Entity, CreateAndGetPos)
TEST(Entity, ApplyGravityCapsMaxFall)
TEST(Entity, MoveAndCollideHitsFloor)
TEST(Entity, MoveAndCollideHitsWall)
TEST(Entity, UpdatePhysicsCombined)
TEST(Entity, AtEdgeDetectsPlatformEnd)
TEST(Entity, PatrolReverseOnWall)
TEST(Entity, OverlapsAABB)
TEST(Entity, VelocityZeroedOnCollision)
TEST(Entity, NullTilemapSafe)
```

### Doc update
- New: `docs/viperlib/game/entity.md`
