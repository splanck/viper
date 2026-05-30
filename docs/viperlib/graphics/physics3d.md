---
status: active
audience: public
last-verified: 2026-05-26
---

# 3D Physics
> Physics3DWorld, PhysicsHit3D, PhysicsHitList3D, CollisionEvent3D, ContactPoint3D, Collider3D, Physics3DBody, Character3D, DistanceJoint3D, SpringJoint3D

**Part of [Viper Runtime Library](../README.md) › [Graphics](README.md)**

---

This page covers the `Viper.Graphics3D` physics and movement surface that is available
through the runtime library in both Zia and BASIC. For the broader 3D rendering guide,
asset pipeline, and scene examples, see [Graphics 3D Guide](../../graphics3d-guide.md).

---

## Viper.Graphics3D.Physics3DWorld

3D rigid-body simulation world with manual stepping, gravity control, collision contact access,
and joint integration.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics3D.Physics3DWorld(gx, gy, gz)`

### Properties

| Property         | Type    | Access | Description |
|------------------|---------|--------|-------------|
| `BodyCount`      | Integer | Read   | Number of active bodies in the world |
| `CollisionCount` | Integer | Read   | Number of contacts from the most recent `Step()` |
| `CollisionEventCount` | Integer | Read | Number of current collision events from the most recent `Step()` |
| `EnterEventCount` | Integer | Read | Number of collision pairs that began touching this step |
| `StayEventCount` | Integer | Read | Number of collision pairs that remained touching this step |
| `ExitEventCount` | Integer | Read | Number of collision pairs that stopped touching this step |
| `JointCount`     | Integer | Read   | Number of active joints |
| `LastCCDRequestedSubsteps` | Integer | Read | Unclamped CCD substep demand from the most recent `Step()` |
| `LastCCDSubsteps` | Integer | Read | Actual CCD substeps used after applying the runtime cap |
| `CCDSubstepClampedCount` | Integer | Read | Number of steps whose CCD demand exceeded the cap |
| `SolverIterations` | Integer | Read | Iterative constraint-solver passes used by `Step()` |

### Methods

| Method                    | Signature             | Description |
|---------------------------|-----------------------|-------------|
| `Step(dt)`                | `Void(Double)`        | Advance simulation by `dt` seconds |
| `Add(body)`               | `Void(Object)`        | Add a `Physics3DBody` to the world |
| `TryAdd(body)`            | `Boolean(Object)`     | Add a body and report allocation/validation failure without changing the world |
| `Remove(body)`            | `Void(Object)`        | Remove a body from the world |
| `ContainsBody(body)`      | `Boolean(Object)`     | Return whether the body is currently registered in the world |
| `SetSolverIterations(iterations)` | `Void(Integer)` | Tune iterative solver passes, clamped to `1..64` |
| `SetGravity(x, y, z)`     | `Void(Double, Double, Double)` | Change the gravity vector |
| `AddJoint(joint, type)`   | `Void(Object, Integer)` | Add a joint (`0 = DistanceJoint3D`, `1 = SpringJoint3D`, `2 = HingeJoint3D`, `3 = RopeJoint3D`, `4 = SixDofJoint3D`) |
| `RemoveJoint(joint)`      | `Void(Object)`        | Remove a joint from the world |
| `Raycast(origin, direction, maxDistance, mask)` | `Object(Object, Object, Double, Integer)` | Return the nearest `PhysicsHit3D` or `Nothing` |
| `RaycastAll(origin, direction, maxDistance, mask)` | `Object(Object, Object, Double, Integer)` | Return a sorted `PhysicsHitList3D` or `Nothing` |
| `SweepSphere(center, radius, delta, mask)` | `Object(Object, Double, Object, Integer)` | Sweep a sphere and return the first `PhysicsHit3D` or `Nothing` |
| `SweepCapsule(a, b, radius, delta, mask)` | `Object(Object, Object, Double, Object, Integer)` | Sweep a capsule segment and return the first `PhysicsHit3D` or `Nothing` |
| `OverlapSphere(center, radius, mask)` | `Object(Object, Double, Integer)` | Return a `PhysicsHitList3D` of overlaps or `Nothing` |
| `OverlapAABB(min, max, mask)` | `Object(Object, Object, Integer)` | Return a `PhysicsHitList3D` of overlaps or `Nothing` |
| `GetCollisionBodyA(i)`    | `Object(Integer)`     | Get the first body in contact pair `i` |
| `GetCollisionBodyB(i)`    | `Object(Integer)`     | Get the second body in contact pair `i` |
| `GetCollisionNormal(i)`   | `Object(Integer)`     | Get the contact normal as a `Vec3` |
| `GetCollisionDepth(i)`    | `Double(Integer)`     | Get the penetration depth for contact `i` |
| `GetCollisionEvent(i)`    | `Object(Integer)`     | Get the current `CollisionEvent3D` at index `i` |
| `GetEnterEvent(i)`        | `Object(Integer)`     | Get an enter `CollisionEvent3D` |
| `GetStayEvent(i)`         | `Object(Integer)`     | Get a stay `CollisionEvent3D` |
| `GetExitEvent(i)`         | `Object(Integer)`     | Get an exit `CollisionEvent3D` |

### Notes

- `Step()` is explicit; the world does not simulate itself automatically.
- Contact queries reflect the latest completed step.
- Query `mask` uses the same layer bits as `Physics3DBody.CollisionLayer`. A mask of `0` matches no layers; use `-1` or an all-layers mask when you want any layer.
- Static bodies are immovable. Kinematic bodies move from explicit velocity but do not
  receive gravity or force integration.
- `Add(body)` keeps the historical void API. `TryAdd(body)` returns `false` for invalid handles or allocation failure, returns `true` for already-present bodies, and leaves the body count stable on duplicates.
- World storage for bodies, contacts, contact events, and joints grows on demand from production-sized initial capacities. Query result lists store a bounded nearest/result prefix for predictable allocation behavior, while `PhysicsHitList3D.TotalCount` and `Truncated` expose whether more matches existed.
- Collision detection uses a sweep-and-prune broadphase before shape-specific narrow-phase tests. Box colliders honor body and compound-child orientation.
- The unit lane includes a sparse 321-body step stress that exercises body
  storage growth, broadphase scratch growth, and dynamic integration without
  producing contacts.
- World queries reuse the broadphase and honor each body's collision scale before running shape tests.
- `Raycast` and `RaycastAll` test collider geometry, not only broadphase bounds: boxes, spheres, capsules, compound leaves, mesh/convex triangles, and heightfields report nearest shape hits. Mesh raycasts build and reuse a per-mesh BVH, and heightfield raycasts adapt their step to the heightfield cell spacing.
- Mesh colliders use the per-mesh BVH to prune candidate triangles for sphere,
  capsule, and box contacts, with a full triangle scan retained as a correctness
  fallback. Convex hulls use support-point GJK/EPA for hull-vs-hull and
  hull-vs-simple contacts.
- Sphere sweeps use analytic tests against primitive spheres and boxes before falling back to adaptive sampling. Capsule sweeps use adaptive sampling, so small-radius sweeps and long capsules can hit thin geometry without a fixed world-unit step floor.
- `LastCCDRequestedSubsteps`, `LastCCDSubsteps`, and `CCDSubstepClampedCount` are diagnostics for fast-body tuning; clamping is expected when very high velocity would require more substeps than the engine's safety cap.
- `SolverIterations` defaults to `6` and drives both contact-resolution passes and joint
  constraint passes. `SetSolverIterations()` clamps to `1..64`; higher values can reduce
  penetration and make constraints stiffer at additional CPU cost.

---

## Viper.Graphics3D.PhysicsHit3D

Query result object returned by `Raycast`, `SweepSphere`, and `SweepCapsule`.

**Type:** Instance (obj)

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Body` | Object | Read | Hit `Physics3DBody` |
| `Collider` | Object | Read | Hit `Collider3D` leaf collider |
| `Point` | Object (`Vec3`) | Read | Contact point approximation |
| `Normal` | Object (`Vec3`) | Read | Surface normal |
| `Distance` | Double | Read | Distance travelled before the hit |
| `Fraction` | Double | Read | `Distance / maxDistance` |
| `StartedPenetrating` | Boolean | Read | Query began already overlapping the target |
| `IsTrigger` | Boolean | Read | Hit body is trigger-only |

