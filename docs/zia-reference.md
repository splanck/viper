---
status: active
audience: public
last-verified: 2026-04-05
---

# Zia — Reference

Complete language reference for Zia. This document describes **syntax**, **types**, **statements**, **expressions**, **declarations**, and **runtime integration**. For a tutorial introduction, see **[Zia Getting Started](zia-getting-started.md)**.

---

## Key Language Features

- **Static typing**: All variables have compile-time types with inference
- **Class types**: Reference semantics with identity, methods, inheritance, properties, static members, and destructors
- **Struct types**: Copy semantics with stack allocation
- **Interfaces**: Contracts with full runtime itable dispatch
- **Enums**: Named sets of integer constants with exhaustiveness checking in match
- **Generics**: Parameterized types and functions with optional constraints (`List[T]`, `func max[T: Comparable]`)
- **Exception handling**: `try`/`catch`/`finally` with structured error propagation
- **Modules**: File-based modules with bind system
- **C-like syntax**: Familiar braces, semicolons, and operators
- **Runtime library**: Full access to Viper.* classes

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

```viper
module ModuleName;

// Bind declarations (bring other modules into scope)
bind "path/to/module";

// Global variable declarations
var globalVar: Type = value;
final CONSTANT = value;

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

```viper
func start() {
    // Program execution begins here
}
```

---

## Lexical Elements

### Comments

```viper
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

```viper
42          // Decimal
0xFF        // Hexadecimal
0b1010      // Binary
()          // Unit literal (void value)
```

#### Floating-Point Literals

```viper
3.14159     // Decimal
1e10        // Scientific notation
2.5e-3      // Scientific with negative exponent
```

#### String Literals

```viper
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

```viper
true
false
```

#### Null Literal

```viper
null    // Used with optional types
```

---

## Types

### Primitive Types

| Type | Description | Default Value |
|------|-------------|---------------|
| `Boolean` | True or false | `false` |
| `Integer` | 64-bit signed integer | `0` |
| `Number` | 64-bit floating-point | `0.0` |
| `Ptr` | Raw pointer / opaque handle (for interop) | `null` |
| `String` | UTF-8 string | `""` |

### Optional Types

Optional types can hold a value or `null`:

```viper
var name: String? = null;   // Optional string
var count: Integer? = 42;   // Optional with value
```

Optional values support safe access with `?.`, defaults with `??`, and force-unwrap with `!`.

### Generic Types

Parameterized types with type arguments:

```viper
List[Integer]           // List of integers
List[Player]            // List of class instances
Map[String, Integer]    // Map from strings to integers
[Integer]               // Shorthand for List[Integer]
```

Map keys are restricted to `String`.

### Class Types

Reference types defined with the `class` keyword:

```viper
var player: Player = new Player();
```

### Struct Types

Copy-semantics types defined with the `struct` keyword:

```viper
var point: Point;
```

### Tuple Types

Tuple types group multiple values into a single type:

```viper
var pair: (Integer, String) = (42, "hello");
var x = pair.0;     // Access by index
```

### Fixed-Size Array Types

Arrays with a compile-time size:

```viper
var grid: Integer[100];     // Fixed array of 100 integers
```

### Set Types

Sets hold unique values. Created with set literals or the `Set` constructor:

```viper
var s: Set[Integer] = {1, 2, 3};
```

### Function Types

Function signatures as types:

```viper
(Integer, Integer) -> Integer   // Function taking two ints, returning int
() -> void                      // Function taking nothing, returning void
```

---

## Expressions

### Literals

```viper
42                  // Integer literal
3.14                // Number literal
"hello"             // String literal
true                // Boolean literal
null                // Null literal
```

### Identifiers

```viper
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

The `&` operator obtains a reference (pointer) to a function, which can be stored in a variable or passed as an argument:

```viper
bind Viper.Threads;

func handler(arg: Ptr) {
    // Handle something
}

func takeCallback(callback: Ptr) {
    // Use the callback
}

func start() {
    var h = &handler;           // Get function reference
    takeCallback(&handler);     // Pass function reference directly

    // With Thread.Start
    var thread = Thread.Start(&handler, 0);
}
```

**Notes:**
- The `&` operator can only be applied to function names, not variables or expressions
- Function references are stored as `Ptr` type
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

