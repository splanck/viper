---
status: active
audience: contributors
last-verified: 2026-07-02
---

# ADR 0063: IL `select` Opcode and If-Conversion

## Status

Accepted

## Context

Small branch diamonds — "pick one of two values" — are pervasive in optimized
IL: clamps, min/max, conditional accumulators. After SimplifyCFG they collapse
into a single `cbr` whose two edges target the same join block with differing
arguments, but both backends still lower that shape as a compare + branch +
two argument-copy paths. Modern cores predict poorly on data-dependent
branches of this kind; AArch64 has `csel`/`fcsel` and x86-64 has `cmov`
precisely for it, and the x86-64 backend already carried a latent (unreachable)
`select` lowering rule.

The IL had no way to express a branchless conditional value, so no IL pass
could hand the backends that opportunity.

## Decision

1. **New IL opcode `select`.** Textual form:

   ```
   %r = select <type>, %cond, %tval, %fval
   ```

   - `%cond` is `i1`; `%tval`, `%fval`, and the result all have exactly
     `<type>`. The generated verifier spec expresses this with
     `TypeCategory::InstrType` operands, so `VerifyStrategy::Default` enforces
     the whole rule with no custom checker.
   - **Strict semantics:** `select` is a pure value computation. Both arms are
     ordinary SSA operands, already evaluated wherever they were defined;
     there is no short-circuiting and no control dependence. Consequently only
     values whose producing instructions were safe to execute unconditionally
     may be routed through a `select` — that safety burden falls on whoever
     creates the instruction, not on the opcode.
   - Deterministic across all engines: tree-walk VM, both bytecode dispatch
     loops (real `SELECT` bytecode opcode, 3-pop/1-push), and both native
     backends must agree bit-for-bit.

2. **Backend lowering.**
   - x86-64: the pre-existing `select` rule (SELECT_GPR / SELECT_XMM, `cmov`)
     is now reachable via the IL adapter.
   - AArch64: `csel` for integer/pointer selects; a new `FCsel` machine opcode
     (base encoding `0x1E600C00`) for `f64`, wired through every classifier
     surface. A compare+branch fallback was rejected — it would anti-optimize
     the exact pattern the opcode exists to express.

3. **If-conversion pass (`if-conv`).** `src/il/transform/IfConvert.cpp`
   converts three shapes into `select`s:
   - the collapsed diamond (`cbr` with both edges to one block, differing
     args) — SimplifyCFG's canonical residue;
   - the classic diamond with single-predecessor, parameterless arm blocks;
   - the triangle (one arm block, one direct edge).

   Arms are speculated only when every hoisted instruction is pure,
   result-producing, and non-trapping (side-effect flag plus an explicit
   exclusion list: `load`, division/remainder, `alloca`, calls), capped at
   three instructions per arm. Escape hatch: `VIPER_NO_IF_CONVERT=1`.

4. **Pipeline placement: late O2.** `if-conv` runs after `gvn`/`earlycse` and
   after the last `check-opt`, before the final `dce`/`simplify-cfg`. Earlier
   placement would erase the branch-edge facts `check-opt`'s demotion proofs
   consume.

5. **Range analysis transfer function.** The verifier re-proves checked-op
   demotions after all optimization (ADR 0026), so `IntRangeAnalysis` learned
   `select`: the result range is the interval union of the two arm ranges.
   Without this, converting a clamping branch destroys the proof for a
   downstream demoted `add` and verified programs stop verifying.

## Consequences

- The select_diamond benchmark kernel drops from 150.9 ms to 92.1 ms (1.64×)
  on Apple Silicon at -O2 with checksums intact; the generated AArch64 code
  uses `csel` with no branches in the hot loop.
- Every hand-maintained enum-ordered table (VM dispatch, bytecode, opcode
  info, serializer) gained one entry; compile-time asserts and the dispatch
  coverage tests pin the ordering.
- New IL surface is covered by parse-roundtrip corpus, verifier
  positive/negative tests, IfConvert unit tests, and range-transfer tests.
- Any future pass that rewrites control flow around checked arithmetic must
  preserve or reconstruct range facts, or final verification fails — `select`
  now provides the information-preserving replacement for value-only
  diamonds.
