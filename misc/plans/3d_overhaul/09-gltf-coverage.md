# Plan 09 — glTF Ecosystem Coverage + Loader/API Hygiene

> **Status (2026-07-03): §3.2–§3.4 IMPLEMENTED; §3.1 (EXT_meshopt_compression) DEFERRED.**
> - **PostFX add-time validation (§3.4):** implemented as bind-time validation —
>   `Canvas3D.SetPostFX` refuses chains carrying GPU-scene-buffer effects
>   (SSAO/DOF/motion blur/TAA) on canvases without GPU window postfx, recording the
>   reason in the new `PostFX3D.LastError` property (cleared on successful bind). The
>   apply-time trap remains as the backstop for direct/legacy flows. No back-pointer
>   from chain to canvas was added (avoids a dangling weak reference), so the add-time
>   variant validates at the next bind rather than at the Add call itself.
> - **Load options + forced tangents (§3.3):** `rt_gltf_load_options` +
>   `SceneAsset.LoadWithOptions(path, forceTangents)` / `LoadResultWithOptions`.
>   Options scope **thread-locally** around the load
>   (`rt_gltf_set_thread_load_options`) instead of threading a parameter through every
>   loader layer — both tangent gates (sync + preload-commit) consult
>   `rt_gltf_active_load_options()`, and preload bundles apply gates on the committing
>   thread so worker safety holds. Default entry points are behavior-identical (pinned
>   by test).
> - **Spec-glossiness (§3.2):** `KHR_materials_pbrSpecularGlossiness` accepted in both
>   extension gates and converted per the documented approximation (diffuse → base
>   color, `roughness = 1 - glossiness`, metallic from max-component specular vs 0.04;
>   diffuseTexture → base-color slot, specularGlossinessTexture → specular slot).
>   The extension takes precedence over a fallback `pbrMetallicRoughness`.
> - **DEFERRED — EXT_meshopt_compression (§3.1):** the from-scratch triple codec
>   (ATTRIBUTES/TRIANGLES/INDICES + filters) is a Zstd-scale clean-room build whose
>   correctness gate is byte-exact round-trips against gltfpack fixtures; it needs a
>   dedicated session with the wire-format reference at hand. Current behavior remains
>   correct: assets requiring the extension fail cleanly at the required-extension gate
>   with a named diagnostic (`requires EXT_meshopt_compression (unsupported)`).
>   gltfpack is confirmed runnable offline via `npx gltfpack` for fixture authoring.
> - Verified: 570/570 glTF tests (spec-gloss conversion incl. metallic extremes,
>   forced-tangents on/off pinning), 248/248 canvas3d (bind-time validation test),
>   completeness/surface gates green. Docs: rendering3d.md extension table +
>   LoadWithOptions + PostFX3D.LastError rows.

## 1. Objective & scope

