---
status: active
audience: public
last-updated: 2025-11-12
---

# Viper BASIC Namespaces — Reference

This document provides a complete reference for the namespace feature in Viper BASIC (Track A implementation). For tutorial-style examples, see [basic-language.md](../basic-language.md). For grammar details, see [grammar.md](grammar.md).

## Overview

Namespaces organize types (classes and interfaces) into hierarchical groups, preventing name collisions and improving code clarity. The namespace system includes:

- **NAMESPACE declarations**: Define nested namespace hierarchies
- **USING directives**: Import namespaces for unqualified type references
- **Namespace aliases**: Create shorthand names for long namespace paths
- **Type resolution**: Automatic search through namespace hierarchy and imports

## Track A Implementation — Complete

Track A is fully implemented and includes:

- Syntax: `NAMESPACE A.B ... END NAMESPACE` (dotted segments form a nested hierarchy)
- Declarations: procedures (SUB/FUNCTION) and types inside a namespace are considered fully-qualified at that path (e.g., `A.B.F`, `A.Point`)
- **USING directives**: Import namespaces for unqualified type references (both simple and aliased forms)
- **Namespace aliases**: Create shorthand names with `USING Alias = Full.Path`
- Type resolution algorithm: searches current namespace → parent namespaces → USING imports → global
- Calls:
  - Qualified: `A.B.F()` calls the exact fully-qualified procedure
  - Unqualified: resolves via current namespace, parent-walk, and USING imports
- Case-insensitive: all segments and identifiers are matched ignoring case. The following are equivalent: `Namespace`, `NAMESPACE`, `nameSpace`; and `A.B.F` ≡ `a.b.f` for lookup and duplicate checks
- Nine diagnostic error codes (E_NS_001 through E_NS_009) for comprehensive error handling

### Basic Examples

1) Flattened namespace call (t01)

```basic
NAMESPACE A
  SUB F: PRINT "ok": END SUB
END NAMESPACE

A.F()   ' qualified call
```

Output:

```
ok
```

2) Nested namespace call (t02)

```basic
NAMESPACE A.B
  SUB F: PRINT "ok": END SUB
END NAMESPACE

A.B.F() ' fully-qualified call
```

Output:

```
ok
```

3) Parent-walk resolution within nested namespace (t03)

```basic
NAMESPACE A.B
  SUB F: PRINT "ok": END SUB
  SUB Main
    F()   ' unqualified; resolves to A.B.F via parent-walk
  END SUB
END NAMESPACE

A.B.Main()
```

Output:

```
ok
```

4) Cross-file usage with USING directive

File 1 defines:

```basic
NAMESPACE Lib
  SUB Ping: PRINT "pong": END SUB
END NAMESPACE
```

File 2 imports and calls:

```basic
USING Lib

Lib.Ping()   ' qualified call works
Ping()       ' unqualified call works via USING
```

Both calls succeed. Without the USING directive, only the qualified call (`Lib.Ping()`) would work.

### Notes on Case-Insensitivity

- Identifiers and each segment of qualified names are compared case-insensitively
- Canonicalization uses ASCII lowercase for resolution and duplicate checks
- Examples (all equivalent): `A.B.F`, `a.b.f`, `A.b.f`, `a.B.F`

## Known Limitations

Current implementation has the following limitations:

- **Qualified names in DIM AS**: Parser does not yet support dotted paths like `DIM p AS Graphics.Point`. Use USING directives to enable unqualified type references.
- **Case-sensitive qualified procedure calls**: Lowercase qualified calls like `a.b.f()` fail when the procedure is defined as `A.B.F()`. This is a bug; all lookups should be case-insensitive per the specification.
- **File-scoped USING only**: USING directives are not inherited across compilation units; each file must declare its own imports.

## NAMESPACE Declaration

### Syntax

```basic
NAMESPACE A.B.C
  CLASS MyClass
    ...
  END CLASS

  INTERFACE MyInterface
    ...
  END INTERFACE
END NAMESPACE
```

### Rules

1. **Dotted paths**: `NAMESPACE A.B.C` creates a three-level hierarchy (A::B::C)
2. **Merged namespaces**: Multiple `NAMESPACE` blocks with the same path contribute to the same namespace
3. **Nested declarations**: Namespaces can contain CLASS, INTERFACE, and nested NAMESPACE blocks
4. **Case insensitive**: All namespace identifiers follow BASIC case-insensitive semantics
5. **Global scope**: NAMESPACE blocks must appear at file scope (not inside other blocks)

