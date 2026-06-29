---
status: active
audience: public
last-verified: 2026-06-13
---

# 3D Rendering, Animation, and Environment
> Camera, lighting, animation, audio, particles, post-processing, terrain, navigation, and environment helpers for the Viper.Graphics3D namespace.

**Part of [Viper Runtime Library](../README.md) › [Graphics](README.md)**

---

This page documents the `Viper.Graphics3D` runtime surface for classes not covered by [3D Physics](physics3d.md). For mesh loading, material authoring, scene graphs, and the full 3D asset pipeline, see the [Graphics 3D Guide](../../graphics3d-guide.md).

For the higher-level code-first game workflow, see [Game3D](game3d.md).
`Viper.Game3D` is implemented in the same C runtime layer and wraps the lower
level rendering, physics, input, audio, and final-frame contracts documented
here.

## Asset Load Diagnostics

Script-facing content loaders do not trap for bad or missing content. Missing
files, unreadable files, wrong magic bytes, truncated payloads, corrupt
structure, unsupported formats, and excessive content sizes return `null` and
record diagnostics on `Viper.Graphics3D.AssetDiagnostics3D`. Traps remain reserved for
programmer errors such as `null` or invalid argument handles. Successful partial
degradation, such as an OBJ material whose albedo texture is missing, returns
the loaded asset and records warnings.

`AssetDiagnostics3D.LastLoadError` is empty after a fully successful load. When a loader
returns `null`, `AssetDiagnostics3D.LastLoadErrorCode` is one of:

| Code | Meaning |
|------|---------|
| `0` | No error |
| `1` | NotFound |
| `2` | Unreadable |
| `3` | BadMagic |
| `4` | Corrupt |
| `5` | Unsupported |
| `6` | TooLarge |

Warnings are per outer load, append-only, and capped. Use
`AssetDiagnostics3D.LoadWarningCount`, `AssetDiagnostics3D.GetLoadWarning(index)`, or
`AssetDiagnostics3D.GetLoadWarnings()` to inspect partial degradation.

| Loader | Content failure behavior | Partial degradation |
|--------|--------------------------|---------------------|
| `SceneAsset.Load(path)` / `SceneAsset.LoadAsset(path)` | Returns `null` and sets `AssetDiagnostics3D.LastLoadError` for missing, unreadable, unsupported, malformed, truncated, or oversized `.vscn`, `.fbx`, `.gltf`, `.glb`, `.obj`, and `.stl` content | Preserves lower-level warnings from material texture and dependency loads |
| `FBX.Load(path)` | Returns `null` for missing, unreadable, wrong-magic, truncated, malformed, unsupported, or oversized FBX content | Missing texture references leave the material untextured and add warnings |
| `GLTF.Load(path)` / `GLTF.LoadAsset(path)` | Returns `null` for missing roots, unreadable roots, wrong JSON/GLB magic, malformed JSON, corrupt buffers/accessors, missing required buffers, unsupported dependencies, or oversized content | Missing or unreadable material images leave that texture slot empty and add warnings |
| `Mesh3D.FromOBJ(path)` | Returns `null` for missing files, invalid face indices, invalid numeric tokens, empty geometry, malformed syntax, or oversized accumulators | None |
| `Mesh3D.FromSTL(path)` | Returns `null` for missing files, unreadable files, wrong magic, truncated binary payloads, malformed ASCII payloads, or oversized files | Degenerate triangles are skipped as before |
| `SceneGraph.Load(path)` | Returns `null` for missing, unreadable, non-JSON, malformed, corrupt, or oversized `.vscn` content | None |
| `Pixels.Load(path)` and image loads reached from materials | Return `null` for missing, unreadable, wrong-magic, corrupt, unsupported, or oversized PNG/JPEG/BMP/GIF content | Material loaders catch this and record a warning instead of failing the whole model |

ASCII FBX files with the standard `; FBX` comment header are rejected as
unsupported content with the exact error
`ASCII FBX is not supported; re-export as binary FBX`. This is distinct from
wrong-magic and corrupt binary FBX diagnostics.

glTF extension support is explicit:

| Extension | `extensionsRequired` | Optional `extensionsUsed` | Degradation behavior |
|-----------|----------------------|---------------------------|----------------------|
| `KHR_texture_transform` | Supported | Supported | UV transform metadata is imported |
| `KHR_materials_emissive_strength` | Supported | Supported | Emissive intensity is imported |
| `KHR_materials_unlit` | Supported | Supported | Material uses unlit shading |
| `KHR_materials_specular` | Supported | Supported | Specular factors/textures are imported |
| `KHR_lights_punctual` | Supported | Supported | Punctual lights are imported |
| `KHR_texture_basisu` | Not supported as required | Optional KTX2 source selection supported | Required use returns `requires KHR_texture_basisu (unsupported)` |
| `KHR_materials_clearcoat` | Not supported as required | Optional approximation supported | Clearcoat values map to reflectivity/custom material params |
| `KHR_materials_transmission` | Not supported as required | Optional approximation supported | Transmission factor is recorded; material remains opaque |
| Other extensions, including `KHR_draco_mesh_compression` | Not supported | Ignored with a load warning | Visual result may miss extension-specific material, geometry, animation, lighting, or texture behavior |

Unsupported required extensions fail the load and name the extension list, for
example `GLTF.Load: requires KHR_draco_mesh_compression (unsupported)`. Draco
mesh compression is not implemented.

Writable `Viper.Graphics3D` properties expose both property accessors and
method-call setters. A writable property named `X` has a `set_X` runtime
accessor and a matching `SetX(...)` class method, such as
`Material3D.SetRoughness(material, value)` or `material.SetRoughness(value)`.

---

## Camera And Rendering

### Canvas3D Frame Finalization

`Canvas3D.End()` closes the current 3D or 2D draw pass. It does not present the
frame and does not run post-processing. Finalization is the step that applies
`PostFX3D` and composites any final overlay recorded for the frame.

| Method | Signature | Description |
|--------|-----------|-------------|
| `BeginOverlay()` | `Void()` | Start recording a final 2D overlay pass |
| `EndOverlay()` | `Void()` | Finish final overlay recording |
| `ClearOverlay()` | `Void()` | Discard the current frame's recorded final overlay |
| `DrawRect3D(x, y, w, h, color)` | `Void(Integer, Integer, Integer, Integer, Integer)` | Draw a filled screen-space rectangle (used in overlay/2D passes) |
| `DrawText3D(x, y, text, color)` | `Void(Integer, Integer, String, Integer)` | Draw screen-space text (used in overlay/2D passes) |
| `Resize(w, h)` | `Void(Integer, Integer)` | Resize the canvas and active backend output targets |
| `FinalizeFrame()` | `Void()` | Apply post-FX and final overlay once, without presenting |
| `ScreenshotFinal()` | `Object()` | Finalize if needed, then capture finalized pixels |
| `FrameFinalized` | `Boolean` | True once the current frame has been finalized |

Use final overlays for HUD text, reticles, debug labels, and capture annotations
that must remain crisp after bloom, tonemapping, or color grading. `Flip()`
finalizes automatically if the frame has not already been finalized, so
`ScreenshotFinal()` can be followed by `Flip()` without re-running post-FX.

```rust
Canvas3D.Begin(canvas, cam)
Canvas3D.DrawMesh(canvas, mesh, model, material)
Canvas3D.End(canvas)

Canvas3D.BeginOverlay(canvas)
Canvas3D.DrawText2D(canvas, 12, 12, "READY", 0xFFFFFFFF)
Canvas3D.EndOverlay(canvas)

var capture = Canvas3D.ScreenshotFinal(canvas)
Canvas3D.Flip(canvas)
```

The repo-level sample `examples/3d/walk_min.zia` shows the complete Phase 0B
frame path in one small program: explicit default lighting, a primitive scene,
CPU-safe post-FX, final overlay recording, and `ScreenshotFinal()` coverage.
`examples/3d/walk_min_probe.zia` is registered as a software-backend ctest and
compares against `examples/3d/baselines/walk_min_software.png`.

### Canvas3D Window, Image, and Foliage Helpers

- **Fullscreen** — `Canvas3D.SetFullscreen(canvas, on)`, `Canvas3D.ToggleFullscreen(canvas)`,
  and `Canvas3D.get_IsFullscreen(canvas)` switch the backing window between windowed and
  native desktop fullscreen (implemented per-platform on Cocoa, Win32, and X11). The canvas
  re-syncs its size on the toggle and the per-frame projection derives aspect from the active
  output, so the view stays un-stretched. `Game3D.Keys.get_F11` pairs naturally with these.
- **Overlay image blit** — `Canvas3D.DrawImage2D(canvas, x, y, w, h, pixels)` blits a `Pixels`
  image into the 2D overlay (unlit, screen-space) scaled to `w×h`. Combine with
  `RenderTarget3D.AsPixels(rt)` to show a rendered texture, such as a top-down minimap, on the HUD.
- **Wind foliage** — `Canvas3D.DrawMeshWind(canvas, mesh, transform, material, dirX, dirZ, strength, phase)`
  draws a mesh with a height-weighted per-vertex sway: the base (lowest local-Y) stays planted
  while the canopy bends along `(dirX, dirZ)` by `sin(phase)`. Pass per-instance `phase` offsets so
  a cluster ripples instead of pulsing. It runs on every backend because the geometry is deformed
  before submission rather than in a vertex shader.
- **Skinned draw** — `Canvas3D.DrawMeshSkinned(canvas, mesh, transform, material, animator)` accepts
  an `AnimController3D` as well as an `AnimPlayer3D`, so a state-machine pose (idle/walk crossfades)
  can skin the mesh directly.

### Canvas3D Synthetic Input and Clock

Live loops call `Canvas3D.Poll()` once per frame and then read
`Viper.Input.Keyboard`, `Viper.Input.Mouse`, gamepad, or action APIs. `Poll()`
returns `1` while the canvas remains open and `0` when the backing window is
closed or event pumping fails. Use `PollEvent()` when low-level integrations need
raw window event types; gameplay should normally use the state APIs because they
expose coherent `WasPressed`, `WasReleased`, held, and mouse-delta state.
With the live clock selected, `Poll()` also advances `DeltaTime` so manual loops
that tick simulation before presentation do not depend on `Flip()` for timing.

