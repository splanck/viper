---
status: active
audience: contributors
last-verified: 2026-07-18
---

# ADR 0138: Debug Class-Layout Sidecar for VM Debugger Field Expansion

## Status

Accepted (2026-07-18)

## Context

The VM debugger's structured-variables protocol (varRef/childCount +
paged `variables` expansion, ADR 0009/0012 lineage) expands `List`,
`Seq`, and `Map` locals, but user class instances are leaves: the
Variables panel shows `<object #N>` with no way to inspect fields.

Field inspection needs per-class layout metadata — field names, byte
offsets, and storage kinds — which does not exist at runtime. The
runtime's class registry (`rt_class_info`) carries only type id, qname,
base pointer, and vtable; field access is lowered to raw GEP offsets by
the Zia frontend, and the names die with the `Lowerer`.

Options considered:

1. **Runtime reflection tables** — extend `rt_class_info` (or add an
   `rt_register_class_fields` call) so compiled modules register field
   metadata at init. Rejected: changes the stable runtime C ABI, taxes
   every native binary with metadata it never uses (the debugger is
   VM-only), and requires codegen work in both backends.
2. **IL-level metadata** — carry class layouts in `il::core::Module`.
   Rejected: changes IL structure and serialization for a debugger-only
   concern; the IL spec is normative and this metadata has no execution
   semantics.
3. **Compile-time sidecar (chosen)** — the debug adapter runs in the
   same process that just compiled the program (`zanna run
   --debug-adapter` compiles Zia → IL → VM in-process). The frontend
   exports its already-computed class layouts alongside the module; the
   tool converts them to a VM-owned plain-data table and installs it on
   the debug controller.

## Decision

Add a class-layout sidecar that flows compiler → tool → VM debug
controller, with each layer owning its own plain-data type so no new
inter-layer include edges are created:

- **Frontend** (`il::frontends::zia`): `CompilerOptions.
  captureDebugLayouts` (default off). When set, `CompilerResult.
  debugClassLayouts` is populated after lowering from the `Lowerer`'s
  `ClassTypeInfo` map: one entry per instantiated class, keyed by the
  compiler-assigned runtime class id, carrying the qualified name and
  the flattened field list (inherited first) with name, byte offset,
  storage kind, and semantic type string. Storage kinds are derived
  from `toILType` plus semantic refinement (managed pointer vs raw
  pointer vs weak reference); any field whose storage size does not
  match its IL scalar size (inline aggregates such as fixed arrays) is
  exported as opaque and stays a leaf.
- **VM** (`il::vm`): new plain-data header
  `include/zanna/vm/debug/DebugClassLayout.hpp` defining
  `DebugFieldStorage`, `DebugFieldLayout`, `DebugClassLayout`, and
  `DebugClassLayoutTable` (map from class id). `DebugCtrl` gains
  `setClassLayouts`/`classLayouts`. The stop-time variable store
  (`VmDebugVarStore`) classifies a managed pointer whose positive
  `rt_obj_type_id` appears in the table as an expandable object:
  children are produced by reading frame-owned object memory at
  `base + offset` per storage kind on the paused VM thread — never by
  re-entering the interpreter. Weak fields are resolved through
  `rt_weak_load` (non-retaining). Managed fields recurse through the
  existing `describeValue` path, so objects nest with lists/seqs/maps
  in both directions.
- **Tool** (`src/tools/zanna`): the run pipeline requests capture when
  `--debug-adapter` is active, converts the frontend export to the VM
  table (the tool is the composition root and already includes both
  layers), and passes it to `runDebugAdapter`, which installs it on
  `RunConfig.debug`.

The adapter wire protocol is unchanged: object children are ordinary
`DebugLocalInfo` rows and ride the existing `variables` paging; nested
expandability already serializes (`varRef`/`childCount` on children).
Class ids are positive and monotonically assigned by the frontend;
builtin collection ids are negative, so table lookups cannot collide.

## Consequences

- Class instances expand in the Variables panel (and nested anywhere a
  list/seq/map/object chain reaches), with a compact `{field=value}`
  preview as the summary. No IDE-side changes are required.
- No IL grammar/serialization change, no runtime C ABI change, no
  codegen change, and zero cost unless a debug-adapter session
  requests capture.
- The table is valid precisely because the debugged module and the
  layouts come from the same in-process compile; `zanna run file.il`
  (direct IL) and BASIC debug sessions have no table and keep today's
  leaf behavior.
- Boxed struct-typed fields display as a typed leaf (their payload
  layout is a `StructTypeInfo` keyed by name, not by class id);
  expanding struct payloads is recorded as a follow-up in
  `misc/plans/zannastudio/10-debugger-depth.md`.
