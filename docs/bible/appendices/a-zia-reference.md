# Appendix A: Zia Reference

A comprehensive reference for Zia syntax and features. This appendix is designed for quick lookup -- for deeper understanding, follow the cross-references to the relevant chapters.

---

## Table of Contents

1. [Comments](#comments)
2. [Variables and Constants](#variables-and-constants)
3. [Primitive Types](#primitive-types)
4. [Operators](#operators)
5. [Control Flow](#control-flow)
6. [Functions](#functions)
7. [Collections](#collections)
8. [Strings](#strings)
9. [Values (Structures)](#values-structures)
10. [Entities (Objects)](#entities-objects)
11. [Visibility Modifiers](#visibility-modifiers)
12. [Inheritance](#inheritance)
13. [Interfaces](#interfaces)
14. [Enums](#enums)
15. [Generics](#generics)
16. [Error Handling](#error-handling)
17. [Modules](#modules)
18. [Nullable Types](#nullable-types)
19. [Type Aliases](#type-aliases)
20. [Attributes](#attributes)
21. [Testing](#testing)
22. [Keywords](#keywords)
23. [Operator Precedence](#operator-precedence)

---

## Comments

Comments document your code and are ignored by the compiler.

### Single-Line Comments

```rust
// This is a single-line comment
var x = 42;  // Comments can follow code
```

### Multi-Line Comments

```rust
/*
   This is a multi-line comment.
   Use for longer explanations or
   temporarily disabling code blocks.
*/
```

### Documentation Comments

```rust
/// Calculates the area of a circle.
/// @param radius The radius of the circle
/// @return The area as a floating-point number
bind Viper.Math;

func circleArea(radius: Number) -> Number {
    return Math.PI * radius * radius;
}
```

**When to use each:**
- Single-line (`//`): Quick notes, explaining a single line
- Multi-line (`/* */`): Longer explanations, temporarily commenting out code
- Documentation (`///`): API documentation for functions, entities, and modules

**Cross-reference:** [Chapter 2: Your First Program](../part1-foundations/02-first-program.md)

---

## Variables and Constants

Variables store values that can be referenced by name.

### Mutable Variables (`var`)

```rust
var x = 42;              // Type inferred as Integer
var name = "Alice";      // Type inferred as String
var pi = 3.14159;        // Type inferred as Number

var count: Integer = 0;  // Explicit type annotation
var ratio: Number = 0.5; // Explicit type when needed

// Variables can be reassigned
var score = 0;
score = 100;             // Valid
score += 50;             // Compound assignment
```

### Immutable Variables (`final`)

```rust
final PI = 3.14159265358979;    // Cannot be changed
final MAX_PLAYERS = 4;          // Convention: UPPER_CASE
final TAX_RATE = 0.08;

// PI = 3.0;  // Error: cannot reassign a constant
```

**When to use:**
- Use `var` when the value needs to change during execution
- Use `final` for values that should never change (mathematical constants, configuration, conversion factors)
- Prefer `final` when possible -- it prevents bugs and communicates intent

**Common patterns:**

```rust
// Accumulator pattern
var total = 0;
for item in items {
    total += item.price;
}

// Configuration constants
final CONFIG_PATH = "/etc/app/config.json";
final DEFAULT_TIMEOUT = 30000;

// Mathematical constants
final GOLDEN_RATIO = 1.618033988749895;
final EULER = 2.718281828459045;
```

**Edge cases and gotchas:**
- Variables must be initialized when declared
- Type inference uses the initial value -- `var x = 5` creates an `i64`, not `f64`
- `final` prevents reassignment of the variable, but if the value is a collection, the collection's contents can still be modified

**Cross-reference:** [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md)

---

## Primitive Types

Zia provides built-in types for common data.

### Integer Types

| Type | Size | Range | Use Case |
|------|------|-------|----------|
| `i8` | 8 bits | -128 to 127 | Small counters, byte data |
| `i16` | 16 bits | -32,768 to 32,767 | Audio samples |
| `i32` | 32 bits | -2.1B to 2.1B | General integers |
| `i64` | 64 bits | Very large range | Default integer type |
| `u8` | 8 bits | 0 to 255 | Byte data, colors |
| `u16` | 16 bits | 0 to 65,535 | Ports, small positive values |
| `u32` | 32 bits | 0 to 4.2B | Array indices |
| `u64` | 64 bits | Very large positive | File sizes, timestamps |

```rust
var count: i64 = 42;       // Default choice for integers
var age: i32 = 25;         // When memory matters
var byte: u8 = 255;        // Unsigned byte
var bigNum: i64 = 9223372036854775807;

// Literals
var decimal = 42;
var hex = 0xFF;            // 255 in hexadecimal
var binary = 0b1010;       // 10 in binary
var withUnderscores = 1_000_000;  // Underscores for readability
```

### Floating-Point Types

| Type | Size | Precision | Use Case |
|------|------|-----------|----------|
| `f32` | 32 bits | ~7 digits | Graphics, games |
| `f64` | 64 bits | ~15 digits | Default for decimals |

```rust
var price: f64 = 19.99;
var temperature = -273.15;        // Type inferred as f64
var scientific = 6.022e23;        // Scientific notation
var small: f32 = 0.001;           // Single precision

// Be aware of floating-point precision
var result = 0.1 + 0.2;           // May not be exactly 0.3
```

### Boolean Type

```rust
var isActive: bool = true;
var isEmpty = false;              // Type inferred

// Boolean expressions
var isAdult = age >= 18;
var canVote = isAdult && isCitizen;
```

### Character Type

```rust
var letter: char = 'a';
var emoji: char = '?';            // Unicode supported
var newline: char = '\n';         // Escape characters work
```

### String Type

```rust
var greeting: string = "Hello, World!";
var empty = "";                   // Empty string
var multiline = """
    This is a
    multi-line string.
    """;
```

**When to use which numeric type:**
- Default to `i64` for integers, `f64` for decimals
- Use smaller types when memory is constrained or interfacing with external systems
- Use unsigned types only when negative values are impossible and you need the extra range

**Cross-reference:** [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md)

---

## Operators

### Arithmetic Operators

```rust
var a = 10;
var b = 3;

a + b      // 13 - Addition
a - b      // 7  - Subtraction
a * b      // 30 - Multiplication
a / b      // 3  - Division (integer division for integers!)
a % b      // 1  - Modulo (remainder)
-a         // -10 - Negation

// Float division
var x = 10.0;
var y = 3.0;
x / y      // 3.333... - True division with floats
```

**Common patterns:**

```rust
// Check if even/odd
var isEven = number % 2 == 0;

// Wrap around (e.g., hours on a clock)
var hour = 25 % 24;  // 1

// Extract last digit
var lastDigit = number % 10;
```

**Gotcha:** Integer division truncates: `7 / 2` is `3`, not `3.5`. Use floats for true division.

### Comparison Operators

```rust
a == b     // Equal
a != b     // Not equal
a < b      // Less than
a <= b     // Less than or equal
a > b      // Greater than
a >= b     // Greater than or equal

// String comparison (lexicographic)
"apple" < "banana"   // true
"Apple" < "apple"    // true (uppercase comes first)
```

**Gotcha:** Don't confuse `=` (assignment) with `==` (comparison).

### Logical Operators

```rust
a && b     // Logical AND - both must be true
a || b     // Logical OR - at least one must be true
!a         // Logical NOT - inverts the value

// Short-circuit evaluation
if obj != null && obj.isValid() {
    // obj.isValid() only called if obj != null
}

// Common patterns
var inRange = x >= 0 && x <= 100;
var isWeekend = day == "Saturday" || day == "Sunday";
var isNotEmpty = !list.isEmpty();
```

### Bitwise Operators

```rust
a & b      // Bitwise AND
a | b      // Bitwise OR
a ^ b      // Bitwise XOR
~a         // Bitwise NOT (complement)
a << n     // Left shift by n bits
a >> n     // Right shift by n bits

// Common patterns
var flag = 0b0001;
var mask = 0b1111;
var result = flag & mask;        // Check bits
var combined = flag1 | flag2;    // Combine flags
var toggled = value ^ mask;      // Toggle bits
```

### Assignment Operators

```rust
x = 5;         // Simple assignment
x += 5;        // x = x + 5
x -= 5;        // x = x - 5
x *= 5;        // x = x * 5
x /= 5;        // x = x / 5
x %= 5;        // x = x % 5
x &= mask;     // x = x & mask
x |= flag;     // x = x | flag
x ^= bits;     // x = x ^ bits
x <<= n;       // x = x << n
x >>= n;       // x = x >> n
```

**Cross-reference:** [Chapter 3: Values and Names](../part1-foundations/03-values-and-names.md)

---

## Control Flow

### If/Else Statements

```rust
// Basic if
if condition {
    // executed if condition is true
}

// If-else
if score >= 90 {
    Terminal.Say("A");
} else if score >= 80 {
    Terminal.Say("B");
} else if score >= 70 {
    Terminal.Say("C");
} else {
    Terminal.Say("Below C");
}

// If as expression (returns a value)
var max = if a > b { a } else { b };
var status = if isActive { "Active" } else { "Inactive" };
```

**Common patterns:**

```rust
// Guard clause pattern - handle edge cases early
func processData(data: Data?) {
    if data == null {
        return;  // Exit early
    }
    // Main logic here
}

// Nested conditions vs. logical operators
// Prefer:
if age >= 18 && hasID {
    allowEntry();
}
// Over deeply nested:
if age >= 18 {
    if hasID {
        allowEntry();
    }
}
```

### While Loops

```rust
// Basic while
var count = 0;
while count < 10 {
    Terminal.Say(count);
    count += 1;
}

// Infinite loop with break
while true {
    var input = readInput();
    if input == "quit" {
        break;           // Exit the loop
    }
    if input == "" {
        continue;        // Skip to next iteration
    }
    processInput(input);
}
```

**Common patterns:**

```rust
// Read until condition
while !file.isEOF() {
    var line = file.readLine();
    process(line);
}

// Polling with timeout
var attempts = 0;
while !isReady() && attempts < 100 {
    Time.Clock.Sleep(100);
    attempts = attempts + 1;
}
```

### For Loops

```rust
// Range (exclusive end)
for i in 0..10 {
    // i = 0, 1, 2, ... 9
}

// Range (inclusive end)
for i in 0..=10 {
    // i = 0, 1, 2, ... 10
}

// Range with step
for i in 0..100 step 10 {
    // i = 0, 10, 20, ... 90
}

// Reverse iteration
for i in 10..0 step -1 {
    // i = 10, 9, 8, ... 1
}

// Iterate over collection
for item in items {
    process(item);
}

// With index using enumerate
for i, item in items.enumerate() {
    Terminal.Say("Item " + i + ": " + item);
}

// Iterate over map entries
for key, value in map {
    Terminal.Say(key + " = " + value);
}
```

**Common patterns:**

```rust
// Process pairs
for i in 0..items.length step 2 {
    var first = items[i];
    var second = items[i + 1];
}

// Find first match
var found = -1;
for i, item in items.enumerate() {
    if item.matches(criteria) {
        found = i;
        break;
    }
}
```

### Match Expressions

Pattern matching for cleaner conditional logic.

```rust
// Basic match
match value {
    1 => handleOne(),
    2 => handleTwo(),
    3 => handleThree(),
    _ => handleDefault()   // _ is the wildcard pattern
}

// Multiple values
match day {
    "Saturday" | "Sunday" => Terminal.Say("Weekend!"),
    _ => Terminal.Say("Weekday")
}

// Range matching
match score {
    90..=100 => "A",
    80..90 => "B",
    70..80 => "C",
    60..70 => "D",
    _ => "F"
}

// Match as expression
var grade = match score {
    90..=100 => "A",
    80..90 => "B",
    _ => "C or below"
};

// Destructuring with bindings
match result {
    Ok(value) => process(value),
    Err(message) => reportError(message)
}

// Matching entity types
match shape {
    Circle(c) => Terminal.Say("Circle with radius " + c.radius),
    Rectangle(r) => Terminal.Say("Rectangle " + r.width + "x" + r.height),
    _ => Terminal.Say("Unknown shape")
}
```

**When to use match vs. if-else:**
- Use `match` when comparing one value against multiple possibilities
- Use `match` for exhaustive handling of enum variants
- Use `if-else` for complex boolean conditions
- Use `match` when you need to destructure values

**Cross-reference:** [Chapter 4: Making Decisions](../part1-foundations/04-decisions.md), [Chapter 5: Repetition](../part1-foundations/05-repetition.md)

---

## Functions

Functions organize code into reusable, named blocks.

### Basic Function Syntax

```rust
// Function with parameters and return type
func add(a: Integer, b: Integer) -> Integer {
    return a + b;
}

// Single-expression function (implicit return)
func add(a: Integer, b: Integer) -> Integer = a + b;

// Function with no return value
bind Viper.Terminal;

func greet(name: String) {
    Terminal.Say("Hello, " + name + "!");
}

// Function with no parameters
func sayHello() {
    Terminal.Say("Hello!");
}
```

### Default Parameters

```rust
bind Viper.Terminal;

func greet(name: string, greeting: string = "Hello") {
    Terminal.Say(greeting + ", " + name + "!");
}

greet("Alice");              // "Hello, Alice!"
greet("Bob", "Hi");          // "Hi, Bob!"

// Multiple defaults
func createRect(width: f64 = 100.0, height: f64 = 100.0) -> Rectangle {
    return Rectangle { width: width, height: height };
}

createRect();                // 100x100
createRect(50.0);            // 50x100
createRect(50.0, 75.0);      // 50x75
```

### Named Parameters

```rust
func createUser(name: String, age: Integer, admin: Boolean, active: Boolean) {
    // ...
}

// Call with named parameters for clarity
createUser(name: "Alice", age: 30, admin: false, active: true);

// Can mix positional and named (positional must come first)
createUser("Alice", 30, admin: false, active: true);
```

**When to use named parameters:**
- When a function has multiple parameters of the same type
- When the meaning of arguments isn't obvious from context
- For boolean parameters (what does `true` mean here?)

### Variadic Functions

```rust
func sum(numbers: ...i64) -> i64 {
    var total = 0;
    for n in numbers {
        total += n;
    }
    return total;
}

sum(1, 2, 3);           // 6
sum(1, 2, 3, 4, 5);     // 15

func log(level: string, messages: ...string) {
    for msg in messages {
        Terminal.Say("[" + level + "] " + msg);
    }
}

log("INFO", "Starting", "Processing", "Done");
```

### Lambda Expressions and Closures

```rust
// Full lambda syntax
var add = func(a: i64, b: i64) -> i64 { return a + b; };

// Single-expression lambda
var square = func(x: i64) -> i64 = x * x;

// Arrow syntax (short form)
var double = (x) => x * 2;
var add = (a, b) => a + b;

// With explicit types
var toUpper = (s: string) => s.toUpperCase();

// Closures capture variables from enclosing scope
var multiplier = 3;
var triple = (x) => x * multiplier;  // Captures 'multiplier'
triple(5);  // 15

// Passing functions as arguments
var numbers = [1, 2, 3, 4, 5];
var squared = numbers.map((x) => x * x);     // [1, 4, 9, 16, 25]
var evens = numbers.filter((x) => x % 2 == 0);  // [2, 4]
```

**Common patterns:**

```rust
// Callback pattern
func fetchData(url: string, onComplete: func(Data)) {
    var data = // ... fetch data ...
    onComplete(data);
}

fetchData("api/users", (data) => {
    Terminal.Say("Got " + data.count + " users");
});

// Sorting with custom comparator
items.sort((a, b) => a.name.compareTo(b.name));

// Chaining operations
var result = numbers
    .filter((x) => x > 0)
    .map((x) => x * 2)
    .reduce(0, (acc, x) => acc + x);
```

**Cross-reference:** [Chapter 7: Breaking It Down](../part1-foundations/07-functions.md)

---

## Collections

### Arrays

Dynamic arrays that grow as needed.

```rust
// Creation
var numbers = [1, 2, 3, 4, 5];
var empty: [i64] = [];
var sized = [i64](100);           // Array of 100 zeros
var filled = [i64](100, 42);      // Array of 100 forty-twos

// Access
var first = numbers[0];           // First element (0-indexed)
var last = numbers[numbers.length - 1];
numbers[0] = 10;                  // Modify element

// Properties
numbers.length                    // Number of elements

// Methods
numbers.push(6);                  // Add to end
var popped = numbers.pop();       // Remove and return last
numbers.insert(0, 99);            // Insert at index
numbers.removeAt(2);              // Remove at index

numbers.contains(3);              // true if element exists
numbers.indexOf(3);               // Index of element, or -1
numbers.lastIndexOf(3);           // Last index of element

var slice = numbers.slice(1, 3);  // Elements from index 1 to 2
numbers.reverse();                // Reverse in place
numbers.sort();                   // Sort in place
numbers.shuffle();                // Randomize order

// Iteration
for num in numbers {
    Terminal.Say(num);
}

for i, num in numbers.enumerate() {
    Terminal.Say("Index " + i + ": " + num);
}

// Functional methods
var doubled = numbers.map((x) => x * 2);
var evens = numbers.filter((x) => x % 2 == 0);
var sum = numbers.reduce(0, (acc, x) => acc + x);
var any = numbers.any((x) => x > 10);
var all = numbers.all((x) => x > 0);
var first = numbers.find((x) => x > 5);
```

**Edge cases:**
- Accessing an invalid index throws an error
- `pop()` on empty array throws an error
- `indexOf()` returns -1 if not found

### Maps (Dictionaries)

Key-value collections for fast lookup.

```rust
// Creation
var ages = Map<string, i64>.new();

// Operations
ages.set("Alice", 30);
ages.set("Bob", 25);

var age = ages.get("Alice");      // 30
var exists = ages.has("Charlie"); // false

ages.delete("Bob");

// Default values
var age = ages.getOrDefault("Unknown", 0);  // 0 if not found

// Properties
ages.size                         // Number of entries
ages.isEmpty()                    // true if empty

// Iteration
for key, value in ages {
    Terminal.Say(key + " is " + value);
}

for key in ages.keys() {
    Terminal.Say(key);
}

for value in ages.values() {
    Terminal.Say(value);
}

// Common pattern: counting occurrences
var counts = Map<string, i64>.new();
for word in words {
    var current = counts.getOrDefault(word, 0);
    counts.set(word, current + 1);
}
```

### Sets

Collections of unique values.

```rust
// Creation
var seen = Set<string>.new();
var fromArray = Set.from(["apple", "banana", "cherry"]);

// Operations
seen.add("apple");
seen.add("apple");                // No effect - already present
var exists = seen.contains("apple");  // true
seen.remove("apple");

// Properties
seen.size
seen.isEmpty()

// Set operations
var other = Set.from(["banana", "cherry", "date"]);
var union = seen.union(other);           // All elements from both
var intersection = seen.intersection(other);  // Elements in both
var difference = seen.difference(other);  // Elements in seen but not other

// Iteration
for item in seen {
    Terminal.Say(item);
}

// Common pattern: removing duplicates
var unique = Set.from(arrayWithDupes).toArray();
```

**Cross-reference:** [Chapter 6: Collections](../part1-foundations/06-collections.md)

---

## Strings

Strings are immutable sequences of characters.

### String Properties and Methods

```rust
var s = "Hello, World!";

// Properties
s.length                          // 13

// Case conversion
s.toUpperCase()                   // "HELLO, WORLD!"
s.toLowerCase()                   // "hello, world!"

// Trimming whitespace
"  hello  ".trim()                // "hello"
"  hello  ".trimStart()           // "hello  "
"  hello  ".trimEnd()             // "  hello"

// Searching
s.contains("World")               // true
s.startsWith("Hello")             // true
s.endsWith("!")                   // true
s.indexOf("o")                    // 4 (first occurrence)
s.lastIndexOf("o")                // 8 (last occurrence)

// Extraction
s.substring(0, 5)                 // "Hello"
s.charAt(0)                       // 'H'
s[0]                              // 'H' (character at index)

// Splitting and joining
"a,b,c".split(",")                // ["a", "b", "c"]
["a", "b", "c"].join(",")         // "a,b,c"

// Replacement
s.replace("World", "Viper")       // "Hello, Viper!"
s.replaceAll("l", "L")            // "HeLLo, WorLd!"

// Padding
"42".padStart(5, '0')             // "00042"
"hi".padEnd(5, '!')               // "hi!!!"

// Repeating
"ab".repeat(3)                    // "ababab"
```

### String Interpolation

```rust
var name = "Alice";
var age = 30;

// Template syntax with ${}
var message = "Hello, ${name}! You are ${age} years old.";

// Expressions in interpolation
var message = "In 10 years: ${age + 10}";
var message = "Name length: ${name.length}";
```

### Multi-Line Strings

```rust
var text = """
    This is a multi-line string.
    Indentation is preserved.
    Special characters like "quotes" work.
    """;

// Leading whitespace up to the closing """ is trimmed
```

### Escape Sequences

| Sequence | Meaning |
|----------|---------|
| `\"` | Double quote |
| `\\` | Backslash |
| `\n` | Newline |
| `\t` | Tab |
| `\r` | Carriage return |
| `\0` | Null character |

```rust
var path = "C:\\Users\\Alice\\Documents";
var quote = "She said \"Hello\"";
var lines = "Line 1\nLine 2\nLine 3";
```

**Cross-reference:** [Chapter 8: Text and Strings](../part2-building-blocks/08-strings.md)

---

## Values (Structures)

Values group related data together. They have value semantics -- assignment creates a copy.

### Defining Values

```rust
value Point {
    x: f64;
    y: f64;
}

value Person {
    name: string;
    age: i64;
    email: string;
}
```

### Creating Instances

```rust
// All fields must be provided
var p = Point { x: 10.0, y: 20.0 };
var person = Person { name: "Alice", age: 30, email: "alice@example.com" };

// Field order doesn't matter
var p2 = Point { y: 5.0, x: 3.0 };
```

### Accessing and Modifying Fields

```rust
// Read fields
var x = p.x;
var name = person.name;

// Modify fields
p.x = 15.0;
person.age = 31;
```

### Methods on Values

```rust
value Rectangle {
    width: f64;
    height: f64;

    // Instance method - uses self
    func area() -> f64 {
        return self.width * self.height;
    }

    func perimeter() -> f64 {
        return 2 * (self.width + self.height);
    }

    // Method with parameters
    func scale(factor: Number) -> Rectangle {
        return Rectangle {
            width: self.width * factor,
            height: self.height * factor
        };
    }

    // Static method - no self, called on the type
    static func square(size: Number) -> Rectangle {
        return Rectangle { width: size, height: size };
    }
}

var rect = Rectangle { width: 10.0, height: 5.0 };
var area = rect.area();          // 50.0
var scaled = rect.scale(2.0);    // Rectangle { width: 20.0, height: 10.0 }
var sq = Rectangle.square(5.0);  // Static method call
```

### Value Semantics

```rust
var p1 = Point { x: 10.0, y: 20.0 };
var p2 = p1;          // p2 is a COPY of p1

p2.x = 99.0;          // Modify p2

Terminal.Say(p1.x);  // 10.0 - p1 is unchanged
Terminal.Say(p2.x);  // 99.0 - only p2 changed
```

**When to use values vs. entities:**
- Use `value` for simple data containers (Point, Color, Config)
- Use `value` when you want copy semantics
- Use `entity` when you need identity, inheritance, or reference semantics

**Cross-reference:** [Chapter 11: Structures](../part2-building-blocks/11-structures.md)

---

## Entities (Objects)

Entities combine data and behavior with reference semantics and support for inheritance.

### Defining Entities

```rust
entity Counter {
    hide count: i64;        // Hidden field (private)

    // Initializer - called when creating instances
    expose func init() {
        self.count = 0;
    }

    // Overloaded initializer
    expose func init(initial: Integer) {
        self.count = initial;
    }

    // Methods
    expose func increment() {
        self.count += 1;
    }

    expose func decrement() {
        if self.count > 0 {
            self.count -= 1;
        }
    }

    expose func getCount() -> i64 {
        return self.count;
    }
}
```

### Creating Instances

```rust
// Calls init()
var counter = Counter();

// Calls init(initial:)
var counter2 = Counter(100);
```

### The `self` Keyword

Inside methods, `self` refers to the current instance:

```rust
entity Player {
    name: string;
    score: i64;

    expose func init(name: String) {
        self.name = name;     // self.name is the field
        self.score = 0;       // name is the parameter
    }

    expose func addScore(points: Integer) {
        self.score += points;
        self.checkAchievement();  // Can call other methods on self
    }

    hide func checkAchievement() {
        if self.score >= 1000 {
            Terminal.Say(self.name + " reached 1000 points!");
        }
    }
}
```

### Reference Semantics

```rust
var c1 = Counter(10);
var c2 = c1;              // c2 references the SAME object as c1

c2.increment();

Terminal.Say(c1.getCount());  // 11 - both see the same object
Terminal.Say(c2.getCount());  // 11
```

**When to use entities:**
- When objects need identity (two Players are different even with same stats)
- When you need inheritance or polymorphism
- When you want to share modifications between references
- When objects have complex lifecycle (initialization, state changes)

**Cross-reference:** [Chapter 14: Objects and Entities](../part3-objects/14-objects.md)

---

## Visibility Modifiers

Control access to fields and methods.

| Modifier | Access Level |
|----------|--------------|
| `expose` | Accessible from anywhere (public) |
| `hide` | Only within the entity (private) |
| `protected` | Within entity and subentities |
| `internal` | Within the same module |

```rust
entity BankAccount {
    hide balance: f64;              // Only this entity can access directly
    protected accountType: string;   // This entity and subentities
    internal bankCode: string;       // Anywhere in this module
    expose ownerName: string;        // Anywhere

    expose func init(owner: String, initial: Number) {
        self.ownerName = owner;
        self.balance = initial;
    }

    // Public method provides controlled access
    expose func getBalance() -> f64 {
        return self.balance;
    }

    expose func deposit(amount: Number) {
        if amount > 0 {
            self.balance += amount;
        }
    }

    // Private helper method
    hide func logTransaction(type: String, amount: Number) {
        // Internal logging
    }
}

// Usage
var account = BankAccount("Alice", 100.0);
Terminal.Say(account.ownerName);     // OK - exposed
Terminal.Say(account.getBalance());  // OK - exposed method
// account.balance = 1000000;               // Error - hidden
```

**Best practices:**
- Hide fields by default, expose through methods
- Use `expose` for the public API
- Use `hide` for implementation details
- Use `protected` when subentities need access
- Use `internal` for module-internal utilities

**Cross-reference:** [Chapter 14: Objects and Entities](../part3-objects/14-objects.md)

---

## Inheritance

Entities can extend other entities to inherit and specialize behavior.

### Basic Inheritance

```rust
entity Animal {
    protected name: string;

    expose func init(name: String) {
        self.name = name;
    }

    expose func speak() {
        Terminal.Say(self.name + " makes a sound");
    }

    expose func getName() -> string {
        return self.name;
    }
}

entity Dog extends Animal {
    hide breed: string;

    expose func init(name: String, breed: String) {
        super(name);          // Call parent initializer
        self.breed = breed;
    }

    // Override parent method
    override expose func speak() {
        Terminal.Say(self.name + " barks: Woof!");
    }

    // New method specific to Dog
    expose func fetch() {
        Terminal.Say(self.name + " fetches the ball");
    }
}

var dog = Dog("Rex", "German Shepherd");
dog.speak();              // "Rex barks: Woof!"
dog.fetch();              // "Rex fetches the ball"
Terminal.Say(dog.getName());  // "Rex" - inherited method
```

### Calling Parent Methods

```rust
entity Cat extends Animal {
    expose func init(name: String) {
        super(name);
    }

    override expose func speak() {
        super.speak();        // Call parent's speak()
        Terminal.Say(self.name + " also purrs");
    }
}

var cat = Cat("Whiskers");
cat.speak();
// Output:
// Whiskers makes a sound
// Whiskers also purrs
```

### Abstract Methods

```rust
entity Shape {
    // Abstract method - must be overridden
    abstract func area() -> f64;
    abstract func perimeter() -> f64;

    // Concrete method using abstract methods
    expose func describe() {
        Terminal.Say("Area: " + self.area());
        Terminal.Say("Perimeter: " + self.perimeter());
    }
}

entity Circle extends Shape {
    hide radius: f64;

    expose func init(radius: Number) {
        self.radius = radius;
    }

    override expose func area() -> f64 {
        return Math.PI * self.radius * self.radius;
    }

    override expose func perimeter() -> f64 {
        return 2 * Math.PI * self.radius;
    }
}
```

**Cross-reference:** [Chapter 15: Inheritance](../part3-objects/15-inheritance.md)

---

## Interfaces

Interfaces define contracts that entities must fulfill.

### Defining Interfaces

```rust
interface Drawable {
    func draw();
    func getBounds() -> Rectangle;
}

interface Clickable {
    func onClick(x: Integer, y: Integer);
    func isPointInside(x: Integer, y: Integer) -> Boolean;
}
```

### Implementing Interfaces

```rust
entity Button implements Drawable, Clickable {
    hide x: i64;
    hide y: i64;
    hide width: i64;
    hide height: i64;
    hide label: string;

    expose func init(x: Integer, y: Integer, w: Integer, h: Integer, label: String) {
        self.x = x;
        self.y = y;
        self.width = w;
        self.height = h;
        self.label = label;
    }

    // Implement Drawable
    expose func draw() {
        // Draw the button
    }

    expose func getBounds() -> Rectangle {
        return Rectangle { x: self.x, y: self.y, width: self.width, height: self.height };
    }

    // Implement Clickable
    expose func onClick(x: Integer, y: Integer) {
        Terminal.Say("Button clicked: " + self.label);
    }

    expose func isPointInside(x: Integer, y: Integer) -> Boolean {
        return x >= self.x && x < self.x + self.width &&
               y >= self.y && y < self.y + self.height;
    }
}
```

### Using Interfaces as Types

```rust
// Function accepts any Drawable
func render(item: Drawable) {
    item.draw();
}

// Array of mixed types implementing same interface
var drawables: [Drawable] = [
    Button(0, 0, 100, 30, "OK"),
    Circle(50, 50, 25),
    Rectangle(10, 10, 80, 60)
];

for item in drawables {
    item.draw();  // Polymorphic call
}
```

### Default Implementations

```rust
interface Printable {
    func toString() -> string;

    // Default implementation
    func print() {
        Terminal.Say(self.toString());
    }
}

entity User implements Printable {
    expose name: string;

    expose func toString() -> string {
        return "User: " + self.name;
    }

    // print() is inherited from interface default
}
```

**Cross-reference:** [Chapter 16: Interfaces](../part3-objects/16-interfaces.md)

---

## Enums

Enums define a type with a fixed set of possible values.

### Simple Enums

```rust
enum Color {
    RED,
    GREEN,
    BLUE
}

var c = Color.RED;

match c {
    Color.RED => Terminal.Say("Red"),
    Color.GREEN => Terminal.Say("Green"),
    Color.BLUE => Terminal.Say("Blue")
}
```

### Enums with Values

```rust
enum HttpStatus {
    OK = 200,
    NOT_FOUND = 404,
    INTERNAL_ERROR = 500
}

var status = HttpStatus.OK;
var code = status.value;          // 200
```

### Enums with Associated Data

```rust
enum Result<T, E> {
    Ok(T),
    Err(E)
}

var success: Result<i64, string> = Result.Ok(42);
var failure: Result<i64, string> = Result.Err("Not found");

match success {
    Result.Ok(value) => Terminal.Say("Got: " + value),
    Result.Err(msg) => Terminal.Say("Error: " + msg)
}

enum Option<T> {
    Some(T),
    None
}

func findUser(id: Integer) -> Option<User> {
    if id == 1 {
        return Option.Some(User("Alice"));
    }
    return Option.None;
}
```

### Enum Methods

```rust
enum Direction {
    NORTH,
    SOUTH,
    EAST,
    WEST;

    func opposite() -> Direction {
        match self {
            Direction.NORTH => Direction.SOUTH,
            Direction.SOUTH => Direction.NORTH,
            Direction.EAST => Direction.WEST,
            Direction.WEST => Direction.EAST
        }
    }

    func isVertical() -> bool {
        return self == Direction.NORTH || self == Direction.SOUTH;
    }
}

var dir = Direction.NORTH;
var opp = dir.opposite();         // Direction.SOUTH
```

**Cross-reference:** [Chapter 17: Polymorphism](../part3-objects/17-polymorphism.md)

---

## Generics

Write code that works with multiple types.

### Generic Functions

```rust
// Single type parameter
func identity<T>(value: T) -> T {
    return value;
}

var num = identity(42);           // T inferred as i64
var str = identity("hello");      // T inferred as string

// Multiple type parameters
func pair<A, B>(first: A, second: B) -> (A, B) {
    return (first, second);
}

var p = pair(1, "one");           // (i64, string)
```

### Generic Entities

```rust
entity Box<T> {
    hide value: T;

    expose func init(value: T) {
        self.value = value;
    }

    expose func get() -> T {
        return self.value;
    }

    expose func set(value: T) {
        self.value = value;
    }
}

var intBox = Box<i64>(42);
var strBox = Box<string>("hello");

Terminal.Say(intBox.get());  // 42
```

### Generic Constraints

```rust
// T must implement Comparable
func max<T: Comparable>(a: T, b: T) -> T {
    if a.compareTo(b) > 0 {
        return a;
    }
    return b;
}

// Multiple constraints
func process<T: Printable + Serializable>(item: T) {
    item.print();
    var data = item.serialize();
}

// Constraint with where clause
func combine<T, U>(items: [T], transform: func(T) -> U) -> [U]
    where U: Hashable
{
    // ...
}
```

### Generic Interfaces

```rust
interface Container<T> {
    func add(item: T);
    func remove() -> T?;
    func contains(item: T) -> bool;
}

entity Stack<T> implements Container<T> {
    hide items: [T];

    expose func init() {
        self.items = [];
    }

    expose func add(item: T) {
        self.items.push(item);
    }

    expose func remove() -> T? {
        return self.items.pop();
    }

    expose func contains(item: T) -> bool {
        return self.items.contains(item);
    }
}
```

---

## Error Handling

Handle exceptional conditions gracefully.

### Throwing Errors

```rust
func divide(a: Number, b: Number) -> Number {
    if b == 0 {
        throw DivisionByZeroError("Cannot divide by zero");
    }
    return a / b;
}

func validateAge(age: Integer) -> Integer {
    if age < 0 {
        throw ValidationError("Age cannot be negative");
    }
    if age > 150 {
        throw ValidationError("Age seems unrealistic");
    }
    return age;
}
```

### Try/Catch Blocks

```rust
try {
    var result = divide(10, 0);
    Terminal.Say(result);
} catch DivisionByZeroError {
    Terminal.Say("Cannot divide by zero!");
} catch MathError as e {
    Terminal.Say("Math error: " + e.message);
} catch Error as e {
    Terminal.Say("General error: " + e.message);
} finally {
    // Always executed, even if error occurred
    cleanup();
}
```

### Custom Error Types

```rust
entity ValidationError extends Error {
    hide field: string;

    expose func init(message: String, field: String) {
        super(message);
        self.field = field;
    }

    expose func getField() -> string {
        return self.field;
    }
}

throw ValidationError("Invalid email format", "email");
```

### Optional Chaining and Null Coalescing

```rust
// Optional chaining - returns null if any part is null
var length = user?.profile?.bio?.length;

// Null coalescing - provide default if null
var name = user.name ?? "Anonymous";
var count = map.get("key") ?? 0;

// Combined
var displayName = user?.profile?.displayName ?? user?.name ?? "Unknown";
```

**Cross-reference:** [Chapter 10: Errors and Recovery](../part2-building-blocks/10-errors.md)

---

## Modules

Organize code into reusable units.

### Defining a Module

```rust
// File: math_utils.zia
module MathUtils;

// Exported - visible to other modules
export func square(x: Number) -> Number {
    return x * x;
}

export func cube(x: Number) -> Number {
    return x * x * x;
}

// Not exported - private to this module
func helper(x: Number) -> Number {
    return x;
}

export entity Calculator {
    // ...
}
```

### Binding Modules

```rust
// Bind entire module
bind MathUtils;
var result = MathUtils.square(5);

// Bind specific items
bind MathUtils.square;
bind MathUtils.Calculator;
var result = square(5);

// Bind with alias
bind MathUtils as Math;
var result = Math.square(5);

// Bind all exports
bind MathUtils.*;
var result = square(5);
```

### Module Organization

```rust
// Nested modules
module Graphics.Shapes;

export entity Circle { ... }
export entity Rectangle { ... }

// Usage
bind Graphics.Shapes.Circle;
var c = Circle(10);
```

**Cross-reference:** [Chapter 12: Modules](../part2-building-blocks/12-modules.md)

---

## Nullable Types

Explicitly handle the absence of values.

### Declaring Nullable Types

```rust
var x: i64? = null;           // Nullable integer
var y: i64? = 42;             // Has value

var name: string? = null;
var user: User? = findUser(id);
```

### Checking for Null

```rust
// Explicit check
if x != null {
    Terminal.Say(x);    // x is known to be non-null here
}

// Pattern matching
match user {
    null => Terminal.Say("No user found"),
    _ => Terminal.Say("Found: " + user.name)
}
```

### Null Coalescing

```rust
// Provide default value
var value = x ?? 0;           // Use 0 if x is null
var name = user?.name ?? "Anonymous";
```

### Optional Chaining

```rust
// Returns null if any part is null
var city = user?.address?.city;
var length = text?.length;

// Chain method calls
var upper = text?.trim()?.toUpperCase();
```

### Force Unwrap (Use Carefully!)

```rust
// Assert that value is not null
var value = x!;               // Throws if x is null

// Only use when you're certain it's not null
if x != null {
    process(x!);
}
```

**Gotcha:** Force unwrap (`!`) will crash your program if the value is null. Prefer null checking or null coalescing.

**Cross-reference:** [Chapter 10: Errors and Recovery](../part2-building-blocks/10-errors.md)

---

## Type Aliases

Create alternative names for types.

```rust
// Simple alias
type UserId = string;
type Timestamp = i64;

// Function type alias
type Callback = func(i64) -> bool;
type EventHandler = func(Event) -> void;

// Collection aliases
type StringList = [string];
type UserMap = Map<string, User>;

// Generic alias
type Result<T> = Result<T, Error>;

// Usage
func processUser(id: UserId) {
    // ...
}

func registerCallback(handler: Callback) {
    // ...
}
```

**When to use:**
- To give meaningful names to complex types
- To make function signatures more readable
- To create domain-specific vocabulary
- To make future type changes easier

---

## Attributes

Metadata annotations that provide additional information to the compiler or tools.

### Built-in Attributes

```rust
@deprecated("Use newFunction instead")
func oldFunction() {
    // ...
}

@deprecated
func anotherOldFunction() {
    // ...
}

@inline                       // Hint to inline this function
func fastPath() {
    // ...
}

@noinline                     // Prevent inlining
func slowPath() {
    // ...
}

@test                         // Mark as test function
func testSomething() {
    // ...
}

@ignore                       // Skip this test
@test
func testBrokenFeature() {
    // ...
}
```

### Multiple Attributes

```rust
@deprecated("Use v2 API")
@inline
func legacyFunction() {
    // ...
}
```

### Custom Attributes

```rust
@serializable
entity User {
    @jsonName("user_name")
    expose name: string;

    @jsonIgnore
    hide password: string;
}
```

---

## Testing

Built-in testing support.

### Test Syntax

```rust
bind Viper.Test;

test "addition works correctly" {
    assert 2 + 2 == 4;
}

test "string concatenation" {
    var result = "Hello" + " " + "World";
    assertEqual(result, "Hello World");
}

test "division by zero throws" {
    assertThrows(func() {
        divide(10, 0);
    });
}
```

### Assertions

```rust
assert condition;                           // Fails if condition is false
assert condition : "Custom message";        // With message

assertEqual(actual, expected);              // Values must be equal
assertNotEqual(a, b);                       // Values must differ
assertTrue(condition);                      // Must be true
assertFalse(condition);                     // Must be false
assertNull(value);                          // Must be null
assertNotNull(value);                       // Must not be null

assertThrows(func() { ... });               // Must throw any error
assertThrows<SpecificError>(func() { ... });// Must throw specific error type
```

### Setup and Teardown

```rust
var testData: [string];

setup {
    // Runs before each test
    testData = ["a", "b", "c"];
}

teardown {
    // Runs after each test
    testData = [];
}

test "uses setup data" {
    assertEqual(testData.length, 3);
}
```

### Test Suites

```rust
suite "Math Operations" {
    test "addition" {
        assertEqual(2 + 2, 4);
    }

    test "subtraction" {
        assertEqual(5 - 3, 2);
    }

    test "multiplication" {
        assertEqual(3 * 4, 12);
    }
}
```

**Cross-reference:** [Chapter 27: Testing](../part5-mastery/27-testing.md)

---

## Keywords

Reserved words that cannot be used as identifiers.

### Declaration Keywords

| Keyword | Purpose |
|---------|---------|
| `entity` | Declare entity (class) |
| `enum` | Declare enumeration |
| `final` | Declare immutable variable |
| `func` | Declare function |
| `interface` | Declare interface |
| `module` | Declare module |
| `type` | Declare type alias |
| `value` | Declare value type (struct) |
| `var` | Declare mutable variable |

### Control Flow Keywords

| Keyword | Purpose |
|---------|---------|
| `break` | Exit loop |
| `continue` | Skip to next iteration |
| `else` | Alternative branch |
| `for` | For loop |
| `if` | Conditional branch |
| `match` | Pattern matching |
| `return` | Return from function |
| `while` | While loop |

### Object-Oriented Keywords

| Keyword | Purpose |
|---------|---------|
| `abstract` | Declare abstract method |
| `extends` | Inherit from entity |
| `implements` | Implement interface |
| `override` | Override parent method |
| `self` | Current instance |
| `static` | Static member |
| `super` | Parent entity |

### Visibility Keywords

| Keyword | Purpose |
|---------|---------|
| `bind` | Bind another module |
| `export` | Export from module |
| `expose` | Public access |
| `hide` | Private access |
| `internal` | Module-internal access |
| `protected` | Protected access |

### Value Keywords

| Keyword | Purpose |
|---------|---------|
| `false` | Boolean false |
| `null` | Null value |
| `true` | Boolean true |

### Other Keywords

| Keyword | Purpose |
|---------|---------|
| `and` | Logical and (alternative to &&) |
| `as` | Type casting, aliasing |
| `catch` | Handle specific error |
| `finally` | Always-execute block |
| `guard` | Guard statement |
| `in` | Iteration, range membership |
| `is` | Type checking |
| `let` | Pattern binding in match |
| `namespace` | Namespace declaration |
| `new` | Object creation (alternative syntax) |
| `not` | Logical not (alternative to !) |
| `or` | Logical or (alternative to ||) |
| `setup` | Test setup |
| `teardown` | Test teardown |
| `test` | Test declaration |
| `throw` | Throw error |
| `try` | Begin error handling block |
| `weak` | Weak reference |

**Note:** `let` is used for pattern binding in match expressions, not for general variable declarations. Use `var` for mutable variables and `final` for immutable variables.

---

## Operator Precedence

Operators from highest to lowest precedence:

| Precedence | Operators | Description |
|------------|-----------|-------------|
| 1 | `()` `[]` `.` `?.` | Grouping, indexing, member access, optional chaining |
| 2 | `!` `-` `~` | Unary not, negation, bitwise complement |
| 3 | `*` `/` `%` | Multiplication, division, modulo |
| 4 | `+` `-` | Addition, subtraction |
| 5 | `<<` `>>` | Bit shifts |
| 6 | `<` `<=` `>` `>=` | Comparisons |
| 7 | `==` `!=` | Equality |
| 8 | `&` | Bitwise and |
| 9 | `^` | Bitwise xor |
| 10 | `|` | Bitwise or |
| 11 | `&&` | Logical and |
| 12 | `||` | Logical or |
| 13 | `??` | Null coalescing |
| 14 | `=` `+=` `-=` `*=` `/=` `%=` | Assignment |

**Examples:**

```rust
// Multiplication before addition
2 + 3 * 4           // 14, not 20

// Use parentheses to override
(2 + 3) * 4         // 20

// Comparison before logical
x > 0 && x < 10     // Parsed as (x > 0) && (x < 10)

// Null coalescing has low precedence
a ?? b + c          // Parsed as a ?? (b + c)

// Assignment has lowest precedence
x = y + z           // Parsed as x = (y + z)
```

**When in doubt, use parentheses!** They make your intent clear and prevent precedence bugs.

---

## Quick Reference Card

### Variable Declaration
```rust
var x = 42;                   // Mutable
final PI = 3.14;              // Immutable
```

### Function Definition
```rust
func name(param: Type) -> ReturnType {
    return value;
}
```

### Entity Definition
```rust
entity Name {
    hide field: Type;
    expose func init() { }
    expose func method() { }
}
```

### Value Definition
```rust
value Name {
    field: Type;
    func method() -> Type { }
}
```

### Control Flow
```rust
if cond { } else { }          // Conditional
for i in 0..10 { }            // Loop
while cond { }                // While loop
match x { a => ..., _ => ...} // Pattern match
```

### Error Handling
```rust
try { } catch Error { } finally { }
throw ErrorType();
value ?? default
obj?.field
```

### Common Patterns
```rust
// Null check
if obj != null { }

// Type check
if obj is Type { }

// Collection iteration
for item in collection { }
for i, item in collection.enumerate() { }
```

---

*[Back to Table of Contents](../README.md) | [Next: Appendix B: BASIC Reference](b-basic-reference.md)*
