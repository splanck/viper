---
status: active
audience: public
last-updated: 2025-10-24
---

# Object Layout and Call ABI (Milestone B)

This page documents the runtime object layout and how the BASIC and Zia front ends lower method calls to IL. Both
languages use the same ABI for runtime compatibility.

## Object header and vtable

- Objects begin with a vtable pointer (vptr) at offset 0.
- The vtable is a contiguous array of function pointers ordered by slot number.
- Slot order is base‑first and append‑only per class (see `docs/oop-semantics.md`).

## Method name mangling

- Direct calls name methods using a qualified, class‑aware mangling (e.g. `Namespace.Class.Method`).
- Constructors use a dedicated mangled name (e.g. `Namespace.Class.__ctor`).

## Lowering of calls

- Virtual calls: when targeting a method with a vtable slot, lowering emits an indirect call using the method symbol as
  the callee operand and passes the receiver as the first argument. In IL this is represented with `call.indirect` and a
  global callee operand; the runtime resolves the actual function via the receiver’s vptr and the method’s slot.
- Direct calls: for non‑virtual methods, `FINAL` methods, or `BASE`‑qualified calls, lowering emits a direct call to the
  mangled symbol.
- Base‑qualified calls: `BASE.M(...)` is lowered to a direct call to the immediate base class’s implementation of `M`,
  substituting the current instance (`ME`) for the receiver argument.

## Allocation

- `NEW C(...)` requests the runtime helper to allocate an object of class `C`, then invokes the constructor as a direct
  call with the newly allocated object as the leading argument.

