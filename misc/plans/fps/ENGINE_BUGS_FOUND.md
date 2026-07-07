# Viper/Zia Bugs Found & Fixed During Engine-Plan Implementation

Running log for the Track E implementation (docs 01–10). Every entry was fixed
at the root (no workarounds), with regression coverage where applicable.

## Fixed

### BUG-E1 — Zia lowerer: struct construction never zero-initialized its alloca
- **Found:** 2026-07-07 (doc 10, while reproducing the "aggregate-return miscompile")
- **Severity:** P0 for struct-with-managed-field code (crash/heap corruption in native builds)
- **Symptom:** `new Struct(...)` where the struct has a `String`/object field crashes
  native binaries ("rt_string_header: invalid string handle" or SEGV), VM unaffected.
  Configuration-sensitive (multi-module -O0 crashed; single-module/-O2 often got lucky
  stack garbage) — the shape behind xenoscape's "aggregate-return miscompile" note.
- **Root cause:** `Lowerer::lowerNewStruct` (`src/frontends/zia/Lowerer_Expr_Complex.cpp`)
  emitted a raw `alloca` and called `init` on it. Managed-field assignment emits
  retain-new/release-old; the "old" value on first assignment is uninitialized stack
  memory. The VM zero-fills allocas (masking the bug); native codegen does not.
  Same hazard class as the for-in string-slot fix documented at `Lowerer_Stmt.cpp:897`.
- **Fix:** construction now uses `emitStructTypeAlloc` (alloca + per-field zero-init).
- **Tests:** `tests/zia_runtime/51_struct_return_abi.zia` (new) — VM + `native_run_zia_51_*`
  at -O0 and -O2; failed before, passes after.

### BUG-E2 — Zia lowerer: `ValueTypeAddField` retain flag emitted as i64, ABI says i1
- **Found:** 2026-07-07
- **Severity:** P1 — any struct containing a String/object field failed IL verify at
  construction (`call arg type mismatch: parameter 3 expects i1 but got i64`), on every
  backend including the VM.
- **Root cause:** `Lowerer_Emit.cpp` `emitBoxValue` passed `Value::constInt(field.retainNow)`
  for the `i1` parameter of `Viper.Core.Box.ValueTypeAddField` (`runtime.def:286`).
- **Fix:** `Value::constBool(field.retainNow != 0)`.
- **Tests:** covered by `51_struct_return_abi.zia` (the `Named` string-field cases).

### BUG-E3 — Native linker: `getrlimit`/`setrlimit` missing from the dynamic-symbol policy
- **Found:** 2026-07-07 (viperide native link failed: `undefined symbol 'getrlimit'`)
- **Root cause:** `src/runtime/io/rt_watcher.c:577` gained getrlimit/setrlimit calls, but
  `src/codegen/common/linker/DynamicSymbolPolicy.hpp` (the libc import allowlist) was
  never updated, so any natively linked program pulling in the watcher failed to link.
- **Fix:** added both symbols to the policy (next to `sysconf`).

### BUG-E4 — Broken test source: googletest streaming on the Viper test framework
- **Found:** 2026-07-07 (tree did not compile: `src/tests/zia/test_zia_control_flow.cpp:246`)
- **Root cause:** `EXPECT_TRUE(x) << "msg"` — Viper's `EXPECT_TRUE` is a
  `do{}while(false)` macro with no `operator<<` sink.
- **Fix:** converted the message to a comment; assertion kept.

### BUG-E5 — 3D audio: Doppler factor computed, then discarded
- **Found:** 2026-07-07 (doc 02, surveying the mixer for the pitch work)
- **Severity:** P2 (documented feature silently inert)
- **Symptom:** `SoundSource3D.DopplerFactor` reported plausible values but playback pitch
  never changed — fly-bys sounded static.
- **Root cause:** `rt_sound3d.c` (`rt_sound3d_update_voice_ex`) computed the Doppler factor
  and dropped it (`(void)doppler;`) because the ViperAUD mixer had no per-voice playback
  rate. The docs hedged ("kept ... for playback-rate-capable backends").
- **Fix:** the doc-02 pitch work added per-voice fractional-cursor resampling to the mixer;
  Doppler now multiplies the user `Pitch` and is applied for real on every update path
  (`rt_sound3d.c`, `rt_sound3d_objects.c` `sound3d_source_apply_spatial`). Docs updated.
- **Tests:** `test_vaud_core_fixes` DSP suite (pitch consumption/clamps, lowpass/occlusion
  attenuation, duck engage/release) — rendered-buffer assertions.

### BUG-E6 — Metal backend: render-to-texture frame end double-ends the command encoder
- **Found:** 2026-07-07 (doc 05, while testing `RenderTarget3D.CopyTo` on the Metal backend)
- **Severity:** P0 for Metal RTT — hard crash (`endEncoding has already been called`
  Metal validation assertion) on **every** `Canvas3D.End` with a render target bound.
  Masked in CTest because headless runs fall back to the software backend; any
  windowed run that touched `SetRenderTarget` died.
