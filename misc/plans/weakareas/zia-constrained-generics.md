# Zia Constrained Generics

**Status:** Doc-sourced (deferred feature, `docs/GENERICS_IMPLEMENTATION_PLAN.md` §10.1)
**Area:** `src/frontends/zia/`
**Effort:** M–L
**Roadmap fit:** v0.3.x P3 (missing features)

## Problem

Zia generics work end-to-end via monomorphization, but **interface constraints on type
parameters** (e.g. `func max[T: Comparable](a: T, b: T) -> T`) are deferred per
`docs/GENERICS_IMPLEMENTATION_PLAN.md` §10.1. Today a generic body can only use
operations available on all types (or via explicit casts); it cannot require and rely on
an interface contract, and mis-instantiation isn't caught with a clear diagnostic.

This plan scopes the **remaining** work and defers to the existing design doc for the
broader architecture rather than duplicating it.

## Current state (verified via memory + the design doc)

- `Sema_Generics.cpp` performs monomorphization with a type-parameter substitution stack.
- Generic syntax parses; constraint clauses are (per the plan) not fully enforced.
- Interfaces + itable dispatch exist in the language already (used by non-generic code).

## Goal & scope

- **In:** Parse `T: Interface` (and multiple bounds `T: A + B`); enforce at instantiation
  that the concrete type satisfies every bound; allow constrained bodies to call the
  interface's methods; emit a precise diagnostic on violation.
- **Out:** Higher-kinded types, associated types, variance, where-clauses beyond simple
  interface bounds (note as future work).

## Design

1. **Parser:** extend the generic-parameter grammar to accept `: Bound (+ Bound)*`;
   attach bounds to the type-parameter AST node.
2. **Sema:** when instantiating a generic with concrete type args, check each arg against
   its bounds (reuse the existing interface-conformance check used for non-generic
   `x: SomeInterface`). Record the satisfying itable so the body can dispatch. Inside a
   generic body, treat a bounded `T` as conforming to its bounds for method resolution.
3. **Lowerer:** constrained method calls on a `T` lower to interface (itable) dispatch,
   exactly as a non-generic interface call does; monomorphization can later devirtualize
   when the concrete type is known.

## Implementation steps

1. `Parser*`: grammar + AST for bounds (see `parseGenericParams`).
2. `Sema_Generics.cpp` + conformance checker: validate bounds at instantiation; thread
   the itable through the substitution; resolve `T`-method calls via the bound.
3. `Lowerer_*`: emit itable dispatch for constrained calls; keep devirt opportunity.
4. Update `docs/GENERICS_IMPLEMENTATION_PLAN.md` §10.1 status when done.

## Tests (`src/tests/zia/` + `tests/zia_runtime/`)

- **Positive:** `max[T: Comparable]` instantiated with a conforming type compiles and runs
  correctly; constrained method call dispatches to the right impl.
- **Negative:** instantiating with a non-conforming type fails sema with a clear message
  naming the unmet bound (Given/When/Then; assert the diagnostic code + text).
- **Multiple bounds:** `T: A + B` requires both; missing either errors.
- **Monomorphization:** confirm the constrained call devirtualizes where the concrete
  type is statically known (IL inspection).

## Documentation

- Expand the generics section of `docs/zia-reference.md` and `docs/zia-grammar.md` with
  constraint syntax, semantics, and a worked example.
- Add a short constrained-generics example under `examples/` (and wire it into the
  example build-smoke).
- Mark §10.1 complete in `docs/GENERICS_IMPLEMENTATION_PLAN.md`; one release-notes line.

## Cross-platform

Frontend-only; no platform concerns. Output IL must remain VM/native-deterministic.

## Risks / open questions

- **Diagnostic quality** is the user-visible payoff — invest in the "unmet bound X for
  type Y" message and a fixit suggestion where possible.
- **Inherited/forward-reference interactions** (see the memory note on Sema pass-3
  inherited-method copying) — test constrained generics that reference parent interfaces.
