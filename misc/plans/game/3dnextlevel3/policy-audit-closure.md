# NL3-034 Policy Audit Closure

Date: 2026-05-31

Scope: local 3dnextlevel3 slices through NL3-034 on macOS arm64 with graphics,
audio, GUI, display, and native AArch64 linking enabled.

## Results

- Platform policy is clean for changed files:
  `./scripts/lint_platform_policy.sh --strict --changed-only`.
- Host smoke is green after building the linker smoke targets:
  `cmake --build cmake-build-debug --target test_linker_elf_exe_writer test_linker_platform_import_planners test_linker_runtime_import_audit --parallel 8`
  followed by `./scripts/run_cross_platform_smoke.sh --build-dir cmake-build-debug`.
- The smoke script now respects capability-disabled surface tests:
  `test_rt_canvas_unavailable` runs only in no-graphics builds and
  `test_rt_audio_unavailable` runs only in no-audio builds. Normal
  graphics/audio builds still run `test_rt_graphics_surface_link` and
  `test_rt_audio_surface_link`.
- The smoke script now skips CTest entries with missing executables in partial
  build trees, then runs all runnable matches one by one. The final local pass
  had no missing executable skips after the linker targets were built.
- The platform-policy lint now prints filenames for raw macro hits when `rg` is
  available, so future failures point at the offending file and line.
- Dependency audit found no new third-party dependency mechanism or submodule.
  The 3dnextlevel3 diff from checkpoint `6e3484f84` touches only
  `src/tests/CMakeLists.txt` and scripts in dependency-relevant paths; package
  manager manifests, `.gitmodules`, and top-level dependency config are
  unchanged. Repository-wide search found only existing `find_package` uses
  (`Git`, `Threads`, `X11`, `ALSA`, and installed `Viper` smoke fixtures), and
  `git submodule status` is empty.
- ADR/IL audit found registry-only IL runtime surface edits:
  `src/il/runtime/runtime.def` and
  `src/il/runtime/RuntimeSurfacePolicy.inc`. No 3dnextlevel3 slice changed VM
  execution, native codegen lowering, IL opcodes, verifier rules, or linker
  semantics. `docs/adr/0004-graphics3d-runtime-surface-expansion.md` records the
  registry-only policy and keeps future semantic IL/VM/native changes gated on a
  dedicated ADR.

## Local Smoke Coverage

The passing `run_cross_platform_smoke.sh` lane ran:

- `smoke_term_basic`
- `smoke_basic_oop`
- `zia_smoke_paint`
- `zia_smoke_vipersql`
- `zia_smoke_chess`
- `test_rt_graphics_surface_link`
- `test_rt_audio_surface_link`
- `test_linker_elf_exe_writer`
- `test_linker_platform_import_planners`
- `test_linker_runtime_import_audit`
- `native_smoke_3dbowling_build_arm64`
- `native_smoke_xenoscape_start_arm64`
- `native_smoke_xenoscape_action_names_arm64`
- `native_smoke_viperide_completion_arm64`
- `zia_smoke_viperide`
- `zia_smoke_3dbowling`
- `zia_smoke_3dscene`
- `zia_smoke_3dbaseball`
- `zia_smoke_xenoscape`
