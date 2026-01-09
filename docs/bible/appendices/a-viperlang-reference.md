# Appendix A: ViperLang Reference

A quick reference for ViperLang syntax and features.

---

## Comments

```rust
// Single-line comment

/*
   Multi-line
   comment
*/

/// Documentation comment
func documented() { ... }
```

---

## Variables

```rust
var x = 42;           // Mutable variable
final y = 42;         // Immutable variable (recommended)

var a: i64 = 42;      // Explicit type annotation
var b = 3.14;         // Type inferred (f64)

final PI = 3.14159;   // Immutable constant
```

---

## Primitive Types

| Type | Description | Example |
|------|-------------|---------|
| `i8`, `i16`, `i32`, `i64` | Signed integers | `42`, `-7` |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers | `42` |
| `f32`, `f64` | Floating point | `3.14`, `2.0e10` |
| `bool` | Boolean | `true`, `false` |
| `char` | Unicode character | `'a'`, `'π'` |
| `string` | Text | `"hello"` |

---

## Operators

### Arithmetic
```rust
a + b    // Addition
a - b    // Subtraction
a * b    // Multiplication
a / b    // Division
a % b    // Modulo (remainder)
-a       // Negation
```

### Comparison
```rust
a == b   // Equal
a != b   // Not equal
a < b    // Less than
a <= b   // Less than or equal
a > b    // Greater than
a >= b   // Greater than or equal
```

### Logical
```rust
a && b   // And
a || b   // Or
!a       // Not
```

### Bitwise
```rust
a & b    // Bitwise and
a | b    // Bitwise or
a ^ b    // Bitwise xor
~a       // Bitwise not
a << n   // Left shift
a >> n   // Right shift
```

### Assignment
```rust
x = 5;     // Assign
x += 5;    // Add and assign
x -= 5;    // Subtract and assign
x *= 5;    // Multiply and assign
x /= 5;    // Divide and assign
x %= 5;    // Modulo and assign
```

---

## Control Flow

### If/Else
```rust
if condition {
    // ...
} else if other {
    // ...
} else {
    // ...
}

// Expression form
var x = if a > b { a } else { b };
```

### While
```rust
while condition {
    // ...
}

while true {
    if done { break; }
    if skip { continue; }
}
```

### For
```rust
// Range (exclusive end)
for i in 0..10 {
    // i = 0, 1, 2, ... 9
}

// Range (inclusive end)
for i in 0..=10 {
    // i = 0, 1, 2, ... 10
}

// With step
for i in 0..100 step 10 {
    // i = 0, 10, 20, ... 90
}

// Iterate collection
for item in items {
    // ...
}

// With index
for i, item in items.enumerate() {
    // ...
}
```

### Match
```rust
match value {
    1 => handleOne(),
    2 | 3 => handleTwoOrThree(),
    4..10 => handleRange(),
    _ => handleDefault()
}

// With bindings
match result {
    Ok(value) => process(value),
    Err(msg) => report(msg)
}
```

---

## Functions

### Basic
```rust
func add(a: i64, b: i64) -> i64 {
    return a + b;
}

// Single expression (implicit return)
func add(a: i64, b: i64) -> i64 = a + b;

// No return value
func greet(name: string) {
    Viper.Terminal.Say("Hello, " + name);
}
```

### Default Parameters
```rust
func greet(name: string, greeting: string = "Hello") {
    Viper.Terminal.Say(greeting + ", " + name);
}

greet("Alice");              // "Hello, Alice"
greet("Bob", "Hi");          // "Hi, Bob"
```

### Named Parameters
```rust
func createUser(name: string, age: i64, admin: bool) { ... }

createUser(name: "Alice", age: 30, admin: false);
```

### Variadic Functions
```rust
func sum(numbers: ...i64) -> i64 {
    var total = 0;
    for n in numbers {
        total += n;
    }
    return total;
}

sum(1, 2, 3, 4, 5);  // 15
```

### Lambda/Closures
```rust
var add = func(a: i64, b: i64) -> i64 { return a + b; };
var square = func(x: i64) -> i64 = x * x;

// Short form
var double = (x) => x * 2;

// Capturing variables
var multiplier = 3;
var triple = (x) => x * multiplier;
```

---

## Collections

### Arrays
```rust
var numbers = [1, 2, 3, 4, 5];
var first = numbers[0];
numbers[0] = 10;

var empty: [i64] = [];
var sized = [i64](100);  // Array of 100 zeros

// Methods
numbers.length
numbers.push(6)
numbers.pop()
numbers.contains(3)
numbers.indexOf(3)
numbers.slice(1, 3)
numbers.reverse()
numbers.sort()
```

### Maps
```rust
var ages = Map<string, i64>.new();
ages.set("Alice", 30);
ages.set("Bob", 25);

var age = ages.get("Alice");  // 30
var exists = ages.has("Charlie");  // false

ages.delete("Bob");

for key, value in ages {
    Viper.Terminal.Say(key + ": " + value);
}
```

### Sets
```rust
var seen = Set<string>.new();
seen.add("apple");
seen.add("banana");

var exists = seen.contains("apple");  // true
seen.remove("apple");

var other = Set.from(["banana", "cherry"]);
var union = seen.union(other);
var intersection = seen.intersection(other);
```

---

## Strings

```rust
var s = "Hello, World!";

// Properties
s.length

// Methods
s.toUpperCase()
s.toLowerCase()
s.trim()
s.split(",")
s.contains("World")
s.startsWith("Hello")
s.endsWith("!")
s.replace("World", "Viper")
s.substring(0, 5)
s.charAt(0)

// String interpolation
var name = "Alice";
var greeting = "Hello, ${name}!";

// Multi-line strings
var text = """
    This is a
    multi-line string.
    """;
```