Compound assignment operators desugar to `a = a op b` at parse time. The left-hand side must be a mutable variable, field, or indexed expression.

### Ternary Operator

```viper
condition ? thenValue : elseValue
```

### Match Expression

```viper
var result = match value {
    0 => "zero";
    n if n > 0 => "positive";
    _ => "negative";
};
```

### Field Access

```viper
object.field            // Access field
object.method()         // Call method
```

### Optional Chaining and Unwrapping

```viper
object?.field           // Null if object is null, otherwise optional field value
object?.field?.subfield // Chained optional access
value ?? defaultValue   // Returns defaultValue if value is null
value!                  // Force-unwrap: converts T? to T, traps if null
```

The force-unwrap operator `!` asserts that an optional value is non-null and extracts
the inner value. If the value is null at runtime, the program terminates. Use after
a null guard or when you are certain the value is non-null:

```viper
if maybePage == null { return null; }
var page = maybePage!;              // Safe: null was handled above
```

### Indexing

```viper
list[index]             // Access list element
map["key"]              // Access map value (keys are String)
```

### Block Expressions

A block can produce a value when the last statement is an expression without a trailing semicolon:

```viper
var x = {
    var a = 10;
    var b = 20;
    a + b           // Block evaluates to 30
};
```

### If Expressions

`if`/`else` can be used as value-producing expressions:

```viper
var sign = if x > 0 { "positive" } else { "non-positive" };
```

Both branches must be present and produce the same type.

### Try Expression

The postfix `?` operator propagates null or error values from an expression. If the operand is null (or an error), the enclosing function returns immediately with that null/error. Otherwise, the inner value is unwrapped:

```viper
func findUser(id: Integer) -> User? {
    var record = database.lookup(id)?;  // Returns null if lookup returns null
    return record.toUser();
}
```

### Function/Method Calls

```viper
functionName(arg1, arg2)
object.methodName(arg1)
```

Strings also support instance-style calls for common `Viper.String` operations:

```viper
var name = "  viper  ".Trim().ToUpper();
var part = "abcdef".Substring(1, 3);   // "bcd"
```

#### Named Arguments

Arguments can be passed by name using `name: value` syntax. This works for
user-defined functions, methods, constructors, and runtime APIs that expose
surface parameter names:

```viper
func createRect(x: Integer, y: Integer, w: Integer, h: Integer) -> Rect { ... }

var r = createRect(x: 10, y: 20, w: 100, h: 50);
var part = "abcdef".Substring(start: 1, len: 3);
SetPosition(row: 10, col: 20);
```

### Object Creation

```viper
new ClassName(args)     // Create a class or struct value
```

### Struct Type Initialization (Struct Literals)

Struct types can be initialized with field assignments:

```viper
struct Point {
    Integer x;
    Integer y;
}

var p = Point { x = 10, y = 20 };
```

### Tuple Expressions

Tuples group multiple values. Access elements with `.0`, `.1`, etc.:

```viper
var pair = (42, "hello");
var num = pair.0;           // 42
var str = pair.1;           // "hello"
```

> **Known limitation:** Tuple destructuring in `var` declarations (`var (x, y) = expr;`)
> is not yet supported. Use `.0` / `.1` element access instead.

### Collection Literals

```viper
var list = [1, 2, 3];              // List[Integer]
var map = {"key": 42, "other": 7}; // Map[String, Integer]
var set = {1, 2, 3};               // Set[Integer]
```

`{}` is the empty map literal. To create an empty set, use an explicit type or
constructor such as `new Set[Integer]()`.

### Range Expressions

```viper
start..end              // Exclusive range [start, end)
start..=end             // Inclusive range [start, end]
```

### Type Operations

```viper
value is Type           // Type check (returns Boolean)
value as Type           // Type cast
```

### Lambda Expressions

```viper
(x: Integer) => x + 1             // Single parameter
(a: Integer, b: Integer) => a + b // Multiple parameters
() => 42                          // No parameters
```

Lambda parameters must include explicit type annotations.

### `is` Type Check

The `is` operator checks whether a value's runtime type matches a target type, including subclass relationships:

```viper
class Animal { }
class Dog : Animal { }
class Cat : Animal { }

func check(a: Animal) {
    if a is Dog {
        // true if a is Dog or any subclass of Dog
    }
    if a is Animal {
        // always true for any Animal subclass
    }
}
```

