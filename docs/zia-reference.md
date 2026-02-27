---
status: active
audience: public
last-updated: 2026-02-17
---

# Zia — Reference

Complete language reference for Zia. This document describes **syntax**, **types**, **statements**, **expressions**, **declarations**, and **runtime integration**. For a tutorial introduction, see **[Zia Getting Started](zia-getting-started.md)**.

---

## Key Language Features

- **Static typing**: All variables have compile-time types with inference
- **Entity types**: Reference semantics with identity, methods, and inheritance
- **Value types**: Copy semantics with stack allocation
- **Generics**: Parameterized types for collections (`List[T]`, `Map[K, V]`)
- **Modules**: File-based modules with bind system
- **C-like syntax**: Familiar braces, semicolons, and operators
- **Runtime library**: Full access to Viper.* classes

---

## Table of Contents

- [Program Structure](#program-structure)
- [Lexical Elements](#lexical-elements)
- [Types](#types)
- [Expressions](#expressions)
- [Statements](#statements)
- [Declarations](#declarations)
- [Entity Types](#entity-types)
- [Value Types](#value-types)
- [Interfaces](#interfaces)
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

// Type declarations (entity, value, interface)
entity MyEntity { ... }
value MyValue { ... }
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

```
identifier  ::= [a-zA-Z_][a-zA-Z0-9_]*
```

### Literals

#### Integer Literals

```viper
42          // Decimal
0xFF        // Hexadecimal
0b1010      // Binary
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
```

**Escape sequences:**

| Sequence | Meaning |
|----------|---------|
| `\n` | Newline |
| `\t` | Tab |
| `\r` | Carriage return |
| `\\` | Backslash |
| `\"` | Double quote |
| `\$` | Dollar sign (in interpolated strings) |

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
List[Player]            // List of entity instances
Map[String, Integer]    // Map from strings to integers
```

Map keys are restricted to `String`.

### Entity Types

Reference types defined with the `entity` keyword:

```viper
var player: Player = new Player();
```

### Value Types

Copy-semantics types defined with the `value` keyword:

```viper
var point: Point;
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
| `!` | Logical NOT | `!flag` |
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
| `&&` | Logical AND |
| `||` | Logical OR |

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

### Function/Method Calls

```viper
functionName(arg1, arg2)
object.methodName(arg1)
```

### Object Creation

```viper
new EntityType()        // Create entity instance
```

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
(x) => x + 1                    // Single parameter
(a, b) => a + b                 // Multiple parameters
(x: Integer) => x * 2           // Typed parameter
```

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

// Map iteration with tuple destructuring
for (key, value) in map {
    // key is String, value is map value type
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
- Guards (`pattern if condition => ...`)

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

### Global Variable Declaration

```viper
var globalName: Type = initialValue;
final CONSTANT = value;
```

---

## Entity Types

Entities are reference types with identity, stored on the heap.

### Entity Declaration

```viper
entity EntityName {
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
entity Player {
    Integer health;         // Default visibility
    hide Integer secret;    // Private field
    expose String name;     // Public field
}
```

### Entity Inheritance

```viper
entity ChildEntity extends ParentEntity {
    // Additional fields and methods
}
```

### Creating Entities

```viper
var entity = new EntityType();
entity.init(args);
```

### Self Reference

Within methods, fields can be accessed directly or with `self`:

```viper
entity Counter {
    Integer count;

    func increment() {
        count = count + 1;      // Direct access
        self.count = self.count + 1;  // Explicit self
    }
}
```

---

## Value Types

Value types have copy semantics — assignments copy the entire value.

### Value Declaration

```viper
value ValueName {
    // Fields
    Type fieldName;

    // Methods
    func methodName(params) -> ReturnType {
        return value;
    }
}
```

### Using Value Types

```viper
value Point {
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

Interfaces define contracts that entities and values can implement.

### Interface Declaration

```viper
interface InterfaceName {
    func methodSignature(params) -> ReturnType;
}
```

### Implementing Interfaces

```viper
entity MyEntity implements InterfaceName {
    func methodSignature(params) -> ReturnType {
        // Implementation
    }
}
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

// Namespace binds - import Viper runtime namespaces
bind Viper.Terminal;            // Import all symbols
bind Viper.Graphics;            // Now Canvas, Sprite, etc. available
bind Viper.Math as M;           // With alias: M.Sqrt(), M.Sin()
bind Viper.Terminal { Say };    // Import specific symbols only
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

    entity Parser {
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
- Entity types
- Value types
- Interfaces
- Global variables (final or var)
- Other namespaces

```viper
namespace Config {
    final VERSION = 42;
    var debug = false;

    value Point {
        Integer x;
        Integer y;
    }

    interface Configurable {
        func configure();
    }

    entity Settings {
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
SetPosition(r, c);   // Move cursor to row/col (1-based)

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
list.Len;                        // Get count
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
map.Len;                         // Entry count
```

For complete runtime documentation, see **[Runtime Library Reference](viperlib/README.md)**.

---

## Operator Precedence

From highest to lowest:

| Precedence | Operators | Associativity |
|------------|-----------|---------------|
| 1 | `()` `[]` `.` `?.` | Left |
| 2 | `-` `!` `~` `&` (unary) | Right |
| 3 | `*` `/` `%` | Left |
| 4 | `+` `-` | Left |
| 5 | `<` `<=` `>` `>=` | Left |
| 6 | `==` `!=` | Left |
| 7 | `&` | Left |
| 8 | `^` | Left |
| 9 | `|` | Left |
| 10 | `&&` | Left |
| 11 | `||` | Left |
| 12 | `??` | Left |
| 13 | `..` `..=` | Left |
| 14 | `? :` | Right |
| 15 | `=` | Right |

---

## Reserved Words

The following words are reserved and cannot be used as identifiers:

### Keywords

```
and         as          bind        break       continue
else        entity      expose      extends     false
final       for         func        guard       hide
if          implements  in          interface   is
let         match       module      namespace   new
not         null        or          override    return
self        super       true        value       var
weak        while
```

### Type Names

```
Boolean     Integer     List        Map         Number
Ptr         String
```

---

## Grammar Summary

### Module

```
module      ::= "module" IDENT ";" bind* decl*
bind        ::= "bind" STRING ["as" IDENT] ";"
              | "bind" qualifiedName ["as" IDENT] ["{" identList "}"] ";"
```

### Declarations

```
decl        ::= entityDecl | valueDecl | interfaceDecl | funcDecl | varDecl | namespaceDecl
entityDecl  ::= "entity" IDENT ["extends" IDENT] ["implements" identList] "{" member* "}"
valueDecl   ::= "value" IDENT ["implements" identList] "{" member* "}"
interfaceDecl ::= "interface" IDENT "{" methodSig* "}"
funcDecl    ::= "func" IDENT "(" params ")" ["->" type] block
varDecl     ::= ("var" | "final") IDENT [":" type] ["=" expr] ";"
namespaceDecl ::= "namespace" qualifiedName "{" decl* "}"
qualifiedName ::= IDENT ("." IDENT)*
```

### Statements

```
stmt        ::= block | varStmt | ifStmt | whileStmt | forStmt | forInStmt
              | returnStmt | breakStmt | continueStmt | guardStmt | matchStmt
              | exprStmt
block       ::= "{" stmt* "}"
ifStmt      ::= "if" expr block ["else" (ifStmt | block)]
whileStmt   ::= "while" expr block
forStmt     ::= "for" "(" [varStmt | exprStmt] expr ";" expr ")" block
forInStmt   ::= "for" IDENT "in" expr block
returnStmt  ::= "return" [expr] ";"
breakStmt   ::= "break" ";"
continueStmt ::= "continue" ";"
guardStmt   ::= "guard" expr "else" block
matchStmt   ::= "match" expr "{" matchArm* "}"
matchArm    ::= pattern ["if" expr] "=>" (block | expr ";")
exprStmt    ::= expr ";"
```

### Expressions

```
expr        ::= assignment
assignment  ::= ternary ["=" assignment]
ternary     ::= logicalOr ["?" expr ":" ternary]
logicalOr   ::= logicalAnd ("||" logicalAnd)*
logicalAnd  ::= equality ("&&" equality)*
equality    ::= comparison (("==" | "!=") comparison)*
comparison  ::= additive (("<" | "<=" | ">" | ">=") additive)*
additive    ::= multiplicative (("+" | "-") multiplicative)*
multiplicative ::= unary (("*" | "/" | "%") unary)*
unary       ::= ("-" | "!" | "~" | "&") unary | postfix
postfix     ::= primary (call | index | field | optionalChain | "!" | "?")*
primary     ::= literal | IDENT | "(" expr ")" | "new" type "(" args ")"
              | "[" exprList "]" | "{" mapEntries "}"
```

### Types

```
type        ::= IDENT ["[" typeList "]"] ["?"]
              | "(" typeList ")" "->" type
```

---

## See Also

- [Zia Getting Started](zia-getting-started.md) — Tutorial introduction
- [Runtime Library Reference](viperlib/README.md) — Complete API documentation
- [IL Guide](il-guide.md) — Understanding compiled output
- [Frontend How-To](frontend-howto.md) — Building language frontends
