# Appendix — Proposed API Register

## Status and authority

This register keeps the individual plans from inventing conflicting public
surfaces. It is a design draft. Existing entries describe live APIs; proposed
entries require the ADR and review named by their owning plan. The ADR may
change names or signatures, but dependent plans and this register must then be
updated together.

Status labels:

- **Existing**: present in the reviewed 2026-07-15 source.
- **Extend**: add behavior or members to an existing class.
- **Proposed**: new public class or static surface.
- **Incubate**: example-library API; not runtime ABI in its first delivery.
- **Internal**: no public surface should be added.

## Naming and representation rules

- Low-level `Viper.Graphics3D` uses its existing PascalCase conventions.
- Stateful `Viper.Game3D` follows the actual class registry's member style;
  match the neighboring class rather than normalizing unrelated APIs.
- New public runtime objects end in `3D` unless they extend an established
  static helper name.
- Class IDs are appended permanently after checking the live ID registry.
- Pollable objects are preferred over callbacks. Public callbacks require VM
  signature validation, native parity, re-entry/trap recovery, and main-thread
  execution.
- Avoid generic `obj` returns when the registry can express a qualified type.
- Recoverable operations return Boolean, Option, Result, or a result object
  with a diagnostic; do not use null/trap ambiguity.
- Collection/view lifetime must be stated in API docs.

## Existing surfaces retained as foundations

| Surface | Decision |
|---|---|
| `World3D.Update/StepSimulation/BeginFrame/DrawScene/DrawEffects/EndScene/Present` | Retain as authoritative manual escape hatch |
| `World3D.Run*` and `RunFrames*` | Retain for simple games/tests; do not force advanced games through fixed render order |
| `CharacterController3D` and built-in camera controllers | Retain; compose rather than replace |
| `Environment3D` and `EnvHandle` | Retain presets; make their renderable ownership participate in the new environment registry |
| `EffectRegistry3D` and `Sound3D` | Extend with named bounded pools/cues; no parallel feedback engine |
| `Viper.Assets.Resolver` and `Viper.IO.Assets` | Extend/reuse for resolution; do not add a second filesystem policy |
| `Entity3D.SetPersistent` and `World3D.SaveState/LoadState` | Compose into slots; do not create another world snapshot format |
| `Viper.Game.UI.Hud*` and Canvas3D widget draw adapter | Reuse; GameBase3D only orchestrates UI state and overlay timing |
| `Hitbox3D`, `Health3D`, hit/damage events | Extend for ray hurt regions; do not fork melee/ranged health models |

## Plan 01 — Overlay alpha

Status: **Internal**.

No public API change. `Canvas3D.DrawRect2DAlpha` keeps its current signature and
contract: straight-alpha input in `[0,1]`, source-over blend in the final
overlay, no depth test, no post-FX modification.

## Plan 02 — AA-text identity

Status: **Internal**.

No public API change. Distinct `Pixels` objects queued in one frame must map to
distinct content generations even if dimensions and allocator reuse patterns
match.

## Plan 03 — Fixed-step frame driver

Status: **Proposed**, owner: plan 03.

Proposed class: `Viper.Game3D.FrameDriver3D`.

Draft construction and configuration:

```text
New(world: World3D, fixedDt: Float) -> FrameDriver3D
MaxFrameDt: Float read/write       default 0.25
MaxFixedSteps: Integer read/write  default 8
```

Draft poll/step contract:

```text
BeginFrame() -> Boolean
BeginFixedStep() -> Boolean
FixedDt: Float read-only
FrameDt: Float read-only
UnscaledFrameDt: Float read-only
InterpolationAlpha: Float read-only
PendingFixedSteps: Integer read-only
DroppedFixedSteps: Integer read-only
CommitFixedStep() -> Void
CancelFrame() -> Void
```

`BeginFrame` owns input/window polling and accumulator calculation. It consumes
the scaled `World3D.DeltaTime` produced by that one poll, so World3D remains the
single authority for pause, hit-stop, and time scale; `UnscaledFrameDt` exposes
the real clamped frame delta separately.
`BeginFixedStep` reserves one pending fixed step but does not simulate; script
runs authoritative game logic, then `CommitFixedStep` calls exactly one
World3D internal simulation-core step at `FixedDt` without polling, applying
time scale, or advancing frame/unscaled clocks a second time. Calling methods
out of order traps with a phase diagnostic.

Draft render delegation:

```text
BeginRender() -> Void
DrawStandardScene() -> Void
DrawStandardEffects() -> Void
EndRender() -> Void
BeginOverlay() -> Void
EndOverlay() -> Void
Present() -> Void
Phase: Integer read-only
```

