# 05 — Engine: View-Model Pass, Additive Sprites, Zero-Copy RT, SW Instancing, Portal Frustum PVS

> **STATUS: IMPLEMENTED (2026-07-07)** · Baseline `3166d1dc2` · Track E.
> Shipped: E18 `Canvas3D.BeginViewModel(camera, fovY)` — implemented as a secondary pass
> (`load_existing_color=1`, fresh depth) rather than the planned per-backend depth-range
> remap: identical guarantees (never clips walls, self-occludes, independent FOV via
> projection patch, receives world shadow maps, casts none, skips skybox) with ZERO
> backend-specific code — one shared implementation on all four backends. E19
> `Sprite3D.Additive` prop + `SetColor(0xRRGGBB)` tint riding the existing
> material `additive_blend` path (Particles3D recipe). E20 RT→material:
> `Material3D.SetAlbedoRenderTarget/ClearAlbedoRenderTarget/SetEmissiveRenderTarget` —
> RenderTarget3D became a first-class material texture source via a generation-tracked
> Pixels mirror (`content_revision` bumps at frame end; mirror refreshes only on real
> content change; self-bind is inherently safe: mid-frame draws sample the previous
> completed frame). Uniform across backends; native GPU-texture bind noted as a future
> Metal-first optimization. E21 `RenderTarget3D.CopyTo(pixels)` allocation-free readback
> (exact trap `"RenderTarget3D.CopyTo: size mismatch"`; bumps Pixels generation for GPU
> caches). E22 software `submit_draw_instanced` hook — opaque batches never fall back on
> ANY backend; the blend/rebase fallback trap became clamp+telemetry
> (`Canvas3D.InstancedFallbackCount`); capability split: `"instancing"` = hook present
> (now true on software), `"hardware_instancing"` = GPU+hook. E23 portal-frustum PVS —
> portals propagate through projected NDC windows (portal frame = interval box between
> the linked zone AABBs; near-plane straddle → conservative full view) with per-zone
> window unions run to fixpoint; `SceneGraph.PortalClipping` toggle (default on) +
> `get_PortalTraversalCount`; discriminating test: a frustum-visible zone reachable only
> through a behind-camera portal culls with clipping on, draws with it off.
> **Bugs found & fixed along the way:** BUG-E6 (Metal RTT frame end double-ended the
> command encoder — crashed every windowed `SetRenderTarget` use) and BUG-E7 (Metal
> `PerMaterial` C/MSL layout mismatch — ALL textured sampling on Metal read shifted
> garbage UV transforms; latent under uniform/palette textures). See ENGINE_BUGS_FOUND.md.
> Coverage: `tests/runtime/test_canvas3d_viewmodel_sprite.zia` (behind-wall view-model
> draw, FOV magnification, additive sum-to-white, tint, CopyTo==AsPixels) and
> `test_canvas3d_renderer_upgrades.zia` (non-uniform-texture texel asserts, live RT
> material rebind-free refresh, 70k-instance batch with fallback count 0, portal-PVS
> discriminator + corridor no-over-cull) — both PASS on Metal and software;
> `-L graphics3d` 95/95; runtime completeness green. Docs: rendering3d.md (view-model
> pass section, Sprite3D, RenderTarget3D CopyTo, Material3D RT bindings, PVS clipping,
> InstancedFallbackCount, instancing capability split).
> Eliminates constraints #10, #12, #13, #14, #15. Consumers: 12-core-loop/13-weapons
> (view-model), 20-levels-act2 (L4 monitors + PVS), 13/14 weapons FX (additive sprites),
> 22-world-systems (instanced debris/foliage on SW).

## 0. TL;DR

Five renderer upgrades that FPS presentation needs: a **camera-space view-model pass** with its
own FOV and depth-range remap (weapon never clips walls, world FOV stays independent);
**additive blending on `Sprite3D`** (muzzle glows, tracers, EMP arcs); **zero-copy
render-target→material binding** (live security monitors, rail scope inset) plus an
allocation-free `CopyTo`; a **real instanced path in the software backend** (removes the
per-instance fallback and its trap cap); and **per-portal frustum clipping** so indoor PVS
culls tightly instead of conservatively.

