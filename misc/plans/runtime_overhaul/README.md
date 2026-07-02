# Runtime API Overhaul Plan

Date: 2026-07-02

Scope: the public `Viper.*` runtime API as emitted by
`./build/src/tools/viper/viper --dump-runtime-api`, with source-of-truth
cross-checks against `src/il/runtime/runtime.def`, runtime docs, generated
runtime class metadata, and runtime stub behavior.

This directory is a long-term planning package for making the public runtime API
consistent, modern, understandable, and powerful without hiding important
capabilities. It is intentionally split into decision and workstream documents
so individual changes can move independently while still converging on one API
shape.

## Current Baseline

Fresh local dump used for this plan:

```sh
./build/src/tools/viper/viper --dump-runtime-api > /tmp/viper_runtime_api_current.json
```

Measured surface:

| Item | Count |
|---|---:|
| Public functions | 6310 |
| Runtime classes | 461 |
| Public class properties | 1471 |
| Public class methods | 4649 |

Current mechanical health is good: no public `ptr` signatures are emitted, no
public alias surface remains in the live dump, and previous runtime audit lanes
pass. The remaining issues are mostly policy, naming, failure semantics,
namespace ownership, duplication, and incomplete/stub behavior.

Second-round planning was intentionally source-only because concurrent codegen
work was in progress and this task was document-only. Before implementation,
refresh the dump snapshot and reconcile it with `runtime.def` so counts and
generated metadata are current.

## Document Map

| File | Purpose |
|---|---|
| [00-current-surface-inventory.md](00-current-surface-inventory.md) | Live measurements, hotspots, and evidence anchors. |
| [01-api-design-policy.md](01-api-design-policy.md) | The proposed long-term API rules and decisions. |
| [02-signature-schema-and-generator-plan.md](02-signature-schema-and-generator-plan.md) | Public signature dialect, JSON schema, and generator fixes. |
| [03-namespace-and-domain-model-plan.md](03-namespace-and-domain-model-plan.md) | Namespace ownership, 2D/3D/game/input/asset boundaries. |
| [04-naming-and-rename-backlog.md](04-naming-and-rename-backlog.md) | Concrete naming decisions and rename backlog. |
| [05-failure-nullability-and-lifecycle-plan.md](05-failure-nullability-and-lifecycle-plan.md) | `Try*`, null, `Option`/`Result`, and resource lifecycle rules. |
| [06-properties-constructors-and-factories-plan.md](06-properties-constructors-and-factories-plan.md) | Property mutability, setters, constructors, factories, and large classes. |
| [07-domain-workstreams.md](07-domain-workstreams.md) | Area-by-area implementation plan. |
| [08-audit-migration-and-compatibility-plan.md](08-audit-migration-and-compatibility-plan.md) | Audit gates, migration strategy, test lanes, and ADR triggers. |
| [09-documentation-update-plan.md](09-documentation-update-plan.md) | Documentation ownership, stale-doc prevention, examples, generated references, and release notes. |
| [10-second-round-deep-dive.md](10-second-round-deep-dive.md) | Second-pass findings that were missed by the first review, with source anchors and priorities. |
| [11-production-contracts-and-metadata-plan.md](11-production-contracts-and-metadata-plan.md) | Runtime catalog metadata needed for stable, tool-friendly, production APIs. |
| [12-errors-results-and-diagnostics-plan.md](12-errors-results-and-diagnostics-plan.md) | Unified error, result, diagnostic, and trap-state plan. |
| [13-security-and-networking-api-plan.md](13-security-and-networking-api-plan.md) | Safe-by-default crypto/networking decisions and migration plan. |
| [14-events-async-and-stateful-results-plan.md](14-events-async-and-stateful-results-plan.md) | Async, callback, event, and "last result" cleanup plan. |
| [15-examples-demos-migration-plan.md](15-examples-demos-migration-plan.md) | Required audit and migration plan for demos, examples, snippets, and sample apps. |

## Target Outcomes

- A single public runtime signature dialect.
- Predictable namespace ownership.
- One canonical place for each concept, especially input keys and 3D assets.
- Consistent spelling, acronym policy, and verb model.
- Clear failure semantics that do not rely on ambiguous `0`, `""`, or `NULL`.
- Writable properties where state is writable, and command methods where work is
  being performed.
- Runtime internals hidden from ordinary user-facing API discovery.
- Capability-disabled builds that fail consistently and explainably.
- A catalog that declares stability, capability, fallibility, units, ownership,
  thread-safety, and docs anchors.
- Safe-by-default crypto/network APIs with legacy and dangerous operations named
  as legacy or unsafe.
- Examples and demos updated as API consumers, not left as stale showcase code.
- Audits that prevent the old drift from coming back.

## Planning Decisions

The decision files in this directory are written as proposed API policy, not
casual suggestions. Runtime surface changes still need the normal project
process when implemented. In particular, changes to the runtime C ABI surface or
public runtime registry should be covered by ADRs per the repository operating
guide.

## Recommended Phase Order

1. **Freeze and snapshot.** Save the current API dump as a review fixture and
   add drift-audit scaffolding before large renames begin.
2. **Fix public signature/schema output.** This is the safest high-value
   foundation and improves every downstream tool.
3. **Adopt naming and failure policy.** Add audits for forbidden abbreviations,
   `Try*` return categories, and public null/sentinel contracts.
4. **Normalize small foundational APIs.** Core, Memory, Collections, Math, Time,
   and Text should be cleaned before graphics/game layers.
5. **Add production metadata.** Class kind, enum/domain, unit, fallibility,
   ownership, thread-safety, capability, and stability data should exist before
   large application-facing migrations.
6. **Resolve duplicate domains.** Input keys, 2D/3D graphics, Game3D assets, and
   GUI/editor large classes should move after the core rules are enforced.
7. **Update docs, examples, demos, and generated catalogs.** Runtime docs and
   runnable samples should be changed in the same implementation slices as the
   API, following the dedicated documentation and examples workstreams.

## Non-Goals

- Do not add external dependencies.
- Do not introduce platform-specific policy exceptions outside approved adapter
  layers.
- Do not preserve old public aliases indefinitely. Temporary aliases are only a
  migration mechanism, and hidden/internal compatibility is preferred during
  pre-alpha cleanup.
