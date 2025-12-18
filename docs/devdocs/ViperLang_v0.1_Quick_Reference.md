# ViperLang — Quick Reference

## Version 0.1 Final

<div align="center">

**Everything you need in 10 minutes**

*Copy types. Pattern matching. No exceptions. One way to do things.*

</div>

---

## Hello World

```viper
module Hello

func main() {
    print("Hello, ViperLang!")
}
```

---

## Types at a Glance

| Type | Alias | Description |
|------|-------|-------------|
| `Text` | `String` | UTF-8 string |
| `Integer` | `i64` | 64-bit signed |
| `Number` | `f64` | 64-bit float |
| `Boolean` | `i1` | true/false |
| `T?` | `Option[T]` | Nullable T |

---

## Values (Copy Types)

```viper
value Point {
    x: Number
    y: Number
}

let p1 = Point(x: 10, y: 20)
var p2 = p1       // Copies the value
p2.x = 30         // Only p2 changes
```

**Rule:** Cannot mutate temporaries (`getPoint().x = 10` is illegal).

---

## Entities (Reference Types)

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
u2.name = "Bob"   // Both see "Bob"
```

---

## Inheritance (Entities Only)

```viper
entity Animal {
    name: Text
    func speak() -> Text { return "..." }
}

entity Dog extends Animal {
    breed: Text
    
    override func speak() -> Text {  // 'override' required
        return "Woof!"
    }
}
```

---

## Interfaces

```viper
interface Drawable {
    func draw(canvas: Canvas)
    
    // Default implementation
    func drawTwice(canvas: Canvas) {
        draw(canvas)
        draw(canvas)
    }
}

entity Circle implements Drawable {
    radius: Number
    func draw(canvas: Canvas) { /* ... */ }
}
```

---

## Variables

```viper
let immutable = 42         // Can't reassign
var mutable = 42           // Can reassign
mutable = 43

let inferred = "Hello"     // Type: Text
let typed: Number = 3.14   // Explicit type

// Destructuring
let Point(x, y) = getPoint()
let (first, second) = getPair()
```

---

## Control Flow

### Pattern Matching

```viper
value Result[T] = Ok(T) | Err(Error)

