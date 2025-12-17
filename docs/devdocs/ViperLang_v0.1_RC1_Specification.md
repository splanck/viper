# ViperLang — Language Specification

## Version 0.1 RC1 (Release Candidate 1)

<div align="center">

**Status: Release Candidate - Seeking Final Feedback**

*A modern language that's actually simple*

</div>

---

## Philosophy

ViperLang aims for five core principles:

1. **Truly Simple** — The entire language fits in your head
2. **One Way** — There's an obvious way to do things
3. **Modern First** — Async, pattern matching, and null safety by default
4. **Fast by Default** — Zero-cost abstractions, no hidden allocations
5. **Amazing Tools** — Formatter, linter, and test runner are part of the language

---

## Table of Contents

1. [Core Concepts](#core-concepts) — 10 minutes to understand ViperLang
2. [Types](#types) — Values, entities, and interfaces
3. [Control Flow](#control-flow) — Pattern matching and conditionals
4. [Error Handling](#error-handling) — Result types, no exceptions
5. [Concurrency](#concurrency) — Async/await and channels
6. [Memory Management](#memory-management) — Reference counting with cycle detection
7. [Modules](#modules) — Organization and visibility
8. [Standard Library](#standard-library) — Batteries included
9. [Complete Grammar](#complete-grammar) — The entire language

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

// Async is built-in
async func main() {
    let user = User(name: "Alice", email: "alice@example.com")
    print(user.greet())
}
```

**Key points:**

- Semicolons optional (formatter adds them as needed)
- Types are non-nullable by default
- String interpolation with `${}`
- Memory managed automatically (reference counting)

---

## Types

### Values (Copy Types)

Values are **copy types**. They copy when assigned or passed. Mutating a variable that holds a value affects only that
copy:

```viper
value Color {
    r: Integer  // 0-255
    g: Integer
    b: Integer
}

let red = Color(r: 255, g: 0, b: 0)
var myColor = red  // Copies the value
myColor.g = 128    // Only affects myColor; red unchanged
```

**Rule:** Cannot mutate temporaries or accessor returns (e.g., `getColor().r = 10` is illegal).

Values are ideal for:

- Small data (points, colors, dates)
- Domain values (money, IDs, coordinates)
- Data without identity

### Entities (Reference Types)

Entities are mutable objects with identity:

```viper
entity Account {
    hide id: Text    // Private field
    balance: Decimal
    
    func deposit(amount: Decimal) -> Result[Void] {
        if amount <= 0 {
            return Error("Amount must be positive")
        }
        balance = balance + amount
        return Ok()
    }
}

let account1 = Account(balance: 100)
let account2 = account1  // Same object (reference)
account2.deposit(50)?    // Both see balance of 150
```

Entities are ideal for:

- Stateful objects
- Resources (files, connections)
- Domain entities with identity

### Simple Inheritance

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
    
    override func speak() -> Text {  // 'override' required
        return "Woof!"
    }
    
    func describe() -> Text {
        return "${name} is a ${breed}"
    }
}
```

**Rules:**

- Only entities can inherit (not values)
- Single inheritance only
- `override` keyword required when overriding
- `super.f()` only valid inside an override of `f`
- Private fields (`hide`) are not inherited

### Interfaces

Interfaces define contracts that types must fulfill:

```viper
interface Drawable {
    func draw(canvas: Canvas)
    
    // Default implementation
    func drawTwice(canvas: Canvas) {
        draw(canvas)
        draw(canvas)
    }
}

entity Button implements Drawable {
    label: Text
    position: Point
    
    func draw(canvas: Canvas) {
        canvas.drawRect(position)
        canvas.drawText(label, position)
    }
}
```

**Rule:** Default methods may only use interface-declared members.

Both values and entities can implement interfaces.

### Optionals

**T? is syntactic sugar for Option[T]**:

- `null` is syntactic sugar for `None`
- Assigning `T` to `T?` automatically wraps in `Some(T)`
- The `?` operator propagates both `Error` (from Result) and `None` (from Option)

```viper
let maybe: Text? = null      // Same as: Option[Text] = None
maybe = "Hello"               // Same as: Some("Hello")

// Pattern matching (preferred)
match maybe {
    Some(text) => print(text)
    None => print("Nothing")
}

// If-let sugar
if let text = maybe {
    print(text)  // text is Text, not Text?
}

// Optional chaining and null coalescing
let length = user?.address?.street?.length()  // Returns Integer?
let name = user?.name ?? "Anonymous"          // Returns Text

// ? operator works with Option too
func findName(id: Text) -> Option[Text] {
    let user = findUser(id)?  // Returns None if findUser returns None
    return Some(user.name)
}
```

### Generics

Types and functions can be generic:

```viper
// Option is built-in, but defined like:
value Option[T] = Some(T) | None

entity List[T] {
    hide items: Array[T]
    
    func add(item: T) {
        items.append(item)
    }
    
    func get(index: Integer) -> Option[T] {
        if index >= 0 && index < items.length {
            return Some(items[index])
        }
        return None
    }
}
```

---

## Control Flow

### Pattern Matching

Pattern matching is the primary control flow mechanism:

```viper
value Result[T] = Ok(T) | Error(Text)

func divide(a: Number, b: Number) -> Result[Number] {
    if b == 0 {
        return Error("Division by zero")
    }
    return Ok(a / b)
}

// Match expressions return values
let message = match divide(10, 2) {
    Ok(value) => "Result: ${value}"
    Error(msg) => "Error: ${msg}"
}

// Destructuring
let Point(x, y) = getPoint()
let (first, rest) = list.split()
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

// Conditional expressions
let status = isOnline ? "Connected" : "Offline"

// If-let for optionals
if let user = findUser(id) {
    print(user.name)  // user is User, not User?
}

// While-let
while let message = channel.receive() {
    process(message)
}

// Guard for early returns
func process(data: Data?) -> Text {
    guard data != null else {
        return "No data"
    }
    
    guard data.isValid() else {
        return "Invalid data"
    }
    
    return processValid(data)
}
```

### Loops

```viper
// For each
for item in collection {
    process(item)
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

// Range (half-open: includes start, excludes end)
for i in 0..10 {
    print(i)  // 0 through 9
}

// Inclusive range
for i in 0..=10 {
    print(i)  // 0 through 10
}
```

---

## Error Handling

**No exceptions!** Errors are just values. Use `Result[T]` for operations that can fail:

```viper
value Result[T] = Ok(T) | Error(ErrorInfo)

value ErrorInfo {
    code: Text
    message: Text
    details: Map[Text, Any]?
}

func readFile(path: Text) -> Result[Text] {
    if !exists(path) {
        return Error(ErrorInfo(
            code: "NOT_FOUND",
            message: "File not found: ${path}"
        ))
    }
    
    // Read file...
    return Ok(contents)
}

// Handle with pattern matching
match readFile("data.txt") {
    Ok(content) => process(content)
    Error(e) => print("Failed: ${e.message}")
}
```

### Propagating Errors

The `?` operator works for both Result and Option:

```viper
// With Result: propagates Error
func processFile(path: Text) -> Result[Data] {
    let content = readFile(path)?     // Returns Error if failed
    let parsed = parseData(content)?  // Returns Error if failed
    let validated = validate(parsed)? // Returns Error if failed
    return Ok(validated)
}

// With Option: propagates None
func getUserEmail(id: Text) -> Option[Text] {
    let user = findUser(id)?    // Returns None if not found
    let profile = user.profile?  // Returns None if no profile
    return Some(profile.email)
}
```

### Panics

For truly unrecoverable errors (programmer errors, violated invariants):

```viper
func getUser(id: Text) -> User {
    match database.find(id) {
        Some(user) => user
        None => panic("User ${id} must exist")  // Crashes program
    }
}
```

**Rule:** Use `Result` for expected failures, `panic` for impossible states.

---

## Concurrency

### Tasks and Async/Await

`async { }` creates a `Task[T]`. Tasks are futures that can be awaited:

```viper
async func fetchUser(id: Text) -> User {
    let response = await http.get("/users/${id}")
    return User.fromJson(response.body)
}

async func main() {
    // Sequential
    let user = await fetchUser("123")
    let posts = await fetchPosts(user.id)
    
    // Parallel with all() - fails fast on first error
    let [user, config] = await all([
        fetchUser("123"),
        fetchConfig()
    ])
    
    // Create task directly
    let task: Task[User] = async {
        return fetchUser("123")
    }
    
    let user = await task
}
```

**Task Rules:**

- `await` re-raises task errors (preserves stack trace)
- `all([...])` fails fast: first error cancels remaining tasks
- Returns results in input order
- Cancellation is cooperative: tasks check `isCancelled()`

### Channels

Channels provide typed communication between tasks:

```viper
let channel = Channel[Message](capacity: 10)

// Producer
async {
    for i in 0..100 {
        match channel.send(Message(i)) {
            Ok => continue
            Error(Closed) => break
        }
    }
    channel.close()
}

// Consumer
async {
    while let msg = channel.receive() {  // Returns None when closed
        process(msg)
    }
}
```

**Channel Rules:**

- `receive()` returns `Option[T]` (None after close and drain)
- `send()` returns `Result[Void]` (Error(Closed) if closed)
- Bounded channels apply backpressure (block on send when full)
- `close()` is idempotent

### Structured Concurrency

Tasks have parent-child relationships:

```viper
async func processItems(items: List[Item]) -> List[Result] {
    let tasks = []
    
    for item in items {
        // Child tasks
        tasks.add(async {
            return processItem(item)
        })
    }
    
    // Wait for all children
    return await all(tasks)
}

// Cancelling parent cancels children
let task = async { processItems(items) }
if timeout {
    task.cancel()  // All child tasks cancelled
}
```

---

## Memory Management

### Reference Counting

ViperLang uses **automatic reference counting** with cycle detection:

- Values: Stack-allocated when possible, no refcounting needed
- Entities: Reference counted, deallocated when count reaches zero
- Cycles: Detected and collected periodically

### Weak References

Break cycles with weak references:

```viper
entity Node {
    value: Any
    children: List[Node]
    weak parent: Node?  // Weak reference breaks cycle
}

entity Delegate {
    weak owner: Controller?  // Prevent retain cycles
}

// Weak references return optionals
if let parent = node.parent {
    parent.updateChild(node)
}
```

**Rules:**

- Weak references don't increase refcount
- Reading weak references returns `T?`
- Automatically cleared when target deallocated

---

## Modules

### Module System

Every file declares its module:

```viper
module MyApp.Services.UserService

import MyApp.Models.User
import MyApp.Data.Database as DB
import Viper.Http

// Module contents...
```

### Visibility

**Default: private**. Use `expose` for public API:

```viper
entity UserService {
    // Private by default
    database: DB.Connection
    
    // Explicitly public
    expose func getUser(id: Text) -> User {
        return database.find(id)
    }
    
    // Private helper
    func validateId(id: Text) {
        // ...
    }
}
```

**Rule:** Everything is private unless marked `expose`. This prevents accidental API leaks.

---

## Collections and Indexing

### Indexing Rules

- `collection[i]` — **Panics** if out of bounds (programmer error)
- `collection.get(i)` — Returns `Option[T]` (safe access)

```viper
let list = [1, 2, 3]

// Fast path - panics if out of bounds
let first = list[0]  // 1
let bad = list[10]   // panic!

// Safe path - returns Option
match list.get(10) {
    Some(val) => print(val)
    None => print("No element at index 10")
}
```

This matches the philosophy: panics for programmer errors, Result/Option for expected failures.

---

## Standard Library

Built-in modules for modern development:

```viper
import Viper.Http      // HTTP client/server
import Viper.Json      // JSON parsing
import Viper.Test      // Testing framework
import Viper.Async     // Async utilities
import Viper.IO        // File I/O
import Viper.Crypto    // Cryptography
import Viper.Time      // Date/time handling
import Viper.Process   // Run external commands
```

Legacy protocols (XML, SOAP, etc.) are available as separate packages, not in core.

---

## Complete Grammar

The entire ViperLang grammar:

```ebnf
// Program Structure
module = "module" path
import = "import" path ["as" name]
program = module import* declaration*

// Declarations
declaration = value_decl | entity_decl | interface_decl | func_decl
value_decl = "value" name [generics] "{" field* "}" | sum_type
entity_decl = "entity" name [generics] [extends] [implements] "{" member* "}"
interface_decl = "interface" name [generics] [extends] "{" method* "}"
sum_type = "value" name [generics] "=" variant ("|" variant)*
variant = name ["(" field_list ")"]

// Members
field = [visibility] name ":" type
member = field | method
method = [visibility] ["override"] ["async"] "func" name [generics] params ["->" type] block?
params = "(" [param ("," param)*] ")"
param = name ":" type ["=" expr]
visibility = "hide" | "expose"

// Types
type = name [generics] | type "?" | "weak" type
generics = "[" type ("," type)* "]"
extends = "extends" type
implements = "implements" type ("," type)*

// Statements
statement = let | var | assign | if | if_let | while | while_let | for | match | return | guard | expr
let = "let" pattern "=" expr
var = "var" name [":" type] "=" expr
assign = lvalue "=" expr
if = "if" expr block ["else" (if | block)]
if_let = "if" "let" pattern "=" expr block ["else" block]
while = "while" expr block
while_let = "while" "let" pattern "=" expr block
for = "for" pattern "in" expr block
match = "match" expr "{" case+ "}"
return = "return" expr?
guard = "guard" expr "else" block

// Expressions
expr = literal | name | binary | unary | call | field_access | lambda | 
       match_expr | async_block | range | optional_chain | null_coalesce |
       type_check | type_cast | index
literal = number | text | boolean | "null"
binary = expr op expr
unary = op expr
call = expr "(" [arg ("," arg)*] ")"
arg = [name ":"] expr
field_access = expr "." name
index = expr "[" expr "]"
lambda = "(" params ")" "->" (expr | block)
match_expr = "match" expr "{" case+ "}"
async_block = "async" block
range = expr ".." expr | expr "..=" expr
optional_chain = expr "?." name
null_coalesce = expr "??" expr
type_check = expr "is" type
type_cast = expr "as" type

// Patterns
pattern = literal | name | tuple | constructor | "_"
tuple = "(" pattern ("," pattern)* ")"
constructor = name ["(" pattern ("," pattern)* ")"]
case = pattern ["where" expr] "=>" (expr | block)

// Operators
op = "+" | "-" | "*" | "/" | "%" | "==" | "!=" | "<" | ">" | "<=" | ">=" |
     "&&" | "||" | "!" | "?" | "is" | "as" | "?." | "??"

// Basics
block = "{" statement* "}"
path = name ("." name)*
name = letter (letter | digit | "_")*
```

---

## Key Design Decisions

### Frozen for v0.1

1. **Indexing:** `list[i]` panics; `list.get(i)` returns `Option[T]`
2. **Propagation:** `?` works for both Result and Option
3. **Ranges:** `..` is half-open, `..=` is inclusive
4. **Async:** `async { }` creates Task[T]; `await` joins
5. **Temporaries:** Cannot mutate temporaries or accessor returns
6. **Interface defaults:** Can only call interface-declared members

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

test "async operations" async {
    let result = await fetchData()
    assert result != null
}

test "error cases" {
    match divide(10, 0) {
        Ok(_) => fail("Should have failed")
        Error(e) => assert e.code == "DIVISION_BY_ZERO"
    }
}
```

---

## Tooling

The formatter, linter, and test runner are **part of the language spec**:

```bash
viper fmt     # Format code (enforced style)
viper lint    # Check for common issues
viper test    # Run tests
viper build   # Compile project
viper run     # Build and run
```

---

**Status:** Release Candidate 1 - Final feedback welcome before v0.1 freeze.

**© 2024 ViperLang Contributors** | [GitHub](https://github.com/viperlang) | [Discord](https://discord.gg/viperlang)
