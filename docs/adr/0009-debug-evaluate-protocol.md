# ADR 0009: Debug Adapter Evaluate Protocol Extension

Date: 2026-06-25

Status: Accepted

## Context

Phase 6 of the ViperIDE overhaul (plan
`~/.claude/plans/viperide-needs-to-be-sharded-puppy.md`) adds watch/evaluate to
the debugger — the foundation for hover-to-inspect and (future) conditional
breakpoints. The IDE drives an out-of-process VM debug adapter
(`viper run --debug-adapter`) over a newline-JSON control protocol (established
in the Phase 3/4 work; the IDE client is `viperide/src/build/debug_session.zia`,
the adapter is `src/tools/viper/DebugAdapter.cpp`). Adding evaluate extends that
control protocol, which is a cross-layer contract between the IDE and the VM
adapter — covered by the spec-currency gate (ADR 0006).

## Decision

Add one request/response pair to the debug control protocol:

- **Request** (IDE → adapter, while stopped): `{"type":"evaluate","expr":"<name>"}`.
- **Response** (adapter → IDE): `{"type":"evaluated","expr":"<name>","value":"<v>","valueType":"<t>","ok":<bool>}`.

The adapter resolves the expression **against the current stop's frame-local
snapshot** (`DebugStopInfo::locals`, already collected for the `stopped` event)
and stays stopped — evaluate is a side-effect-free query, not a resume. v1 is a
**name lookup** (an unresolved name returns `ok:false`); dotted/field and
arbitrary-expression evaluation are explicitly out of scope for this increment.

Crucially, this does **not** change VM execution semantics: no new VM entry
point, no expression interpreter, no mutation of frame state. The adapter reuses
the locals it already computes. The protocol is internal (IDE ↔ adapter over
stdio); it is not a runtime C ABI, IL, or codegen surface, so no `runtime.def`
or VM change accompanies it.

## Consequences

Phase 6 closes its gate via this note and the debugger probe
(`viperide/src/debug_probe.zia`), which sets a breakpoint, stops, evaluates a
known local (asserts value), and evaluates an unknown name (asserts `ok:false`).
Watch/evaluate works for any stopped frame's named locals across Zia and BASIC.

Because the request is ignored unless the adapter is stopped and the response is
purely advisory, older IDE clients that never send `evaluate` are unaffected,
and the adapter degrades cleanly for unknown names. A future ADR will cover
expression evaluation / conditional breakpoints, which *will* require a VM-side
evaluator and therefore its own VM-semantics review.

## Spec Impact

No IL or VM semantics changed. The impact is limited to one additional pair of
newline-JSON control messages between the IDE and the debug adapter.
