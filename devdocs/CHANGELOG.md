---
status: active
audience: public
last-verified: 2025-09-24
---

# Documentation Changelog

## 2025-11-10 — Milestone C: Interfaces and RTTI
- Grammar: added `INTERFACE … END INTERFACE`, `IMPLEMENTS` on `CLASS`, and documented `IS`/`AS` expressions with class vs interface behavior; clarified inheritance syntax uses `:` (docs may reference `INHERITS` descriptively).
- Semantics: nominal interfaces with per‑interface itables; slot assignment equals interface declaration order; classes bind one itable per implemented interface.
- Dispatch: interface calls lower via itable lookup to `call.indirect` (diagram included); VM and native use identical indirect call semantics.
- RTTI: `IS` tests dynamic type (class or interface conformance); `AS` returns the object on success or NULL on failure.
- Runtime APIs: documented `rt_itable_lookup`, `rt_typeid_of`, `rt_type_is_a`, `rt_type_implements`, `rt_cast_as`, `rt_cast_as_iface`.
- Tests: noted new/updated goldens for interface dispatch, conformance errors, and `IS`/`AS` behavior.

## 2025-10-24 — Milestone B: BASIC OOP (inheritance, virtual dispatch)
- Grammar: documented `CLASS B : A` with single inheritance; per‑member modifiers `PUBLIC|PRIVATE`, `VIRTUAL`, `OVERRIDE`, `ABSTRACT`, `FINAL`; added `BASE.M(...)` for direct base calls.
- Semantics: defined slot assignment (base‑first, append‑only), override rules and signature checks, abstract/final behavior, and dispatch rules (virtual vs direct/base‑qualified).
- ABI: described object header with vptr at offset 0, vtable entry ordering, method name mangling, and lowering of virtual calls via IL `call.indirect` versus direct calls for non‑virtual/final/base‑qualified.
- Tests: captured positive and negative OOP goldens validating virtual dispatch, base calls, and diagnostics.

## 2025-09-25 — Unified traps and BASIC handlers
- Documented the addition of trap-raising IL opcodes (`trap.*`), structured handler instructions (`eh.*`), and resume forms (`resume.*`).
- Captured BASIC `ON ERROR` / `RESUME` lowering to the new handler and resume primitives.
- Noted the runtime `Err` to trap-kind mapping that now powers consistent diagnostics across the VM and host tools.

## 2025-09-24 — Precise numerics update
- Documented the addition of checked IL arithmetic opcodes (`*.ovf`, `*.chk0`) and checked cast instructions (`cast.*.chk`) introduced with the precise numerics work.
- Recorded that BASIC lowering now emits those checked operations to preserve the new overflow and divide-by-zero traps.
