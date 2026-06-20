# Code Coverage Measurement

**Status:** Verified real (no coverage instrumentation exists anywhere)
**Area:** root `CMakeLists.txt`, `scripts/`
**Effort:** S
**Roadmap fit:** v0.2.x hardening — **this is the highest-leverage item**: it turns every
other "thin coverage" claim from a guess into a measurement.

## Problem

A grep of the build for `coverage`/`gcov`/`llvm-cov`/`-fprofile-instr`/`--coverage`
returns nothing. `.gitignore` reserves a `coverage/` path but it is never populated. So
coverage is **inferred, never measured** — exactly the blind spot that let the original
review over- and under-state several subsystems.

## Goal & scope

- **In:** A `VIPER_ENABLE_COVERAGE` CMake option (Clang source-based coverage) and a
  local `scripts/coverage.sh` lane that builds instrumented, runs the suite, and emits a
  **per-subsystem** HTML + summary report. **Visibility only — no failing gate initially.**
- **Out:** Hard coverage thresholds in CI (add later, once a baseline is known); GitHub
  Actions changes (prohibited this phase — this is a local script).

## Design

Clang is canonical, so use source-based coverage:
`-fprofile-instr-generate -fcoverage-mapping` at compile/link, `LLVM_PROFILE_FILE` to
collect `.profraw`, then `llvm-profdata merge` + `llvm-cov show/report`. Region/branch
coverage is far more accurate than line-only gcov and matches the toolchain.

## Implementation steps

1. `CMakeLists.txt`: add
   ```cmake
   option(VIPER_ENABLE_COVERAGE "Source-based coverage (Clang)" OFF)
   if(VIPER_ENABLE_COVERAGE)
     add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
     add_link_options(-fprofile-instr-generate)
   endif()
   ```
   Guard so it only engages with Clang.
2. `scripts/coverage.sh`: configure with the option, build, run `ctest` with a
   per-test `LLVM_PROFILE_FILE` pattern, `llvm-profdata merge`, then `llvm-cov report`
   (summary) + `llvm-cov show --format=html` (drill-down). Emit a **per-directory**
   rollup keyed to `src/runtime/<module>`, `src/frontends/*`, `src/codegen/*`, etc.
3. `.gitignore`: ensure `*.profraw`, `*.profdata`, and the `coverage/` output are ignored.
4. Print a ranked "lowest-covered subsystems" table so the output directly feeds the
   backlog.

## Tests

- A smoke that `scripts/coverage.sh` runs end-to-end and produces a non-empty report
  (can be a `slow`-labelled, opt-in ctest or a CI-script self-check).
- Sanity assertion: a known well-tested subsystem (IL core) reports high coverage and a
  known-thin one (audio, pre-`audio-effects-dsp.md`) reports low — validates the pipeline.

## Documentation

- Add a "Measuring coverage" section to `docs/testing.md`: how to run the lane, read the
  report, and where the per-subsystem rollup lives.
- Note in the contributor guide that new subsystems should be checked against the rollup.

## Cross-platform

Clang source-based coverage works on macOS (Apple Clang) and Linux (clang++). Windows
(clang-cl) is possible but secondary; scope the lane to the two canonical Unix dev
platforms first and note Windows as a follow-up.

## Risks / open questions

- **Instrumented runtime is slower** — keep the lane opt-in; don't make it the default
  build.
- **Flaky/excluded tests** skew numbers — exclude device/display-gated tests from the
  coverage run or annotate them so the rollup isn't misread.