The glTF loader is excellent for hand-authored assets but misses three things production files carry: `EXT_meshopt_compression` (hard-fails the required-extension check — correctly, but that means compressed assets simply don't load), `KHR_materials_pbrSpecularGlossiness` (legacy but widespread in older libraries — silently loses its material data), and tangents for assets whose materials gain normal maps after import (tangent generation is gated on a normal map being bound at load time). Plus one surviving API-hygiene item from the ridgebound bug log: PostFX capability failures surface at apply time (trap) instead of add time.

**In scope:** EXT_meshopt_compression decode; spec-glossiness → metallic-roughness conversion; a glTF load-options mechanism + forced tangents; PostFX add-time capability validation. **Out of scope:** `KHR_draco_mesh_compression` — **explicitly deferred**: Draco is a large edge-collapse/entropy codec, meshopt is its modern replacement, and Draco-compressed assets keep failing cleanly at the required-extension gate with a clear diagnostic.

**Zero external dependencies:** the meshopt vertex/index/filter codecs are implemented from scratch against the published EXT_meshopt_compression spec (the wire format is fully specified in the extension text); no meshoptimizer library, no Draco, no gltf SDKs.

## 2. Current state (verified anchors)

- Extension gates: `gltf_required_extension_supported` (`assets/rt_gltf.c:519` — KHR_texture_transform, emissive_strength, unlit, specular, lights_punctual) and `gltf_used_extension_supported` (`:531` — adds texture_basisu, clearcoat, transmission). Enforcement `gltf_validate_required_extensions` (`:576`) → `rt_asset_error_setf(RT_ASSET_ERROR_UNSUPPORTED, ...)` naming offenders. **No meshopt/specGloss/draco anywhere in the tree.**
- bufferView resolution — the meshopt attach point: `gltf_get_buffer_view_data(root, view_idx, buffers, buf_count, &out_len)` (`assets/rt_gltf_codec.inc:1276`) returns `buffers[i].data + byteOffset` after bounds checks; **bufferView `extensions` are never inspected**. Accessors bind through `gltf_accessor_bind_buffer_view` (`rt_gltf_accessor.inc:87`) / `gltf_get_accessor_view` (`:228`); `gltf_buffer_t {data,len}` (`codec.inc:14`).
- Material parse: `gltf_load_materials` loop (`rt_gltf_material.inc:758, :791-814`): `jget(mat_json,"pbrMetallicRoughness")` → `gltf_material_create_pbr_from_json` (`:432`) + `gltf_material_apply_pbr_textures` (`:461`); extension dispatch `gltf_material_apply_extensions` (`:644`) with the KHR_materials_specular branch (`:507`) as the pattern; setters `rt_material3d_set_{metallic,roughness,alpha,...}` + slot binds (`RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR/_METALLIC_ROUGHNESS`, `:475-493`).
- Tangent gate (verified verbatim): `rt_gltf_mesh.inc:104-111` — normals regenerated when absent; tangents generated only when `!HAS_TANGENTS && HAS_UV0 && material && material->normal_map`. Generator: `rt_mesh3d_calc_tangents` (`render/rt_canvas3d.h:334`).
- Load entry points take only a path — **no options struct exists**: `rt_gltf_load(rt_string)` (`rt_gltf.h:33`), `rt_gltf_load_asset`, `rt_gltf_load_preloaded`, preload-bundle API (`rt_gltf.h:48-83`). Zia chain: `GLTF.Load` (def:13855/13907); `SceneAsset` routes by extension via `rt_model3d_load_impl` (`rt_model3d_api.inc:81`), Result variants `SceneAsset.LoadResult/LoadAssetResult` (def:13863/13865, built by `model3d_load_value_to_result`, `rt_model3d_api.inc:13`).
- Hygiene reality (re-verified 2026-07): ridgebound BUG-002 (loader traps on missing files) **already fixed** — `rt_model3d_load` returns NULL + asset error (`rt_model3d_api.inc:31-40`); BUG-003 phantom `Set*` methods **already removed** (property setters only). **Remaining:** `rt_postfx3d_add_ssao/_add_dof/_add_motion_blur` (`rt_postfx3d.c:1371-1406`) do no capability check; the GPU-requirement trap fires at apply (`:1329-1333` via `vgfx3d_postfx_requires_gpu_scene_buffers`). Capability query exists: `rt_canvas3d_backend_supports` + `postfx3d_canvas_supports_gpu_scene_effects`.

## 3. Design

### 3.1 EXT_meshopt_compression

- Register in **both** extension lists (`rt_gltf.c:519/:531`) — it appears in `extensionsRequired` when fallback buffers are absent.
- Decode at bufferView materialization: after buffers load, walk `bufferViews`; for each with `extensions.EXT_meshopt_compression {buffer, byteOffset, byteLength, byteStride, count, mode, filter}`, decode into an owned shadow buffer and repoint the view. Implementation shape: a `gltf_meshopt_views` table built once (parallel to `gltf_buffer_t`), consulted inside `gltf_get_buffer_view_data` (one lookup added at `codec.inc:1276`) so accessors/images downstream are untouched.
- Codecs (new `assets/rt_gltf_meshopt.inc`, from scratch per spec):
  - `ATTRIBUTES` mode: byte-plane delta codec (header version byte, block-encoded deltas per byte plane, tail block).
  - `TRIANGLES` mode: index codec (edge FIFO + vertex FIFO, codeaux table).
  - `INDICES` mode: sequence index codec.
  - Filters: `OCTAHEDRAL` (normals/tangents), `QUATERNION`, `EXPONENTIAL` — applied post-decode per the `filter` field.
- Malformed data → `rt_asset_error_setf(CORRUPT, ...)` + load failure (never trap); the existing 256 KB-bounded fuzz harness (`fuzz_gltf_loader.cpp`) automatically covers the new path once wired — add meshopt seed corpora.

### 3.2 KHR_materials_pbrSpecularGlossiness

- New branch in the `gltf_load_materials` loop (`material.inc:791-814`): when the material's `extensions.KHR_materials_pbrSpecularGlossiness` exists, prefer it over (typically absent) `pbrMetallicRoughness`.
- Conversion (standard approximation, implemented from the published math): `diffuseFactor/diffuseTexture` → base color; `glossinessFactor` → `roughness = 1 - glossiness`; `specularFactor` → metallic estimate (max-component specular vs dielectric 0.04: `metallic ≈ saturate((maxSpec - 0.04) / (1 - 0.04))` with albedo compensation). `specularGlossinessTexture` → bind as the specular map slot (the engine has one: `RT_MATERIAL3D_TEXTURE_SLOT` specular) and set roughness from the factor — per-texel gloss→roughness conversion of the texture is out of scope v1 (documented approximation).
- Add to `gltf_used_extension_supported` and `gltf_required_extension_supported` (assets exist with it in required).

### 3.3 Load options + forced tangents

- New POD `rt_gltf_load_options { int8_t force_tangents; int8_t generate_missing_normals /*=1, current behavior*/; reserved bytes }` with `rt_gltf_load_options_default()`. New entry `rt_gltf_load_with_options(rt_string path, const rt_gltf_load_options *opts)`; existing entries call it with defaults (zero behavior change). Threaded through `rt_model3d_load_impl` and the preload-bundle path (options captured into the bundle at create time — the tangent decision happens in `rt_gltf_mesh.inc:106-110`, which changes to `if (!HAS_TANGENTS && HAS_UV0 && (opts->force_tangents || (material && material->normal_map)))`.
- Zia surface (avoid an options-object class for two booleans): `SceneAsset.LoadWithOptions(path, forceTangents: i1) -> obj` + `LoadResultWithOptions`; `GLTF.Load` unchanged. FBX ignores unknown options gracefully (they're glTF-scoped; the impl routes by extension).

### 3.4 PostFX add-time validation

- `rt_postfx3d_add_ssao/_add_dof/_add_motion_blur` gain a canvas-aware check **when the chain is already bound to a canvas** (`rt_canvas3d_set_postfx` path): if `!postfx3d_canvas_supports_gpu_scene_effects(canvas)`, set a recoverable per-chain `last_error` + return self *without* appending (no trap), and surface `PostFX3D.get_LastError -> str`. When built detached (no canvas yet), validation defers to `SetPostFX` bind time — same error surface, still before first frame. The apply-time trap (`:1329-1333`) remains as the final backstop but becomes unreachable through public flows.
- Rationale vs trap-at-add: ridgebound's real-world pattern is capability-gated fallback (`BackendSupports` + reduced chain) — a queryable error supports that; a trap doesn't.

## 4. Implementation steps

1. PostFX add/bind-time validation + `get_LastError` + tests (SW canvas + AddSSAO → error set, chain unchanged, no trap; GPU canvas → appended) — smallest item, ships first.
2. Load-options plumbing + forced tangents + tests (asset w/o tangents + no normal map: default → no tangents (current behavior pinned), forced → tangents present; byte-compare tangent buffer against the normal-map-bound variant).
3. Spec-glossiness conversion + tests (fixture with diffuse+glossiness: converted metallic/roughness within tolerance; extension in required list loads; texture slot bound).
4. meshopt ATTRIBUTES codec + unit tests (hand-built encoded blocks from the spec's reference vectors; note: generate fixtures with `gltfpack` **offline as test data only** — the tool never enters the build or runtime).
5. meshopt TRIANGLES/INDICES codecs + filters + end-to-end fixture (gltfpack-compressed cube+skin loads identical to its uncompressed twin: positions/indices/normals byte-compare post-decode).
6. Extension-list registration + fuzz corpora + docs (`docs/viperlib/graphics/scene.md` asset-import section: supported-extension table update, meshopt/specGloss/Draco-deferred notes).

## 5. Public API changes (runtime.def)

- `Viper.Graphics3D.SceneAsset`: `RT_METHOD("LoadWithOptions","obj(str,i1)")`, `RT_METHOD("LoadResultWithOptions","obj<Viper.Result>(str,i1)")` (handlers `rt_model3d_load_with_options`, `_load_result_with_options`).
- `Viper.Graphics3D.PostFX3D`: `RT_PROP("LastError","str", getter, none)`.
- New file `assets/rt_gltf_meshopt.inc` (included from `rt_gltf.c`'s TU — .inc files don't hit the baseline count; verify `source_health_audit` treatment of new .inc under `runtime_api_contract_files` and bump if flagged).
- No new classes/IDs; surface audits + completeness script after def changes. No ADR (no C ABI change; loader-internal).

## 6. Tests

- Given a gltfpack-compressed asset with `EXT_meshopt_compression` in extensionsRequired, When loaded, Then it succeeds and mesh data matches the uncompressed twin (fail-before: UNSUPPORTED error).
- Given a specGloss-only material, When loaded, Then base color = diffuseFactor and roughness = 1-glossiness (fail-before: default white PBR).
- Forced-tangents Given/When/Then per §4.2.
- PostFX: add-on-unsupported sets LastError, chain length unchanged, apply never traps.
- Fuzz replay green with new corpora; existing 75-test glTF suite untouched.

## 7. Verification gates

Full build + ctest + `-L slow`; `test_rt_gltf` suite + new fixtures; fuzz smoke on the meshopt path; surface audits. All-platform code (pure CPU parsing) — no GPU waivers needed for this plan.

## 8. Risks & constraints

- **meshopt codec fidelity:** the byte-exact wire format is versioned (header byte); implement v0, reject unknown versions with UNSUPPORTED (graceful). Reference vectors from the spec + gltfpack-generated fixtures are the truth source.
- **Spec-gloss approximation:** conversion is lossy by design; document, and don't attempt per-texel texture conversion in v1.
- **Options plumbing through the async path:** the preload bundle runs on workers — options must be captured by value at bundle creation (no pointer into caller memory).
- **Zero external dependencies:** gltfpack/toktx are used *offline* to author test fixtures only; no library code, headers, or binaries enter the repo's build or runtime. All codecs are clean-room from the spec text.
