---
status: active
audience: public
last-verified: 2026-04-09
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

### Methods

| Method                    | Signature             | Description |
|---------------------------|-----------------------|-------------|
| `Step(dt)`                | `Void(Double)`        | Advance simulation by `dt` seconds |
| `Add(body)`               | `Void(Object)`        | Add a `Physics3DBody` to the world |
| `Remove(body)`            | `Void(Object)`        | Remove a body from the world |
| `SetGravity(x, y, z)`     | `Void(Double, Double, Double)` | Change the gravity vector |
| `AddJoint(joint, type)`   | `Void(Object, Integer)` | Add a joint (`0 = DistanceJoint3D`, `1 = SpringJoint3D`) |
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
- Query `mask` uses the same layer bits as `Physics3DBody.CollisionLayer`. A mask of `0` matches any layer.
- Static bodies are immovable. Kinematic bodies move from explicit velocity but do not
  receive gravity or force integration.
- The runtime currently supports up to 256 bodies per world.

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
| `ContactCount` | Integer | Read | Manifold point count (`1` in the current backend) |
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
| `NewCapsule(radius, height)` | `Collider3D(Double, Double)` | Upright capsule collider |
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
- `NewConvexHull()` expects convex source geometry and uses the mesh surface as the hull shape.
- Compound colliders are the preferred way to build richer dynamic bodies from simple children.

---

## Viper.Graphics3D.Physics3DBody

3D rigid body with position, quaternion orientation, linear/angular velocity, collision filtering,
sleeping, and optional CCD.

**Type:** Instance (obj)

### Constructors

| Constructor | Signature | Description |
|-------------|-----------|-------------|
| `New(mass)` | `Physics3DBody(Double)` | Create an empty body and assign a collider later |
| `NewAABB(sx, sy, sz, mass)` | `Physics3DBody(Double, Double, Double, Double)` | Axis-aligned box body (`mass = 0` makes it static) |
| `NewSphere(radius, mass)` | `Physics3DBody(Double, Double)` | Sphere body |
| `NewCapsule(radius, height, mass)` | `Physics3DBody(Double, Double, Double)` | Capsule body |

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Collider` | Object | Read/Write | Active `Collider3D` attached to the body |
| `Position` | Object (`Vec3`) | Read | World position |
| `Orientation` | Object (`Quat`) | Read | World orientation quaternion |
| `Velocity` | Object (`Vec3`) | Read | Linear velocity |
| `AngularVelocity` | Object (`Vec3`) | Read | Angular velocity in radians per second |
| `Restitution` | Double | Read/Write | Bounciness, typically `0.0` to `1.0` |
| `Friction` | Double | Read/Write | Surface friction |
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
- `Orientation` uses the runtime quaternion type `Viper.Math.Quat`.
- `Sleep()` and `Wake()` only affect dynamic bodies. Static and kinematic bodies do not enter
  the sleeping state.
- `Kinematic = true` makes the body move from explicit `Velocity` / `AngularVelocity` only.
- `UseCCD` uses additional substeps to reduce tunneling for fast-moving bodies.
- Rotational state is fully integrated for all body types, but AABB and capsule collision shapes
  still collide as axis-aligned / upright primitives. Sphere bodies currently provide the most
  accurate rotational behavior.

### Zia Example

```rust
module Physics3DBodyDemo;

bind Viper.Graphics3D;
bind Viper.Math.Quat;
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

## See Also

- [Graphics](README.md) - 2D graphics, scene graph, fonts, and sprites
- [Mathematics](../math.md) - `Vec3`, `Mat4`, and `Quaternion` / `Quat`
- [Graphics 3D Guide](../../graphics3d-guide.md) - broader 3D rendering, materials, scenes, and asset loading
