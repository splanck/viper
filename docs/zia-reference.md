---
status: active
audience: public
last-verified: 2026-05-16
---

# Zia — Reference

Complete language reference for Zia. This document describes **syntax**, **types**, **statements**, **expressions**, **declarations**, and **runtime integration**. For a tutorial introduction, see **[Zia Getting Started](zia-getting-started.md)**. For the formal EBNF grammar, see **[Zia Grammar](zia-grammar.md)**.

---

## Key Language Features

- **Static typing**: All variables have compile-time types with inference
- **Class types**: Reference semantics with identity, methods, inheritance, properties, static members, and destructors
- **Struct types**: Copy semantics with stack allocation
- **Interfaces**: Contracts with full runtime itable dispatch and optional default method bodies
- **Enums**: Named sets of integer constants with exhaustiveness checking in match
- **Generics**: Parameterized types, methods, and functions with optional interface constraints (`List[T]`, `class Box[T: Named]`, `func max[T: Comparable]`)
- **Exception handling**: `try`/`catch`/`finally` with structured error propagation
- **Modules**: File-based modules with bind system
- **C-like syntax**: Familiar braces, semicolons, and operators
- **Runtime library**: Typed access to supported Viper.* classes and safe callback bridges

---

## Table of Contents

- [Program Structure](#program-structure)
- [Lexical Elements](#lexical-elements)
- [Types](#types)
- [Expressions](#expressions)
- [Statements](#statements) (includes try/catch/finally)
- [Declarations](#declarations) (includes default parameters)
- [Class Types](#class-types) (includes properties, static members, destructors)
- [Struct Types](#struct-types)
- [Interfaces](#interfaces)
- [Enums](#enums)
- [Modules and Imports](#modules-and-imports)
- [Namespaces](#namespaces)
- [Runtime Library Access](#runtime-library-access)
- [Operator Precedence](#operator-precedence)
- [Reserved Words](#reserved-words)

---

## Program Structure

A Zia source file has the following structure:

```rust
module ModuleName;

// Bind declarations (bring other modules into scope)
bind "path/to/module";

// Global variable declarations
var globalVar: Type = value;
final CONSTANT = value;       // Compile-time constant or runtime-initialized immutable global

// Type declarations (class, struct, interface)
class MyClass { ... }
struct MyStruct { ... }
interface MyInterface { ... }

// Function declarations
func functionName(params) -> ReturnType { ... }

// Entry point
func start() { ... }
```

### Entry Point

The `start()` function is the program entry point. It takes no parameters and returns void:

```rust
func start() {
    // Program execution begins here
}
```

---

## Lexical Elements

### Comments

```rust
// Single-line comment

/* Multi-line
   comment */
```

### Identifiers

Identifiers start with a letter or underscore, followed by letters, digits, or underscores:

```text
identifier  ::= [a-zA-Z_][a-zA-Z0-9_]*
```

### Literals

#### Integer Literals

```rust
42          // Decimal
1_000_000   // Decimal with digit separators
0xFF        // Hexadecimal
0xDEAD_BEEF // Hexadecimal with digit separators
0x8000000000000000 // Hex bit pattern for Integer min
0b1010      // Binary
0b1010_0101 // Binary with digit separators
```

#### Floating-Point Literals

```rust
3.14159     // Decimal
1_024.5     // Decimal with digit separators
1e10        // Scientific notation
2.5e-3      // Scientific with negative exponent
1.0e+1_0    // Digit separators in exponent digits
```

Digit separators (`_`) are allowed only between digits. Decimal literals use the
signed `Integer` range; hexadecimal literals are accepted as 64-bit bit patterns,
so values with the high bit set are interpreted as negative `Integer` values.

#### String Literals

```rust
"Hello, World!"           // Basic string
"Line 1\nLine 2"          // With escape sequences
"Value: ${expression}"    // String interpolation
"""multi
line"""                  // Triple-quoted string literal
```

**Escape sequences:**

| Sequence | Meaning |
|----------|---------|
| `\n` | Newline |
| `\t` | Tab |
| `\r` | Carriage return |
| `\\` | Backslash |
| `\"` | Double quote |
| `\'` | Single quote |
| `\0` | Null character |
| `\xNN` | Hex byte (two hex digits) |
| `\uXXXX` | Unicode code point (four hex digits, UTF-8 encoded) |
| `\$` | Dollar sign (in interpolated strings) |

Triple-quoted strings may span multiple lines and still honor the same escape
sequences. They do not use `${...}` interpolation; contents are treated as a
plain string literal.

#### Boolean Literals

```rust
true
false
```

#### Null Literal

```rust
null    // Used with optional types
```

#### Unit Literal

```rust
()      // Unit literal
```

Use `null` for optional empty values. `()` is accepted as a standalone statement,
void return value, `Unit` return value, `Unit` argument, and `Result[Unit]`
payload. It cannot be stored in variables, collections, maps, sets, or ordinary
assignments.

---

## Types

### Primitive Types

| Type | Description | Default Value |
|------|-------------|---------------|
| `Boolean` | True or false | `false` |
| `Integer` | 64-bit signed integer | `0` |
| `Number` | 64-bit floating-point | `0.0` |
| `Byte` | 8-bit value lowered through the IL integer path | `0` |
| `String` | UTF-8 string | `""` |
| `Any` | Managed top type for boxed values, objects, strings, and function references | `null` |
| `Never` | Bottom type for code paths that do not produce a value | — |
| `Unit` | Explicit no-value marker for `Result[Unit]` and `()` | `()` |

The canonical type spellings are PascalCase. Compatibility aliases are accepted
for common scalar types: `int`/`Int`/`integer`, `double`/`Double`/`float`/`Float`
for `Number`, `bool`/`Bool`/`boolean`, plus lowercase `string`, `byte`, `unit`,
`void`, `error`, `any`, and `never`.

`Ptr` is not part of the Zia source surface. Runtime handles are exposed as
typed runtime classes, collections, `Any`, or function references; raw pointer
details remain inside the runtime and backend.

`Void` is the implicit return type of functions and methods with no `->` clause.
`Void` and `Never` are not value types: they cannot be used for parameters,
locals, globals, or fields.

### Optional Types

Optional types can hold a value or `null`:

```rust
var name: String? = null;   // Optional string
var count: Integer? = 42;   // Optional with value
```

Optional values support safe access with `?.`, defaults with `??`, try propagation
with postfix `?`, and force-unwrap with `!`. `??` requires an optional left-hand
operand and a fallback value assignable to the optional's inner type.
Use `null`, not `()`, to create an empty optional value.

Class instances, interfaces, strings, collections, `Any`, and typed runtime
classes are reference-backed values and can carry a runtime null handle for
interop. Use `T?` in public APIs and ordinary program logic when absence is part
of the type contract; bare reference nulls are accepted for runtime-facing code
and should still be guarded with a null check or force unwrap before member
access when a null value is possible.

### Generic Types

Parameterized types with type arguments:

```rust
List[Integer]           // List of integers
List[Player]            // List of class instances
Map[String, Integer]    // Map from strings to integers
[Integer]               // Shorthand for List[Integer]
Result[Integer]         // Success value or error string
```

Map keys are restricted to `String`.

`Result[T]` values are constructed with `Ok(value)` and `Err(message)`. The
success type is `T`; the error payload is a `String`.

Generic declarations may constrain type parameters to interfaces. Constraints
are supported on functions, methods, classes, structs, and interfaces, and are
checked when a generic is instantiated:

```rust
interface Named {
    func name() -> String;
}

class Box[T: Named] {
    T value;
}
```

Each type parameter may name at most one interface constraint. The constraint
may be qualified, such as `T: Contracts.Named`. Multiple bounds (`T: A + B`)
and `where` clauses are future extensions, not accepted syntax. A concrete class
argument satisfies a constraint through interfaces implemented by the class or
by any base class; struct arguments satisfy constraints through their own
`implements` list.

### Class Types

Reference types defined with the `class` keyword:

```rust
var player: Player = new Player();
```

### Struct Types

Copy-semantics types defined with the `struct` keyword:

```rust
var point: Point;
```

### Tuple Types

Tuple types group multiple values into a single type:

```rust
var pair: (Integer, String) = (42, "hello");
var x = pair.0;     // Access by index
```

### Fixed-Size Array Types

Arrays with a compile-time size:

```rust
var grid: Integer[100];     // Fixed array of 100 integers
```

Fixed-size array indexes must be integral. Reads and writes use the configured
bounds-checking mode, and sub-width indexes such as `Byte` are widened before
offset arithmetic. Fixed arrays are available as local variables, struct/class
fields, and assignable values; assignment copies the inline elements.

### Set Types

Sets hold unique values. Created with set literals or the `Set` constructor:

```rust
var s: Set[Integer] = {1, 2, 3};
var empty: Set[Integer] = {};
```

### Function Types

Function signatures as types:

```rust
(Integer, Integer) -> Integer   // Function taking two ints, returning int
() -> void                      // Function taking nothing, returning void
```

---

## Expressions

### Literals

```rust
42                  // Integer literal
3.14                // Number literal
"hello"             // String literal
true                // Boolean literal
null                // Null literal
```

### Identifiers

```rust
variableName        // Variable reference
```

### Unary Operators

| Operator | Description | Example |
|----------|-------------|---------|
| `-` | Negation | `-x` |
| `!` / `not` | Logical NOT | `!flag` or `not flag` |
| `~` | Bitwise NOT | `~bits` |
| `&` | Function reference | `&myFunc` |

### Function References

The `&` operator explicitly obtains a typed function reference. A bare function
name is also accepted where the expected type is a function reference, or where
a runtime API exposes a safe callback bridge:

```rust
bind Viper.Threads;

func handler(arg: Any) {
    // Handle something
}

func takeCallback(callback: Any) {
    // Store or forward the callback as a managed value
}

func start() {
    var h = &handler;           // Inferred function-reference type
    takeCallback(&handler);     // Pass through Any
    takeCallback(handler);      // Also accepted in callback context

    // With Thread.Start
    var thread = Thread.Start(&handler, 0);
    var thread2 = Thread.Start(handler, 0);
}
```

**Notes:**
- The `&` operator can only be applied to function names, not variables or expressions
- Function names are still ordinary call expressions when followed by `(...)`
- Use with `Viper.Threads.Thread.Start()` to spawn threads with custom entry points

### Binary Operators

#### Arithmetic

| Operator | Description |
|----------|-------------|
| `+` | Addition |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Division |
| `%` | Modulo |

#### Comparison

| Operator | Description |
|----------|-------------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `<=` | Less or equal |
| `>` | Greater than |
| `>=` | Greater or equal |

#### Logical (Short-Circuit)

| Operator | Description |
|----------|-------------|
| `&&` / `and` | Logical AND |
| `||` / `or` | Logical OR |

#### Bitwise

| Operator | Description |
|----------|-------------|
| `&` | Bitwise AND |
| `|` | Bitwise OR |
| `^` | Bitwise XOR |

#### Assignment

| Operator | Description |
|----------|-------------|
| `=` | Assignment |
| `+=` | Add and assign |
| `-=` | Subtract and assign |
| `*=` | Multiply and assign |
| `/=` | Divide and assign |
| `%=` | Modulo and assign |
| `<<=` | Shift left and assign |
| `>>=` | Shift right and assign |
| `&=` | Bitwise AND and assign |
| `|=` | Bitwise OR and assign |
| `^=` | Bitwise XOR and assign |

Compound assignment operators desugar to `a = a op b` at parse time. The left-hand side must be a mutable variable, field, or indexed expression.
Targets with side effects, such as a function call inside the indexed receiver or
index expression, are rejected because compound assignment would otherwise need
to evaluate the target twice.

Operators are built into the language and apply to the built-in scalar, string, and
collection types. Zia does not currently support user-defined operator overloading — a
`struct` or `class` cannot define its own `+`, `==`, and so on; use named methods for
custom value semantics.

### Ternary Operator

```rust
condition ? thenValue : elseValue
```

### Match Expression

```rust
var result = match value {
    0 => "zero";
    n if n > 0 => "positive";
    _ => "negative";
};
```

### Field Access

```rust
object.field            // Access field
object.method()         // Call method
```

### Optional Chaining and Unwrapping

```rust
object?.field           // Null if object is null, otherwise optional field value
object?.field?.subfield // Chained optional access
object?.method(args)    // Calls method only when object is non-null
value ?? defaultValue   // Returns defaultValue if value is null
value!                  // Force-unwrap: converts T? to T, traps if null
```

Optional chaining uses the same field and property lookup rules as normal member
access. It supports stored fields, readable computed properties, runtime
properties, string `Length`, and collection count aliases such as `Count`,
`Length`, `Len`, `count`, `length`, and `size`. It also supports method calls
on optional struct, class, and interface receivers; non-null calls return the
method result wrapped as an optional unless it is already optional, while null
receivers skip the call and return null. Optional calls to `Void` methods simply
no-op when the receiver is null.

The force-unwrap operator `!` asserts that an optional or reference-backed value
is non-null and extracts the inner value when the source is `T?`. If the value is
null at runtime, the program terminates. On a value whose static type has already
been narrowed to a non-optional reference, `!` is accepted as a redundant runtime
assertion. Use after a null guard or when you are certain the value is non-null:

```rust
if maybePage == null { return null; }
var page = maybePage!;              // Safe: null was handled above
```

### Indexing

```rust
list[index]             // Access list element
map["key"]              // Access map value (keys are String)
string[index]           // Read one-character String
fixed[index]            // Access fixed-size array element
```

List, map, and fixed-size array indexes are assignable when the container is
mutable. String indexes are read-only.

### Block Expressions

A block can produce a value when the last statement is an expression without a trailing semicolon:

```rust
var x = {
    var a = 10;
    var b = 20;
    a + b           // Block evaluates to 30
};
```

A bare `{expr}` remains a set literal. Use statement-bearing blocks, or block
bodies in lambda, `if`, and `match` contexts, when you want block-expression
semantics.

### If Expressions

`if`/`else` can be used as value-producing expressions:

```rust
var sign = if x > 0 { "positive" } else { "non-positive" };
```

Both branches must be present and produce the same type.

### Try Expression

The postfix `?` operator propagates optional absence or `Result` errors out of
the enclosing function.

For an optional operand, `?` returns `null` immediately when the operand is null;
otherwise it unwraps the inner value. The enclosing function must return an
optional type, and the unwrapped operand value must be assignable to that
optional's inner type.

```rust
func findUser(id: Integer) -> User? {
    var record = database.lookup(id)?;  // Returns null if lookup returns null
    return record.toUser();              // toUser is a user-defined method on User
}
```

For a `Result[T]` operand, `?` returns the `Err` immediately from an enclosing
function that also returns `Result[...]`; otherwise it unwraps the `Ok` payload:

```rust
func readScore(path: String) -> Result[Integer] {
    var text: String = readFile(path)?;
    return Ok(parseScore(text));
}
```

> **Note:** The postfix `?` (try expression, propagates null or `Err` out of the function) is distinct from `?.` (optional chaining, returns null if the receiver is null without unwinding). They can be combined: `obj?.field?` chains optional access with null propagation.

### Function/Method Calls

```rust
functionName(arg1, arg2)
object.methodName(arg1)
```

Strings also support instance-style calls for common `Viper.String` operations:

```rust
var name = "  viper  ".Trim().ToUpper();
var part = "abcdef".Substring(1, 3);   // "bcd"
```

#### Named Arguments

Arguments can be passed by name using `name: value` syntax. Named arguments are supported for user-defined functions, methods, and constructors that have declared parameter names. Runtime APIs (`Viper.*` methods, including `String.Substring`) require positional arguments — they don't carry parameter names through the IL, so use the positional form for those.

```rust
func createRect(x: Integer, y: Integer, w: Integer, h: Integer) -> Rect { ... }

var r = createRect(x: 10, y: 20, w: 100, h: 50);    // OK: user-defined function
var part = "abcdef".Substring(1, 3);                 // Runtime API: positional only
```

### Object Creation

```rust
new ClassName(args)     // Create a class or struct value
```

### Struct Type Initialization (Struct Literals)

Struct types can be initialized with field assignments:

```rust
struct Point {
    Integer x;
    Integer y;
}

var p = Point { x = 10, y = 20 };
var q = Geometry.Point { x: 30, y: 40 };  // ':' is also accepted
```

Struct literals are valid in variable, field, global, return, parameter default,
call argument, assignment right-hand side, ternary branch, collection element,
match-arm expression, and single-expression function or method initializer
contexts. Struct literals are not parsed in statement conditions such as
`if value { ... }`, because the following braces are reserved for the statement
body.
The type name may be qualified through a bound module or namespace. Each field
may be assigned at most once with either `=` or `:`, and literal values must be
assignable to the declared field type. Struct literals can be nested inside field
values. Omitted fields use their declared initializer when one is present;
otherwise they use the typed default for the field type. Private fields may only
be initialized inside the declaring type.

### Tuple Expressions

Tuples group multiple values. Access elements with `.0`, `.1`, etc.:

```rust
var pair = (42, "hello");
var num = pair.0;           // 42
var str = pair.1;           // "hello"
var (n: Integer, s: String) = pair;
```

Tuple destructuring is supported for tuple declarations with matching arity:

```rust
var (x, y) = (1, 2);
final (code: Integer, label: String) = (200, "ok");
var (r, g, b) = (255, 128, 0);
```

The initializer must be a tuple with the same number of elements. Optional type
annotations are checked against the corresponding tuple element.

### Collection Literals

```rust
var list = [1, 2, 3];              // List[Integer]
var map = {"key": 42, "other": 7}; // Map[String, Integer]
var set = {1, 2, 3};               // Set[Integer]
```

`{}` is the empty map literal by default. `map {}` is an explicit empty map. In a
declared `Set[T]` initializer `{}` is an empty set; `set {}` and constructors
such as `new Set[Integer]()` are unambiguous empty set forms.
Non-empty list, map, and set literals must be homogeneous: all list/set elements
and all map values must have compatible types.
List, map, and set literals permit a trailing comma.

### Generic Collection Operations

```rust
var nums: List[Integer] = [];
nums.add(10);
nums.set(0, 20);
var first = nums.get(0);
var hasTen = nums.has(10);
nums.insert(0, 5);
nums.removeAt(1);
nums.sortDesc();
nums.shuffle();
var n = nums.count();

var ages: Map[String, Integer] = new Map[String, Integer]();
ages.set("Alice", 30);
var maybeAliceAge: Integer? = ages.get("Alice");
var aliceAge = ages.get("Alice") ?? 0;
var hasAlice = ages.has("Alice");
var ageCount = ages.count();

var tags: Set[String] = new Set[String]();
tags.add("urgent");
var hasUrgent = tags.has("urgent");
var tagCount = tags.count();
```

These are language-level generic collections. The object-style runtime classes
under `Viper.Collections.*` use their own constructor and method surface (for
example `Map.New()`, `List.New()`, `Set.New()`).
Collection count aliases such as `.Count`, `.Length`, `.Len`, `.count`,
`.length`, and `.size` are read-only properties. Unknown collection and string
fields or methods are compile-time errors.
List index arguments are widened to the runtime index width before dispatch, so
sub-width integral values such as `Byte` are accepted for `get`, `set`,
`insert`, and `removeAt`. `Map.get` returns an optional value; for
`Map[String, String]`, a missing key returns `null`, not an empty string.

### Range Expressions

```rust
start..end              // Exclusive range [start, end)
start..=end             // Inclusive range [start, end]
```

Ranges can be used directly as list-producing expressions or as `for ... in`
sources. Range-only modifiers are available before iteration:

```rust
var values = (0..=10).rev().step(2);  // List[Integer]
for (i in (0..10).rev()) { ... }
for (i in (0..10).step(2)) { ... }
for (i in (0..=10).rev().step(2)) { ... }
```

`.rev()` and `.step(n)` are only valid on range expressions, not arbitrary
`List[Integer]` values. `step` must be a positive, non-zero integer; literal
zero and negative steps are rejected at compile time and dynamic non-positive
steps trap before the loop or list construction starts. A range chain may contain
at most one `.step(...)`; use a single step value after any `.rev()` modifier.
Range updates guard the inclusive endpoint before incrementing or decrementing,
so boundary values such as `Integer` min/max do not overflow after the final
element.

### Type Operations

```rust
value is Type           // Type check (returns Boolean)
value as Type           // Type cast
```

### Lambda Expressions

```rust
(x: Integer) => x + 1             // Single parameter
(a: Integer, b: Integer) => a + b // Multiple parameters
() => 42                          // No parameters
```

Lambda parameters must include explicit type annotations.

### Result Values

`Result[T]` represents either `Ok(T)` or `Err(String)`:

```rust
var ok: Result[Integer] = Ok(7);
var err: Result[Integer] = Err("bad");

if ok.isOk() {
    SayInt(ok.unwrap());
}

var fallback = err.unwrapOr(0);
var message = err.unwrapErr();
```

Result patterns are supported in `match` statements and expressions:

```rust
match ok {
    Ok(value) => SayInt(value);
    Err(message) => Say(message);
}
```

`unwrap()` traps when called on `Err`, and `unwrapErr()` traps when called on
`Ok`. `unwrapOr(default)` returns the success value or the provided default.
Postfix `?` unwraps `Ok` and propagates `Err` from functions that return
`Result[...]`.

### `is` Type Check

The `is` operator returns `Boolean`. For classes it checks runtime type identity
with subclass relationships; for interfaces it checks implemented interfaces:

```rust
class Animal { }
class Dog extends Animal { }
class Cat extends Animal { }

func check(a: Animal) {
    if a is Dog {
        // true if a is Dog or any subclass of Dog
    }
    if a is Animal {
        // always true for any Animal subclass
    }
}
```

Primitive checks are exact-type checks known at compile time after the operand is
evaluated. Optional values can be checked against their inner type; the result is
`false` when the optional is null.

---

## Statements

### Variable Declaration

```rust
var name = value;                   // Type inferred
var name: Type = value;             // Explicit type
var name: Type;                     // Default initialized
final name = value;                 // Immutable local
let name = value;                   // Compatibility alias for final
```

### Expression Statement

Any expression can be used as a statement:

```rust
functionCall();
object.method();
x = x + 1;
```

### Block Statement

```rust
{
    statement1;
    statement2;
}
```

### If Statement

```rust
if condition {
    // then branch
}

if condition {
    // then branch
} else {
    // else branch
}

if condition1 {
    // first branch
} else if condition2 {
    // second branch
} else {
    // else branch
}
```

Parentheses around conditions are optional. Braced blocks are preferred, but a
single statement body is accepted for compact control flow:

```rust
if ready startWork();
while pending tick();
```

### While Statement

```rust
while condition {
    // body
}
```

### For Statement (C-style)

```rust
for (init; condition; update) {
    // body
}

// Example
for (var i = 0; i < 10; i = i + 1) {
    // body
}
```

The initializer may be a variable declaration or expression statement. `let` is
accepted as an alias for `final` here too, so a `let` loop binding cannot be
assigned in the update expression.

### For-In Statement

```rust
for variable in iterable {
    // body
}

// List iteration
for item in list {
    // item is element type
}

// Map iteration (keys only)
for key in map {
    // key is String
}

// Map iteration with tuple binding
for key, value in map {
    // key is String, value is map value type
}

// List / Set iteration with tuple binding
for index, item in list {
    // index is Integer, item is element type
}
for index, item in set {
    // index is Integer, item is element type
}

// Runtime collection iteration
for item in queue {
    // Queue[T], Stack[T], Deque[T], List[T], Ring[T], and Heap[T] iterate as T
}
for index, item in stack {
    // tuple binding is also supported for runtime collections
}

// Parenthesized tuple form
for ((key, value) in map) {
    // same as: for key, value in map { ... }
}

// Range iteration
for i in 0..10 {        // 0 to 9
    // body
}

for i in 0..=10 {       // 0 to 10
    // body
}
```

### Return Statement

```rust
return;                 // Return void
return expression;      // Return value
```

### Break Statement

```rust
break;                  // Exit innermost loop
```

### Continue Statement

```rust
continue;               // Skip to next iteration
```

### Guard Statement

```rust
guard condition else {
    return;             // Must exit scope
}
// condition is true here
```

### Match Statement

```rust
match value {
    pattern1 => { /* body */ }
    pattern2 => { /* body */ }
    _ => { /* default */ }
}
```

Supported patterns:

- `_` wildcard
- Literals (`0`, `"text"`, `true`, `false`, `null`)
- Binding identifiers (`x`)
- Tuple patterns with matching arity (`(x, y)`, `(r, g, b)`)
- Constructor patterns (`Point(x, y)`, `Some(value)`, `None`)
- OR patterns (`pattern1 | pattern2 | pattern3 => ...`) — multiple alternatives for one arm
- Enum variant patterns (`Color.Red`, `Direction.Left`)
- Guards (`pattern if condition => ...`)

OR pattern example:

```rust
match x {
    1 | 2 | 3 => Say("small");
    10 | 20 => Say("round");
    _ => Say("other");
}
```

Statement arms may use a single statement directly after `=>`:

```rust
func classify(x: Integer) -> Integer {
    match x {
        1 => return 10;
        _ => return 20;
    }
    return -1;
}
```

### Try/Catch/Finally Statement

Structured exception handling for runtime errors:

```rust
try {
    riskyOperation();
} catch(e) {
    Say("caught: " + e.message);
} catch(e: DivideByZero) {
    Say("math failed: " + e.kind);
}
```

- The `try` block is always required.
- One or more `catch` clauses may follow the `try` block. Named error binding
  uses parentheses: `catch(e) { ... }`.
- A named catch binding has type `Error`. It exposes:
  - `kind` / `type`: runtime error kind name such as `"RuntimeError"` or `"DivideByZero"`
  - `message`: `throw` payload text for language throws, or a default message for runtime faults
  - `code`: numeric runtime error code
  - `line`: source line if available, otherwise `-1`
  - `location`: formatted source location string when available
- Anonymous catch `catch { ... }` is supported.
- Typed catch is supported for runtime error kinds such as `DivideByZero`,
  `Bounds`, `RuntimeError`, and the catch-all alias `Error`.
- Catch clauses are checked in source order. A catch-all clause (`catch`,
  `catch(e)`, or `catch(e: Error)`) must be last.
- Both `catch` and `finally` are optional, but at least one must be present.
- `throw value;` raises a `RuntimeError`; runtime faults such as divide-by-zero keep
  their specific trap kind.
- `throw;` is a bare rethrow and is only valid inside a catch clause. It preserves
  the original error kind, code, line, and message.
- `finally` runs on normal exit, while unwinding, and before nonlocal control flow
  leaves the protected region through `return`, `break`, or `continue`. If no catch
  clause handles the error, or a typed catch does not match it, the original error
  is rethrown after the `finally` block runs. If the `finally` block itself throws
  while unwinding, that new error becomes the propagated error.

### Defer Statement

`defer` registers a cleanup action for the current block. Deferred actions run
when the block exits normally and before `return`, `break`, or `continue` leaves
the scope.

```rust
func writeLine(path: String, text: String) {
    var file = openFile(path);
    defer file.close();
    file.write(text);
}

func withBlockCleanup() {
    defer {
        Viper.Terminal.Say("leaving scope");
    }
}
```

Use `defer` for deterministic high-level cleanup. Direct memory release APIs are
runtime primitives; ordinary Zia code should rely on scoped cleanup, object
lifetimes, and `deinit`.

### Throw Statement

Raises a `RuntimeError`:

```rust
throw someErrorValue;
```

Inside a catch clause, `throw;` rethrows the active error.

---

## Declarations

### Function Declaration

```rust
func functionName(param1: Type1, param2: Type2) -> ReturnType {
    // body
    return value;
}

// Void return
func noReturn(param: Type) {
    // body
}
```

#### Default Parameter Values

Parameters may have default values. When a call omits trailing arguments, the default expressions are used:

```rust
func greet(name: String, greeting: String = "Hello") -> String {
    return greeting + ", " + name;
}

func start() {
    var msg1 = greet("Alice", "Hi");    // "Hi, Alice"
    var msg2 = greet("Bob");            // "Hello, Bob"
}
```

Default values must be trailing — a parameter with a default cannot be followed by a parameter without one.
Parameter and argument lists permit a trailing comma.

### Generic Function Declaration

Functions can be parameterized over types with optional interface constraints:

```rust
func identity[T](x: T) -> T {
    return x;
}

func findMax[T: Comparable](a: T, b: T) -> T {
    if a > b { return a; }
    return b;
}
```

Type parameters are declared in `[...]` after the function name. Constraints
(like `T: Comparable` or `T: Contracts.Named`) restrict the type parameter to
types implementing the named interface.
Each parameter supports a single interface constraint; multiple bounds and
`where` clauses are reserved for future language work.
Type inference works through generic container parameters such as `List[T]` and
`Map[String, T]`. Explicit type arguments may be comma-separated and nested:
`pair[Integer, String](1, "one")` and `identity[List[Integer]]([1])`.

Methods may also declare type parameters and constraints. Explicit generic
method calls use the same bracket syntax after the method name:

```rust
class Box {
    expose func id[T: Named](value: T) -> T {
        return value;
    }
}

var box = new Box();
var user: User = box.id[User](currentUser);
```

### Async Functions

Use `async func` to return a `Viper.Threads.Future`, and `await` to unwrap the completed payload:

```rust
async func fetchName() -> String {
    return "viper";
}

expose async func fetchPublicName() -> String {
    return "viper";
}

func start() {
    var name: String = await fetchName();
}
```

`await` is valid on values of type `Viper.Threads.Future`. The awaited value is unboxed back to the async function's declared return type.

### Variadic Parameters

The last parameter of a function may be variadic, accepting zero or more arguments that are collected into a `List`:

```rust
func sum(nums: ...Integer) -> Integer {
    var total = 0;
    for i in 0..nums.count() {
        total = total + nums.get(i);
    }
    return total;
}

func start() {
    var s = sum(1, 2, 3);   // s == 6
    var z = sum();           // z == 0
}
```

Only the last parameter may be variadic. Inside the function body, the variadic parameter is a `List[T]`.

### Single-Expression Functions

Functions whose body is a single expression can use `=` shorthand:

```rust
func double(x: Integer) -> Integer = x * 2;
func greet(name: String) -> String = "Hello, " + name;
```

This desugars to a `return` statement wrapping the expression. Works for both top-level functions and class methods.

### Foreign Function Declarations

`foreign func` declares a function whose body is provided by another module — typically the BASIC frontend or another linked translation unit. Foreign declarations have a signature but no body; the linker resolves the implementation at build time.

```rust
foreign func Factorial(n: Integer) -> Integer;
expose foreign func Render(canvas: Canvas, frame: Integer);
```

The semicolon terminator shown above is the documented form. A legacy spelling
without the semicolon is still accepted for compatibility.

Use a foreign declaration when the function is implemented in BASIC and called from Zia, or when binding to a runtime entry that isn't already exposed through `Viper.*`. Calling a foreign function uses the same syntax as any other function call. `expose` and `hide` may prefix ordinary, async, and foreign functions.

### Type Alias Declarations

Create compile-time type aliases with `type`:

```rust
type Name = String;
type Score = Integer;

bind Viper.Terminal;

func display(name: Name, score: Score) {
    Say(name);
    SayInt(score);
}
```

Aliases are resolved during semantic analysis and have no runtime representation.

### Global Variable Declaration

```rust
var globalName: Type = initialValue;
final CONSTANT = value;             // Scalar constants may be folded at compile time
final ITEMS: List[String] = ["a"];  // Aggregate/object finals initialize once at startup
```

---

## Class Types

Classes are reference types with identity, stored on the heap.

### Class Declaration

```rust
class ClassName[T: InterfaceName] {
    // Fields
    T fieldName;

    // Initializer method
    func init(value: T) {
        fieldName = value;
    }

    // Methods
    func methodName(params) -> ReturnType {
        return value;
    }
}
```

### Field Visibility

Fields and methods can be marked with visibility:

```rust
class Player {
    Integer health;         // Default visibility
    hide Integer secret;    // Private field
    expose String name;     // Public field
    expose weak var parent: Player?;
}
```

Class fields may use familiar `var name: Type;` syntax or the original
`Type name;` syntax. A `weak` field stores a non-owning reference to a class,
interface, `Any`, or optional reference type. Weak fields cannot be `static`,
and are loaded like ordinary fields:

```rust
class Node {
    expose weak var parent: Node?;
}
```

Member modifiers are checked for the declaration they modify:

- `weak` is valid only on fields.
- `override` is valid only on instance methods; `static override` is invalid.
- `static` is valid on fields and methods.
- `property` supports `expose`/`hide` but not `weak`, `override`, or `static`.
- `deinit` does not take visibility, `override`, `static`, or `weak`.

### Class Inheritance

```rust
class ChildClass extends ParentClass {
    // Additional fields and methods
}

class Widget extends UI.Control {
    // Qualified base class names are valid.
}
```

#### The `super` Keyword

Within a child class, `super` refers to the parent class's implementation. Use it to call the parent's methods:

```rust
class Child extends Parent {
    override func greet() -> String {
        return super.greet() + " (child)";
    }
}
```

#### The `override` Keyword

Methods that override a parent's method must be marked with `override`:

```rust
class Base {
    func describe() -> String { return "Base"; }
}

class Derived extends Base {
    override func describe() -> String { return "Derived"; }
}
```

`init` methods are constructor bodies. A child `init` with the same signature as
a parent `init` does not require the `override` keyword; call `super.init(...)`
when the parent initializer must run.

### Creating Instances

```rust
var obj = new ClassName(args);
```

`new Type(args)` calls `init(...)` when the type defines one. For structs and
classes without `init`, arguments are matched in field declaration order.

### Self Reference

Within methods, fields can be accessed directly or with `self`:

```rust
class Counter {
    Integer count;

    func increment() {
        count = count + 1;      // Direct access
        self.count = self.count + 1;  // Explicit self
    }
}
```

### Properties

Properties provide computed get/set accessors for class fields:

```rust
class Temperature {
    Number celsius;

    expose property fahrenheit: Number {
        get { return self.celsius * 1.8 + 32.0; }
        set(v) { self.celsius = (v - 32.0) / 1.8; }
    }
}
```

- The `get` body returns the computed value.
- The `set` body receives a parameter whose name is declared in parentheses.
- A property may omit `set` to be read-only.
- A property may omit `get` to be write-only; reading a write-only property is an error.
- Writing a read-only property is an error.
- Use `expose property` when the property should be accessible outside the declaring type.
- Properties are accessed like fields: `temp.fahrenheit` calls the getter, `temp.fahrenheit = 212.0` calls the setter.

### Static Members

Fields and methods marked `static` belong to the class type, not to instances:

```rust
class Counter {
    expose static Integer instanceCount;

    static func getCount() -> Integer {
        return instanceCount;
    }

    func init() {
        instanceCount = instanceCount + 1;
    }
}
```

- Static fields are stored in module-level runtime storage (not per-instance).
- Static methods have no `self` parameter.
- Access via the class name: `Counter.getCount()`, `Counter.instanceCount`.
- Use `expose static` when code outside the declaring type should access the
  field.

### Destructors

The `deinit` block defines cleanup logic that runs when an object is destroyed:

```rust
class FileHandle {
    Integer fd;

    deinit {
        // Cleanup code — release resources
        closeFile(self.fd);
    }
}
```

- At most one `deinit` block per class.
- The destructor automatically releases reference-typed fields after the user body executes.
- The generated IL function is named `TypeName.__dtor`.
- Bound runtime/module symbols remain visible inside `deinit`, just like other class members.
- If you need deterministic cleanup around a resource, use `defer` to call the
  resource's close/dispose method at scope exit.

---

## Struct Types

Struct types have copy semantics — assignments copy the entire struct.

### Struct Declaration

```rust
struct StructName[T: InterfaceName] {
    // Fields
    T fieldName;

    // Methods
    func methodName(params) -> ReturnType {
        return value;
    }
}
```

### Using Struct Types

```rust
struct Point {
    Integer x;
    Integer y;

    func init(px: Integer, py: Integer) {
        x = px;
        y = py;
    }
}

func start() {
    var p: Point;
    p.init(10, 20);
}
```

---

## Interfaces

Interfaces define contracts that classes and structs can implement.

### Interface Declaration

```rust
interface InterfaceName {
    func methodSignature(params) -> ReturnType;

    func defaultMethod() -> String {
        return "default";
    }
}
```

Interface methods may be abstract signatures ending in `;` or default method
bodies. Implementors may override a default method, but they are not required to
redeclare it. Default methods are bound into the interface table when a class or
struct does not provide its own implementation.

### Implementing Interfaces

```rust
class MyClass implements InterfaceName {
    expose func methodSignature(params) -> ReturnType {
        // Implementation
    }
}

struct MyStruct implements InterfaceName {
    expose func methodSignature(params) -> ReturnType {
        // Implementation
    }
}
```

Implementing methods must be marked `expose` (public visibility). Classes dispatch
through their object identity. Struct values are boxed when coerced to an
interface-typed value, and dispatch then uses adapter thunks for the struct
methods.
Implemented interface names may be qualified, such as
`class Button implements UI.Clickable`.

### Interface Dispatch

Interface-typed variables use runtime itable dispatch. Calling a method on an interface variable performs a lookup through the interface table (itable) to find the correct implementation:

```rust
interface IShape {
    func area() -> Number;
}

class Circle implements IShape {
    expose Number radius;
    expose func area() -> Number { return 3.14 * self.radius * self.radius; }
}

func printArea(s: IShape) {
    // Runtime dispatch via itable lookup
    var a = s.area();
}
```

At module initialization, a `__zia_iface_init` function registers each interface and binds implementation itables for both class and struct implementors. Method calls on interface-typed parameters dispatch through a managed function reference stored in the interface table.

---

## Enums

Enums define a type with a fixed set of named integer constants. Each variant is lowered to an `I64` constant at the IL level, while source programs still treat the value as its enum type.

### Declaration

```rust
enum Color {
    Red,
    Green,
    Blue,
}
```

Variants are automatically numbered starting from 0. A trailing comma after the last variant is permitted.

### Explicit Values

Variants may specify explicit integer values. Unspecified variants auto-increment from the previous value.

```rust
enum HttpStatus {
    OK = 200,
    NOT_FOUND = 404,
    INTERNAL_ERROR = 500,
}
```

Mixed auto-increment and explicit values:

```rust
enum Priority {
    Low,          // 0
    Medium = 5,   // 5
    High,         // 6
    Critical,     // 7
}
```

### Variant Access

Access variants with dot notation:

```rust
var c: Color = Color.Red;
var s = HttpStatus.NOT_FOUND;
```

Enum values can be compared with `==` and `!=`:

```rust
if c != Color.Red {
    // ...
}
```

Enum variants have their declared enum type in source. They can be widened to
`Integer` or `Number` for runtime interop and bit-level code, but `Integer` values
are not implicitly assignable back to enum variables. Prefer comparing variants
of the same enum, or use `match` for branching:

```rust
if s == HttpStatus.NOT_FOUND {
    // ...
}

var statusCode: Integer = HttpStatus.NOT_FOUND; // accepted
// var bad: HttpStatus = 404;                   // error
```

### Visibility

Use `expose` to make an enum accessible from other modules:

```rust
expose enum Direction {
    North,
    South,
    East,
    West,
}
```

### Match Exhaustiveness

When matching on an enum, the compiler verifies that all variants are covered:

```rust
// Error: Non-exhaustive patterns: missing variants Direction.East, Direction.West
match dir {
    Direction.North => handleNorth();
    Direction.South => handleSouth();
}
```

Use the wildcard `_` to cover remaining variants:

```rust
match dir {
    Direction.North => handleNorth();
    _ => handleOther();
}
```

### As Function Parameters and Return Types

Enums can be used as parameter types, return types, and variable types:

```rust
func describeColor(c: Color) -> String {
    return match c {
        Color.Red => "red",
        Color.Green => "green",
        Color.Blue => "blue",
    };
}
```

### Grammar

```text
enumDecl    ::= "enum" IDENT "{" enumVariant ("," enumVariant)* [","] "}"
enumVariant ::= IDENT ["=" ["-"] INTEGER]
```

---

## Modules and Imports

### Module Declaration

Every source file begins with a module declaration:

```rust
module ModuleName;
```

### Bind Declaration

Bind modules or runtime namespaces to use their types and functions:

```rust
// File binds - import Zia source files
bind "path/to/module";          // Relative or simple path
bind "./sibling";               // Same directory
bind "../parent/module";        // Parent directory
bind "./utils" as U;            // With alias
bind U = "./utils";             // Legacy alias-first form

// Namespace binds - import Viper runtime namespaces
bind Viper.Terminal;            // Import all symbols
bind Viper.Graphics;            // Now Canvas, Sprite, etc. available
bind Viper.Math as M;           // With alias: M.Sqrt(), M.Sin()
bind Viper.Terminal { Say };    // Import specific symbols only
bind Math = Viper.Math;         // Legacy alias-first form
```

**File Path Resolution:**

- `"./foo"` — Resolves to `foo.zia` in the same directory
- `"../bar"` — Resolves to `bar.zia` in parent directory
- `"name"` — Resolves to `name.zia` in the same directory

**File Module Names:**

Top-level declarations are exported by default unless marked `hide`; `expose`
documents that intent explicitly. Unique exported top-level declarations remain
available by their short name for backwards compatibility. If two bound files
declare the same top-level function, global, type alias, class, struct,
interface, or enum name, the compiler scopes each colliding declaration under
its declaring module name. Use the bound module name or bind alias to
disambiguate:

```rust
bind "./alpha"; // module Alpha; expose class WishDup { ... }
bind "./beta";  // module Beta;  expose class WishDup { ... }

var a: Alpha.WishDup = new Alpha.WishDup();
var b: Beta.WishDup = new Beta.WishDup();

Alpha.make();
Beta.make();
Viper.Terminal.SayInt(Alpha.VALUE + Beta.VALUE);
```

**Namespace Imports:**

When you bind a runtime namespace like `Viper.Terminal`, all its functions
become available without qualification:

```rust
bind Viper.Terminal;

Say("Hello!");           // Instead of Viper.Terminal.Say("Hello!")
var name = ReadLine();   // Instead of Viper.Terminal.ReadLine()
```

You can also use an alias to avoid conflicts:

```rust
bind Viper.Terminal as T;

T.Say("Hello!");
```

Or import only specific items:

```rust
bind Viper.Terminal { Say, ReadLine };

Say("Hello!");      // Works
// Print("x");      // Error: Print not imported
```

### Circular Bind Protection

The compiler detects circular binds and reports an error. Maximum bind depth is 50 levels.

---

## Namespaces

Namespaces organize declarations under qualified names to prevent name collisions and provide logical grouping.

### Basic Namespace

```rust
namespace MyLib {
    func helper() -> Integer {
        return 42;
    }

    class Parser {
        Integer value;

        func parse(input: String) -> Boolean {
            return true;
        }
    }
}
```

Access namespaced members using dot notation:

```rust
var result = MyLib.helper();
var p = new MyLib.Parser();
var version = Config.VERSION;
Config.debug = true;
```

### Dotted Namespace Names

Namespaces can use dotted names for nested organization:

```rust
namespace MyLib.Internal {
    func secret() -> String {
        return "hidden";
    }
}

// Access:
var s = MyLib.Internal.secret();
```

### Nested Namespaces

Namespaces can be nested within other namespaces:

```rust
namespace Outer {
    namespace Inner {
        func nested() -> Integer {
            return 100;
        }
    }
}

// Access:
var n = Outer.Inner.nested();
```

### What Can Be in a Namespace

Namespaces can contain:
- Functions
- Class types
- Struct types
- Interfaces
- Global variables (final or var)
- Other namespaces

```rust
namespace Config {
    final VERSION = 42;
    var debug = false;

    struct Point {
        Integer x;
        Integer y;
    }

    interface Configurable {
        func configure();
    }

    class Settings {
        String name;
    }
}
```

### Built-in Namespaces

The `Viper.*` namespaces (Viper.Terminal, Viper.Math, etc.) use the same namespace mechanism as user-defined namespaces.

---

## Runtime Library Access

Zia programs have access to the registered Viper runtime APIs through the `Viper.*` namespace. The exposed surface is generated from `src/il/runtime/runtime.def`; APIs not registered there are not part of Zia's callable runtime surface.

### Common Runtime Classes

#### Terminal I/O

```rust
bind Viper.Terminal;

// Output with newline
Say(str);            // Print string with newline
SayBool(b);          // Print boolean with newline
SayInt(i);           // Print integer with newline
SayNum(f);           // Print float with newline

// Output without newline
Print(str);          // Print string without newline
PrintInt(i);         // Print integer without newline
PrintNum(f);         // Print float without newline
PrintF64(f);         // Print f64 (low-level)
PrintI64(i);         // Print i64 (low-level)
PrintStr(str);       // Print string (low-level)

// Input
TryReadLine();       // Read a line (returns Option<String>, None on EOF)
ReadLineResult();    // Read a line (returns Result<String, String>)
ReadLine();          // Compatibility API; prefer TryReadLine or ReadLineResult for EOF
GetKey();            // Block until key press, return key string
GetKeyTimeout(ms);   // Read key with timeout in ms (returns "" on timeout)
InKey();             // Non-blocking key check (returns "" if no key)

// Terminal control
Bell();              // Emit bell character
Clear();             // Clear screen
SetAltScreen(e);     // Enable/disable alternate screen buffer
SetColor(fg, bg);    // Set foreground/background color (0-15)
SetCursorVisible(v); // Show/hide cursor
SetPosition(row, col);   // Move cursor to row/col (1-based)

// Buffered output
BeginBatch();        // Start batch output
EndBatch();          // End batch output and flush
Flush();             // Flush output buffer
```

#### Time

```rust
Viper.Time.Clock.Sleep(ms);         // Sleep for milliseconds
Viper.Time.Clock.Ticks();      // Monotonic milliseconds (CLOCK_MONOTONIC, not Unix epoch)
```

#### Math

```rust
bind Viper.Math;

Abs(x);                  // Absolute value (f64)
AbsInt(x);               // Absolute value (i64)
Sqrt(x);                 // Square root
Sin(x);                  // Sine
Cos(x);                  // Cosine
Atan2(y, x);             // Two-argument arctangent
Clamp(x, lo, hi);        // Clamp f64 to range
ClampInt(x, lo, hi);     // Clamp i64 to range
Floor(x);                // Floor
Ceil(x);                 // Ceiling
// ... and many more (see Viper.Math.* in runtime.def)
```

#### Random

```rust
// Use the fully qualified name, or bind Viper.Math and use Viper.Math.Random.NextInt
Viper.Math.Random.NextInt(max);   // Random integer [0, max)

// Seeded instances keep independent state.
var rng = new Viper.Math.Random(42);
rng.NextDouble();                 // Random Number [0.0, 1.0)
rng.NextInt(10, 20);              // Random integer [10, 20]
```

#### Generic Collections

```rust
// Language-level generic collections
var list: List[Integer] = [];
list.add(value);                    // Add element
list.get(index);                    // Get element
list.set(index, value);             // Set element
list.count();                       // Get count
list.remove(value);                 // Remove first matching element
list.insert(index, value);          // Insert at position

var map: Map[String, Integer] = new Map[String, Integer]();
map.set("key", value);              // Set or replace value
map.get("key");                     // Get Value? (null if missing)
map.getOr("key", fallback);         // Get Value or default
map.setIfMissing("key", value);     // Set only if missing
map.has("key");                     // Key exists?
map.containsKey("key");             // Alias for has
map.remove("key");                  // Remove entry
map.clear();                        // Remove all entries
map.keys();                         // Sequence of keys
map.values();                       // Sequence of values
map.count();                        // Entry count

var set: Set[String] = new Set[String]();
set.add("item");
set.has("item");
set.count();
```

These language-level collections are distinct from the object-style runtime
classes in `Viper.Collections.*`. For runtime collection classes such as
`Viper.Collections.List`, `Viper.Collections.Map`, `Viper.Collections.Set`,
`Queue`, `Stack`, `Deque`, `Bytes`, and `Seq`, see
**[Runtime Library Reference](viperlib/README.md)**. Zia provides typed aliases
for common object-style collections, so `Queue[String]`, `Stack[Integer]`, and
`Deque[MyType]` preserve element types for `push`, `pop`, `peek`, and iteration
helpers without exposing runtime handles directly.

---

## Operator Precedence

From highest to lowest:

| Precedence | Operators | Associativity |
|------------|-----------|---------------|
| 1 | `()` `[]` `.` `?.` | Left |
| 2 | `-` `!`/`not` `~` `&` (unary) | Right |
| 3 | `*` `/` `%` | Left |
| 4 | `+` `-` | Left |
| 4.5 | `<<` `>>` | Left |
| 5 | `<` `<=` `>` `>=` | Left |
| 6 | `==` `!=` | Left |
| 7 | `&` | Left |
| 8 | `^` | Left |
| 9 | `|` | Left |
| 10 | `&&` / `and` | Left |
| 11 | `||` / `or` | Left |
| 12 | `??` | Left |
| 13 | `..` `..=` | Left |
| 14 | `? :` | Right |
| 15 | `=` `+=` `-=` `*=` `/=` `%=` `<<=` `>>=` `&=` `|=` `^=` | Right |

---

## Reserved Words

The lexer recognizes the following words as keywords. Most are reserved;
`match` and `struct` are contextual and may be used as ordinary identifiers
where the grammar is unambiguous:

### Keywords

```text
and         as          async       await       bind
break       catch       class       continue    deinit
else        enum        expose      extends     false
final       finally     for         func        guard
hide        if          implements  in          interface
is          match       module      namespace   new
not         null        or          override    property
private     public      return      self        static      struct      super
defer       throw       true        try         type        var
while       export      foreign     let         weak
```

`async func` returns `Viper.Threads.Future`. `await` is only valid on `Viper.Threads.Future` values and unwraps the payload produced by an async call.

Compatibility aliases:

- `export` and `public` are accepted aliases for `expose`
- `private` is an accepted alias for `hide`
- `let` is an accepted alias for `final`

### Reserved for Future Use

There are currently no lexer-only reserved keywords documented here. Keywords listed above either have language semantics or are accepted compatibility aliases.

### Type Names

```text
Boolean     Integer     List        Map
Number      Byte        Set         String
Any         Never       Unit        Void
Result      Queue       Stack       Deque
Seq         Bytes
```

`Queue`, `Stack`, `Deque`, `Seq`, and `Bytes` are convenience aliases for typed
runtime collection classes under `Viper.Collections.*`. They are not reserved
words.

---

## Grammar Summary

### Module

```text
module      ::= "module" IDENT ";" bind* decl*
bind        ::= "bind" STRING ["as" IDENT] ";"
              | "bind" IDENT "=" (STRING | qualifiedName) ";"
              | "bind" qualifiedName ["as" IDENT] ["{" identList "}"] ";"
```

### Declarations

```text
decl        ::= [visibility] (classDecl | structDecl | interfaceDecl | enumDecl | funcDecl | asyncFuncDecl | foreignFuncDecl | varDecl | namespaceDecl | typeAlias)
visibility  ::= "expose" | "hide" | "public" | "export" | "private"
typeAlias   ::= "type" IDENT "=" type ";"
classDecl   ::= "class" IDENT ["[" genericParams "]"] ["extends" qualifiedName] ["implements" qualifiedNameList] "{" member* "}"
structDecl  ::= "struct" IDENT ["[" genericParams "]"] ["implements" qualifiedNameList] "{" member* "}"
interfaceDecl ::= "interface" IDENT ["[" genericParams "]"] "{" interfaceMember* "}"
enumDecl    ::= "enum" IDENT "{" enumVariant ("," enumVariant)* [","] "}"
enumVariant ::= IDENT ["=" ["-"] INTEGER]
funcDecl    ::= "func" IDENT ["[" genericParams "]"] "(" params ")" ["->" type] (block | "=" expr ";")
asyncFuncDecl ::= "async" funcDecl
foreignFuncDecl ::= "foreign" "func" IDENT "(" params ")" ["->" type] [";"]
interfaceMember ::= "func" IDENT ["[" genericParams "]"] "(" params ")" ["->" type] (";" | block)
genericParams ::= IDENT [":" qualifiedName] ("," IDENT [":" qualifiedName])*
param       ::= IDENT ":" ["..."] type ["=" expr]
varDecl     ::= ("var" | "final" | "let") (IDENT [":" type] | tupleBinding) ["=" expr] ";"
tupleBinding ::= "(" IDENT [":" type] "," IDENT [":" type] ("," IDENT [":" type])* [","] ")"
namespaceDecl ::= "namespace" qualifiedName "{" decl* "}"
qualifiedName ::= IDENT ("." IDENT)*
member      ::= memberModifier* (fieldDecl | funcDecl | propertyDecl | deinitDecl)
memberModifier ::= "expose" | "hide" | "static" | "override" | "weak"
fieldDecl   ::= ["var"] (IDENT ":" type | type IDENT) ["=" expr] ";"
propertyDecl ::= "property" IDENT ":" type "{" ["get" block] ["set" "(" IDENT ")" block] "}"
deinitDecl  ::= "deinit" block
```

### Statements

```text
stmt        ::= block | varStmt | ifStmt | whileStmt | forStmt | forInStmt
              | returnStmt | breakStmt | continueStmt | deferStmt | guardStmt | matchStmt
              | tryStmt | throwStmt | exprStmt
block       ::= "{" stmt* "}"
body        ::= block | stmt
ifStmt      ::= "if" expr body ["else" (ifStmt | body)]
whileStmt   ::= "while" expr body
forStmt     ::= "for" "(" [varDecl | expr ";"] [expr] ";" [expr] ")" body
forInStmt   ::= "for" forBinding "in" expr body
              | "for" "(" forBinding "in" expr ")" body
forBinding  ::= IDENT [":" type] ["," IDENT [":" type]]
returnStmt  ::= "return" [expr] ";"
breakStmt   ::= "break" ";"
continueStmt ::= "continue" ";"
deferStmt   ::= "defer" (block | expr ";")
guardStmt   ::= "guard" expr "else" block
matchStmt   ::= "match" expr "{" matchArm* "}"
matchArm    ::= pattern ["if" expr] "=>" (block | stmt | expr ";")
tryStmt     ::= "try" block catchClause* ["finally" block]
catchClause ::= "catch" ["(" IDENT [":" type] ")"] block
throwStmt   ::= "throw" [expr] ";"
exprStmt    ::= expr ";"
```

### Expressions

```text
expr        ::= assignment
assignment  ::= ternary [("=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=" | "&=" | "|=" | "^=") assignment]
ternary     ::= range ["?" expr ":" ternary]
range       ::= coalesce ((".." | "..=") coalesce)*
coalesce    ::= logicalOr ("??" logicalOr)*
logicalOr   ::= logicalAnd (("||" | "or") logicalAnd)*
logicalAnd  ::= bitwiseOr (("&&" | "and") bitwiseOr)*
bitwiseOr   ::= bitwiseXor ("|" bitwiseXor)*
bitwiseXor  ::= bitwiseAnd ("^" bitwiseAnd)*
bitwiseAnd  ::= equality ("&" equality)*
equality    ::= comparison (("==" | "!=") comparison)*
comparison  ::= additive (("<" | "<=" | ">" | ">=") additive)*
additive    ::= shift (("+" | "-") shift)*
shift       ::= multiplicative (("<<" | ">>") multiplicative)*
multiplicative ::= unary (("*" | "/" | "%") unary)*
unary       ::= ("-" | "!" | "not" | "~" | "&") unary | postfix
postfix     ::= primary (call | index | field | optionalChain | "!" | "?" | "as" type | "is" type)*
primary     ::= literal | IDENT | "(" expr ")" | "(" expr "," exprList ")"
              | "new" type "(" args ")" | type "{" fieldInits "}"
              | "[" exprList "]" | "{" mapEntries "}" | "{" exprList "}"
              | "set" "{" exprList "}" | "map" "{" mapEntries "}"
              | "if" expr block "else" block | "match" expr "{" matchExprArm* "}"
              | blockExpr
blockExpr   ::= "{" stmt* [expr] "}"
matchExprArm ::= pattern ["if" expr] "=>" expr
fieldInits  ::= IDENT ("=" | ":") expr ("," IDENT ("=" | ":") expr)* [","]
```

### Types

```text
type        ::= "[" type "]" ["?"]
              | IDENT ["[" typeList "]"] ["?"]
              | IDENT "[" INTEGER "]"
              | "(" typeList ")" ["->" type]
```

---

## See Also

- [Zia Getting Started](zia-getting-started.md) — Tutorial introduction
- [Runtime Library Reference](viperlib/README.md) — Complete API documentation
- [IL Guide](il-guide.md) — Understanding compiled output
- [Frontend How-To](frontend-howto.md) — Building language frontends
