# ADR 0009: Debug Adapter Evaluate Protocol Extension

Date: 2026-06-25

Status: Accepted; implemented and verified against source/tests on 2026-06-27

## Context

ViperIDE watch/evaluate uses the out-of-process VM debug adapter
(`viper run --debug-adapter`) over a newline-JSON control protocol. The IDE
client is `viperide/src/build/debug_session.zia`; the adapter is
`src/tools/viper/DebugAdapter.cpp`. Adding evaluate extends that control
protocol, which is a cross-layer contract between the IDE and the VM adapter and
is covered by ADR 0006.

## Decision

Add one request/response pair to the debug control protocol:

- **Request** (IDE to adapter, while stopped): `{"type":"evaluate","expr":"<expr>"}`.
- **Response** (adapter to IDE): `{"type":"evaluated","expr":"<expr>","value":"<v>","valueType":"<t>","ok":<bool>}`.

The adapter resolves the expression **against the current stop's frame-local
snapshot** (`DebugStopInfo::locals`, already collected for the `stopped` event)
and stays stopped. Evaluate is a side-effect-free query, not a resume.

Current implementation uses the adapter-local evaluator in
`src/tools/viper/DebugExpr.hpp`. Supported expressions are locals and literals,
unary `-`/`not`/`!`, arithmetic `+ - * / %`, comparisons `== != < > <= >=`,
logical `and`/`or` (`&&`/`||`), and parentheses. Numeric int-to-float promotion
applies. Unknown identifiers, parse errors, and type errors return `ok:false`.

Crucially, this does **not** change VM execution semantics: no new VM entry
point, no VM expression interpreter, and no mutation of frame state. The
adapter reuses the locals it already computes. The protocol is internal
(IDE to adapter over stdio); it is not a runtime C ABI, IL, or codegen surface,
so no `runtime.def` or VM change accompanies it.

## Implementation Status

Verified on 2026-06-27:

- `src/tools/viper/DebugAdapter.cpp` builds a `localResolver` from
  `DebugStopInfo::locals`, emits `evaluated` events from `evaluatedEvent`, and
  handles `evaluate` while stopped without resuming the VM.
- `src/tools/viper/DebugExpr.hpp` implements the pure expression evaluator used
  by evaluate, conditional breakpoints, and logpoint interpolation.
- `viperide/src/probes/debug_probe.zia` checks `Evaluate("a")`,
  `Evaluate("a + b")`, and an unknown local against the real out-of-process
  adapter.
- Focused checks passed:
  `ctest --test-dir build -R 'zia_viperide_debug|test_vm_debug_src_breakpoint|test_vm_debug_watches|test_vm_debug_script' --output-on-failure`.

## Consequences

Watch/evaluate works for any stopped frame's named locals across Zia and BASIC.

Because the request is ignored unless the adapter is stopped and the response is
purely advisory, older IDE clients that never send `evaluate` are unaffected,
and the adapter degrades cleanly for unknown names and invalid expressions.
Dotted field access, calls, object inspection, and debuggee mutation remain
outside this adapter-level evaluator and would require separate VM-semantics
review if introduced.

## Spec Impact

No IL or VM semantics changed. The impact is limited to one additional pair of
newline-JSON control messages between the IDE and the debug adapter. The
evaluate expression grammar is the same adapter-local grammar used by ADR 0012.