---

## Viper.Graphics3D.PhysicsHitList3D

List of `PhysicsHit3D` results returned by `RaycastAll`, `OverlapSphere`, and `OverlapAABB`.

**Type:** Instance (obj)

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Count` | Integer | Read | Number of hits in the list |
| `TotalCount` | Integer | Read | Total matching hits before the bounded result prefix was applied |
| `Truncated` | Boolean | Read | True when more matches existed than are stored in the list |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Get(i)` | `Object(Integer)` | Return hit `i` as a `PhysicsHit3D` |

---

## Viper.Graphics3D.CollisionEvent3D

Structured per-pair contact snapshot from the most recent world step.

**Type:** Instance (obj)

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `BodyA` | Object | Read | First body in the pair |
| `BodyB` | Object | Read | Second body in the pair |
| `ColliderA` | Object | Read | Leaf collider for body A |
| `ColliderB` | Object | Read | Leaf collider for body B |
| `IsTrigger` | Boolean | Read | Pair includes a trigger body |
| `ContactCount` | Integer | Read | Manifold point count; AABB pairs can expose up to four points, other pairs currently expose one representative point |
| `RelativeSpeed` | Double | Read | Relative speed along the contact normal before resolution |
| `NormalImpulse` | Double | Read | Solver normal impulse (`0` for trigger pairs) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `GetContact(i)` | `Object(Integer)` | Return `ContactPoint3D` for manifold point `i` |
| `GetContactPoint(i)` | `Object(Integer)` | Return point `i` as a `Vec3` |
| `GetContactNormal(i)` | `Object(Integer)` | Return point normal `i` as a `Vec3` |
| `GetContactSeparation(i)` | `Double(Integer)` | Signed separation (`< 0` means penetration) |

