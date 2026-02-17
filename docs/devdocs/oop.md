# Object‑Oriented Features: Interfaces and RTTI

This note summarizes nominal interfaces, interface dispatch, and runtime type checks (`IS`/`AS`). It complements the
class and virtual method semantics introduced earlier (see oop-semantics.md) and explains how interface calls are
executed in both the VM and the native backend.

## Interfaces

- Interfaces are nominal: the name (optionally qualified) identifies the contract. Implementations are declared by
  listing one or more interface names on the class declaration.

  INTERFACE I
  SUB Speak()
  FUNCTION F() AS I64
  END INTERFACE

  CLASS A IMPLEMENTS I
  OVERRIDE SUB Speak()
  ' ...
  END SUB
  OVERRIDE FUNCTION F() AS I64
  ' ...
  END FUNCTION
  END CLASS

- Slot assignment:
    - Each interface assigns slot indices to members in declaration order. These slot indices are used to build the
      class‑specific “itable” for that interface.
    - A class that implements multiple interfaces has one itable per interface; implementing one interface does not
      affect the slot layout of another.

- Conformance:
    - A class must provide a method for each interface slot with a compatible signature. Duplicate interface method
      names are rejected.
    - Errors:
        - E_CLASS_MISSES_IFACE_METHOD: "class '{C}' does not implement '{I}.{M}'."
        - E_IFACE_DUP_METHOD: "interface '{I}' declares duplicate method '{M}'."

## Dispatch Model

Two equivalent views are useful for understanding interface calls (intrinsic helper vs. indirect call):

1) Conceptual (slot invocation):
    - Resolve the itable for (object, interfaceId) at runtime.
    - Load function pointer at `[itable + slot * ptrsize]`.
    - Call that function with the object as the first argument.

2) Implementation (indirect):
    - Milestone B introduced `call.indirect`. Milestone C lowers interface calls by materializing the callee pointer
      from the itable and emitting `call.indirect`.

   ASCII sketch:

        obj ──┐
              ├─ rt_itable_lookup(obj, ifaceId) → it
        I.slot ──(offset)──► fn = [it + slot*ptrsize]
              └─ call.indirect fn(obj, args...)

    - VM: executes pointer‑based `call.indirect` by jumping to the IL function mapped by the pointer.
    - Native: `call.indirect` lowers to a normal indirect CALL following the platform ABI (SysV x86‑64: integer args in
      rdi, rsi, rdx, rcx, r8, r9; returns in rax/xmm0).
    - Intrinsic view: `rt_itable_lookup` is an intrinsic‑style helper used only to produce the callee pointer; the
      actual dispatch remains an IL‑level indirect call for determinism across VM and native.

## RTTI: IS / AS

- IS:
    - `expr IS Class` → true iff the dynamic type equals the class or derives from it.
    - `expr IS Interface` → true iff the dynamic type implements the interface.
    - Lowering uses `rt_typeid_of` + `rt_type_is_a` or `rt_type_implements`.

- AS:
    - `expr AS Class` → returns `expr` when IS succeeds; otherwise returns NULL.
    - `expr AS Interface` → returns `expr` when IS succeeds; otherwise returns NULL.
    - Lowering calls `rt_cast_as` / `rt_cast_as_iface`, which perform the test and propagate either the original pointer
      or NULL.

## Relationship to Virtual Methods

- Virtual methods (Milestone B) use one vtable per class and slot indices assigned in the class hierarchy.
- Interfaces (Milestone C) use one itable per (class, interface) pair with slot indices assigned by the interface
  declaration. A class can have both a vtable (for virtuals) and multiple itables (for interfaces).

## Runtime Surface (selected)

- `void *rt_cast_as(void *obj, int target_type_id)`
- `void *rt_cast_as_iface(void *obj, int iface_id)`
- `void **rt_itable_lookup(void *obj, int iface_id)`
- `int rt_type_implements(int type_id, int iface_id)`
- `int rt_type_is_a(int type_id, int test_type_id)`
- `int rt_typeid_of(void *obj)`
