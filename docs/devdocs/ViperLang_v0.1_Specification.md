# ViperLang — Language Specification

## Version 0.1 Final

<div align="center">

**A modern language that's actually simple**

*Copy types. Pattern matching. No exceptions. One way to do things.*

</div>

---

## Philosophy

ViperLang adheres to five core principles:

1. **Truly Simple** — The entire language fits in your head (25 keywords)
2. **One Way** — There's an obvious way to do things
3. **Modern First** — Async, pattern matching, and null safety by default
4. **Fast by Default** — Zero-cost abstractions, no hidden allocations
5. **Amazing Tools** — Formatter, linter, and test runner are part of the language

---

## Table of Contents

1. [Core Concepts](#core-concepts)
2. [Type System](#type-system)
3. [Primitive Types](#primitive-types)
4. [Values and Entities](#values-and-entities)
5. [Optionals](#optionals)
6. [Generics](#generics)
7. [Control Flow](#control-flow)
8. [Error Handling](#error-handling)
9. [Functions](#functions)
10. [Concurrency](#concurrency) *(v0.2 Preview)*
11. [Memory Management](#memory-management)
12. [Modules and Visibility](#modules-and-visibility)
13. [Collections](#collections)
14. [Standard Library Mapping](#standard-library-mapping)
15. [Complete Grammar](#complete-grammar)
16. [Keywords](#keywords)
17. [Operators](#operators)
18. [Design Decisions](#design-decisions)
19. [Testing](#testing)
20. [Tooling](#tooling)

---

## Core Concepts

ViperLang programs are made of **modules** containing **types** and **functions**.

```viper
module HelloWorld

// Two kinds of types: values (copied) and entities (referenced)
value Point {
    x: Number
    y: Number
}

entity User {
    name: Text
    email: Text
    
    func greet() -> Text {
        return "Hello, ${name}!"
    }
}

// Entry point
func main() {
    let user = User(name: "Alice", email: "alice@example.com")
    print(user.greet())
}
```

**Key points:**

- Semicolons optional (formatter adds them)
- Types are non-nullable by default
- String interpolation with `${}`
- Memory managed automatically (reference counting)

---

## Type System

### Type Philosophy

ViperLang has a deliberately minimal type system with two fundamental categories:

| Category | Keyword | Semantics | Use Case |
|----------|---------|-----------|----------|
| **Copy Types** | `value` | Copied on assignment | Small data, domain values |
| **Reference Types** | `entity` | Shared reference | Stateful objects, resources |

### Type Aliases

For clarity, ViperLang provides semantic type names that map to implementation types:

| Language Type | Implementation | IL Type | Description |
|---------------|----------------|---------|-------------|
| `Text` | `Viper.String` | `str` | UTF-8 immutable string |
| `Integer` | — | `i64` | 64-bit signed integer |
| `Number` | — | `f64` | 64-bit floating point |
| `Boolean` | — | `i1` | True or false |
| `Byte` | — | `i8` | 8-bit unsigned (0-255) |

Both forms are valid and interchangeable:

```viper
let name: Text = "Alice"      // Semantic name (preferred)
let count: Integer = 42       // Semantic name (preferred)

let name: String = "Alice"    // Implementation name (valid)
let count: i64 = 42           // IL type name (valid in low-level code)
```

**Recommendation:** Use semantic names (`Text`, `Integer`, `Number`, `Boolean`) in application code. Use IL types (`i64`, `f64`, `str`) only in performance-critical or low-level code.

---

## Primitive Types

### Integer

64-bit signed integer with full arithmetic support.

```viper
let count: Integer = 42
let negative: Integer = -17
let hex: Integer = 0xFF
let binary: Integer = 0b1010
let big: Integer = 1_000_000    // Underscores for readability

// Arithmetic
let sum = a + b
let diff = a - b
let product = a * b
let quotient = a / b           // Integer division
let remainder = a % b

// Comparisons return Boolean
let isEqual = a == b
let isLess = a < b
```

### Number

64-bit IEEE 754 floating-point.

```viper
let pi: Number = 3.14159
let scientific: Number = 6.022e23
let negative: Number = -273.15

// Arithmetic (same operators as Integer)
let sum = a + b
let quotient = a / b           // True division

// Special values
let inf = 1.0 / 0.0            // Infinity
let nan = 0.0 / 0.0            // Not a Number
```

### Boolean

Two values: `true` and `false`.

```viper
let active: Boolean = true
let done: Boolean = false

// Logical operators
let both = a && b              // Short-circuit AND
let either = a || b            // Short-circuit OR
let negated = !a               // NOT
```

### Text

Immutable UTF-8 string with rich manipulation methods.

```viper
let greeting: Text = "Hello, World!"
let multiline: Text = """
    This is a
    multi-line string
"""

// String interpolation
let message = "Hello, ${name}! You have ${count} messages."

// Common operations (matches Viper.String runtime)
let length = text.len()
let upper = text.upper()
let lower = text.lower()
let trimmed = text.trim()
let contains = text.has("needle")
let parts = text.split(",")
```

### Byte

8-bit unsigned integer for binary data.

```viper
let b: Byte = 255
let ascii: Byte = 0x41         // 'A'
```

---

## Values and Entities

### Values (Copy Types)

Values are **copied** when assigned or passed. Mutations affect only that copy:

```viper
value Color {
    r: Integer
    g: Integer
    b: Integer
}

let red = Color(r: 255, g: 0, b: 0)
var myColor = red              // Creates a copy
myColor.g = 128                // Only myColor changes; red unchanged
```

**Rules for values:**

- Cannot mutate temporaries: `getColor().r = 10` is illegal
- Cannot inherit from other values
- Can implement interfaces
- Ideal for small, immutable data

**When to use values:**

- Points, vectors, colors, dates
- Domain values (Money, UserId, Coordinates)
- Data without identity
- Types under ~64 bytes

### Entities (Reference Types)

Entities are **shared** by reference. Multiple variables can refer to the same object:

```viper
entity Account {
    owner: Text
    balance: Number
    
    func deposit(amount: Number) -> Result[Void] {
        if amount <= 0 {
            return Err(Error(code: "INVALID", message: "Amount must be positive"))
        }
        balance = balance + amount
        return Ok(())
    }
}

let account1 = Account(owner: "Alice", balance: 100)
let account2 = account1        // Same object (reference)
account2.deposit(50)?          // Both see balance of 150
```

**When to use entities:**

- Stateful objects
- Resources (files, connections, handles)
- Objects with identity (User, Order, Session)
- Types that shouldn't be copied

### Inheritance (Entities Only)

Entities support single inheritance:

```viper
entity Animal {
    name: Text
    
    func speak() -> Text {
        return "..."
    }
}

entity Dog extends Animal {
    breed: Text
    
    override func speak() -> Text {    // 'override' required
        return "Woof!"
    }
    
    func describe() -> Text {
        let base = super.speak()       // Call parent (only in override)
        return "${name} is a ${breed}"
    }
}
```

**Inheritance rules:**

- Only entities can inherit (not values)
- Single inheritance only (no diamond problem)
- `override` keyword required when overriding methods
- `super.method()` only valid inside an override of that method
- Private fields (`hide`) are not visible to subclasses

### Interfaces

Interfaces define contracts. Both values and entities can implement them:

```viper
interface Drawable {
    func draw(canvas: Canvas)
    
    // Default implementation (can only call interface members)
    func drawTwice(canvas: Canvas) {
        draw(canvas)
        draw(canvas)
    }
}

interface Serializable {
    func toJson() -> Text
}

// Entity implementing multiple interfaces
entity Button implements Drawable, Serializable {
    label: Text
    position: Point
    
    func draw(canvas: Canvas) {
        canvas.drawRect(position)
        canvas.drawText(label, position)
    }
    
    func toJson() -> Text {
        return "{\"label\": \"${label}\"}"
    }
}

// Value implementing interface
value Circle implements Drawable {
    center: Point
    radius: Number
    
    func draw(canvas: Canvas) {
        canvas.drawCircle(center, radius)
    }
}
```

**Interface rules:**

- Default methods may only use interface-declared members
- No fields in interfaces
- Multiple interface implementation allowed

---

## Optionals

### T? is Option[T]

Optionals represent values that may be absent. `T?` is syntactic sugar for `Option[T]`:

```viper
// These are equivalent:
let maybe: Text? = null           // Sugar syntax
let maybe: Option[Text] = None    // Explicit syntax

// Assigning a value automatically wraps in Some
maybe = "Hello"                   // Becomes Some("Hello")
```

### Working with Optionals

```viper
// Pattern matching (most explicit)
match findUser(id) {
    Some(user) => print(user.name)
    None => print("Not found")
}

// If-let (convenient for single case)
if let user = findUser(id) {
    print(user.name)             // user is User, not User?
}

// Optional chaining
let street = user?.address?.street?.name    // Returns Text?

// Null coalescing
let name = user?.name ?? "Anonymous"        // Returns Text

// Propagation with ?
func getUserEmail(id: Text) -> Option[Text] {
    let user = findUser(id)?               // Returns None if not found
    let profile = user.profile?            // Returns None if no profile
    return Some(profile.email)
}
```

### Option Definition

Option is a built-in sum type:

```viper
value Option[T] = Some(T) | None
```

---

## Generics

Types and functions can be parameterized:

```viper
// Generic value (sum type)
value Result[T] = Ok(T) | Err(Error)

// Generic entity
entity Box[T] {
    contents: T
    
    func get() -> T {
        return contents
    }
    
    func set(value: T) {
        contents = value
    }
}

// Generic function
func first[T](list: List[T]) -> Option[T] {
    if list.isEmpty() {
        return None
    }
    return Some(list[0])
}

// Generic function with multiple type parameters
func zip[A, B](listA: List[A], listB: List[B]) -> List[(A, B)] {
    // ...
}
```

**Note:** Generic type constraints (bounds) are deferred to v0.2. In v0.1, generic types are unconstrained.

---

## Control Flow

### Pattern Matching

Pattern matching is the primary control flow mechanism:

```viper
value Shape = 
    | Circle(radius: Number)
    | Rectangle(width: Number, height: Number)
    | Triangle(base: Number, height: Number)

func area(shape: Shape) -> Number {
    match shape {
        Circle(r) => 3.14159 * r * r
        Rectangle(w, h) => w * h
        Triangle(b, h) => 0.5 * b * h
    }
}

// With guards
match value {
    n where n < 0 => "Negative"
    0 => "Zero"
    n where n > 0 => "Positive"
}

// Match as expression
let description = match status {
    Active => "Running"
    Paused => "On hold"
    Stopped => "Complete"
}
```

### Destructuring

```viper
// Value destructuring
let Point(x, y) = getPoint()

// Tuple destructuring
let (first, second) = getPair()

// In function parameters
func distance(Point(x1, y1): Point, Point(x2, y2): Point) -> Number {
    let dx = x2 - x1
    let dy = y2 - y1
    return sqrt(dx * dx + dy * dy)
}
```

### Conditionals

```viper
// Standard if/else
if temperature > 30 {
    turnOnAC()
} else if temperature < 10 {
    turnOnHeater()
} else {
    maintainTemperature()
}

// Conditional expression (ternary)
let status = isOnline ? "Connected" : "Offline"

// If-let for optionals
if let user = findUser(id) {
    print(user.name)           // user is User, not User?
} else {
    print("Not found")
}

// While-let
while let message = channel.receive() {
    process(message)
}
```

### Guard (Early Exit)

Guard provides clean early-exit validation:

```viper
func processOrder(order: Order?) -> Result[Receipt] {
    guard order != null else {
        return Err(Error(code: "NULL", message: "Order required"))
    }
    
    guard order.items.len() > 0 else {
        return Err(Error(code: "EMPTY", message: "Order has no items"))
    }
    
    guard order.total > 0 else {
        return Err(Error(code: "INVALID", message: "Invalid total"))
    }
    
    // order is now known to be valid
    return Ok(createReceipt(order))
}
```

**Note:** Guard is syntactic sugar that can always be replaced with `if-else`. It exists for readability in validation sequences.

### Loops

```viper
// For-each
for item in collection {
    process(item)
}

// For with index (using enumerate)
for (index, item) in collection.enumerate() {
    print("${index}: ${item}")
}

// While
while hasMore() {
    let item = getNext()
    if shouldSkip(item) {
        continue
    }
    if isDone() {
        break
    }
    handle(item)
}

// Ranges
for i in 0..10 {               // Half-open: 0, 1, 2, ..., 9
    print(i)
}

for i in 0..=10 {              // Inclusive: 0, 1, 2, ..., 10
    print(i)
}

// Reverse iteration
for i in (0..10).reverse() {
    print(i)                   // 9, 8, 7, ..., 0
}
```

---

## Error Handling

### No Exceptions

ViperLang does not have exceptions. Errors are values returned from functions using `Result[T]`:

```viper
value Error {
    code: Text
    message: Text
}

value Result[T] = Ok(T) | Err(Error)
```

### Returning Errors

```viper
func divide(a: Number, b: Number) -> Result[Number] {
    if b == 0 {
        return Err(Error(code: "DIV_ZERO", message: "Division by zero"))
    }
    return Ok(a / b)
}

func readFile(path: Text) -> Result[Text] {
    if !fileExists(path) {
        return Err(Error(code: "NOT_FOUND", message: "File not found: ${path}"))
    }
    // Read and return file contents
    return Ok(contents)
}
```

### Handling Errors

```viper
// Pattern matching (most explicit)
match divide(10, 2) {
    Ok(value) => print("Result: ${value}")
    Err(e) => print("Error: ${e.message}")
}

// Propagation with ? (concise)
func calculate() -> Result[Number] {
    let a = getValue("a")?        // Returns Err if getValue fails
    let b = getValue("b")?        // Returns Err if getValue fails
    let result = divide(a, b)?    // Returns Err if divide fails
    return Ok(result * 2)
}
```

### The ? Operator

The `?` operator works with both `Result` and `Option`:

```viper
// With Result: propagates Err
func processFile(path: Text) -> Result[Data] {
    let content = readFile(path)?         // Err propagates up
    let parsed = parseData(content)?      // Err propagates up
    let validated = validate(parsed)?     // Err propagates up
    return Ok(validated)
}

// With Option: propagates None
func findUserEmail(id: Text) -> Option[Text] {
    let user = findUser(id)?              // None propagates up
    let profile = user.profile?           // None propagates up
    return Some(profile.email)
}
```

### Panic

For truly unrecoverable programmer errors:

```viper
func getUser(id: Text) -> User {
    match database.find(id) {
        Some(user) => user
        None => panic("User ${id} must exist")   // Crashes program
    }
}
```

**Rule:** Use `Result` for expected failures. Use `panic` only for violated invariants and impossible states.

---

## Functions

### Basic Functions

```viper
// Simple function
func add(a: Integer, b: Integer) -> Integer {
    return a + b
}

// No return value
func greet(name: Text) {
    print("Hello, ${name}!")
}

// Expression body (single expression)
func double(x: Integer) -> Integer = x * 2

// Default parameters
func greet(name: Text = "World") {
    print("Hello, ${name}!")
}
```

### Named Arguments

```viper
func createUser(name: Text, email: Text, age: Integer = 0) -> User {
    return User(name: name, email: email, age: age)
}

// Call with named arguments (order doesn't matter)
let user = createUser(email: "bob@example.com", name: "Bob")
```

### Lambdas

```viper
// Full syntax
let add = (a: Integer, b: Integer) -> Integer {
    return a + b
}

// Expression syntax (single expression, inferred return type)
let double = (x: Integer) -> x * 2

// Type can be inferred from context
let numbers = [1, 2, 3, 4, 5]
let doubled = numbers.map(x => x * 2)
let evens = numbers.filter(x => x % 2 == 0)
```

### Methods

Methods are functions defined inside types:

```viper
entity Counter {
    count: Integer = 0
    
    // Instance method (has access to fields)
    func increment() {
        count = count + 1
    }
    
    // Method with return value
    func get() -> Integer {
        return count
    }
    
    // Method with parameters
    func add(amount: Integer) {
        count = count + amount
    }
}
```

---

## Concurrency

> **Status:** Concurrency primitives are planned for v0.2. This section describes the intended design for early feedback. Implementation details may change.

### Tasks and Async/Await (v0.2)

```viper
// Async function
async func fetchUser(id: Text) -> User {
    let response = await http.get("/users/${id}")
    return User.fromJson(response.body)
}

// Sequential await
async func main() {
    let user = await fetchUser("123")
    let posts = await fetchPosts(user.id)
}

// Parallel await with all()
async func loadDashboard() {
    let [user, settings, notifications] = await all([
        fetchUser(),
        fetchSettings(),
        fetchNotifications()
    ])
}
```

### Channels (v0.2)

```viper
let channel = Channel[Message](capacity: 10)

// Producer
async {
    for i in 0..100 {
        match channel.send(Message(i)) {
            Ok(()) => continue
            Err(Closed) => break
        }
    }
    channel.close()
}

// Consumer
async {
    while let msg = channel.receive() {
        process(msg)
    }
}
```

**v0.2 Preview Notes:**

- `Task[T]` represents an async computation
- `await` joins a task and gets its result
- `all([...])` runs tasks in parallel, fails fast on first error
- Channels are bounded with configurable capacity
- `receive()` returns `Option[T]` (None when closed and drained)
- `send()` returns `Result[(), Closed]`

---

## Memory Management

### Reference Counting

ViperLang uses automatic reference counting:

- **Values:** Stack-allocated when possible, copied when assigned
- **Entities:** Reference counted, deallocated when count reaches zero
- **Cycles:** Detected and collected periodically

### Weak References

Break reference cycles with `weak`:

```viper
entity Node {
    value: Integer
    children: List[Node]
    weak parent: Node?          // Doesn't increase refcount
}

entity Observer {
    weak subject: Subject?      // Prevent retain cycles
}
```

**Rules:**

- Weak references don't increase refcount
- Reading a weak reference returns `T?` (may be null if target was deallocated)
- Use weak for parent pointers, delegates, observers

```viper
// Reading weak reference
if let parent = node.parent {
    parent.notifyChild(node)
}
```

---

## Modules and Visibility

### Module Declaration

Every file declares its module:

```viper
module MyApp.Services.UserService

import MyApp.Models.User
import MyApp.Data.Database as DB
import Viper.IO

// Module contents...
```

### Visibility

**Default: private.** Use `expose` to make items public:

```viper
module MyApp.Services

// Private by default
value InternalConfig {
    timeout: Integer
}

// Public type
expose entity UserService {
    // Private field
    database: DB.Connection
    
    // Explicitly private (documentation)
    hide cache: Map[Text, User]
    
    // Public method
    expose func getUser(id: Text) -> Result[User] {
        // ...
    }
    
    // Private helper
    func validateId(id: Text) -> Boolean {
        // ...
    }
}

// Public function
expose func createService(config: Config) -> UserService {
    // ...
}
```

**Visibility rules:**

- `expose` — Public to importers
- `hide` — Explicitly private (same as default, but documents intent)
- Default — Private within module

---

## Collections

### Built-in Collections

ViperLang provides generic collection types that map to the runtime library:

| Language Type | Runtime Implementation | Description |
|---------------|----------------------|-------------|
| `List[T]` | `Viper.Collections.Seq` | Dynamic array |
| `Map[Text, V]` | `Viper.Collections.Map` | String-keyed hash map |
| `Set[T]` | `Viper.Collections.Bag` | Unique elements |

### List[T]

```viper
// Creation
let numbers: List[Integer] = [1, 2, 3, 4, 5]
let empty: List[Text] = []

// Access (panics if out of bounds)
let first = numbers[0]          // 1
let bad = numbers[100]          // panic!

// Safe access (returns Option)
match numbers.get(0) {
    Some(n) => print(n)
    None => print("Empty")
}

// Common operations
numbers.push(6)                 // Add to end
let last = numbers.pop()        // Remove from end
let length = numbers.len()      // Count
let found = numbers.has(3)      // Contains?
let index = numbers.find(3)     // Index or -1

// Functional operations
let doubled = numbers.map(x => x * 2)
let evens = numbers.filter(x => x % 2 == 0)
let sum = numbers.reduce(0, (acc, x) => acc + x)
```

### Map[Text, V]

```viper
// Creation
let ages: Map[Text, Integer] = Map()
ages.set("Alice", 30)
ages.set("Bob", 25)

// Access (panics if missing)
let aliceAge = ages["Alice"]    // 30
let missing = ages["Eve"]       // panic!

// Safe access
match ages.get("Alice") {
    Some(age) => print(age)
    None => print("Not found")
}

// With default
let age = ages.getOr("Charlie", 0)

// Operations
let exists = ages.has("Alice")
ages.drop("Bob")                // Remove
let keys = ages.keys()          // List[Text]
let values = ages.values()      // List[Integer]
```

### Set[T]

```viper
let tags: Set[Text] = Set()
tags.put("viper")
tags.put("language")
tags.put("viper")               // No duplicate

let hasTags = tags.has("viper") // true
let count = tags.len()          // 2

// Set operations
let other: Set[Text] = Set()
other.put("language")
other.put("design")

let union = tags.merge(other)       // All from both
let intersection = tags.common(other) // In both
let difference = tags.diff(other)    // In tags but not other
```

### Indexing Rules

| Syntax | Behavior | Use When |
|--------|----------|----------|
| `list[i]` | Panics if out of bounds | You're certain index is valid |
| `list.get(i)` | Returns `Option[T]` | Index might be invalid |
| `map[key]` | Panics if key missing | You're certain key exists |
| `map.get(key)` | Returns `Option[V]` | Key might be missing |

This follows the principle: panics for programmer errors, Options for expected cases.

---

## Standard Library Mapping

### Viper.* Runtime Integration

ViperLang code maps to the Viper runtime library:

| Language Construct | Runtime Type/Function |
|-------------------|----------------------|
| `Text` methods | `Viper.String.*` |
| `List[T]` | `Viper.Collections.Seq` |
| `Map[Text, V]` | `Viper.Collections.Map` |
| `Set[Text]` | `Viper.Collections.Bag` |
| `print(x)` | `Viper.Terminal.PrintStr` |
| `readLine()` | `Viper.Terminal.ReadLine` |
| Math functions | `Viper.Math.*` |
| File I/O | `Viper.IO.*` |

### Naming Convention Alignment

ViperLang uses short, distinctive names that match the runtime:

| Method | Meaning | Rationale |
|--------|---------|-----------|
| `len()` | Length/count | Short, universal |
| `has(x)` | Contains x? | Reads naturally |
| `get(i)` | Safe access | Standard |
| `set(i, v)` | Update at index | Standard |
| `put(x)` | Add to collection | Distinct from `set` |
| `drop(x)` | Remove from collection | Distinct from `remove` |
| `push(x)` | Add to end | Stack terminology |
| `pop()` | Remove from end | Stack terminology |

### Direct Runtime Access

For advanced use cases, runtime functions can be called directly:

```viper
import Viper.Math
import Viper.IO.File
import Viper.Text.Codec

let angle = Viper.Math.sin(0.5)
let encoded = Viper.Text.Codec.base64Enc("Hello")
let contents = Viper.IO.File.read("data.txt")
```

---

## Complete Grammar

```ebnf
// Program Structure
program      = module import* declaration*
module       = "module" path
import       = "import" path ["as" name]

// Declarations
declaration  = value_decl | entity_decl | interface_decl | func_decl
value_decl   = "value" name [generics] "{" field* "}" 
             | "value" name [generics] "=" variant ("|" variant)*
entity_decl  = "entity" name [generics] [extends] [implements] "{" member* "}"
interface_decl = "interface" name [generics] "{" method_sig* "}"
func_decl    = [visibility] ["async"] "func" name [generics] params ["->" type] block

// Sum Type Variants
variant      = name ["(" field_list ")"]

// Members
field        = [visibility] ["weak"] name ":" type ["=" expr]
member       = field | method
method       = [visibility] ["override"] ["async"] "func" name [generics] params ["->" type] block?
method_sig   = "func" name [generics] params ["->" type] [block]
params       = "(" [param ("," param)*] ")"
param        = name ":" type ["=" expr]
visibility   = "hide" | "expose"

// Types
type         = name [generics] | type "?" | "weak" type | "(" type ")"
generics     = "[" type ("," type)* "]"
extends      = "extends" type
implements   = "implements" type ("," type)*

// Statements
statement    = let_stmt | var_stmt | assign | if_stmt | match_stmt 
             | while_stmt | for_stmt | return_stmt | guard_stmt | expr_stmt
let_stmt     = "let" pattern [":" type] "=" expr
var_stmt     = "var" name [":" type] "=" expr
assign       = target "=" expr
if_stmt      = "if" expr block ["else" (if_stmt | block)]
             | "if" "let" pattern "=" expr block ["else" block]
match_stmt   = "match" expr "{" case+ "}"
while_stmt   = "while" expr block
             | "while" "let" pattern "=" expr block
for_stmt     = "for" pattern "in" expr block
return_stmt  = "return" [expr]
guard_stmt   = "guard" expr "else" block
expr_stmt    = expr

// Expressions
expr         = literal | name | binary | unary | call | member_access 
             | index | lambda | match_expr | construct | list_literal
             | range | optional_chain | null_coalesce | type_check | type_cast
literal      = integer | number | text | "true" | "false" | "null"
binary       = expr op expr
unary        = op expr | expr "?"
call         = expr "(" [arg ("," arg)*] ")"
arg          = [name ":"] expr
member_access = expr "." name
index        = expr "[" expr "]"
lambda       = "(" [param ("," param)*] ")" "->" (expr | block)
             | name "=>" expr
match_expr   = "match" expr "{" case+ "}"
construct    = name "(" [field_init ("," field_init)*] ")"
field_init   = name ":" expr
list_literal = "[" [expr ("," expr)*] "]"
range        = expr ".." expr | expr "..=" expr
optional_chain = expr "?." name | expr "?[" expr "]"
null_coalesce = expr "??" expr
type_check   = expr "is" type
type_cast    = expr "as" type

// Patterns
pattern      = literal | name | "_" | tuple_pattern | constructor_pattern
tuple_pattern = "(" pattern ("," pattern)+ ")"
constructor_pattern = name "(" [pattern ("," pattern)*] ")"
case         = pattern ["where" expr] "=>" (expr | block)

// Operators (by precedence, lowest to highest)
op           = "||"                          // Logical OR
             | "&&"                          // Logical AND
             | "==" | "!=" | "<" | ">" | "<=" | ">="  // Comparison
             | "+" | "-"                     // Addition
             | "*" | "/" | "%"               // Multiplication
             | "!" | "-"                     // Unary

// Basics
block        = "{" statement* "}"
path         = name ("." name)*
name         = letter (letter | digit | "_")*
integer      = digit+ | "0x" hex+ | "0b" bin+
number       = digit+ "." digit+ [("e" | "E") ["-"] digit+]
text         = '"' char* '"' | '"""' multiline_char* '"""'
```

---

## Keywords

ViperLang has exactly **25 keywords**:

| Category | Keywords |
|----------|----------|
| Types | `value` `entity` `interface` `weak` |
| Variables | `let` `var` |
| Functions | `func` `return` `async` `await` |
| Control | `if` `else` `match` `while` `for` `in` `guard` `break` `continue` |
| OOP | `extends` `implements` `override` `super` |
| Visibility | `expose` `hide` |
| Modules | `module` `import` |

**Reserved for future use:** `as`, `is` (currently operators)

---

## Operators

### By Precedence (lowest to highest)

| Precedence | Operators | Associativity | Description |
|------------|-----------|---------------|-------------|
| 1 | `??` | Right | Null coalescing |
| 2 | `\|\|` | Left | Logical OR |
| 3 | `&&` | Left | Logical AND |
| 4 | `==` `!=` | Left | Equality |
| 5 | `<` `>` `<=` `>=` | Left | Comparison |
| 6 | `is` `as` | Left | Type check/cast |
| 7 | `+` `-` | Left | Addition |
| 8 | `*` `/` `%` | Left | Multiplication |
| 9 | `!` `-` (unary) | Right | Unary |
| 10 | `?` | Postfix | Propagation |
| 11 | `.` `?.` `[]` `?[]` `()` | Left | Access/call |

### Operator Summary

| Operator | Description | Example |
|----------|-------------|---------|
| `+` `-` `*` `/` `%` | Arithmetic | `a + b` |
| `==` `!=` `<` `>` `<=` `>=` | Comparison | `a < b` |
| `&&` `\|\|` `!` | Logical | `a && b` |
| `?.` | Optional chaining | `user?.name` |
| `??` | Null coalescing | `name ?? "default"` |
| `?` | Propagation | `getValue()?` |
| `is` | Type check | `x is Integer` |
| `as` | Type cast | `x as Text` |
| `..` | Half-open range | `0..10` |
| `..=` | Inclusive range | `0..=10` |

---

## Design Decisions

### Frozen for v0.1

These decisions are final for v0.1:

1. **Two type kinds:** `value` (copy) and `entity` (reference)
2. **No exceptions:** All errors via `Result[T]`
3. **? operator:** Works for both `Result` and `Option`
4. **T? syntax:** Sugar for `Option[T]`, `null` for `None`
5. **Indexing:** `[i]` panics, `.get(i)` returns `Option`
6. **Ranges:** `..` half-open, `..=` inclusive
7. **Visibility:** Private by default, `expose` for public
8. **Single inheritance:** Entities only, `override` required
9. **Default methods:** Interface defaults can only use interface members
10. **Temporaries:** Cannot mutate temporaries or accessor returns

### Deferred to v0.2

These features are planned but not in v0.1:

1. **Async/await and Task[T]** — Full concurrency model
2. **Channels** — Inter-task communication
3. **Generic constraints** — Type bounds like `[T: Comparable]`
4. **Pattern matching in parameters** — `func f(Point(x, y): Point)`
5. **Decimal type** — Arbitrary-precision decimal

### Rationale Notes

**Why no exceptions?**
- Exceptions hide control flow
- Errors as values are explicit and composable
- The `?` operator provides ergonomic propagation

**Why `value` and `entity` keywords?**
- Clear semantic distinction (copy vs. reference)
- More intuitive than `struct`/`class`
- Matches how developers think about data

**Why single inheritance only?**
- Avoids diamond problem complexity
- Interfaces provide multiple-conformance
- Composition over inheritance

**Why `expose` instead of `public`?**
- Distinct from other languages (intentional)
- Reads as an action: "expose this to importers"
- Matches the private-by-default philosophy

---

## Testing

Built-in testing with simple syntax:

```viper
module UserTests

import Viper.Test

test "user creation" {
    let user = User(name: "Alice", email: "alice@example.com")
    assert user.name == "Alice"
    assert user.email == "alice@example.com"
}

test "division by zero returns error" {
    match divide(10, 0) {
        Ok(_) => fail("Expected error")
        Err(e) => assert e.code == "DIV_ZERO"
    }
}

test "list operations" {
    let list = [1, 2, 3]
    assert list.len() == 3
    assert list.has(2)
    assert !list.has(4)
}
```

Run tests with:

```bash
viper test                    # Run all tests
viper test UserTests          # Run specific module
viper test --filter "user"    # Filter by name
```

---

## Tooling

The formatter, linter, and test runner are **part of the language specification**:

```bash
viper fmt              # Format code (enforced style)
viper fmt --check      # Check without modifying

viper lint             # Check for common issues
viper lint --fix       # Auto-fix where possible

viper test             # Run tests
viper test --coverage  # With coverage report

viper build            # Compile project
viper build --release  # Optimized build

viper run              # Build and run
viper run --vm         # Run in VM (default)
viper run --native     # Run native binary
```

### Formatting Rules (Enforced)

- 4-space indentation
- Opening braces on same line
- Semicolons added by formatter (optional in source)
- Maximum line length: 100 characters
- Trailing commas in multi-line constructs

---

## Appendix A: Complete Example

```viper
module TodoApp

import Viper.IO.File
import Viper.Text.Codec

// Domain types
value TodoId {
    raw: Text
}

value Todo {
    id: TodoId
    title: Text
    done: Boolean
}

// Storage interface
interface TodoStore {
    func save(todo: Todo) -> Result[()]
    func load(id: TodoId) -> Result[Option[Todo]]
    func all() -> Result[List[Todo]]
    func delete(id: TodoId) -> Result[()>
}

// File-based implementation
entity FileTodoStore implements TodoStore {
    path: Text
    
    expose func save(todo: Todo) -> Result[()]> {
        let json = todoToJson(todo)
        let filename = "${path}/${todo.id.raw}.json"
        File.write(filename, json)?
        return Ok(())
    }
    
    expose func load(id: TodoId) -> Result[Option[Todo]] {
        let filename = "${path}/${id.raw}.json"
        if !File.exists(filename) {
            return Ok(None)
        }
        let json = File.read(filename)?
        let todo = todoFromJson(json)?
        return Ok(Some(todo))
    }
    
    expose func all() -> Result[List[Todo]] {
        let files = Dir.files(path)?
        let todos: List[Todo] = []
        for file in files {
            if file.endsWith(".json") {
                let content = File.read("${path}/${file}")?
                let todo = todoFromJson(content)?
                todos.push(todo)
            }
        }
        return Ok(todos)
    }
    
    expose func delete(id: TodoId) -> Result[()]> {
        let filename = "${path}/${id.raw}.json"
        File.delete(filename)?
        return Ok(())
    }
    
    // Private helpers
    func todoToJson(todo: Todo) -> Text {
        return "{\"id\":\"${todo.id.raw}\",\"title\":\"${todo.title}\",\"done\":${todo.done}}"
    }
    
    func todoFromJson(json: Text) -> Result[Todo] {
        // Simplified parsing for example
        // Real implementation would use Viper.Json
        return Ok(Todo(
            id: TodoId(raw: "extracted-id"),
            title: "extracted-title",
            done: false
        ))
    }
}

// Application entry point
func main() {
    let store = FileTodoStore(path: "./todos")
    
    // Create a todo
    let todo = Todo(
        id: TodoId(raw: "1"),
        title: "Learn ViperLang",
        done: false
    )
    
    match store.save(todo) {
        Ok(()) => print("Saved!")
        Err(e) => print("Error: ${e.message}")
    }
    
    // List all todos
    match store.all() {
        Ok(todos) => {
            print("Todos:")
            for t in todos {
                let status = t.done ? "✓" : " "
                print("[${status}] ${t.title}")
            }
        }
        Err(e) => print("Error: ${e.message}")
    }
}
```

---

## Appendix B: Migration from Other Languages

### From Java/C#

| Java/C# | ViperLang | Notes |
|---------|-----------|-------|
| `class` | `entity` | Reference type |
| `struct` (C#) | `value` | Copy type |
| `interface` | `interface` | Same concept |
| `public` | `expose` | Explicit visibility |
| `private` | default | Private by default |
| `try/catch` | `Result[T]` + `?` | Errors as values |
| `null` | `T?` / `Option[T]` | Explicit optionals |
| `List<T>` | `List[T]` | Square brackets |

### From Rust

| Rust | ViperLang | Notes |
|------|-----------|-------|
| `struct` | `value` | Copy semantics |
| `struct` + heap | `entity` | Reference counted |
| `trait` | `interface` | With default methods |
| `impl` | Inside type | Methods in type body |
| `Result<T, E>` | `Result[T]` | Simplified error type |
| `Option<T>` | `Option[T]` / `T?` | Same concept |
| `?` operator | `?` operator | Same semantics |
| `match` | `match` | Same concept |

### From Go

| Go | ViperLang | Notes |
|----|-----------|-------|
| `struct` | `value` | Copy semantics |
| `*T` pointer | `entity` | No pointer syntax |
| `interface{}` | `interface` | Nominal, not structural |
| `error` return | `Result[T]` | Type-safe errors |
| `if err != nil` | `?` operator | More concise |
| No generics* | Generics | Full generics support |

---

**Status:** v0.1 Final Specification

**Version History:**
- v0.1 Final — Type alias clarification, runtime alignment, async deferred to v0.2
- v0.1 RC1 — Initial release candidate

**© 2024-2025 ViperLang Project**
