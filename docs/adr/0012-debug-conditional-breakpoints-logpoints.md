---
status: active
audience: contributors
last-verified: 2026-06-27
---

# ADR 0012: Debug Conditional Breakpoints and Logpoints

Date: 2026-06-25

Status: Accepted

## Context

The ZannaIDE overhaul (plan `~/.claude/plans/zannaide-needs-to-be-golden-blum.md`,
Phase 3C) adds conditional breakpoints and logpoints to the debugger. The IDE
drives an out-of-process VM debug adapter (`zanna run --debug-adapter`) over a
newline-JSON control protocol; the IDE client is
`zannaide/src/build/debug_session.zia`, the adapter is
`src/tools/zanna/DebugAdapter.cpp`. Plain source-line breakpoints (ADR-era Phase
3/4) and a name-only `evaluate` query (ADR 0009) already exist.

A conditional breakpoint halts only when an expression is true (e.g. `i > 3`); a
logpoint never halts — it emits a formatted message (with `{expr}` interpolation)
and continues. Both require evaluating real expressions at a stop, which the
ADR-0009 name-lookup does not provide. Extending the control protocol is a
cross-layer contract between the IDE and the VM adapter, covered by the
spec-currency gate (ADR 0006).

## Decision

**Keep the VM untouched.** A conditional/logpoint line remains an ordinary
source-line breakpoint in `DebugCtrl` — the VM stops there exactly as today. The
**adapter** decides what to do at the stop, using the frame-local snapshot
(`DebugStopInfo::locals`) it already computes. No new VM entry point, no VM
expression interpreter, no frame mutation; this is not a runtime C ABI, IL, or
codegen surface, so no `runtime.def` or VM change accompanies it.

**Protocol extension** (backward compatible — the existing `lines` array is
unchanged):

- `setBreakpoints` gains an optional `meta` array, one entry per line that has a
  condition and/or a log message:
  `{"type":"setBreakpoints","path":"<p>","lines":[5,10],
    "meta":[{"line":5,"condition":"i > 3"},{"line":10,"logMessage":"i={i}"}]}`.
- New adapter→IDE event for logpoints:
  `{"type":"log","line":<n>,"message":"<interpolated>"}`.
- `setBreakpoints` is now also honored **while stopped** (not only in the
  pre-launch handshake), so conditions/logpoints can be edited mid-session.

**Adapter expression evaluator** (`src/tools/zanna/DebugExpr.hpp`, header-only):
a side-effect-free recursive-descent evaluator over the locals snapshot.
Supported: int/float/bool/string literals, identifiers resolved from locals,
unary `-`/`not`/`!`, arithmetic `+ - * / %`, comparisons `== != < > <= >=`,
logical `and`/`or` (`&&`/`||`), and parentheses. Numeric promotion (int→double)
applies; unknown identifiers or parse errors make a **condition** behave as
**true** (fail-safe: a malformed condition still stops, never silently skips a
breakpoint). Logpoint messages interpolate `{expr}` segments; an errored segment
renders as `<err>`.

At a breakpoint stop the adapter: looks up `meta` by `basename(path):line` (the
same basename-tolerant match used elsewhere, since the adapter reports canonical
paths); for a **logpoint** emits a `log` event and returns `Continue` without
surfacing the stop; for a **conditional** evaluates the condition and surfaces
the stop only when true, else returns `Continue`.

## Consequences

The IDE persists `condition`/`logMessage` per breakpoint
(`zannaide/src/build/breakpoints.zia`) and sends them via `setBreakpoints`. Users
set them through "Conditional Breakpoint…" / "Add Logpoint…" commands. The
debugger probe (`zannaide/src/probes/debug_probe.zia`) gains conditional-stop and
logpoint cases. Because the evaluator runs on the locals snapshot, conditions can
reference only in-scope source locals (the same surface as `evaluate`); dotted
field access and calls remain out of scope. A future VM-side evaluator could
widen this and also enable a breakpoint-based run-to-cursor and data breakpoints.
