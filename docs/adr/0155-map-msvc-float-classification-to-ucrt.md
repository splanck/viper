---
status: active
audience: contributors
last-verified: 2026-07-23
---

# ADR 0155: Map MSVC Float Classification to UCRT

## Status

Accepted

## Context

Zanna's in-tree Windows native linker resolves a deliberately fixed set of
dynamic symbols to their owning system DLLs. Current MSVC toolsets can lower
single-precision finite-value checks in the native Zanna Studio build to the
`_fdtest` helper. Release optimization can also lower related classification
checks to `_fdclass`. UCRT exports both helpers, but the dynamic-symbol policy
did not recognize the compiler-emitted spellings. As a result, otherwise valid
Studio builds failed after compilation with unresolved symbols that had no DLL
mapping.

This mapping is a cross-layer dependency between compiler-emitted object code
and the Windows system runtime, so it must be recorded explicitly.

## Decision

The dynamic-symbol policy recognizes the exact `_fdtest` and `_fdclass`
symbols only for Windows. The Windows import planner maps them to
`ucrtbase.dll` for release runtimes and `ucrtbased.dll` for debug runtimes,
using the same selection policy as the planner's existing UCRT symbols.

Both helpers remain Windows-only. Linux and macOS planners must not recognize
or import them.

## Consequences

- Native Zanna Studio and demo links accept MSVC's single-precision
  classification helper without adding a product dependency.
- The system UCRT remains the sole owner of the implementation; Zanna does not
  duplicate CRT behavior.
- Platform-import tests pin both the DLL owner and Windows-only scope.
- IL, verifier rules, runtime C ABI, and serialized formats are unchanged.

## Alternatives Considered

- Rewriting every finite-value check to discourage MSVC from emitting
  these helpers was rejected because code-generation choices can vary by
  compiler version and optimization level.
- Adding in-tree implementations was rejected because UCRT already defines the
  ABI and ships on supported Windows systems.
