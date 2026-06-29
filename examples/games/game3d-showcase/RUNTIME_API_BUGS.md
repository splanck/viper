# Runtime / API Bug Log — game3d-showcase enhancement work

A running record of Viper compiler / runtime / 3D-API issues found while extending this demo.
Each entry: date, severity, area, symptom, repro, workaround/status.

Severity: **P0** blocks the demo · **P1** wrong/missing behavior with a workaround · **P2** papercut/inconsistency.

---

> Note on tooling: the in-tree `build/install-check/bin/viper` is **stale** (predates
> `Viper.Game3D`) and must not be used for validation — it reports phantom errors such as
> "Unknown runtime namespace: Viper.Game3D". The authoritative current binary is
> `/usr/local/bin/viper` (equivalently `build/src/tools/viper/viper`). Two findings first
> attributed to "bugs" were actually this stale binary; they have been retracted below.

## BUG-002 — `SceneAsset.Load` traps (DomainError) on a missing file instead of returning null
- **Date:** 2026-06-03
- **Severity:** P1 (no graceful failure path)
- **Area:** `Viper.Graphics3D.SceneAsset` / FBX loader
- **Symptom:** When the path cannot be opened, the call raises an uncatchable
  `DomainError (code=0): FBX.Load: cannot open file` trap that aborts execution. The documented
  `if (m == null)` recovery pattern never runs, so a script cannot detect/skip a missing asset.
- **Repro:** `SceneAsset.Load("definitely_missing.fbx")` →
  `Trap @main...: DomainError (code=0): FBX.Load: cannot open file` (statements after never run).
- **Workaround:** Historical: probe with `Viper.IO.File.Exists(path)` before loading. Now obsolete —
  the demo's `spawnForest` relies on the `null` return to try candidate paths
  (`MapleTree_1.fbx`, then `examples/games/game3d-showcase/MapleTree_1.fbx`) and uses whichever loads,
  so the forest resolves whether the demo is run from the repo root or from inside the folder.
- **Status:** Resolved 2026-06-04. `SceneAsset.Load` now routes FBX files through a recoverable FBX
  loader and returns `null` for missing/unreadable/unrecognized FBX files. Direct `FBX.Load` remains
  strict, and malformed binary FBX data still reports the existing hard parse diagnostic.

---

## BUG-003 — `Material3D.SetMetallic` / `SetRoughness` methods advertised but not callable
- **Date:** 2026-06-03
- **Severity:** P2 (inconsistency)
- **Area:** `Viper.Graphics3D.Material3D`
- **Symptom:** Introspection lists `SetMetallic(f64)` and `SetRoughness(f64)` as methods, but the
  compiler (current binary) rejects them: `V-ZIA-SEMA: Runtime class 'Viper.Graphics3D.Material3D'
  has no method 'SetRoughness'`. The scalar factors must instead be set via the property setters
  `set_Metallic` / `set_Roughness` (and likewise `set_AO`, `set_EmissiveIntensity`,
  `set_NormalScale`, `set_Reflectivity`). The *map* setters (`SetAlbedoMap`, `SetNormalMap`, …)
  and `SetShadingModel`/`SetUnlit` ARE bound as methods, so the asymmetry is surprising.
- **Repro:** `Material3D.SetRoughness(mat, 0.8);` fails; `Material3D.set_Roughness(mat, 0.8);` works.
- **Workaround:** Historical: use the `set_*` property setters for scalar PBR factors.
- **Status:** Resolved 2026-06-04. The advertised `Set*` scalar aliases are bound instead of
  removed: `SetMetallic`, `SetRoughness`, `SetAO`, `SetEmissiveIntensity`, `SetNormalScale`, and
  `SetReflectivity`. `Light3D.SetEnabled` and `Light3D.SetCastsShadows` are also bound.
- **Generalizes:** Same defect on **`Light3D.SetEnabled(i1)`** — advertised by introspection,
  rejected by the compiler (`has no method 'SetEnabled'`); must use `Light3D.set_Enabled(...)`.
  Yet `Light3D.SetIntensity` / `SetColor` (no matching property) ARE bound. The pattern: when a
  boolean/scalar *property* exists, its `Set<Name>` method twin is often a phantom — prefer `set_<Name>`.

---

## Note N1 — `List[T]` fields are non-nullable; `list == null` is a type error
- **Date:** 2026-06-03
- **Severity:** P2 (language gotcha, not a defect)
- **Area:** Zia type system
- **Symptom:** A defensive `if (myList == null)` guard fails to compile:
  `V-ZIA-SEMA: Cannot compare List[...] with null`. List-typed fields are reference-but-non-null;
  they must be initialized (e.g. `= []`) before use and cannot be null-checked.
