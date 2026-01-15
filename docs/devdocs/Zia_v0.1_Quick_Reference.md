# Zia v0.1 — Quick Reference

## Syntax at a Glance

```viper
module MyApp;                          // Module declaration

import Viper.IO.File;                  // Import

// Value type (copied)
value Point {
    Number x;
    Number y;
}

// Entity type (referenced)  
entity User {
    String name;
    final String id;                   // Immutable field
    
    expose func greet() -> String {
        return "Hello, ${name}!";
    }
}

// Function
func add(a: Integer, b: Integer) -> Integer {
    return a + b;
}

// Entry point
func start() {
    Point p = Point(1.0, 2.0);         // Value: no 'new'
    User u = new User("Alice", "123"); // Entity: with 'new'
    print(u.greet());
}
```

---

## Variables

```viper
// Type-first declaration (Java style)
Integer count = 42;
String name = "Alice";
List[Integer] nums = [1, 2, 3];

// Type inference
var count = 42;                        // Integer
var name = "Alice";                    // String

// Immutable
final Integer x = 10;
final var y = 20;

// Mutable by default
Integer z = 10;
z = 20;                                // OK
```

---

## Primitive Types

| Type | Example | Description |
|------|---------|-------------|
| `Integer` | `42`, `-17`, `0xFF` | 64-bit signed |
| `Number` | `3.14`, `6.02e23` | 64-bit float |
| `Boolean` | `true`, `false` | Boolean |
| `String` | `"hello"`, `"""multi"""` | UTF-8 string |
| `Byte` | `255`, `0x41` | 8-bit unsigned (stored as i32) |
| `Unit` | `()` | Single value, for `Result[Unit]` |

---

## Value vs Entity

| | Value | Entity |
|-|-------|--------|
| Keyword | `value` | `entity` |
| Creation | `Point(1, 2)` | `new User("Alice")` |
| Assignment | Copies data | Copies reference |
| Inheritance | No | Yes |
| Use for | Data, coordinates | Objects, resources |

```viper
// Value - copied
value Color { Integer r; Integer g; Integer b; }
Color c1 = Color(255, 0, 0);
Color c2 = c1;                         // Copy
c2.g = 128;                            // Only c2 changes

// Entity - shared
entity Counter { Integer count; }
Counter a = new Counter(0);
Counter b = a;                         // Same object
b.count = 10;                          // Both see change
```

---

## Functions

```viper
// Basic function
func add(a: Integer, b: Integer) -> Integer {
    return a + b;
}

// No return value
func sayHi(name: String) {
    print("Hi ${name}!");
}

// Default parameters
func greet(name: String, msg: String = "Hello") -> String {
    return "${msg}, ${name}!";
}

// Parameter names optional (for signatures)
func process(Integer, String) -> Boolean;

// Named arguments at call site
greet(name: "Alice", msg: "Hi");
greet("Bob");                          // Uses default
```

---

## Control Flow

```viper
// If/else
if (x > 0) {
    print("positive");
} else if (x < 0) {
    print("negative");
} else {
    print("zero");
}

// Ternary
Integer abs = x >= 0 ? x : -x;

// While
while (condition) { }

// For (range)
for (i in 0..10) { }                   // 0 to 9
for (i in 0..=10) { }                  // 0 to 10

// For (collection)
for (item in list) { }

// Guard
guard (x != null) else { return; }

// Break/continue
for (i in 0..100) {
    if (i == 50) break;
    if (i % 2 == 0) continue;
}
```

---

## Optionals

```viper
String? name = null;                   // Optional, no value
String? name = "Alice";                // Optional, has value

// Check
if (name != null) {
    print(name);
}

// Optional chaining
String? result = user?.profile?.name;

// Null coalescing
String displayName = name ?? "Guest";

// If-let
if (let n = name) {
    print(n);
}

// Guard-let
guard (let n = name) else { return; }
```

---

## Error Handling

```viper
// Result type
func divide(a: Number, b: Number) -> Result[Number] {
    if (b == 0) {
        return Err(Error("DIV_ZERO", "Division by zero"));
    }
    return Ok(a / b);
}

// ? operator propagates errors
func calc(x: String) -> Result[Number] {
    Number n = parse(x)?;              // Returns Err if fails
    return Ok(n * 2);
}

// Match on result
match (divide(10, 2)) {
    Ok(v) => print("Result: ${v}");
    Err(e) => print("Error: ${e.message}");
}
```