match divide(10, 2) {
    Ok(value) => print("Result: ${value}")
    Err(e) => print("Error: ${e.message}")
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
} else if other {
    doOther()
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
for item in list { process(item) }

while condition {
    if done { break }
    if skip { continue }
}

for i in 0..10 { }    // 0 to 9 (half-open)
for i in 0..=10 { }   // 0 to 10 (inclusive)
```

### Guard

```viper
func process(data: Data?) -> Text {
    guard data != null else { return "No data" }
    guard data.isValid() else { return "Invalid" }
    return data.process()
}
```

---

## Functions

```viper
func add(a: Number, b: Number) -> Number {
    return a + b
}

func greet(name: Text = "World") {
    print("Hello, ${name}!")
}

// Expression body
func double(x: Number) -> Number = x * 2

// Lambda
let triple = (x: Number) -> x * 3
let doubled = list.map(x => x * 2)
```

---

## Error Handling (No Exceptions!)

```viper
value Error {
    code: Text
    message: Text
}

value Result[T] = Ok(T) | Err(Error)

func readFile(path: Text) -> Result[Text] {
    if !exists(path) {
        return Err(Error(code: "NOT_FOUND", message: "File not found"))
    }
    return Ok(contents)
}

// The ? operator propagates errors
func processFile(path: Text) -> Result[Data] {
    let content = readFile(path)?     // Returns Err if failed
    let parsed = parse(content)?      // Returns Err if failed
    return Ok(parsed)
}
```

---

## Optionals (T? = Option[T])

```viper
let maybe: Text? = null   // Same as: Option[Text] = None
maybe = "Hello"           // Same as: Some("Hello")

// Pattern matching
match maybe {
    Some(value) => print(value)
    None => print("Nothing")
}

// If-let
if let value = maybe {
    print(value)
}

// Chaining and coalescing
let length = user?.address?.street?.len()
let name = user?.name ?? "Anonymous"

// ? propagates None
func getName(id: Text) -> Option[Text] {
    let user = findUser(id)?
    return Some(user.name)
}
```

---

## Collections

### List[T]

```viper
let list = [1, 2, 3, 4, 5]

// Indexing (panics if out of bounds)
let first = list[0]

// Safe access (returns Option)
match list.get(10) {
    Some(val) => print(val)
    None => print("Not found")
}

// Operations
list.push(6)              // Add to end
let last = list.pop()     // Remove from end
let doubled = list.map(x => x * 2)
let evens = list.filter(x => x % 2 == 0)
```

### Map[Text, V]

```viper
let map = Map[Text, Integer]()
map.set("alice", 30)

// Safe access
match map.get("alice") {
    Some(age) => print(age)
    None => print("Not found")
}

// Direct access (panics if missing)
let age = map["alice"]
```

---

## Memory Management

```viper
// Values: copied
let p1 = Point(x: 10, y: 20)
let p2 = p1  // Deep copy

// Entities: reference counted
let e1 = Entity()
let e2 = e1  // Refcount = 2

// Weak references (break cycles)
entity Node {
    children: List[Node]
    weak parent: Node?  // Doesn't increase refcount
}

if let parent = node.parent {
    parent.update()
}
```

---

## Visibility

**Default: private.** Use `expose` for public:

```viper
entity Service {
    database: Connection        // Private
    hide secret: Text          // Explicitly private
    
    expose func getUser() { }  // Public
}
```

---

## Cheat Sheet

### Primitive Types

| Type | Description |
|------|-------------|
| `Integer` | 64-bit signed int |
| `Number` | 64-bit float |
| `Text` | UTF-8 string |
| `Boolean` | true/false |
| `T?` | Optional T |

### Keywords (25)

```
async     await     break     continue   else
entity    expose    extends   for        func
guard     hide      if        implements import
interface let       match     module     override
return    super     value     var        weak
while
```

### Operators

| Category | Operators |
|----------|-----------|
| Math | `+ - * / %` |
| Compare | `== != < > <= >=` |
| Logic | `&& \|\| !` |
| Optional | `?. ?? ?` |
| Type | `is as` |
| Range | `.. ..=` |

---

## Key Rules

1. **Values copy, entities reference**
2. **No exceptions — use Result[T]**
3. **? works for both Result and Option**
4. **T? is Option[T] sugar**
5. **list[i] panics, list.get(i) is safe**
6. **Private by default, expose for public**
7. **Pattern matching is primary control flow**
8. **Weak references break cycles**

---

## Quick Example

```viper
module TodoApp

value Todo {
    id: Text
    title: Text
    done: Boolean
}

entity TodoService {
    todos: Map[Text, Todo] = Map()
    
    expose func add(title: Text) -> Todo {
        let id = generateId()
        let todo = Todo(id: id, title: title, done: false)
        todos.set(id, todo)
        return todo
    }
    
    expose func complete(id: Text) -> Result[Todo] {
        match todos.get(id) {
            Some(todo) => {
                let updated = Todo(id: todo.id, title: todo.title, done: true)
                todos.set(id, updated)
                return Ok(updated)
            }
            None => Err(Error(code: "NOT_FOUND", message: "Todo not found"))
        }
    }
    
    expose func list() -> List[Todo] {
        return todos.values()
    }
}

func main() {
    let service = TodoService()
    
    service.add("Learn ViperLang")
    service.add("Build something cool")
    
    for todo in service.list() {
        let status = todo.done ? "✓" : " "
        print("[${status}] ${todo.title}")
    }
}
```

---

**Status:** v0.1 Final

**Learn in an hour. Master in a day. Build anything.**
