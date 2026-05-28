# Plan 10 — Upload only the active shadow-VP matrices (OpenGL)

- **Severity:** Low (performance) — largely **subsumed by Plan 03**
- **Type:** Optimization (behavior-preserving)
- **Primary file:** `src/runtime/graphics/3d/backend/vgfx3d_backend_opengl.c`
- **Status:** DEFERRED — see `STATUS.md`. Subsumed by Plan 03; OpenGL is not buildable on the
  macOS dev environment.

## Problem

`upload_main_uniforms` uploads **all** `VGFX3D_MAX_SHADOW_LIGHTS` shadow view-projection matrices on
every draw, regardless of how many shadow slots are actually active:

```c
gl.Uniform1i(ctx->uShadowCount, ctx->shadow_count);
for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++)
    gl.UniformMatrix4fv(ctx->uShadowVP[slot], 1, GL_TRUE, ctx->shadow_vp[slot]);
```
(`vgfx3d_backend_opengl.c:3674-3676`)

When `shadow_count == 0` (no shadows this frame, the common case) this still uploads
`VGFX3D_MAX_SHADOW_LIGHTS` matrices per draw — pure waste; the shader ignores slots `>= shadowCount`.

## Investigation notes / nuances

- **Relationship to Plan 03:** the shadow-VP matrices are *frame-constant* (set during the shadow
  pass / `begin_frame`, identical for every main-pass draw). Plan 03's content-gated, once-per-frame
  upload of the frame-constant uniform group **already eliminates** this per-draw cost entirely. If
  Plan 03 lands first, this finding disappears — do **not** implement both independently.
- This standalone plan exists only as the cheap interim fix if Plan 03 is deferred: bound the loop by
  `ctx->shadow_count` instead of `VGFX3D_MAX_SHADOW_LIGHTS`.
- Correctness caveat for the standalone version: the shader keys off `uShadowCount`, and
  `vgfx3d_opengl_sanitize_shadow_index` already clamps light shadow indices to `[0, shadow_count)`
  (`upload_light_uniforms`, `:3628-3629`). So uploading only `shadow_count` matrices is safe — slots
  `>= shadow_count` are never sampled. Verify the shader never indexes `uShadowVP` beyond
  `shadowCount` (it shouldn't, given the sanitize step).

## Proposed fix

**Preferred:** fold into Plan 03 — the shadow-VP uniforms move into the once-per-frame frame-constant
group and are no longer re-uploaded per draw. Mark this plan "resolved by 03."

**Interim standalone (only if 03 is deferred):**
```c
for (int32_t slot = 0; slot < ctx->shadow_count && slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++)
    gl.UniformMatrix4fv(ctx->uShadowVP[slot], 1, GL_TRUE, ctx->shadow_vp[slot]);
```

## Files to modify

- `src/runtime/graphics/3d/backend/vgfx3d_backend_opengl.c` — only (or none, if resolved by Plan 03).

## Tests

- A shadow-casting visual probe (Linux/OpenGL) must match baseline — both the 0-shadow and
  ≥1-shadow cases.
- Build + `ctest --test-dir build -L graphics3d` on Linux.

## Risk

Very low standalone. Recommend simply implementing Plan 03 and closing this as subsumed to avoid two
overlapping edits to the same uniform-upload code.
