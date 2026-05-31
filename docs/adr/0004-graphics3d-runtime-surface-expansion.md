# ADR 0004: Graphics3D Runtime Surface Expansion Uses Registry-Only Semantics

Date: 2026-05-31

Status: Accepted

## Context

The 3dnextlevel3 work added and extended many public `Viper.Graphics3D` and
`Viper.Game3D` runtime APIs. Public runtime APIs are registered through
`src/il/runtime/runtime.def` and classified by
`src/il/runtime/RuntimeSurfacePolicy.inc`, so broad 3D API work can touch the IL
runtime registry even when it does not change IL instructions or VM execution.

GATE-009 requires ADR coverage for IL/VM-touching changes. The audit for
3dnextlevel3 found registry and surface-policy edits, but no VM execution,
native codegen, IL opcode, verifier, linker, or language semantic changes.

## Decision

Treat Graphics3D/Game3D runtime-surface additions as registry-only IL touchpoints
when they meet all of these conditions:

- they add or classify extern names, aliases, classes, or runtime signatures for
  existing runtime implementation functions;
- they do not add or reinterpret IL opcodes, IL types, linkage rules, verifier
  rules, VM call/heap/exception behavior, numeric semantics, or native codegen
  lowering;
- they are covered by runtime completeness, ABI/surface, docs-snippet, and
  focused behavior tests for the new API.

Registry-only touchpoints do not need a feature-specific ADR beyond this policy
note. Any future change that alters IL, VM, or native codegen semantics still
requires its own ADR and proof note before the 3D roadmap gate can be closed.

## Consequences

The 3dnextlevel3 runtime-surface edits can close GATE-009 by pointing to this
ADR, the runtime completeness/surface tests, and the NL3 determinism evidence.
The public API remains visible to interpreted and native execution through the
same runtime registry, while the IL and VM semantics remain unchanged.

## Spec Impact

No IL language semantics changed. The impact is limited to additional runtime
catalog entries and policy classifications for public Graphics3D/Game3D APIs.
