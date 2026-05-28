# Plan 03 — Stop re-uploading frame-constant scene/light data on every draw call

- **Severity:** High (performance) — largest CPU-side draw-call win in the audit
- **Type:** Optimization (behavior-preserving, content-gated)
- **Primary files:** `vgfx3d_backend_d3d11.c`, `vgfx3d_backend_opengl.c`, `vgfx3d_backend_metal.m`
- **Status:** Planned (do not code yet)

## Problem

All three GPU backends re-pack and re-push **frame-constant** scene + light data for *every* mesh
draw, even though the camera/VP/fog/shadow state changes only once per frame (`begin_frame`) and the
light set is almost always identical across a frame's draws:

- **D3D11** (`d3d11_submit_draw`, `vgfx3d_backend_d3d11.c:4022-4041`): every draw calls
  `d3d11_prepare_scene_data` + `Map(cb_per_scene)` and `d3d11_prepare_light_data` +
  `Map(cb_per_lights)`. Each `Map`/`Unmap` (DISCARD) is a per-draw CPU/driver sync point.
  `d3d11_prepare_scene_data` (`3450-3481`) reads **only** `ctx->vp/draw_prev_vp/shadow_vp/cam_pos/
  cam_forward/fog_*/shadow_bias/shadow_count` + the passed `lights`/`ambient` — provably frame-derived.
- **OpenGL** (`upload_main_uniforms`, `vgfx3d_backend_opengl.c:3666-3743` + `upload_light_uniforms`
  `3619-3645`): ~20 `glUniform*` calls per draw for VP/prev-VP/camera/ambient/fog/shadow-count/all
  `VGFX3D_MAX_SHADOW_LIGHTS` shadow-VP matrices, plus a `glUniform*` storm for the whole light array.
- **Metal** (`metal_submit_draw`, `vgfx3d_backend_metal.m:3134-3135, 3307`): re-`setFragmentBytes`
  the scene struct and the packed light array per draw (after re-packing on the CPU).

**Impact:** scales with draw count. A 500-mesh scene pays 500× redundant scene/light packing +
uploads. On D3D11 the per-draw `Map`/`Unmap` is the costliest; on GL the `glUniform` call volume;
on Metal the CPU re-pack.

## Investigation notes / nuances (read carefully — this drives correctness)

- **The gate MUST be content-based, not a per-frame "uploaded once" flag.** Canvas3D's deferred
  queue snapshots `lights`/`light_count` **per command**: `canvas3d_apply_shadow_light_params(
  cmds[i].lights, cmds[i].light_count, …)` (`rt_canvas3d.c:3826`), and the comment at
  `rt_canvas3d.c:1664` explicitly warns caller-side lights/ambient can change between enqueue and
  flush. So different draws in one frame *can* legitimately carry different light sets (and
  `light_count`). A blind "skip after first draw" gate would render later draws with stale lights.
- **Therefore:** cache a signature of the inputs that feed scene+light cbuffers and re-upload only
  when the signature changes. In the common case (all draws share the scene lights) the signature is
  constant → we upload once and skip the rest. When it differs → we re-upload (correctness preserved).
- The frame-constant *camera/VP/fog/shadow* part (everything in `d3d_per_scene_t` except
  `light_count`) never varies within a frame, but it's cheapest to fold the whole thing into one
  signature comparison rather than split cbuffers.
- **State persistence differs per API** (affects where "skip" is safe):
  - *D3D11:* `cb_per_scene`/`cb_per_lights` are bound once (in `d3d11_bind_main_pipeline`); only their
    *contents* are re-`Map`ped per draw. Skipping the `Map` when unchanged is safe — the bound buffer
    still holds last frame-correct contents.
  - *OpenGL:* uniforms persist in the program object until changed. Uploading once and skipping is
    safe as long as the program isn't relinked mid-frame (it isn't).
  - *Metal:* `setBytes` bindings persist within a *render command encoder* but **not across encoders**
    (a new pass/encoder resets them). So the Metal gate must be invalidated whenever the encoder is
    (re)created, not merely per frame.
- `ctx->frame_serial` already exists in the D3D11 (`4915`) and GL (`4572`) contexts — reuse it.

## Proposed fix

Introduce a small per-context "last uploaded scene/light signature" and gate the per-draw upload.

**Shared approach (all three backends):**
1. Add to each `ctx`: a cached copy of the packed scene struct + light array (or a compact signature)
   and a `scene_light_valid` flag (+ for Metal, the encoder generation it was bound under).
2. In `submit_draw`, after packing `scene_data`/`light_data` into **stack** locals (as today), compare
   against the cached copy with `memcmp` (the structs are small — `d3d_per_scene_t` + up to
   `VGFX3D_MAX_LIGHTS` lights). Prefer `memcmp` over a hash to eliminate collision risk.
3. If equal **and** valid (and, Metal: same encoder) → skip the upload/`glUniform`/`setBytes`.
   Else → upload, copy into the cache, set valid, record encoder gen.
4. Invalidate (`scene_light_valid = 0`) in `begin_frame` (D3D11/GL) and on encoder (re)creation
   (Metal), so the first draw of each frame/encoder always uploads.

**Per backend:**
- *D3D11:* gate the two `d3d11_update_constant_buffer(cb_per_scene/cb_per_lights)` calls in
  `d3d11_submit_draw` (and the instanced path `d3d11_submit_draw_instanced`). Keep the `VSSet/PSSet
  ConstantBuffers` bindings (cheap, and needed after pipeline rebinds) or also cache those (see
  Plan 07).
- *OpenGL:* split `upload_main_uniforms` into per-object (model/prev-model/normal — always uploaded)
  and frame-constant (VP/prev-VP/camera/ambient/fog/shadow-VPs/shadow-count + `upload_light_uniforms`)
  groups; gate the frame-constant group on the signature. (Subsumes Plan 10's shadow-VP fix.)
- *Metal:* gate the scene `setFragmentBytes`/`setVertexBytes` and the light `setFragmentBytes` on the
  signature + encoder generation; skip the CPU re-pack too.

## Files to modify

- `src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11.c` (+ its ctx struct)
- `src/runtime/graphics/3d/backend/vgfx3d_backend_opengl.c` (+ its ctx struct)
- `src/runtime/graphics/3d/backend/vgfx3d_backend_metal.m` (+ its ctx struct)
- No shared-header or vtable change required (the gate is internal per backend).

## Tests

- **Correctness:** existing `g3d_*` graphics3d probes must be unchanged. Add/extend a probe that
  draws ≥2 meshes with **different light sets in the same frame** (e.g. one lit, one with an extra
  point light) to prove the content-gate re-uploads correctly and doesn't bleed lights between draws.
- **Perf:** extend a `game3d` probe to spawn 300–500 meshes; record CPU frame time (or draw-submit
  time) before/after per backend on its platform.
- Validate each backend on its own OS (D3D11/Windows, Metal/macOS, OpenGL/Linux). The software path
  is unaffected.

## Risk

Medium. The correctness hazard is the per-command light variation (handled by content comparison) and
Metal's per-encoder binding lifetime (handled by encoder-gen invalidation). Recommend landing
**D3D11 first** (biggest, clearest win via `Map` elision), then OpenGL, then Metal.

## Sequencing

Do after the low-risk SW/shared fixes (Plans 01,02,04,05,06,08,09). Plan 10 folds into the OpenGL
half of this change.