Deterministic tests can switch a canvas to scripted input and fixed time:

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetInputSource(mode)` | `Void(Integer)` | `0` live, `1` synthetic, `2` live plus synthetic |
| `PushSyntheticKey(key, down)` | `Void(Integer, Boolean)` | Queue a synthetic key transition |
| `PushSyntheticMouse(dx, dy, buttons, wheel)` | `Void(Double, Double, Integer, Double)` | Queue mouse delta, button bitmask, and vertical wheel |
| `ClearSyntheticInput()` | `Void()` | Clear queues and release synthetic-held buttons/keys |
| `SetClockSource(mode)` | `Void(Integer)` | `0` live wall clock, `1` fixed synthetic dt |
| `SetSyntheticDeltaTimeSec(dt)` | `Void(Double)` | Configure fixed synthetic dt in seconds |
| `AdvanceSyntheticFrame()` | `Void()` | Advance one deterministic input/timing frame without pumping OS events |

Synthetic events are applied through the same keyboard and mouse state update
functions as live events. That means camera controllers and action bindings do
not need a test-only input path. `DeltaTime`/`DeltaTimeSec` can be zero on the
first live frame; with the synthetic clock selected, they report the configured
fixed dt after `AdvanceSyntheticFrame()`, synthetic `Poll()`, or `Flip()`.

`Canvas3D.Width` / `Height` report the active output. They follow the bound
`RenderTarget3D` during RTT passes. `WindowWidth` / `WindowHeight` always report
the backing window, and `ActiveOutputWidth` / `ActiveOutputHeight` are explicit
aliases for the active-output behavior.
`Canvas3D.Resize(width, height)` updates the backing window size and the backend
framebuffer size together; the next `Begin(camera)` uses the resized active
output aspect without mutating the camera's stored projection.

`PollEvent()` drains the per-canvas event queue in FIFO order and returns `0`
when no queued event remains.

### Canvas3D Backend Selection

`Canvas3D.New(title, width, height)` selects the platform GPU backend by
default: Metal on macOS, Direct3D 11 on Windows, and OpenGL 3.3 on Linux. If
the selected GPU backend cannot create its context, Canvas3D automatically
creates the software backend instead and emits one stderr notice for the process.
Windows on ARM64 defaults to software because affected drivers can fail during
presentation; users can still opt into D3D11 with `VIPER_3D_BACKEND=d3d11`.

When the software backend is active, the main color raster pass and software
shadow-map pass use fixed 64x64 tiles and a context-owned worker pool. The
default worker count is `min(hardware_threads, 8)`. Set
`VIPER_3D_SW_THREADS=1` before creating the canvas to force the exact serial
path; positive values above one are clamped to `1..8`. Tile bins preserve mesh
submission order inside each tile, so depth, alpha, blending, texture sampling,
fog, lighting, and shadow depth generation remain byte-deterministic across
worker counts.

| Property / Method | Type | Description |
|-------------------|------|-------------|
| `Backend` | `String` | Active renderer name: `software`, `metal`, `d3d11`, or `opengl` |
| `BackendName` | `String` | Alias for `Backend` for code that wants an explicit name getter |
| `BackendFallback` | `Boolean` | True when Canvas3D fell back from the selected GPU backend to software at creation |
| `BackendSupports(name)` | `Boolean` | Tests backend capabilities; `runtime-fallback`, `backend-fallback`, and `software-fallback` report the `BackendFallback` state |

Use `BackendSupports("gpu")`, `BackendSupports("software")`, or
`BackendFallback` for control flow. String comparisons against `Backend` or
`BackendName` should be reserved for logs and diagnostics.

### Canvas3D Quality Profiles

`Canvas3D.SetQuality(profile)` installs a backend-safe post-FX profile:

| Profile | Value | Behavior |
|---------|-------|----------|
| Performance | `0` | Minimal CPU-safe chain |
| Balanced | `1` | CPU-safe bloom, tonemap, FXAA, and color grade |
| Cinematic | `2` | CPU-safe cinematic chain, plus SSAO/DOF/motion blur only on GPU-window post-FX |

The fallback state is inspectable for debug overlays:

| Property | Type | Description |
|----------|------|-------------|
| `QualityRequested` | `Integer` | Last requested profile |
| `QualityActive` | `Integer` | Active profile after fallback |
| `QualityFallback` | `Boolean` | True when unsupported GPU-only effects were omitted |
| `QualityFallbackReason` | `String` | Empty when no fallback occurred |

`PostFX3D.NewQuality(canvas, profile)` builds the same chain without attaching
it. Game3D quality helpers should wrap these runtime APIs rather than hand-roll
backend checks in Zia code.

### Canvas3D Lighting Helpers

`Canvas3D.SetDefaultLighting()` installs an explicit key/fill directional setup
and a readable ambient color. It is not implicit fallback lighting: scenes stay
dark after `ClearLights()` plus low ambient unless the caller opts into new
lights.

| Method / Property | Signature | Description |
|-------------------|-----------|-------------|
| `SetLight(index, light)` | `Void(Integer, Object)` | Bind or clear a retained `Light3D` slot; invalid indices or non-light handles trap |
| `ClearLights()` | `Void()` | Clear every retained canvas light slot |
| `SetDefaultLighting()` | `Void()` | Install conservative key/fill/ambient defaults |
| `LightCount` | `Integer` | Count active enabled canvas-slot lights |
| `MaxActiveLights` | `Integer` | Current active-light budget for the selected lighting path |
| `SetClusteredLighting(enabled)` | `Void(Boolean)` | Enable clustered/forward+ lighting only when the backend advertises support |
| `SetAmbient(r, g, b)` | `Void(Double, Double, Double)` | Set ambient color |
| `SetShadowCascades(count)` | `Void(Integer)` | Request cascaded shadow maps; counts above `1` require backend CSM support |

The default path remains fixed forward lighting with `MaxActiveLights == 16`.
`BackendSupports("clustered-lighting")` gates the many-light path; enabling it
without support traps before mutating the canvas so fallback behavior stays
explicit. The software backend advertises this capability as the correctness
baseline and raises the bounded active-light payload to 64. The real Metal,
D3D11, and OpenGL backend vtables also advertise it: their main shaders and
light-upload paths consume the bounded 64-light payload, while synthetic/fake
test backends with GPU-like names do not advertise production support. The
open-world GPU smoke records a 24-light run against the 16-light fallback.
`BackendSupports("shadow-csm")` similarly gates cascaded shadows;
`SetShadowCascades(1)` preserves the non-cascaded shadow path. On supporting
software and real platform GPU backends, counts above one render the primary
directional shadow caster into up to four camera-depth cascades, publish split
metadata in the backend light payload, and keep unsupported/fake backends
trapping before mutation. The open-world GPU smoke records a 3-cascade Metal
fixture (`CSM_SHADOWS`) after the clustered-lighting probe.

Shadow-map slots are a shared four-light budget. Enabled directional lights with
`CastsShadows` are selected first by luminance-weighted intensity. Any remaining
slots are filled by enabled spot lights with `CastsShadows`, ranked by
intensity and distance to the active camera. Directional lights may use cascades;
spot lights always use one perspective shadow map built from the light position,
direction, outer cone angle, and range. Shadow contribution is limited to the
spot cone, so pixels outside the outer cone do not sample the map border. Point
lights do not cast shadows yet: setting `CastsShadows = true` on a point light is
accepted and trap-free, but it does not allocate a shadow slot.

`BackendSupports("bc7")`, `BackendSupports("astc")`, and
`BackendSupports("etc2")` report native compressed texture upload support for
the active backend/device. `BackendSupports("texture:bc7")`,
`BackendSupports("texture:etc2")`, `BackendSupports("texture:astc")`, and
`BackendSupports("texture:ktx2-cpu")` report runtime CPU fallback coverage.
KTX2 supercompression is rejected; native mip payload lengths are validated
against the declared format/block dimensions.
`BackendSupports("anisotropy")` reports whether the active GPU backend applies
`Material3D.Anisotropy`; the software backend accepts the property but ignores
it and reports false.

| Texture format | Software / RenderTarget3D CPU path | Native GPU upload path | Undecodable CPU blocks |
|----------------|-------------------------------------|------------------------|------------------------|
| RGBA8 KTX2 | Full decode to `Pixels` | Uploaded as ordinary RGBA texture | Load fails on malformed bytes |
| BC3 KTX2 | Full BC3/DXT5 decode to `Pixels` | No `BackendSupports` native key | Load fails on malformed bytes |
| BC7 KTX2 | BC7 modes 0-7 decode to `Pixels`; `texture:bc7` reports true | Device-dependent `bc7` key | 8x8 magenta/black checker + load warning |
| ETC2 RGBA8/EAC KTX2 | Individual/differential color modes decode; `texture:etc2` reports true | Device-dependent `etc2` key | 8x8 magenta/black checker + load warning |
| ASTC KTX2 | LDR 2D void-extent blocks decode; `texture:astc` reports true | Device-dependent `astc` key | 8x8 magenta/black checker + load warning |

All native block-compressed mip payloads are still retained internally for
capability-gated backend upload. On the software backend, the native upload keys
are false and the `texture:*` CPU keys are true for the decoder coverage above.
`TextureAsset3D.SetResidentMipRange` switches the active fallback to the first
resident mip while updating byte telemetry.
Materials retain the texture asset and resolve that active fallback at draw
time, so already-bound materials follow later residency changes; when no
fallback exists, capable GPU backends receive the resident compressed blocks
through the same upload-budget telemetry path.

| Backend | `BackendSupports("anisotropy")` | Sampler behavior |
|---------|---------------------------------|------------------|
| Software | False | Accepted and ignored; software sampling remains controlled by `texture_filter` |
| D3D11 | True | Uses `D3D11_FILTER_ANISOTROPIC` with `MaxAnisotropy` clamped to `1..16` |
| Metal | True | Uses `MTLSamplerDescriptor.maxAnisotropy` clamped to `1..16` |
| OpenGL | Extension-dependent | Uses `GL_TEXTURE_MAX_ANISOTROPY_EXT` when `GL_EXT_texture_filter_anisotropic` is present, clamped to `GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT` and `16` |

`Material3D.Anisotropy = 1` disables anisotropic filtering. Values below `1`
clamp to `1`; values above `16` clamp to `16`. GPU backends cache sampler
states by wrap mode, filter, and effective anisotropy, so changed settings reuse
existing sampler objects once created.

### Canvas3D Performance Telemetry

| Member | Type | Description |
|--------|------|-------------|
| `TextureUploadBytes` | `Integer` property | Texture bytes uploaded into backend storage during the latest ended frame |
| `TextureUploadPendingBytes` | `Integer` property | Texture bytes still waiting for backend texture or cubemap upload budget |
| `FrameGpuTimeUs` | `Integer` property | Latest completed backend GPU frame time in microseconds, or `0` when unsupported/not yet available |
| `DrawsSubmitted` | `Integer` property | Backend draw submissions issued since the latest public `Begin`/`Begin2D` |
| `AabbTransforms` | `Integer` property | World-AABB transform computations performed since the latest public `Begin`/`Begin2D` |
| `SortPasses` | `Integer` property | Stable deferred sort passes run since the latest public `Begin`/`Begin2D` |
| `BackendStateChanges` | `Integer` property | Material/backend state runs observed in Canvas3D submission order |
| `SetTextureUploadBudget(bytes)` | `Void(Integer)` method | Set the backend material-texture/cubemap upload byte budget per frame; negative means unlimited, `0` pauses new upload rows |

`TextureUploadBytes` counts actual texture cache uploads and re-uploads performed by the active
Metal, OpenGL, or D3D11 backend. Pixels-backed 2D material textures and cubemaps
are advanced in row slices under `SetTextureUploadBudget`; native compressed
`TextureAsset3D` blocks are submitted by resident mip under the same budget and
telemetry. Cache hits report no new upload bytes, software/unsupported backends
report `0`, and the value is reset at the next non-overlay frame begin. D3D11
rejects out-of-range row slices, malformed native block descriptors, and uploads
whose byte counts cannot fit D3D11 `UINT` fields before it touches GPU resources.
Its post-FX and readback paths also reject malformed effect-chain storage and
unsupported staging formats before replaying GPU passes or mapping CPU-readable
textures.
`TextureUploadPendingBytes` reports remaining queued texture/cubemap row bytes
plus native compressed mip bytes and returns to `0` after final submissions
drain. The open-world native-compressed hitch CTest records the selected
backend's raw RGBA bytes, compressed resident/upload bytes, RAM/VRAM reduction
percentages, and final-frame tolerance after the budgeted native mip upload
drains. Use these members with
`Game3D.Assets3D.SetUploadBudget` and streaming counters to find frames where decoded asset commits are
followed by GPU texture upload pressure.

`FrameGpuTimeUs` is backend-owned timing telemetry. The D3D11 backend records it
with `D3D11_QUERY_TIMESTAMP` plus a disjoint query and reports the latest
completed non-disjoint sample; unsupported backends or frames without a ready
sample report `0`. The open-world perf and GPU-smoke probes include the value in
their `PERF:` / `GPU_FRAME_TIME:` lines so Windows reference runs can record CPU
frame-loop metrics and D3D11 GPU timestamp evidence together.

The CPU submission counters reset at public frame begin. `DrawsSubmitted`
counts backend mesh or instanced submissions, `AabbTransforms` exposes the
world-bounds work avoided by Canvas3D's per-frame mesh+matrix AABB cache,
`SortPasses` counts deferred stable sort stages, and `BackendStateChanges`
counts material/state runs after Canvas3D ordering when the backend exposes
stateful submission costs; the software backend reports `0`.

### Canvas3D Visibility Controls

The current visibility controls are a coarse CPU path: frustum rejection for
bounded draws, SceneGraph BVH candidate selection before Canvas3D draw sorting, and
a low-resolution screen-space coverage/depth grid for conservative occlusion
skips. SceneGraph also has an authored portal/PVS accelerator for interiors: add
visibility-zone AABBs and portal links, and `Draw` skips drawables inside
interior zones that are not reachable from the camera's current zone. These
paths are not GPU occlusion queries or Hi-Z culling.

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetFrustumCulling(enabled)` | `Void(Boolean)` | Toggle frustum rejection only |
| `SetOcclusionCulling(enabled)` | `Void(Boolean)` | Toggle frustum rejection plus conservative CPU occlusion skips |
| `DrawCount` | `Integer` | Main 3D draw submissions queued by the latest ended frame |
| `OccludedDrawCount` | `Integer` | Latest scene draw submissions skipped by visibility culling |
| `OcclusionCandidateCount` | `Integer` | Opaque draw candidates tested by the CPU occlusion grid in the latest frame |
| `TextureUploadBytes` | `Integer` | Backend texture upload bytes in the latest ended frame |
| `TextureUploadPendingBytes` | `Integer` | Backend material texture and cubemap bytes still pending upload |
| `FrameGpuTimeUs` | `Integer` | Latest completed backend GPU frame time in microseconds, or `0` when unsupported |