---

## Viper.Graphics3D.ContactPoint3D

Contact manifold point returned by `CollisionEvent3D.GetContact`.

**Type:** Instance (obj)

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Point` | Object (`Vec3`) | Read | Contact position |
| `Normal` | Object (`Vec3`) | Read | Contact normal |
| `Separation` | Double | Read | Signed separation (`< 0` while penetrating) |

---

## Viper.Graphics3D.Collider3D

Reusable 3D collision shape object. This is the preferred authoring path for advanced physics
content; bodies now own a collider instead of baking all shape state directly into the body.

**Type:** Instance (obj)

### Constructors

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `NewBox(hx, hy, hz)` | `Collider3D(Double, Double, Double)` | Box collider with half-extents |
| `NewSphere(radius)` | `Collider3D(Double)` | Sphere collider |
| `NewCapsule(radius, height)` | `Collider3D(Double, Double)` | Upright capsule collider; `height` is total height including caps, and values below `2*radius` collapse to a sphere-like capsule |
| `NewConvexHull(mesh)` | `Collider3D(Object)` | Convex-hull collider sourced from a `Mesh3D` |
| `NewMesh(mesh)` | `Collider3D(Object)` | Static triangle-mesh collider |
| `NewHeightfield(heightmap, sx, sy, sz)` | `Collider3D(Object, Double, Double, Double)` | Static heightfield collider from `Pixels` |
| `NewCompound()` | `Collider3D()` | Empty compound collider |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Type` | Integer | Read | `0=box`, `1=sphere`, `2=capsule`, `3=convexHull`, `4=mesh`, `5=compound`, `6=heightfield` |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddChild(child, localTransform)` | `Void(Object, Object)` | Add a child collider to a compound collider |
| `GetLocalBoundsMin()` | `Object()` | Local-space minimum bounds as a `Vec3` |
| `GetLocalBoundsMax()` | `Object()` | Local-space maximum bounds as a `Vec3` |

### Notes

- `NewMesh()` and `NewHeightfield()` are static-only in v1 and must be attached to static bodies.
- Primitive collider constructors substitute a positive unit extent/radius for zero, negative-zero,
  or non-finite inputs; capsule height is still clamped to at least its diameter.
- `NewHeightfield()` requires a valid `Pixels` object, not just a matching class ID.
- `NewConvexHull()` treats the mesh vertex cloud as a convex support set.
  Hull-vs-hull and hull-vs-simple pairs use the GJK/EPA simplex narrow phase,
  including contained primitive contacts.
- Compound colliders are the preferred way to build richer dynamic bodies from simple children.
- Heightfield contacts account for signed and non-uniform Y scale when computing sphere penetration depth and normals.

---

## Viper.Graphics3D.Physics3DBody

3D rigid body with position, quaternion orientation, linear/angular velocity, collision filtering,
sleeping, and optional CCD.

**Type:** Instance (obj)

### Constructors

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(mass)` | `Physics3DBody(Double)` | Create an empty body and assign a collider later |
| `NewAABB(sx, sy, sz, mass)` | `Physics3DBody(Double, Double, Double, Double)` | Box body (`mass = 0` makes it static); name retained for compatibility |
| `NewSphere(radius, mass)` | `Physics3DBody(Double, Double)` | Sphere body |
| `NewCapsule(radius, height, mass)` | `Physics3DBody(Double, Double, Double)` | Capsule body; `height` is total height including caps |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Collider` | Object | Read/Write | Active `Collider3D` attached to the body |
| `Position` | Object (`Vec3`) | Read | World position |
| `Scale` | Object (`Vec3`) | Read | Collision scale applied to the attached collider |
| `Orientation` | Object (`Quat`) | Read | World orientation quaternion |
| `Velocity` | Object (`Vec3`) | Read | Linear velocity |
| `AngularVelocity` | Object (`Vec3`) | Read | Angular velocity in radians per second |
| `Restitution` | Double | Read/Write | Bounciness, clamped to `0.0` to `1.0` |
| `Friction` | Double | Read/Write | Surface friction, clamped to finite non-negative values |
| `LinearDamping` | Double | Read/Write | Per-step damping applied to linear motion |
| `AngularDamping` | Double | Read/Write | Per-step damping applied to spin |
| `CollisionLayer` | Integer | Read/Write | Layer bitmask for this body |
| `CollisionMask` | Integer | Read/Write | Which layers this body collides with |
| `Static` | Boolean | Read/Write | Treat body as static |
| `Kinematic` | Boolean | Read/Write | Treat body as kinematic |
| `Trigger` | Boolean | Read/Write | Trigger-only overlap body; no impulse response |
| `CanSleep` | Boolean | Read/Write | Allow the body to auto-sleep when idle |
| `Sleeping` | Boolean | Read | Body is currently asleep |
| `UseCCD` | Boolean | Read/Write | Enable substep-based CCD for fast motion |
| `Grounded` | Boolean | Read | Body touched a ground-like contact in the last step |
| `GroundNormal` | Object (`Vec3`) | Read | Normal of the last ground contact |
| `Mass` | Double | Read | Body mass |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetCollider(collider)` | `Void(Object)` | Attach or replace the active collider |
| `SetPosition(x, y, z)` | `Void(Double, Double, Double)` | Teleport body position |
| `SetScale(x, y, z)` | `Void(Double, Double, Double)` | Set per-body collider scale |
| `SetOrientation(quat)` | `Void(Object)` | Set body orientation from a `Viper.Math.Quat` |
| `SetVelocity(vx, vy, vz)` | `Void(Double, Double, Double)` | Set linear velocity |
| `SetAngularVelocity(wx, wy, wz)` | `Void(Double, Double, Double)` | Set angular velocity |
| `ApplyForce(fx, fy, fz)` | `Void(Double, Double, Double)` | Accumulate force for the next step |
| `ApplyImpulse(ix, iy, iz)` | `Void(Double, Double, Double)` | Apply an immediate linear impulse |
| `ApplyTorque(tx, ty, tz)` | `Void(Double, Double, Double)` | Accumulate torque for the next step |
| `ApplyAngularImpulse(ix, iy, iz)` | `Void(Double, Double, Double)` | Apply an immediate angular impulse |
| `Wake()` | `Void()` | Wake a sleeping dynamic body |
| `Sleep()` | `Void()` | Force a dynamic body into the sleeping state |

