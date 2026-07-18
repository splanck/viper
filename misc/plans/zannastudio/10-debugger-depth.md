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
