# Plan: Physics Joints & Constraints

## 1. Summary & Objective

Add joint/constraint types to the existing `Viper.Game.Physics2D` system: DistanceJoint, SpringJoint, HingeJoint, and RopeJoint. Also adds circle collision shapes alongside the existing AABB shapes.

**Why:** Physics2D is AABB-only with no joints. Cannot create swinging mechanics, rope/chain physics, vehicles, physics puzzles, or connected objects. Joints enable entire game genres (physics puzzles, platformers with swinging, wrecking ball games, vehicles).

## 2. Scope

**In scope:**
- DistanceJoint: maintains fixed distance between two bodies
- SpringJoint: elastic connection with stiffness and damping
- HingeJoint: rotation around anchor point (constraint only, no motor)
- RopeJoint: maximum distance constraint (slack allowed)
- Circle collision shape for bodies
- Joints solved iteratively per physics step
- Max 64 joints per world

**Out of scope:**
- Full rotational physics (angular velocity, torque, moment of inertia)
- Joint motors (powered rotation/translation)
- Prismatic joints (slider)
- Weld joints (rigid connection)
- Breakable joints
- Joint limits (angle limits on hinge)
- Continuous collision detection (CCD)
- Collision between circles and AABBs (mixed shapes)

## 3. Zero-Dependency Implementation Strategy

Joints are position-based constraints solved with iterative relaxation (Gauss-Seidel). Each joint stores its two body references, parameters, and applies corrective impulses during the world step. This is the same approach used by Box2D Lite and most educational physics engines — well-documented, ~100 LOC per joint type.

Circle collision uses distance-squared comparison (no sqrt for broadphase) with penetration vector for resolution. ~80 LOC.

## 4. Technical Requirements

### New Files
- `src/runtime/graphics/rt_physics2d_joint.h` — joint API declarations
- `src/runtime/graphics/rt_physics2d_joint.c` — joint implementations (~500 LOC)

### Modified Files
- `src/runtime/graphics/rt_physics2d.h` — add circle body constructor, joint integration
- `src/runtime/graphics/rt_physics2d.c` — integrate joint solving into world step, circle collision

### C API (rt_physics2d_joint.h)

```c
// Joint types
#define RT_JOINT_DISTANCE  0
#define RT_JOINT_SPRING    1
#define RT_JOINT_HINGE     2
#define RT_JOINT_ROPE      3

// === Distance Joint ===
void *rt_physics2d_distance_joint_new(void *body_a, void *body_b, double length);
double rt_physics2d_distance_joint_get_length(void *joint);
void   rt_physics2d_distance_joint_set_length(void *joint, double length);

// === Spring Joint ===
void *rt_physics2d_spring_joint_new(void *body_a, void *body_b,
                                     double rest_length, double stiffness,
                                     double damping);
double rt_physics2d_spring_joint_get_stiffness(void *joint);
void   rt_physics2d_spring_joint_set_stiffness(void *joint, double stiffness);
double rt_physics2d_spring_joint_get_damping(void *joint);
void   rt_physics2d_spring_joint_set_damping(void *joint, double damping);

// === Hinge Joint ===
void *rt_physics2d_hinge_joint_new(void *body_a, void *body_b,
                                    double anchor_x, double anchor_y);
double rt_physics2d_hinge_joint_get_angle(void *joint);

// === Rope Joint ===
void *rt_physics2d_rope_joint_new(void *body_a, void *body_b, double max_length);
double rt_physics2d_rope_joint_get_max_length(void *joint);
void   rt_physics2d_rope_joint_set_max_length(void *joint, double max_length);

// === Joint Common ===
void   rt_physics2d_joint_destroy(void *joint);
void  *rt_physics2d_joint_get_body_a(void *joint);
void  *rt_physics2d_joint_get_body_b(void *joint);
int64_t rt_physics2d_joint_get_type(void *joint);
int8_t  rt_physics2d_joint_is_active(void *joint);

// === World Joint Management ===
void   rt_physics2d_world_add_joint(void *world, void *joint);
void   rt_physics2d_world_remove_joint(void *world, void *joint);
int64_t rt_physics2d_world_joint_count(void *world);

// === Circle Bodies ===
void  *rt_physics2d_circle_body_new(double cx, double cy, double radius, double mass);
double rt_physics2d_body_radius(void *body);       // 0 for AABB bodies
int8_t rt_physics2d_body_is_circle(void *body);
```

### Internal Joint Structure

```c
#define PH_MAX_JOINTS 64
#define PH_JOINT_ITERATIONS 4  // Iterative solver passes

typedef struct {
    int32_t  type;        // RT_JOINT_DISTANCE, etc.
    void    *body_a;
    void    *body_b;
    double   anchor_ax, anchor_ay;  // Local anchor on body A
    double   anchor_bx, anchor_by;  // Local anchor on body B
    // Type-specific params:
    double   length;       // Distance/rope target
    double   stiffness;    // Spring only
    double   damping;      // Spring only
    int8_t   active;
} ph_joint;
```

