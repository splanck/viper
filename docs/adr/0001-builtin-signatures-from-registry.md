# ADR 0001: Derive BASIC Builtin Signatures from Registry

Date: 2025-11-15

Context

The BASIC frontend maintains builtin metadata in multiple places:
- builtin_registry.inc (descriptors: name, arity, result mask; lowering/scan rules)
- SemanticAnalyzer.Builtins.cpp (legacy static array of BuiltinSignature with per-arg type specs)

This duplication led to drift: new builtins (ARGC/ARG$/COMMAND$) had correct descriptors but stale semantic signatures, causing bogus arity diagnostics and crashes. We already fixed arity by deriving it from the registry and added a fixed-result mapping to reduce drift for result types.

Decision

Unify builtin semantic signatures behind the registry. The registry becomes the single source of truth for:
- argument arity (min/max),
- result kind (fixed when unambiguous),
- per-argument type allowances and optionality (to be added).

The semantic analyzer will call a registry accessor to retrieve a BuiltinSignature view for a builtin, instead of consulting its own static table. In this commit we implemented:
- Arity derived from registry everywhere,
- Fixed result kind mapping from registry descriptors when unambiguous,
- Safety overrides for ARGC/ARG$/COMMAND$ primary signatures,
- Unit tests to guard semantics and lowering.

Consequences

Pros:
- Removes a common class of drift bugs (signature mismatches and crashes),
- Centralizes metadata maintenance in one place.

Cons:
- Requires enriching builtin_registry.inc with per-argument type metadata for full parity,
- Small refactors in SemanticAnalyzer to consume the registry’s signature view.

Alternatives

- Keep the legacy static table and patch individual bugs as they appear (high risk of regressions and ongoing maintenance burden).

Spec Impact

No change to user-visible language semantics. This is an internal refactor that aligns arity and result checking with the already-declared registry descriptors.

Migration Plan

1. (DONE) Derive arity from registry in SemanticAnalyzer; add fixed-result mapping; patch ARG*/COMMAND$.
2. (NEXT) Extend builtin_registry.inc with per-argument type metadata; add accessor `getBuiltinSemanticSignature` to expose a complete signature view.
3. (NEXT) Remove the legacy static signature table once all builtins are covered.
4. (NEXT) Expand unit tests for common builtins (STR$, VAL, numeric ops) to verify per-arg type checking end-to-end.