Use `SetFrustumCulling` when you only want off-frustum rejection. Use
`SetOcclusionCulling` for the stronger CPU visibility path; transparent draws
are never used as occluders and are not rejected by the coarse coverage grid.
`BackendSupports("hlod")` reports support for runtime-authored LOD/impostor
proxies. `BackendSupports("occlusion")` reports the CPU occlusion baseline; GPU
query/Hi-Z/portal acceleration can advertise the same capability once added.
The unit lane includes a dense covered-draw fixture that queues 65 opaque draws,
submits only the front occluder, and reports 64 occlusion skips. The SceneGraph
indexed-occlusion fixture indexes 130 drawables, narrows the CPU occlusion grid
to 2 spatial candidates before Canvas3D sorting, and submits only the front draw.
The open-world slice adds a named authored dense city/forest fixture in
`visibility_dense_probe.zia`; the local macOS software Release baseline records
169 authored drawables reduced to 49 submitted draws, 120 PVS skips, 50.407%
fill-proxy reduction, and a final-frame pixel match against the no-PVS render.

SceneGraph interior PVS is authored on the scene:

| Method / Property | Signature | Description |
|-------------------|-----------|-------------|
| `AddVisibilityZone(name, min, max)` | `Integer(String, Vec3, Vec3)` | Add a world-space interior visibility zone AABB and return its index |
| `AddVisibilityPortal(from, to, bidirectional)` | `Integer(Integer, Integer, Boolean)` | Add a directed or bidirectional visibility link between zones |
| `VisibilityZoneCount` | `Integer` | Authored zone count |
| `VisibilityPortalCount` | `Integer` | Directed portal-link count |
| `PvsCulledCount` | `Integer` | Drawables skipped by the latest portal/PVS pass |

Nodes that intersect no visibility zone remain visible, which keeps outdoor or
mixed scenes from disappearing when a PVS graph is only authored for interiors.
The count properties and PVS traversal clamp zone/portal counters to the live
allocation bounds, and appending zones or portals repairs malformed counters
before writing the next entry.

### Viper.Graphics3D.SceneGraph

Hierarchical scene graph with an implicit root node and lazily recomputed world
transforms.

| Method / Property | Signature | Description |
|-------------------|-----------|-------------|
| `Root` | `SceneNode` | Implicit scene root |
| `NodeCount` | `Integer` | Total nodes including root |
| `VisibleNodeCount` | `Integer` | Drawable mesh nodes submitted by the most recent `Draw` |
| `Add(node)` / `Remove(node)` | `Void(Object)` | Attach or detach root-level nodes |
| `TryAdd(node)` | `Boolean(Object)` | Add a node and report validation/allocation failure |
| `Find(name)` | `SceneNode(String)` | Search the scene by node name |
| `QueryAABB(min, max)` | `Seq(SceneNode)(Vec3, Vec3)` | Return visible mesh nodes whose world AABB intersects the box |
| `QuerySphere(center, radius)` | `Seq(SceneNode)(Vec3, Double)` | Return visible mesh nodes whose world AABB intersects the sphere |
| `RaycastNodes(origin, direction, maxDistance)` | `SceneNode(Vec3, Vec3, Double)` | Return the closest visible mesh node hit by the ray |
| `Draw(canvas, camera)` | `Void(Object, Object)` | Draw visible node meshes |
| `SyncBindings(dt)` | `Void(Double)` | Push physics, animation, and binding transforms |
| `RebaseOrigin(dx, dy, dz)` | `Void(Double, Double, Double)` | Shift every root-level subtree by `-delta` while leaving the root unchanged |

The query methods are backed by the SceneGraph BVH spatial index, with the
deterministic flat walk kept as the internal parity fallback. Transform-only
dirties refit the existing BVH; hierarchy, visibility, mesh, LOD, and impostor
changes rebuild it lazily. Results skip hidden subtrees and only return nodes
with their own mesh bounds. Draw culling uses the same indexed candidate set,
then runs the exact selected-LOD/impostor frustum test before submitting. The
index stores transformed world AABBs in double precision, so far-origin queries
and raycasts keep nodes distinct even when their separation is below
single-precision world-space granularity. Mesh
vertices and backend upload remain float data. Game3D floating-origin frames
opt into a Canvas3D camera-relative upload path for double-precision `DrawMesh`
model matrices, camera frame position/view translation, and point/spot light
positions. Programmatic `Mesh3D.AddVertex` keeps an internal double-position
sidecar so identity-matrix raw world-space meshes can subtract the active camera
origin before vertex upload; generated billboard paths such as standalone
`Particles3D`, `Sprite3D`, and decal meshes likewise upload camera-relative
vertices. Caller-provided float instancing data remains limited by the precision
of the data supplied to Canvas3D. Runtime tests
keep a generated 10k drawable-node grid in the normal SceneGraph ctest lane to
guard BVH shape, transform refit, isolated-query reduction, and frame-cull
candidate reduction.

`RebaseOrigin` is the low-level floating-origin primitive used by Game3D. It
keeps child-local transforms stable by moving only root-level subtrees in world
space; physics bodies, cameras, and audio listeners should be rebased through
their owning world systems as well. Query results returned before a rebase are
snapshots; run spatial queries again after the between-frame rebase boundary.

`Mesh3D.Resident` and read-only `ResidentBytes` expose mesh-payload residency
for streaming systems. Nonresident meshes keep their handles and authored data
but report zero resident bytes and are skipped by Canvas3D/SceneGraph draw paths.
SceneGraph `.vscn` save/load persists each mesh's resident flag, so authored
streaming state survives scene round trips while older files default to
resident meshes.

### Viper.Graphics3D.SceneNode LOD

`SceneNode` supports authored mesh LODs through `AddLOD(distance, mesh)`.
Entries remain sorted by distance, duplicate distances replace the previous
mesh, and `ClearLOD()` restores the base mesh at every distance. LOD selection
skips nonresident meshes and falls back to the next resident choice.

| Method / Property | Signature | Description |
|-------------------|-----------|-------------|
| `AddLOD(distance, mesh)` | `Void(Double, Object)` | Use `mesh` once camera distance reaches `distance` |
| `SetAutoLOD(enabled, screenErrorPx)` | `Void(Boolean, Double)` | Select authored LODs by projected screen size instead of distance thresholds |
| `SetImpostor(distance, pixels)` | `Void(Double, Object)` | Generate an unlit textured quad proxy used at or beyond `distance`; pass `null` pixels to clear |
| `ClearLOD()` | `Void()` | Remove authored LOD entries |
| `LodCount` | `Integer` | Number of registered LOD entries |
| `GetLodMesh(index)` | `Object(Integer)` | Borrow the mesh for an LOD entry |
| `GetLodDistance(index)` | `Double(Integer)` | Get the sorted distance threshold |
| `SetLodResident(index, resident)` | `Void(Integer, Boolean)` | Mark an LOD mesh payload resident/nonresident |
| `GetLodResident(index)` | `Boolean(Integer)` | Return whether an LOD mesh payload is resident |
| `GetLodResidentBytes(index)` | `Integer(Integer)` | Return resident bytes for an LOD mesh payload |

`SetAutoLOD` does not synthesize new meshes; it selects among meshes already
registered with `AddLOD`. A lower `screenErrorPx` keeps the base mesh longer,
while a higher value switches to lower-detail meshes sooner. `SetImpostor`
retains the supplied `Pixels` object and builds a regular textured `Mesh3D`
proxy, so it works on the same draw path as other meshes.

### Viper.Graphics3D.Camera3D

3D perspective or orthographic camera with smooth follow, orbit, and screen-to-ray projection.

