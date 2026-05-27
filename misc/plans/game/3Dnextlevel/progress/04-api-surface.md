# API surface tracker

This file tracks the public runtime-backed `Viper.Game3D` surface described in
`api-spec.md`. Each row should end with implementation, tests, docs, and proof.

## Namespace, enums, and masks

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| API-NS-001 | Confirm and expose `Viper.Game3D.*` namespace | `api-spec.md` Conventions | done | `runtime.def`, runtime signatures/classes | `test_rt_game3d`; `g3d_test_game3d_world_probe` | Game3D docs, `zia-feasibility.md`, `02-decisions.md` | focused ctests passed | D-001 closed |
| API-ENUM-001 | `Layers` values: World, Dynamic, Player, Trigger, Debris | `api-spec.md` Enums | done | runtime constants | `test_rt_game3d` | Game3D docs | focused ctest passed | Single-bit values |
| API-ENUM-002 | `BodyShape` values: Box, Sphere, Capsule | `api-spec.md` Enums | done | runtime constants and BodyDef creation paths | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENUM-003 | `SyncMode` values | `api-spec.md` Enums | done | runtime constants | `test_rt_game3d` | Game3D docs | focused ctest passed | Node/body/root-motion modes |
| API-ENUM-004 | `AlphaMode` values | `api-spec.md` Enums | done | runtime constants | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENUM-005 | `ShadingModel` values | `api-spec.md` Enums | done | runtime constants | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENUM-006 | `QualityLevel` values | `api-spec.md` Enums | done | runtime constants | `test_rt_game3d`; Game3D world probe uses Balanced | Game3D docs | focused ctests passed |  |
| API-ENUM-007 | `CollisionPhase` values | `api-spec.md` Enums | done | runtime constants | `test_rt_game3d` | Game3D docs | focused ctest passed | Enter/Stay/Exit/Any |
| API-MASK-001 | `LayerMask.bits` | `api-spec.md` LayerMask | done | runtime property | `test_rt_game3d` | Game3D docs | focused ctest passed | Negative bits clamp to all safe bits |
| API-MASK-002 | `LayerMask.None()` | `api-spec.md` LayerMask | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-MASK-003 | `LayerMask.All()` | `api-spec.md` LayerMask | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-MASK-004 | `LayerMask.Of(layer)` | `api-spec.md` LayerMask | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed | Validates single-bit layer |
| API-MASK-005 | `LayerMask.include(layer)` | `api-spec.md` LayerMask | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed | Fluent |
| API-MASK-006 | `LayerMask.includes(layer)` | `api-spec.md` LayerMask | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |

