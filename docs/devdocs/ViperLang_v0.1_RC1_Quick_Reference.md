# ViperLang — Quick Reference

## Version 0.1 RC1 (Release Candidate 1)

<div align="center">

**Everything you need in 10 minutes**

*Copy types. Pattern matching. No exceptions. One way to do things.*

</div>

---

## Hello World

```viper
module Hello

async func main() {
    print("Hello, ViperLang!")
}
```

---

## Types

### Values (Copy Types)

```viper
value Point {
    x: Number
    y: Number
}

let p1 = Point(x: 10, y: 20)
var p2 = p1       // Copies the value
p2.x = 30         // Only p2 changes; p1 unchanged
```

**Key:** Values copy on assignment. Cannot mutate temporaries (`getPoint().x = 10` is illegal).

### Entities (Reference Types)

```viper
entity User {
    name: Text
    email: Text
    
    func display() -> Text {
        return "${name} <${email}>"
    }
}

let u1 = User(name: "Alice", email: "alice@example.com")
let u2 = u1       // Same object (reference)
u2.name = "Bob"   // Both u1 and u2 see "Bob"
```

### Inheritance (Entities Only)

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
        return "Woof!"  // super.speak() valid here
    }
}
```

### Interfaces

```viper
interface Drawable {
    func draw(canvas: Canvas)
    
    // Default implementation (can only use interface members)
    func drawTwice(canvas: Canvas) {
        draw(canvas)
        draw(canvas)
    }
}

entity Circle implements Drawable {
    radius: Number
    
    func draw(canvas: Canvas) {
        canvas.drawCircle(radius)
    }
}
```

---

## Variables

```viper
let immutable = 42         // Can't reassign
var mutable = 42           // Can reassign
mutable = 43

let inferred = "Hello"     // Type inferred as Text
let typed: Number = 3.14   // Explicit type

// Destructuring
let Point(x, y) = getPoint()
let (first, second) = getPair()
```

---

## Control Flow

### Pattern Matching

```viper
value Result[T] = Ok(T) | Error(Text)

match divide(10, 2) {
    Ok(value) => print("Result: ${value}")
    Error(msg) => print("Error: ${msg}")
}

// With guards
match number {
    n where n < 0 => "Negative"
    0 => "Zero"
    n where n > 0 => "Positive"
}
```

### If/Else

```viper
if condition {
    doSomething()
} else if otherCondition {
    doOtherThing()
} else {
    doDefault()
}

// Ternary
let result = condition ? "yes" : "no"

// If-let for optionals
if let user = findUser(id) {
    print(user.name)  // user is User, not User?
}
```

### Loops

```viper
// For each
for item in list {
    process(item)
}

// While
while condition {
    doWork()
    if done { break }
    if skip { continue }
}

// While-let
while let msg = channel.receive() {
    process(msg)
}

// Range (half-open)
for i in 0..10 {
    print(i)  // 0 through 9
}

// Range (inclusive)
for i in 0..=10 {
    print(i)  // 0 through 10
}
```

### Guard (Early Return)

```viper
func process(data: Data?) -> Text {
    guard data != null else {
        return "No data"
    }
    
    guard data.isValid() else {
        return "Invalid"
    }
    
    return data.process()
}
```

---

## Functions

```viper
// Basic function
func add(a: Number, b: Number) -> Number {
    return a + b
}

// No return value
func printSum(a: Number, b: Number) {
    print(a + b)
}

// Default parameters
func greet(name: Text = "World") {
    print("Hello, ${name}!")
}

// Generic function
func first[T](list: List[T]) -> Option[T] {
    return list.isEmpty() ? None : Some(list[0])  // list[0] panics if empty!
}

// Lambda
let double = (x: Number) -> x * 2
let result = double(5)  // 10
```

---

## Async/Await

```viper
// Async function
async func fetchData(url: Text) -> Data {
    let response = await http.get(url)
    return response.body
}

// Using async
async func main() {
    // Sequential
    let user = await fetchUser()
    let posts = await fetchPosts(user.id)
    
    // Parallel (fails fast on first error)
    let [a, b, c] = await all([
        fetchA(),
        fetchB(), 
        fetchC()
    ])
}

// Create task directly
let task: Task[User] = async {
    return fetchUser("123")
}
let user = await task
```

---

## Error Handling (No Exceptions!)

```viper
// Errors as values
value Result[T] = Ok(T) | Error(ErrorInfo)

func readFile(path: Text) -> Result[Text] {
    if !exists(path) {
        return Error(ErrorInfo("NOT_FOUND", "File not found"))
    }
    return Ok(contents)
}

// Handle with pattern matching
match readFile("data.txt") {
    Ok(content) => process(content)
    Error(e) => print("Failed: ${e.message}")
}

// The ? operator works for BOTH Result and Option
func processFile(path: Text) -> Result[Data] {
    let content = readFile(path)?     // Propagates Error
    let parsed = parse(content)?      // Propagates Error
    return Ok(parsed)
}

func getUserEmail(id: Text) -> Option[Text] {
    let user = findUser(id)?          // Propagates None
    let profile = user.profile?       // Propagates None
    return Some(profile.email)
}