**Type:** Instance (obj)
**Constructor:** `Camera3D.New(fov, near, far)`

#### Properties

| Property   | Type   | Access     | Description |
|------------|--------|------------|-------------|
| `Fov`      | Double | Read/Write | Vertical field of view in degrees |
| `Position` | Object | Read/Write | Camera position as `Vec3` |
| `Forward`  | Object | Read       | Unit forward vector as `Vec3` |
| `Right`    | Object | Read       | Unit right vector as `Vec3` |
| `IsOrtho`  | Boolean | Read      | True when camera is in orthographic projection mode |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `LookAt(pos, target, up)` | `Void(Object, Object, Object)` | Orient camera from a position toward a target using an up vector |
| `Orbit(pivot, yaw, pitch, distance)` | `Void(Object, Double, Double, Double)` | Position and orient camera on a sphere around `pivot` |
| `ScreenToRay(sx, sy, screenW, screenH)` | `Object(Integer, Integer, Integer, Integer)` | Return a normalized `Vec3` direction from a screen pixel |
| `Shake(amplitude, frequency, duration)` | `Void(Double, Double, Double)` | Start a procedural camera shake |
| `SmoothFollow(target, speed, minDist, maxDist, height)` | `Void(Object, Double, Double, Double, Double)` | Lerp toward a target with distance clamping |
| `SmoothLookAt(target, speed, roll)` | `Void(Object, Double, Double)` | Slerp the camera orientation toward a target |

Camera positions and FPS-style movement inputs are clamped to the runtime's safe world range before
view/projection matrices are generated. Non-finite position components fall back to `0.0`.
`SmoothFollow` and `SmoothLookAt` keep FPS-style yaw/pitch state synchronized with the resulting view.
For per-frame camera, listener, light, and particle update paths, reuse `Vec3`
instances with `Set(x, y, z)`, `SetX/Y/Z`, `CopyFrom(other)`, or writable
`X`/`Y`/`Z` properties before passing them to runtime setters. The runtime
setters copy component values; the pure `Vec3` arithmetic methods still return
new vectors.

```rust
bind Viper.Graphics3D.Camera3D as Camera3D;
bind Viper.Math.Vec3 as Vec3;

var cam = Camera3D.New(60.0, 0.1, 1000.0)
cam.LookAt(Vec3.New(0.0, 2.0, -5.0), Vec3.New(0.0, 0.0, 0.0), Vec3.New(0.0, 1.0, 0.0))
cam.Shake(0.3, 15.0, 0.5)
```

```basic
DIM cam AS Viper.Graphics3D.Camera3D
cam = NEW Viper.Graphics3D.Camera3D(60.0, 0.1, 1000.0)
DIM eye AS Viper.Math.Vec3 = NEW Viper.Math.Vec3(0.0, 2.0, -5.0)
DIM tgt AS Viper.Math.Vec3 = NEW Viper.Math.Vec3(0.0, 0.0, 0.0)
DIM up  AS Viper.Math.Vec3 = NEW Viper.Math.Vec3(0.0, 1.0, 0.0)
cam.LookAt(eye, tgt, up)
```

---

### Viper.Graphics3D.RenderTarget3D

Offscreen color buffer for 3D scenes, optionally in HDR format.

**Type:** Instance (obj)
**Constructor:** `RenderTarget3D.New(width, height)`

#### Properties

| Property | Type    | Access | Description |
|----------|---------|--------|-------------|
| `IsHdr`  | Boolean | Read   | True for floating-point HDR buffers |
| `Width`  | Integer | Read   | Buffer width in pixels |
| `Height` | Integer | Read   | Buffer height in pixels |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `NewHdr(width, height)` | `Object(Integer, Integer)` | Create an HDR floating-point render target |
| `AsPixels()` | `Object()` | Download the color buffer into a `Pixels` object |

GPU-backed render targets synchronize their CPU `Pixels` mirror lazily when `AsPixels()` or a
screenshot requests it. Resetting a render target or destroying its owning `Canvas3D` detaches the
backend sync callback, so later CPU readback cannot call into a stale GPU context.

---

### Viper.Graphics3D.CubeMap3D

Cubemap texture resource for environment mapping and skyboxes. Use `Canvas3D.LoadCubeMap` to load six-face cubemaps from disk. Skybox sampling uses the camera forward vector for orthographic cameras and falls back to the engine's `-Z` camera direction if that vector is degenerate; GPU smoke coverage also exercises a degenerate-basis normal-map draw.

**Type:** Instance (obj)
**Constructor:** `CubeMap3D.New(size)`

---

### Viper.Graphics3D.Material3D

Surface material used by mesh, model, instanced, terrain, water, decal, and
sprite draws.

**Type:** Instance (obj)
**Constructor:** `Material3D.New()`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `NewColor(r, g, b)` | `Object(Double, Double, Double)` | Create a diffuse-color material |
| `NewTextured(texture)` | `Object(Object)` | Create a material with a base `Pixels` or `TextureAsset3D` texture |
| `NewPBR(r, g, b)` | `Object(Double, Double, Double)` | Create a PBR material |
| `SetColor(r, g, b)` | `Void(Double, Double, Double)` | Set diffuse/base color |
| `SetTexture(texture)` / `SetAlbedoMap(texture)` | `Void(Object)` | Bind or clear the base-color texture slot |
| `SetNormalMap(texture)` | `Void(Object)` | Bind or clear a tangent-space normal map |
| `SetSpecularMap(texture)` | `Void(Object)` | Bind or clear a legacy specular map |
| `SetEmissiveMap(texture)` | `Void(Object)` | Bind or clear an emissive texture |
| `SetMetallicRoughnessMap(texture)` | `Void(Object)` | Bind or clear the packed PBR metallic-roughness map |
| `SetAOMap(texture)` | `Void(Object)` | Bind or clear the ambient-occlusion map |
| `SetEnvMap(cubemap)` | `Void(Object)` | Bind or clear an environment cubemap |

Texture map methods accept `Pixels` or `TextureAsset3D` handles with either an
active RGBA8 fallback or retained native mip blocks. KTX2 BC3, BC7, supported
ETC2 RGBA8/EAC, and ASTC LDR void-extent texture assets expose CPU fallbacks
alongside retained native mip block payloads and mip residency byte telemetry;
unsupported compressed blocks use an 8x8 magenta/black checker fallback and
record a load warning naming the format. Native-only assets draw on GPU backends
that advertise the matching compression capability and otherwise behave as
unbound textures until a fallback-capable mip is resident.
When a `TextureAsset3D` is bound, the material retains the asset and resolves
the currently resident RGBA8 mip and native block source for each draw.

#### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Color` | Object | Read | Diffuse/base color as `Vec3` |
| `Alpha` | Double | Read/Write | Material opacity |
| `Metallic` | Double | Read/Write | PBR metallic factor |
| `Roughness` | Double | Read/Write | PBR roughness factor |
| `AO` | Double | Read/Write | Ambient-occlusion factor |
| `EmissiveIntensity` | Double | Read/Write | Emissive multiplier |
| `NormalScale` | Double | Read/Write | Normal-map strength |
| `Anisotropy` | Integer | Read/Write | Texture anisotropy; `1=off`, clamps to `1..16` |
| `AlphaMode` | Integer | Read/Write | `0=opaque`, `1=mask`, `2=blend` |
| `DoubleSided` | Boolean | Read/Write | Render both triangle sides |
| `Unlit` | Boolean | Read | True when unlit shading is enabled |
| `ShadingModel` | Integer | Read | Current shading model |
| `HasTexture` | Boolean | Read | Base-color/albedo texture slot is bound |
| `HasNormalMap` | Boolean | Read | Normal-map slot is bound |
| `HasSpecularMap` | Boolean | Read | Specular-map slot is bound |
| `HasEmissiveMap` | Boolean | Read | Emissive-map slot is bound |
| `HasMetallicRoughnessMap` | Boolean | Read | PBR metallic-roughness map slot is bound |
| `HasAOMap` | Boolean | Read | Ambient-occlusion map slot is bound |
| `HasEnvMap` | Boolean | Read | Environment cubemap slot is bound |
| `Reflectivity` | Double | Read/Write | Environment reflection strength |

Texture setters accept `Pixels` handles, except `SetEnvMap`, which accepts a
`CubeMap3D`. Passing `NULL` clears the slot and immediately updates the matching
`Has*` property.

---

### Viper.Graphics3D.Light3D

Scene light with configurable color, intensity, and enabled state.

**Type:** Instance (obj)
**Constructor:** `Canvas3D.AddDirectionalLight(...)` (returned as `Light3D`)

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetIntensity(value)` | `Void(Double)` | Set light intensity multiplier |
| `SetColor(r, g, b)` | `Void(Double, Double, Double)` | Set light color using normalized RGB components |
| `SetEnabled(enabled)` | `Void(Boolean)` | Toggle contribution without clearing the light slot |
| `SetCastsShadows(enabled)` | `Void(Boolean)` | Toggle whether the light may be selected for shadow-map rendering |

#### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Type` | Integer | Read | `0=directional`, `1=point`, `2=ambient`, `3=spot` |
| `Color` | Object | Read | RGB `Vec3` |
| `Intensity` | Double | Read | Brightness multiplier |
| `Enabled` | Boolean | Read/Write | Disabled lights are skipped by rendering |
| `CastsShadows` | Boolean | Read/Write | Enabled directional lights are selected first for shadow passes; enabled spot lights may use remaining slots; point lights accept the flag but do not shadow yet; ambient lights default to false |
| `Direction` | Object | Read | Direction `Vec3` |
| `Position` | Object | Read | Position `Vec3` |

---

### Viper.Graphics3D.PostFX3D

Post-processing effect chain applied to a rendered scene.

**Type:** Instance (obj)
**Constructor:** `PostFX3D.New()`

#### Properties

| Property      | Type    | Access     | Description |
|---------------|---------|------------|-------------|
| `Enabled`     | Boolean | Read/Write | Enable or disable the entire chain |
| `EffectCount` | Integer | Read       | Number of effects currently in the chain |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddBloom(threshold, intensity, passes)` | `Void(Double, Double, Integer)` | Add a bloom bloom effect |
| `AddTonemap(mode, exposure)` | `Void(Integer, Double)` | Add tone mapping (`0 = Reinhard`, `1 = ACES`) |
| `AddFXAA()` | `Void()` | Add FXAA anti-aliasing |
| `AddColorGrade(brightnessOffset, contrast, saturation)` | `Void(Double, Double, Double)` | Add color grading. Brightness is an additive offset centered on `0.0`; contrast and saturation are multipliers centered on `1.0` |
| `AddVignette(strength, radius)` | `Void(Double, Double)` | Add a vignette darkening effect |
| `AddSSAO(radius, intensity, samples)` | `Void(Double, Double, Integer)` | Add screen-space ambient occlusion |
| `AddDOF(focusDist, focalRange, blurRadius)` | `Void(Double, Double, Double)` | Add depth of field |
| `AddMotionBlur(strength, samples)` | `Void(Double, Integer)` | Add motion blur |
| `Clear()` | `Void()` | Remove all effects from the chain |

```rust
bind Viper.Graphics3D.PostFX3D as PostFX3D;