- **Workaround:** Initialize every `List` field in `setup`/`resetWorld` and drop the null guards;
  guard on element count instead. (`obj`-typed fields like `Pixels`/`Light3D` remain nullable.)
- **Status:** Working as intended. The compiler diagnostic now explicitly says the List is
  non-nullable and suggests using an Optional type when absence is required.

---

## Note N2 — `PostFX3D.AddColorGrade` brightness is an additive offset, not a multiplier
- **Date:** 2026-06-03
- **Severity:** P2 (documentation ambiguity)
- **Area:** `Viper.Graphics3D.PostFX3D.AddColorGrade(brightness, contrast, saturation)`
- **Symptom:** Passing `brightness = 1.02` (intending a 2% multiplicative lift) blows the entire
  frame to pure white. The parameter is an **additive** exposure offset centered on `0.0`; sane
  values are ~`0.0–0.03` (existing demos use `0.015`). Contrast/saturation, by contrast, ARE
  multipliers centered on `1.0`.
- **Repro:** `PostFX3D.AddColorGrade(fx, 1.02, 1.06, 1.10)` → white screen; `0.015` is correct.
- **Workaround:** Treat brightness as a small additive offset near 0. Documented in `config.zia`.
- **Status:** Resolved 2026-06-04. Runtime comments and user docs now call the first parameter a
  `brightnessOffset` and state that it is additive/centered on `0.0`, unlike contrast and saturation.

---

## Retracted (stale-binary artifacts, not real bugs)
- ~~`SceneAsset.get_SceneCount` has no bound getter~~ — **false positive.** Works on the current
  binary (`scenes=1`); the failure was the stale `install-check` binary.
- ~~Binding `Viper.IO.File as FileSys` breaks `Viper.Game3D`~~ — **false positive.** Both binds
  coexist fine on the current binary; the stale binary simply lacked `Viper.Game3D`.

---

## BUG-003 — `Material3D.SetMetallic` / `SetRoughness` methods advertised but not callable
- **Date:** 2026-06-03
- **Severity:** P2 (inconsistency)
- **Area:** `Viper.Graphics3D.Material3D`
- **Symptom:** Introspection lists `SetMetallic(f64)` and `SetRoughness(f64)` as methods, but the
  compiler rejects them: `V-ZIA-SEMA: Runtime class 'Viper.Graphics3D.Material3D' has no method
  'SetRoughness'`. The scalar factors must instead be set via the property setters
  `set_Metallic` / `set_Roughness` (and likewise `set_AO`, `set_EmissiveIntensity`,
  `set_NormalScale`, `set_Reflectivity`). Note the asymmetry: the *map* setters (`SetAlbedoMap`,
  `SetNormalMap`, …) and `SetShadingModel`/`SetUnlit` ARE bound as methods.
- **Repro:** `Material3D.SetRoughness(mat, 0.8);` fails; `Material3D.set_Roughness(mat, 0.8);` works.
- **Workaround:** Historical: use the `set_*` property setters for scalar PBR factors.
- **Status:** Duplicate retained for history. Superseded by the resolved BUG-003 entry above; the
  advertised `Set*` scalar methods are now bound rather than removed.

---

## BUG-004 — Native asset embedding defined `viper_asset_blob` twice (multiply-defined symbol)
- **Date:** 2026-06-04
- **Severity:** P0 (native `embed` never worked)
- **Area:** `src/tools/viper/cmd_run.cpp` + `src/tools/common/native_compiler.cpp`
- **Symptom:** A single-step native build of a project with an `embed` directive failed to link:
  `error: multiply defined symbol 'viper_asset_blob'`. `cmd_run.cpp` both injected the VPA blob into
  `.rodata` via codegen (`--asset-blob`) **and** wrote/linked a separate asset `.o`
  (`writeAssetBlobObject`), and `native_compiler.cpp` set both `opts.asset_blob_path` and pushed the
  `.o` onto `extra_objects` — two definitions of the same symbol. (The two-step demo pipeline
  `viper build -o file.il` + `viper codegen` passed neither, so packaged binaries silently had no
  embedded assets at all.)
- **Fix:** `cmd_run.cpp` now embeds via the `.rodata` injection path only and leaves `assetObjPath`
  empty, so the redundant object is never linked. Verified: a native build embeds the blob
  (binary grows by the asset size, survives dead-strip) and `Assets.Exists`/`Size` resolve it from
  any working directory.
- **Status:** Fixed 2026-06-04.

---

## BUG-005 — `SceneAsset.LoadAsset` ignored the asset store for FBX (filesystem only)
- **Date:** 2026-06-04
- **Severity:** P1 (embedded/packed FBX models unreachable)
- **Area:** `src/runtime/graphics/3d/render/rt_model3d.c`
- **Symptom:** `SceneAsset.LoadAsset("model.fbx")` returned NULL for an embedded/packed FBX even though
  `Assets.Exists("model.fbx") == 1`. The glTF branch honored `load_assets`
  (`rt_gltf_load_asset` → `rt_asset_load_raw`), but `model3d_load_from_fbx` had no `load_assets`
  parameter and always read from the filesystem via `rt_fbx_load_recoverable(path)`.
