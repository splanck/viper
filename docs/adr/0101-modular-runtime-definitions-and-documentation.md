---
status: active
audience: contributors
last-verified: 2026-07-13
---

# ADR 0101: Modular Runtime Definitions and Authored API Documentation

Date: 2026-07-11

## Status

Accepted

## Context

`src/il/runtime/runtime.def` is the canonical registry for the public runtime
surface. It currently contains more than seventeen thousand lines and defines
over seven thousand callable rows and five hundred runtime classes. Adding
authored class documentation directly to the monolith would make the registry
harder to navigate and review.

The generated runtime class catalog already feeds the Zia and BASIC frontends,
language services, and `zanna --dump-runtime-api`, but it does not carry class
documentation. Zia therefore contains a small hard-coded documentation table,
BASIC emits empty documentation, and the public API dump can expose only
inferred documentation anchors.

The registry must remain dependency-free, deterministic, cross-platform, and
usable by its existing X-macro consumer.

## Decision

- Keep `src/il/runtime/runtime.def` as the single canonical entry point, but
  make it a manifest that includes domain-oriented `.def` fragments with
  quoted, relative `#include` directives.
- Teach `rtgen` to resolve those includes itself. Includes are relative to the
  including file, may not escape the definition root, and are rejected when
  missing, duplicated, cyclic, or placed inside an open class block. Each
  fragment must leave class-block state balanced.
- Preserve declaration order across includes. Fragmentation is an
  organizational change and must not change generated registry semantics.
- Treat structured `/// @summary` and `/// @details` blocks immediately before
  `RT_CLASS_BEGIN` as authored registry data. Summaries are one-line plain
  text; details are Markdown and may span paragraphs.
- Carry class documentation through the generated runtime class catalog and
  expose it to frontends, language services, Zanna Studio, and
  `zanna --dump-runtime-api`.
- Add a schema-versioned `documentation` object to each class in the public API
  dump. It contains `summary`, `details`, and `format: "markdown"`.
- Generate exhaustive runtime API reference pages from the parsed canonical
  registry, using the same anchor rules as the live API dump.
  Conceptual guides remain handwritten, but canonical class descriptions and
  signatures must not be maintained in a second handwritten table.
- Make missing documentation observable through generator audits and tests.
  Every public runtime class requires a non-empty summary and details block.
- Runtime surface validation must consume parsed/generated registry data rather
  than scrape only the root manifest text.

## Fragment Layout

The root manifest includes top-level domain manifests under
`src/il/runtime/defs/`. Large domains use nested leaf fragments. Leaf files are
kept at a reviewable size and group related functions and class declarations;
the initial migration preserves the existing declaration order exactly.

Definition fragments deliberately have no include guards because the root is
also consumed as an X-macro table. `rtgen` provides duplicate-include
diagnostics instead.

## Documentation Contract

Example:

```cpp
/// @summary Reads, writes, and inspects filesystem files.
/// @details
/// Provides whole-file text and byte operations, metadata queries, copying,
/// moving, and deletion.
RT_CLASS_BEGIN("Zanna.IO.File", File, "obj", none)
```

The documentation block applies only to the next class declaration. Orphaned,
duplicate, or malformed fields are generator errors. The generated catalog
stores static string views; it does not add a runtime C ABI symbol or alter
object layouts visible to runtime programs.

## Consequences

- Runtime API changes and their canonical documentation are reviewed together.
- Zia, BASIC, Zanna Studio, generated reference docs, and API tooling consume the
  same descriptions.
- Adding a fragment requires no external package or preprocessing tool.
- `rtgen`, CMake dependency tracking, tests, and source-health tooling must all
  understand the definition set rather than assuming one physical file.
- Authored documentation increases compiler/tool binary data modestly, but does
  not enter the standalone C runtime library or generated user programs.

## Alternatives

- Keep one monolithic file. Rejected because documentation growth would make an
  already oversized registry less maintainable.
- Use YAML, TOML, or JSON. Rejected because multiline prose remains awkward in
  JSON and the other formats would require another parser or dependency.
- Store documentation in a sidecar table keyed by class name. Rejected because
  it recreates synchronization and review drift.
- Add long string arguments to `RT_CLASS_BEGIN`. Rejected because it creates
  unreadable macro calls and breaks the existing X-macro shape.

## Spec Impact

This changes a cross-layer metadata dependency and the agent-facing runtime API
dump schema, so an ADR is required. It does not change IL grammar, opcodes,
verifier rules, runtime C ABI, or language semantics.