### Notes

- `NewAABB()`, `NewSphere()`, and `NewCapsule()` are now convenience factories that create a body,
  create the matching collider, and attach it internally.
- Use `New(mass)` plus `SetCollider()` when you want reusable, mesh, compound, or heightfield shapes.
- `Scale` is a physics scale, not a renderer transform; Game3D keeps it synchronized from `Entity3D` scale when a body is attached to an entity.
- `Orientation` uses the runtime quaternion type `Viper.Math.Quat`; `SetOrientation`
  traps when passed any other object type.
- `CollisionLayer` must be a positive bitmask. Use `CollisionMask = 0` when you
  need a body to collide with no layers.
- `Sleep()` and `Wake()` only affect dynamic bodies. Static and kinematic bodies do not enter
  the sleeping state.
- `Kinematic = true` makes the body move from explicit `Velocity` / `AngularVelocity` only.
- `UseCCD` uses additional substeps to reduce tunneling for fast-moving bodies.
- Position, velocity, angular velocity, force, torque, and integrated state are
  sanitized to finite values and saturated to the runtime state bounds, so
  extreme impulses or forces cannot create `NaN`/`Inf` body state. CCD keeps
  diagnostics for requested-vs-applied substeps when that demand is clamped.
- Body position and velocity integration use double precision. The regression
  lane covers a body at `1e9` world units retaining a `0.125` unit step delta.
