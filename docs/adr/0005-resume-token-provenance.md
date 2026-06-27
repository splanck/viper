# ADR 0005: Resume Tokens Are Handler-Provenance Capabilities

Date: 2026-06-12

Status: Accepted; implemented and verified against source/tests on 2026-06-27

## Context

IL exception handling uses `eh.push`, handler blocks, and opaque `ResumeTok`
values to implement resumable traps. The VM validates resume tokens dynamically,
but the verifier historically tracked only whether some token was active. That
left malformed IL able to route forged or stale `ResumeTok` values through
handler-shaped blocks until a backend or VM runtime check observed the mismatch.

Native EH lowering also rewrites tokens into site identifiers before ordinary
backend lowering. Without a static provenance invariant, VM and native execution
can disagree on which handler scope a `resume.*` belongs to.

## Decision

Treat `ResumeTok` as a linear handler-provenance capability:

- A token is produced only by EH dispatch into the handler selected from the
  active handler stack.
- A token may be forwarded through basic-block parameters only when the incoming
  edge carries the currently active token unchanged.
- Handler-shaped continuation blocks may receive the active token through
  explicit branch arguments. This preserves typed-catch/rethrow helper blocks.
- A `resume.*` instruction may consume only the active token that reached its
  block by EH dispatch or verified forwarding.
- `resume.label` consumes the token before entering its target and therefore
  must not target a handler-shaped block.
- `ResumeTok` values must not be used as ordinary values in calls, stores, returns,
  arithmetic, or other non-control-flow instructions.

## Implementation Status

Verified on 2026-06-27:

- `src/il/verify/EhChecks.cpp` models active tokens with handler-block and
  `eh.push` site provenance, validates handler-shaped block entry, rejects
  `resume.label` targets that are handler blocks, and consumes the active token
  on `resume.*`.
- `src/il/verify/FunctionVerifier.cpp` rejects `ResumeTok` operands in ordinary
  value positions and requires `resume.*` to use the handler `%tok` parameter.
- `src/codegen/common/NativeEHLowering.cpp` lowers handler tokens to site ids
  and emits validation branches for `resume.label` before transferring control.
- `src/vm/ops/Op_TrapEh.cpp`, `src/bytecode/BytecodeVM.cpp`, and
  `src/bytecode/BytecodeVM_threaded.cpp` still dynamically validate active
  resume tokens at execution time.
- Focused checks pass: `test_il_invalid_eh`, CTest's
  `il_verify_invalid_eh_*` cases, `test_vm_errors_eh`,
  `test_il_exception_handler_analysis`, `test_vm_trap_kind`, and
  `test_vm_trap_loc`.

## Consequences

Pros:

- Aligns verifier guarantees with VM runtime token validation.
- Gives native lowering a static contract for translating EH sites and resume
  targets deterministically.
- Preserves existing typed-catch helper blocks by validating their incoming token
  flow instead of banning all handler-to-handler branches.

Cons:

- Hand-written IL that treated `ResumeTok` as an ordinary pointer-like value is
  now rejected.
- Verifier diagnostics become stricter for malformed EH fixtures that previously
  failed only when executed.

## Alternatives

- Reject every branch into a handler-shaped block. This is simpler, but it would
  remove the existing typed-catch helper-block pattern used by lowering tests.
- Keep VM-only validation. This preserves legacy permissiveness, but leaves
  native lowering and optimization without a deterministic static contract.

## Spec Impact

This ADR updates the IL exception-handling contract in `docs/il-guide.md` and
`docs/specs/errors.md`. The grammar is unchanged; only verifier legality for
`ResumeTok` flow is tightened.
