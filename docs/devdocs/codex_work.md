# Codex Work Plan - Viper

Last updated: 2025-12-22

## Objectives
- Complete runtime sweep coverage (ViperLang + BASIC) and update `bugs/runtime_test.md`.
- Log runtime/ViperLang bugs discovered during the sweep.
- Refresh runtime docs and root README runtime class list.
- Regenerate LOC/SLOC and C/C++ hotspot reports.
- Keep the tree green (build + tests passing).

## Runtime Sweep Plan (ViperLang + BASIC)

### Goals
- Exercise every Viper.* runtime class, property, and method.
- Prefer ViperLang; fall back to BASIC when ViperLang bindings are missing/buggy.
- Stress test language features and runtime behavior in realistic programs.
- Record all runtime defects in `bugs/runtime_bugs.md` and all ViperLang defects in `bugs/viperlang.md`.
- Track coverage in `bugs/runtime_test.md` and update as tests run.

### Plan
1) **Harness + Coverage**
   - Build a runner that executes ViperLang and BASIC test programs.
   - Parse `COVER:` tags in tests to tick `bugs/runtime_test.md`.
   - Capture stdout/stderr and exit code for report + bug logging.

2) **ViperLang Tests**
   - Create a suite of ViperLang programs to stress the language and available runtime bindings.
   - Validate via output checks (no diagnostics/assert runtime in ViperLang).
   - Log any missing runtime bindings as ViperLang bugs.

3) **BASIC Tests**
   - Build subsystem-focused BASIC programs to cover runtime classes not reachable from ViperLang.
   - Use `Viper.Diagnostics.Assert*` for hard failures.
   - Use real-world mini-programs when possible (file/archive pipeline, CSV/Template formatting, HTTP/TCP/UDP, etc).

4) **Run + Report**
   - Execute full sweep; update `bugs/runtime_test.md`.
   - Document all failures with repros.
   - Produce a comprehensive runtime report.
   - Run full build + tests to keep tree green.

## Plan

### 1) Coverage + Tests
- [x] Extend runtime checklist with non-class namespaces (Box/Diagnostics/Parse).
- [x] Add BASIC sweep coverage for Diagnostics/Parse/Box.
- [ ] Run runtime sweep and update checklist.

### 2) Docs + README
- [x] Update viperlib core/diagnostics/utilities docs for Box + diagnostics + parse notes.
- [x] Update root README runtime class list.

### 3) Metrics + Reports
- [ ] Regenerate `docs/devdocs/LOC_REPORT.md` with current `cloc` numbers.
- [ ] Update `docs/devdocs/CODE_HOTSPOTS_REPORT.md` with top C/C++ hotspots and actions.

### 4) Build + Validation
- [ ] Regenerate runtime registry snapshot with `rtgen`.
- [ ] `cmake -S . -B build`
- [ ] `cmake --build build -j`
- [ ] `ctest --test-dir build --output-on-failure`

### 5) Bugs + Report
- [ ] Update `bugs/runtime_bugs.md` and `bugs/viperlang.md` with findings.
- [ ] Provide runtime sweep report and status summary.

## Progress Log
- 2025-12-22: Extended runtime test matrix with Box/Diagnostics/Parse sections.
- 2025-12-22: Added BASIC runtime sweep coverage for Diagnostics and Parse/Box helpers.
- 2025-12-22: Updated viperlib core/diagnostics/utilities docs and refreshed README runtime class list.
- 2025-12-21: Regenerated LOC/SLOC report (`docs/devdocs/LOC_REPORT.md`).
- 2025-12-21: Refreshed hotspot report (`docs/devdocs/CODE_HOTSPOTS_REPORT.md`).
- 2025-12-21: Fixed runtime registry duplication and terminal lowering alignment.
- 2025-12-21: Normalized archive directory lookup to accept trailing slashes.
- 2025-12-21: Updated ViperLang parser/sema disambiguation (`?` vs ternary, named args, value-type `new`).
- 2025-12-21: Updated docs for ViperLang reference, viperlib IO/input, and generated-files guidance.
- 2025-12-21: Full build + test run completed (932 tests).
- 2025-12-21: Reviewed runtime docs; documented Terminal APIs, keyboard `KEY_UNKNOWN`, and updated runtime README coverage.
- 2025-12-21: Created runtime test matrix (`bugs/runtime_test.md`) and initialized bug logs (`bugs/runtime_bugs.md`, `bugs/viperlang.md`).
- 2025-12-21: Logged runtime sweep plan and tracking requirements.