### Examples

```basic
NAMESPACE Collections
  CLASS List
    DIM size AS I64
  END CLASS
END NAMESPACE

NAMESPACE Collections
  CLASS Dictionary
    DIM count AS I64
  END CLASS
END NAMESPACE

REM Both List and Dictionary belong to Collections namespace
```

### Qualified Names

Types declared in namespaces have fully-qualified names:

```basic
NAMESPACE Graphics.Rendering
  CLASS Camera
  END CLASS
END NAMESPACE

REM Fully-qualified reference:
DIM cam AS Graphics.Rendering.Camera
```

## USING Directive

### Syntax

The USING directive has two forms:

1. **Simple import**: `USING NamespacePath`
2. **Aliased import**: `USING Alias = NamespacePath`

```basic
USING Collections
USING Graphics.Rendering
USING GFX = Graphics.Rendering
```

### Placement Rules

**CRITICAL**: USING directives must satisfy strict placement constraints:

1. **File scope only**: Cannot appear inside NAMESPACE, CLASS, or INTERFACE blocks
2. **Before all declarations**: Must appear before any NAMESPACE, CLASS, or INTERFACE declarations
3. **File-scoped effect**: Each file's USING directives do not affect other compilation units

```basic
REM ✓ CORRECT: USING comes first
USING Collections

NAMESPACE App
  CLASS MyApp
  END CLASS
END NAMESPACE

REM ✗ WRONG: USING after NAMESPACE
NAMESPACE App
  CLASS MyApp
  END CLASS
END NAMESPACE

USING Collections    REM Error: E_NS_005
```

### Semantics

**Simple form** (`USING Collections`):
- Makes all types from the namespace available for unqualified lookup
- Multiple USING directives accumulate
- Order matters: earlier imports checked first during resolution

**Alias form** (`USING Coll = Collections`):
- Creates a shorthand alias for the namespace path
- Alias can be used in type references: `Coll.List`
- Aliases must be unique within a file

### Examples

```basic
USING Collections
USING Utils.Helpers
USING DB = Application.Database

NAMESPACE Collections
  CLASS List
  END CLASS
END NAMESPACE

NAMESPACE Application.Database
  CLASS Connection
  END CLASS
END NAMESPACE

REM Unqualified via USING Collections
DIM myList AS List

REM Via alias
DIM conn AS DB.Connection
```

## Type Resolution

### Resolution Algorithm

When resolving an unqualified type reference (e.g., `MyClass`), the compiler searches in this order:

1. **Current namespace**: Types declared in the same namespace as the reference
2. **Parent namespaces**: Walk up the namespace hierarchy to the root
3. **USING imports**: Search imported namespaces in declaration order
4. **Global namespace**: Types declared at global scope

**Fully-qualified references** (e.g., `A.B.MyClass`) bypass this search and resolve directly.

### Examples

```basic
USING Foundation

NAMESPACE Foundation
  CLASS Entity
  END CLASS
END NAMESPACE

NAMESPACE App.Domain
  CLASS Service
    REM Resolution order:
    REM 1. App.Domain (current)
    REM 2. App (parent)
    REM 3. Global root (parent)
    REM 4. Foundation (via USING)
    DIM entity AS Entity    REM Found via USING Foundation
  END CLASS
END NAMESPACE
```

### Case Insensitivity

All namespace and type lookups are case-insensitive:

```basic
NAMESPACE MyLib
  CLASS Helper
  END CLASS
END NAMESPACE

REM All equivalent:
DIM h1 AS MyLib.Helper
DIM h2 AS MYLIB.HELPER
DIM h3 AS mylib.helper
DIM h4 AS MyLib.HELPER
```

## Reserved Namespaces

The `Viper` root namespace is **reserved** for future built-in libraries (Track B). User code cannot:

- Declare namespaces under `Viper` (e.g., `NAMESPACE Viper.MyLib`)
- Use `USING Viper` (the root itself)

This ensures future compatibility with Viper's standard library.

