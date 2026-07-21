---
status: active
audience: contributors
last-verified: 2026-07-20
---

# ADR 0145: Complete FBX Evaluation and Native Authored Lights

## Status

Accepted (2026-07-20).

## Context

ADR 0142 completed FBX mesh, hierarchy, camera, punctual-light, object-animation,
and morph publication, while explicitly recording the source constructs that the
runtime still could not represent. The remaining gaps are not parser gaps: the
typed FBX graph already contains procedural surface geometry, animation layers,
constraints, camera property curves, and area/volume-light attributes. They need
deterministic evaluation and retained runtime destinations.

Today the importer:

- only accepts polygon `Mesh` geometry and leaves NURBS surfaces and patches
  unpublished;
- takes the first curve found for an axis, ignoring animation-layer order,
  weights, mute/solo state, and accumulation mode;
- handles camera look-at/up connections as a one-time orientation but ignores
  the general FBX constraint graph;
- publishes a camera independently from its scene node, so object animation does
  not move the returned camera and camera projection curves have no destination;
  and
- converts FBX area and volume lights to point lights.

Completing these paths changes the `SceneNode`, `Camera3D`, `Light3D`, animation,
renderer-backend, and VSCN runtime contracts. Repository policy therefore
requires an ADR before implementation.

## Decision

### Shared, bounded FBX evaluator

Static scene publication, skeletal clips, node clips, cameras, and constraints
use one evaluator over the typed FBX object/property/connection graph. Object IDs
remain the only identity keys. Every generated sample is finite and charged to
the existing aggregate FBX load budget. Evaluation dependency cycles, invalid
required targets, impossible topology, or output beyond the mesh/sample limits
reject the complete load transaction rather than publishing a partial scene.

No feature toggle is added. Loading an FBX file opts into its authored semantics,
and there is no new user configuration. The evaluator remains dependency-free
and uses only fixed-width integers, checked allocation arithmetic, and portable C.

### NURBS surfaces and patches

`Geometry` objects of class `NurbsSurface`, `Nurbs`, and `Patch` are converted to
ordinary indexed `Mesh3D` resources before the existing material, skin, morph,
and scene-node passes run.

For NURBS, the importer reads U/V control counts, order, open/closed/periodic
form, knot vectors, step counts, and homogeneous control points. It validates
nondecreasing finite knots, legal order/count relationships, nonzero rational
weights, and an exact control-net length. Cox-de Boor basis evaluation uses a
stable iterative implementation; closed/periodic nets wrap their authored
control domain without duplicating a seam vertex. Missing legal clamped knot
vectors are generated deterministically from order and control count.

For patches, the importer accepts linear, Bézier, quadratic Bézier, cardinal,
and B-spline U/V bases and evaluates their tensor product. U/V step values mean
subdivisions between adjacent authored spans. Steps less than one use one;
values above 256 reject the surface. Tessellation emits row-major positions,
UVs over the evaluated parameter domain, derivative normals with a bounded
finite-difference fallback, and two consistently wound triangles per quad.
Closed seams share indices. The existing maximum vertex/index and aggregate-byte
budgets apply before allocation.

Unsupported trim curves do not silently change a surface boundary: a surface
with a connected active trim region is rejected with `FBX: trimmed NURBS
surfaces are malformed or unsupported by this file version` until the trim is
represented by the typed graph. Curves with no surface connection remain
non-renderable editor data and do not constitute scene loss.

### Animation-layer composition

Every `AnimationStack` evaluates all connected `AnimationLayer` objects in
connection order. Solo layers, when any are active, suppress every non-solo
layer; otherwise muted layers are skipped. Layer weight is a finite percentage
clamped to `[0, 100]` and may itself be driven by an animation curve.

The base/default property value seeds each channel. Layers then compose in stack
order:

- additive translation and Euler channels add the weighted layer delta;
- override channels interpolate from the accumulated value to the layer value;
- override-passthrough applies override to channels present on the layer and
  leaves absent channels unchanged;
- rotation-by-layer composes shortest-path weighted quaternions in layer order,
  while rotation-by-channel composes weighted Euler components in the model's
  authored rotation order; and
- scale accumulation `Multiply` exponentiates each positive layer scale ratio by
  its weight, while `Additive` adds the weighted delta from unit scale.

