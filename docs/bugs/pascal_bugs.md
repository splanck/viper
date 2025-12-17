---
status: active
audience: internal
last-updated: 2025-12-11
---

# Pascal OOP Bug Tracking

This document tracks known issues, limitations, and planned improvements for Pascal OOP support.

## Status Legend

- **RESOLVED**: Fixed and tested
- **OPEN**: Known issue, not yet addressed
- **WONTFIX**: By design or out of scope
- **DEFERRED**: Planned for future milestone

---

## Resolved Issues

### RESOLVED: Interface implementation requires explicit method in declaring class

**Description:** When a class implements an interface, the method must be declared in the class itself or an immediate
parent. Deep inheritance chains where the interface method is only in a grandparent could fail to be detected.

**Resolution:** Interface checking now walks the full inheritance chain to find implementing methods.

**Test coverage:** `SemanticClassTests::InterfaceImplementedByBase`

---

### RESOLVED: Destructor must be named "Destroy"

**Description:** Custom destructor names like `Free` or `Finalize` were silently accepted.

**Resolution:** Semantic analyzer now enforces `Destroy` as the only valid destructor name.

**Test coverage:** `PascalDestructorTest::DestructorMustBeNamedDestroy`

---

### RESOLVED: Override without base virtual not detected

**Description:** Using `override` on a method when no virtual method with that name exists in the base class should
produce an error.

**Resolution:** Semantic analyzer validates that override targets an existing virtual method.

**Test coverage:** `PascalInheritance::OverrideWithoutVirtualFails`, `PascalInheritance::OverrideNoBaseFails`

---

### RESOLVED: Private visibility not enforced in with statements

**Description:** Private fields could be accessed via `with obj do field := value` even outside the class.

**Resolution:** Visibility checking now applies within `with` statement bodies.

**Test coverage:** `PascalVisibility::WithStatementPrivateFieldFails`

---

### RESOLVED: AS operator accepts non-class target types

**Description:** `obj as Integer` should be rejected but was sometimes accepted.

**Resolution:** Semantic analyzer validates that AS operator target is a class or interface type.

**Test coverage:** `PascalOOPDiag::AsOperator_RhsMustBeClass`

---

## Open Issues

### OPEN: Inherited method not directly callable on child type variable

**Description:** When calling an inherited method through a child class variable, the method may not be found if only
declared in the parent.

```pascal
type
  TBase = class
    procedure A; virtual;
  end;
  TChild = class(TBase)
    procedure B; virtual;
  end;
var c: TChild;
begin
  c := TChild.Create;
  c.A;  // May fail to resolve A
end.
```

**Workaround:** Cast to base type or call through base type variable.

**Severity:** Low (workaround available)

---

### OPEN: Constructor overloading resolution edge cases

**Description:** When multiple constructors have similar signatures, overload resolution may not always pick the best
match in edge cases involving type coercion.

**Severity:** Low

---

## Design Limitations (WONTFIX)

### WONTFIX: Multiple class inheritance not supported

**Description:** Pascal supports single class inheritance only. Multiple interfaces are supported.

**Rationale:** Matches Delphi/FPC semantics and simplifies vtable layout.

---

### WONTFIX: Protected visibility not implemented

**Description:** Only `public` and `private` visibility sections are supported. `protected` is not available.

**Rationale:** Deferred to future milestone. Currently private is strictly within-class-only.

---

### WONTFIX: Class methods (static methods) not implemented

**Description:** Methods that operate on the class itself rather than instances are not yet supported.

**Rationale:** Deferred to future milestone.

---

### WONTFIX: Generic types not supported in OOP

**Description:** Generic classes like `TList<T>` are not supported.

**Rationale:** Generics are a larger feature planned for a future milestone.

---

## Deferred Features

### DEFERRED: Automatic destructor chaining

**Description:** Automatically calling `inherited Destroy` at the end of destructor implementations.

**Rationale:** Requires runtime support changes. Currently users must call `inherited Destroy` explicitly.

---

### DEFERRED: Interface delegation

**Description:** Delegating interface implementation to a field: `property Handler: IHandler implements IHandler`

**Rationale:** Planned for future milestone.

---

### DEFERRED: Class references (metaclasses)

**Description:** `class of TFoo` type for factory patterns.

**Rationale:** Planned for future milestone.

---

## Test Coverage Summary

### Semantic Tests (100+ tests)

- `SemanticClassTests.cpp` - Class, interface, visibility, weak refs
- `SemanticInheritanceTests.cpp` - Inheritance, virtual, override, abstract
- `SemanticDestructorTests.cpp` - Destructor semantics
- `SemanticVisibilityTests.cpp` - Public/private enforcement
- `PascalOOPDiagnosticsTests.cpp` - Error message quality

### Parser Tests

- `ParserOOPTests.cpp` - OOP syntax parsing
- `ParserDeclTypeTests.cpp` - Type declarations including classes

### Lowering Tests

- `BasicOOP_Lowering.cpp` - IL generation for OOP constructs

### E2E Tests

- `PascalIsTests.cpp` - IS/AS operators runtime behavior
- `examples/pascal/oop_shapes.pas` - Comprehensive OOP example

### Interop Tests

- `PascalBasicInteropTests.cpp` - Pascal/BASIC compatibility
- `PascalBasicABITests.cpp` - ABI-level compatibility

---

## Change Log

**2025-12-11**: Initial documentation created for Pascal OOP bug tracking.