- **Root cause:** `metal_end_frame` (`vgfx3d_backend_metal.m`) called
  `[ctx.encoder endEncoding]` and then, on the RTT branch, `metal_commit_pending`,
  which re-runs `metal_finish_encoding` — `ctx.encoder` was only nil'd at the end of
  the function, so the already-ended encoder was ended a second time.
- **Fix:** nil `ctx.encoder` immediately after the first `endEncoding`.
- **Tests:** `tests/runtime/test_canvas3d_viewmodel_sprite.zia` exercises the RTT
  readback path; verified interactively on Metal (crashed before, passes after)
  and on the software backend.

### BUG-E7 — Metal backend: PerMaterial C/MSL layout mismatch broke ALL textured sampling
- **Found:** 2026-07-07 (doc 05, via the RT→material coverage test's non-uniform texture)
- **Severity:** P0 for Metal visual correctness — every textured mesh draw on Metal
  sampled through a garbage UV transform (observed as the whole surface reading one
  texel or the 1×1 mip average). Latent because most existing content uses uniform or
  palette-flat textures, where "wrong UVs" and "right UVs" look identical, and the
  software backend (used by headless CTest) was correct.
- **Root cause:** `mtl_per_material_t` (`vgfx3d_backend_metal.m`) packs
  `int32_t shadingModel; float customParams[8];` (36 bytes) directly before
  `int32_t textureUvSets0[4]`. MSL aligns `int4` members of a constant buffer to
  16 bytes, so the shader's `PerMaterial` places the UV-set/UV-transform block 12
  bytes later than the C struct wrote it — the shader read shifted garbage for UV
  sets and transforms on every draw.
- **Fix:** 12-byte explicit pad (`_pad_uv_block[3]`) after `customParams`, making the
  C layout byte-identical to the MSL struct; comment documents the alignment contract.
- **Tests:** `tests/runtime/test_canvas3d_renderer_upgrades.zia` renders a
  half-green/half-blue texture and asserts exact texel colors on both halves (this is
  the first test in the suite that samples a non-uniform texture and checks specific
  texels); verified on Metal and software.

### BUG-E8 — Software backend: legacy materials never received shadows
- **Found:** 2026-07-07 (doc 03, while writing the atlas-slot visual shadow assert)
- **Severity:** P1 visual parity — on the software backend, plain-color/legacy-workflow
  materials without a normal map were lit per-vertex (Gouraud), and only the per-pixel
  lighting path samples shadow maps. Result: shadows from ANY light (directional, CSM,
  spot) silently never darkened legacy-material geometry on software, while every GPU
  backend shades per pixel and applied them. Masked because demos run on Metal and the
  software shadow tests exercised PBR/telemetry paths.
- **Root cause:** `sw_shade_fragment` (`vgfx3d_backend_sw_raster.inc`) routed to the
  per-pixel lighting stages only for `workflow == PBR || normal_map`; the per-vertex path
  has no shadow sampling.
- **Fix:** the per-triangle fragment context precomputes `per_pixel_shadows` (any light
  with a resolvable shadow slot) and the dispatch now takes the per-pixel Phong path for
  shadowed legacy materials. Note: toggling shadows on/off on software switches those
  materials between Gouraud and per-pixel shading, which slightly changes their base
  brightness — per-pixel is the GPU-parity result.
- **Tests:** `tests/runtime/test_canvas3d_shadow_budget.zia` visual section (atlas-slot
  spot shadow must darken the scene) — fails before, passes after on software; full
  `-L graphics3d` (97/97) green, no golden shifts.

### BUG-E9 — Zia sema: member access on anonymous `obj` values escaped to an internal lowering error
- **Found:** 2026-07-07 (doc 09, writing the ASCII FBX test: `m.TriangleCount` where
  `m = FBX.GetMesh(asset, 0)`)
- **Severity:** P2 diagnostic quality + API usability
- **Symptom:** `error[V-ZIA-INTERNAL]: internal: unresolved field 'TriangleCount' reached
  lowering; please report this as a compiler bug` — an internal-compiler-error message for
  ordinary user code.
- **Root cause (two layers):**
  1. `Sema::analyzeField` (`Sema_Expr_Advanced.cpp`) diagnosed unknown members for every
     typed base (List/Map/Set/String/Error/primitive/runtime class) but fell off the end
     returning an undiagnosed `unknown()` for anonymous `Ptr` bases — the type given to
     values returned by runtime signatures declared as bare `obj`. The lowerer's
     BUG-FE-006 backstop then correctly refused to guess, but with an ICE-style message.
  2. The extractor APIs themselves were needlessly untyped: `runtime.def` supports
     `obj<Class>` return annotations (used by `SceneAsset.Load`, `SceneNode.get_Mesh`),
     but `FBX.Load/GetMesh/GetSkeleton/GetAnimation/GetMaterial/GetMorphTarget`,
     `GLTF.Load/LoadAsset/GetMesh/GetMaterial`, and four `SceneAsset` getters
     (`GetMesh/GetMaterial/GetSkeleton/GetAnimation`) all returned bare `obj`, so
     property/method syntax on their results could never resolve.
- **Fix:** sema now reports `Type 'X' has no member 'Y'` (with a hint about the
  static-call form) for member access on any resolved memberless type, and the
  FBX/GLTF/SceneAsset extractor signatures carry typed `obj<...>` returns in both the
  `RT_FUNC` and `RT_METHOD` strings — `asset.MeshCount`, `asset.GetMesh(0)`, and
  `mesh.TriangleCount` now simply work.
- **Tests:** `ZiaRuntimeMemberAccess.AnonymousObjectMemberIsSemaErrorNotInternal` and
  `.TypedExtractorReturnsResolveMembers` (test_zia_bugfixes.cpp); `test_fbx_ascii.zia`
  uses the property forms end-to-end.

### BUG-E10 — Shadow cascade splits wrote past the caller's cascade array (stack corruption)
- **Found:** 2026-07-07 (final full-suite gate; `test_rt_canvas3d_gpu_paths` CSM checks)
- **Severity:** P1 — undefined behavior in every cascaded-shadow frame
- **Symptom:** cascade split distances degenerated to thirds of [0,1] instead of real
  camera depths, so all shadow draws landed in cascade 0 (cascades 1..N rendered empty).
- **Root cause:** the doc-03 budget decoupling shrank the caller's split scratch from
  `float[VGFX3D_MAX_SHADOW_LIGHTS]` to `float[VGFX3D_CSM_SLOTS]`
  (`rt_canvas3d_render_pass.inc`), but `canvas3d_compute_shadow_cascade_splits`
  (`rt_canvas3d_occlusion.inc`) still zero-filled `VGFX3D_MAX_SHADOW_LIGHTS` (12)
  entries — 32 bytes of adjacent stack (including the computed near/far depths) were
  overwritten every call.
- **Fix:** the splits helper now writes only `VGFX3D_CSM_SLOTS` entries (its documented
  contract); loop bounds updated to match.
- **Tests:** `test_rt_canvas3d_gpu_paths` (410/410) — the CSM per-cascade pass/draw
  assertions fail before, pass after.

### BUG-E11 — `World3D.New` never initialized the configurable query-hit capacity
- **Found:** 2026-07-07 (final full-suite gate; `test_rt_physics3d` overlap truncation)
- **Severity:** P1 — every overlap/raycast-all query was silently capped at 16 hits
- **Root cause:** the doc-04 E16 work added `max_query_hits`, but its default assignment
  landed in `world3d_finalizer` (dead — runs at destruction) instead of `rt_world3d_new`.
  Fresh worlds carried 0, which the query path clamps up to `PH3D_QUERY_HITS_MIN` (16)
  rather than the documented 256 default.
- **Fix:** `rt_world3d_new` initializes `max_query_hits = PH3D_MAX_QUERY_HITS`.
- **Tests:** `test_rt_physics3d` (760/760) — the 260-body overlap test asserts 256
  stored hits + total 260 + truncated flag.

### BUG-E12 — Swept CCD froze bodies resting or rolling on a surface
- **Found:** 2026-07-07 (final full-suite gate; `zia_smoke_3dbowling_trajectory` — the
  ball stalled ~0.65m down the lane on every shot; bisected to the doc-04 E13 TOI clip
  with the E11 fix already in place)
- **Severity:** P0 for any game using `UseCcd` on ground-contact bodies
- **Root cause (two layers):**
  1. `world3d_ccd_sweep_sphere_raw` accepted started-penetrating hits, so a body in
     persistent contact clipped at toi=0 every substep.
  2. The TOI clip scaled the WHOLE substep translation by toi, so a grazing hit (rolling
     ball, tiny gravity-induced downward closing velocity, toi ~= 0) discarded the
     tangential motion too — the comment claimed "the tangential component is preserved"
     but only the velocity reflection preserved it; the position advance did not.
- **Fix:** the CCD sweep skips surfaces the body already overlaps at t=0 (persistent
  contacts belong to the impulse solver), and the clip decomposes the translation
  against the hit normal — the normal component stops just before the surface, the
  tangential component is carried in full. Head-on impacts have no tangential part, so
  the thin-wall anti-tunneling guarantee (`g3d_test_physics3d_ccd_toi`) is unchanged.
- **Tests:** `zia_smoke_3dbowling_trajectory` (fails before, passes after — verified by
  reverting just this fix), `g3d_test_physics3d_ccd_toi`, `test_rt_physics3d` (760/760).

## Resolved-as-already-fixed (stale bug notes updated)

- **Aggregate-return miscompile (xenoscape)** — the codegen-side return-classification
  story no longer reproduces on any tested shape (HFA {f64×2}/{f64×4}, {i64,i64},
  {i64×3} sret, {i64×2,i1×4} MoveResult shape, mixed {i64,f64}, nested, method returns,
  chained calls; VM == native -O0/-O1/-O2). The *observable* breakage was BUG-E1/E2.
  `examples/games/xenoscape/VIPER_ZIA_BUGS.md` updated.
- **W001 discard-loop-variable papercut** — `for _ in 0..N` already supported by sema;
  documented in `docs/bible/part1-foundations/05-repetition.md`.
- **V3001 ambiguity noise** — already emits once per conflicting symbol per bind
  statement (not per use site); no change needed.