These methods delegate to existing World3D/Canvas3D operations and validate
ordering. They do not prevent custom Canvas3D draws between phases.

## Plan 04 — Scene scope

Status: **Proposed**, owner: plan 04.

Proposed class: `Viper.Game3D.SceneScope3D`.

```text
New(world: World3D, name: String) -> SceneScope3D
Name: String read-only
Released: Boolean read-only
EntityCount/NodeCount/BodyCount/OtherCount: Integer read-only
TrackEntity(entity: Entity3D) -> Entity3D
TrackNode(node: Graphics3D.SceneNode) -> SceneNode
TrackBody(body: Graphics3D.PhysicsBody3D) -> PhysicsBody3D
TrackLight(light: Graphics3D.Light3D) -> Light3D
TrackParticles(particles: Graphics3D.Particles3D) -> Particles3D
TrackDecal(decal: Graphics3D.Decal3D) -> Decal3D
TrackSound(source: Graphics3D.SoundSource3D) -> SoundSource3D
CreateChild(name: String) -> SceneScope3D
Release() -> Void
```

Tracking the same object twice is idempotent. Child scopes release before the
parent. Release despawns tracked live entities first, detaches registered
services/effects, removes raw bodies/nodes, stops sounds, then drops retained
references. The ADR may replace individual typed methods with a safe common
owner hook only if all frontends retain qualified type safety.

Optional World3D convenience:

```text
CreateScope(name: String) -> SceneScope3D
```

## Plan 05 — Environment registry

Status: **Proposed/Extend**, owner: plan 05.

Proposed class: `Viper.Game3D.EnvironmentStack3D`, exposed read-only as
`World3D.Environment`.

```text
RegisterTerrain(terrain, x, y, z, scope?) -> Integer handle
RegisterWater(water, scope?) -> Integer handle
RegisterVegetation(vegetation, scope?) -> Integer handle
SetSky(sky, scope?) -> Integer handle
SetTimeOfDay(timeOfDay, scope?) -> Integer handle
SetPhase(handle, EnvironmentPhase3D value) -> Boolean
SetEnabled(handle, enabled) -> Boolean
Remove(handle) -> Boolean
Clear() -> Void
Count: Integer read-only
Update(dt: Float) -> Void
Draw(pass: RenderPass, canvas: Canvas3D, camera: Camera3D) -> Void
```

Proposed static constants surface `Viper.Game3D.EnvironmentPhase3D` identifies
environment orchestration points such as `OpaqueBeforeScene`,
`OpaqueAfterScene`, and `TransparentAfterScene`. It is deliberately separate
from the existing profiling-oriented `RenderPass` constants unless the ADR
proves an exact semantic match. The runtime chooses safe defaults per type.
`EnvHandle.WithTerrain/WithWater`
continues to work and registers its created content in the stack. Streamed
terrain remains owned by `WorldStream3D` and is not double-registered.

## Plan 06 — Character motor

Status: **Proposed**, owner: plan 06.

Proposed class: `Viper.Game3D.CharacterMotor3D` wrapping an existing
`CharacterController3D`.

```text
New(controller: CharacterController3D) -> CharacterMotor3D
Controller: CharacterController3D read-only
MoveIntent: Vec2 write/read
FacingYaw: Float write/read
JumpRequested/Sprint/Crouch: Boolean write/read
BaseSpeed/SprintMultiplier/Acceleration/Deceleration/AirControl: Float read/write
Gravity/JumpSpeed: Float read/write
Mode: Integer read/write  (grounded, fly, swim; constants in CharacterMotorMode)
Update(dt: Float) -> Void
AfterSimulation() -> Void
Velocity: Vec3 read-only
SpeedFraction: Float read-only
Grounded/JustLanded/JustJumped: Boolean read-only
ClearTransientIntent() -> Void
```

`Update` consumes intent before the World3D simulation commit.
`AfterSimulation` refreshes landed/jumped/velocity state immediately after that
commit; it is idempotent for the same committed step. The motor never polls
keys or owns a camera. A convenience method may map `Input3D` plus a yaw to
intent, but the core stays device- and camera-independent. Existing controllers
remain supported.

## Plan 07 — Camera rig

Status: **Proposed**, owner: plan 07.

Proposed classes: `Viper.Game3D.CameraRig3D` and static mode/constants class
`CameraRigMode`.

```text
New(world: World3D) -> CameraRig3D
Mode: Integer read/write  (first-person, follow, orbit, third-person)
Target: Entity3D read/write
Yaw/Pitch/Distance/PivotHeight: Float read/write
CollisionRadius/CollisionMask: Float/Integer read/write
Fov/BaseFov: Float read/write
SetLookIntent(delta: Vec2) -> Void
AddRecoil(pitch, yaw, returnSeconds) -> Void
AddShake(amplitude, frequency, duration, seed) -> Void
SetBob(amplitude, frequency, weight) -> Void
SetFovTarget(fov, blendSeconds) -> Void
Snap() -> Void
Update(dt: Float, interpolationAlpha: Float) -> Void
```

