# Plan 10 — Debugger Depth (ADR-Gated)

Date: 2026-07-17 · Track: D · Loop: C++ tools + Zia · Size: L

## 1. Objective

Structured object inspection in the debugger — class instances expand to
their fields — plus a real watch-management panel. Today class values render
as opaque `<TypeName>` leaves and watches are managed only through the
command palette.

## 2. ADR first

The phase opens with a **debug reflection and structured variable protocol
ADR** (number re-verified against `docs/adr/` at execution). It pins:

- Adapter protocol extension (`src/tools/zanna/DebugAdapter.{hpp,cpp}`,
  newline-JSON over stdio): a stop event's locals gain stable object
  handles; new `variables` request: handle → { class name, fields [name,
  declared type, value preview, child handle], collection paging (seq/list/
  map already lazily expand — unify under the same request) }.
- Source of field layout: the runtime class metadata already available to the
  VM/tools layer (`RuntimeClasses.hpp` + generated registries) — no new IL
  or verifier surface.
- Handle lifetime: valid between stop and resume; invalidated on
  continue/step; adapter enforces.

## 2a. As-built record (2026-07-18)

- **ADR 0138** (`docs/adr/0138-debug-class-layout-sidecar.md`). Execution
  reconnaissance changed the shape from what §2 sketched, in two ways:
  1. The structured-variable protocol **already existed end-to-end** (varRef/
     childCount on locals, paged `variables` requests, nested refs on
     children, ref invalidation on resume — plan-25 lineage), consumed by
     `debug_session.zia`/`debug_commands.zia`. The only gap was the VM
     classifying user class instances as leaves.
  2. `RuntimeClasses.hpp` metadata describes *runtime* classes, not user Zia
     classes; user field layouts exist only inside the frontend's `Lowerer`.
     So the ADR pins a **compile-time class-layout sidecar** instead of
     runtime reflection: `CompilerOptions.captureDebugLayouts` →
     `CompilerResult.debugClassLayouts` (qname + flattened fields with byte
     offsets/storage kinds, keyed by runtime class id), captured only for
     debug-adapter runs; `cmd_run` converts it to the VM-owned
     `DebugClassLayoutTable` (new plain-data header
     `include/zanna/vm/debug/DebugClassLayout.hpp`) and installs it via
     `DebugCtrl.setClassLayouts` → `runDebugAdapter`. No IL change, no
     runtime ABI change, no new inter-layer includes (the tool converts).
- **VM expansion** (`src/vm/debug/VMDebug.cpp`): `VmDebugVarStore` gained
  `Kind::Object` — positive `rt_obj_type_id` + table hit → expandable with
  one child per field; children are raw reads of frame-owned object memory
  at compiler-recorded offsets (i64/i32/i16/i1/f64/str-handle/managed/weak
  via `rt_weak_load`; oversized inline aggregates exported opaque and left
  as typed leaves). Managed fields recurse through `describeValue`, so
  objects nest with lists/seqs/maps in both directions. Locals rows show a
  depth-capped `{hp=100, name="Ann", ...}` preview (3 fields / 80 chars,
  cycle-safe). Display types are semantic (`Integer`, `List[Integer]`), read
  strategy is IL storage — decoupled on purpose.
- **Fixed while here**: `expand()` held a reference into `entries_` across
  child description; registering a nested container reallocates the vector
  and dangled it (surfaced as a dropped trailing field; the List/Seq path
  had the same latent hazard). Entry state is now copied before iterating.
- **IDE**: zero changes needed — expandable rows, lazy child fetch, and
  previews all flow through the existing `variables` path.
- **Watch management**: already complete from prior work (add/remove-
  selected/refresh/clear-all as palette commands + Variables-panel watch
  rows + `SelectedWatchIndex` guard); §3's "palette-only" premise was stale.
  Hover-to-inspect remains future polish (below).
- **Probe** `debug_fields_probe.zia` (registered as
  `zia_zannastudio_debug_fields`): scripted session against a fixture mixing
  every storage kind; asserts preview on the locals row, 5 named fields in
  layout order, list-field expansion, nested-object expansion, class-inside-
  list expansion, scalar leaves, and undisturbed exit code. Label total: 47
  green.
- **Remaining scope (recorded)**: boxed struct-typed fields display as typed
  leaves (payload layout is keyed by name, not class id — needs a struct
  sidecar + box-payload access); hover-to-inspect structured tooltip;
  mixed-language projects don't capture layouts (per-file Zia compiles have
  independent class-id spaces).

## 3. IDE scope (after the ADR)

- `build/var_model.zia` + Variables panel: expandable class nodes using the
  new request; lazy child fetch; preview strings.
- Watch management UI: add/edit/remove watches inline in the Variables/Watch
  panel (today palette-only); persisted per project alongside breakpoints.
- Hover-to-inspect while stopped: hover an identifier → evaluate → structured
  tooltip with expansion into the panel.

## 4. Tests / verification (exit gate)

- C++ adapter protocol tests beside the existing tool tests (scripted
  session against a fixture program with nested classes/collections).
- Probe `debugger_fields_probe.zia` driving a scripted debug session
  (harness precedent: `phase2_phase3_probe` passes `--zia-bin`/`--zanna-bin`)
  asserting field expansion and watch CRUD.
- Incremental build + targeted ctest.

## 5. Risks

- Cross-layer coordination (VM/adapter/IDE) — the ADR pins the protocol
  before code; adapter and IDE halves land separately against protocol
  fixtures.
- Handle lifetime across pause/resume — enforced adapter-side and covered by
  protocol tests.
- Deep/recursive object graphs — depth cap + paging in the protocol from day
  one.