```basic
REM ✗ WRONG: Reserved namespace
NAMESPACE Viper.MyLib    REM Error: E_NS_009
END NAMESPACE

REM ✗ WRONG: Reserved root
USING Viper              REM Error: E_NS_009
```

## Diagnostics Reference

The namespace system defines nine error codes covering all failure modes.

| Code | Category | Description |
|------|----------|-------------|
| E_NS_001 | Not Found | Namespace does not exist |
| E_NS_002 | Not Found | Type not found in namespace |
| E_NS_003 | Ambiguity | Type reference is ambiguous |
| E_NS_004 | Duplicate | Alias already defined |
| E_NS_005 | Placement | USING after declaration |
| E_NS_006 | Structure | Nested USING not allowed |
| E_NS_007 | Reserved | Global type shadowed by namespace |
| E_NS_008 | Placement | USING inside namespace block |
| E_NS_009 | Reserved | Viper namespace reserved |

### E_NS_001: Namespace Not Found

**Cause**: USING directive references a namespace that doesn't exist.

```basic
USING NonExistent    REM Error: E_NS_001

END
```

**Message**: `namespace not found: 'NONEXISTENT'`

**Fix**: Ensure the namespace is declared before using it (possibly in another file).

---

### E_NS_002: Type Not Found in Namespace

**Cause**: Fully-qualified reference to a type that doesn't exist in the specified namespace.

```basic
NAMESPACE Collections
  CLASS List
  END CLASS
END NAMESPACE

DIM d AS Collections.Dictionary    REM Error: E_NS_002
```

**Message**: `type 'DICTIONARY' not found in namespace 'COLLECTIONS'`

**Fix**: Check spelling or declare the missing type.

---

### E_NS_003: Ambiguous Type Reference

**Cause**: Multiple imported namespaces contain types with the same name.

```basic
USING A
USING B

NAMESPACE A
  CLASS Thing
    DIM x AS I64
  END CLASS
END NAMESPACE

NAMESPACE B
  CLASS Thing
    DIM y AS I64
  END CLASS
END NAMESPACE

CLASS MyClass : Thing    REM Error: E_NS_003
  DIM z AS I64
END CLASS
END
```

**Message**: `ambiguous type reference 'THING' (candidates: A.THING, B.THING)`

**Candidate list is sorted alphabetically for stability.**

**Fixes**:

1. Use fully-qualified name:
```basic
CLASS MyClass : A.Thing
```

2. Use namespace alias:
```basic
USING AliasA = A
CLASS MyClass : AliasA.Thing
```

---

### E_NS_004: Duplicate Alias

**Cause**: Same alias defined twice in the same file.

```basic
USING Coll = Collections
USING Coll = Utilities    REM Error: E_NS_004
```

**Message**: `duplicate alias 'COLL'`

**Fix**: Use unique aliases for different namespaces.

**Note**: This error is difficult to demonstrate in single-file tests due to ordering constraints (USING must come before namespace declarations, but checking namespace existence happens before checking duplicate aliases).

---

### E_NS_005: USING After Declaration

**Cause**: USING directive appears after a NAMESPACE, CLASS, or INTERFACE declaration.

```basic
NAMESPACE App
  CLASS MyApp
  END CLASS
END NAMESPACE

USING Collections    REM Error: E_NS_005

END
```

**Message**: `USING must appear before all declarations`

**Fix**: Move all USING directives to the top of the file.

---

### E_NS_006: Nested USING Not Allowed

**Cause**: USING directive inside a NAMESPACE or CLASS block.

```basic
NAMESPACE App
  USING Collections    REM Error: E_NS_006
END NAMESPACE
```

**Message**: `USING cannot be nested inside namespace or class blocks`

**Fix**: Move USING to file scope.

---

### E_NS_007: Global Type Shadowed

**Cause**: A namespace type shadows a global type (reserved for future use).

**Note**: This diagnostic is defined but not actively enforced in Track A.

---

### E_NS_008: USING Inside Namespace Block

**Cause**: USING directive appears inside a NAMESPACE block.

```basic
NAMESPACE App
  USING Collections    REM Error: E_NS_008

  CLASS MyClass
  END CLASS
END NAMESPACE

END
```

**Message**: `USING inside NAMESPACE block not allowed`

**Fix**: Move USING to file scope before all NAMESPACE declarations.

---

### E_NS_009: Reserved Viper Namespace

