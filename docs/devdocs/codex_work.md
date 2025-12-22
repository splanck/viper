# Codex Work Plan - Viper

Last updated: 2025-12-21

## Objectives
- Produce a full LOC/SLOC report by subsystem and language.
- Review C/C++ hotspots and document cleanup/refactor actions.
- Resolve any TODOs in C/C++ sources (none found in `src/` or `include/`).
- Fix issues found during the audit (runtime registry, archive directory lookup, ViperLang parsing/typing).
- Update docs/tests where behavior changed.
- Keep the tree green (build + tests passing).

## Plan

### 1) Metrics + Reports
- [x] Regenerate `docs/devdocs/LOC_REPORT.md` with current `cloc` numbers.
- [x] Update `docs/devdocs/CODE_HOTSPOTS_REPORT.md` with top C/C++ hotspots and actions.

### 2) Audit Fixes (C/C++)
- [x] Runtime registry cleanup: remove duplicate entries, restore `rt_str_eq`, align terminal helpers with manual lowering.
- [x] Runtime archive directory lookup: normalize trailing slash handling for `Has`/`Info`.
- [x] ViperLang parser/sema: named-arg lookahead, postfix `?` vs ternary disambiguation, identifier-like decl names, allow `new` on value types.

### 3) Docs + Tests
- [x] Update ViperLang reference for optionals, match, indexing, and map key rules.
- [x] Update viperlib IO/input docs for archive name rules and gamepad backends.
- [x] Update generated-files guide for `rtgen` workflow.
- [x] Verify tests cover archive directories and ViperLang optional/match behavior.

### 4) Validation
- [x] `cmake -S . -B build`
- [x] `cmake --build build -j`
- [x] `ctest --test-dir build --output-on-failure`

## Progress Log
- 2025-12-21: Regenerated LOC/SLOC report (`docs/devdocs/LOC_REPORT.md`).
- 2025-12-21: Refreshed hotspot report (`docs/devdocs/CODE_HOTSPOTS_REPORT.md`).
- 2025-12-21: Fixed runtime registry duplication and terminal lowering alignment.
- 2025-12-21: Normalized archive directory lookup to accept trailing slashes.
- 2025-12-21: Updated ViperLang parser/sema disambiguation (`?` vs ternary, named args, value-type `new`).
- 2025-12-21: Updated docs for ViperLang reference, viperlib IO/input, and generated-files guidance.
- 2025-12-21: Full build + test run completed (932 tests).