var fx = PostFX3D.New()
fx.AddBloom(0.8, 1.2, 4)
fx.AddTonemap(1, 1.0)
fx.AddFXAA()
fx.Enabled = true
```

---

### Viper.Graphics3D.RayHit3D

Mesh raycast result returned by `Canvas3D.Raycast`. Read-only value type.

**Type:** Static (none)

#### Properties

| Property        | Type    | Access | Description |
|-----------------|---------|--------|-------------|
| `Distance`      | Double  | Read   | Euclidean distance along the ray to the hit point |
| `Point`         | Object  | Read   | World-space hit point as `Vec3` |
| `Normal`        | Object  | Read   | Surface normal at the hit point as `Vec3` |
| `TriangleIndex` | Integer | Read   | Index of the hit triangle in the mesh |

Ray queries normalize non-zero directions internally. Zero-length or non-finite directions miss, and distances remain world-unit distances even when the input direction was not normalized.

---

## Asset Loading

### Viper.Graphics3D.SceneAsset

High-level reusable model container for `.vscn`, `.fbx`, `.gltf`, `.glb`, `.obj`, and `.stl` assets. OBJ imports preserve safe relative `mtllib`/`usemtl` material groups as separate template nodes; STL imports synthesize one default-material mesh node.

#### Scene and Camera Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `get_SceneCount(model)` | `Integer(Object)` | Number of immutable scenes addressable by indexed APIs |
| `GetSceneName(model, index)` | `String(Object, Integer)` | Name for a scene index, or `""` when out of range |
| `GetCameraCount(model, sceneIndex)` | `Integer(Object, Integer)` | Number of imported cameras for a scene |
| `GetCamera(model, sceneIndex, index)` | `Object(Object, Integer, Integer)` | Imported `Camera3D`, or `null` when absent/out of range |
| `InstantiateSceneAt(model, index)` | `Object(Object, Integer)` | Clone a scene by index as a fresh `SceneGraph` |

glTF cameras are imported as standalone `Camera3D` handles with the node's world transform applied. Cached `SceneAsset` assets remain immutable: index `0` is the active/default scene, secondary glTF scene roots follow it, and invalid scene indices return zero/null rather than changing shared loader state. FBX imports preserve authored model hierarchy where available and split polygon material assignments into instantiable material-specific mesh nodes.

---

## Skeletal Animation

### Viper.Graphics3D.Skeleton3D

Bone hierarchy for skeletal mesh deformation. Typically loaded alongside a model via `SceneAsset`.

**Type:** Instance (obj)
**Constructor:** `Skeleton3D.New()`

#### Properties

| Property    | Type    | Access | Description |
|-------------|---------|--------|-------------|
| `BoneCount` | Integer | Read   | Number of bones in the skeleton |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddBone(name, parentIndex, localTransform)` | `Integer(String, Integer, Object)` | Add a named bone with a local transform and parent index |
| `ComputeInverseBind()` | `Void()` | Pre-compute inverse bind pose matrices after all bones are added |
| `FindBone(name)` | `Integer(String)` | Return the bone index, or `-1` if not found |
| `GetBoneName(index)` | `String(Integer)` | Return the name of bone at `index` |

Skinning weights are normalized consistently across CPU and GPU draw paths. Missing palettes copy
vertices through unchanged, and unused backend bone-palette slots are treated as identity transforms.
Add every bone before binding the skeleton to a mesh or constructing animation players, blenders, or
controllers; those runtime objects freeze the skeleton topology because their pose buffers are sized
from the current bone count.

---

### Viper.Graphics3D.Animation3D

Single keyframe animation track referencing a `Skeleton3D`.

**Type:** Instance (obj)
**Constructor:** `Animation3D.New(name, durationSeconds)`

#### Properties

| Property  | Type    | Access     | Description |
|-----------|---------|------------|-------------|
| `Name`    | String  | Read       | Animation name |
| `Duration`| Double  | Read       | Total duration in seconds |
| `Looping` | Boolean | Read/Write | Whether the animation loops |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddKeyframe(boneIndex, time, translation, rotation, scale)` | `Void(Integer, Double, Object, Object, Object)` | Add a keyframe for the given bone at `time` seconds; `null` TRS parts fall back to bind pose |
| `Retarget(srcSkeleton, dstSkeleton)` | `Object(Object, Object)` | Copy channels onto matching destination bones |

`Retarget` maps channels by exact bone name first, then humanoid role aliases, then matching bone
index as a fallback. It preserves clip name, duration, looping, and keyframe values, scales keyed
translations by source/destination bind-length ratio where both bones expose a comparable segment,
and leaves destination-only bones in bind pose.

---

### Viper.Graphics3D.AnimPlayer3D

Plays a single `Animation3D` track on a `Skeleton3D` with optional crossfade.

**Type:** Instance (obj)
**Constructor:** `AnimPlayer3D.New(skeleton)`

#### Properties

| Property    | Type    | Access     | Description |
|-------------|---------|------------|-------------|
| `Speed`     | Double  | Read/Write | Playback speed multiplier |
| `IsPlaying` | Boolean | Read       | True when an animation is playing |
| `Time`      | Double  | Read/Write | Current playback time in seconds |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Play(animation)` | `Void(Object)` | Start playing an `Animation3D` |
| `Crossfade(animation, duration)` | `Void(Object, Double)` | Blend into a new animation over `duration` seconds |
| `Stop()` | `Void()` | Stop the current animation and output the bind pose |
| `Update(deltaSeconds)` | `Void(Double)` | Advance the animation by the given delta |
| `GetBoneMatrix(boneIndex)` | `Object(Integer)` | Return the current global/world matrix for `boneIndex` |

Crossfades blend all bones, including channels that exist in only one clip, against bind pose. The
fading-out clip keeps its own speed and looping behavior during the transition.

```rust
bind Viper.Graphics3D.AnimPlayer3D as AnimPlayer3D;
bind Viper.Graphics3D.Animation3D as Animation3D;

var player = AnimPlayer3D.New(skeleton)
player.Play(walkAnim)
player.Speed = 1.5

// per frame
player.Update(deltaSeconds)
```

```basic
DIM player AS Viper.Graphics3D.AnimPlayer3D = NEW Viper.Graphics3D.AnimPlayer3D(skeleton)
player.Play(walkAnim)
player.Update(deltaSeconds)
```

---

### Viper.Graphics3D.AnimBlend3D

Blends multiple named animation states by weight. Useful for blend trees (run/walk, aim offsets).

**Type:** Instance (obj)
**Constructor:** `AnimBlend3D.New(skeleton)`

#### Properties

| Property     | Type    | Access | Description |
|--------------|---------|--------|-------------|
| `StateCount` | Integer | Read   | Number of animation states in the blend |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddState(name, animation)` | `Integer(String, Object)` | Add a named `Animation3D` state; returns its index |
| `SetWeight(index, weight)` | `Void(Integer, Double)` | Set normalized blend weight for state at `index` |
| `SetWeightByName(name, weight)` | `Void(String, Double)` | Set blend weight by state name |
| `GetWeight(index)` | `Double(Integer)` | Get the current weight for state at `index` |
| `SetSpeed(index, speed)` | `Void(Integer, Double)` | Set playback speed for state at `index` |
| `Update(deltaSeconds)` | `Void(Double)` | Advance all states and produce the blended pose |

`Update(0.0)` recomputes the pose without advancing time. New states inherit their animation's
looping flag unless overridden by speed/time control in code.

---

### Viper.Graphics3D.BlendTree3D

Parametric 1D/2D blendspaces over `AnimBlend3D`. `Canvas3D.DrawMeshBlended` accepts a
`BlendTree3D` anywhere it accepts an `AnimBlend3D`.

**Type:** Instance (obj)
**Constructors:** `BlendTree3D.New1D(skeleton)`, `BlendTree3D.New2D(skeleton)`

#### Properties

| Property      | Type    | Access | Description |
|---------------|---------|--------|-------------|
| `SampleCount` | Integer | Read   | Number of animation samples in the tree |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddSample(animation, x, y)` | `Integer(Object, Double, Double)` | Add an `Animation3D` sample and return its index |
| `SetParam(x, y)` | `Void(Double, Double)` | Set blend parameters and recompute weights |
| `Update(deltaSeconds)` | `Void(Double)` | Recompute weights and advance the underlying blended pose |

`New1D` linearly blends between neighboring `x` samples and clamps outside the sample range.
`New2D` uses exact-coordinate selection when possible, otherwise normalized inverse-distance
weights across registered samples.

---

### Viper.Graphics3D.IKSolver3D

Inverse-kinematics solvers for final pose adjustment before skinning.

