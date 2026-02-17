---
status: active
audience: public
last-updated: 2026-02-17
---

# OOP Semantics (Milestone B)

This page describes inheritance, method modifiers, and virtual dispatch semantics for the BASIC front end implemented in
Milestone B.

See also:

- Grammar: `grammar.md` (surface syntax and `BASE.M(...)`).
- ABI: `abi/object-layout.md` (object header, vtable, call lowering).

## Inheritance model

- Single inheritance only (`CLASS B : A`). Interfaces and multiple inheritance are out of scope for this milestone.
- Namespaces may qualify base names, e.g. `CLASS B : Foo.Bar.A`.

## Method modifiers

- `VIRTUAL`: Declares a new virtual method on the class. A vtable slot is introduced if the method name does not already
  exist in a base. If a base declares a virtual of the same name, use `OVERRIDE` instead.
- `OVERRIDE`: Reuses the slot of the closest base virtual with the same name. The override’s signature must match
  exactly (parameter types and return type); otherwise a diagnostic is raised.
- `ABSTRACT`: Method has no body and must be implemented in a non‑abstract descendant. A class is considered abstract if
  it declares any abstract method or inherits one without providing an override.
- `FINAL`: Prevents further overrides in descendants. Attempting to override a `FINAL` method is an error.
- Access control `PUBLIC|PRIVATE` is a single‑use prefix per member and is enforced at call/field‑access sites;
  `PRIVATE` members are only visible within the declaring class.

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
- `BASE.M(...)` is a direct call to the immediate base class implementation for `M`, using the current instance as the
  receiver.

## Diagnostics (selected)

The implementation surfaces the following error conditions:

- Cannot instantiate abstract class: `cannot instantiate abstract class '<Name>'`.
- Cannot override final: `cannot override final '<Name>'`.
- Cannot override non‑virtual: `cannot override non-virtual '<Name>'`.
- Override signature mismatch: `override signature mismatch for '<Name>'`.
- Unknown base class: `base class not found: '<Name>'`.