---

## Values

```rust
value Point {
    x: f64;
    y: f64;
}

// Create instance
var p = Point { x: 10.0, y: 20.0 };

// Access fields
var x = p.x;

// Methods
value Point {
    x: f64;
    y: f64;

    func distance(other: Point) -> f64 {
        var dx = self.x - other.x;
        var dy = self.y - other.y;
        return Viper.Math.sqrt(dx*dx + dy*dy);
    }

    static func origin() -> Point {
        return Point { x: 0.0, y: 0.0 };
    }
}
```

---

## Entities

```rust
entity Counter {
    hide count: i64;

    expose func init() {
        self.count = 0;
    }

    expose func init(initial: i64) {
        self.count = initial;
    }

    func increment() {
        self.count += 1;
    }

    func getCount() -> i64 {
        return self.count;
    }
}

var counter = Counter();
counter.increment();
```

### Visibility
```rust
expose    // Accessible everywhere
hide      // Only within entity
protected // Within entity and subclasses
internal  // Within module
```

---

## Inheritance

```rust
entity Animal {
    protected name: string;

    expose func init(name: string) {
        self.name = name;
    }

    func speak() {
        Viper.Terminal.Say("...");
    }
}

entity Dog extends Animal {
    expose func init(name: string) {
        super(name);
    }

    override func speak() {
        Viper.Terminal.Say(self.name + " says Woof!");
    }
}
```

---

## Interfaces

```rust
interface Drawable {
    func draw();
    func getBounds() -> Rect;
}

entity Circle implements Drawable {
    func draw() {
        // ...
    }

    func getBounds() -> Rect {
        // ...
    }
}

// Multiple interfaces
entity Button implements Drawable, Clickable {
    // ...
}
```

---

## Enums

```rust
enum Color {
    RED,
    GREEN,
    BLUE
}

var c = Color.RED;

// With values
enum HttpStatus {
    OK = 200,
    NOT_FOUND = 404,
    ERROR = 500
}

// With data
enum Result<T, E> {
    Ok(T),
    Err(E)
}

var result: Result<i64, string> = Result.Ok(42);
```

---

## Generics

```rust
// Generic function
func identity<T>(value: T) -> T {
    return value;
}

// Generic entity
entity Box<T> {
    hide value: T;

    expose func init(value: T) {
        self.value = value;
    }

    func get() -> T {
        return self.value;
    }
}

// Constraints
func compare<T: Comparable>(a: T, b: T) -> i64 {
    return a.compareTo(b);
}
```

---

## Error Handling

```rust
// Throwing functions
func divide(a: f64, b: f64) -> f64 {
    if b == 0 {
        throw DivisionByZeroError();
    }
    return a / b;
}

// Try/catch
try {
    var result = divide(10, 0);
} catch DivisionByZeroError {
    Viper.Terminal.Say("Cannot divide by zero");
} catch Error as e {
    Viper.Terminal.Say("Error: " + e.message);
} finally {
    cleanup();
}

// Optional chaining
var value = obj?.property?.method();

// Null coalescing
var name = user.name ?? "Unknown";
```

---

## Modules

```rust
// Define module
module MyModule;

export func publicFunction() { ... }
func privateFunction() { ... }

export entity PublicEntity { ... }

// Import
import MyModule;
import MyModule.PublicEntity;
import MyModule as M;

// Use
MyModule.publicFunction();
M.publicFunction();
```

---

## Nullable Types

```rust
var x: i64? = null;         // Nullable integer
var y: i64? = 42;           // Has value

// Check for null
if x != null {
    Viper.Terminal.Say(x);  // Safe to use
}

// Null coalescing
var value = x ?? 0;  // Use 0 if null

// Optional chaining
var length = name?.length;  // null if name is null
```

---

## Type Aliases

```rust
type UserId = string;
type Callback = func(i64) -> bool;
type StringList = [string];
```

---

## Attributes

```rust
@deprecated("Use newFunction instead")
func oldFunction() { ... }

@inline
func fastFunction() { ... }

@test
func testSomething() {
    assert true;
}
```

---

## Testing

```rust
import Viper.Test;

test "description" {
    assert condition;
    assertEqual(actual, expected);
    assertThrows(func() { riskyCode(); });
}

setup {
    // Run before each test
}

teardown {
    // Run after each test
}
```

---

## Keywords

```
and         as          break       continue    else
entity      expose      extends     false       final
for         func        guard       hide        if
implements  import      in          interface   is
let         match       module      namespace   new
not         null        or          override    return
self        super       true        value       var
weak        while
```

Note: `let` is used for pattern binding in match expressions, not for general variable declarations. Use `var` for mutable variables and `final` for immutable variables.

---

## Operators Precedence

(Highest to lowest)

1. `()` `[]` `.` `?.` Function call, indexing, member access
2. `!` `-` `~` Unary operators
3. `*` `/` `%` Multiplication, division, modulo
4. `+` `-` Addition, subtraction
5. `<<` `>>` Bit shifts
6. `<` `<=` `>` `>=` Comparisons
7. `==` `!=` Equality
8. `&` Bitwise and
9. `^` Bitwise xor
10. `|` Bitwise or
11. `&&` Logical and
12. `||` Logical or
13. `??` Null coalescing
14. `=` `+=` `-=` etc. Assignment

---

*[Back to Table of Contents](../README.md) | [Next: Appendix B: BASIC Reference →](b-basic-reference.md)*