**Type:** Instance (obj)
**Constructors:** `IKSolver3D.TwoBone(skeleton, root, mid, end)`, `IKSolver3D.LookAt(skeleton, bone)`, `IKSolver3D.FABRIK(skeleton, chain)`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetTarget(pos)` | `Void(Object)` | Set the target as a `Vec3` |
| `SetWeight(weight)` | `Void(Double)` | Blend solver output from `0.0` to `1.0`; non-finite values become zero |
| `SetPole(pos)` | `Void(Object)` | Set a world-space pole target for `TwoBone` bend-plane control |
| `SetGroundNormal(normal)` | `Void(Object)` | Set a world-space ground normal used to orient solved end-effectors |
| `Solve()` | `Void()` | Solve against the skeleton bind pose for standalone inspection |

Attach a solver through `AnimController3D.SetIKSolver(solver)` or the Game3D
wrapper `Animator3D.setIKSolver(solver)`. Controller-bound IK runs after the
base state/blend tree and overlays are composed, then before skinning palettes
are generated. `TwoBone` and `FABRIK` use a positional chain solve and preserve
the chain root; `SetPole` controls two-bone bend direction, `SetGroundNormal`
orients solved end-effectors to terrain/contact normals, and `LookAt` aims the
selected bone's local +Z axis.

---

### Viper.Graphics3D.AnimController3D

Stateful animation controller with named states, triggered transitions, animation events, root motion, and multi-layer blending.

**Type:** Instance (obj)
**Constructor:** `AnimController3D.New(skeleton)`

#### Properties

| Property           | Type    | Access | Description |
|--------------------|---------|--------|-------------|
| `CurrentState`     | String  | Read   | Name of the currently active state |
| `PreviousState`    | String  | Read   | Name of the state before the last transition |
| `IsTransitioning`  | Boolean | Read   | True while a crossfade is in progress |
| `StateCount`       | Integer | Read   | Total number of registered states |
| `RootMotionDelta`  | Object  | Read   | Accumulated root motion `Vec3` since last `ConsumeRootMotion` |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddState(name, animation)` | `Integer(String, Object)` | Register a named animation state |
| `AddTransition(from, to, duration)` | `Boolean(String, String, Double)` | Add a crossfade transition between two states |
| `Play(state)` | `Boolean(String)` | Switch immediately to the named state |
| `Crossfade(state, duration)` | `Boolean(String, Double)` | Blend into the named state over `duration` seconds |
| `Stop()` | `Void()` | Stop all animation and enter idle |
| `Update(deltaSeconds)` | `Void(Double)` | Advance the controller and produce the current pose |
| `IsStatePlaying(state)` | `Boolean(String)` | True when the named state is currently playing (active or being transitioned into) |
| `SetStateSpeed(state, speed)` | `Void(String, Double)` | Override playback speed for a state |
| `SetStateLooping(state, loop)` | `Void(String, Boolean)` | Override loop setting for a state |
| `SetAnimationLOD(distance, rateHz)` | `Void(Double, Double)` | Batch animation updates at a lower deterministic rate; non-positive inputs disable throttling |
| `SetBoneLOD(maxBones)` | `Void(Integer)` | Freeze bones at or after `maxBones` for deterministic bone-count LOD; non-positive values restore full-pose output |
| `SetBlendTree(tree)` | `Boolean(Object)` | Use a compatible `BlendTree3D` as the base pose source; pass `Nothing` to clear |
| `SetIKSolver(solver)` | `Boolean(Object)` | Apply a compatible `IKSolver3D` after overlays and before skinning; pass `Nothing` to clear |
| `AddEvent(state, time, name)` | `Void(String, Double, String)` | Register a named event to fire at a playback time |
| `PollEvent()` | `String()` | Dequeue the next fired event name, or empty string |
| `SetRootMotionBone(index)` | `Void(Integer)` | Designate a bone to extract root motion from; `-1` disables it |
| `ConsumeRootMotion()` | `Object()` | Read and clear the accumulated root motion `Vec3` |
| `SetLayerWeight(layer, weight)` | `Void(Integer, Double)` | Set the blend weight for an overlay layer |
| `SetLayerMask(layer, boneMask)` | `Void(Integer, Integer)` | Restrict a layer to a bone mask bitmask |
| `PlayLayer(layer, state)` | `Boolean(Integer, String)` | Play a state as a masked replace overlay |
| `PlayLayerAdditive(layer, state)` | `Boolean(Integer, String)` | Play a state as a true additive bind-pose delta overlay |
| `CrossfadeLayer(layer, state, duration)` | `Boolean(Integer, String, Double)` | Blend into a new masked replace state on an overlay layer |
| `CrossfadeLayerAdditive(layer, state, duration)` | `Boolean(Integer, String, Double)` | Blend into a true additive bind-pose delta state on an overlay layer |
| `StopLayer(layer)` | `Void(Integer)` | Stop the overlay layer |
| `GetBoneMatrix(boneIndex)` | `Object(Integer)` | Return the current global/world matrix for `boneIndex` |

Root motion is disabled by default, preserves loop-wrap deltas, and resets its accumulated
translation/rotation when disabled or switched to another bone. `Stop()` returns all layers to bind
pose while keeping state metadata intact. `PlayLayer` keeps the compatibility replace-overlay
behavior; `PlayLayerAdditive` applies `(overlayPose - bindPose) * weight` over the current base pose.
`CrossfadeLayerAdditive` uses the same additive composition while blending overlay states.
`SetAnimationLOD(distance, rateHz)` accumulates elapsed time and samples only when the configured
interval elapses, then applies the full accumulated delta so playback remains deterministic.
`SetBoneLOD(maxBones)` limits palette updates to the retained bone-count prefix while keeping
ancestors valid for deterministic distant-character LOD; pass `0` to restore full output.
`SetBlendTree(tree)` updates the tree with the controller tick and uses its blended local pose as
the base layer before overlay layers are applied. Root-motion extraction still comes from the
controller's state-player base layer. `SetIKSolver(solver)` requires a solver bound to the same
skeleton and applies it after overlays, before the final skin palette.

```rust
bind Viper.Graphics3D.AnimController3D as AnimController3D;

var ctrl = AnimController3D.New(skeleton)
ctrl.AddState("idle",  idleAnim)
ctrl.AddState("run",   runAnim)
ctrl.AddTransition("idle", "run",  0.2)
ctrl.AddTransition("run",  "idle", 0.3)
ctrl.AddEvent("run", 0.3, "footstepLeft")
ctrl.Play("idle")
ctrl.SetRootMotionBone(0)

// per frame
ctrl.Update(deltaSeconds)
var delta = ctrl.ConsumeRootMotion()
var evt = ctrl.PollEvent()
if evt == "footstepLeft" then
    Audio.Play(footstepSound)
end if
```

---

### Viper.Graphics3D.MorphTarget3D

Per-vertex morph target system for facial animation or shape blending.

**Type:** Instance (obj)
**Constructor:** `MorphTarget3D.New(mesh)`

#### Properties

| Property     | Type    | Access | Description |
|--------------|---------|--------|-------------|
| `ShapeCount` | Integer | Read   | Number of registered morph shapes |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddShape(name)` | `Integer(String)` | Add a named morph shape; returns its index |
| `SetDelta(shape, vertex, dx, dy, dz)` | `Void(Integer, Integer, Double, Double, Double)` | Set the position delta for a vertex in a shape |
| `SetNormalDelta(shape, vertex, dx, dy, dz)` | `Void(Integer, Integer, Double, Double, Double)` | Set the normal delta for a vertex in a shape |
| `SetWeight(index, weight)` | `Void(Integer, Double)` | Set blend weight `[-1.0–1.0]` for shape at `index` |
| `GetWeight(index)` | `Double(Integer)` | Get the current weight for a shape |
| `SetWeightByName(name, weight)` | `Void(String, Double)` | Set blend weight by shape name |

GPU backends clamp active morph shapes to shader-indexable limits and disable morphing on upload
failure instead of reusing stale buffers; larger shape sets should use the CPU-applied path.

---

## Particles And Effects

### Viper.Graphics3D.Particles3D

3D particle emitter with configurable spawn, physics, color, and render properties.
`Draw` submits one batched billboard mesh per emitter. Additive particles skip
sorting; alpha particles sort a temporary key array back-to-front without
mutating the emitter's live particle order.

**Type:** Instance (obj)
**Constructor:** `Particles3D.New(maxParticles)`

#### Properties

| Property   | Type    | Access | Description |
|------------|---------|--------|-------------|
| `Count`    | Integer | Read   | Currently active particle count |
| `Emitting` | Boolean | Read   | True while the emitter is running |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `Void(Double, Double, Double)` | Set emitter world position |
| `SetDirection(x, y, z, spread)` | `Void(Double, Double, Double, Double)` | Set emission direction and cone spread in degrees |
| `SetSpeed(min, max)` | `Void(Double, Double)` | Set spawn speed range |
| `SetLifetime(min, max)` | `Void(Double, Double)` | Set particle lifetime range in seconds |
| `SetSize(min, max)` | `Void(Double, Double)` | Set particle size range |
| `SetGravity(x, y, z)` | `Void(Double, Double, Double)` | Apply per-particle gravity vector |
| `SetColor(startColor, endColor)` | `Void(Integer, Integer)` | Set per-particle color gradient (`0xRRGGBBAA`) |
| `SetAlpha(start, end)` | `Void(Double, Double)` | Set per-particle alpha fade |
| `SetRate(particlesPerSecond)` | `Void(Double)` | Set continuous emission rate |
| `SetTexture(pixels)` | `Void(Object)` | Set the particle sprite texture |
| `SetEmitterShape(shape)` | `Void(Integer)` | Set emitter shape (`0 = point`, `1 = sphere`, `2 = box`) |
| `SetEmitterSize(x, y, z)` | `Void(Double, Double, Double)` | Set emitter volume extent for sphere/box shapes |
| `Start()` | `Void()` | Begin continuous emission |
| `Stop()` | `Void()` | Stop continuous emission |
| `Burst(count)` | `Void(Integer)` | Emit `count` particles immediately |
| `Clear()` | `Void()` | Remove all active particles |
| `RebaseOrigin(dx, dy, dz)` | `Void(Double, Double, Double)` | Shift the emitter and live particles by `-delta` |
| `Update(deltaSeconds)` | `Void(Double)` | Advance particle simulation |
| `Draw(canvas3D, camera)` | `Void(Object, Object)` | Render particles to the scene |

```rust
bind Viper.Graphics3D.Particles3D as Particles3D;

var emitter = Particles3D.New(256)
emitter.SetPosition(0.0, 1.0, 0.0)
emitter.SetDirection(0.0, 1.0, 0.0, 30.0)
emitter.SetSpeed(2.0, 6.0)
emitter.SetLifetime(0.5, 1.5)
emitter.SetColor(0xFF6622FF, 0xFF220000)
emitter.SetRate(40.0)
emitter.Start()

// per frame
emitter.Update(deltaSeconds)
emitter.Draw(canvas3d, camera)
```

---

### Viper.Graphics3D.Decal3D

Time-limited projected decal placed in a 3D scene (bullet holes, blood splats, scorch marks).

**Type:** Instance (obj)
**Constructor:** `Decal3D.New(pixels, position, normal)`

#### Properties

| Property  | Type    | Access | Description |
|-----------|---------|--------|-------------|
| `Expired` | Boolean | Read   | True when the decal lifetime has elapsed |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetLifetime(seconds)` | `Void(Double)` | Override the decal lifetime |
| `Update(deltaSeconds)` | `Void(Double)` | Advance the lifetime timer |

---

## Spatial Audio