Constant, linear, and cubic curve behavior continues to use ADR 0142's adaptive
sampling bounds. The sample-time union includes keys from every contributing
layer, animated layer weights, parent/constraint dependencies, and camera
properties. The same composition code feeds skeleton and non-skeleton clips, so
the two paths cannot disagree.

### Constraints

The importer resolves FBX `Constraint` objects by numeric connections and bakes
their evaluated result into static transforms and generated animation samples.
It supports position, rotation, scale, parent, aim, and single-chain IK
constraints, including active/locked state, per-source weight, overall weight,
axis-affect masks, maintain-offset values, world/up objects, and authored aim/up
axes.

Position/rotation/scale constraints blend finite source components in world
space and convert the result back through the target parent. Parent constraints
blend source TRS using linear translation/scale and hemisphere-aligned normalized
quaternions. Aim constraints construct an orthonormal basis from the weighted
target direction and the selected world-up mode, with a deterministic fallback
axis for parallel vectors. Single-chain IK uses a bounded FABRIK solve over the
connected ancestor chain, preserves segment lengths, honors pole/up guidance,
and stops after 32 iterations or a relative error of `1e-6`; an unsolvable chain
retains its closest finite pose and records one bounded warning.

Constraint dependencies are topologically ordered for each sample. A strongly
connected active dependency, missing required constrained object, mismatched IK
chain, non-finite offset, or source-weight table mismatch rejects the FBX load.
Unknown custom constraint classes are preserved in the import report but do not
claim successful constraint fidelity.

### Cameras belong to scene nodes

`SceneNode` gains a retained optional `Camera3D` slot, parallel to its existing
mesh, material, and light slots:

| Runtime name | C symbol | Signature |
|---|---|---|
| `Zanna.Graphics3D.SceneNode.set_Camera` | `rt_scene_node3d_set_camera` | `void(obj,obj<Zanna.Graphics3D.Camera3D>)` |
| `Zanna.Graphics3D.SceneNode.get_Camera` | `rt_scene_node3d_get_camera` | `obj<Zanna.Graphics3D.Camera3D>(obj)` |

Imported FBX cameras are attached to their model node and remain listed in the
`SceneAsset` camera inventory. Instantiation clones each attached camera; scene
instances therefore have independent mutable projection/view state. After node
animation and constraints update, `SceneGraph.SyncBindings` derives the camera
eye, forward (`-Z`), and up (`+Y`) from the node's world matrix. This automatic
coupling is also applied after reparenting and direct transform edits.

`Camera3D` adds writable projection mode and orthographic size:

| Runtime name | C symbol | Signature |
|---|---|---|
| `Zanna.Graphics3D.Camera3D.set_IsOrtho` | `rt_camera3d_set_is_ortho` | `void(obj,i1)` |
| `Zanna.Graphics3D.Camera3D.get_OrthoSize` | `rt_camera3d_get_ortho_size` | `f64(obj)` |
| `Zanna.Graphics3D.Camera3D.set_OrthoSize` | `rt_camera3d_set_ortho_size` | `void(obj,f64)` |

Private `NodeAnimation3D` paths extend the existing TRS/weights enum with scalar
camera FOV, aspect, near, far, orthographic-size, and projection-mode paths.
FBX curves targeting `FieldOfView`, `FieldOfViewX/Y`, `FocalLength`,
`AspectWidth/Height`, `NearPlane`, `FarPlane`, `OrthoZoom`, or
`CameraProjectionType` are evaluated through the same layer stack. Projection
mode uses step sampling. Focal length is converted using the animated aperture.
Every value passes through native `Camera3D` sanitizers before publication.

VSCN persists the node-to-camera table index and all camera scalar animation
paths. Versions before the new format load with unattached inventory cameras,
matching their historical behavior.

### Native area and volume lights

`Light3D` adds native types `4=rectangle area`, `5=sphere area`, and `6=volume`.
Its retained state adds an orthonormal U/V basis, width, height, radius, finite
range, and FBX decay mode (`0=none`, `1=linear`, `2=quadratic`, `3=cubic`). New
constructors and properties are:

