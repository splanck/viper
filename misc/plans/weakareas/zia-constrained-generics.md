# Zia Constrained Generics

**Status:** Completed — constrained generics are implemented, hardened with a
dedicated matrix, documented, and covered by example smoke.
**Area:** `src/frontends/zia/`
**Effort:** M
**Roadmap fit:** v0.3.x P3 (missing features)

## Problem

The historical `docs/GENERICS_IMPLEMENTATION_PLAN.md` still contains old future-work
language around constrained generics, but the active code and reference docs have moved
on. `docs/zia-reference.md` documents optional interface constraints, release notes for
0.2.6 say constrained generics shipped, and `src/frontends/zia/Sema_Generics.cpp`
implements `validateGenericConstraints()`.

The real risk is not "feature absent"; it is that constrained generics are complex enough
to regress at the edges: qualified constraints, generic classes/structs/interfaces,
method-level constraints, inherited interface satisfaction, diagnostics, and lowering of
calls inside constrained generic bodies.

## Completed state (verified)

- `Parser_Decl.cpp` parses generic parameters with optional `: qualifiedName`
  constraints for functions, methods, classes, structs, and interfaces.
- `Sema_Generics.cpp` records per-parameter constraints and validates each concrete type
  argument with `typeImplementsInterface()`.
- Class arguments now satisfy constraints through interfaces implemented by base
  classes, with cycle protection while walking the base chain.
- `docs/zia-reference.md` documents `class Box[T: Named]`,
  `func max[T: Comparable]`, qualified constraints, single-bound syntax, and
  future status for multiple bounds / `where` clauses.
- `src/tests/zia/test_zia_constrained_generics.cpp` covers the shipped matrix:
  constrained functions, methods, classes, structs, generic interfaces,
  qualified constraints, inherited interface satisfaction, violation
  diagnostics, rejected multiple-bound syntax, and stable lowered calls inside
  constrained generic bodies.
- `examples/zia/constrained_generics.zia` is wired into the manifest-driven
  example smoke lane.

## Goal & scope

- **In:** Expand conformance tests and diagnostics around the implemented constraint
  model; clarify unsupported syntax in the historical plan; evaluate multiple bounds as a
  deliberate future extension instead of assuming it belongs in the shipped grammar.
- **Out:** Higher-kinded types, associated types, variance, and broad where-clauses.

## Design

The elegant path is to keep the single-bound syntax currently implemented and strengthen
the semantic contract around it before adding new surface area.

1. **Conformance matrix:** collect the shipped constrained-generic behavior into a single
   test file/table covering function, method, class, struct, interface, qualified name,
   inherited implementation, and violation cases.
2. **Diagnostics:** ensure every violation names the concrete type, required interface,
   generic parameter, and generic subject; add stable diagnostic-code assertions where
   the frontend has codes.
3. **Lowering audit:** add IL inspections for constrained method calls inside generic
   bodies so the generated dispatch is explicit and stable.
4. **Future extension:** only after the matrix is green, decide whether `T: A + B` belongs
   in the language. If yes, implement it as a small additive extension to the existing
   `genericParamConstraints` representation (likely vector-of-vectors), not as a new
   generics design.

## Completed work

1. Added `src/tests/zia/test_zia_constrained_generics.cpp` and registered
   `test_zia_constrained_generics`.
2. Added IL inspection for constrained generic body calls. Current
   monomorphized lowering emits a concrete class-id check and direct concrete
   method call such as `genericName$User -> User.name`, not interface-table
   dispatch.
3. Updated `docs/GENERICS_IMPLEMENTATION_PLAN.md` so §10.1 is historical and
   the appendix grammar shows shipped single-bound constraints.
4. Updated `docs/zia-reference.md`, `docs/testing.md`, and `examples/README.md`.
5. Added `examples/zia/constrained_generics.zia` to `examples/smoke_manifest.tsv`.

## Tests (`src/tests/zia/` + `tests/zia_runtime/`)

- **Positive:** constrained function, method, class, struct, and interface declarations
  instantiate with conforming class and struct types.
- **Qualified:** `T: Contracts.Named` resolves through module/namespace qualification.
- **Inherited:** a type satisfying a constraint through a parent/interface chain is
  accepted if the language intends that behavior; otherwise rejected with a clear
  diagnostic.
- **Negative:** non-conforming type fails sema with a clear message naming the unmet
  bound.
- **Lowering:** constrained method calls inside generic bodies produce stable IL and run
  identically on VM/native/bytecode.
- **Future multiple-bounds tests:** add only when the grammar is intentionally extended.

## Documentation

- Keep the generics section of `docs/zia-reference.md` authoritative and add any missing
  edge semantics discovered by the matrix.
- Add a short constrained-generics example under `examples/` (and wire it into the
  example build-smoke).
- Mark §10.1 in `docs/GENERICS_IMPLEMENTATION_PLAN.md` as historical/completed or move it
  to a "future extensions beyond shipped constrained generics" subsection.

## Cross-platform

Frontend-only; no platform concerns. Output IL must remain VM/native-deterministic.

## Risks / open questions

- **Multiple bounds:** intentionally rejected until the grammar and
  `genericParamConstraints` representation are extended.
- **Richer diagnostics:** the current violation message names the concrete type,
  required interface, type parameter, and generic subject. Fix-it suggestions can
  be added later if the diagnostic framework gains a good interface-stub fix-it.

## Verification

Completed on 2026-06-20:

```sh
VIPER_SKIP_CLEAN=1 VIPER_SKIP_TESTS=1 VIPER_SKIP_LINT=1 VIPER_SKIP_AUDIT=1 VIPER_SKIP_SMOKE=1 VIPER_SKIP_INSTALL=1 ./scripts/build_viper_mac.sh
cmake --build build --target test_zia_constrained_generics -j 8
ctest --test-dir build -R '^test_zia_constrained_generics$' --output-on-failure
ctest --test-dir build -R '^(example_smoke_manifest_audit|example_smoke_fast)$' --output-on-failure
./build/src/tools/viper/viper run examples/zia/constrained_generics.zia
```
