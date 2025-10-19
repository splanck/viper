---
status: active
audience: public
last-verified: 2025-09-24
---

# BASIC Object-Oriented Programming

Viper's BASIC frontend now ships with object-oriented programming (OOP) support
enabled by default. Classes encapsulate data and behavior, while the runtime
handles allocation, reference counting, and deterministic destruction. This
document explains the surface syntax, runtime model, and lowering details for
BASIC OOP programs.

## Declaring classes

Use the `CLASS`/`END CLASS` block to declare a new type. Fields appear directly
inside the class body and must specify a type. Methods are declared with `SUB`
or `FUNCTION` blocks, while constructors and destructors use the `SUB NEW` and
`DESTRUCTOR` forms respectively.

```basic
10 CLASS Counter
20   value AS INTEGER
30   SUB NEW(start)
40     LET value = start
50   END SUB
60   SUB INC()
70     LET value = value + 1
80   END SUB
90   FUNCTION CURRENT() AS INTEGER
100    RETURN value
110  END FUNCTION
120  DESTRUCTOR
130    PRINT "Releasing"
140  END DESTRUCTOR
150 END CLASS
```

### Constructors and destructors

- `SUB NEW(...)` runs after storage is allocated but before the caller receives
the object reference.
- `DESTRUCTOR` executes when the runtime detects the reference count reached
  zero, letting programs perform deterministic cleanup (e.g., releasing nested
  objects or printing diagnostics).

Both bodies can access fields directly or through helper methods.

### Methods

Class methods behave like scoped procedures. They receive an implicit `ME`
parameter representing the current instance. Methods may be parameterless or
include arguments, and they can return values when declared with `FUNCTION`.

## Object usage

### Creating instances

Allocate a class instance with the `NEW` expression. Arguments are forwarded to
`SUB NEW` and evaluated left-to-right.

```basic
10 LET counter = NEW Counter(1)
```

The compiler lowers this to the runtime helper `rt_obj_new_i64`, storing field
layouts computed during scanning.

### Accessing the current instance

Inside constructors, destructors, and methods the `ME` expression yields the
current instance reference. Use it for explicit member access or passing the
object to other routines.

```basic
10 SUB INC_BY(amount)
20   LET ME.value = ME.value + amount
30 END SUB
```

### Destroying instances

Use the `DELETE` statement to release an object explicitly. This decrements the
reference count and, when it reaches zero, runs the destructor and frees the
object storage. Programs typically pair `DELETE` with `NEW` when object
lifetimes are well-scoped.

```basic
10 DELETE counter
```

The runtime also decrements reference counts when object variables go out of
scope, so `DELETE` is optional for simple cases.

## Lowering and runtime integration

During scanning the frontend records class metadata (field layouts, method
signatures, helper requirements). Lowering emits:

- Runtime declarations for allocator helpers (`rt_obj_new_i64`,
  `rt_obj_release_check0`, `rt_obj_free`).
- Constructor, destructor, and method functions with mangled names such as
  `Counter.__ctor`, `Counter.__dtor`, and `Counter.inc`.
- Reference-counting operations around `NEW`, `DELETE`, and implicit releases.

These IL artifacts ensure the virtual machine and native backends manage object
lifetimes consistently.

## Example program

```basic
10 CLASS Counter
20   value AS INTEGER
30   SUB NEW(start)
40     LET value = start
50   END SUB
60   SUB INC()
70     LET value = value + 1
80   END SUB
90   FUNCTION CURRENT() AS INTEGER
100    RETURN value
110  END FUNCTION
120  DESTRUCTOR
130    PRINT "Counter disposed"
140  END DESTRUCTOR
150 END CLASS
160 DIM c
170 LET c = NEW Counter(0)
180 CALL c.INC()
190 PRINT c.CURRENT()
200 DELETE c
210 END
```

Build the project and run the program through the BASIC frontend:

```bash
cmake -S . -B build
cmake --build build -j
build/src/tools/ilc/ilc front basic -run path/to/counter.bas
```

All existing BASIC compilation and execution commands work without additional
CMake options because OOP support is permanently enabled.
