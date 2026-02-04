# Zia — Language Specification

## Version 0.1 Final

<div align="center">

**A modern language with familiar syntax**

*Java-style declarations. Pattern matching. No exceptions. Memory safe.*

</div>

---

## Philosophy

Zia adheres to five core principles:

1. **Familiar Syntax** — Java-style declarations and semicolons
2. **Two Type Kinds** — Values (copied) and entities (referenced)
3. **Null Safety** — Optional types prevent null pointer exceptions
4. **No Exceptions** — Errors as values with Result[T]
5. **Pattern Matching** — Exhaustive matching on sum types

---

## Table of Contents

1. [Core Concepts](#core-concepts)
2. [Type System](#type-system)
3. [Primitive Types](#primitive-types)
4. [Variables](#variables)
5. [Values and Entities](#values-and-entities)
6. [Functions and Methods](#functions-and-methods)
7. [Control Flow](#control-flow)
8. [Optionals](#optionals)
9. [Error Handling](#error-handling)
10. [Generics](#generics)
11. [Pattern Matching](#pattern-matching)
12. [Interfaces](#interfaces)
13. [Inheritance](#inheritance)
14. [Collections](#collections)
15. [Modules and Visibility](#modules-and-visibility)
16. [Concurrency](#concurrency) *(v0.1)*
17. [Memory Management](#memory-management)
18. [Complete Grammar](#complete-grammar)
19. [Keywords](#keywords)
20. [Operators](#operators)

---

## Core Concepts

Zia programs are made of **modules** containing **types** and **functions**.

```viper
module HelloWorld;

// Two kinds of types: values (copied) and entities (referenced)
value Point {
    Number x;
    Number y;
}

entity User {
    String name;
    String email;
    
    expose func greet() -> String {
        return "Hello, ${name}!";
    }
}

// Entry point
func start() {
    User user = new User(name: "Alice", email: "alice@example.com");
    print(user.greet());
}
```

**Key points:**

- Semicolons required at end of statements
- Type-first variable declarations: `Type name = value;`
- `new` keyword for entity creation
- Types are non-nullable by default
- String interpolation with `${}`
- Memory managed automatically (reference counting)

---

## Type System

### Type Philosophy

Zia has a minimal type system with two fundamental categories:

| Category | Keyword | Semantics | Use Case |
|----------|---------|-----------|----------|
| **Copy Types** | `value` | Copied on assignment | Small data, domain values |
| **Reference Types** | `entity` | Shared reference | Stateful objects, resources |

### Primitive Types

| Type | IL Type | Description |
|------|---------|-------------|
| `Integer` | `i64` | 64-bit signed integer |
| `Number` | `f64` | 64-bit floating point |
| `Boolean` | `i1` | True or false |
| `String` | `str` | UTF-8 immutable string |
| `Byte` | `i32` | 8-bit unsigned (0-255), stored as i32 |
| `Unit` | `void` | Single value `()`, used in `Result[Unit]` |

```viper
Integer count = 42;
Number pi = 3.14159;
Boolean active = true;
String name = "Alice";
Byte b = 255;
```

---

## Primitive Types

### Integer

64-bit signed integer with full arithmetic support.

```viper
Integer count = 42;
Integer negative = -17;
Integer hex = 0xFF;
Integer binary = 0b1010;
Integer big = 1_000_000;    // Underscores for readability

// Arithmetic
Integer sum = a + b;
Integer diff = a - b;
Integer product = a * b;
Integer quotient = a / b;   // Integer division
Integer remainder = a % b;

// Comparisons return Boolean
Boolean isEqual = a == b;
Boolean isLess = a < b;
```

### Number

64-bit IEEE 754 floating-point.

```viper
Number pi = 3.14159;
Number scientific = 6.022e23;
Number negative = -273.15;

// Arithmetic
Number sum = a + b;
Number quotient = a / b;    // True division
```

### Boolean

Two values: `true` and `false`.

```viper
Boolean active = true;
Boolean done = false;

// Logical operators
Boolean both = a && b;      // Short-circuit AND
Boolean either = a || b;    // Short-circuit OR
Boolean negated = !a;       // NOT
```

### String

Immutable UTF-8 string with rich manipulation methods.

```viper
String greeting = "Hello, World!";
String multiline = """
    This is a
    multi-line string
""";

// String interpolation
String message = "Hello, ${name}! You have ${count} messages.";

// Common operations
Integer length = text.len();
String upper = text.upper();
String lower = text.lower();
String trimmed = text.trim();
Boolean contains = text.has("needle");
List[String] parts = text.split(",");
```

### Byte

8-bit unsigned integer for binary data. Values are in range 0-255.

**Note:** Due to IL constraints (no i8 type), `Byte` is stored internally as `i32`. Arithmetic operations mask to 8 bits. This is transparent to the programmer.

```viper
Byte b = 255;
Byte ascii = 0x41;         // 'A'
Byte overflow = 256;       // ERROR: value out of range
```

---

## Variables

### Declaration Syntax

Variables are declared with **type first, then name** (Java style):

```viper
// Explicit type
Integer count = 42;
String name = "Alice";
List[Integer] numbers = [1, 2, 3];

// Type inference with 'var'
var count = 42;            // Inferred as Integer
var name = "Alice";        // Inferred as String
var numbers = [1, 2, 3];   // Inferred as List[Integer]
```

### Mutability

Variables are **mutable by default**. Use `final` for immutable:

```viper
Integer x = 10;            // Mutable
x = 20;                    // OK

final Integer y = 10;      // Immutable
y = 20;                    // ERROR: cannot assign to final variable

final var z = 10;          // Immutable with type inference
```

### Fields

Fields in types follow the same pattern:

```viper
value Point {
    Number x;              // Mutable field
    Number y;
    final String label;    // Immutable field
}

entity User {
    String name;           // Mutable field
    final String id;       // Immutable after construction
}
```

---

## Values and Entities

### Values (Copy Types)

Values are **copied** when assigned or passed. Created **without** `new`:

```viper
value Color {
    Integer r;
    Integer g;
    Integer b;
}

Color red = Color(255, 0, 0);      // No 'new' for values
Color myColor = red;               // Creates a copy
myColor.g = 128;                   // Only myColor changes; red unchanged
```

**Rules for values:**

- Created with `TypeName(args)` — no `new` keyword
- Copied on assignment
- Cannot inherit from other values
- Can implement interfaces
- Ideal for small, immutable data

**Value mutability:**

Value *variables* can have their fields mutated directly:

```viper
Color c = Color(255, 0, 0);
c.g = 128;                    // OK: direct field mutation
```

However, value *methods* receive a copy of `self` and cannot mutate the caller's value:

```viper
value Point {
    Number x;
    Number y;

    // Returns a new Point (does not modify caller's copy)
    func translated(dx: Number, dy: Number) -> Point {
        return Point(x + dx, y + dy);
    }

    // This would only modify the method's local copy, not the caller's
    // func badMutate() { x = 0; }  // Allowed but doesn't affect caller
}

Point p = Point(1.0, 2.0);
Point p2 = p.translated(10.0, 0.0);  // p is unchanged; p2 is new
```

To "mutate" a value via methods, reassign:

```viper
Point p = Point(1.0, 2.0);
p = p.translated(10.0, 0.0);  // Replace p with new value
```

**When to use values:**

- Points, vectors, colors, dates
- Domain values (Money, UserId, Coordinates)
- Data without identity
- Types under ~64 bytes

### Entities (Reference Types)

Entities are **shared** by reference. Created **with** `new`:

```viper
entity Account {
    String owner;
    Number balance;

    expose func deposit(amount: Number) -> Result[Unit] {
        if (amount <= 0) {
            return Err(Error(code: "INVALID", message: "Amount must be positive"));
        }
        balance = balance + amount;
        return Ok(());  // () is the Unit value
    }
}

Account account1 = new Account(owner: "Alice", balance: 100);
Account account2 = account1;    // Same object (reference)
account2.deposit(50)?;          // Both see balance of 150
```

**Key distinction:**

| | Values | Entities |
|-|--------|----------|
| Creation | `Point(1, 2)` | `new User("Alice")` |
| Assignment | Copies data | Copies reference |
| Identity | No identity | Has identity |

**When to use entities:**

- Stateful objects
- Resources (files, connections, handles)
- Objects with identity (User, Order, Session)
- Types that shouldn't be copied

---

## Functions and Methods

### Function Syntax

Functions use `func` keyword. Parameter names are **optional**:

```viper
// With parameter names (required to use the values)
func add(a: Integer, b: Integer) -> Integer {
    return a + b;
}

// Without parameter names (for signatures or unused params)
func process(Integer, String) -> Boolean;

// Void return (no -> clause)
func sayHello(name: String) {
    print("Hello, ${name}!");
}

// With default parameters
func greet(name: String, greeting: String = "Hello") -> String {
    return "${greeting}, ${name}!";
}
```

**Default parameter rules:**
- Default values must be **constant expressions** (literals, const values)
- Evaluated at **compile time**, not each call
- Parameters with defaults must come after parameters without defaults

### Parameter Name Rules

- **With name:** `name: Type` — parameter can be used in body
- **Without name:** `Type` — useful for interface declarations or ignored params
- In implementations, you typically want names to use the values:

```viper
// Interface declares signature (names optional)
interface Comparator[T] {
    func compare(T, T) -> Integer;
}

// Implementation needs names to use values
entity StringComparator implements Comparator[String] {
    expose func compare(a: String, b: String) -> Integer {
        return a.compareTo(b);
    }
}
```

### Methods

Methods are defined inside types:

```viper
value Point {
    Number x;
    Number y;
    
    func distanceTo(other: Point) -> Number {
        Number dx = x - other.x;
        Number dy = y - other.y;
        return sqrt(dx * dx + dy * dy);
    }
    
    func translated(dx: Number, dy: Number) -> Point {
        return Point(x + dx, y + dy);
    }
}

entity Counter {
    Integer count = 0;
    
    expose func increment() {
        count = count + 1;
    }
    
    expose func getCount() -> Integer {
        return count;
    }
}
```

### Named Arguments at Call Site

Arguments can be passed by name at the call site:

```viper
Point p = Point(x: 10.0, y: 20.0);
String msg = greet(name: "Alice", greeting: "Hi");

// Positional arguments
Point p = Point(10.0, 20.0);
String msg = greet("Alice", "Hi");

// Mix positional and named (positional must come first)
String msg = greet("Alice", greeting: "Hi");
```

---

## Control Flow

### If/Else

```viper
if (condition) {
    // then branch
} else if (otherCondition) {
    // else-if branch
} else {
    // else branch
}

// If expression (like ternary)
Integer abs = x >= 0 ? x : -x;

// Multi-line if expression
String grade = if (score >= 90) {
    "A"
} else if (score >= 80) {
    "B"
} else {
    "C"
};
```

### Guard

Early exit for preconditions:

```viper
func process(data: String?) {
    guard (data != null) else {
        print("No data");
        return;
    }
    // data is non-null here
    print(data);
}
```

### While Loop

```viper
while (condition) {
    // body
}

// While-let (checks and unwraps optional)
while (let item = iterator.next()) {
    process(item);
}
```

### For Loop

```viper
// Range-based for (half-open: 0 to 9)
for (i in 0..10) {
    print(i);
}

// Inclusive range (0 to 10)
for (i in 0..=10) {
    print(i);
}

// Collection iteration
for (item in list) {
    print(item);
}

// With explicit type
for (String item in list) {
    print(item);
}

// Tuple destructuring (Map iteration)
for ((key, value) in map) {
    print("${key}: ${value}");
}
```

**Tuple destructuring limitations (v0.1):**
- Only 2-element tuples supported: `(a, b)`
- Only in for-loops and match patterns
- No nested destructuring in for-loops

### Break and Continue

```viper
for (i in 0..100) {
    if (i == 50) {
        break;      // Exit loop
    }
    if (i % 2 == 0) {
        continue;   // Skip to next iteration
    }
    print(i);
}
```

---

## Optionals

### Optional Types

`T?` represents a value that may or may not exist:

```viper
String? maybeName = null;          // No value
String? definitelyName = "Alice";  // Has value

// Check and use
if (maybeName != null) {
    print(maybeName);              // Safe: compiler knows it's non-null
}
```

### Optional Chaining

```viper
// Safe navigation with ?.
String? name = user?.profile?.name;

// Safe indexing with ?[]
Integer? first = list?[0];

// Null coalescing with ??
String name = maybeName ?? "Guest";
```

### If-Let

Unwrap and bind in one step:

```viper
if (let name = maybeName) {
    print("Hello, ${name}!");
} else {
    print("Hello, stranger!");
}
```

### While-Let

Loop while optional has value:

```viper
while (let line = reader.readLine()) {
    process(line);
}
```

### Guard-Let

Early exit if optional is null:

```viper
func greet(name: String?) {
    guard (let n = name) else {
        print("No name provided");
        return;
    }
    print("Hello, ${n}!");
}
```

---

## Error Handling

### Result Type

Zia uses `Result[T]` instead of exceptions:

```viper
value Result[T] = Ok(T) | Err(Error);

value Error {
    String code;
    String message;
}
```

**Unit Type for Success-Only Results:**

When a function can fail but has no meaningful return value, use `Result[Unit]`:

```viper
func saveFile(path: String, content: String) -> Result[Unit] {
    if (!isValidPath(path)) {
        return Err(Error(code: "INVALID_PATH", message: "Path is invalid"));
    }
    File.write(path, content);
    return Ok(());  // () is the Unit value
}
```

The `Unit` type has exactly one value: `()`. It indicates "success with no data".

### Returning Errors

```viper
func divide(a: Number, b: Number) -> Result[Number] {
    if (b == 0) {
        return Err(Error(code: "DIV_ZERO", message: "Division by zero"));
    }
    return Ok(a / b);
}
```

### The ? Operator

Propagate errors concisely:

```viper
func calculate(x: String, y: String) -> Result[Number] {
    Number a = parse(x)?;      // Returns Err if parse fails
    Number b = parse(y)?;
    Number result = divide(a, b)?;
    return Ok(result);
}
```

### Handling Results

```viper
// Pattern matching
match (divide(10, 2)) {
    Ok(value) => print("Result: ${value}");
    Err(e) => print("Error: ${e.message}");
}

// Or with if-let
if (let Ok(value) = divide(10, 2)) {
    print("Result: ${value}");
}
```

---

## Generics

### Generic Types

```viper
value Box[T] {
    T contents;
    
    func map[U](transform: func(T) -> U) -> Box[U] {
        return Box[U](transform(contents));
    }
}

value Pair[A, B] {
    A first;
    B second;
    
    func swap() -> Pair[B, A] {
        return Pair[B, A](second, first);
    }
}
```

### Generic Functions

```viper
func identity[T](value: T) -> T {
    return value;
}

func swap[A, B](pair: Pair[A, B]) -> Pair[B, A] {
    return Pair[B, A](pair.second, pair.first);
}
```

### Usage

```viper
Box[Integer] box = Box[Integer](42);
Box[String] mapped = box.map((x: Integer) -> "${x}");

// Type inference often works
var box = Box(42);               // Box[Integer] inferred
var result = identity("hello");  // String inferred
```

### Generic Constraints (v0.2 — Not Available in v0.1)

v0.1 generics are **unconstrained**: type parameters accept any type, but you cannot use operators or methods on them without explicit casts.

```viper
// v0.1: Works — no operations on T
func identity[T](x: T) -> T {
    return x;
}

// v0.1: Works — explicit type in body
func intMax(a: Integer, b: Integer) -> Integer {
    return a > b ? a : b;
}

// v0.2: Generic constraint syntax (NOT available in v0.1)
func max[T: Comparable](a: T, b: T) -> T {
    return a > b ? a : b;
}
```

**v0.1 Workarounds:**
- Use concrete types instead of generic functions for operations
- Use interface types where polymorphism is needed
- Monomorphization handles type-specific code generation

---

## Pattern Matching

### Sum Types

Define types with multiple variants:

```viper
value Shape = 
    | Circle(radius: Number)
    | Rectangle(width: Number, height: Number)
    | Triangle(a: Number, b: Number, c: Number);
```

### Match Expression

```viper
func area(shape: Shape) -> Number {
    return match (shape) {
        Circle(r) => 3.14159 * r * r;
        Rectangle(w, h) => w * h;
        Triangle(a, b, c) => {
            Number s = (a + b + c) / 2;
            return sqrt(s * (s-a) * (s-b) * (s-c));
        }
    };
}
```

### Pattern Types

**Supported in v0.1:**

| Pattern | Syntax | Description |
|---------|--------|-------------|
| Literal | `0`, `"hello"`, `true` | Match exact value |
| Variable | `x` | Bind value to name |
| Wildcard | `_` | Match anything, discard (reserved symbol) |
| Constructor | `Some(x)`, `Circle(r)` | Match variant, bind fields |
| Tuple | `(a, b)` | Match 2-element tuple |
| Nested | `Some(Circle(r))` | Combine patterns |
| Guard | `x if x > 0` | Conditional match |

**Note:** `_` is a **reserved symbol**, not an identifier. It cannot be used as a variable name anywhere in the program. In patterns, it matches any value without binding.

```viper
match (value) {
    // Literal patterns
    0 => "zero";
    1 => "one";

    // Variable binding
    x => "other: ${x}";

    // Wildcard (ignore value)
    _ => "anything";

    // Constructor patterns
    Some(x) => "has ${x}";
    None => "empty";

    // Nested patterns
    Pair(Some(x), None) => "first only: ${x}";

    // Guards
    x if x > 0 => "positive";
    x if x < 0 => "negative";
}
```

**Not supported in v0.1:**
- Range patterns (`1..10 =>`)
- Type patterns (`x is Integer =>`)
- 3+ element tuple patterns

### Exhaustiveness

The compiler ensures all cases are covered:

```viper
// ERROR: non-exhaustive match
match (shape) {
    Circle(r) => 3.14159 * r * r;
    // Missing Rectangle and Triangle!
}
```

---

## Interfaces

### Defining Interfaces

```viper
interface Drawable {
    func draw(canvas: Canvas);
    func bounds() -> Rectangle;
    
    // Default implementation
    func drawWithBorder(canvas: Canvas, color: Color) {
        draw(canvas);
        canvas.strokeRect(bounds(), color);
    }
}

interface Serializable {
    func toJson() -> String;
}
```

### Implementing Interfaces

Both values and entities can implement interfaces:

```viper
value Circle implements Drawable, Serializable {
    Point center;
    Number radius;
    
    expose func draw(canvas: Canvas) {
        canvas.fillCircle(center, radius);
    }
    
    expose func bounds() -> Rectangle {
        return Rectangle(
            center.x - radius,
            center.y - radius,
            radius * 2,
            radius * 2
        );
    }
    
    expose func toJson() -> String {
        return """{"type":"circle","radius":${radius}}""";
    }
}
```

### Interface as Type

```viper
func render(items: List[Drawable], canvas: Canvas) {
    for (item in items) {
        item.draw(canvas);
    }
}
```

---

## Inheritance

### Entity Inheritance

Only entities can inherit (not values):

```viper
entity Animal {
    String name;
    
    func speak() -> String {
        return "...";
    }
}

entity Dog extends Animal {
    String breed;
    
    override func speak() -> String {
        return "Woof!";
    }
    
    func fetch() -> String {
        return "${name} fetches the ball";
    }
}
```

### Override Rules

- `override` keyword required when overriding
- Cannot override `final` methods
- `super.method()` calls parent implementation

```viper
entity Cat extends Animal {
    override func speak() -> String {
        String base = super.speak();  // Call parent
        return "Meow! (was: ${base})";
    }
}
```

### Using `super`

The `super` keyword accesses the parent class implementation:

| Usage | Description |
|-------|-------------|
| `super.method()` | Call parent's method |
| `super.field` | Access parent's field (if visible) |

**Constraints on `super`:**

```viper
entity Child extends Parent {
    // OK: in override method
    override func greet() -> String {
        return super.greet() + "!";
    }

    // OK: in any method of a subclass
    func callParent() {
        super.doSomething();
    }

    // ERROR: super cannot be used outside a method
    // String x = super.name;  // Not in a method body

    // ERROR: super cannot be assigned
    // super = something;
}

// ERROR: super cannot be used in non-extending entity
entity Standalone {
    func test() {
        // super.anything();  // ERROR: no parent class
    }
}
```

**Note:** `super` always refers to the immediate parent class, not grandparents.

### Polymorphism

```viper
func makeNoise(animal: Animal) {
    print(animal.speak());
}

Dog dog = new Dog(name: "Rex", breed: "German Shepherd");
Cat cat = new Cat(name: "Whiskers");

makeNoise(dog);  // "Woof!"
makeNoise(cat);  // "Meow! (was: ...)"
```

---

## Collections

### List

Dynamic array:

```viper
List[Integer] numbers = [1, 2, 3, 4, 5];
numbers.push(6);

Integer first = numbers[0];          // Panics if out of bounds
Integer? maybe = numbers.get(10);    // Returns null if out of bounds

Integer length = numbers.len();

// Functional operations
List[Integer] doubled = numbers.map((x: Integer) -> x * 2);
List[Integer] evens = numbers.filter((x: Integer) -> x % 2 == 0);
Integer sum = numbers.fold(0, (acc: Integer, x: Integer) -> acc + x);

// Iteration
for (num in numbers) {
    print(num);
}
```

### Map

Key-value dictionary:

```viper
Map[String, Integer] scores = {
    "Alice": 100,
    "Bob": 85
};

scores["Carol"] = 92;
Integer aliceScore = scores["Alice"];     // Panics if missing
Integer? maybeScore = scores.get("Dave"); // Returns null if missing

Boolean hasAlice = scores.has("Alice");
scores.drop("Bob");

// Iteration
for ((name, score) in scores) {
    print("${name}: ${score}");
}
```

### Set

Unique elements:

```viper
Set[Integer] unique = {1, 2, 3, 2, 1};  // {1, 2, 3}

unique.put(4);
Boolean hasTwo = unique.has(2);
unique.drop(1);

for (item in unique) {
    print(item);
}
```

### Collection Literal Disambiguation

Both Map and Set use `{}` syntax. The parser distinguishes them as follows:

| Syntax | Interpretation |
|--------|----------------|
| `{}` | Empty Map (requires type annotation or context) |
| `{expr, expr, ...}` | Set literal (no colons) |
| `{expr: expr, ...}` | Map literal (has colons) |

**Examples:**

```viper
// Requires type annotation for empty
Map[String, Integer] emptyMap = {};
Set[String] emptySet = Set[String]();  // Use constructor for empty Set

// Unambiguous literals
Set[Integer] nums = {1, 2, 3};           // Set (no colons)
Map[String, Integer] ages = {"a": 1};    // Map (has colons)

// Type inference from context
var scores = {"Alice": 100};             // Inferred as Map[String, Integer]
var tags = {"alpha", "beta"};            // Inferred as Set[String]
```

---

## Modules and Visibility

### Module Declaration

```viper
module MyApp.Services.UserService;
```

### Binds

**File binds** — Import Zia source modules:

```viper
bind "./models";                  // Relative path (adds .zia)
bind "./utils" as U;              // With alias
```

**Namespace binds** — Import Viper runtime namespaces:

```viper
bind Viper.Terminal;              // Import all symbols (Say, Print, etc.)
bind Viper.Graphics;              // Import graphics (Canvas, Sprite, etc.)
bind Viper.Terminal as T;         // With alias: T.Say("hi")
bind Viper.Terminal { Say };      // Import specific symbols only
```

When you bind a namespace, its symbols become available without qualification:

```viper
bind Viper.Terminal;
Say("Hello!");                    // Instead of Viper.Terminal.Say("Hello!")
```

### Visibility

**Default visibility:**
- **Entity fields:** Private by default (use `expose` for public)
- **Value fields:** **Public by default** (data containers)
- **Methods:** Private by default (use `expose` for public)

```viper
entity User {
    expose String name;        // Public field (explicit)
    String passwordHash;       // Private field (default for entity)

    expose func getName() -> String {   // Public method
        return name;
    }

    func validatePassword(String) -> Boolean {  // Private method
        // ...
    }
}

value Point {
    Number x;              // Public (default for values)
    Number y;              // Public (default for values)
}

// Use 'hide' to make inherited members private
entity AdminUser extends User {
    hide func getName() -> String {  // Now private in AdminUser
        return super.getName();
    }
}
```

### Module-Level Visibility

```viper
module MyLib;

// Public function
expose func publicApi(x: Integer) -> Integer {
    return helper(x);
}

// Private function (module-internal)
func helper(x: Integer) -> Integer {
    return x * 2;
}
```

---

## Concurrency (v0.1)

Zia v0.1 is **thread-first**: OS threads + shared memory + FIFO locks.

### Threads

Thread entry functions must have one of two signatures:
- `func() -> Void` — No argument
- `func(arg: Ptr) -> Void` — Single opaque pointer argument

**Thread.Start Overloads:**

| Call | Entry Signature | Description |
|------|-----------------|-------------|
| `Thread.Start(fn)` | `func() -> Void` | Start thread with no argument |
| `Thread.Start(fn, arg)` | `func(Ptr) -> Void` | Start thread, passing `arg` to entry |

The `arg` parameter is an opaque `Ptr` (can be `null` or any entity/object cast to `Ptr`).

**Type Checking:**
- Compile-time: Entry function signature must match one of the two allowed forms
- Runtime: Trap with `Thread.Start: invalid entry signature` if signature check fails

```viper
bind Viper.Threads;

func worker() {
    // Work with no argument
}

func workerWithArg(arg: Ptr) {
    // Cast arg back to expected type
    Counter c = arg as Counter;
    c.increment();
}

func start() {
    Thread t1 = Thread.Start(worker);

    Counter c = new Counter();
    Thread t2 = Thread.Start(workerWithArg, c as Ptr);

    t1.join();
    t2.join();
}
```

### Mutex and Monitor (FIFO, Re-entrant)

`Mutex` is the recommended explicit lock object in `Viper.Threads`.
It is implemented on top of the runtime’s FIFO-fair, re-entrant `Monitor` primitive.

```viper
bind Viper.Threads;

entity Counter {
    Mutex mu = new Mutex();
    Integer value = 0;

    expose func inc() {
        mu.enter();
        value = value + 1;
        mu.exit();
    }
}
```

Wait/Pause (v0.1):
- `wait()/waitFor(ms)` release the lock and re-acquire it before returning
- `pause()/pauseAll()` wake one/all waiters (caller must own the lock)

### Safe Variables

Viper exposes `SafeI64` as a FIFO-safe integer cell:

```viper
bind Viper.Threads;

func start() {
    SafeI64 c = SafeI64.New(0);
    c.Add(1);
    Integer now = c.Get();
    print("${now}");
}
```

### Async/Await and Channels (v0.2 - Deferred)

Async/await, tasks, channels, and schedulers are deferred to v0.2 (not part of v0.1).

---

## Memory Management

### Automatic Reference Counting

Zia uses ARC for entities:

```viper
func example() {
    User user = new User("Alice");  // refcount = 1
    User user2 = user;              // refcount = 2
    // user goes out of scope       // refcount = 1
    // user2 goes out of scope      // refcount = 0, deallocated
}
```

### Weak References

The `weak` modifier creates non-owning references that don't affect reference counting:

```viper
entity Node {
    String value;
    Node? next;           // Strong reference (affects refcount)
    weak Node? parent;    // Weak reference (doesn't affect refcount)
}
```

**Weak reference rules:**

| Rule | Description |
|------|-------------|
| Position | Only valid on optional entity fields: `weak T?` |
| Zeroing | When target is deallocated, weak field becomes `null` |
| Load semantics | Reading yields `null` OR a retained strong reference |
| Thread safety | Safe to read from any thread |

**Usage patterns:**

```viper
entity Child {
    weak Parent? parent;  // Weak back-reference (prevents cycle)

    func getParentName() -> String? {
        // Reading weak ref: returns strong ref or null
        if (let p = parent) {
            return p.name;  // Safe: p is retained while in scope
        }
        return null;
    }
}

entity Parent {
    String name;
    List[Child] children;  // Strong references to children
}
```

**Preventing reference cycles:**

```viper
// Without weak: memory leak (A → B → A)
entity A { B? b; }
entity B { A? a; }  // Cycle! Neither can be freed

// With weak: no leak
entity A { B? b; }
entity B { weak A? a; }  // B doesn't keep A alive
```

**Invalid uses of `weak`:**
```viper
weak Integer x;           // ERROR: weak only on entity fields
weak String? name;        // ERROR: String is not an entity
Node? node;
weak node = someNode;     // ERROR: weak is a field modifier, not a variable modifier
```

---

## Complete Grammar

```ebnf
(* Program structure *)
program         = module_decl? import_decl* declaration* ;
module_decl     = "module" qualified_name ";" ;
import_decl     = "import" qualified_name ("as" IDENT)? ";" 
                | "import" qualified_name ".{" IDENT ("," IDENT)* "}" ";" ;

(* Declarations *)
declaration     = value_decl | entity_decl | interface_decl | func_decl ;
value_decl      = "value" IDENT generics? implements? "{" member* "}"
                | "value" IDENT generics? "=" variant ("|" variant)* ";" ;
entity_decl     = "entity" IDENT generics? extends? implements? "{" member* "}" ;
interface_decl  = "interface" IDENT generics? "{" signature* "}" ;
func_decl       = visibility? "func" IDENT generics? "(" params? ")" ("->" type)? block ;

(* Type members *)
member          = field_decl | method_decl ;
field_decl      = visibility? "final"? type IDENT ("=" expr)? ";" ;
method_decl     = visibility? "override"? "func" IDENT "(" params? ")" ("->" type)? block ;

(* Parameters - names are optional *)
params          = param ("," param)* ;
param           = (IDENT ":")? type ("=" expr)? ;

(* Generics *)
generics        = "[" IDENT ("," IDENT)* "]" ;
extends         = "extends" type ;
implements      = "implements" type ("," type)* ;

(* Types *)
type            = IDENT generics?           (* Named type *)
                | type "?"                   (* Optional *)
                | "func" "(" types? ")" "->" type  (* Function type *)
                ;
types           = type ("," type)* ;

(* Statements *)
statement       = var_decl | assignment | expr_stmt | return_stmt
                | if_stmt | match_stmt | while_stmt | for_stmt
                | guard_stmt | break_stmt | continue_stmt | block ;
var_decl        = ("final"? type | "var" | "final" "var") IDENT "=" expr ";" ;
assignment      = expr "=" expr ";" ;
expr_stmt       = expr ";" ;
return_stmt     = "return" expr? ";" ;
if_stmt         = "if" "(" expr ")" block ("else" (if_stmt | block))? ;
match_stmt      = "match" "(" expr ")" "{" match_arm* "}" ;
while_stmt      = "while" "(" ("let" IDENT "=")? expr ")" block ;
for_stmt        = "for" "(" (type? IDENT | "(" IDENT "," IDENT ")") "in" expr ")" block ;
guard_stmt      = "guard" "(" ("let" IDENT "=")? expr ")" "else" block ;
break_stmt      = "break" ";" ;
continue_stmt   = "continue" ";" ;
block           = "{" statement* "}" ;

(* Expressions *)
expr            = literal | IDENT | "self" | "super" "." IDENT
                | "()"                       (* Unit literal *)
                | expr "." IDENT | expr "?." IDENT
                | expr "[" expr "]" | expr "?[" expr "]"
                | expr "(" args? ")"
                | "new" type "(" args? ")"
                | type "(" args? ")"
                | "[" exprs? "]" | "{" map_entries? "}"
                | expr binop expr | unop expr | expr "?"
                | expr "??" expr
                | expr "?" expr ":" expr
                | "if" "(" expr ")" block "else" block
                | "match" "(" expr ")" "{" match_arm* "}"
                | "(" params? ")" "->" (expr | block)
                | "(" expr ")"
                ;

(* Operators *)
binop           = "+" | "-" | "*" | "/" | "%" 
                | "==" | "!=" | "<" | "<=" | ">" | ">="
                | "&&" | "||" | ".." | "..=" ;
unop            = "-" | "!" ;

(* Arguments *)
args            = arg ("," arg)* ;
arg             = (IDENT ":")? expr ;

(* Patterns *)
pattern         = "_"                        (* Wildcard - matches anything *)
                | literal                    (* Literal match *)
                | IDENT                      (* Variable binding *)
                | IDENT "(" patterns? ")"    (* Constructor/variant pattern *)
                | "(" pattern "," pattern ")" (* Tuple pattern - 2 elements only *)
                ;
patterns        = pattern ("," pattern)* ;
match_arm       = pattern ("if" expr)? "=>" (expr | block) ";" ;

(* Visibility *)
visibility      = "expose" | "hide" ;
```

---

## Keywords

### Reserved Keywords (29)

| Keyword | Usage |
|---------|-------|
| `module` | Module declaration |
| `import` | Import declaration |
| `as` | Import alias, type cast |
| `value` | Value type declaration |
| `entity` | Entity type declaration |
| `interface` | Interface declaration |
| `implements` | Interface implementation |
| `extends` | Entity inheritance |
| `func` | Function/method declaration |
| `return` | Return statement |
| `var` | Type inference |
| `final` | Immutable variable/field |
| `new` | Entity creation |
| `if` | Conditional |
| `else` | Conditional branch |
| `let` | Optional binding in if/while/guard |
| `match` | Pattern matching |
| `while` | While loop |
| `for` | For loop |
| `in` | Loop iteration |
| `is` | Type check |
| `break` | Loop exit |
| `continue` | Loop continue |
| `guard` | Guard statement |
| `override` | Method override |
| `weak` | Weak reference |
| `expose` | Public visibility |
| `hide` | Hide inherited member |
| `self` | Current instance |
| `super` | Parent class |

### v0.2 Keywords (+3)

| Keyword | Usage |
|---------|-------|
| `async` | Async function |
| `await` | Await expression |
| `spawn` | Spawn task |

### Contextual Keywords

| Keyword | Usage |
|---------|-------|
| `true` | Boolean true |
| `false` | Boolean false |
| `null` | No value |

---

## Operators

### Precedence Table (highest to lowest)

| Precedence | Operators | Associativity | Description |
|------------|-----------|---------------|-------------|
| 11 | `.` `?.` `[]` `?[]` `()` | Left | Access/call |
| 10 | `?` (postfix) | Postfix | Error propagation |
| 9 | `!` `-` (unary) | Right | Unary |
| 8 | `*` `/` `%` | Left | Multiplication |
| 7 | `+` `-` | Left | Addition |
| 6 | `..` `..=` | None | Range |
| 5 | `is` `as` | Left | Type check/cast |
| 4 | `<` `>` `<=` `>=` | Left | Comparison |
| 3 | `==` `!=` | Left | Equality |
| 2 | `&&` | Left | Logical AND |
| 1 | `||` | Left | Logical OR |
| 0 | `??` | Right | Null coalescing |

### Operator Summary

| Operator | Description | Example |
|----------|-------------|---------|
| `+` `-` `*` `/` `%` | Arithmetic | `a + b` |
| `==` `!=` `<` `>` `<=` `>=` | Comparison | `a < b` |
| `&&` `\|\|` `!` | Logical | `a && b` |
| `?.` | Optional chaining | `user?.name` |
| `??` | Null coalescing | `name ?? "default"` |
| `?` | Error propagation | `getValue()?` |
| `is` | Type check | `x is Integer` |
| `as` | Type cast | `x as String` |
| `..` | Half-open range | `0..10` |
| `..=` | Inclusive range | `0..=10` |

### Type Operations: `is` and `as`

**The `is` operator** checks if a value is of a specific type at runtime:

```viper
func describe(obj: Animal) -> String {
    if (obj is Dog) {
        return "It's a dog!";
    } else if (obj is Cat) {
        return "It's a cat!";
    }
    return "Unknown animal";
}
```

**Valid `is` checks:**
- Entity subtype: `animal is Dog` (where Dog extends Animal)
- Interface implementation: `shape is Drawable`
- Sum type variant: `result is Ok` or `result is Err`

**The `as` operator** performs type casting:

```viper
// Safe cast (returns optional)
Dog? maybeDog = animal as? Dog;

// Forced cast (panics if wrong type)
Dog dog = animal as Dog;
```

| Syntax | Behavior | On Failure |
|--------|----------|------------|
| `x as T` | Forced cast | Panic with "invalid cast" |
| `x as? T` | Safe cast | Returns `null` |

**Usage examples:**

```viper
// Combining is + as
if (animal is Dog) {
    Dog d = animal as Dog;  // Safe: we just checked
    d.fetch();
}

// Safe cast pattern
if (let d = animal as? Dog) {
    d.fetch();
}

// Ptr casting (for thread arguments)
Counter c = new Counter();
Thread.Start(worker, c as Ptr);

func worker(arg: Ptr) {
    Counter c = arg as Counter;
    c.increment();
}
```

**Not supported in v0.1:**
- `is` with generic type parameters: `x is T` where T is generic
- `is` with primitive types: `x is Integer` (use explicit type at declaration)

---

## Design Decisions

### Frozen for v0.1

1. **Java-style declarations:** `Type name = value;` with semicolons
2. **Two type kinds:** `value` (copy) and `entity` (reference)
3. **Entity creation with `new`:** `new User(...)` vs `Point(...)`
4. **No exceptions:** All errors via `Result[T]`
5. **? operator:** Works for both `Result` and `Option`
6. **T? syntax:** Optional type, `null` for no value
7. **Indexing:** `[i]` panics, `.get(i)` returns `Option`
8. **Visibility:** Private by default, `expose` for public
9. **Parameter names optional:** `func foo(Integer, String) -> Boolean`
10. **`var` for inference:** `var x = 42;` instead of explicit type

### Deferred to v0.2

1. **Async/await and Task[T]**
2. **Channels**
3. **Generic constraints** — `[T: Comparable]`
4. **Decimal type**
5. **Pattern matching in parameters**

---

## Appendix: Complete Example

```viper
module TodoApp;

bind Viper.IO.File;

// Domain types
value TodoId {
    String raw;
}

value Todo {
    TodoId id;
    String title;
    Boolean done;
}

// Storage interface
interface TodoStore {
    func save(Todo) -> Result[Unit];
    func load(TodoId) -> Result[Todo?];
    func all() -> Result[List[Todo]];
    func delete(TodoId) -> Result[Unit];
}

// File-based implementation
entity FileTodoStore implements TodoStore {
    String path;

    expose func save(todo: Todo) -> Result[Unit] {
        String json = todoToJson(todo);
        String filename = "${path}/${todo.id.raw}.json";
        File.write(filename, json)?;
        return Ok(());
    }
    
    expose func load(id: TodoId) -> Result[Todo?] {
        String filename = "${path}/${id.raw}.json";
        if (!File.exists(filename)) {
            return Ok(null);
        }
        String json = File.read(filename)?;
        Todo todo = todoFromJson(json)?;
        return Ok(todo);
    }
    
    expose func all() -> Result[List[Todo]] {
        List[String] files = Dir.files(path)?;
        List[Todo] todos = [];
        for (file in files) {
            if (file.endsWith(".json")) {
                String content = File.read("${path}/${file}")?;
                Todo todo = todoFromJson(content)?;
                todos.push(todo);
            }
        }
        return Ok(todos);
    }
    
    expose func delete(id: TodoId) -> Result[Unit] {
        String filename = "${path}/${id.raw}.json";
        File.delete(filename)?;
        return Ok(());
    }
    
    // Private helpers
    func todoToJson(todo: Todo) -> String {
        return """{"id":"${todo.id.raw}","title":"${todo.title}","done":${todo.done}}""";
    }
    
    func todoFromJson(json: String) -> Result[Todo] {
        // Simplified - real implementation would use Viper.Json
        return Ok(Todo(
            id: TodoId("parsed-id"),
            title: "parsed-title",
            done: false
        ));
    }
}

// Entry point
func start() {
    FileTodoStore store = new FileTodoStore(path: "./todos");
    
    Todo todo = Todo(
        id: TodoId("1"),
        title: "Learn Zia",
        done: false
    );
    
    match (store.save(todo)) {
        Ok(_) => print("Saved!");
        Err(e) => print("Error: ${e.message}");
    }
    
    match (store.all()) {
        Ok(todos) => {
            print("Todos:");
            for (t in todos) {
                String status = t.done ? "✓" : " ";
                print("[${status}] ${t.title}");
            }
        }
        Err(e) => print("Error: ${e.message}");
    }
}
```

---

## Migration from Other Languages

### From Java

| Java | Zia | Notes |
|------|-----------|-------|
| `class` | `entity` | Reference type |
| `record` | `value` | Copy type |
| `interface` | `interface` | Same concept |
| `public` | `expose` | Visibility |
| `private` | default | Private by default |
| `new Foo()` | `new Foo()` | Entity creation |
| `throw/catch` | `Result[T]` + `?` | Error handling |
| `null` | `T?` / `null` | Explicit optionals |
| `List<T>` | `List[T]` | Square brackets |

### From C#

| C# | Zia | Notes |
|----|-----------|-------|
| `class` | `entity` | Reference type |
| `struct` | `value` | Copy type |
| `new Foo()` | `new Foo()` | Entity creation |
| `Foo(...)` | `Foo(...)` | Value creation |
| `var` | `var` | Type inference |
| `?.` | `?.` | Optional chaining |
| `??` | `??` | Null coalescing |
| `readonly` | `final` | Immutable |

---

**Version:** v0.1 Final  
**Status:** Specification Complete