// Panic for programmer errors only
func mustExist(id: Text) -> User {
    match findUser(id) {
        Some(user) => user
        None => panic("User ${id} must exist")
    }
}
```

---

## Collections and Indexing

### Indexing Rules

- `collection[i]` — **Panics** if out of bounds
- `collection.get(i)` — Returns `Option[T]` (safe)

### List

```viper
let list = [1, 2, 3, 4, 5]

// Fast indexing (panics if out of bounds)
let first = list[0]     // 1
let bad = list[10]      // panic!

// Safe indexing
match list.get(0) {
    Some(val) => print(val)
    None => print("Empty")
}

// Functional operations
let doubled = list.map(x => x * 2)
let evens = list.filter(x => x % 2 == 0)
let sum = list.reduce(0, (a, b) => a + b)
```

### Map

```viper
let map = Map[Text, Integer]()
map.put("alice", 30)
map.put("bob", 25)

// get returns Option[T]
match map.get("alice") {
    Some(age) => print("Alice is ${age}")
    None => print("Not found")
}

// Direct access panics if missing
let age = map["alice"]  // 30
let bad = map["eve"]    // panic!
```

### Set

```viper
let set = Set[Text]()
set.add("apple")
set.add("banana")
set.add("apple")  // No duplicates

if set.contains("apple") {
    print("Has apple")
}
```

---

## Optionals (T? = Option[T])

```viper
// T? is sugar for Option[T]
let maybe: Text? = null   // Same as: Option[Text] = None
maybe = "Hello"            // Same as: Some("Hello")

// Pattern matching (preferred)
match maybe {
    Some(value) => print(value)
    None => print("Nothing")
}

// If-let
if let value = maybe {
    print(value)  // value is Text, not Text?
}

// Optional chaining
let length = user?.address?.street?.length()

// Null coalescing
let name = user?.name ?? "Anonymous"

// ? operator works with Option
func getName(id: Text) -> Option[Text] {
    let user = findUser(id)?  // Returns None if not found
    return Some(user.name)
}
```

---

## Channels

```viper
let channel = Channel[Message](capacity: 10)

// Send (returns Result[Void])
async {
    match channel.send(Message("Hello")) {
        Ok => continue
        Error(Closed) => print("Channel closed")
    }
}

// Receive (returns Option[T])
async {
    while let msg = channel.receive() {
        print(msg)  // Loops until channel closed
    }
}

// close() is idempotent
channel.close()
channel.close()  // Safe to call again
```

---

## Memory Management

### Reference Counting (Automatic)

```viper
// Values: copied, no refcounting
let p1 = Point(x: 10, y: 20)
let p2 = p1  // Deep copy

// Entities: reference counted
let e1 = Entity()
let e2 = e1  // Refcount = 2
// Deallocated when refcount = 0
```

### Weak References (Break Cycles)

```viper
entity Node {
    value: Any
    children: List[Node]
    weak parent: Node?  // Weak doesn't increase refcount
}

// Reading weak refs returns optional
if let parent = node.parent {
    parent.updateChild(node)
}
```

---

## Visibility

**Default: private**. Use `expose` for public:

```viper
module MyModule

entity Service {
    database: Connection        // Private by default
    hide secret: Text          // Extra explicit private
    
    expose func getUser() {    // Public API
        // ...
    }
}
```

---

## Complete Example: REST API

```viper
module TodoAPI

import Viper.Http
import Viper.Json

value Todo {
    id: Text
    title: Text
    done: Boolean
}

entity TodoService {
    todos: Map[Text, Todo] = Map()
    
    expose async func handle(req: Request) -> Response {
        match (req.method, req.path) {
            ("GET", "/todos") => 
                Response.json(todos.values())
                
            ("GET", "/todos/${id}") =>
                match todos.get(id) {
                    Some(todo) => Response.json(todo)
                    None => Response.notFound()
                }
                
            ("POST", "/todos") =>
                match Json.decode[Todo](req.body) {
                    Ok(todo) => {
                        todos.put(todo.id, todo)
                        Response.created(todo)
                    }
                    Error(e) => Response.badRequest(e.message)
                }
                
            ("DELETE", "/todos/${id}") => {
                todos.remove(id)
                Response.noContent()
            }
            
            _ => Response.notFound()
        }
    }
}

async func main() {
    let service = TodoService()
    let server = Http.server(port: 8080)
    
    server.handle("*", service.handle)
    
    print("Server running on http://localhost:8080")
    await server.listen()
}
```

---

## Cheat Sheet

### Types

- `Integer` - 64-bit integer
- `Number` - 64-bit float
- `Text` - UTF-8 string
- `Boolean` - true/false
- `T?` - Optional T (same as Option[T])

### Keywords (25 total)

```
async     await     break     continue   else
entity    expose    extends   for        func
guard     hide      if        implements import
interface let       match     module     override
return    super     value     var        weak
while
```

### Operators

- Math: `+ - * / %`
- Compare: `== != < > <= >=`
- Logic: `&& || !`
- Optional: `?. ??`
- Type: `is as`
- Error/Option propagation: `?`

---

## Key Rules

1. **Values copy, entities reference**
2. **No exceptions - use Result[T]**
3. **? works for both Result and Option**
4. **T? is Option[T] with sugar**
5. **list[i] panics, list.get(i) is safe**
6. **Everything private unless exposed**
7. **Pattern matching is primary control flow**
8. **Weak references break cycles**

---

**Status:** v0.1 RC1 - Final feedback before freeze!

**Learn in an hour. Master in a day. Build anything.**