## World3D

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| API-WORLD-001 | Fields: `canvas`, `camera`, `scene`, `physics`, `input`, `audio`, `effects`, `dt`, `elapsed`, `frame` | `api-spec.md` World3D | done | runtime properties | `test_rt_game3d`; Zia probes | Game3D docs | focused ctests passed | Escape hatches documented |
| API-WORLD-002 | `World3D.New(title,width,height)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| API-WORLD-003 | `World3D.NewWithCamera(...)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-WORLD-004 | `world.destroy()` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed | Lifetime contract |
| API-WORLD-005 | `world.isDestroyed()` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| API-WORLD-006 | `world.spawn(e)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| API-WORLD-007 | `world.despawn(e)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| API-WORLD-008 | `world.findNode(name)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed | Raw node |
| API-WORLD-009 | `world.findEntity(name)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed | Registry only |
| API-WORLD-010 | `world.setCameraController(c)` | `api-spec.md` World3D | done | stores retained built-in controller and dispatches update/lateUpdate from `stepSimulation` | `test_rt_game3d`; controller Zia probes | Game3D docs | focused tests passed | Interpreted Zia controller objects remain out of scope until callback trampoline support exists |
| API-WORLD-011 | `world.lookAt(target)` | `api-spec.md` World3D | done | runtime method | `g3d_test_game3d_world_probe` | Game3D docs | focused ctest passed |  |
| API-WORLD-012 | `world.onResize(width,height)` | `api-spec.md` World3D | done | runtime method | `g3d_test_game3d_world_probe` | Game3D docs | focused ctest passed | Tick also syncs aspect |
| API-WORLD-013 | `world.setAmbient(r,g,b)` | `api-spec.md` World3D | done | runtime method | `g3d_test_game3d_world_probe` | Game3D docs | focused ctest passed |  |
| API-WORLD-014 | `world.addLight(slot, light)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-WORLD-015 | `world.clearLights()` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-WORLD-016 | `world.setSkybox(cube)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-WORLD-017 | `world.setFog(r,g,b,near,far)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-WORLD-018 | `world.setQuality(q)` | `api-spec.md` World3D | done | runtime method | `g3d_test_game3d_world_probe` | Game3D docs | focused ctest passed | Updates canvas quality and world post-FX |
| API-WORLD-019 | `world.collisionEventCount(phase)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed | Required event API |
| API-WORLD-020 | `world.collisionEvent(phase,index)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed | Required event API |
| API-WORLD-021 | `world.clearCollisionEvents()` | `api-spec.md` World3D | done | runtime method + physics clear helper | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-WORLD-022 | Optional `world.onCollision(...)` | `api-spec.md` World3D | todo |  |  |  |  | Requires callback approval |
| API-WORLD-023 | Optional `world.onCollisionSimple(...)` | `api-spec.md` World3D | todo |  |  |  |  | Requires callback approval |
| API-WORLD-024 | `world.setGravity(x,y,z)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-WORLD-025 | Optional `world.run(update)` | `api-spec.md` World3D | partial | native callback-loop runtime method | `test_rt_game3d` native callback coverage | Game3D callback-boundary docs | focused ctest passed | Native function pointers only; interpreted Zia uses manual APIs |
| API-WORLD-026 | Optional `world.runWithOverlay(update, overlay)` | `api-spec.md` World3D | partial | native callback-loop runtime method | `test_rt_game3d` native overlay coverage | Game3D callback-boundary docs | focused ctest passed | Native function pointers only |
| API-WORLD-027 | Optional `world.runFixed(step, update)` | `api-spec.md` World3D | partial | native callback-loop runtime method | build coverage; explicit fixed-loop ctest pending | Game3D callback-boundary docs | local build | Native function pointers only |
| API-WORLD-028 | Optional `world.runFixedWithOverlay(step, update, overlay)` | `api-spec.md` World3D | partial | native callback-loop runtime method | build coverage; explicit fixed-loop ctest pending | Game3D callback-boundary docs | local build | Native function pointers only |
| API-WORLD-029 | Optional `world.runFrames(frameCount, step, update)` callback overload | `api-spec.md` World3D | partial | native callback loop plus callback guard | `test_rt_game3d`; `g3d_test_game3d_runframes_callback_reject` | Game3D callback-boundary docs | focused ctests passed | Interpreted Zia uses `runFramesOnly` |
| API-WORLD-029A | `world.runFramesOnly(frameCount, step)` deterministic no-callback helper | implemented extension | done | runtime method | `g3d_test_game3d_runframes_probe` | Game3D docs | focused ctest passed | Added to keep interpreted Zia deterministic tests safe |
| API-WORLD-030 | `world.tick()` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed | Mandatory manual loop |
| API-WORLD-031 | `world.stepSimulation(step)` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed | Mandatory manual loop |
| API-WORLD-032 | `world.beginFrame()` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| API-WORLD-033 | `world.drawScene()` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| API-WORLD-034 | `world.drawEffects()` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed | Current Phase 1 registry is post-FX only |
| API-WORLD-035 | `world.endScene()` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| API-WORLD-036 | `world.drawOverlay(overlay)` | `api-spec.md` World3D | partial | native overlay callback runtime method | `test_rt_game3d` | Game3D callback-boundary docs | focused ctest passed | Native function pointers only |
| API-WORLD-037 | `world.captureFinalFrame() -> Pixels` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe`, `g3d_test_game3d_runframes_probe` | Game3D docs | focused ctests passed |  |
| API-WORLD-038 | `world.present()` | `api-spec.md` World3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| API-WORLD-039 | `World3D.New` creates all default owned subsystems | `api-spec.md` New defaults | done | runtime constructor | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed | Canvas/camera/scene/physics/input/audio/effects |
| API-WORLD-040 | `World3D.New` installs explicit default lighting | `api-spec.md` New defaults | done | `rt_canvas3d_set_default_lighting` | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-WORLD-041 | `World3D.New` applies backend-safe `Quality.Balanced` | `api-spec.md` New defaults | done | canvas quality + `PostFX3D.NewQuality` | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| API-WORLD-042 | `World3D.New` enables frustum culling | `api-spec.md` New defaults | done | runtime constructor | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-WORLD-043 | `World3D.New` sets neutral clear/sky | `api-spec.md` New defaults | done | neutral ambient and clear color path | `g3d_test_game3d_world_probe` | Game3D docs | focused ctest passed | Sky presets remain later |
| API-WORLD-044 | `World3D.New` binds active audio listener to camera | `api-spec.md` New defaults | done | `Audio3D` listener created from camera | `test_rt_game3d` | Game3D docs | focused ctest passed | Follow update helpers remain Phase 6 |
| API-WORLD-045 | Managed frame sequence matches documented order | `api-spec.md` Managed frame sequence | done | shared frame helpers and manual stages | `test_rt_game3d`, Zia probes | Game3D docs | focused ctests passed |  |
| API-WORLD-046 | Fixed timestep accumulator behavior matches spec | `api-spec.md` Fixed-timestep loop | partial | `runFixedWithOverlay` accumulator implemented | explicit fixed-loop ctest pending | Game3D docs | local build |  |

## Entity3D

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| API-ENT-001 | Fields: id/node/mesh/material/body/anim/layer/collisionMask/name | `api-spec.md` Entity3D | done | runtime properties | `test_rt_game3d`; Zia world probe | Game3D docs | focused ctests passed |  |
| API-ENT-002 | `Entity3D.New()` | `api-spec.md` Entity3D | done | runtime constructor | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-003 | `Entity3D.Of(mesh, material)` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| API-ENT-004 | `Entity3D.FromNode(root)` | `api-spec.md` Entity3D | partial | runtime method; Assets3D uses imported root groups | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed | Direct public `FromNode` Zia probe remains nice-to-have |
| API-ENT-005 | Fluent `setPosition(x,y,z)` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d`, Zia world probe | Game3D docs | focused ctests passed |  |
| API-ENT-006 | Fluent `setPositionV(p)` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-007 | Fluent `setScale(s)` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-008 | Fluent `setScaleXYZ(x,y,z)` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-009 | Fluent `setRotationEuler(xDeg,yDeg,zDeg)` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-010 | `setMesh(m)` | `api-spec.md` Entity3D | done | runtime method/property | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-011 | `setMaterial(m)` | `api-spec.md` Entity3D | done | runtime method/property | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-012 | `addChild(child)` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-013 | `isGroup()` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-014 | `setName(n)` | `api-spec.md` Entity3D | done | runtime method/property | `test_rt_game3d`, Zia world probe | Game3D docs | focused ctests passed |  |
| API-ENT-015 | `setLayer(layer)` | `api-spec.md` Entity3D | done | runtime method/property | `test_rt_game3d`, Zia world probe | Game3D docs | focused ctests passed |  |
| API-ENT-016 | `setCollisionMask(mask)` | `api-spec.md` Entity3D | done | runtime method/property | `test_rt_game3d`, Zia world probe | Game3D docs | focused ctests passed |  |
| API-ENT-017 | `attachBody(def)` | `api-spec.md` Entity3D | done | `attachBody` accepts `BodyDef` and raw `Physics3DBody`, applies filters/flags, binds node, and registers spawned bodies | `test_rt_game3d`, `g3d_test_game3d_physics_probe` | Game3D docs | focused ctests passed | Raw body remains escape hatch |
| API-ENT-018 | `applyImpulse(x,y,z)` | `api-spec.md` Entity3D | done | runtime method over attached body | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-019 | `setVelocity(x,y,z)` | `api-spec.md` Entity3D | done | runtime method over attached body | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-020 | Optional `onUpdate(fn)` | `api-spec.md` Entity3D | todo |  |  |  |  | Requires callback approval |
| API-ENT-021 | Optional `onCollision(fn)` | `api-spec.md` Entity3D | todo |  |  |  |  | Requires callback approval |
| API-ENT-022 | `position() -> Vec3` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-023 | `worldPosition() -> Vec3` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-024 | `isSpawned()` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d`, Zia world probe | Game3D docs | focused ctests passed |  |
| API-ENT-025 | `isDestroyed()` | `api-spec.md` Entity3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-026 | `spawn` attaches node to world scene and registers entity | `api-spec.md` Entity3D Notes | done | runtime world registry/scene attach | `test_rt_game3d`, Zia world probe | Game3D docs | focused ctests passed |  |
| API-ENT-027 | `despawn` removes registry, scene, physics, effects, child ownership | `api-spec.md` Entity3D Notes | partial | removes registry, scene, physics, children | `test_rt_game3d`, Zia world probe | Game3D docs | focused ctests passed | Per-entity effects remain later |
| API-ENT-028 | Group despawn recurses over Game3D child entities | `api-spec.md` Entity3D Notes | done | recursive child despawn | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-ENT-029 | Raw imported child nodes remain part of root node subtree | `api-spec.md` Entity3D Notes | done | `Assets3D` returns group entities with imported root node subtree intact | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed |  |
| API-ENT-030 | Destroyed world/entity diagnostics use `Game3D.<Type>.<method>` form | `api-spec.md` Entity3D Notes | partial | trap guards use Game3D prefixes | callback rejection negative probe | Game3D docs | focused ctest passed | Destroyed-handle negative probe pending |

## Physics and collisions

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| API-PHY-001 | `BodyDef` fields | `api-spec.md` BodyDef | done | runtime class stores shape/mass/friction/restitution/static/kinematic/trigger/CCD/layer/mask/sync | `test_rt_game3d`, `g3d_test_game3d_physics_probe` | Game3D docs | focused ctests passed |  |
| API-PHY-002 | `BodyDef.Box(...)` | `api-spec.md` BodyDef | done | runtime helper creates dynamic AABB BodyDef | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| API-PHY-003 | `BodyDef.Sphere(...)` | `api-spec.md` BodyDef | done | runtime helper creates dynamic sphere BodyDef | `test_rt_game3d`, `g3d_test_game3d_physics_probe` | Game3D docs | focused ctests passed |  |
| API-PHY-004 | `BodyDef.Capsule(...)` | `api-spec.md` BodyDef | done | runtime helper creates dynamic capsule BodyDef | build + runtime registration | Game3D docs | build passed | Attach path covered by shared BodyDef creation code; direct capsule Zia probe remains nice-to-have |
| API-PHY-005 | `BodyDef.StaticBox(...)` | `api-spec.md` BodyDef | done | runtime helper creates static world-layer AABB BodyDef | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| API-PHY-006 | `BodyDef.StaticPlane(size)` | `api-spec.md` BodyDef | done | runtime helper creates shallow static world-layer floor box | `test_rt_game3d`, `g3d_test_game3d_physics_probe` | Game3D docs | focused ctests passed | Docs state 0.1 total thickness and centered origin |
| API-PHY-007 | `BodyDef.withLayer(layer)` | `api-spec.md` BodyDef | done | fluent runtime method marks explicit layer override | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| API-PHY-008 | `BodyDef.withMask(mask)` | `api-spec.md` BodyDef | done | fluent runtime method copies mask bits | `test_rt_game3d`, `g3d_test_game3d_physics_probe` | Game3D docs | focused ctests passed |  |
| API-PHY-009 | `BodyDef.asTrigger()` | `api-spec.md` BodyDef | done | fluent runtime method marks attached body trigger-only | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| API-PHY-010 | `BodyDef.withSync(mode)` | `api-spec.md` BodyDef | done | fluent runtime method applies node/body sync mode on attachment | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-PHY-011 | `attachBody` creates body, applies filters/flags, binds node, registers ownership | `api-spec.md` BodyDef | done | BodyDef attach path creates Physics3DBody, applies material/CCD/static/kinematic/trigger/filter flags, binds to node, and registers with spawned world | `test_rt_game3d`, `g3d_test_game3d_physics_probe` | Game3D docs | focused ctests passed |  |
| API-COL-001 | `Collision3DEvent.phase` | `api-spec.md` Collision3DEvent | done | runtime wrapper property | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| API-COL-002 | `Collision3DEvent.a` / `b` | `api-spec.md` Collision3DEvent | done | body-owner registry resolves raw bodies to Game3D entities | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| API-COL-003 | `Collision3DEvent.raw` | `api-spec.md` Collision3DEvent | done | raw `Graphics3D.CollisionEvent3D` retained as escape hatch | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed | Escape hatch |
| API-COL-004 | `Collision3DEvent.isTrigger` | `api-spec.md` Collision3DEvent | done | forwarded from raw collision event | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| API-COL-005 | `Collision3DEvent.relativeSpeed` | `api-spec.md` Collision3DEvent | done | forwarded from raw collision event | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-COL-006 | `Collision3DEvent.normalImpulse` | `api-spec.md` Collision3DEvent | done | forwarded from raw collision event | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-COL-007 | `Collision3DEvent.point()` | `api-spec.md` Collision3DEvent | done | returns first raw contact point as `Vec3` | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| API-COL-008 | `Collision3DEvent.normal()` | `api-spec.md` Collision3DEvent | done | returns first raw contact normal as `Vec3` | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| API-COL-009 | `Collision3DEvent.other(e)` | `api-spec.md` Collision3DEvent | done | resolves opposite Game3D entity for either event endpoint | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| API-COL-010 | Runtime enter/stay/exit buffers are source of truth | `api-spec.md` Dispatch rules | done | World3D wrappers read physics world's enter/stay/exit buffers | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| API-COL-011 | Layer pair keys canonicalized for unordered handlers | `api-spec.md` Dispatch rules | deferred | collision callback handler sugar not shipped under current callback policy | callback rejection probe | Game3D docs | explicit waiver | Event buffers do not need layer-pair handler keys |
| API-COL-012 | `CollisionPhase.Any` receives enter, stay, and exit | `api-spec.md` Dispatch rules | done | Any count/index walks enter, stay, then exit buffers | `test_rt_game3d` | Game3D docs | focused ctest passed |  |

## Input, cameras, and character controller

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| API-IN-001 | `Input3D.lookSensitivity` | `api-spec.md` Input3D | done | runtime property | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-IN-002 | `Input3D.update()` | `api-spec.md` Input3D | done | runtime method | `test_rt_game3d` | Game3D docs | focused ctest passed | After `Canvas3D.Poll()` |
| API-IN-003 | `isDown` / `pressed` / `released` | `api-spec.md` Input3D | done | runtime methods | `test_rt_game3d` | Game3D docs | focused ctest passed | Keyboard |
| API-IN-004 | `mouseDelta` / `mouseButton` / `mousePressed` / `wheelY` | `api-spec.md` Input3D | done | runtime methods | `test_rt_game3d` | Game3D docs | focused ctest passed | Mouse |
| API-IN-005 | `moveAxis()` / `lookAxis()` | `api-spec.md` Input3D | done | runtime methods | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-IN-006 | `captureMouse()` / `releaseMouse()` | `api-spec.md` Input3D | done | runtime methods | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-IN-007 | `Keys` and `MouseButtons` re-export stable runtime constants | `api-spec.md` Input3D | done | runtime constants | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| API-CAM-001 | `CameraController.update(world,dt)` | `api-spec.md` Cameras | done | Built-in controller update methods in C runtime | `test_rt_game3d`; controller Zia probes | Game3D docs | focused tests passed | Public methods exist; `World3D` dispatches only built-in Game3D controller objects |
| API-CAM-002 | `CameraController.lateUpdate(world,dt)` | `api-spec.md` Cameras | done | Built-in controller lateUpdate methods in C runtime | `test_rt_game3d`; controller Zia probes | Game3D docs | focused tests passed | Runs after physics and sync |
| API-CAM-003 | `BaseCameraController` fallback if default methods unsuitable | `api-spec.md` Cameras | deferred | Not shipped; built-in C controller classes are the supported runtime surface |  | Game3D docs | D-003/D-008 resolved by runtime-first approach | Add only if VM callback trampoline/custom controller support becomes real |
| API-CAM-004 | `FirstPersonController.New(world)` | `api-spec.md` Cameras | done | `rt_game3d_first_person_controller_new` | `test_rt_game3d`; `g3d_test_game3d_character_controller_probe` | Game3D docs | focused tests passed | WASD + mouse, optional grounding |
| API-CAM-005 | `FreeFlyController.New(world)` | `api-spec.md` Cameras | done | `rt_game3d_free_fly_controller_new` | `test_rt_game3d`; `g3d_test_game3d_camera_controllers_probe` | Game3D docs | focused tests passed | Free-fly spectator movement |
| API-CAM-006 | `OrbitController.New(world,target)` | `api-spec.md` Cameras | done | `rt_game3d_orbit_controller_new` | `test_rt_game3d`; `g3d_test_game3d_camera_controllers_probe` | Game3D docs | focused tests passed | Drag/wheel/pitch clamp |
| API-CAM-007 | `FollowController.New(world,target,offset)` | `api-spec.md` Cameras | done | `rt_game3d_follow_controller_new` | `test_rt_game3d`; `g3d_test_game3d_camera_controllers_probe` | Game3D docs | focused tests passed | Late entity follow |
| API-CHAR-001 | `CharacterController3D` fields | `api-spec.md` CharacterController3D | done | `character`, `entity`, `speed`, `jumpSpeed`, `gravity` runtime properties | `test_rt_game3d`; `g3d_test_game3d_character_controller_probe` | Game3D docs | focused tests passed | Wraps raw `Character3D` |
| API-CHAR-002 | `CharacterController3D.New(...)` | `api-spec.md` CharacterController3D | done | `rt_game3d_character_controller_new` | `test_rt_game3d`; `g3d_test_game3d_character_controller_probe` | Game3D docs | focused tests passed | Binds raw controller to world physics |
| API-CHAR-003 | `CharacterController3D.update(input,camera,dt)` | `api-spec.md` CharacterController3D | done | camera-relative movement, gravity, jump, entity sync | `test_rt_game3d`; `g3d_test_game3d_character_controller_probe` | Game3D docs | focused tests passed | Called by first-person controller and available directly |
| API-CHAR-004 | `CharacterController3D.teleport(x,y,z)` | `api-spec.md` CharacterController3D | done | runtime method | build + C coverage | Game3D docs | focused build passed | Direct Zia teleport probe still nice-to-have |
| API-CHAR-005 | `CharacterController3D.grounded()` | `api-spec.md` CharacterController3D | done | runtime method over raw `Character3D.Grounded` | C/Zia movement probes | Game3D docs | focused tests passed |  |

## Presets, environment, prefabs, and assets

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| API-LIGHT-001 | `Lighting.Studio/Outdoor/Night/Interior/Clear` | `api-spec.md` Presets | done | Runtime C methods clear/install light slots and tune ambient/clear color | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Clear managed slots and install readable rigs |
| API-MAT-001 | Material presets: Plastic/Metal/Rubber/Glass/Emissive/Unlit/FromAlbedoMap | `api-spec.md` Presets | done | Runtime C material factories set PBR/shading/alpha/emissive/unlit state | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Set shading/alpha modes |
| API-FX-001 | `PostFX.Cinematic/Crisp/None` | `api-spec.md` Presets | done | Runtime C wrappers update world effect registry and canvas post-FX binding | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Built on lower-level `PostFX3D` profile primitives |
| API-QUAL-001 | `Quality.Apply(world, Performance/Balanced/Cinematic)` | `api-spec.md` Presets | done | Runtime C wrapper applies quality, frustum culling, and shadow capability policy | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Canvas fallback diagnostics remain source of truth |
| API-ENV-001 | `Environment.Outdoor/Sunset/Overcast/Night` | `api-spec.md` Environment | done | Runtime C constructors apply lighting, clear color, fog, terrain | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Good default scene |
| API-ENV-002 | `EnvHandle.withTerrain(size,height)` | `api-spec.md` Environment | done | Spawns/replaces ground entity with static body | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Height controls entity Y |
| API-ENV-003 | `EnvHandle.withWater(level)` | `api-spec.md` Environment | done | Spawns/replaces transparent water plane prefab | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed |  |
| API-ENV-004 | `EnvHandle.withFog(near,far)` | `api-spec.md` Environment | done | Applies fog through Canvas3D with environment clear color | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Also fixed `World3D.setFog` argument forwarding |
| API-PREF-001 | Prefabs: Box, BoxXYZ, Sphere, Cylinder, Plane, Ground | `api-spec.md` Prefabs | done | Runtime C prefab factories return `Entity3D` with mesh/material | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Spawnable entities |
| API-ASSETS-001 | `Assets3D.LoadModel(path)` | `api-spec.md` Assets | done | runtime C wrapper loads filesystem model and returns group Entity3D | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed | Filesystem/dev |
| API-ASSETS-002 | `Assets3D.LoadModelAsset(assetPath)` | `api-spec.md` Assets | done | runtime wrapper over `Model3D.LoadAsset` package-aware paths | `test_rt_gltf`, `test_rt_model3d`, `g3d_test_model3d_load_asset`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed | Mounted package proof at lower layer; Game3D wrapper proof uses asset API |
| API-ASSETS-003 | `Assets3D.LoadModelTemplate(path)` | `api-spec.md` Assets | done | filesystem template cache | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed | C test verifies cache identity |
| API-ASSETS-004 | `Assets3D.LoadModelTemplateAsset(assetPath)` | `api-spec.md` Assets | done | package-aware template cache | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed | Records `isAsset` |
| API-ASSETS-005 | `Assets3D.Preload(path)` | `api-spec.md` Assets | done | warms filesystem template cache | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed |  |
| API-ASSETS-006 | `Assets3D.ClearCache()` | `api-spec.md` Assets | done | releases cached template entries | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed |  |
| API-ASSETS-007 | `ModelTemplate` caches loaded models and instantiates group entities cheaply | `api-spec.md` Assets | done | runtime ModelTemplate exposes model/path/isAsset and clones root subtree on instantiate | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed | `entity.anim` escape hatch set when root animator exists |

## Animation, audio, VFX, and debug

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| API-ANIM-001 | `Animator3D.controller` | `api-spec.md` Animation | todo |  |  |  |  | Escape hatch |
| API-ANIM-002 | `play` / `crossfade` / `setSpeed` / `isPlaying` / `stateTime` | `api-spec.md` Animation | todo |  |  |  |  |  |
| API-ANIM-003 | `eventCount()` / `eventName(index)` | `api-spec.md` Animation | todo |  |  |  |  | Required event API |
| API-ANIM-004 | Optional `onAnimEvent(fn)` | `api-spec.md` Animation | todo |  |  |  |  | Requires callback approval |
| API-ANIM-005 | `Animator3D.update(dt)` | `api-spec.md` Animation | todo |  |  |  |  | Pre-physics |
| API-AUDIO-001 | `Audio3D.listener` | `api-spec.md` 3D audio | done | world-owned listener property | `test_rt_game3d` | Game3D docs | focused ctest passed | Playback helpers remain Phase 6 |
| API-AUDIO-002 | `listenerFollowCamera(enabled)` | `api-spec.md` 3D audio | todo |  |  |  |  |  |
| API-AUDIO-003 | `setListenerPose(pos,forward,up)` | `api-spec.md` 3D audio | todo |  |  |  |  |  |
| API-AUDIO-004 | `load(path)` / `loadAsset(assetPath)` | `api-spec.md` 3D audio | partial | Runtime foundation: `Sound.LoadAsset` loads mounted/embedded audio bytes | `test_rt_audio_integration`, `test_rt_audio_surface_link` | `docs/viperlib/audio.md` | focused ctests passed | Game3D `Audio3D.loadAsset` wrapper pending |
| API-AUDIO-005 | `playAt` / `playAttached` / `play2D` | `api-spec.md` 3D audio | todo |  |  |  |  |  |
| API-AUDIO-006 | `setAttenuation(refDist,maxDist)` | `api-spec.md` 3D audio | todo |  |  |  |  |  |
| API-AUDIO-007 | Audio binding sync after scene/body sync | `api-spec.md` 3D audio | todo |  |  |  |  |  |
| API-VFX-001 | `Effects3D.Explosion/Sparks/Dust/Smoke/ImpactDecal` | `api-spec.md` VFX | todo |  |  |  |  |  |
| API-VFX-002 | `EffectRegistry3D.addParticles(p,lifetime)` | `api-spec.md` VFX | todo |  |  |  |  |  |
| API-VFX-003 | `EffectRegistry3D.addDecal(d)` | `api-spec.md` VFX | todo |  |  |  |  |  |
| API-VFX-004 | `EffectRegistry3D.update(dt)` / `draw(canvas,camera)` / `clear()` | `api-spec.md` VFX | partial | Phase 1 exposes world `postfx` and `drawEffects()` placeholder | `test_rt_game3d`, Zia world probe | Game3D docs | focused ctests passed | Particle/decal registry remains Phase 6 |
| API-VFX-005 | Effects draw after scene and before final overlay | `api-spec.md` VFX | partial | `World3D` frame order reserves `drawEffects()` between scene and overlay | `test_rt_game3d` | Game3D docs | focused ctest passed | Rich VFX drawing remains Phase 6 |
| API-DBG-001 | `Debug3D.ShowOverlay` | `api-spec.md` Debug3D | done | Runtime C flag rendered from `World3D.endScene` | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed |  |
| API-DBG-002 | `Debug3D.DrawAxes` | `api-spec.md` Debug3D | done | Runtime C stores origin/size and draws in `drawEffects` | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed |  |
| API-DBG-003 | `Debug3D.DrawPhysics` | `api-spec.md` Debug3D | done | Runtime C draws body AABB wires and reports physics debug state | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed |  |
| API-DBG-004 | `Debug3D.DrawCameraInfo` | `api-spec.md` Debug3D | done | Runtime C overlay reports camera position when enabled | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed |  |
| API-DBG-005 | `Debug3D.DrawCapabilities` | `api-spec.md` Debug3D | done | Runtime C overlay reports backend capability bitmask when enabled | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed |  |
| API-DBG-006 | Debug text uses final overlay path | `api-spec.md` Debug3D | done | `World3D.endScene` records overlay with `Canvas3D.BeginOverlay/EndOverlay` | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Captured by `captureFinalFrame` |

## API examples

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| API-EX-001 | Hello world compiles | `api-spec.md` Hello world | partial | docs quick start mirrors world probe | `g3d_test_game3d_world_probe` | Game3D docs | focused ctest passed | Dedicated docs-snippet ctest pending |
| API-EX-002 | Hello world stays about 15 lines and no common-case `Mat4` | `api-spec.md` Hello world | partial | Game3D quick start uses entity transforms, no `Mat4` | `g3d_test_game3d_world_probe` | Game3D docs | focused ctest passed | Official starter still pending |
| API-EX-003 | Richer physics/collision/audio/VFX/follow-camera example compiles or is updated | `api-spec.md` Richer example | todo |  |  |  |  | May need event-buffer version if callbacks not approved |