- **Fix:** `model3d_load_from_fbx` now takes `load_assets`; when set it pulls bytes from the asset
  store (`rt_asset_load_raw`) and decodes them through a temp-file bridge
  (`model3d_fbx_from_asset_bytes`, mirroring the `rt_asset_decode` image pattern, since the FBX
  importer is path-based), falling back to the filesystem. Verified: `SceneAsset.LoadAsset` on an
  embedded FBX returns `meshes=3` in both the interpreter and a native binary, from any CWD.
- **Status:** Fixed 2026-06-04.

---

## BUG-006 — `-O1` native codegen miscompiles the forest placement loop (0 trees vs 152)
- **Date:** 2026-06-04
- **Severity:** P1 (wrong native output under optimization)
- **Area:** native AArch64 codegen — IL-to-MIR cross-block temp reloads
  (`src/codegen/aarch64/LowerILToMIR.cpp`)
- **Symptom:** `game3d-showcase` plants **0** maples in a `-O1` native binary but **152** at `-O0`
  and **152** interpreted. All inputs are byte-identical across native/interpreted (model
  `meshes=3`, terrain height `14.41`, `hash01` values, every `config` constant), and
  `canPlantTree(...)` returns `true` when called standalone — yet inside the optimized
  `plantHillForest`/`plantHomeGrove` loops the predicate evaluates `false` for every site.
  Root cause: a forward-defined `f64` block parameter was reloaded from its cross-block spill slot
  before its defining block had populated `tempRegClass`, so lowering defaulted it to GPR and emitted
  `LdrRegFpImm` + `SCvtF` instead of `LdrFprFpImm`. That converted the raw double bits to a huge
  numeric value and made the floating-point bounds checks fail.
- **Repro:** `viper build examples/games/game3d-showcase -o /tmp/g3d -O1` → "planted 0";
  same with `-O0` → "planted 152".
- **Workaround:** Historical: build the showcase at `-O0`.
- **Status:** Fixed 2026-06-04. `LowerILToMIR` now seeds temp register classes from the whole IL
  function before lowering blocks, so cross-block reloads keep the defining IL type even when the use
  block appears earlier in textual order. Verified: `-O1` native now logs "planted 152 maples";
  added `Arm64Bugfix.ForwardDefinedF64BlockParamReloadKeepsFprClass`.

---

## Note N3 — `PostFX3D.AddSSAO` / `AddDOF` / `AddMotionBlur` trap on the CPU post-FX path
- **Date:** 2026-06-04
- **Severity:** P2 (papercut — a clear trap, but easy to hit)
- **Area:** `Viper.Graphics3D.PostFX3D`
- **Symptom:** Adding SSAO, depth-of-field, or motion blur to a chain that is later applied on the
  **software backend or to a RenderTarget3D** raises `DomainError: SSAO, DOF, and motion blur require
  GPU window postfx; RenderTarget3D and software CPU postfx support Bloom, Tonemap, FXAA, ColorGrade,
  and Vignette`. The error surfaces at apply/validate time (e.g. the smoke probe), not at `Add*`.
- **Workaround:** Gate all three behind `Canvas3D.BackendSupports(canvas, "ssao")` (this demo's
  `buildPostFX` does exactly this); the CPU-safe effects (bloom/tonemap/colour-grade/vignette/FXAA)
  may always be added. Status: working as intended; documented here so future post-FX work gates the
  GPU-only effects up front.

---

## Note N4 — New Canvas3D / Keys bindings added for this enhancement pass
- **Date:** 2026-06-04
- **Area:** runtime additions (not bugs) supporting recommendations #1, #5, #6, #9.
- **Added:** `Canvas3D.SetFullscreen(i1)` / `get_IsFullscreen` / `ToggleFullscreen` (→ existing
  `vgfx_set_fullscreen`), `Canvas3D.DrawImage2D(x,y,w,h,pixels)` (overlay image blit; also added a
  final-overlay GC-object retention list so HUD-pass textured draws survive post-FX), `Canvas3D.
  DrawMeshWind(mesh,xform,mat,dirX,dirZ,strength,phase)` (CPU height-weighted vertex sway), and
  `Game3D.Keys.get_F11`. Also: `Canvas3D.DrawMeshSkinned` now accepts an `AnimController3D` as well
  as an `AnimPlayer3D`. Unit coverage in `src/tests/unit/test_rt_canvas3d.cpp` (wind deform + NULL
  safety); docs in `docs/viperlib/graphics/rendering3d.md`.