**Cause**: User code attempts to declare or use the reserved `Viper` root namespace.

```basic
REM Example 1: Declaration
NAMESPACE Viper.MyLib    REM Error: E_NS_009
  CLASS Helper
  END CLASS
END NAMESPACE

REM Example 2: USING
USING Viper              REM Error: E_NS_009
```

**Message**: `namespace 'VIPER' is reserved for built-in libraries`

**Fix**: Use a different root namespace for user code.

---

## Common Pitfalls and Solutions

### Pitfall 1: Ambiguous References

**Problem**: Importing multiple namespaces that define the same type name.

```basic
USING Collections
USING Utilities

REM Both define "List"
DIM myList AS List    REM Error: E_NS_003
```

**Solutions**:

1. **Fully-qualify the reference**:
```basic
DIM myList AS Collections.List
```

2. **Use namespace aliases**:
```basic
USING Coll = Collections
USING Util = Utilities
DIM myList AS Coll.List
```

3. **Remove unnecessary USING directives**:
```basic
USING Collections    REM Only import what you need
DIM myList AS List
```

---

### Pitfall 2: Wrong USING Placement

**Problem**: Placing USING after declarations.

```basic
NAMESPACE App
END NAMESPACE

USING Collections    REM Error: E_NS_005
```

**Solution**: Always put USING directives at the top of the file:

```basic
USING Collections

NAMESPACE App
END NAMESPACE
```

---

### Pitfall 3: USING Inside Namespace

**Problem**: Attempting to scope USING to a specific namespace.

```basic
NAMESPACE App
  USING Collections    REM Error: E_NS_008
END NAMESPACE
```

**Solution**: USING is always file-scoped. Put it at file scope:

```basic
USING Collections

NAMESPACE App
  REM Collections is now available
END NAMESPACE
```

---

### Pitfall 4: Reserved Viper Namespace

**Problem**: Trying to use `Viper` for user code.

```basic
NAMESPACE Viper.MyApp    REM Error: E_NS_009
```

**Solution**: Choose a different root namespace:

```basic
NAMESPACE MyCompany.MyApp
```

---

### Pitfall 5: Case Mismatches (Not an Error)

**Not a problem**: BASIC is case-insensitive, so these are all valid:

```basic
NAMESPACE MyLib
  CLASS Helper
  END CLASS
END NAMESPACE

DIM h1 AS MyLib.Helper     REM OK
DIM h2 AS MYLIB.HELPER     REM OK (same as above)
DIM h3 AS mylib.helper     REM OK (same as above)
```

This is intentional BASIC behavior, not a pitfall.

---

## Multi-File Compilation

### File-Scoped USING

Each file's USING directives are **file-scoped** and do not affect other files:

**file1.bas**:
```basic
USING Collections

NAMESPACE App
  CLASS MyApp : List    REM OK: List available via USING
  END CLASS
END NAMESPACE
```

**file2.bas**:
```basic
NAMESPACE App
  CLASS OtherApp : Collections.List    REM Must fully-qualify: no USING here
  END CLASS
END NAMESPACE
```

### Namespace Merging Across Files

Multiple files can contribute to the same namespace:

**file1.bas**:
```basic
NAMESPACE Collections
  CLASS List
  END CLASS
END NAMESPACE
```

**file2.bas**:
```basic
NAMESPACE Collections
  CLASS Dictionary
  END CLASS
END NAMESPACE
```

Both `List` and `Dictionary` belong to `Collections` namespace after linking.

---

## Best Practices

1. **Use namespaces for organization**: Group related types together
2. **Avoid deep nesting**: Keep namespace hierarchies shallow (2-3 levels)
3. **Choose descriptive names**: `Graphics.Rendering` is better than `Gfx.Rnd`
4. **Use aliases for long paths**: `USING GFX = Graphics.Rendering.Advanced`
5. **Minimize USING imports**: Only import what you need to reduce ambiguity
6. **Prefer qualification over USING**: For rarely-used types, use fully-qualified names
7. **Document public namespaces**: Add comments explaining the purpose of each namespace
8. **Reserve root namespaces**: Choose a company or project root (e.g., `MyCompany.*`)

---

## Viper.* Runtime Namespace (Implemented)

The Viper standard library exposes runtime functions and types under the reserved `Viper.*` root namespace. This canonical namespace organization is now implemented and available in IL and BASIC code.

