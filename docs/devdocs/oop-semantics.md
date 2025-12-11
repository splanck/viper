---
status: active
audience: public
last-updated: 2025-10-24
---

# OOP Semantics (Milestone B)

This page describes inheritance, method modifiers, and virtual dispatch semantics for the BASIC front end implemented in Milestone B.

See also:
- Grammar: `docs/grammar.md` (surface syntax and `BASE.M(...)`).
- ABI: `docs/abi/object-layout.md` (object header, vtable, call lowering).

## Inheritance model

- Single inheritance only (`CLASS B : A`). Interfaces and multiple inheritance are out of scope for this milestone.
- Namespaces may qualify base names, e.g. `CLASS B : Foo.Bar.A`.

## Method modifiers

- `VIRTUAL`: Declares a new virtual method on the class. A vtable slot is introduced if the method name does not already exist in a base. If a base declares a virtual of the same name, use `OVERRIDE` instead.
- `OVERRIDE`: Reuses the slot of the closest base virtual with the same name. The override’s signature must match exactly (parameter types and return type); otherwise a diagnostic is raised.
- `ABSTRACT`: Method has no body and must be implemented in a non‑abstract descendant. A class is considered abstract if it declares any abstract method or inherits one without providing an override.
- `FINAL`: Prevents further overrides in descendants. Attempting to override a `FINAL` method is an error.
- Access control `PUBLIC|PRIVATE` is a single‑use prefix per member and is enforced at call/field‑access sites; `PRIVATE` members are only visible within the declaring class.

Constructors (`SUB NEW`) may not be marked `VIRTUAL`, `OVERRIDE`, `ABSTRACT`, or `FINAL`.

## Slot assignment and stability

- Slot numbering is base‑first, stable, and append‑only:
  1. A class inherits its base’s vtable verbatim (same order and slot numbers).
  2. Each new `VIRTUAL` method that is not overriding appends one entry at the end.
  3. An `OVERRIDE` keeps the exact slot number of the overridden base method.
- These rules guarantee deterministic ABI layout across builds and platforms.

## Dispatch rules

- Virtual calls (non‑final, non‑base‑qualified) dispatch through the vtable using the receiver’s dynamic type.
- Non‑virtual methods use direct calls.
- `BASE.M(...)` is a direct call to the immediate base class implementation for `M`, using the current instance as the receiver.

## Diagnostics (selected)

The implementation surfaces the following error conditions:

- Unknown base class: `base class not found: '<Name>'`.
- Cannot override non‑virtual: `cannot override non-virtual '<Name>'`.
- Override signature mismatch: `override signature mismatch for '<Name>'`.
- Cannot override final: `cannot override final '<Name>'`.
- Cannot instantiate abstract class: `cannot instantiate abstract class '<Name>'`.

## Pascal–BASIC OOP Interoperability

Both Pascal and BASIC frontends lower OOP constructs to IL using the same underlying ABI, enabling runtime-level compatibility.

### ABI Compatibility (Guaranteed)

The following are guaranteed compatible at the runtime ABI level:

| Feature | Status | Notes |
|---------|--------|-------|
| Object layout | Compatible | Vptr at offset 0, fields follow |
| Class registration | Compatible | Both use `rt_register_class_with_base_rs` |
| Object allocation | Compatible | Both use `rt_obj_new_i64` |
| Vtable access | Compatible | Both use `rt_get_class_vtable` |
| Vtable slot assignment | Compatible | Base-first, append-only, deterministic |
| Virtual dispatch | Compatible | Both use `CallIndirect` with vtable lookup |
| RTTI (type checks) | Compatible | Both support `is`/`as` with runtime helpers |
| Interface support | Compatible | Both register interface implementations |

### Naming Conventions (Different)

The two languages use different method naming conventions in the generated IL:

| Aspect | Pascal | BASIC |
|--------|--------|-------|
| Method names | `TFoo.DoWork` (case-preserved) | `TFOO.DOWORK` (uppercase) |
| Constructor names | `TFoo.Create` (named) | `TFOO.__ctor` |
| Interface syntax | `TShape = class(IDrawable)` | `CLASS TShape IMPLEMENTS IDrawable` |

### Supported Cross-Language Patterns

**Supported at runtime:**
- Objects created by one language can be passed to code compiled from the other
- Virtual dispatch works correctly for objects regardless of which language created them
- RTTI type checks work across language boundaries

**Not supported (due to naming differences):**
- Direct method calls between languages require symbol name normalization at link time
- A Pascal class cannot directly subclass a BASIC class (or vice versa) without explicit interop stubs
- Constructor invocation across languages requires matching the target language's naming convention

### Test Coverage

Interoperability guarantees are verified by:
- `src/tests/frontends/pascal/PascalBasicInteropTests.cpp` - Runtime call compatibility
- `src/tests/oop_interop/PascalBasicABITests.cpp` - Comprehensive ABI compatibility

