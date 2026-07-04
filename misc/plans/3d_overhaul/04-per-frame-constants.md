# Plan 04 — Per-Frame Constants: Stop Re-Uploading Scene/Light Data Per Draw

> **Status (2026-07-03): IMPLEMENTED with a design improvement.** The planned
> `set_frame_lights` vtable hook was replaced by **light-snapshot revision
> stamps** after implementation-time analysis showed deferred draws carry
> per-draw light snapshots (mid-frame light and Light3D-property mutations are
> legal), which a once-per-frame hook would silently break. The shipped design:
> the canvas compares each queued draw's flattened light set + ambient against
> the previous snapshot (entries are memset-padded, so memcmp is deterministic)
> and stamps `vgfx3d_draw_cmd_t.lights_revision`, advancing only on real
> change; the shadow-pass patch re-stamps uniformly with distinctness
> preserved. Backends skip constant re-upload while consecutive draws share a
> stamp: OpenGL skips the 64-light glUniform storm (uniforms persist in the
> program object), Metal skips rebuilding + re-encoding the PerScene and light
> blocks (encoder argument state persists; cache reset on encoder recreation),
> and D3D11 skips the cbPerScene/cbPerLights maps (WRITE_DISCARD renaming keeps
> earlier in-flight draws consistent). Semantics verified by a mock-backend
> test: identical light sets share one nonzero stamp across draws and frames,
> and a mid-frame `Light3D.SetIntensity` advances it (243/243 canvas3d,
> 161/161 production, 88/88 graphics3d).

## 1. Objective & scope

Every GPU backend currently re-uploads all frame-constant data (camera, fog, shadow matrices, and the **entire 64-light array**) on **every** `submit_draw`. In a 500-draw frame that's 500× redundant uploads of ~5 KB of light data plus redundant uniform traffic — pure CPU/driver overhead scaling with draw count. Restructure so frame-constant data uploads once per frame in `begin_frame`, leaving only per-object/per-material data per draw.

**In scope:** D3D11, OpenGL, Metal upload restructuring; a per-frame dirty-tracking contract in the canvas layer; before/after measurements. **Out of scope:** clustered light culling (plan 07 — builds on this), instancing changes, software backend (no "upload" concept — already reads canvas state directly).

**Zero external dependencies:** pure restructuring of existing from-scratch backend code; no graphics libraries, no helper SDKs.

## 2. Current state (verified anchors)

- Lights travel **per draw call** as arguments: `submit_draw(ctx, win, cmd, const vgfx3d_light_params_t *lights, int32_t light_count, const float *ambient, wireframe, backface_cull)` (`backend/vgfx3d_backend.h:288-393`); same for `submit_draw_instanced` (`:328-337`). Camera/fog go through `begin_frame(ctx, const vgfx3d_camera_params_t *cam)` (`:218-232`) — already per-frame — but backends still re-push derived uniforms per draw.
- **OpenGL:** `gl_submit_draw` (`_opengl_frame.inc:200`) → `upload_main_uniforms` (`_opengl_material.inc:242-344`) re-issues `glUniform*` for ALL scene state per draw: `uViewProjection/uPrevViewProjection/uCameraPos/uCameraForward/uAmbientColor/uFog*/uShadowCount/uShadowVP[]/uShadowBias` plus the full `uLight*[]` arrays via `upload_light_uniforms`. The only UBOs are the `Bones`/`PrevBones` std140 blocks. Values are cached on ctx (`ctx->vp`, `ctx->shadow_vp[]`) in `begin_frame` but re-sent every draw.
- **D3D11:** `d3d11_submit_draw` (`_d3d11_draw.inc:992`) maps and refills four cbuffers per draw (`:1027-1055`): `PerObject`(b0), `PerScene`(b1), `PerMaterial`(b2), `PerLights`(b3) via `d3d11_update_constant_buffer` with `d3d11_prepare_{object,scene,light,material}_data`; `d3d11_prepare_light_data` **memsets the full 64-light array** (`:468-475`) each time. The instanced path repeats all of it (`:1136-1170`). cbuffer layouts: `_d3d11_shaders.inc:23-97`.
- **Metal:** `PerObject/PerScene/PerMaterial/PerLights` structs (`vgfx3d_backend_metal.m:283+`), populated and bound per draw (PerObject buffer(1), PerScene buffer(2)); frame history `prev_vp` on ctx (`:1074`).
- Light source of truth: the canvas flattens scene lights once per frame (`rt_canvas3d_lighting.c`, storage `rt_canvas3d_internal.h:826-831`) — the data handed to `submit_draw` is **already frame-stable**; per-draw only the count can be clipped by `canvas3d_active_light_limit`. Shadow VPs are frame-stable after the shadow pass (`shadow_light_vps[4][16]`, `internal.h:890-899`).
- Existing perf counters to measure with: Canvas3D diagnostics props (draw calls, upload bytes, mesh-cache hits — surfaced on `Viper.Graphics3D.Canvas3D` and `Game3D.Diagnostics3D`).

## 3. Design

### 3.1 Vtable contract change (minimal, additive)

Add one optional hook rather than changing `submit_draw`'s signature (keeps SW backend and any out-of-tree callers untouched):

