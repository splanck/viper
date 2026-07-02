# Tooling, Docs, Schema, And Audits

The runtime API catalog should be a source of truth for tools, docs, and
migration diagnostics. Today much of the metadata is inferred in
`src/tools/viper/main.cpp`; that must change before public freeze.

## Catalog Schema

Add explicit fields:

- `canonical_name`;
- `stability`;
- `migration_target`;
- `deprecated_since`;
- `planned_removal`;
- `capabilities`;
- `availability`;
- `fallibility`;
- `error_domain`;
- `ownership`;
- `nullable`;
- `thread_safety`;
- `units`;
- `enum_domain`;
- `class_kind`;
- `docs_anchor`;
- `examples`;
- `unsafe_reason`;
- `waiver_id`.

Fields can be generated from `runtime.def` annotations or a sidecar table, but
they should not be guessed by name heuristics.

## Docs Anchors

Decision: every public row gets a real docs target before freeze.

Two acceptable models:

1. Generate reference docs from the runtime catalog and make `docs_anchor`
   point to generated entries.
2. Require handwritten docs headings for every stable row and audit them.

Preferred model: generate reference docs and link handwritten guides to the
generated reference. Handwritten guides should teach concepts, not duplicate
thousands of signatures.

## Required Audits

Add or extend audits for:

- duplicate public C symbol plus signature;
- duplicate canonical names;
- same class/name/arity method collisions;
- unresolved docs anchors;
- unknown ownership on stable object/string APIs;
- bare `obj` on stable concrete handles;
- public `ptr`;
- suffix nullable syntax;
- side-channel stable APIs;
- sentinel stable APIs;
- capability-dependent stable APIs without availability metadata;
- no-op/fake-value stubs in stable capability APIs;
- high arity above threshold;
- boolean setter/getter type mismatch;
- non-PascalCase public leaves;
- legacy row missing migration target;
- examples using legacy names.

## Snapshot Testing

Create a checked-in API snapshot fixture after canonical decisions are made.
The snapshot should be reviewed like ABI:

- changes require intentional approval;
- diffs group additions, removals, signature changes, metadata changes, and
  stability changes;
- CI reports user-facing breakage.

## Migration Tooling

Add tool support for API migration:

- `viper runtime-api --json` or keep `--dump-runtime-api` with richer schema;
- `viper explain-runtime <name>` for a public row;
- `viper migrate-api --check <file|dir>` to detect legacy names;
- diagnostics with fixits when a canonical replacement is mechanical.

## Documentation Work

Docs should be reorganized into:

- conceptual guides by domain;
- generated reference pages by namespace/class;
- migration guide from legacy names to canonical names;
- capability matrix;
- error and result guide;
- ownership/lifetime guide;
- unsafe API guide.

## Examples And Demos

Examples are API consumers. They must be audited and migrated in the same
phases as the public API:

- no legacy names;
- no side-channel error flow in canonical examples;
- no raw sentinel checks where `Option`/`Result` exists;
- no disabled capability calls without probes;
- no unsafe APIs unless the example is specifically about unsafe/runtime work.