## 1. Current state (verified anchors)

- No view-model support: nothing in `rt_canvas3d.c`/backends draws with a per-draw projection
  override or depth-range remap; games hand-roll near-camera transforms (clips geometry, shares
  world FOV).
- `Sprite3D` surface = New/SetPosition/SetScale/SetAnchor/SetFrame/RebaseOrigin + DrawSprite3D
  (runtime.def:16292-16305) — **no blend control**; `Particles3D` has `Additive` (RT_PROP,
  Particles3D class block).
- `RenderTarget3D.AsPixels` returns a **fresh Pixels copy every call** (`rt_rendertarget3d.c:19,329`);
  no way to bind an RT's color as a material texture — monitors require GPU→CPU→GPU round trips.
- SW instancing: no `submit_draw_instanced` hook in the SW vtable (`vgfx3d_backend_sw.c:549-556`);
  dispatch falls back to per-instance queued draws (`rt_canvas3d_instanced.inc:72-98,428-430`)
  and **traps** above `CANVAS3D_MAX_FALLBACK_INSTANCES` (`:431`).
- PVS: portal-reachability BFS marks every zone reachable through any portal — no frustum
  narrowing (`rt_scene3d_helpers.inc:1775-1825`; zone/portal structs
  `rt_scene3d_internal.h:132-240`).

## 2. Design

### E18 — View-model pass
```text
Viper.Graphics3D.Canvas3D.BeginViewModelPass(f64,f64,f64)  void(obj,f64,f64,f64)
    — (fovYDegrees, depthMin, depthMax): subsequent DrawMesh/DrawMeshSkinned calls render
      with a dedicated projection (own FOV, canvas aspect, near 0.01) and their depth output
      remapped into [depthMin,depthMax] (typical 0.0–0.05) so they always pass the depth test
      against world geometry yet self-occlude correctly.
Viper.Graphics3D.Canvas3D.EndViewModelPass()               void(obj)
```
- Implementation: draws inside the pass are tagged; backends set viewport depth range
  (Metal `MTLViewport.znear/zfar`, D3D11 `D3D11_VIEWPORT.MinDepth/MaxDepth`, GL
  `glDepthRangef`) and swap the projection constant — **no shader changes needed** on GPU
  backends. Software: raster path scales interpolated depth into the remap window.
  Pass renders **after opaque world, before transparents/particles** so world post-FX
  (SSAO etc.) doesn't smear the weapon; lighting uses the world light set at camera position.
- Constraints documented: shadows are not cast by view-model draws (tag skips shadow pass);
  motion vectors use the pass projection (TAA-safe).

### E19 — Sprite3D additive
`Sprite3D.set_Additive(i1)` → sprite draw command carries blend mode; backends select the
existing additive blend state already used by `Particles3D` (all four have it). Also add
`Sprite3D.SetColor(i64)` tint (RGBA) — muzzle glow needs per-flash color; verified absent today.

### E20 — Zero-copy RT→material
```text
Viper.Graphics3D.Material3D.SetAlbedoRenderTarget(obj)  void(obj,obj)  — bind RT color as albedo
Viper.Graphics3D.Material3D.ClearAlbedoRenderTarget()   void(obj)
```
- GPU backends: material samples the RT's native color texture (Metal `id<MTLTexture>`,
  D3D11 SRV, GL texture id) — zero copies. Hazard rule: binding the **currently bound** RT as
  a source is refused per draw (validation → skip draw + `AssetDiagnostics3D` warning), matching
  D3D11 simultaneous-bind rules; games render monitors first, then the main scene.
- Software: material samples the RT's CPU buffer directly (shared pointer, no copy).
- Emissive variant `SetEmissiveRenderTarget` included (monitors glow in dark rooms).

### E21 — `RenderTarget3D.CopyTo(pixels)` `void(obj,obj)`
Reuses caller's Pixels (dims must match, else trap with exact message
`"RenderTarget3D.CopyTo: size mismatch"`); no allocation; same readback path as AsPixels.