The rig may internally compose existing follow/third-person collision logic.
Additive recoil/shake/bob operates after the base mode and never mutates the
target entity. Randomized modifiers require an explicit seed.

## Plan 08 — Asset catalog/resolution

Status: **Proposed convenience over existing resolver**, owner: plan 08.

Proposed class: `Viper.Game3D.AssetCatalog3D`; proposed result class:
`AssetResolution3D`.

```text
New(gameRoot: String) -> AssetCatalog3D
Add(logicalName, packagePath, sourcePath, required) -> Boolean
Resolve(logicalName) -> AssetResolution3D
Exists(logicalName) -> Boolean
ClearCache() -> Void
MissingCount: Integer read-only
```

```text
AssetResolution3D.Found/Required/Packaged: Boolean
AssetResolution3D.LogicalName/ResolvedPath/Source/Diagnostic: String
```

Resolution policy delegates to `Viper.IO.Assets` and
`Viper.Assets.Resolver`. `ResolvedPath` preserves a package logical path when
the package resolver wins, allowing typed `Load*Asset` APIs. Typed loading stays
in `Assets3D`, SceneAsset, Sound3D, or IO APIs.

## Plan 09 — Quality policy

Status: **Proposed/Extend**, owner: plan 09.

Proposed class: `Viper.Game3D.QualityProfile3D`; static helper:
`Viper.Game3D.Quality.Resolve`.

```text
Quality.Resolve(world: World3D, requestedLevel: Integer) -> QualityProfile3D
Quality.ApplyProfile(world, profile) -> Void
```

Read-only resolved fields include:

```text
RequestedLevel, ActiveLevel, Fallback, FallbackReason
RenderScale, ShadowQuality, ShadowCascades, LightBudget
ParticleScale, DecalBudget, VegetationDensity
TerrainLodBias, WaterQuality, AnimationRateScale
OcclusionEnabled, PostFxLevel
```

The profile is an immutable decision record. The runtime applies fields it
owns; game-specific systems read the same record. Existing `Quality.Apply`
remains a compatibility wrapper around resolve + apply.

## Plan 10 — Entity-aware query and event surfaces

Status: **Proposed/Extend**, owner: plan 10.

Proposed classes: `WorldHit3D`, `WorldHitList3D`, `WorldEvent3D`; static
constants: `WorldEventKind`.

```text
World3D.Raycast(origin, direction, maxDistance, layerMask) -> WorldHit3D
World3D.RaycastAll(origin, direction, maxDistance, layerMask) -> WorldHitList3D
World3D.SweepSphere(center, radius, delta, layerMask) -> WorldHit3D
World3D.OverlapSphere(center, radius, layerMask) -> WorldHitList3D
```

`WorldHit3D` exposes `Hit`, `Distance`, `Fraction`, `Point`, `Normal`, raw
`Body`, resolved `Entity`, optional `Hitbox`, `RegionTag`, and
`StartedPenetrating`. Misses return a non-null result with `Hit=false`.

`WorldHitList3D` exposes `Count`, `Truncated`, and `Hit(index)`. The returned
hits remain valid for the documented list lifetime; invalid indexes trap using
the standard collection contract.

Event draft:

```text
World3D.EventCount() -> Integer
World3D.Event(index) -> WorldEvent3D
World3D.ClearEvents() -> Void
WorldEvent3D.Kind, Frame, EntityA, EntityB, HasPoint, Point, HasNormal, Normal,
Value, Tag
```

The unified stream initially mirrors existing collision, hit, damage,
animation, stream-load, interaction, and sound-report events. Existing typed
polling APIs remain. Event objects are immutable frame views and must not retain
dead entities beyond existing stale-handle rules.

## Plan 11 — Pooled feedback

Status: **Extend**, owner: plan 11.

Extend `EffectRegistry3D`:

```text
RegisterParticles(name, prototype, poolSize, lifetime) -> Boolean
RegisterDecal(name, prototype, poolSize, lifetime) -> Boolean
Emit(name, position, normal) -> Boolean
SetBudget(maxParticles, maxDecals) -> Void
PoolActive/PoolAvailable/PoolDropped(name) -> Integer
```

Extend `Sound3D`:

```text
RegisterCue(name, sound, voices, priority, minInterval) -> Boolean
PlayCueAt(name, position, volume, pitch) -> Boolean
PlayCueAttached(name, entity, volume, pitch) -> Boolean
StopCue(name) -> Void
CueActive/CueDropped(name) -> Integer
```