The `is` expression returns a `Boolean`. It is polymorphic: `obj is Base` returns `true` when `obj`'s runtime type is `Base` or any subclass of `Base`.

---

## Statements

### Variable Declaration

```viper
var name = value;                   // Type inferred
var name: Type = value;             // Explicit type
var name: Type;                     // Default initialized
final name = value;                 // Immutable
```

### Expression Statement

Any expression can be used as a statement:

```viper
functionCall();
object.method();
x = x + 1;
```

### Block Statement

```viper
{
    statement1;
    statement2;
}
```

### If Statement

```viper
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

Note: Parentheses around conditions are optional.

### While Statement

```viper
while condition {
    // body
}
```

### For Statement (C-style)

```viper
for (init; condition; update) {
    // body
}

// Example
for (var i = 0; i < 10; i = i + 1) {
    // body
}
```

### For-In Statement

```viper
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

```viper
return;                 // Return void
return expression;      // Return value
```

### Break Statement

```viper
break;                  // Exit innermost loop
```

### Continue Statement

```viper
continue;               // Skip to next iteration
```

### Guard Statement

```viper
guard condition else {
    return;             // Must exit scope
}
// condition is true here
```

### Match Statement

```viper
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
- Tuple patterns with two elements (`(x, y)`)
- Constructor patterns (`Point(x, y)`, `Some(value)`, `None`)
- OR patterns (`pattern1 | pattern2 | pattern3 => ...`) — multiple alternatives for one arm
- Enum variant patterns (`Color.Red`, `Direction.Left`)
- Guards (`pattern if condition => ...`)

OR pattern example:

```viper
match x {
    1 | 2 | 3 => Say("small");
    10 | 20 => Say("round");
    _ => Say("other");
}
```

### Try/Catch/Finally Statement

Structured exception handling for runtime errors:

```viper
try {
    riskyOperation();
} catch(e) {
    Say("caught: " + e);
}
```

- The `try` block is always required.
- The `catch` block handles exceptions. Named error binding uses parentheses: `catch(e) { ... }`.
  Anonymous catch `catch { ... }` is also supported.
- Typed catch is supported for runtime error kinds such as `DivideByZero`,
  `Bounds`, `RuntimeError`, and the catch-all alias `Error`.
- Both `catch` and `finally` are optional, but at least one must be present.
- `finally` runs regardless of whether an exception was thrown.

### Throw Statement

Raises a runtime error:

```viper
throw someErrorValue;
```

---

## Declarations

### Function Declaration

```viper
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

```viper
func greet(name: String, greeting: String = "Hello") -> String {
    return greeting + ", " + name;
}

func start() {
    var msg1 = greet("Alice", "Hi");    // "Hi, Alice"
    var msg2 = greet("Bob");            // "Hello, Bob"
}
```

Default values must be trailing — a parameter with a default cannot be followed by a parameter without one.

### Generic Function Declaration

Functions can be parameterized over types with optional constraints:

```viper
func identity[T](x: T) -> T {
    return x;
}

func findMax[T: Comparable](a: T, b: T) -> T {
    if a > b { return a; }
    return b;
}
```

Type parameters are declared in `[...]` after the function name. Constraints (like `T: Comparable`) restrict the type parameter to types implementing the named interface.

### Async Functions

Use `async func` to return a `Viper.Threads.Future`, and `await` to unwrap the completed payload:

```viper
async func fetchName() -> String {
    return "viper";
}

func start() {
    var name: String = await fetchName();
}
```

`await` is valid on values of type `Viper.Threads.Future`. The awaited value is unboxed back to the async function's declared return type.

### Variadic Parameters

The last parameter of a function may be variadic, accepting zero or more arguments that are collected into a `List`:

```viper
func sum(nums: ...Integer) -> Integer {
    var total = 0;
    for i in 0..nums.Length {
        total = total + nums.Get(i);
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

```viper
func double(x: Integer) -> Integer = x * 2;
func greet(name: String) -> String = "Hello, " + name;
```

This desugars to a `return` statement wrapping the expression. Works for both top-level functions and class methods.

### Type Alias Declarations

Create compile-time type aliases with `type`:

```viper
type Name = String;
type Score = Integer;

