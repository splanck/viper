# ADR 0005: Resume Tokens Are Handler-Provenance Capabilities

Date: 2026-06-12

Context

IL exception handling uses `eh.push`, handler blocks, and opaque `resumetok`
values to implement resumable traps. The VM validates resume tokens dynamically,
but the verifier historically tracked only whether some token was active. That
left malformed IL able to route forged or stale `ResumeTok` values through
handler-shaped blocks until a backend or VM runtime check observed the mismatch.

Native EH lowering also rewrites tokens into site identifiers before ordinary
backend lowering. Without a static provenance invariant, VM and native execution
can disagree on which handler scope a `resume.*` belongs to.

Decision

Treat `resumetok` as a linear handler-provenance capability:

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
- Resume tokens must not be used as ordinary values in calls, stores, returns,
  arithmetic, or other non-control-flow instructions.

Consequences

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

Alternatives

- Reject every branch into a handler-shaped block. This is simpler, but it would
  remove the existing typed-catch helper-block pattern used by lowering tests.
- Keep VM-only validation. This preserves legacy permissiveness, but leaves
  native lowering and optimization without a deterministic static contract.

Spec Impact

This ADR updates the IL exception-handling contract in `docs/il-guide.md` and
`docs/specs/errors.md`. The grammar is unchanged; only verifier legality for
`resumetok` flow is tightened.

Migration Plan

1. Add verifier diagnostics for missing or mismatched resume-token provenance.
2. Validate handler-shaped block entry edges against active-token forwarding.
3. Harden native EH lowering so `resume.label` validates the site token before
   branching.
4. Add negative tests for forged tokens, handler entry without dispatch, and
   handler resume targets.
