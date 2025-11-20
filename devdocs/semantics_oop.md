# OOP Semantics (Milestone D)

## Static Members

- Static fields are class-wide storage. They lower to module-scope globals; reads/writes are independent of any instance.
- Static methods do not receive `ME`; referencing `ME` in a static method is a semantic error.
- The static constructor (`STATIC SUB NEW()`) is parameterless and runs once per class during module initialization before user code.

## Properties

- A `PROPERTY` defines up to two accessors:
  - `GET` produces a value; `SET` receives the new value (parameter defaults to `value` and the property type).
  - Accessor-level access is supported: `PUBLIC`/`PRIVATE` modifier may appear on each accessor and cannot be more permissive than the property head.

### Property sugar

Expressions and assignments are mapped to accessor calls:

- Instance:
  - `x.Name` → `get_Name(x)`
  - `x.Name = v` → `set_Name(x, v)`
- Static:
  - `C.Name` → `get_Name()`
  - `C.Name = v` → `set_Name(v)`

Access control applies at the accessor, not the head, when the accessor has a stricter modifier.

## Interfaces

- In this milestone, interfaces do not support `PROPERTY` blocks or `STATIC` members.
- Method dispatch through interfaces uses a per-(class,interface) indirection table (itable).

## Destructors and Disposal

### Destructor semantics

- One instance destructor per class; no parameters or return value.
- Virtual by default: disposing a derived instance invokes the most‑derived destructor body.
- Chaining order is deterministic: Derived body runs first, then base, continuing up the chain.
- `STATIC DESTRUCTOR` is allowed once per class and runs at program shutdown.
- Destructors are not permitted in interfaces.

### Static destructors (shutdown)

- Static destructors run exactly once at program exit in class declaration order within the module.

### DISPOSE

- `DISPOSE expr` performs deterministic cleanup of an object: invokes the derived→base destructor chain and releases storage when the retain count drops to zero.
- Disposing `NULL` is a no‑op.
- Double dispose: debug builds mark objects as disposed at destructor entry and trap when a disposed object is disposed again.
- Recursion guard: disposing `ME` in a destructor body is diagnosed to prevent infinite recursion.
