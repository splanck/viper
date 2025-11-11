# BASIC Frontend Grammar Notes

This document records small grammar extensions supported by the BASIC frontend so contributors know what to expect when reading or writing tests.

## Namespaces

- Syntax:
  
  NAMESPACE A.B
    … declarations …
  END NAMESPACE

- Semantics:
  - The dotted path `A.B` defines a nested namespace. Multiple `NAMESPACE` blocks may be nested and combined.
  - Names declared inside a namespace are fully qualified as `"A.B.Name"`.
  - Mangled symbols include the full qualification, e.g. `A.B.C.__ctor`, `A.B.C.M`.

## Access Modifiers (CLASS members)

- Scope and default:
  - Inside `CLASS … END CLASS`, a single `PUBLIC` or `PRIVATE` keyword applies to the next member only (single‑use prefix).
  - If no access modifier is present, the default is `PUBLIC`.

- Supported member forms affected by access:
  - Field declarations: `DIM v AS TYPE` or `v AS TYPE`.
  - Methods and constructors/destructors: `SUB … END SUB`, `FUNCTION … END FUNCTION`, `DESTRUCTOR … END DESTRUCTOR`.

- Enforcement:
  - `PRIVATE` members may only be accessed from within the declaring class.
  - The frontend enforces this during lowering and emits a diagnostic when violated.

## Classes, inheritance, and method modifiers

- Declaration:

  CLASS B : A
    [PUBLIC|PRIVATE] [VIRTUAL] [OVERRIDE] [ABSTRACT] [FINAL] SUB M(...)
      ... optional body ...
    END SUB
  END CLASS

  - Single inheritance only: `CLASS B : A` declares `B` with base `A`. The base may be qualified, e.g. `Namespace.A`.
  - Parser note: Some documentation may refer to `INHERITS A` as a descriptive synonym for readability. The BASIC frontend syntax is `:` for inheritance.
  - Method modifiers are per‑member (single‑use) prefixes in any order:
    - `VIRTUAL`: introduces a vtable slot for dispatch.
    - `OVERRIDE`: marks an override of a base virtual method; signature must match.
    - `ABSTRACT`: forbids a body; the method must be implemented by a non‑abstract derived class.
    - `FINAL`: disallows further overrides in derived classes.
  - Constructors are declared as `SUB NEW(...)` and cannot carry the above modifiers.

- Base‑qualified call:

  - `BASE.M(...)` calls the base implementation directly, bypassing virtual dispatch. Parsing treats `BASE` as a special receiver token; lowering substitutes the current instance (`ME`) as the first argument when emitting the direct call.

## Interfaces and conformance

- Declaration:

  INTERFACE I
    SUB M(a AS I64)    # abstract signatures only; no bodies
    FUNCTION F() AS I64
  END INTERFACE

  - Interface members are abstract signatures (SUB/FUNCTION) without bodies.
  - Slot assignment follows declaration order and is used for interface dispatch.

- Implementing interfaces:

  CLASS C [ : Base ] [IMPLEMENTS I1 [, I2 ...]]
    OVERRIDE SUB M(a AS I64)
      ' ... body ...
    END SUB
  END CLASS

  - A class may implement multiple interfaces via an `IMPLEMENTS` list; each declared interface method must be provided with a compatible signature. Missing or mismatched members trigger a conformance diagnostic.
  - Slot assignment equals the interface declaration order and is used to build per‑interface itables bound by each implementing class.

## Runtime type tests and casts (RTTI)

- Expressions:
  - `expr IS Type` → BOOLEAN
    - If `Type` is a class, tests whether the dynamic type of `expr` is that class or a derived class.
    - If `Type` is an interface, tests whether the dynamic type of `expr` implements that interface.
  - `expr AS Type` → pointer/reference value or NULL
    - If `Type` is a class, returns `expr` when the dynamic type is that class or a derived class; otherwise returns NULL.
    - If `Type` is an interface, returns `expr` when the dynamic type implements the interface; otherwise returns NULL.

Notes:
  - The frontend lowers `IS`/`AS` to runtime helpers (type id queries and itable checks). See also the dispatch and RTTI details in [oop.md](oop.md).
