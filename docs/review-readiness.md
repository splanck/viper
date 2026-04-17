---
status: active
audience: contributors
last-verified: 2026-04-16
---

# Review Readiness Checklist

Use this checklist before external review of Viper changes. It separates what
the local tree can verify immediately from cross-platform claims that require
additional hosts or CI.

## Required Local Checks

```bash
cmake -S . -B build
cmake --build build -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"
ctest --test-dir build --output-on-failure
./scripts/audit_runtime_surface.sh --summary-only --build-dir=build
./scripts/lint_platform_policy.sh --strict
```

For runtime-signature or runtime-header changes, also run:

```bash
ctest --test-dir build --output-on-failure -R 'runtime_surface|rtgen|runtime_name_map|runtime_classes_catalog'
```

For native/codegen changes, run the host-supported native smoke slice and record
the host architecture in the review notes. That smoke slice now includes the
archive-wide runtime import audit (`test_linker_runtime_import_audit`), so a
passing run is evidence that every built runtime/support archive member has an
explicit host-native import classification.

## Platform Claims

Do not claim full platform parity from a single macOS run. Use this language:

| Claim Type | Acceptable Evidence |
|------------|---------------------|
| macOS behavior | Local macOS build/test output |
| Linux behavior | Linux build/test output or CI link |
| Windows behavior | Windows build/test output or CI link |
| Cross-platform policy compliance | `lint_platform_policy.sh --strict` plus affected-platform tests |
| Runtime surface compatibility | `audit_runtime_surface.sh` plus focused runtime tests |

If evidence is missing, state the gap explicitly in the review summary.

## High-Risk Areas

| Area | Why It Is High Risk | Expected Review Posture |
|------|---------------------|--------------------------|
| Runtime signatures | Frontends, VM, bytecode VM, and native codegen all depend on exact ABI shape | Require runtime.def, header, implementation, and generated registry alignment |
| Bytecode VM | `BytecodeVM.cpp` still owns dispatch, frames, EH, runtime calls, debug, and state | Prefer small behavioral changes with focused tests |
| IL optimizer | Some passes remain in rehab pipelines because production workloads exposed correctness issues | Do not promote disabled passes without equivalence tests |
| Graphics stubs | Disabled graphics builds must trap deterministically while pure helpers still work | Require explicit stub contract tests |
| Platform backends | Cocoa, X11, Win32, Metal, D3D11, OpenGL differ in event, pixel, and ABI details | Require platform-specific validation when touched |

## Documentation Hygiene

- Update codemaps when ownership boundaries change.
- Remove or rewrite stale bug ledgers instead of leaving fixed issues described
  as current behavior.
- Keep release notes historical, but make current guides describe current
  behavior and current validation status.
