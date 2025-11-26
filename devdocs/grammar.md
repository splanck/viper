# BASIC Frontend Grammar Notes

This document records small grammar extensions supported by the BASIC frontend so contributors know what to expect when reading or writing tests.

## Namespaces

### NAMESPACE Declaration

- Syntax:

  NAMESPACE A.B.C
    … declarations …
  END NAMESPACE

- Semantics:
  - The dotted path `A.B.C` defines a nested namespace hierarchy.
  - Multiple `NAMESPACE` blocks with the same path contribute to the same namespace (merged namespaces).
  - Names declared inside a namespace are fully qualified as `"A.B.C.Name"`.
  - Namespaces can contain CLASS, INTERFACE, and nested NAMESPACE declarations.
  - All namespace identifiers are case-insensitive (per BASIC semantics).
  - Mangled symbols include the full qualification, e.g. `A.B.C.__ctor`, `A.B.C.M`.

- Examples:

  NAMESPACE Graphics.Rendering
    CLASS Renderer
      DIM width AS I64
    END CLASS
  END NAMESPACE

  NAMESPACE Graphics.Rendering
    CLASS Camera
      DIM position AS I64
    END CLASS
  END NAMESPACE

  REM Both classes belong to Graphics.Rendering

- Reserved namespace:
  - The root namespace `Viper` is reserved for future built-in libraries and cannot be declared by user code.

### USING Directive

- Syntax (two forms):

  1. Simple import:
     USING NamespacePath

  2. Aliased import:
     USING Alias = NamespacePath

- Placement rules:
  - `USING` directives must appear at file scope (not inside NAMESPACE or CLASS blocks).
  - `USING` directives must appear before any NAMESPACE, CLASS, or INTERFACE declarations.
  - All `USING` directives are file-scoped and do not affect other compilation units.

- Semantics:
  - Simple form: Makes types from the specified namespace available for unqualified lookup.
  - Alias form: Creates a shorthand alias for the namespace path, e.g. `USING IO = Viper.IO` or `USING Text = Viper.Text`.
  - Multiple `USING` directives accumulate; type resolution checks them in declaration order.
  - If multiple imported namespaces contain the same type name, an unqualified reference is ambiguous (E_NS_003).
  - Aliases must be unique within a file (E_NS_004).

- Examples:

  USING Collections
  USING Utils.Helpers
  USING DB = Application.Database
  USING IO = Viper.IO

  NAMESPACE Collections
    CLASS List
      DIM size AS I64
    END CLASS
  END NAMESPACE

  REM Can now reference List unqualified (via USING Collections)
  REM Or use DB.Connection (via alias) and IO.File (canonical alias)

### Type Resolution Precedence

When resolving an unqualified type reference, the compiler searches in this order:

1. Current namespace
2. Parent namespaces (walking up the hierarchy)
3. Imported namespaces via USING (in declaration order)
4. Global namespace

Fully-qualified names (e.g. `A.B.Type`) bypass this search and resolve directly.

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

## Destructors and Disposal

### Class destructors

- Instance destructor (no params/return):

  ```basic
  DESTRUCTOR
    ' body
  END DESTRUCTOR
  ```

- Static destructor (no params):

  ```basic
  STATIC DESTRUCTOR
    ' body
  END DESTRUCTOR
  ```

Constraints:

- At most one instance destructor per class.
- At most one static destructor per class.
- No access modifiers on destructors.
- Destructors are not allowed in `INTERFACE` declarations.

### DISPOSE statement

- Explicit disposal of an object reference:

  ```basic
  DISPOSE <expr>
  ```

`<expr>` must evaluate to an object handle; disposing `NULL` is a no‑op.

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
# Milestone D: Static Members and Properties

## STATIC modifier placement

`STATIC` applies to the next member declaration inside a `CLASS`:

```
CLASS C
  STATIC value AS INTEGER      ' static field
  STATIC SUB Ping()            ' static method
  STATIC PROPERTY Count AS INTEGER
    GET: RETURN 0: END GET
  END PROPERTY
END CLASS
```

## PROPERTY blocks

```
PROPERTY <Name> AS <Type>
  [<AccessorAccess>] GET
    <statements>
  END GET
  [<AccessorAccess>] SET [(<Param> [AS <Type>])]
    <statements>
  END SET
END PROPERTY
```

- `<AccessorAccess>`: optional `PUBLIC` or `PRIVATE` on each accessor; defaults to the property head's access.
- `SET` parameter defaults to the property type and parameter name `value` when not given explicitly.

## Static constructor

One static constructor per class is supported using the standard constructor syntax preceded by `STATIC`:

```
CLASS A
  STATIC SUB NEW()
    ' one-time initialization for the class
  END SUB
END CLASS
```

The static constructor is parameterless and is invoked by the module initializer before any user code runs.