### Physics Step Integration

In `rt_physics2d_world_step()`, after velocity integration and before collision resolution:
```
for iteration in 0..PH_JOINT_ITERATIONS:
    for each joint:
        solve_joint_constraint(joint, dt)
```

## 5. runtime.def Registration

```c
//=============================================================================
// GAME - PHYSICS 2D JOINTS
//=============================================================================

// Circle body
RT_FUNC(Physics2DCircleBodyNew,  rt_physics2d_circle_body_new, "Viper.Game.Physics2D.CircleBody.New", "obj(f64,f64,f64,f64)")
RT_FUNC(Physics2DBodyRadius,     rt_physics2d_body_radius,     "Viper.Game.Physics2D.Body.get_Radius","f64(obj)")
RT_FUNC(Physics2DBodyIsCircle,   rt_physics2d_body_is_circle,  "Viper.Game.Physics2D.Body.get_IsCircle","i1(obj)")

// Distance Joint
RT_FUNC(DistanceJointNew,      rt_physics2d_distance_joint_new,       "Viper.Game.Physics2D.DistanceJoint.New",       "obj(obj,obj,f64)")
RT_FUNC(DistanceJointGetLen,   rt_physics2d_distance_joint_get_length,"Viper.Game.Physics2D.DistanceJoint.get_Length", "f64(obj)")
RT_FUNC(DistanceJointSetLen,   rt_physics2d_distance_joint_set_length,"Viper.Game.Physics2D.DistanceJoint.set_Length", "void(obj,f64)")

// Spring Joint
RT_FUNC(SpringJointNew,       rt_physics2d_spring_joint_new,        "Viper.Game.Physics2D.SpringJoint.New",       "obj(obj,obj,f64,f64,f64)")
RT_FUNC(SpringJointGetStiff,  rt_physics2d_spring_joint_get_stiffness,"Viper.Game.Physics2D.SpringJoint.get_Stiffness","f64(obj)")
RT_FUNC(SpringJointSetStiff,  rt_physics2d_spring_joint_set_stiffness,"Viper.Game.Physics2D.SpringJoint.set_Stiffness","void(obj,f64)")
RT_FUNC(SpringJointGetDamp,   rt_physics2d_spring_joint_get_damping, "Viper.Game.Physics2D.SpringJoint.get_Damping","f64(obj)")
RT_FUNC(SpringJointSetDamp,   rt_physics2d_spring_joint_set_damping, "Viper.Game.Physics2D.SpringJoint.set_Damping","void(obj,f64)")

// Hinge Joint
RT_FUNC(HingeJointNew,        rt_physics2d_hinge_joint_new,         "Viper.Game.Physics2D.HingeJoint.New",       "obj(obj,obj,f64,f64)")
RT_FUNC(HingeJointGetAngle,   rt_physics2d_hinge_joint_get_angle,   "Viper.Game.Physics2D.HingeJoint.get_Angle", "f64(obj)")

// Rope Joint
RT_FUNC(RopeJointNew,         rt_physics2d_rope_joint_new,          "Viper.Game.Physics2D.RopeJoint.New",        "obj(obj,obj,f64)")
RT_FUNC(RopeJointGetMaxLen,   rt_physics2d_rope_joint_get_max_length,"Viper.Game.Physics2D.RopeJoint.get_MaxLength","f64(obj)")
RT_FUNC(RopeJointSetMaxLen,   rt_physics2d_rope_joint_set_max_length,"Viper.Game.Physics2D.RopeJoint.set_MaxLength","void(obj,f64)")

// Joint common
RT_FUNC(JointGetBodyA,   rt_physics2d_joint_get_body_a,  "Viper.Game.Physics2D.Joint.get_BodyA",  "obj(obj)")
RT_FUNC(JointGetBodyB,   rt_physics2d_joint_get_body_b,  "Viper.Game.Physics2D.Joint.get_BodyB",  "obj(obj)")
RT_FUNC(JointGetType,    rt_physics2d_joint_get_type,    "Viper.Game.Physics2D.Joint.get_Type",   "i64(obj)")
RT_FUNC(JointIsActive,   rt_physics2d_joint_is_active,   "Viper.Game.Physics2D.Joint.get_IsActive","i1(obj)")

// World joint management
RT_FUNC(WorldAddJoint,    rt_physics2d_world_add_joint,    "Viper.Game.Physics2D.World.AddJoint",    "void(obj,obj)")
RT_FUNC(WorldRemoveJoint, rt_physics2d_world_remove_joint, "Viper.Game.Physics2D.World.RemoveJoint", "void(obj,obj)")
RT_FUNC(WorldJointCount,  rt_physics2d_world_joint_count,  "Viper.Game.Physics2D.World.get_JointCount","i64(obj)")

// RT_CLASS blocks for each joint type
RT_CLASS_BEGIN("Viper.Game.Physics2D.DistanceJoint", DistanceJoint, "obj", DistanceJointNew)
    RT_PROP("Length", "f64", DistanceJointGetLen, DistanceJointSetLen)
    RT_PROP("BodyA", "obj", JointGetBodyA, none)
    RT_PROP("BodyB", "obj", JointGetBodyB, none)
    RT_PROP("IsActive", "i1", JointIsActive, none)
RT_CLASS_END()

RT_CLASS_BEGIN("Viper.Game.Physics2D.SpringJoint", SpringJoint, "obj", SpringJointNew)
    RT_PROP("Stiffness", "f64", SpringJointGetStiff, SpringJointSetStiff)
    RT_PROP("Damping", "f64", SpringJointGetDamp, SpringJointSetDamp)
    RT_PROP("BodyA", "obj", JointGetBodyA, none)
    RT_PROP("BodyB", "obj", JointGetBodyB, none)
    RT_PROP("IsActive", "i1", JointIsActive, none)
RT_CLASS_END()

RT_CLASS_BEGIN("Viper.Game.Physics2D.HingeJoint", HingeJoint, "obj", HingeJointNew)
    RT_PROP("Angle", "f64", HingeJointGetAngle, none)
    RT_PROP("BodyA", "obj", JointGetBodyA, none)
    RT_PROP("BodyB", "obj", JointGetBodyB, none)
    RT_PROP("IsActive", "i1", JointIsActive, none)
RT_CLASS_END()

RT_CLASS_BEGIN("Viper.Game.Physics2D.RopeJoint", RopeJoint, "obj", RopeJointNew)
    RT_PROP("MaxLength", "f64", RopeJointGetMaxLen, RopeJointSetMaxLen)
    RT_PROP("BodyA", "obj", JointGetBodyA, none)
    RT_PROP("BodyB", "obj", JointGetBodyB, none)
    RT_PROP("IsActive", "i1", JointIsActive, none)
RT_CLASS_END()
```