- Rotational state is fully integrated for all body types. Non-trigger contacts apply impulses at
  the contact point, so off-center hits can generate angular velocity.
- Contacts are solved by a warm-started sequential-impulse solver: per-manifold-point
  normal and friction impulses are accumulated and carried across frames, so box stacks
  settle and rest stably rather than jittering or sinking. Settled bodies auto-sleep and
  freeze as an island; `SolverIterations` raises support quality for tall stacks. Resting
  contacts apply no restitution (only genuine impacts bounce).
- AABB primitives remain axis-aligned boxes. Capsule collision honors body orientation; use compound
  colliders or mesh/convex-hull colliders when you need rotated box geometry.

### Zia Example

```rust
module Physics3DBodyDemo;

bind Viper.Graphics3D;
bind Viper.Math.Quat as Quat;
bind Viper.Terminal;

func start() {
    var world = Physics3DWorld.New(0.0, 0.0, 0.0);
    var body = Physics3DBody.NewSphere(0.5, 1.0);
    world.Add(body);

    body.SetOrientation(Quat.Identity());
    body.ApplyTorque(0.0, 6.0, 0.0);
    world.Step(0.5);

    Say("AngularVelocity = " + toString(body.get_AngularVelocity()));
    Say("Orientation = " + toString(body.get_Orientation()));

    body.Sleep();
    Say("Sleeping = " + toString(body.get_Sleeping()));
    body.Wake();

    body.set_Kinematic(true);
    body.SetVelocity(1.0, 0.0, 0.0);
    body.SetAngularVelocity(0.0, 1.0, 0.0);
    world.Step(1.0);
}
```

### BASIC Example

```basic
DIM world AS OBJECT
DIM body AS OBJECT
DIM q AS OBJECT

world = Viper.Graphics3D.Physics3DWorld.New(0.0, 0.0, 0.0)
body = Viper.Graphics3D.Physics3DBody.NewSphere(0.5, 1.0)
world.Add(body)

q = Viper.Math.Quat.Identity()
body.SetOrientation(q)
body.ApplyTorque(0.0, 6.0, 0.0)
world.Step(0.5)

PRINT "Sleeping before: "; body.Sleeping
body.Sleep()
PRINT "Sleeping after: "; body.Sleeping
body.Wake()

body.Kinematic = 1
body.SetVelocity(1.0, 0.0, 0.0)
body.SetAngularVelocity(0.0, 1.0, 0.0)
world.Step(1.0)
```