### Runtime Functions by Namespace

#### Viper.Console

Console I/O operations:
- `Viper.Console.PrintStr(str)->void` — Print string
- `Viper.Console.PrintI64(i64)->void` — Print integer
- `Viper.Console.PrintF64(f64)->void` — Print double
- `Viper.Console.ReadLine()->str` — Read line from console

#### Viper.Strings

String manipulation and conversion:
- `Viper.Strings.Len(str)->i64` — String length
- `Viper.Strings.Mid(str,i64,i64)->str` — Substring
- `Viper.Strings.Concat(str,str)->str` — Concatenate strings
- `Viper.Strings.SplitFields(str,ptr str,i64)->i64` — Split into fields
- `Viper.Strings.FromInt(i64)->str` — Convert int64 to string
- `Viper.Strings.FromI16(i16)->str` — Convert int16 to string
- `Viper.Strings.FromI32(i32)->str` — Convert int32 to string
- `Viper.Strings.FromSingle(f32)->str` — Convert float to string
- `Viper.Strings.FromDouble(f64)->str` — Convert double to string
- `Viper.Strings.FromDoublePrecise(f64)->str` — Precise double to string
- `Viper.Strings.Builder.New()->ptr` — Create StringBuilder

#### Viper.Convert

Type conversion with error handling:
- `Viper.Convert.ToInt(str)->i64` — String to int (throws on error)
- `Viper.Convert.ToDouble(str)->f64` — String to double (throws on error)

#### Viper.Parse

Type parsing with explicit error codes:
- `Viper.Parse.Int64(cstr,ptr i64)->i32` — Parse int64
- `Viper.Parse.Double(cstr,ptr f64)->i32` — Parse double

#### Viper.Diagnostics

Error and diagnostic utilities:
- `Viper.Diagnostics.Trap(str)->void` — Trigger runtime trap

### Runtime Classes (Viper.*)

Canonical runtime classes are exposed under the `Viper.*` root and are used directly by the BASIC frontend. These are first‑class and tested:

#### Viper
- `Viper.Object` — Base class for all objects
  - Methods:
    - `ToString() -> STRING` — default returns the class qualified name
    - `Equals(OBJECT other) -> BOOL` — reference equality by default
    - `GetHashCode() -> I64` — process‑consistent hash derived from the object pointer
    - `ReferenceEquals(OBJECT a, OBJECT b) -> BOOL` — static; reference equality
- `Viper.String` — Managed string type (BASIC `STRING` is an alias)
  - Properties: `Length -> I64`, `IsEmpty -> BOOL`
  - Methods:
    - `Substring(I64 start, I64 length) -> STRING` — Extract substring
    - `Concat(STRING other) -> STRING` — Concatenate strings
    - `Left(I64 count) -> STRING` — First N characters
    - `Right(I64 count) -> STRING` — Last N characters
    - `Mid(I64 start) -> STRING` — Substring from position to end
    - `MidLen(I64 start, I64 length) -> STRING` — Substring with length
    - `Trim() -> STRING` — Remove leading/trailing whitespace
    - `TrimStart() -> STRING` — Remove leading whitespace
    - `TrimEnd() -> STRING` — Remove trailing whitespace
    - `ToUpper() -> STRING` — Convert to uppercase
    - `ToLower() -> STRING` — Convert to lowercase
    - `IndexOf(STRING needle) -> I64` — Find first occurrence (1-based, 0 if not found)
    - `IndexOfFrom(I64 start, STRING needle) -> I64` — Find from position
    - `Chr(I64 code) -> STRING` — Character from ASCII code (static)
    - `Asc() -> I64` — ASCII code of first character

#### Viper.Text
- `Viper.Text.StringBuilder` — Mutable string builder
  - Ctor: `NEW()`
  - Properties: `Length -> I64`, `Capacity -> I64`
  - Methods: `Append(STRING) -> OBJECT` (returns the builder for chaining), `Clear() -> VOID`, `ToString() -> STRING`

#### Viper.IO
- `Viper.IO.File` — File operations class (static utility)
  - Methods (static): `Exists(STRING) -> BOOL`, `ReadAllText(STRING) -> STRING`, `WriteAllText(STRING,STRING) -> VOID`, `Delete(STRING) -> VOID`