## 6. CMakeLists.txt Changes

In `src/runtime/CMakeLists.txt`, add to `RT_GRAPHICS_SOURCES`:
```cmake
graphics/rt_physics2d_joint.c
```

## 7. Error Handling

| Scenario | Behavior |
|----------|----------|
| NULL body passed to joint constructor | Return NULL |
| Same body for both A and B | Return NULL (self-joint meaningless) |
| Negative length/stiffness/damping | Clamp to 0 |
| World at 64 joints, adding more | Return silently (joint not added) |
| Joint with body not in world | Constraint has no effect until body added |
| Body removed from world while joint exists | Joint auto-deactivated |
| Circle vs AABB collision | Not supported initially — circles collide only with other circles |
| Negative radius for circle body | Clamp to 1.0 |

## 8. Tests

### Zia Runtime Tests (`tests/runtime/test_physics_joints.zia`)

1. **Distance joint maintains distance**
   - Given: Two bodies connected by DistanceJoint(length=100)
   - When: Bodies pulled apart, world stepped
   - Then: Distance converges toward 100 (within tolerance)

2. **Spring joint oscillation**
   - Given: Static body + dynamic body connected by SpringJoint
   - When: Dynamic body displaced, world stepped 100 times
   - Then: Dynamic body oscillates around rest position

3. **Rope joint max distance**
   - Given: Two bodies connected by RopeJoint(maxLength=50)
   - When: Bodies separated to 80 apart, world stepped
   - Then: Distance clamped to ≤ 50

4. **Circle collision detection**
   - Given: Two circle bodies overlapping
   - When: World stepped
   - Then: Bodies separate (no overlap)

5. **Joint count management**
   - Given: World with 3 joints
   - When: `world.RemoveJoint(joint)`
   - Then: `world.JointCount == 2`

6. **NULL body safety**
   - When: `DistanceJoint.New(null, body, 100.0)`
   - Then: Returns null (no crash)

## 9. Documentation Deliverables

| Action | File |
|--------|------|
| CREATE | `docs/viperlib/game/joints.md` — full joint API reference with examples |
| UPDATE | `docs/viperlib/game/physics.md` — add joints and circle bodies section |
| UPDATE | `docs/viperlib/game.md` — add Physics2D joints to contents |

## 10. Code References

| File | Role |
|------|------|
| `src/runtime/graphics/rt_physics2d.c` | **Primary file to extend** — world step, body collision |
| `src/runtime/graphics/rt_physics2d.h` | **Primary header to extend** — body API |
| `src/runtime/collections/rt_collision.c` | Pattern: geometric collision detection |
| `src/il/runtime/runtime.def` | Registration (add after Physics2D Body entries) |