Registration retains prototypes/sounds. Exhaustion follows documented priority
and oldest-voice rules and increments counters. Existing one-shot helpers keep
working.

## Plan 12 — Application framework

Status: **Incubate**, owner: plan 12.

Upgrade `examples/games/lib/gamebase3d.zia` and `iscene3d.zia`; do not promote
them to C runtime ABI in the first delivery.

Draft `IScene3D` lifecycle:

```text
onEnter(world, scope)
onExit(world)
fixedUpdate(world, dt)
afterFixedStep(world, dt)
update(world, frameDt, alpha)
drawBeforeScene(world)
drawAfterScene(world)
drawOverlay(world)
onResize(world, width, height)
```

Draft `GameBase3D` responsibilities: own World3D, FrameDriver3D, root scope,
current/pending scene scopes, pause policy, transitions, `Viper.Game.UI` root
widgets, and deterministic frame limits. `afterFixedStep` is the documented
place to consume collision/damage/events and refresh intent-driven motor state
after `CommitFixedStep`. Scenes may omit work through default base methods if
Zia interface defaults remain unavailable; use a base class or null-object
adapter consistent with current language capability.

## Plan 13 — Ranged combat

Status: **Proposed/Extend**, owner: plan 13.

Proposed static helper: `Viper.Game3D.Ballistics3D`; result classes:
`ShotResult3D`, `ShotImpact3D`; proposed `DamageSpec3D` value object.

```text
DamageSpec3D.New(amount, tag) -> DamageSpec3D
DamageSpec3D.Penetration, MaxImpacts, Impulse, FriendlyFire: fields
Ballistics3D.Hitscan(world, origin, direction, range, mask, spec, source) -> ShotResult3D
Ballistics3D.RadialDamage(world, center, radius, mask, spec, source) -> Integer
```

`ShotResult3D` is a bounded ordered list of impacts. Each impact exposes the
entity, body, hitbox/hurt region, point, normal, distance, applied damage,
material/surface tag, and whether penetration continued.

`ShotResult3D` also exposes `Count`, `Truncated`, `Impact(index)`, and total
applied damage. Invalid indexes follow the standard trap convention.

Extend `Hitbox3D` for ray hurt regions:

```text
DamageMultiplier: Float read/write
RegionTag: Integer read/write
```

Ray queries must detect registered hurt hitboxes or resolve an entity-level
fallback. Existing melee collision behavior is unchanged.

## Plan 14 — Save-game composition

Status: **Proposed**, owner: plan 14.

Proposed class: `Viper.Game3D.SaveGame3D`; result metadata class:
`SaveSlotInfo3D`.

```text
New(gameId: String, schemaVersion: Integer) -> SaveGame3D
SetString/SetInt/SetFloat/SetBool(key, value)
GetString/GetInt/GetFloat/GetBool(key, fallback)
Save(slot: String, world: World3D) -> Boolean
Load(slot: String, world: World3D) -> Boolean
Delete(slot: String) -> Boolean
Exists(slot: String) -> Boolean
SlotCount/Slot(index) -> SaveSlotInfo3D
LastDiagnostic: String read-only
LoadedVersion: Integer read-only
```

`SaveSlotInfo3D` exposes `Name`, `SchemaVersion`, `Timestamp`, and a validity/
diagnostic field appropriate to the approved on-disk metadata contract.

The class composes an atomic metadata/custom-data document with the existing
world snapshot. Migration remains game-owned through explicit version checks
in v1; no arbitrary script callback is invoked by the runtime.

## Plan 15 — Scenario harness

Status: **Incubate/Internal**, owner: plan 15.

Add `examples/games/lib/scenario3d.zia` and shared test helpers. No product API
is required initially. The harness wraps current synthetic input, FrameDriver3D,
fixed stepping, final capture, pixel sampling, state traces, and standardized
`RESULT: pass/fail` output. Promotion requires evidence from all three games.

## Plans 16–20

Documentation, migrations, and release orchestration add no public product API
unless a preceding plan's ADR explicitly assigns them cleanup work. Demo-only
helpers stay within their game or `examples/games/lib`; they must not silently
become runtime contracts.

## Compatibility policy

- All current methods remain source and behavior compatible unless a separate
  deprecation ADR is approved.
- New framework use is opt-in. No default World3D frame order changes until the
  environment registry is enabled by explicit registration.
- Existing `Quality.Apply`, environment presets, effects presets, and Run loops
  become wrappers/delegates where appropriate, not duplicate implementations.
- New query results do not change low-level PhysicsHit3D semantics.
- Demo migrations may delete game-side abstractions only after equivalent tests
  pass through the runtime abstraction.
