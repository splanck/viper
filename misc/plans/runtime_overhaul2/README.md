# Runtime API Overhaul 2

Date: 2026-07-02

This directory is the second, independent planning package for stabilizing the
public `Viper.*` runtime API before release. It is based on a fresh live dump
from the current checkout and a deeper source review of `runtime.def`,
`rtgen`, runtime class indexing, generated API metadata, and disabled/stub
implementations.

This package is deliberately decision-oriented. The goal is not only to list
problems, but to define the API shape Viper should converge on and the gates
that keep the public surface from drifting again.

## Baseline

Commands used:

```sh
build/install/bin/viper --dump-runtime-api > /tmp/viper_runtime_api_overhaul2.json
build/src/rtgen --audit --summary-only src/il/runtime/runtime.def
```

Observed public surface:

| Item | Count |
|---|---:|
| Functions | 6,624 |
| Classes | 474 |
| Properties | 1,598 |
| Methods | 4,836 |

`rtgen` reported `6624 functions, 474 classes, 7335 header declarations` and
the audit passed. No public signature currently exposes `ptr`.

## Document Map

| File | Purpose |
|---|---|
| [00-investigation-snapshot.md](00-investigation-snapshot.md) | Current measurements, evidence, and highest-risk findings. |
| [01-api-governance-and-release-gates.md](01-api-governance-and-release-gates.md) | Stabilization rules, ADR triggers, release gates, and compatibility policy. |
| [02-naming-namespace-decisions.md](02-naming-namespace-decisions.md) | Naming style, namespace ownership, acronym policy, and domain decisions. |
| [03-alias-rename-migration-plan.md](03-alias-rename-migration-plan.md) | Canonicalization plan for aliases, duplicate C symbols, and legacy migrations. |
| [04-signatures-types-ownership.md](04-signatures-types-ownership.md) | Public signature grammar, typed handles, ownership, nullability, and units. |
| [05-failure-errors-nullability.md](05-failure-errors-nullability.md) | `Result`, `Option`, trap, sentinel, and side-channel failure policy. |
| [06-capabilities-stubs.md](06-capabilities-stubs.md) | Capability metadata and policy for disabled/stub implementations. |
| [07-class-property-method-shape.md](07-class-property-method-shape.md) | Class kinds, properties, constructors, factories, methods, and overloads. |
| [08-domain-decision-backlog.md](08-domain-decision-backlog.md) | Domain-by-domain API decisions and cleanup backlog. |
| [09-tooling-docs-schema-plan.md](09-tooling-docs-schema-plan.md) | Runtime catalog schema, docs anchors, audits, and generated references. |
| [10-roadmap.md](10-roadmap.md) | Phased implementation plan with dependencies and exit criteria. |
| [appendices/audit-tables.md](appendices/audit-tables.md) | Concrete counts and issue tables from the live dump. |
| [appendices/decision-log.md](appendices/decision-log.md) | Compact list of binding proposed decisions. |
| [appendices/release-checklist.md](appendices/release-checklist.md) | Checklist for deciding when the API is ready to freeze. |

## Target API Character

The public runtime should feel:

- Consistent: one canonical way to express each concept.
- Modern: clear names, typed handles, explicit errors, no legacy C-runtime
  leakage in user-facing names.
- Understandable: signatures explain themselves without requiring hidden
  knowledge of sentinel values, side channels, or handle classes.
- Powerful: low-level and unsafe functionality remains available, but it is
  named and surfaced as low-level or unsafe.
- Tool-friendly: the runtime catalog can drive docs, completions, validation,
  migration diagnostics, and compatibility reports without heuristic inference.

## Top-Level Decisions

1. Canonical public names use PascalCase full words. Short aliases such as
   `LeadZ`, `Rotl`, `Fpr`, `Len`, `Cap`, `Norm`, and `Dist` become legacy or
   hidden compatibility names.
2. Public handle signatures use `obj<Viper.Domain.Type>` whenever the type is
   known. Bare `obj` is allowed only for intentionally dynamic values.
3. Public fallible operations return `Result<T>` for operation failure and
   `Option<T>` for ordinary absence. Sentinel and side-channel error APIs are
   legacy.
4. Disabled capabilities must not silently look successful. Constructors and
   operations either return a structured unavailable result or trap with a
   capability diagnostic.
5. API metadata must be declared in runtime definitions or generated sidecar
   tables, not inferred in the CLI dumper.
6. Public docs anchors must resolve to real generated or handwritten reference
   entries before freeze.
7. Runtime C ABI, verifier-facing, IL reference, and workflow changes still
   need ADRs under the repository operating guide.