```c
/* Optional. Called once per frame after the shadow pass, before main-pass draws.
 * Backends that implement it will ignore the per-draw lights/ambient args
 * (which continue to be passed for backward compatibility). */
void (*set_frame_lights)(void *ctx, const vgfx3d_light_params_t *lights,
                         int32_t light_count, const float *ambient,
                         const float *shadow_vps /*[4][16]*/, float shadow_bias);
```

Canvas calls it from the main-pass setup in `render/rt_canvas3d_render_pass.inc` (after `canvas3d_render_shadow_pass`, before the draw loop) when non-NULL. The per-draw `lights/ambient` args stay in the signature — backends with the hook simply don't re-read them. This is the same optional-hook pattern the vtable already uses for shadows/skybox/instancing (`backend.h:316-337` — NULL hook ⇒ fallback behavior).

### 3.2 Per-backend implementation

- **D3D11:** move `d3d11_prepare_scene_data` + `d3d11_prepare_light_data` + the b1/b3 `d3d11_update_constant_buffer` calls into `begin_frame` + `set_frame_lights`. Per draw, only b0 (PerObject) and b2 (PerMaterial) update. Add a `lights_dirty` guard so `set_frame_lights` is the only writer. The overlay pass (which currently re-preps scene data with prev=current VP, `_d3d11_shared.c:203`) keeps its own scene update — it's once per frame, fine.
- **OpenGL:** introduce two std140 UBOs mirroring the existing `Bones` pattern: `SceneBlock` (VP, prevVP, camera, fog, ambient, shadow VPs/bias/count) and `LightsBlock` (the `uLight*` arrays restructured as a struct array). Upload once in `begin_frame`/`set_frame_lights` via `glBufferSubData`; bind points 2/3 (bones use 0/1). Shader change: move the scene/light uniforms into `layout(std140) uniform` blocks — **GLSL only**; per-object uniforms stay loose uniforms. `upload_main_uniforms` shrinks to per-object/material state. Fallback: if UBO creation fails (ancient GL), keep a `use_scene_ubo=0` path that preserves today's per-draw uniforms (cheap to retain since the functions remain).
- **Metal:** allocate PerScene/PerLights in a per-frame ring-buffer allocation (or `setVertexBytes`/`setFragmentBytes` once into a retained `MTLBuffer`), set once in `begin_frame`/`set_frame_lights`; per draw only PerObject/PerMaterial bytes are set. Metal validation: buffers bound once remain bound across draws in the same encoder — verify the render-encoder lifecycle in `metal.m` doesn't recreate encoders mid-pass (overlay/postfx transitions create new encoders → rebind there).
- **Mid-frame invariant:** the canvas must not mutate the light array between `set_frame_lights` and `end_frame`. Today nothing does (flattening happens in `rt_canvas3d_begin`); add an assert/debug check in the canvas (`lights_frozen` flag set by the call, cleared in `end`) to enforce it against future regressions. `DrawMeshBlended`/deferred-queue flushes all happen inside the frozen window — safe.

## 4. Implementation steps

1. Vtable hook + canvas call site + frozen-lights debug assert (no backend implements it yet — no behavior change; full suite green).
2. Metal implementation (locally verifiable) + before/after capture of upload counters on `examples/3d/openworld_slice/` + golden probes unchanged.
3. D3D11 implementation (code-complete, Windows waiver).
4. OpenGL UBO blocks + shader restructure + fallback path (code-complete, Linux waiver).
5. Measurement report: draw-call-heavy scene (500 instanced-off boxes) frame time + upload-bytes counters, recorded in this plan's tracker section.

## 5. Public API changes

None. Internal backend vtable is not public ABI (in-tree backends only), but the vtable struct layout change (appended member) still gets a note in the commit body; no ADR required for internal-only additive hooks (confirm against `docs/architecture.md` guidance — if the backend vtable is listed as a contract surface, file the lightweight ADR).

## 6. Tests

- Given the Metal/SW golden probes, When plan lands, Then images are byte-identical (constants moved, not changed).
- Unit: canvas calls `set_frame_lights` exactly once per frame when hook present; per-draw light args still passed (compat assert via a mock backend in `test_rt_canvas3d.cpp` — a fake vtable recording call order: begin_frame → set_frame_lights → N×submit_draw → end_frame).
- Frozen-lights assert test: mutating a light between begin/end trips the debug check.
- Perf (recorded, not asserted): upload-bytes diagnostic drops by ~(N_draws-1)×sizeof(lights+scene) on the 500-box scene.

## 7. Verification gates

Full build + ctest; goldens unchanged on Metal + SW; `-L slow`; openworld_slice runs with counters logged before/after. Win/Linux waivers recorded for D3D11/GL runtime verification.

## 8. Risks & constraints

- **Encoder/binding lifecycles:** Metal encoder recreation and GL program switches (main vs skybox vs postfx programs) must rebind the frame blocks — audit every program/encoder transition in each backend.
- **Shader/CPU struct layout drift (GL UBO):** std140 padding rules differ from loose uniforms — add a compile-time size check on the C mirror structs and one runtime `GL_UNIFORM_BLOCK_DATA_SIZE` validation at link.
- **Ordering:** `set_frame_lights` must come after the shadow pass (shadow VPs final) — the render-pass orchestration in `render_pass.inc:305+` already has this seam.
- Plan 07 depends on this structure (cluster tables join the per-frame upload); land 04 first.
