---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0027: Add Contract Metadata to the Runtime API Dump

Date: 2026-07-02

Status: Accepted

## Context

`zanna --dump-runtime-api` is the machine-readable inventory for the public
`Zanna.*` runtime surface. Before this change it exposed names and compact
signatures for global functions, plus classes with properties and methods. That
was enough for simple discovery, but not enough for production tools and docs:
callers still had to infer class kind, fallibility, ownership, capability
requirements, parsed type structure, documentation ownership, and whether class
methods were receiverless or instance-shaped.

The runtime overhaul planning documents under `misc/plans/runtime_overhaul/`
call out this gap as foundational. The metadata must be additive because
existing tools already consume the compact dump.

## Decision

Extend `--dump-runtime-api` to emit schema version 2 metadata while preserving
the existing fields.

Top-level additions:

- `schema_version: 2`
- `signature_dialect: "runtime-def-v1"`

Function additions:

- `kind`
- `owner`
- parsed `return_type`
- parsed `params`
- `stability`
- `capabilities`
- `fallibility`
- `ownership`
- `docs_anchor`

Class additions:

- `kind`
- `owner`
- `class_kind`
- `stability`
- `capabilities`
- `docs_anchor`

Property additions:

- `kind`
- `owner`
- `getter`
- `setter`
- parsed `return_type`
- `stability`
- `capabilities`
- `fallibility`
- `ownership`
- `docs_anchor`

Method additions:

- `kind`
- `owner`
- `target`
- `is_static`
- parsed `return_type`
- parsed `params`
- `stability`
- `capabilities`
- `fallibility`
- `ownership`
- `docs_anchor`

The first implementation derives these fields from existing registry and class
catalog data. It does not add a new `runtime.def` annotation syntax yet and does
not remove or rename any public API. Future implementation slices can replace
heuristics with source-authored metadata while keeping the schema shape stable.

## Consequences

- Existing consumers that read only `version`, `functions`, `classes`, `name`,
  `signature`, `constructor`, `properties`, and `methods` continue to work.
- New docs generators, language servers, compatibility checks, and agents can
  consume structured runtime contracts without parsing all policy from names and
  prose.
- Some metadata is advisory in this first slice. In particular, `docs_anchor`,
  `class_kind`, `fallibility`, `ownership`, units, and domains are inferred
  conservatively until exact row-level annotations are added.
- The dump remains generated from the live binary registry, so it cannot drift
  from compiled runtime metadata.

## Alternatives Considered

- **Wait for exact `runtime.def` annotations before changing the dump.**
  Rejected because tooling needs a stable schema before the full public API
  cleanup can proceed safely.
- **Emit a separate `--dump-runtime-contracts` command.** Rejected because the
  data describes the same public runtime rows and would force consumers to join
  two catalogs.
- **Break old JSON shape and emit only the new schema.** Rejected because the
  project already treats `--dump-runtime-api` as an agent-facing stable surface.

## Spec Impact

No IL opcode, verifier, VM, native-codegen, runtime C ABI, or language syntax
change. This is an additive machine-readable registry contract change for the
`zanna --dump-runtime-api` tool output.
