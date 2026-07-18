---
status: active
audience: contributors
last-verified: 2026-07-18
---

# ADR 0114: Preserve IEEE-754 Results During IL Constant Folding

## Status

Accepted.

## Context

The IL defines `f64` arithmetic as IEEE-754 and explicitly permits NaN and
infinity propagation. The VM and native backends therefore produce infinities
for nonzero division by zero and NaNs for operations such as zero divided by
zero. The constant-folding implementations previously declined to materialize
all non-finite results. That was conservative, but it prevented compile-time
evaluation of defined IL behavior and made optimizer output depend on whether
an expression reached SCCP or remained for the standalone constant folder.

## Decision

1. Constant `fadd`, `fsub`, `fmul`, and `fdiv` materialize the IEEE-754 result,
   including NaN, positive or negative infinity, and signed zero.
2. `fcmp_ord` and `fcmp_uno` fold according to the IL NaN truth table.
3. SCCP and the standalone constant folder use the same opcode semantics.
4. Checked or trapping conversions remain unfurled when folding would suppress
   a runtime trap. This decision does not make trapping operations speculative.
5. Regression tests compare result classification and sign bits rather than a
   platform-specific NaN payload.
6. Runtime math-call folding must not embed host-libm approximations. Exact
   integer square roots and bounded integral powers use deterministic integer
   proofs and sequenced IEEE operations; non-exact square roots remain calls.
   Floating min/max spell out NaN and signed-zero selection explicitly.

## Consequences

- Optimized and unoptimized programs agree for IEEE special values.
- Textual optimized IL may contain `NaN`, `Inf`, `-Inf`, and `-0.0`, all of
  which are already canonical IL literals.
- NaN payload preservation is not promised; the IL exposes NaN classification,
  not host-specific payload selection.

## Alternatives Considered

- Leaving non-finite expressions for runtime was rejected because their
  behavior is defined and it creates an unnecessary optimizer blind spot.
- Treating floating division by zero as an IL trap was rejected because it
  conflicts with the existing VM, native lowering, and `f64` specification.
