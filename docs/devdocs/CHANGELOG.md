---
status: active
audience: public
last-verified: 2026-02-09
---

# Documentation Changelog

## 2026-02-09 — Runtime Tiers 2-4: New Subsystems and Platform Completion

Major runtime library expansion adding 10 new C modules, 20 new test files, and completing ViperDOS platform support.

### New Runtime Modules
- **Cycle-detecting GC** (`rt_gc`): Trial-deletion cycle collector supplementing reference counting; zeroing weak
  references with per-target registry; thread-safe via mutex/CRITICAL_SECTION.
- **Unified serialization** (`rt_serialize`): Format-agnostic dispatch across JSON, XML, YAML, TOML, and CSV; automatic
  format detection from content; round-trip conversion between formats.
- **Iterator protocol** (`rt_iter`): Stateful iterator handles wrapping collection + index; supports Seq, List, Deque,
  Map, Set; forward and reverse iteration.
- **Async combinators** (`rt_async`): Async task execution built on Future/Promise + threads; wait-all, map, and
  composition over futures.
- **Concurrent hash map** (`rt_concmap`): Thread-safe string-keyed map with mutex protection; FNV-1a hashing; separate
  chaining collision resolution.
- **Streaming JSON** (`rt_json_stream`): SAX-style pull-based token stream for large/incremental JSON data.
- **Key chord detection** (`rt_keychord`): Simultaneous key chord and sequential combo detection with timing windows.
- **2D physics engine** (`rt_physics2d`): Rigid body dynamics with AABB collision; fixed-timestep Euler integration;
  impulse-based resolution; configurable gravity.
- **Quaternion math** (`rt_quat`): Quaternion constructors, axis-angle/Euler conversion, SLERP interpolation.
- **Spline interpolation** (`rt_spline`): Catmull-Rom, cubic Bezier, and linear splines with tangent queries.

### ViperDOS Platform Completion
- Resolved all 44 `TODO(viperdos)` stubs across 15+ runtime files.
- ViperDOS libc provides POSIX-compatible APIs; most stubs merged into shared Unix `#if !defined(_WIN32)` blocks.
- Affected subsystems: filesystem, exec, threads, monitors, networking, time, terminal, randomness, GUIDs, machine info.

### Testing
- 20 new test files; total test count: 1031 (all passing on Windows and Unix).

## 2025-11-10 — Milestone C: Interfaces and RTTI

- Grammar: added `INTERFACE … END INTERFACE`, `IMPLEMENTS` on `CLASS`, and documented `IS`/`AS` expressions with class
  vs interface behavior; clarified inheritance syntax uses `:` (docs may reference `INHERITS` descriptively).
- Semantics: nominal interfaces with per‑interface itables; slot assignment equals interface declaration order; classes
  bind one itable per implemented interface.
- Dispatch: interface calls lower via itable lookup to `call.indirect` (diagram included); VM and native use identical
  indirect call semantics.
- RTTI: `IS` tests dynamic type (class or interface conformance); `AS` returns the object on success or NULL on failure.
- Runtime APIs: documented `rt_itable_lookup`, `rt_typeid_of`, `rt_type_is_a`, `rt_type_implements`, `rt_cast_as`,
  `rt_cast_as_iface`.
- Tests: noted new/updated goldens for interface dispatch, conformance errors, and `IS`/`AS` behavior.

## 2025-10-24 — Milestone B: BASIC OOP (inheritance, virtual dispatch)

- Grammar: documented `CLASS B : A` with single inheritance; per‑member modifiers `PUBLIC|PRIVATE`, `VIRTUAL`,
  `OVERRIDE`, `ABSTRACT`, `FINAL`; added `BASE.M(...)` for direct base calls.
- Semantics: defined slot assignment (base‑first, append‑only), override rules and signature checks, abstract/final
  behavior, and dispatch rules (virtual vs direct/base‑qualified).
- ABI: described object header with vptr at offset 0, vtable entry ordering, method name mangling, and lowering of
  virtual calls via IL `call.indirect` versus direct calls for non‑virtual/final/base‑qualified.
- Tests: captured positive and negative OOP goldens validating virtual dispatch, base calls, and diagnostics.

## 2025-09-25 — Unified traps and BASIC handlers

- Documented the addition of trap-raising IL opcodes (`trap.*`), structured handler instructions (`eh.*`), and resume
  forms (`resume.*`).
- Captured BASIC `ON ERROR` / `RESUME` lowering to the new handler and resume primitives.
- Noted the runtime `Err` to trap-kind mapping that now powers consistent diagnostics across the VM and host tools.

## 2025-09-24 — Precise numerics update

- Documented the addition of checked IL arithmetic opcodes (`*.ovf`, `*.chk0`) and checked cast instructions (
  `cast.*.chk`) introduced with the precise numerics work.
- Recorded that BASIC lowering now emits those checked operations to preserve the new overflow and divide-by-zero traps.
