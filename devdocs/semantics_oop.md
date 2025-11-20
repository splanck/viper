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

