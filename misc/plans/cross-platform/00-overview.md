# Cross-Platform Maintenance Plan Set

## Goal

Reduce the "fix one platform, break another" loop by moving platform knowledge out of scattered conditionals and into a few enforceable, testable layers.

This plan set is based on the current Viper tree, especially:

- `src/runtime/rt_platform.h`
- `src/codegen/common/RuntimeComponents.hpp`
- `src/codegen/common/linker/NativeLinker.cpp`
- `src/common/RunProcess.cpp`
- `src/tests/cmake/TestHelpers.cmake`
- `src/lib/graphics/CMakeLists.txt`
- `src/lib/audio/CMakeLists.txt`
- `docs/cross-platform/platform-checklist.md`

## Main Friction Points

| # | Friction | Why it keeps causing regressions |
|---|----------|----------------------------------|
| 1 | Raw platform macros are scattered across runtime, codegen, tools, REPL, and tests | Platform policy is duplicated instead of named once |
| 2 | Graphics/audio capability can silently downgrade by host machine | Different developers build different products without noticing |
| 3 | Disabled or stubbed runtime surfaces can drift from declared APIs | A feature works on the fully enabled host but breaks on a stubbed build |
| 4 | Test skipping is visible only in code, not as a first-class coverage signal | CI can say "passed" while significant host-specific gaps remain |
| 5 | Local smoke coverage is host-shaped, not capability-shaped | Regressions show up only after a push or on a different machine |
| 6 | Some cross-platform metadata is still hand-maintained | Runtime/archive/linker mappings can drift silently |
| 7 | A few large common files mix platform-specific logic directly into shared flows | Fixes in one platform's path raise risk in the others |
| 8 | Tooling still shells out through host shell semantics in key places | Quoting, cwd, env, and stderr behavior drift by OS |

## Design Principles

1. One vocabulary for platform and capability decisions.
2. Fail fast when the requested product cannot be built.
3. Prefer generated manifests over "keep this in sync" comments.
4. Make skipped coverage explicit and budgeted.
5. Validate real host capabilities locally; do not rely on fake cross-platform parsing tricks.
6. Keep platform-specific logic in platform-specific files where possible.

## Plan Inventory

| Plan | Theme | Priority |
|------|-------|----------|
| `01-rt-platform-convention.md` | Shared platform and capability conventions across C, C++, tests, and docs | Highest |
| `02-cmake-platform-gate.md` | Fail-fast configure/build capability policy | Highest |
| `03-merge-build-scripts.md` | Canonical build entrypoints and shared script logic | High |
| `04-stub-checker.md` | Disabled-surface completeness audits | High |
| `05-test-skip-tracking.md` | Make platform skips visible and intentional | High |
| `06-platform-coverage-lint.md` | Lint raw platform macro usage and adapter boundaries | High |
| `07-cross-platform-smoke-test.md` | Capability-based local smoke slices | High |
| `08-generated-runtime-manifest.md` | Generate runtime/archive/component metadata | Medium |
| `09-native-subprocess-layer.md` | Replace shell-string process launching with native subprocess APIs | Medium |
| `10-linker-platform-decoupling.md` | Split native linker common flow from per-platform policy | Medium |

## Recommended Order

### Phase 1: Policy And Fail-Fast

Land these first because they reduce accidental drift immediately and are comparatively low risk.

1. `01-rt-platform-convention.md`
2. `02-cmake-platform-gate.md`
3. `03-merge-build-scripts.md`

### Phase 2: Visibility And Coverage

These make cross-platform gaps obvious before they become regressions.

4. `04-stub-checker.md`
5. `05-test-skip-tracking.md`
6. `06-platform-coverage-lint.md`
7. `07-cross-platform-smoke-test.md`

### Phase 3: Structural Cleanup

These are the highest leverage for long-term maintenance, but they touch deeper architecture.

8. `08-generated-runtime-manifest.md`
9. `09-native-subprocess-layer.md`
10. `10-linker-platform-decoupling.md`

## Success Metrics

- A missing host dependency fails configure with a clear message instead of silently downgrading features.
- New raw `_WIN32` / `__APPLE__` / `__linux__` checks stop appearing outside approved adapter files.
- Disabled-surface link gaps are caught by dedicated audits on the developer's host.
- CTest output reports real skips instead of silent passes.
- Each host has a short smoke command that validates the expected capability bundle.
- Runtime component/archive mapping is generated or machine-checked instead of hand-synced.
- Common platform-sensitive chokepoints, especially subprocess and native linker code, are split into clearer ownership boundaries.