SpatialAudio3D is part of the audio runtime, not the renderer. See
[Audio: Spatial Audio](../audio.md#spatial-audio) for the canonical
`Viper.Sound.SpatialAudio3D` API and [Audio: Mix Group Effects](../audio.md#mix-group-effects)
for group-level filters, delay, and reverb.

### Viper.Graphics3D.SoundListener3D

3D audio listener wrapper that tracks a scene node or camera position and feeds
the audio-owned spatial listener state.

**Type:** Instance (obj)
**Constructor:** `SoundListener3D.New()`

#### Properties

| Property   | Type    | Access     | Description |
|------------|---------|------------|-------------|
| `Position` | Object  | Read/Write | Listener world position as `Vec3` |
| `Forward`  | Object  | Read/Write | Listener facing direction as `Vec3` |
| `Up`       | Object  | Read/Write | Listener up direction as `Vec3`; used to derive the spatial right vector |
| `Velocity` | Object  | Read/Write | Listener velocity for Doppler as `Vec3` |
| `IsActive` | Boolean | Read/Write | Activate this listener for spatialization |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(pos)` | `Void(Object)` | Set position from a `Vec3` |
| `SetForward(dir)` | `Void(Object)` | Set facing direction from a `Vec3` |
| `SetUp(up)` | `Void(Object)` | Set up direction from a `Vec3` |
| `SetVelocity(vel)` | `Void(Object)` | Set Doppler velocity from a `Vec3` |
| `BindNode(sceneNode)` | `Void(Object)` | Automatically track a `SceneNode` position each `SpatialAudio3D.SyncBindings` call |
| `ClearNodeBinding()` | `Void()` | Remove the node binding |
| `BindCamera(camera)` | `Void(Object)` | Automatically track a `Camera3D` position and forward |
| `ClearCameraBinding()` | `Void()` | Remove the camera binding |

---

### Viper.Graphics3D.SoundSource3D

3D audio source positioned in world space, with range and Doppler support.
Sources are full-volume through `RefDistance`, attenuate linearly until
`MaxDistance`, and compute a Doppler factor from listener/source velocity. The
current mixer applies volume and pan; the Doppler factor is kept in the spatial
calculation path for playback-rate-capable backends.

**Type:** Instance (obj)
**Constructor:** `SoundSource3D.New(sound)`

#### Properties

| Property      | Type    | Access     | Description |
|---------------|---------|------------|-------------|
| `Position`    | Object  | Read/Write | Source world position as `Vec3` |
| `Velocity`    | Object  | Read/Write | Source velocity for Doppler as `Vec3` |
| `DopplerFactor` | Double | Read       | Latest computed Doppler pitch multiplier |
| `RefDistance` | Double  | Read/Write | Full-volume radius before linear falloff begins |
| `MaxDistance` | Double  | Read/Write | Attenuation roll-off distance |
| `Volume`      | Integer | Read/Write | Base volume `0–100` |
| `Looping`     | Boolean | Read/Write | True to loop the audio |
| `IsPlaying`   | Boolean | Read       | True when the source is playing |
| `VoiceId`     | Integer | Read       | Runtime voice ID of the active playback |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(pos)` | `Void(Object)` | Set position from a `Vec3` |
| `SetVelocity(vel)` | `Void(Object)` | Set Doppler velocity from a `Vec3` |
| `Play()` | `Integer()` | Start playback; returns voice ID |
| `Stop()` | `Void()` | Stop playback |
| `BindNode(sceneNode)` | `Void(Object)` | Auto-track a `SceneNode` each `SpatialAudio3D.SyncBindings` call |
| `ClearNodeBinding()` | `Void()` | Remove node binding |

```rust
bind Viper.Graphics3D.SoundSource3D as SoundSource3D;
bind Viper.Graphics3D.SoundListener3D as SoundListener3D;
bind Viper.Sound.SpatialAudio3D as SpatialAudio3D;
bind Viper.Sound.Sound as Sound;

var listener = SoundListener3D.New()
listener.IsActive = true
listener.BindCamera(cam)

var explosion = Sound.LoadAsset("assets/explosion.ogg")
var src = SoundSource3D.New(explosion)
src.MaxDistance = 40.0
src.SetPosition(Vec3.New(10.0, 0.0, 0.0))
src.Play()

// per frame
SpatialAudio3D.SyncBindings(deltaSeconds)
```

---

## Navigation

### Viper.Graphics3D.NavMesh3D

Walkable navigation mesh built from scene geometry. Used with `NavAgent3D` for pathfinding.
`Build` rejects non-manifold shared edges, where more than two triangles own the
same undirected edge, because adjacency/pathfinding would otherwise be
ambiguous.

**Type:** Static (none)
**Constructors:** `NavMesh3D.Build(mesh, agentRadius, agentHeight)`, `NavMesh3D.Bake(scene, agentRadius, agentHeight, maxSlope, cellSize)`, `NavMesh3D.BakeTiled(scene, tileSize, agentRadius, agentHeight, maxSlope, cellSize)`, `NavMesh3D.Import(path)`

#### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `TriangleCount` | Integer | Read | Number of walkable triangles in the mesh |
| `OffMeshLinkCount` | Integer | Read | Number of authored traversal links |
| `ObstacleCount` | Integer | Read | Number of authored coarse AABB obstacles |
| `LastPathCost` | Float | Read | Weighted cost of the latest successful path query |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `FindPath(start, end)` | `Object(Object, Object)` | Return a `Seq[Vec3]` of waypoints from `start` to `end`, or `Nothing` |
| `SamplePosition(pos)` | `Object(Object)` | Snap `pos` to the nearest walkable position |
| `IsWalkable(pos)` | `Boolean(Object)` | True when `pos` is on the walkable surface |
| `Export(path)` | `Boolean(String)` | Serialize the baked navmesh to a `VNAVMSH2` binary file; returns false on write failure |
| `Import(path)` | `Object(String)` | Reconstruct a path-queryable navmesh from a `VNAVMSH2` or legacy `VNAVMSH1` file, or `Nothing` on a missing/corrupt file |
| `AddOffMeshLink(from, to, bidirectional)` | `Boolean(Object, Object, Boolean)` | Add a directed or bidirectional link between walkable points |
| `SetOffMeshLinkMetadata(index, kind, cost, state)` | `Boolean(Integer, String, Double, Integer)` | Attach kind/cost/state metadata to a traversal link |
| `GetOffMeshLinkKind(index)` | `String(Integer)` | Return a traversal link kind string |
| `GetOffMeshLinkTraversalCost(index)` | `Double(Integer)` | Return a traversal link cost multiplier |
| `GetOffMeshLinkState(index)` | `Integer(Integer)` | Return traversal link state flags |
| `AddObstacle(min, max)` | `Boolean(Object, Object)` | Add a coarse AABB obstacle and re-carve affected walkable triangles |
| `RemoveObstacle(index)` | `Boolean(Integer)` | Remove a coarse obstacle and re-carve affected walkable triangles |
| `UpdateObstacle(index, min, max)` | `Boolean(Integer, Object, Object)` | Move/resize a coarse obstacle and re-carve affected walkable triangles |
| `SetArea(min, max, area, cost)` | `Boolean(Object, Object, String, Double)` | Assign area/cost metadata to polygons in a volume |
| `GetArea(pos)` | `String(Object)` | Return the area name at a walkable position |
| `GetTraversalCost(pos)` | `Double(Object)` | Return the traversal-cost multiplier at a walkable position |
| `RebuildTile(tileX, tileZ)` | `Boolean(Integer, Integer)` | Rebuild one retained tiled-bake voxel source tile |
| `SetMaxSlope(degrees)` | `Void(Double)` | Override the maximum walkable slope angle |
| `DebugDraw(canvas3D)` | `Void(Object)` | Draw the navmesh wireframe for debugging |

`Bake` flattens every `Mesh3D` attached under a `SceneGraph` through each node's
world transform and runs the voxel baker. `BakeTiled` keeps retained voxel-cell
source data for each tile; `RebuildTile(tileX, tileZ)` refreshes only that tile's
geometry, heights, and blocked state from the retained source without a
whole-scene voxel pass.

`AddOffMeshLink` is for authored traversal such as jumps, ladders, and
drop-downs. Both endpoints must be on current walkable polygons. The pathfinder
uses the link as an extra graph edge and includes the link endpoints in the
returned waypoint list. `SetOffMeshLinkMetadata` records link kind, cost
multiplier, and state flags; link cost contributes to A* and `LastPathCost`.
Shared-edge portals narrower than `agentRadius * 2` are not linked when the mesh
is built, so wider agents do not path through narrow authored passages. `SetArea`
tags polygons whose exact XZ footprint intersects a finite volume, and
`GetArea`/`GetTraversalCost` query that metadata. Polygon traversal costs weight
A* edges. `AddObstacle` stores a finite world-space AABB and removes polygons
whose triangle footprint intersects the obstacle volume. On tiled bakes, obstacle
adds/removes/updates re-carve only overlapped tiles; non-tiled meshes still
refilter the preserved source mesh. This remains polygon-level AABB carving
rather than clipped sub-polygons; `NavAgent3D` covers local crowd avoidance.
`Export` writes explicit little-endian vertices, source/current triangles,
blocked flags, traversal costs, area labels, authored off-mesh links, coarse
obstacles, and agent parameters. Import rebuilds adjacency and point-location
grids. Legacy `VNAVMSH1` imports are geometry/cost/blocked-state only. Tiled
voxel heightfield source arrays remain runtime-derived and are not serialized;
an imported tiled navmesh is queryable and editable from serialized triangles,
but it cannot regenerate new voxel heights from an external tile source.

---

### Viper.Graphics3D.NavAgent3D

Pathfinding agent that moves along a `NavMesh3D` toward a target.

**Type:** Instance (obj)
**Constructor:** `NavAgent3D.New(navMesh)`

#### Properties

| Property            | Type    | Access     | Description |
|---------------------|---------|------------|-------------|
| `Position`          | Object  | Read       | Current world position as `Vec3` |
| `Velocity`          | Object  | Read       | Current velocity as `Vec3` |
| `DesiredVelocity`   | Object  | Read       | Steering velocity before clamping |
| `HasPath`           | Boolean | Read       | True when a valid path is set |
| `RemainingDistance` | Double  | Read       | Distance remaining to the goal |
| `StoppingDistance`  | Double  | Read/Write | Distance from goal at which to stop |
| `DesiredSpeed`      | Double  | Read/Write | Maximum movement speed |
| `AutoRepath`        | Boolean | Read/Write | Automatically repath when blocked |
| `AvoidanceEnabled`  | Boolean | Read/Write | Enable same-NavMesh RVO-style steering against other enabled agents |
| `AvoidanceRadius`   | Double  | Read/Write | Local RVO radius; defaults to the agent radius and clamps to `>= 0` |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetTarget(pos)` | `Void(Object)` | Set a new goal position |
| `ClearTarget()` | `Void()` | Cancel current path |
| `Update(deltaSeconds)` | `Void(Double)` | Advance agent along the path |
| `Warp(pos)` | `Void(Object)` | Teleport the agent to a position |
| `BindCharacter(character3D)` | `Void(Object)` | Drive a `Character3D` from agent velocity |
| `BindNode(sceneNode)` | `Void(Object)` | Drive a `SceneNode` position from agent |

Avoidance is local and opt-in. Agents on the same `NavMesh3D` with `AvoidanceEnabled=true` solve a deterministic reciprocal-velocity-obstacle candidate set over nearby grid peers before the update drives a character or node. The solver predicts collisions over a bounded horizon, prefers the path-following velocity, and has a named 200-agent CTest baseline.

```rust
bind Viper.Graphics3D.NavMesh3D as NavMesh3D;
bind Viper.Graphics3D.NavAgent3D as NavAgent3D;

var nav  = NavMesh3D.Build(groundMesh, 0.5, 1.8)
var agent = NavAgent3D.New(nav)
agent.DesiredSpeed = 4.0
agent.StoppingDistance = 0.3
agent.AvoidanceEnabled = true
agent.AvoidanceRadius = 0.6
agent.SetTarget(Vec3.New(12.0, 0.0, 8.0))

// per frame
agent.Update(deltaSeconds)
```

---

## Environment

### Viper.Graphics3D.Terrain3D

Heightmap terrain with multi-layer splat texturing, LOD, and normal generation.

**Type:** Instance (obj)
**Constructor:** `Terrain3D.New(heightmapPixels, scaleX, scaleY, scaleZ)`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetHeightmap(pixels)` | `Void(Object)` | Replace the heightmap with new `Pixels` |
| `SetMaterial(material)` | `Void(Object)` | Assign a base `Material3D` |
| `SetScale(x, y, z)` | `Void(Double, Double, Double)` | Set horizontal and vertical scale |
| `GeneratePerlin(pixels, frequency, octaves, persistence)` | `Void(Object, Double, Integer, Double)` | Generate a Perlin heightmap into `pixels` |
| `SetSplatMap(pixels)` | `Void(Object)` | Set a splat map for multi-layer texture blending |
| `SetLayerTexture(layer, pixels)` | `Void(Integer, Object)` | Set the texture for splat layer `[0–3]` |
| `SetLayerScale(layer, scale)` | `Void(Integer, Double)` | Set UV tiling scale for a splat layer |
| `GetHeightAt(worldX, worldZ)` | `Double(Double, Double)` | Sample interpolated terrain height |
| `GetNormalAt(worldX, worldZ)` | `Object(Double, Double)` | Sample terrain surface normal as `Vec3` |
| `SetLODDistances(near, far)` | `Void(Double, Double)` | Set LOD transition distances |
| `SetSkirtDepth(depth)` | `Void(Double)` | Set the tile skirt depth to hide LOD seams |

Terrain splatting is enabled only when the splat map and all four layer textures are present.
Incomplete splat sets render with the base material/fallback texture.

```rust
bind Viper.Graphics3D.Terrain3D as Terrain3D;

var heightmap = Pixels.New(256, 256)
var terrain = Terrain3D.New(heightmap, 1.0, 0.25, 1.0)
terrain.GeneratePerlin(heightmap, 0.05, 6, 0.5)
terrain.SetHeightmap(heightmap)
terrain.SetSplatMap(splatPixels)
terrain.SetLayerTexture(0, grassTex)
terrain.SetLayerTexture(1, rockTex)
```

---

### Viper.Graphics3D.Water3D

Animated water plane with wave simulation, reflections, and normal mapping.

**Type:** Instance (obj)
**Constructor:** `Water3D.New(scene)`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetHeight(y)` | `Void(Double)` | Set the water plane world Y position |
| `SetWaveParams(amplitude, frequency, speed)` | `Void(Double, Double, Double)` | Configure the base wave simulation |
| `SetColor(r, g, b, a)` | `Void(Double, Double, Double, Double)` | Set base water color |
| `SetTexture(pixels)` | `Void(Object)` | Set the surface texture |
| `SetNormalMap(pixels)` | `Void(Object)` | Set the normal map for surface detail |
| `SetEnvMap(cubeMap)` | `Void(Object)` | Set the reflection `CubeMap3D` |
| `SetReflectivity(amount)` | `Void(Double)` | Set reflection strength `[0.0–1.0]` |
| `SetResolution(pixels)` | `Void(Integer)` | Set reflection render resolution |
| `AddWave(originX, originZ, amplitude, frequency, speed)` | `Void(Double, Double, Double, Double, Double)` | Add a Gerstner wave component |
| `ClearWaves()` | `Void()` | Remove all wave components |

`Water3D.Update` rewrites the retained mesh vertex buffer in place and rebuilds
index topology only when resolution or capacity changes.

---

### Viper.Graphics3D.Vegetation3D

GPU-instanced foliage (grass, bushes) with density map, wind animation, and LOD.

**Type:** Instance (obj)
**Constructor:** `Vegetation3D.New(mesh)`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetDensityMap(pixels)` | `Void(Object)` | Control placement density from a grayscale map |
| `SetWindParams(speed, strength, turbulence)` | `Void(Double, Double, Double)` | Set foliage wind sway |
| `SetLODDistances(near, far)` | `Void(Double, Double)` | Set LOD fade distances |
| `SetBladeSize(width, height, variance)` | `Void(Double, Double, Double)` | Set blade/frond dimensions |
| `Populate(terrain, count)` | `Void(Object, Integer)` | Scatter `count` instances over a `Terrain3D` using the density map |
| `Update(deltaSeconds, camX, camY, camZ)` | `Void(Double, Double, Double, Double)` | Advance wind simulation relative to camera position |

Vegetation draws use a double-sided blade material instead of mutating the
canvas-wide backface-cull flag.

---

## Misc 3D Helpers

### Viper.Graphics3D.Trigger3D

AABB trigger volume that tracks entry and exit events for `Physics3DBody` objects.

**Type:** Instance (obj)
**Constructor:** `Trigger3D.New(minX, minY, minZ, maxX, maxY, maxZ)`

#### Properties

| Property     | Type    | Access | Description |
|--------------|---------|--------|-------------|
| `EnterCount` | Integer | Read   | Number of bodies that entered this frame |
| `ExitCount`  | Integer | Read   | Number of bodies that exited this frame |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Contains(body)` | `Boolean(Object)` | True when a `Physics3DBody` is inside the volume |
| `Update(world)` | `Void(Object)` | Recompute enter/exit events against a `Physics3DWorld` |
| `SetBounds(minX, minY, minZ, maxX, maxY, maxZ)` | `Void(Double, Double, Double, Double, Double, Double)` | Resize the trigger volume |

---

### Viper.Graphics3D.Transform3D

World-space transform (position, rotation quaternion, scale) with matrix output.

**Type:** Instance (obj)
**Constructor:** `Transform3D.New()`

#### Properties

| Property   | Type   | Access     | Description |
|------------|--------|------------|-------------|
| `Position` | Object | Read       | World position as `Vec3` |
| `Rotation` | Object | Read/Write | Rotation as quaternion `Vec4(x,y,z,w)` |
| `Scale`    | Object | Read       | Scale as `Vec3` |
| `Matrix`   | Object | Read       | Combined 4×4 transform matrix as flat `Mat4` |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `Void(Double, Double, Double)` | Set world position |
| `SetEuler(pitchDeg, yawDeg, rollDeg)` | `Void(Double, Double, Double)` | Set rotation from Euler angles in degrees |
| `SetScale(x, y, z)` | `Void(Double, Double, Double)` | Set scale |
| `Translate(delta)` | `Void(Object)` | Move by a `Vec3` offset |

Very large Euler angles and incremental rotation angles are reduced before trigonometry runs, and
the stored quaternion is kept normalized. Finite zero scale is preserved; only non-finite scale
components are replaced with a safe identity value.

---

### Viper.Graphics3D.Path3D

World-space spline path for camera tracks, patrols, or procedural animation.

**Type:** Instance (obj)
**Constructor:** `Path3D.New()`

#### Properties

| Property     | Type    | Access     | Description |
|--------------|---------|------------|-------------|
| `Length`     | Double  | Read       | Total arc length of the path |
| `PointCount` | Integer | Read       | Number of control points |
| `Looping`    | Boolean | Write      | Whether the path wraps at the end |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `AddPoint(pos)` | `Void(Object)` | Append a `Vec3` control point |
| `GetPositionAt(t)` | `Object(Double)` | Interpolated `Vec3` position at normalized `t` `[0.0–1.0]` |
| `GetDirectionAt(t)` | `Object(Double)` | Tangent direction `Vec3` at normalized `t` |
| `Clear()` | `Void()` | Remove all control points |

Length sampling is capped internally, so very large point counts cannot overflow the subdivision step
calculation.

---

### Viper.Graphics3D.InstanceBatch3D

Efficient GPU-instanced rendering of many copies of the same mesh.

**Type:** Instance (obj)
**Constructor:** `InstanceBatch3D.New(mesh, material)`

#### Properties

| Property | Type    | Access | Description |
|----------|---------|--------|-------------|
| `Count`  | Integer | Read   | Current number of instances |

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(transform)` | `Void(Object)` | Append a `Transform3D` instance |
| `Remove(index)` | `Void(Integer)` | Remove instance at `index` |
| `Set(index, transform)` | `Void(Integer, Object)` | Replace the transform at `index` |
| `Clear()` | `Void()` | Remove all instances |

`Add` and `Set` require a valid runtime `Mat4` object with a complete matrix payload; foreign or
undersized objects are ignored. Stack-backed mesh fixtures can be drawn through the instanced path
when used by runtime systems. Draw submission sanitizes material scalars before narrowing to backend
floats. Raw instanced draw submission retains mesh/material objects for the deferred frame and
synthesizes previous instance matrices on the GPU path when explicit previous matrices are not
supplied. Motion history is separated by batch buffer identity, so keep the same instance-matrix
buffer across frames when continuous motion vectors are desired.

---

### Viper.Graphics3D.Sprite3D

Billboard sprite placed in 3D world space, suitable for particles, UI labels, or 2D-in-3D overlays.

**Type:** Instance (obj)
**Constructor:** `Sprite3D.New(pixels)`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `SetPosition(x, y, z)` | `Void(Double, Double, Double)` | Set world position |
| `SetScale(w, h)` | `Void(Double, Double)` | Set billboard scale in world units |
| `SetAnchor(ax, ay)` | `Void(Double, Double)` | Set anchor offset `[0.0–1.0]` |
| `SetFrame(x, y, width, height)` | `Void(Integer, Integer, Integer, Integer)` | Set the source pixel region |
| `RebaseOrigin(dx, dy, dz)` | `Void(Double, Double, Double)` | Shift the sprite position by `-delta` |

---

### Viper.Graphics3D.TextureAtlas3D

Texture atlas for 3D rendering with named-region management.

**Type:** Instance (obj)
**Constructor:** `TextureAtlas3D.New(width, height)`

#### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(pixels)` | `Integer(Object)` | Pack a `Pixels` image into the atlas; returns region index |
| `GetTexture()` | `Object()` | Return the backing `Pixels` buffer |

---

## Notes

- `Transform3D` is distinct from `SceneNode` — use `Transform3D` for standalone matrix math and non-scene-graph transforms; attach nodes to the scene for scene-managed transform hierarchies.
- `AnimController3D.PollEvent` returns events one at a time per call; poll it in a loop until an empty string is returned if multiple events fire in one update.
- `NavMesh3D` direct meshes are rebuilt by `NavMesh3D.Build`; `BakeTiled`
  retains voxel source data so `RebuildTile` can refresh one tile's geometry,
  while tiled obstacle edits re-carve only affected tiles. Keep baked meshes
  manifold at shared edges.
- `Particles3D.Draw` should be called inside the `Canvas3D.Begin`/`End` scene pass after opaque geometry when you want particles over the main scene.
- Deferred heap `Mesh3D` draws snapshot geometry when needed so submitted geometry remains stable through `Canvas3D.End()`; public `DrawMesh` rejects raw stack mesh payloads, while internal skinned and morphed paths retain or snapshot the animation payloads needed for backend submission.
- `SpatialAudio3D.SyncBindings` must be called once per frame after physics/animation updates so bound sources and listeners track their nodes.