---

## Pattern Matching

```viper
// Sum types
value Shape = 
    | Circle(radius: Number)
    | Rectangle(width: Number, height: Number);

// Match
match (shape) {
    Circle(r) => 3.14 * r * r;
    Rectangle(w, h) => w * h;
}

// With guards
match (x) {
    n if n > 0 => "positive";
    n if n < 0 => "negative";
    _ => "zero";
}
```

---

## Generics

```viper
value Box[T] {
    T contents;
}

func identity[T](x: T) -> T {
    return x;
}

// Usage
Box[Integer] box = Box[Integer](42);
var box = Box(42);                     // Type inferred
```

---

## Interfaces

```viper
interface Drawable {
    func draw(Canvas);
}

value Circle implements Drawable {
    Number radius;
    
    expose func draw(canvas: Canvas) {
        canvas.circle(radius);
    }
}
```

---

## Inheritance

```viper
entity Animal {
    String name;
    func speak() -> String { return "..."; }
}

entity Dog extends Animal {
    override func speak() -> String {
        return "Woof!";
    }
}
```

---

## Collections

```viper
// List
List[Integer] nums = [1, 2, 3];
nums.push(4);
Integer first = nums[0];
Integer? maybe = nums.get(10);
for (n in nums) { }

// Map
Map[String, Integer] scores = {"Alice": 100, "Bob": 85};
scores["Carol"] = 90;
Integer? s = scores.get("Dave");
for ((k, v) in scores) { }

// Set
Set[Integer] unique = {1, 2, 3};
unique.put(4);
Boolean has = unique.has(2);
```

---

## Threads

```viper
import Viper.Threads;

// Thread entry must be: func() -> Void  OR  func(Ptr) -> Void
func worker() { }

func start() {
    Thread t = Thread.Start(worker);
    t.join();
}

// FIFO, re-entrant locking
entity Counter {
    Mutex mu = new Mutex();
    Integer value = 0;

    expose func inc() {
        mu.enter();
        value = value + 1;
        mu.exit();
    }
}

// Wait/Pause (condition-variable style)
// - wait() releases + re-acquires the lock
// - pause()/pauseAll() wake one/all waiters (caller must own the lock)
```

---

## Visibility

```viper
entity User {
    expose String name;                // Public
    String password;                   // Private (default)
    
    expose func getName() -> String {  // Public
        return name;
    }
    
    func hash() -> String { }          // Private
}
```

---

## Operators

| Op | Description | Example |
|----|-------------|---------|
| `+` `-` `*` `/` `%` | Arithmetic | `a + b` |
| `==` `!=` `<` `>` `<=` `>=` | Comparison | `a < b` |
| `&&` `\|\|` `!` | Logical | `a && b` |
| `?.` | Optional chain | `x?.y` |
| `??` | Null coalesce | `x ?? y` |
| `?` | Error propagate | `x?` |
| `..` `..=` | Range | `0..10` |
| `is` | Type check | `x is T` |
| `as` | Type cast | `x as T` |

---

## Keywords

**Types:** `value` `entity` `interface`
**Modifiers:** `final` `expose` `hide` `override` `weak`
**Declarations:** `module` `import` `func` `var` `new`
**Control:** `if` `else` `let` `match` `while` `for` `in` `is` `guard` `break` `continue` `return`
**Inheritance:** `extends` `implements` `self` `super` `as`
**Literals:** `true` `false` `null`
**v0.2:** `async` `await` `spawn`

---

## Quick Examples

### Hello World
```viper
func start() {
    print("Hello, World!");
}
```

### FizzBuzz
```viper
func start() {
    for (i in 1..=100) {
        String out = if (i % 15 == 0) {
            "FizzBuzz"
        } else if (i % 3 == 0) {
            "Fizz"
        } else if (i % 5 == 0) {
            "Buzz"
        } else {
            "${i}"
        };
        print(out);
    }
}
```

### Fibonacci
```viper
func fib(n: Integer) -> Integer {
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

func start() {
    for (i in 0..10) {
        print(fib(i));
    }
}
```

---

**Version:** v0.1 Final
