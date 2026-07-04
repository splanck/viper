# 3D Overhaul — Implementation Plan Index

Goal: push the Viper 3D stack toward **Unity-level visual quality**, better **performance**, a more complete **high-level gaming API**, and a **production-robust asset pipeline**. This plan set came out of a deep review (2026-07) of the rendering core (`src/runtime/graphics/3d/`, ~106K LOC), the public API (`src/il/runtime/runtime.def` 3D block, ~95 classes), the asset loaders (glTF/FBX/OBJ/STL/KTX2), and all 3D game demos.

Every plan documents the *current* code shape with verified file references before proposing changes. If a cited anchor has drifted by the time a plan is executed, re-verify the surrounding code first — the design intent still holds, but line numbers move.

## Plans (ranked by impact)

| # | Plan | Axis | Effort |
|---|------|------|--------|
| [01](01-ibl.md) | True image-based lighting (prefiltered specular + SH irradiance + split-sum) | Visuals | L |
| [02](02-game3d-app-framework.md) | Game3D app framework: scenes, behaviors, input map, bone sockets | Platform API | L |
| [03](03-texture-pipeline.md) | KTX2 supercompression (Zstd + Basis), BC1/BC4/BC5(+BC6H), fuzzing, trap→error | Assets | L |
| [04](04-per-frame-constants.md) | Stop per-draw scene/light constant re-upload | Performance | M |
| [05](05-postfx-overhaul.md) | Mip-chain bloom, normal-aware SSAO, TAA, tonemap/gamma fix | Visuals | L |
| [06](06-shadow-quality.md) | Normal-offset/per-cascade bias, Poisson PCF, cascade/slot decoupling | Visuals | M |
| [07](07-clustered-lighting.md) | Clustered Forward+ light culling (CPU froxel binning) | Performance | M |
| [08](08-ui-toolkit-3d.md) | Canvas3D overlay primitives + Game3D UI widget toolkit | Platform API | M |
| [09](09-gltf-coverage.md) | EXT_meshopt, spec-glossiness, forced tangents, load options, PostFX add-time validation | Assets | M |
| [10](10-ssr-soft-particles.md) | Screen-space reflections + soft particles (+ shared depth access) | Visuals | M |
| [11](11-demo-modernization.md) | Facade-first rewrite pass over the 3D game demos | Demos | M |

Effort: M ≈ days, L ≈ a week+ of focused work (AI-assisted velocity; each plan is decomposed into commit-sized steps).

## Dependency graph

```
04 per-frame constants ──────► 07 clustered lighting (cluster tables ride the per-frame block)
05 HDR scene target ─────────► 01 IBL benefits; 10 SSR requires it
05/10 shared: scene-depth availability work (build once, documented in 10 §Design, consumed by 05 SSAO)
03 BC6H (optional) ──────────► 01 HDR environment maps (nice-to-have, not blocking)
02 app framework ────────────► 08 widget adoption in demos; 11 scene/state rewrites
08 UI toolkit ───────────────► 11 HUD rewrites
01/05/06 share the 4-backend shader replication cost — batch them (Phase A)
```

## Sequencing

- **Phase A (visual core):** 01 → 05 → 06 — shader-heavy; amortize the four-shader-source replication cost.
- **Phase B (performance):** 04 → 07 — the constant-buffer split is a structural prerequisite for cluster tables.
- **Phase C (assets):** 03 → 09 — texture pipeline first; it unblocks real-world content.
- **Phase D (platform):** 02 → 08 → 11 — framework, then widgets, then the demo pass as the proof.

Each phase ends with a full no-skip-flags `./scripts/build_viper_unix.sh` run.

## Cross-cutting constraints (apply to every plan)

1. **Four shader sources.** Any shading change lands in all of: GLSL (`backend/vgfx3d_backend_opengl_shaders.inc`), HLSL (`backend/vgfx3d_backend_d3d11_shaders.inc`), MSL (embedded in `backend/vgfx3d_backend_metal.m`), and the CPU rasterizer (`backend/vgfx3d_backend_sw_raster.inc`). On macOS only the Metal + software backends compile and run; OpenGL/D3D11 TUs do not build locally. Implement all four, verify Metal + SW locally, and record a Windows/Linux verification waiver for the other two (the established waiver pattern from `misc/plans/game/3dnextlevel2/`).
2. **Zero external dependencies — no exceptions, no libraries, ever.** Viper is a 100% from-scratch project. Every algorithm in these plans (Zstd, Basis/UASTC, meshopt, all BCn/ETC/ASTC block decoders, IBL prefiltering, SSR/TAA/SSAO shaders, UI widgets) is implemented in-tree from the published specification or math — never vendored, never linked, never copied from external projects. External tools (e.g. `toktx`, `gltfpack`) may be used *offline* to author test fixtures only; no external code, headers, or binaries enter the build or runtime. Each plan restates this as a hard gate; if a piece can't be confidently implemented from scratch within budget, it degrades to a graceful recoverable "unsupported" — it is never solved with a dependency. Precedent: the in-repo DEFLATE inflate (`src/runtime/io/rt_compress.c`) and the from-scratch BC7/ETC2/ASTC decoders (`assets/rt_textureasset3d_codecs.inc`).
3. **100% cross-platform.** Platform checks go through `src/common/PlatformCapabilities.hpp` / `src/runtime/rt_platform.h`, never raw `_WIN32`/`__APPLE__` outside approved adapters. Run `./scripts/lint_platform_policy.sh` for platform-sensitive work.
4. **Spec/ABI discipline.** New public runtime surface (runtime.def) requires: RT_FUNC + RT_CLASS/RT_METHOD/RT_PROP pairs, globally unique class leaf names, `./scripts/check_runtime_completeness.sh`, surface-audit tests green, and `scripts/source_health_baseline.tsv` bumps for new `rt_*.c` files. Runtime C ABI surface changes follow the ADR process (`docs/adr/`).
5. **Determinism.** VM and native outputs must match. Prefer CPU-side computation (light binning, cubemap prefiltering) over GPU compute for anything whose results feed observable behavior.
6. **Testing.** Every plan ships fail-before/pass-after tests. Visual features add golden probes (baseline pattern: `examples/3d/baselines/`, e.g. `walk_min_software.png`). Regenerate goldens via the documented flow only when the change is intentional.
7. **Gate gotcha.** The build script's ctest run uses `-LE slow` — explicitly run `ctest --test-dir build -L slow --output-on-failure` before declaring any phase done, plus `-L graphics3d`, `-L canvas3d`, and the surface audits after runtime.def changes.

## Verification policy

Per plan: the plan's own §7 gates. Per phase: full build + full ctest + `-L slow` + platform lint. Perf items (04, 07) record before/after numbers from the existing Canvas3D diagnostics properties (draw calls, upload bytes) on `examples/3d/openworld_slice/`. Visual items land with a golden probe rendered on both Metal and software backends.