| Runtime name | C symbol | Signature |
|---|---|---|
| `Light3D.AreaRectangle` | `rt_light3d_new_area_rectangle` | `obj(obj,obj,f64,f64,f64,f64,f64,f64,f64)` |
| `Light3D.AreaSphere` | `rt_light3d_new_area_sphere` | `obj(obj,f64,f64,f64,f64,f64)` |
| `Light3D.Volume` | `rt_light3d_new_volume` | `obj(obj,f64,f64,f64,f64,f64)` |
| `Light3D.get_Width/set_Width` | `rt_light3d_get_width` / `rt_light3d_set_width` | `f64(obj)` / `void(obj,f64)` |
| `Light3D.get_Height/set_Height` | `rt_light3d_get_height` / `rt_light3d_set_height` | `f64(obj)` / `void(obj,f64)` |
| `Light3D.get_Radius/set_Radius` | `rt_light3d_get_radius` / `rt_light3d_set_radius` | `f64(obj)` / `void(obj,f64)` |
| `Light3D.get_DecayType/set_DecayType` | `rt_light3d_get_decay_type` / `rt_light3d_set_decay_type` | `i64(obj)` / `void(obj,i64)` |
| `Light3D.get_Range/set_Range` | `rt_light3d_get_range` / `rt_light3d_set_range` | `f64(obj)` / `void(obj,f64)` |

Rectangle shading evaluates the closest point on the oriented emitting rectangle,
one-sided emitter cosine, distance decay, and a bounded solid-angle factor.
Sphere shading evaluates the closest point on the emitter surface and a
radius-dependent solid-angle factor. Volume lights contribute isotropic local
irradiance inside their radius, fading smoothly to zero at the boundary. These
rules are implemented equivalently in the software, OpenGL, Metal, and D3D11
paths and participate in clustered-light bounds and relevance selection.

Area lights may request shadows; the existing shadow system uses a documented
center-point approximation until area-shadow sampling is introduced. Volume
lights never claim a shadow slot. Scene traversal transforms the emitter basis
and dimensions by the node world transform, so animated scale and rotation have
native effect. VSCN persists the complete light state.

FBX `LightType` area/volume attributes map directly. Rectangle/sphere shape,
decay, decay start/range, and area dimensions are read from the attribute with
model-property fallbacks for exporter variants. No approximation warning is
emitted for a successfully represented light.

## Explicit Rejections That Remain

Malformed or unsafe input remains rejected: invalid control nets/knots, excessive
tessellation, non-finite curves or layer weights, invalid constraint graphs,
unknown mandatory projection modes, impossible light dimensions, generated data
beyond aggregate limits, and any out-of-range connection. Unknown optional
editor metadata may be reported or preserved, but never executed.

## Consequences

- Procedural FBX surfaces become ordinary runtime meshes and automatically gain
  the existing material, scene, bake, and rendering behavior.
- Static and animated poses use the same full animation-layer and constraint
  evaluator for skeletal, object, light, and camera nodes.
- An instantiated scene camera follows its authored node without application
  glue, while each instance remains independent.
- Area and volume lighting is a portable renderer feature rather than importer
  metadata or a point-light approximation.
- The Graphics3D ABI manifest, graphics-disabled stubs, generated runtime docs,
  and VSCN format all change deliberately under this ADR.

## Validation

- Binary and ASCII fixtures cover open/closed/periodic rational NURBS, every
  patch basis, seam indexing, invalid knots/control counts, and budget rollback.
- Layer fixtures cover additive/override/override-passthrough, mute/solo,
  animated weight, rotation and scale accumulation modes, negative time, and
  identical skeleton/object results.
- Constraint fixtures cover every built-in type, multiple weighted sources,
  offsets/masks, animated dependencies, cycle rejection, and bounded IK failure.
- Camera fixtures cover node attachment, instance cloning, transform and scalar
  projection animation, direct edits, VSCN round trip, and old-file behavior.
- Pixel-reference tests compare rectangle, sphere, and volume output across the
  software reference and backend parameter layouts; unit tests cover decay,
  transformed basis/size, clustering, shadows, serialization, and FBX mapping.
- Registry, ABI, generated docs, graphics-disabled builds, platform-policy lint,
  cross-platform smoke, and the official build scripts complete the gate.