func display(name: Name, score: Score) {
    Terminal.PrintLine(name + ": " + score.ToString());
}
```

Aliases are resolved during semantic analysis and have no runtime representation.

### Global Variable Declaration

```viper
var globalName: Type = initialValue;
final CONSTANT = value;
```

---

## Class Types

Classes are reference types with identity, stored on the heap.

### Class Declaration

```viper
class ClassName {
    // Fields
    Type fieldName;

    // Initializer method
    func init(params) {
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

```viper
class Player {
    Integer health;         // Default visibility
    hide Integer secret;    // Private field
    expose String name;     // Public field
}
```

### Class Inheritance

```viper
class ChildClass extends ParentClass {
    // Additional fields and methods
}
```

#### The `super` Keyword

Within a child class, `super` refers to the parent class's implementation. Use it to call the parent's methods:

```viper
class Child extends Parent {
    override func greet() -> String {
        return super.greet() + " (child)";
    }
}
```

#### The `override` Keyword

Methods that override a parent's method must be marked with `override`:

```viper
class Base {
    func describe() -> String { return "Base"; }
}

class Derived extends Base {
    override func describe() -> String { return "Derived"; }
}
```

### Creating Instances

```viper
var obj = new ClassName(args);
```

`new Type(args)` calls `init(...)` when the type defines one. For structs and
classes without `init`, arguments are matched in field declaration order.

### Self Reference

Within methods, fields can be accessed directly or with `self`:

```viper
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

```viper
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
- Either `get` or `set` may be omitted for read-only or write-only properties.
- Reading a write-only property is an error.
- Writing a read-only property is an error.
- Use `expose property` when the property should be accessible outside the declaring type.
- Properties are accessed like fields: `temp.fahrenheit` calls the getter, `temp.fahrenheit = 212.0` calls the setter.

### Static Members

Fields and methods marked `static` belong to the class type, not to instances:

```viper
class Counter {
    static Integer instanceCount;

    static func getCount() -> Integer {
        return instanceCount;
    }

    func init() {
        instanceCount = instanceCount + 1;
    }
}
```

- Static fields are stored as module-level globals (not per-instance).
- Static methods have no `self` parameter.
- Access via the class name: `Counter.getCount()`, `Counter.instanceCount`.

### Destructors

The `deinit` block defines cleanup logic that runs when an object is destroyed:

```viper
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
- If you need deterministic cleanup, releasing the last reference explicitly with
  `Viper.Memory.Release(handle)` will run `deinit` before the object storage is freed.

---

## Struct Types

Struct types have copy semantics — assignments copy the entire struct.

### Struct Declaration

```viper
struct StructName {
    // Fields
    Type fieldName;

    // Methods
    func methodName(params) -> ReturnType {
        return value;
    }
}
```

### Using Struct Types

```viper
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

```viper
interface InterfaceName {
    func methodSignature(params) -> ReturnType;
}
```

### Implementing Interfaces

```viper
class MyClass implements InterfaceName {
    expose func methodSignature(params) -> ReturnType {
        // Implementation
    }
}
```

Implementing methods must be marked `expose` (public visibility).

### Interface Dispatch

Interface-typed variables use runtime itable dispatch. Calling a method on an interface variable performs a lookup through the interface table (itable) to find the correct implementation:

```viper
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

At module initialization, a `__zia_iface_init` function registers each interface and binds implementation itables. Method calls on interface-typed parameters use `rt_get_interface_impl` to find the function pointer at the correct slot, then invoke it via indirect call.

---

## Enums

Enums define a type with a fixed set of named integer constants. Each variant is lowered to an `I64` constant at the IL level, while source programs still treat the value as its enum type.

### Declaration

```viper
enum Color {
    Red,
    Green,
    Blue,
}
```

Variants are automatically numbered starting from 0. A trailing comma after the last variant is permitted.

### Explicit Values

Variants may specify explicit integer values. Unspecified variants auto-increment from the previous value.

```viper
enum HttpStatus {
    OK = 200,
    NOT_FOUND = 404,
    INTERNAL_ERROR = 500,
}
```

Mixed auto-increment and explicit values:

```viper
enum Priority {
    Low,          // 0
    Medium = 5,   // 5
    High,         // 6
    Critical,     // 7
}
```

### Variant Access

Access variants with dot notation:

```viper
var c: Color = Color.Red;
var s = HttpStatus.NOT_FOUND;
```

Enum values can be compared with `==` and `!=`:

```viper
if c != Color.Red {
    // ...
}
```

Enum variants are not implicitly typed as `Integer` in source code. Compare them to variants of the same enum, or use `match` for branching:

```viper
if s == HttpStatus.NOT_FOUND {
    // ...
}
```

### Visibility

Use `expose` to make an enum accessible from other modules:

```viper
expose enum Direction {
    North,
    South,
    East,
    West,
}
```

### Match Exhaustiveness

When matching on an enum, the compiler verifies that all variants are covered:

```viper
// Error: Non-exhaustive patterns: missing variants Direction.East, Direction.West
match dir {
    Direction.North => handleNorth();
    Direction.South => handleSouth();
}
```

Use the wildcard `_` to cover remaining variants:

```viper
match dir {
    Direction.North => handleNorth();
    _ => handleOther();
}
```

### As Function Parameters and Return Types

Enums can be used as parameter types, return types, and variable types:

```viper
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
enumDecl    ::= ["expose"] "enum" IDENT "{" enumVariant ("," enumVariant)* [","] "}"
enumVariant ::= IDENT ["=" ["-"] INTEGER]
```

---

## Modules and Imports

### Module Declaration

Every source file begins with a module declaration:

```viper
module ModuleName;
```

### Bind Declaration

Bind modules or runtime namespaces to use their types and functions:

```viper
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

**Namespace Imports:**

When you bind a runtime namespace like `Viper.Terminal`, all its functions
become available without qualification:

```viper
bind Viper.Terminal;

Say("Hello!");           // Instead of Viper.Terminal.Say("Hello!")
var name = ReadLine();   // Instead of Viper.Terminal.ReadLine()
```

You can also use an alias to avoid conflicts:

```viper
bind Viper.Terminal as T;

T.Say("Hello!");
```

Or import only specific items:

```viper
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

```viper
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

```viper
var result = MyLib.helper();
var p = new MyLib.Parser();
```

### Dotted Namespace Names

Namespaces can use dotted names for nested organization:

```viper
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

```viper
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

```viper
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

Zia programs have access to the full Viper Runtime through the `Viper.*` namespace.

### Common Runtime Classes

#### Terminal I/O

```viper
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
ReadLine();          // Read a line (returns String?)
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

```viper
// SleepMs is available under Viper.Time (RT_ALIAS from ClockSleep)
Viper.Time.SleepMs(ms);         // Sleep for milliseconds
Viper.Time.GetTickCount();       // Milliseconds since epoch
```

#### Math

```viper
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

```viper
// Use the fully qualified name, or bind Viper.Math and use Viper.Math.Random.NextInt
Viper.Math.Random.NextInt(max);   // Random integer [0, max)
```

#### Collections

```viper
// List operations
list.add(value);                    // Add element
list.get(index);                    // Get element
list.set(index, value);             // Set element
list.Length;                        // Get count
list.remove(index);                 // Remove element

// Map operations (keys are String)
map.set("key", value);              // Set or replace value
map.put("key", value);              // Alias for set
map.get("key");                     // Get value (null if missing)
map.getOr("key", fallback);         // Get or default
map.setIfMissing("key", value);     // Set only if missing
map.has("key");                     // Key exists?
map.containsKey("key");             // Alias for has
map.remove("key");                  // Remove entry
map.clear();                        // Remove all entries
map.keys();                         // Sequence of keys
map.values();                       // Sequence of values
map.Length;                         // Entry count
```

For complete runtime documentation, see **[Runtime Library Reference](viperlib/README.md)**.

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

The following words are reserved and cannot be used as identifiers:

### Keywords

```text
and         as          async       await       bind
break       catch       class       continue    deinit
else        enum        expose      extends     false
final       finally     for         func        guard
hide        if          implements  in          interface
is          match       module      namespace   new
not         null        or          override    property
public      return      self        static      struct      super
throw       true        try         type        var
while       export      let
```

`async func` returns `Viper.Threads.Future`. `await` is only valid on `Viper.Threads.Future` values and unwraps the payload produced by an async call.

Compatibility aliases:

- `export` and `public` are accepted aliases for `expose`
- `let` is an accepted alias for `final`

### Reserved for Future Use

The following keyword is recognized by the lexer but has no current semantics:

```text
weak
```

### Type Names

```text
Boolean     Bytes       Integer     List        Map
Number      Ptr         Set         String
```

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
decl        ::= classDecl | structDecl | interfaceDecl | enumDecl | funcDecl | varDecl | namespaceDecl | typeAlias
typeAlias   ::= "type" IDENT "=" type ";"
classDecl   ::= "class" IDENT ["extends" IDENT] ["implements" identList] "{" member* "}"
structDecl  ::= "struct" IDENT ["implements" identList] "{" member* "}"
interfaceDecl ::= "interface" IDENT "{" methodSig* "}"
enumDecl    ::= ["expose"] "enum" IDENT "{" enumVariant ("," enumVariant)* [","] "}"
enumVariant ::= IDENT ["=" ["-"] INTEGER]
funcDecl    ::= "func" IDENT ["[" genericParams "]"] "(" params ")" ["->" type] (block | "=" expr ";")
genericParams ::= IDENT [":" IDENT] ("," IDENT [":" IDENT])*
param       ::= IDENT ":" ["..."] type ["=" expr]
varDecl     ::= ("var" | "final" | "let") IDENT [":" type] ["=" expr] ";"
namespaceDecl ::= "namespace" qualifiedName "{" decl* "}"
qualifiedName ::= IDENT ("." IDENT)*
member      ::= ["expose" | "hide"] ["static" | "override"] (fieldDecl | funcDecl | propertyDecl | deinitDecl)
propertyDecl ::= "property" IDENT ":" type "{" ["get" block] ["set" "(" IDENT ")" block] "}"
deinitDecl  ::= "deinit" block
```

### Statements

```text
stmt        ::= block | varStmt | ifStmt | whileStmt | forStmt | forInStmt
              | returnStmt | breakStmt | continueStmt | guardStmt | matchStmt
              | tryStmt | throwStmt | exprStmt
block       ::= "{" stmt* "}"
ifStmt      ::= "if" expr block ["else" (ifStmt | block)]
whileStmt   ::= "while" expr block
forStmt     ::= "for" "(" [varStmt | exprStmt] expr ";" expr ")" block
forInStmt   ::= "for" forBinding "in" expr block
              | "for" "(" forBinding "in" expr ")" block
forBinding  ::= IDENT [":" type] ["," IDENT [":" type]]
returnStmt  ::= "return" [expr] ";"
breakStmt   ::= "break" ";"
continueStmt ::= "continue" ";"
guardStmt   ::= "guard" expr "else" block
matchStmt   ::= "match" expr "{" matchArm* "}"
matchArm    ::= pattern ["if" expr] "=>" (block | expr ";")
tryStmt     ::= "try" block ["catch" ["(" IDENT [":" type] ")"] block] ["finally" block]
throwStmt   ::= "throw" expr ";"
exprStmt    ::= expr ";"
```

### Expressions

```text
expr        ::= assignment
assignment  ::= ternary [("=" | "+=" | "-=" | "*=" | "/=" | "%=" | "<<=" | ">>=" | "&=" | "|=" | "^=") assignment]
ternary     ::= logicalOr ["?" expr ":" ternary]
logicalOr   ::= logicalAnd ("||" logicalAnd)*
logicalAnd  ::= equality ("&&" equality)*
equality    ::= comparison (("==" | "!=") comparison)*
comparison  ::= additive (("<" | "<=" | ">" | ">=") additive)*
additive    ::= shift (("+" | "-") shift)*
shift       ::= multiplicative (("<<" | ">>") multiplicative)*
multiplicative ::= unary (("*" | "/" | "%") unary)*
unary       ::= ("-" | "!" | "~" | "&") unary | postfix
postfix     ::= primary (call | index | field | optionalChain | "!" | "?" | "as" type | "is" type)*
primary     ::= literal | IDENT | "(" expr ")" | "(" expr "," exprList ")"
              | "new" type "(" args ")" | type "{" fieldInits "}"
              | "[" exprList "]" | "{" mapEntries "}" | "{" exprList "}"
              | "if" expr block "else" block | block
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