### E22 — Software instancing
Add `submit_draw_instanced` to the SW vtable: iterate instances in the backend inner loop
(transform reuse, no per-instance command overhead), honoring the same instance data layout
(`backend.h` instanced payload). Remove the fallback trap: `CANVAS3D_MAX_FALLBACK_INSTANCES`
becomes a soft telemetry counter (`get_InstancedFallbackCount`) — blended-material and rebase
cases still fall back but never trap. Capability `"instancing"` becomes true on SW (it now has
the hook); `"hardware_instancing"` stays GPU-only (renamed semantics documented).

### E23 — Portal-frustum PVS
Replace the flat BFS with recursive portal traversal: starting from the camera zone with the
camera frustum, for each portal in the current zone visible against the **current** frustum,
compute the narrowed frustum through the portal quad (clip frustum planes to the portal's
screen-space bounds — classic portal culling) and recurse into the connected zone with it.
Zones collect the union of narrowed frusta for draw culling. Depth cap 8, degenerate-portal
guard (min area), fallback to old BFS via `SceneGraph.SetPortalClipping(i1)` (default on).
Telemetry: existing `get_PvsCulledCount` now reflects tighter culling; add
`get_PortalTraversalCount`.

## 3. Files

| File | Change |
|---|---|
| `src/runtime/graphics/3d/render/rt_canvas3d.c` + `_internal.h` | view-model pass state/tagging, CopyTo, fallback counter |
| `src/runtime/graphics/3d/render/rt_canvas3d_instanced.inc` | SW hook dispatch, trap removal |
| `src/runtime/graphics/3d/render/rt_sprite3d.c` (locate exact) | additive flag, tint |
| `src/runtime/graphics/3d/render/rt_rendertarget3d.c` | CopyTo, native-texture accessor for materials |
| `src/runtime/graphics/3d/render/rt_material3d.c` | RT albedo/emissive slots + hazard validation |
| `src/runtime/graphics/3d/scene/rt_scene3d_helpers.inc` | portal-frustum traversal |
| `src/runtime/graphics/3d/backend/vgfx3d_backend_sw*.c/.inc` | instanced hook, depth-remap raster, RT sampling |
| `backend_metal.m` / `backend_d3d11.c` / `backend_opengl.c` | viewport depth range + projection swap, sprite blend state, RT texture bind |
| `src/il/runtime/runtime.def`, `docs/viperlib/graphics/rendering3d.md` | surface + docs |

## 4. Tests

1. View-model golden (SW): weapon box at z=0.3 inside a wall at z=0.2 → weapon fully visible,
   world unaffected; FOV 54 pass over FOV 90 world → projection assert via golden.
   Depth-remap unit: remapped depths all < world min depth.
2. Sprite additive golden: two overlapping additive sprites sum toward white; tint applies.
3. RT→material: render red triangle to RT, bind as albedo on a quad, render main scene →
   quad shows triangle (SW golden + Metal manual); self-bind refused with warning;
   monitor loop 60 frames → zero Pixels allocations (allocation counter probe).
4. CopyTo: dims mismatch traps with exact message; matched dims → identical bytes to AsPixels.
5. SW instancing: 10,000-instance batch renders (was trap) — golden matches 10k individual
   draws reference at reduced size; draw-submission counter shows 1; fallback counter 0.
6. Portal clipping: corridor of 4 zones in a line, camera looking down it → all visible;
   camera rotated 90° → only current zone visible (`PvsCulledCount` rises); portal behind
   camera culls its subtree. Toggle off restores BFS results (regression compat).
7. Existing `test_rt_scene3d.cpp` zone tests stay green.

## 5. Verification gate

`ctest -R 'canvas3d|scene3d|rendertarget|sprite'` + `-L graphics3d` → goldens committed →
runtime completeness + surface audits + ADR → full no-skip build → Metal manual pass:
view-model weapon over L4-style corridor with monitors live at 60 FPS, no allocation growth
over 5 minutes (Instruments spot-check). GL/D3D11 paths compile-clean via lint; exercised in
the P4/P12 platform lanes.