For a runnable headless example, see
[`examples/apiaudit/graphics3d/physics3d_rotation_demo.zia`](/Users/stephen/git/viper/examples/apiaudit/graphics3d/physics3d_rotation_demo.zia).
For the advanced collider surface, see
[`examples/apiaudit/graphics3d/collider3d_advanced_demo.zia`](/Users/stephen/git/viper/examples/apiaudit/graphics3d/collider3d_advanced_demo.zia).
For world-space query coverage, see
[`examples/apiaudit/graphics3d/physics3d_queries_demo.zia`](/Users/stephen/git/viper/examples/apiaudit/graphics3d/physics3d_queries_demo.zia).
For structured collision events, see
[`examples/apiaudit/graphics3d/collisionevent3d_demo.zia`](/Users/stephen/git/viper/examples/apiaudit/graphics3d/collisionevent3d_demo.zia).

---

## Viper.Graphics3D.Character3D

Controller-based character movement with slide-and-step collision against a `Physics3DWorld`.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics3D.Character3D(radius, height, mass)`

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `StepHeight` | Double | Read/Write | Maximum step-up height |
| `World` | Object | Read/Write | Attached `Physics3DWorld` |
| `Grounded` | Boolean | Read | Character is grounded |
| `JustLanded` | Boolean | Read | Character landed during the latest move |
| `Position` | Object (`Vec3`) | Read | Current world position |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Move(direction, dt)` | `Void(Object, Double)` | Move with collision response using a `Vec3` direction |
| `SetSlopeLimit(degrees)` | `Void(Double)` | Set the maximum climbable slope angle |
| `SetPosition(x, y, z)` | `Void(Double, Double, Double)` | Teleport the character |

---

## Viper.Graphics3D.DistanceJoint3D

Maintains a fixed distance between two `Physics3DBody` instances.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics3D.DistanceJoint3D(bodyA, bodyB, distance)`

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Distance` | Double | Read/Write | Target separation distance |

---

## Viper.Graphics3D.SpringJoint3D

Hooke's-law spring constraint between two `Physics3DBody` instances.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics3D.SpringJoint3D(bodyA, bodyB, restLength, stiffness, damping)`

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Stiffness` | Double | Read/Write | Spring constant |
| `Damping` | Double | Read/Write | Velocity damping factor |
| `RestLength` | Double | Read | Natural spring length |

---

## Viper.Graphics3D.HingeJoint3D

Anchor constraint between two `Physics3DBody` instances that keeps authored
anchor points together while allowing relative angular velocity around the
given hinge axis.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics3D.HingeJoint3D(bodyA, bodyB, anchor, axis)`

### Notes

- `anchor` and `axis` are `Vec3` values. The axis must be finite and non-zero.
- Register with `Physics3DWorld.AddJoint(joint, 2)`.

---

## Viper.Graphics3D.RopeJoint3D

Maximum-distance constraint between two `Physics3DBody` instances. Bodies can
move closer than `MaxLength`; the solver only corrects separation beyond it.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics3D.RopeJoint3D(bodyA, bodyB, maxLength)`

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `MaxLength` | Double | Read/Write | Maximum allowed separation |

### Notes

- Register with `Physics3DWorld.AddJoint(joint, 3)`.

---

## Viper.Graphics3D.SixDofJoint3D

Configurable frame-anchor constraint between two `Physics3DBody` instances. By
default it locks the two frame anchor translations together. `SetLinearLimits`
allows bounded frame-anchor separation, and `SetAngularLimits` clamps relative
angular velocity along each world axis.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics3D.SixDofJoint3D(bodyA, bodyB, frameA, frameB)`

### Notes

- `frameA` and `frameB` are `Mat4` values. Their translation components define
  the local anchor points.
- Register with `Physics3DWorld.AddJoint(joint, 4)`.

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetLinearLimits(min, max)` | `Void(Object, Object)` | Set per-axis minimum and maximum anchor separation as `Vec3` values |
| `SetAngularLimits(min, max)` | `Void(Object, Object)` | Set per-axis relative angular-velocity limits as `Vec3` values |

---

## See Also

- [Graphics](README.md) - 2D graphics, scene graph, fonts, and sprites
- [Mathematics](../math.md) - `Vec3`, `Mat4`, and `Quaternion` / `Quat`
- [Graphics 3D Guide](../../graphics3d-guide.md) - broader 3D rendering, materials, scenes, and asset loading
