# Plan 06: Behavior Composition System

## Context

XENOSCAPE's enemy.zia (1943 lines, 29 types) copy-pastes the same patterns:
Gravity → MoveAndCollide → animation timer → edge reverse → wall reverse.
Only the specific behavior differs (patrol speed, chase logic, shoot cooldown).
No AI framework exists in the runtime — this is entirely new.

## Design

NOT behavior trees (too complex). Instead: lightweight composable **behavior presets**
that attach to Entity objects. Each behavior is a flag + parameters stored on the entity.

The key insight: most 2D game enemies are combinations of 5-6 atomic behaviors.
Rather than a generic tree, provide these as named presets.

## Changes

### New file: `src/runtime/game/rt_behavior.c` (~400 LOC)

**Behavior is a configuration struct stored per-entity:**
```c
#define BHV_PATROL        (1 << 0)  // Walk back and forth
#define BHV_CHASE          (1 << 1)  // Move toward target
#define BHV_GRAVITY        (1 << 2)  // Apply gravity each frame
#define BHV_EDGE_REVERSE   (1 << 3)  // Reverse at platform edges
#define BHV_WALL_REVERSE   (1 << 4)  // Reverse on wall hit
#define BHV_SHOOT          (1 << 5)  // Fire projectiles on cooldown
#define BHV_SINE_FLOAT     (1 << 6)  // Sine-wave vertical movement (bats, moths)
#define BHV_ANIM_LOOP      (1 << 7)  // Auto-advance animation frames

typedef struct rt_behavior_impl {
    uint32_t flags;          // Bitmask of active behaviors

    // Patrol
    int64_t patrol_speed;

    // Chase
    int64_t chase_speed;
    int64_t chase_range;     // Activate within this distance

    // Gravity
    int64_t gravity;
    int64_t max_fall;

    // Shoot
    int64_t shoot_cooldown;  // ms between shots
    int64_t shoot_timer;     // current countdown
    int8_t shoot_ready;      // flag: ready to fire this frame

    // Sine float
    int64_t float_amplitude;
    int64_t float_speed;
    int64_t float_phase;     // accumulator

    // Animation
    int64_t anim_frames;
    int64_t anim_ms;
    int64_t anim_timer;
    int64_t anim_frame;
} rt_behavior_impl;
```

**Core function: UpdateBehavior**
```c
// Apply all enabled behaviors to an entity.
// target_x/y: player position (for Chase/Shoot). Pass 0,0 if unused.
void rt_behavior_update(void *behavior, void *entity, void *tilemap,
                        int64_t target_x, int64_t target_y, int64_t dt);
```

Implementation:
1. If BHV_GRAVITY: apply gravity to entity vy, cap at max_fall
2. If BHV_PATROL: set entity vx to patrol_speed * dir
3. If BHV_CHASE: if distance to target < chase_range, move toward target
4. Move entity via entity.MoveAndCollide(tilemap, dt)
5. If BHV_WALL_REVERSE: check hit_left/hit_right, reverse dir
6. If BHV_EDGE_REVERSE: check AtEdge, reverse dir
7. If BHV_SHOOT: decrement timer, set shoot_ready flag
8. If BHV_SINE_FLOAT: accumulate phase, apply sine offset to vy
9. If BHV_ANIM_LOOP: advance animation frame counter

**Builder API:**
```c
void *rt_behavior_new(void);
void rt_behavior_add_patrol(void *bhv, int64_t speed);
void rt_behavior_add_chase(void *bhv, int64_t speed, int64_t range);
void rt_behavior_add_gravity(void *bhv, int64_t gravity, int64_t max_fall);
void rt_behavior_add_edge_reverse(void *bhv);
void rt_behavior_add_wall_reverse(void *bhv);
void rt_behavior_add_shoot(void *bhv, int64_t cooldown_ms);
void rt_behavior_add_sine_float(void *bhv, int64_t amplitude, int64_t speed);
void rt_behavior_add_anim_loop(void *bhv, int64_t frame_count, int64_t ms_per_frame);
int8_t rt_behavior_shoot_ready(void *bhv);  // check + clear flag
int64_t rt_behavior_anim_frame(void *bhv);  // current animation frame
```

### runtime.def — ~15 entries
```
RT_CLASS_BEGIN("Viper.Game.Behavior", Behavior, "obj", BehaviorNew)
    RT_METHOD("AddPatrol",      "void(i64)",      BehaviorAddPatrol)
    RT_METHOD("AddChase",       "void(i64,i64)",  BehaviorAddChase)
    RT_METHOD("AddGravity",     "void(i64,i64)",  BehaviorAddGravity)
    RT_METHOD("AddEdgeReverse", "void()",          BehaviorAddEdgeReverse)
    RT_METHOD("AddWallReverse", "void()",          BehaviorAddWallReverse)
    RT_METHOD("AddShoot",       "void(i64)",       BehaviorAddShoot)
    RT_METHOD("AddSineFloat",   "void(i64,i64)",  BehaviorAddSineFloat)
    RT_METHOD("AddAnimLoop",    "void(i64,i64)",   BehaviorAddAnimLoop)
    RT_METHOD("Update",         "void(obj,obj,i64,i64,i64)", BehaviorUpdate)
    RT_PROP("ShootReady",       "i1",  BehaviorShootReady, none)
    RT_PROP("AnimFrame",        "i64", BehaviorAnimFrame, none)
RT_CLASS_END()
```

### Zia usage
```zia
// Define slime behavior (once)
var slimeBhv = Behavior.New()
slimeBhv.AddPatrol(100)
slimeBhv.AddGravity(78, 1350)
slimeBhv.AddEdgeReverse()
slimeBhv.AddWallReverse()
slimeBhv.AddAnimLoop(4, 120)

// Per frame (replaces 60+ lines per enemy type):
slimeBhv.Update(entity, tilemap, playerX, playerY, dt)
var frame = slimeBhv.get_AnimFrame()
```

### Dependency
- Requires Plan 03 (Entity class) — Behavior.Update takes Entity + Tilemap.

### Files to modify
- New: `src/runtime/game/rt_behavior.c` (~400 LOC)
- New: `src/runtime/game/rt_behavior.h` (~50 LOC)
- `src/il/runtime/runtime.def` — ~15 entries
- `src/il/runtime/RuntimeSignatures.cpp` — include header
- `src/il/runtime/classes/RuntimeClasses.hpp` — add RTCLS_Behavior
- `src/runtime/CMakeLists.txt` — add source

### Tests

**File:** `src/tests/unit/runtime/TestBehavior.cpp`
```
TEST(Behavior, PatrolMovesEntity)
TEST(Behavior, GravityApplied)
TEST(Behavior, EdgeReverseAtPlatformEnd)
TEST(Behavior, WallReverseOnCollision)
TEST(Behavior, ChaseMovesTowardTarget)
TEST(Behavior, ChaseIgnoresOutOfRange)
TEST(Behavior, ShootCooldown)
TEST(Behavior, SineFloatOscillates)
TEST(Behavior, AnimLoopAdvancesFrames)
TEST(Behavior, CombinedPatrolGravityEdge)
  — Verify all 3 behaviors work together in one Update call
```

### Doc update
- New: `docs/viperlib/game/behavior.md`