#### Viper.Collections
- `Viper.Collections.List` — Dynamic list of object references (non‑generic)
  - Ctor: `NEW()`; Property: `Count -> I64`
  - Methods: `Add(OBJECT)`, `Clear()`, `RemoveAt(I64)`, `get_Item(I64)->OBJECT`, `set_Item(I64,OBJECT)`

#### Viper.Math
- `Viper.Math` — Mathematical functions (static utility)
  - Methods (static):
    - `Abs(F64) -> F64` — Absolute value (float)
    - `AbsInt(I64) -> I64` — Absolute value (integer)
    - `Sqrt(F64) -> F64` — Square root
    - `Sin(F64) -> F64` — Sine (radians)
    - `Cos(F64) -> F64` — Cosine (radians)
    - `Tan(F64) -> F64` — Tangent (radians)
    - `Atan(F64) -> F64` — Arctangent (radians)
    - `Floor(F64) -> F64` — Round down
    - `Ceil(F64) -> F64` — Round up
    - `Pow(F64, F64) -> F64` — Power function
    - `Log(F64) -> F64` — Natural logarithm
    - `Exp(F64) -> F64` — Exponential (e^x)
    - `Sgn(F64) -> F64` — Sign (-1, 0, +1) for float
    - `SgnInt(I64) -> I64` — Sign (-1, 0, +1) for integer
    - `Min(F64, F64) -> F64` — Minimum of two floats
    - `Max(F64, F64) -> F64` — Maximum of two floats
    - `MinInt(I64, I64) -> I64` — Minimum of two integers
    - `MaxInt(I64, I64) -> I64` — Maximum of two integers

#### Viper.Console
- `Viper.Console` — Console I/O (static utility)
  - Methods (static): `WriteLine(STRING)->VOID`, `ReadLine()->STRING`

#### Viper.Random
- `Viper.Random` — Random number generation (static utility)
  - Methods (static):
    - `Seed(I64) -> VOID` — Seed the random number generator
    - `Next() -> F64` — Return next random number in [0, 1)

#### Viper.Environment
- `Viper.Environment` — Command-line and environment (static utility)
  - Methods (static):
    - `GetArgumentCount() -> I64` — Number of command-line arguments
    - `GetArgument(I64 index) -> STRING` — Get argument by index (0-based)
    - `GetCommandLine() -> STRING` — Full command line as single string

#### Viper.Time
- `Viper.Time` — Time and timing utilities (static utility)
  - Methods (static):
    - `GetTickCount() -> I64` — Milliseconds since program start
    - `Sleep(I32 ms) -> VOID` — Pause execution for milliseconds

#### Viper.Terminal
- `Viper.Terminal` — Terminal/console control (static utility)
  - Methods (static):
    - `Clear() -> VOID` — Clear the screen
    - `SetColor(I32 fg, I32 bg) -> VOID` — Set foreground/background colors
    - `SetPosition(I32 row, I32 col) -> VOID` — Move cursor position
    - `SetCursorVisible(I32 visible) -> VOID` — Show/hide cursor (0=hide, 1=show)
    - `SetAltScreen(I32 enable) -> VOID` — Switch to/from alternate screen buffer
    - `Bell() -> VOID` — Sound terminal bell
    - `GetKey() -> STRING` — Wait for and return keypress
    - `GetKeyTimeout(I32 ms) -> STRING` — Wait with timeout (empty if timeout)
    - `InKey() -> STRING` — Non-blocking key check (empty if no key)

**Note:** Legacy `Viper.System.*` aliases have been removed. Use the canonical `Viper.*` names.

### Legacy Aliases

For backward compatibility, legacy `rt_*` function names are maintained as aliases when built with `-DVIPER_RUNTIME_NS_DUAL=ON` (currently the default). Examples:
- `rt_print_str` → `Viper.Console.PrintStr`
- `rt_print_i64` → `Viper.Console.PrintI64`
- `rt_len` → `Viper.Strings.Len`

New code should use the canonical `Viper.*` names.

### OOP Runtime vs Procedural Helpers

The OOP `Viper.*` classes are the preferred surface for new code. The legacy procedural helpers (e.g., `Viper.Strings.Len`, `Viper.IO.*`) remain available and are used internally by some lowering bridges for backwards compatibility. Migration is straightforward: replace procedural calls with equivalent class property/method calls as listed above.

