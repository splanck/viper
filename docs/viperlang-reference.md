---
status: active
audience: public
last-updated: 2025-12-19
---

# ViperLang — Reference

Complete language reference for ViperLang. This document describes **syntax**, **types**, **statements**, **expressions**, **declarations**, and **runtime integration**. For a tutorial introduction, see **[ViperLang Getting Started](viperlang-getting-started.md)**.

---

## Key Language Features

- **Static typing**: All variables have compile-time types with inference
- **Entity types**: Reference semantics with identity, methods, and inheritance
- **Value types**: Copy semantics with stack allocation
- **Generics**: Parameterized types for collections (`List[T]`, `Map[K, V]`)
- **Modules**: File-based modules with import system
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
- [Runtime Library Access](#runtime-library-access)
- [Operator Precedence](#operator-precedence)
- [Reserved Words](#reserved-words)

---

## Program Structure

A ViperLang source file has the following structure:

```viper
module ModuleName;

// Import declarations
import "path/to/module";

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
| `Integer` | 64-bit signed integer | `0` |
| `Number` | 64-bit floating-point | `0.0` |
| `String` | UTF-8 string | `""` |
| `Boolean` | True or false | `false` |

### Optional Types

Optional types can hold a value or `null`:

```viper
var name: String? = null;   // Optional string
var count: Integer? = 42;   // Optional with value
```

### Generic Types

Parameterized types with type arguments:

```viper
List[Integer]           // List of integers
List[Player]            // List of entity instances
Map[String, Integer]    // Map from strings to integers
```

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

### Field Access

```viper
object.field            // Access field
object.method()         // Call method
```

### Optional Chaining

```viper
object?.field           // Returns null if object is null
value ?? defaultValue   // Returns defaultValue if value is null
```

### Indexing

```viper
list.get(index)         // Access list element
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

### Import Declaration

Import other modules to use their types and functions:

```viper
import "path/to/module";        // Relative or simple path
import "./sibling";              // Same directory
import "../parent/module";       // Parent directory
```

**Path Resolution:**

- `"./foo"` — Resolves to `foo.viper` in the same directory
- `"../bar"` — Resolves to `bar.viper` in parent directory
- `"name"` — Resolves to `name.viper` in the same directory

### Circular Import Protection

The compiler detects circular imports and reports an error. Maximum import depth is 50 levels.

---

## Runtime Library Access

ViperLang programs have access to the full Viper Runtime through the `Viper.*` namespace.

### Common Runtime Classes

#### Terminal I/O

```viper
Viper.Terminal.Say(str);            // Print with newline
Viper.Terminal.Print(str);          // Print without newline
Viper.Terminal.SayInt(i);           // Print integer with newline
Viper.Terminal.PrintInt(i);         // Print integer without newline
Viper.Terminal.GetKey();            // Read single key
Viper.Terminal.GetKeyTimeout(ms);   // Read key with timeout
Viper.Terminal.Clear();             // Clear screen
Viper.Terminal.SetPosition(r, c);   // Move cursor
Viper.Terminal.SetColor(code);      // Set text color
Viper.Terminal.SetCursorVisible(v); // Show/hide cursor
Viper.Terminal.BeginBatch();        // Start batch output
Viper.Terminal.EndBatch();          // End batch output
```

#### Time

```viper
Viper.Time.SleepMs(ms);             // Sleep for milliseconds
```

#### Math

```viper
Viper.Math.Abs(x);                  // Absolute value
Viper.Math.Sqrt(x);                 // Square root
Viper.Math.Sin(x);                  // Sine
Viper.Math.Cos(x);                  // Cosine
// ... and many more
```

#### Random

```viper
Viper.Random.Next(max);             // Random integer [0, max)
```

#### Collections

```viper
// List operations
list.add(value);                    // Add element
list.get(index);                    // Get element
list.set(index, value);             // Set element
list.size();                        // Get count
list.remove(index);                 // Remove element
```

For complete runtime documentation, see **[Runtime Library Reference](viperlib/README.md)**.

---

## Operator Precedence

From highest to lowest:

| Precedence | Operators | Associativity |
|------------|-----------|---------------|
| 1 | `()` `[]` `.` `?.` | Left |
| 2 | `-` `!` `~` (unary) | Right |
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
| 13 | `? :` | Right |
| 14 | `=` | Right |

---

## Reserved Words

The following words are reserved and cannot be used as identifiers:

### Keywords

```
and         as          break       continue    else
entity      expose      extends     false       final
for         func        guard       hide        if
implements  import      in          interface   is
match       module      new         null        or
override    return      self        super       true
value       var         while
```

### Type Names

```
Boolean     Integer     List        Map         Number
String
```

---

## Grammar Summary

### Module

```
module      ::= "module" IDENT ";" import* decl*
import      ::= "import" STRING ";"
```

### Declarations

```
decl        ::= entityDecl | valueDecl | interfaceDecl | funcDecl | varDecl
entityDecl  ::= "entity" IDENT ["extends" IDENT] ["implements" identList] "{" member* "}"
valueDecl   ::= "value" IDENT ["implements" identList] "{" member* "}"
interfaceDecl ::= "interface" IDENT "{" methodSig* "}"
funcDecl    ::= "func" IDENT "(" params ")" ["->" type] block
varDecl     ::= ("var" | "final") IDENT [":" type] ["=" expr] ";"
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
guardStmt   ::= "guard" expr "else" block
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
unary       ::= ("-" | "!" | "~") unary | postfix
postfix     ::= primary (call | index | field | optionalChain)*
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

- [ViperLang Getting Started](viperlang-getting-started.md) — Tutorial introduction
- [Runtime Library Reference](viperlib/README.md) — Complete API documentation
- [IL Guide](il-guide.md) — Understanding compiled output
- [Frontend How-To](frontend-howto.md) — Building language frontends
