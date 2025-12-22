# Appendix A: ViperLang Reference

A quick reference for ViperLang syntax and features.

---

## Comments

```viper
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

```viper
let x = 42;           // Immutable (recommended)
var y = 42;           // Mutable

let a: i64 = 42;      // Explicit type
let b = 3.14;         // Type inferred (f64)

const PI = 3.14159;   // Compile-time constant
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
```viper
a + b    // Addition
a - b    // Subtraction
a * b    // Multiplication
a / b    // Division
a % b    // Modulo (remainder)
-a       // Negation
```

### Comparison
```viper
a == b   // Equal
a != b   // Not equal
a < b    // Less than
a <= b   // Less than or equal
a > b    // Greater than
a >= b   // Greater than or equal
```

### Logical
```viper
a && b   // And
a || b   // Or
!a       // Not
```

### Bitwise
```viper
a & b    // Bitwise and
a | b    // Bitwise or
a ^ b    // Bitwise xor
~a       // Bitwise not
a << n   // Left shift
a >> n   // Right shift
```

### Assignment
```viper
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
```viper
if condition {
    // ...
} else if other {
    // ...
} else {
    // ...
}

// Expression form
let x = if a > b { a } else { b };
```

### While
```viper
while condition {
    // ...
}

while true {
    if done { break; }
    if skip { continue; }
}
```

### For
```viper
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
```viper
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
```viper
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
```viper
func greet(name: string, greeting: string = "Hello") {
    Viper.Terminal.Say(greeting + ", " + name);
}

greet("Alice");              // "Hello, Alice"
greet("Bob", "Hi");          // "Hi, Bob"
```

### Named Parameters
```viper
func createUser(name: string, age: i64, admin: bool) { ... }

createUser(name: "Alice", age: 30, admin: false);
```

### Variadic Functions
```viper
func sum(numbers: ...i64) -> i64 {
    let total = 0;
    for n in numbers {
        total += n;
    }
    return total;
}

sum(1, 2, 3, 4, 5);  // 15
```

### Lambda/Closures
```viper
let add = func(a: i64, b: i64) -> i64 { return a + b; };
let square = func(x: i64) -> i64 = x * x;

// Short form
let double = (x) => x * 2;

// Capturing variables
let multiplier = 3;
let triple = (x) => x * multiplier;
```

---

## Collections

### Arrays
```viper
let numbers = [1, 2, 3, 4, 5];
let first = numbers[0];
numbers[0] = 10;

let empty: [i64] = [];
let sized = [i64](100);  // Array of 100 zeros

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
```viper
let ages = Map<string, i64>.new();
ages.set("Alice", 30);
ages.set("Bob", 25);

let age = ages.get("Alice");  // 30
let exists = ages.has("Charlie");  // false

ages.delete("Bob");

for key, value in ages {
    Viper.Terminal.Say(key + ": " + value);
}
```

### Sets
```viper
let seen = Set<string>.new();
seen.add("apple");
seen.add("banana");

let exists = seen.contains("apple");  // true
seen.remove("apple");

let other = Set.from(["banana", "cherry"]);
let union = seen.union(other);
let intersection = seen.intersection(other);
```

---

## Strings

```viper
let s = "Hello, World!";

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
let name = "Alice";
let greeting = "Hello, ${name}!";

// Multi-line strings
let text = """
    This is a
    multi-line string.
    """;
```

---

## Structs

```viper
struct Point {
    x: f64;
    y: f64;
}

// Create instance
let p = Point { x: 10.0, y: 20.0 };

// Access fields
let x = p.x;

// Methods
struct Point {
    x: f64;
    y: f64;

    func distance(other: Point) -> f64 {
        let dx = self.x - other.x;
        let dy = self.y - other.y;
        return Viper.Math.sqrt(dx*dx + dy*dy);
    }

    static func origin() -> Point {
        return Point { x: 0.0, y: 0.0 };
    }
}
```

---

## Classes

```viper
class Counter {
    private count: i64;

    constructor() {
        self.count = 0;
    }

    constructor(initial: i64) {
        self.count = initial;
    }

    func increment() {
        self.count += 1;
    }

    func getCount() -> i64 {
        return self.count;
    }
}

let counter = Counter();
counter.increment();
```

### Visibility
```viper
public    // Accessible everywhere
private   // Only within class
protected // Within class and subclasses
internal  // Within module
```

---

## Inheritance

```viper
class Animal {
    protected name: string;

    constructor(name: string) {
        self.name = name;
    }

    func speak() {
        Viper.Terminal.Say("...");
    }
}

class Dog extends Animal {
    constructor(name: string) {
        super(name);
    }

    override func speak() {
        Viper.Terminal.Say(self.name + " says Woof!");
    }
}
```

---

## Interfaces

```viper
interface Drawable {
    func draw();
    func getBounds() -> Rect;
}

class Circle implements Drawable {
    func draw() {
        // ...
    }

    func getBounds() -> Rect {
        // ...
    }
}

// Multiple interfaces
class Button implements Drawable, Clickable {
    // ...
}
```

---

## Enums

```viper
enum Color {
    RED,
    GREEN,
    BLUE
}

let c = Color.RED;

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

let result: Result<i64, string> = Result.Ok(42);
```

---

## Generics

```viper
// Generic function
func identity<T>(value: T) -> T {
    return value;
}

// Generic class
class Box<T> {
    private value: T;

    constructor(value: T) {
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

```viper
// Throwing functions
func divide(a: f64, b: f64) -> f64 {
    if b == 0 {
        throw DivisionByZeroError();
    }
    return a / b;
}

// Try/catch
try {
    let result = divide(10, 0);
} catch DivisionByZeroError {
    Viper.Terminal.Say("Cannot divide by zero");
} catch Error as e {
    Viper.Terminal.Say("Error: " + e.message);
} finally {
    cleanup();
}

// Optional chaining
let value = obj?.property?.method();

// Null coalescing
let name = user.name ?? "Unknown";
```

---

## Modules

```viper
// Define module
module MyModule;

export func publicFunction() { ... }
func privateFunction() { ... }

export class PublicClass { ... }

// Import
import MyModule;
import MyModule.PublicClass;
import MyModule as M;

// Use
MyModule.publicFunction();
M.publicFunction();
```

---

## Nullable Types

```viper
let x: i64? = null;         // Nullable integer
let y: i64? = 42;           // Has value

// Check for null
if x != null {
    Viper.Terminal.Say(x);  // Safe to use
}

// Null coalescing
let value = x ?? 0;  // Use 0 if null

// Optional chaining
let length = name?.length;  // null if name is null
```

---

## Type Aliases

```viper
type UserId = string;
type Callback = func(i64) -> bool;
type StringList = [string];
```

---

## Attributes

```viper
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

```viper
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
and         as          async       await       break
case        catch       class       const       constructor
continue    default     do          else        enum
export      extends     false       finally     for
func        if          implements  import      in
interface   is          let         match       module
new         not         null        or          override
private     protected   public      return      self
static      step        struct      super       test
throw       true        try         type        var
while       yield
```

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