### Examples

Object methods on user classes:

```basic
NAMESPACE App
  CLASS C
  END CLASS
END NAMESPACE

DIM o AS App.C
o = NEW App.C()
PRINT o.ToString()   ' prints "App.C"
PRINT o.Equals(o)    ' 1
```

Working with strings via Viper.String:

```basic
DIM s AS Viper.String
s = "  Hello World  "
PRINT s.Length            ' 15
PRINT s.IsEmpty           ' 0
PRINT s.Trim()            ' "Hello World"
PRINT s.ToUpper()         ' "  HELLO WORLD  "
PRINT s.Left(7)           ' "  Hello"
PRINT s.Right(7)          ' "World  "
PRINT s.IndexOf("World")  ' 9
PRINT s.Substring(3, 5)   ' "Hello"
```

StringBuilder for efficient text composition:

```basic
DIM sb AS Viper.Text.StringBuilder
sb = NEW Viper.Text.StringBuilder()
sb.Append("hello").Append(", world")
PRINT sb.ToString()
```

File I/O using Viper.IO.File:

```basic
USING Viper.IO
File.WriteAllText("out.txt", "data")
IF File.Exists("out.txt") THEN
  PRINT File.ReadAllText("out.txt")
  File.Delete("out.txt")
END IF
```

In‑memory collections with List:

```basic
DIM list AS Viper.Collections.List
list = NEW Viper.Collections.List()
list.Add(NEW App.C())
PRINT list.Count
PRINT list.get_Item(0).ToString()
list.set_Item(0, list)
list.RemoveAt(0)
list.Clear()
```

Mathematical functions with Viper.Math:

```basic
USING Viper
PRINT Math.Sqrt(16)          ' 4
PRINT Math.Abs(-3.14)        ' 3.14
PRINT Math.Sin(0)            ' 0
PRINT Math.Pow(2, 10)        ' 1024
PRINT Math.Min(5.0, 3.0)     ' 3
PRINT Math.MaxInt(10, 20)    ' 20
```

Random numbers with Viper.Random:

```basic
USING Viper
Random.Seed(12345)           ' Seed for reproducibility
DIM r AS DOUBLE
r = Random.Next()            ' Returns value in [0, 1)
PRINT r
```

Command-line arguments with Viper.Environment:

```basic
USING Viper
DIM argc AS INTEGER
argc = Environment.GetArgumentCount()
PRINT "Arguments: "; argc
FOR i = 0 TO argc - 1
  PRINT "  "; i; ": "; Environment.GetArgument(i)
NEXT i
```

Timing with Viper.Time:

```basic
USING Viper
DIM start AS LONG
start = Time.GetTickCount()
' ... do work ...
Time.Sleep(100)              ' Pause 100ms
PRINT "Elapsed: "; Time.GetTickCount() - start; "ms"
```

Terminal control with Viper.Terminal:

```basic
USING Viper
Terminal.Clear()                    ' Clear screen
Terminal.SetColor(14, 1)            ' Yellow on blue
Terminal.SetPosition(10, 20)        ' Move cursor
PRINT "Hello!"
Terminal.SetCursorVisible(0)        ' Hide cursor
DIM key AS STRING
key = Terminal.GetKeyTimeout(5000)  ' Wait 5 seconds for key
IF key <> "" THEN PRINT "You pressed: "; key
Terminal.SetCursorVisible(1)        ' Show cursor
```

---

## Future Enhancements (Track B)

The following features are planned for future releases:

- **Namespace-level visibility**: Control which types are exported from a namespace
- **Nested namespace imports**: `USING Graphics.Rendering.*` to import all types
- **Namespace forwarding**: Re-export types from other namespaces

These features are not yet implemented in Track A.

---

## Summary

Viper BASIC namespaces provide:

- **Organization**: Group related types hierarchically
- **Collision avoidance**: Multiple libraries can define the same type names
- **Convenience**: USING directives reduce verbosity
- **Clarity**: Fully-qualified names show exactly where types come from

The system is designed to be simple, predictable, and compatible with BASIC's case-insensitive semantics.

For more examples, see the golden tests in `tests/golden/basic/namespace_*.bas` and the e2e tests in `tests/e2e/test_namespace_e2e.cpp`.
