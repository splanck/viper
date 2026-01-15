# Zia — Language Specification

## Version 0.1 RC1 (Release Candidate 1)

<div align="center">

**Status: Release Candidate - Seeking Final Feedback**

*A modern language that's actually simple*

</div>

---

## Philosophy

Zia aims for five core principles:

1. **Truly Simple** — The entire language fits in your head
2. **One Way** — There's an obvious way to do things
3. **Modern First** — Async, pattern matching, and null safety by default
4. **Fast by Default** — Zero-cost abstractions, no hidden allocations
5. **Amazing Tools** — Formatter, linter, and test runner are part of the language

---

## Table of Contents

1. [Core Concepts](#core-concepts) — 10 minutes to understand Zia
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

Zia programs are made of **modules** containing **types** and **functions**.

```viper
module HelloWorld;

// Two kinds of types: values (copied) and entities (referenced)
value Point {
    Number x;
    Number y;
}

entity User {
    Text name;
    Text email;

    func greet() -> Text {
        return "Hello, ${name}!";
    }
}

// Async is built-in
async func main() {
    User user = new User(name: "Alice", email: "alice@example.com");
    Viper.Terminal.Say(user.greet());
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
    Integer r;  // 0-255
    Integer g;
    Integer b;
}

Color red = Color(r: 255, g: 0, b: 0);
Color myColor = red;  // Copies the value
myColor.g = 128;      // Only affects myColor; red unchanged
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
    hide Text id;    // Private field
    Decimal balance;

    func deposit(amount: Decimal) -> Result[Void] {
        if amount <= 0 {
            return Error("Amount must be positive");
        }
        balance = balance + amount;
        return Ok();
    }
}

Account account1 = new Account(balance: 100);
Account account2 = account1;  // Same object (reference)
account2.deposit(50)?;        // Both see balance of 150
```

Entities are ideal for:

- Stateful objects
- Resources (files, connections)
- Domain entities with identity

### Simple Inheritance

Entities support single inheritance:

```viper
entity Animal {
    Text name;

    func speak() -> Text {
        return "...";
    }
}

entity Dog extends Animal {
    Text breed;

    override func speak() -> Text {  // 'override' required
        return "Woof!";
    }

    func describe() -> Text {
        return "${name} is a ${breed}";
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
    func draw(canvas: Canvas);

    // Default implementation
    func drawTwice(canvas: Canvas) {
        draw(canvas);
        draw(canvas);
    }
}

entity Button implements Drawable {
    Text label;
    Point position;

    func draw(canvas: Canvas) {
        canvas.drawRect(position);
        canvas.drawText(label, position);
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
Text? maybe = null;           // Same as: Option[Text] = None
maybe = "Hello";              // Same as: Some("Hello")

// Pattern matching (preferred)
match maybe {
    Some(text) => Viper.Terminal.Say(text);
    None => Viper.Terminal.Say("Nothing");
}

// If-let sugar (future feature)
if let text = maybe {
    Viper.Terminal.Say(text);  // text is Text, not Text?
}

// Optional chaining and null coalescing
Integer? length = user?.address?.street?.length();  // Returns Integer?
Text name = user?.name ?? "Anonymous";              // Returns Text

// ? operator works with Option too
func findName(id: Text) -> Option[Text] {
    User? user = findUser(id)?;  // Returns None if findUser returns None
    return Some(user.name);
}
```

### Generics

Types and functions can be generic:

```viper
// Option is built-in, but defined like:
value Option[T] = Some(T) | None

entity List[T] {
    hide Array[T] items;

    func add(item: T) {
        items.append(item);
    }

    func get(index: Integer) -> Option[T] {
        if index >= 0 && index < items.length {
            return Some(items[index]);
        }
        return None;
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
        return Error("Division by zero");
    }
    return Ok(a / b);
}

// Match expressions return values
Text message = match divide(10, 2) {
    Ok(value) => "Result: ${value}";
    Error(msg) => "Error: ${msg}";
};

// Destructuring
Point(x, y) = getPoint();
(first, rest) = list.split();
```

### Conditionals

```viper
// Standard if/else
if temperature > 30 {
    turnOnAC();
} else if temperature < 10 {
    turnOnHeater();
} else {
    maintainTemperature();
}

// Conditional expressions
Text status = isOnline ? "Connected" : "Offline";

// If-let for optionals (future feature)
if let user = findUser(id) {
    Viper.Terminal.Say(user.name);  // user is User, not User?
}

// While-let (future feature)
while let message = channel.receive() {
    process(message);
}

// Guard for early returns
func process(data: Data?) -> Text {
    guard data != null else {
        return "No data";
    }

    guard data.isValid() else {
        return "Invalid data";
    }

    return processValid(data);
}
```

### Loops

```viper
// For each
for item in collection {
    process(item);
}

// While
while hasMore() {
    Item item = getNext();
    if shouldSkip(item) {
        continue;
    }
    if isDone() {
        break;
    }
    handle(item);
}

// Range (half-open: includes start, excludes end)
for i in 0..10 {
    Viper.Terminal.SayInt(i);  // 0 through 9
}

// Inclusive range
for i in 0..=10 {
    Viper.Terminal.SayInt(i);  // 0 through 10
}
```

---

## Error Handling

**No exceptions!** Errors are just values. Use `Result[T]` for operations that can fail:

```viper
value Result[T] = Ok(T) | Error(ErrorInfo)

value ErrorInfo {
    Text code;
    Text message;
    Map[Text, Any]? details;
}

func readFile(path: Text) -> Result[Text] {
    if !Viper.IO.File.Exists(path) {
        return Error(ErrorInfo(
            code: "NOT_FOUND",
            message: "File not found: ${path}"
        ));
    }

    Text contents = Viper.IO.File.ReadAllText(path);
    return Ok(contents);
}

// Handle with pattern matching
match readFile("data.txt") {
    Ok(content) => process(content);
    Error(e) => Viper.Terminal.Say("Failed: ${e.message}");
}
```

### Propagating Errors

The `?` operator works for both Result and Option:

```viper
// With Result: propagates Error
func processFile(path: Text) -> Result[Data] {
    Text content = readFile(path)?;     // Returns Error if failed
    Data parsed = parseData(content)?;  // Returns Error if failed
    Data validated = validate(parsed)?; // Returns Error if failed
    return Ok(validated);
}

// With Option: propagates None
func getUserEmail(id: Text) -> Option[Text] {
    User? user = findUser(id)?;     // Returns None if not found
    Profile? profile = user.profile?; // Returns None if no profile
    return Some(profile.email);
}
```

### Panics

For truly unrecoverable errors (programmer errors, violated invariants):

```viper
func getUser(id: Text) -> User {
    match database.find(id) {
        Some(user) => user;
        None => panic("User ${id} must exist");  // Crashes program
    }
}
```

**Rule:** Use `Result` for expected failures, `panic` for impossible states.

---

## Concurrency

### Tasks and Async/Await

> **Note:** Async/await is a planned feature for future versions. The syntax is shown here
> for reference. Currently, Zia programs run synchronously.

`async { }` creates a `Task[T]`. Tasks are futures that can be awaited:

```viper
async func loadData(path: Text) -> Text {
    // Future: async file I/O
    return Viper.IO.File.ReadAllText(path);
}

async func main() {
    // Sequential
    Text data = await loadData("config.txt");
    Text processed = await processData(data);

    // Parallel with all() - fails fast on first error
    [Text config, Text users] = await all([
        loadData("config.txt"),
        loadData("users.txt")
    ]);

    // Create task directly
    Task[Text] task = async {
        return loadData("data.txt");
    };

    Text result = await task;
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
Channel[Message] channel = new Channel[Message](10);

// Producer
async {
    for i in 0..100 {
        match channel.send(new Message(i)) {
            Ok => continue;
            Error(Closed) => break;
        }
    }
    channel.close();
}

// Consumer
async {
    while let msg = channel.receive() {  // Returns None when closed
        process(msg);
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
    List tasks = new List();

    for item in items {
        // Child tasks
        tasks.add(async {
            return processItem(item);
        });
    }

    // Wait for all children
    return await all(tasks);
}

// Cancelling parent cancels children
Task task = async { processItems(items); };
if timeout {
    task.cancel();  // All child tasks cancelled
}
```

---

## Memory Management

### Reference Counting

Zia uses **automatic reference counting** with cycle detection:

- Values: Stack-allocated when possible, no refcounting needed
- Entities: Reference counted, deallocated when count reaches zero
- Cycles: Detected and collected periodically

### Weak References

Break cycles with weak references:

```viper
entity Node {
    Any value;
    List[Node] children;
    weak Node? parent;  // Weak reference breaks cycle
}

entity Delegate {
    weak Controller? owner;  // Prevent retain cycles
}

// Weak references return optionals
if node.parent != null {
    node.parent.updateChild(node);
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
module MyApp.Services.UserService;

bind MyApp.Models.User;
bind MyApp.Data.Database as DB;
bind Viper.IO.File;

// Module contents...
```

### Visibility

**Default: private**. Use `expose` for public API:

```viper
entity UserService {
    // Private by default
    DB.Connection database;

    // Explicitly public
    expose func getUser(id: Text) -> User {
        return database.find(id);
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
List list = new List();
list.add(1);
list.add(2);
list.add(3);

// Get element
Integer first = list.get(0);  // 1

// Safe access
if list.get_Count() > 10 {
    Integer val = list.get(10);
    Viper.Terminal.SayInt(val);
} else {
    Viper.Terminal.Say("No element at index 10");
}
```

This matches the philosophy: panics for programmer errors, Result/Option for expected failures.

---

## Standard Library

Zia uses the Viper.* runtime library for all standard functionality. Built-in modules:

```viper
// Collections
bind Viper.Collections.List    // Dynamic arrays
bind Viper.Collections.Map     // Key-value dictionaries
bind Viper.Collections.Seq     // Versatile sequences
bind Viper.Collections.Stack   // LIFO stack
bind Viper.Collections.Queue   // FIFO queue
bind Viper.Collections.Bag     // String sets
bind Viper.Collections.TreeMap // Ordered maps
bind Viper.Collections.Bytes   // Byte arrays

// I/O
bind Viper.IO.File        // File operations (ReadAllText, WriteAllText, etc.)
bind Viper.IO.Dir         // Directory operations
bind Viper.IO.Path        // Path manipulation
bind Viper.IO.BinFile     // Binary file I/O
bind Viper.IO.LineReader  // Text file reading
bind Viper.IO.LineWriter  // Text file writing

// Terminal & Environment
bind Viper.Terminal       // Console I/O (Print, ReadLine, GetKey, etc.)
bind Viper.Environment    // Args, env vars, exit

// Text & Strings
bind Viper.String         // String operations (Split, Replace, Trim, etc.)
bind Viper.Text.Codec     // Base64, Hex, URL encoding
bind Viper.Text.Csv       // CSV parsing/formatting
bind Viper.Text.StringBuilder // Efficient string building
bind Viper.Text.Guid      // GUID generation
bind Viper.Convert        // Type conversions
bind Viper.Fmt            // Number formatting

// Math & Random
bind Viper.Math           // Math functions (Sin, Cos, Sqrt, etc.)
bind Viper.Random         // Random number generation
bind Viper.Vec2           // 2D vector math
bind Viper.Vec3           // 3D vector math
bind Viper.Bits           // Bit manipulation

// Time & Diagnostics
bind Viper.DateTime       // Date/time operations
bind Viper.Time.Clock     // Timing and sleep
bind Viper.Time.Countdown // Countdown timers
bind Viper.Diagnostics.Stopwatch // Performance timing

// Crypto & Security
bind Viper.Crypto.Hash    // MD5, SHA1, SHA256, CRC32

// System
bind Viper.Exec           // Run external commands
bind Viper.Machine        // System info (OS, CPU, memory)
bind Viper.Log            // Logging

// Threading (when needed)
bind Viper.Threads.Thread  // Thread creation
bind Viper.Threads.Monitor // Synchronization

// Graphics (optional)
bind Viper.Graphics.Canvas // 2D graphics window
bind Viper.Graphics.Color  // Color utilities
bind Viper.Graphics.Pixels // Pixel buffer
```

All standard library access goes through the Viper.* namespace.

---

## Complete Grammar

The entire Zia grammar:

```ebnf
// Program Structure
module = "module" path
bind = "import" path ["as" name]
program = module import* declaration*

// Declarations
declaration = value_decl | entity_decl | interface_decl | func_decl
value_decl = "value" name [generics] "{" field* "}" | sum_type
entity_decl = "entity" name [generics] [extends] [implements] "{" member* "}"
interface_decl = "interface" name [generics] [extends] "{" method* "}"
sum_type = "value" name [generics] "=" variant ("|" variant)*
variant = name ["(" field_list ")"]

// Members
field = [visibility] type name ";"
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
statement = var_decl | assign | if | if_let | while | while_let | for | match | return | guard | expr
var_decl = type name ["=" expr] ";"
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
module UserTests;

bind Viper.Test;

test "user creation" {
    User user = new User(name: "Alice", email: "alice@example.com");
    assert user.name == "Alice";
    assert user.email == "alice@example.com";
}

test "async operations" async {
    Text result = await fetchData();
    assert result != null;
}

test "error cases" {
    match divide(10, 0) {
        Ok(_) => fail("Should have failed");
        Error(e) => assert e.code == "DIVISION_BY_ZERO";
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

**© 2024 Zia Contributors** | [GitHub](https://github.com/zia) | [Discord](https://discord.gg/zia)
