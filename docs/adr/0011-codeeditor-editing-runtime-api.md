---
status: active
audience: contributors
last-verified: 2026-06-25
---

# ADR 0011: CodeEditor Editing Runtime API

Date: 2026-06-25

Status: Accepted

## Context

ViperIDE now relies on `Viper.GUI.CodeEditor` for editor behaviors that were
previously either unavailable or split across multiple non-atomic calls:

- Read-only preview documents need to be enforced by the native editor widget so
  diagnostics, reference previews, and generated buffers cannot be accidentally
  edited through normal text insertion paths.
- Gutter-click handling needs a single atomic read/clear operation. Polling
  `WasGutterClicked`, `GetGutterClickLine`, and `GetGutterClickSlot` separately
  can observe stale or inconsistent state across an event pump tick.

Both additions change the public runtime C ABI surface and the runtime registry,
so they are covered by the spec-currency rule in ADR 0006. The change is limited
to GUI runtime surface; it does not alter IL opcodes, verifier legality, VM
execution, native codegen lowering, or language semantics.

## Decision

Add the following `Viper.GUI.CodeEditor` runtime entries:

- `SetReadOnly(enabled)`: toggles native CodeEditor editability.
- `GetReadOnly()`: returns the current native read-only state.
- `TakeGutterClick()`: returns a map describing the pending gutter click and
  clears it in the same operation.

`TakeGutterClick()` returns a map with:

- `clicked`: true when a pending gutter click was consumed.
- `line`: clicked line index when available, or `-1`.
- `slot`: clicked gutter slot when available, or `-1`.

The existing separate gutter getters remain available for compatibility. The new
method is the preferred editor event-pump primitive because it gives callers a
coherent snapshot and prevents repeated handling of the same native click.

## Consequences

Read-only preview enforcement moves below ViperIDE command code into the shared
CodeEditor widget, so interpreted and native execution paths share the same edit
guard. Gutter click consumers can process breakpoint/debug markers with one
runtime call and no stale state window.

The API additions are covered by runtime surface audit checks, focused GUI
runtime tests, and ViperIDE smoke/probe tests. Future changes that reinterpret
the map shape or remove the compatibility getters require a separate ADR or a
documented migration plan.

## Spec Impact

No IL, verifier, VM, or codegen semantics changed. The impact is limited to
additional GUI runtime catalog entries and the native CodeEditor behavior behind
those entries.
