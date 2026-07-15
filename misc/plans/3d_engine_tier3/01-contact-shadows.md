# Plan 01 — Screen-Space Contact Shadows

Short depth-buffer raymarch toward the primary directional light to ground
characters and props where shadow-map resolution fails. Cheap (8–16 taps),
works on every backend, and slots into the existing PostFX snapshot pattern
exactly like SSAO.

## Public API

- `PostFX3D.AddContactShadows(intensity: Float, distance: Float, steps: Integer)`
  - `intensity` 0..1 (shadow darkening scale, sanitize default 0.6)
  - `distance` world-space march length (default 0.35, clamp 0.01..4.0)
  - `steps` 4..32 (default 12)
- Def row in `src/il/runtime/defs/graphics3d/rendering.def`:
  `RT_FUNC(PostFX3DAddContactShadows, rt_postfx3d_add_contact_shadows, "Viper.Graphics3D.PostFX3D.AddContactShadows", "void(obj,f64,f64,i64)")`
  plus `RT_METHOD("AddContactShadows", "void(f64,f64,i64)", PostFX3DAddContactShadows)`
  in the PostFX3D class block.
- Trap stub in `src/runtime/graphics/common/rt_canvas3d_stubs.c` (the
  stub-parity audit in `check_runtime_completeness.sh` fails the build if
  forgotten). Manifest counts + hash in
  `src/tests/unit/test_graphics3d_runtime_manifest.cpp` need the deliberate
  review bump.

## Runtime plumbing (rt_postfx3d)

1. `rt_postfx3d.h`: append `VGFX3D_POSTFX_EFFECT_CONTACT_SHADOWS` to the enum
   (append-only — backends switch on raw values). Append snapshot fields
   `int8_t contact_shadows_enabled; float cs_intensity, cs_distance; int32_t cs_steps;`
   (ABI-stable tail extension, same as TAA/SSR did).
2. `rt_postfx3d.c`:
   - params union member `struct { float intensity, distance; int32_t steps; } contact_shadows;`
   - `rt_postfx3d_add_contact_shadows` with `sanitize_range_f32` clamps
     (mirror `rt_postfx3d_add_vignette`).
   - snapshot fill in the effect switch (~line 629 region).
   - depth-requirement gate: add the new enum to the "needs scene depth"
     check (~line 688) so `Canvas3D.SetPostFX` refuses it on no-depth targets
     with the documented recoverable error.
3. **Sun direction**: extend the postfx param fill in each backend draw path
   with the canvas's primary directional light direction (already tracked for
   shadow cascades — `rt_canvas3d_lighting.c` keeps the primary light; pass it
   through the same route SSAO gets its projection matrices). Add
   `float sun_dir_ws[4]` to each backend's postfx param struct (Metal
   `mtl_postfx_params_t` + MSL string, GL uniforms, D3D11 cbuffer) — keep the
   16-byte tail alignment note in `vgfx3d_backend_metal_draw.inc` satisfied.

## Shader march (all four backends)

Per pixel with depth < 1 (something rendered):

```
world  = reconstructWorld(uv, depth)            // exists for SSAO/SSR
step   = sunDirWS * (cs_distance / cs_steps)
occl   = 0
for i in 1..cs_steps:
    p      = world + step * i
    clip   = viewProjection * p
    suv    = clipToUv(clip)                     // per-backend Y convention!
    if suv outside [0,1]: break
    sceneD = depth sample at suv
    rayD   = clip depth of p (backend depth convention)
    if sceneD < rayD - bias: { occl = 1; break }   // something closer: blocked
color *= 1 - cs_intensity * occl
```

- Bias: `0.02 * cs_distance` in view depth; reversed-Z scenes flip the
  comparison — reuse the SSR depth-compare helper which already handles it.
- Metal/D3D11 sample with top-left UV convention, GL bottom-left — use each
  backend's existing `ndcDeltaToUvDelta`/`uvToNdc` helpers rather than new
  math (this is exactly the class of bug the motion-vector fix repaired).
- Software backend: implement in the CPU postfx parity path
  (`rt_postfx3d.c` ~line 1395, alongside SSAO) using the retained depth
  buffer; the march is identical, scalar.

## Tests / acceptance

- Unit: snapshot fill + sanitize clamps + depth-gate refusal (extend
  `test_rt_canvas3d.cpp` postfx section).
- Conformance: add `"contact"` mode to
  `src/tests/graphics_conformance/conformance_scene.zia` (crate close above
  ground, low sun so the crate base should darken); run
  `scripts/run_backend_conformance.sh` — SW golden vs Metal must pass the
  existing tolerance; GL/D3D11 on their machines.
- Visual sanity: bowling demo title scene with contact shadows on, screenshot
  compare against SW.
