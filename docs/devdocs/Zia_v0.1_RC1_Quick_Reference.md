# Zia — Quick Reference

## Version 0.1 RC1 (Release Candidate 1)

<div align="center">

**Everything you need in 10 minutes**

*Copy types. Pattern matching. No exceptions. One way to do things.*

</div>

---

## Hello World

```viper
module Hello

func main() {
    Viper.Terminal.Say("Hello, Zia!")
}
```

---

## Types

### Values (Copy Types)

```viper
value Point {
    Number x;
    Number y;
}

Point p1 = Point(x: 10, y: 20);
Point p2 = p1;    // Copies the value
p2.x = 30;        // Only p2 changes; p1 unchanged
```

**Key:** Values copy on assignment. Cannot mutate temporaries (`getPoint().x = 10` is illegal).

### Entities (Reference Types)

```viper
entity User {
    Text name;
    Text email;

    func display() -> Text {
        return "${name} <${email}>";
    }
}

User u1 = User(name: "Alice", email: "alice@example.com");
User u2 = u1;     // Same object (reference)
u2.name = "Bob";  // Both u1 and u2 see "Bob"
```

### Inheritance (Entities Only)

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
        return "Woof!";  // super.speak() valid here
    }
}
```

### Interfaces

```viper
interface Drawable {
    func draw(canvas: Canvas);

    // Default implementation (can only use interface members)
    func drawTwice(canvas: Canvas) {
        draw(canvas);
        draw(canvas);
    }
}

entity Circle implements Drawable {
    Number radius;

    func draw(canvas: Canvas) {
        canvas.drawCircle(radius);
    }
}
```

---

## Variables

```viper
// Java-style declarations: Type name = value;
Integer count = 42;
Text message = "Hello";
Number pi = 3.14;

// Reassignment
count = 43;

// Uninitialized (requires type)
Integer total;
total = 100;
```

---

## Control Flow

### Pattern Matching

```viper
value Result[T] = Ok(T) | Error(Text)

match divide(10, 2) {
    Ok(value) => Viper.Terminal.Say("Result: ${value}");
    Error(msg) => Viper.Terminal.Say("Error: ${msg}");
}

// With guards
match number {
    n where n < 0 => "Negative";
    0 => "Zero";
    n where n > 0 => "Positive";
}
```

### If/Else

```viper
if condition {
    doSomething();
} else if otherCondition {
    doOtherThing();
} else {
    doDefault();
}

// Ternary
Text result = condition ? "yes" : "no";
```

### Loops

```viper
// For each
for item in list {
    process(item);
}

// While
while condition {
    doWork();
    if done { break; }
    if skip { continue; }
}

// Range (half-open)
for i in 0..10 {
    Viper.Terminal.SayInt(i);  // 0 through 9
}

// Range (inclusive)
for i in 0..=10 {
    Viper.Terminal.SayInt(i);  // 0 through 10
}
```

### Guard (Early Return)

```viper
func process(data: Data?) -> Text {
    guard data != null else {
        return "No data";
    }

    guard data.isValid() else {
        return "Invalid";
    }

    return data.process();
}
```

---

## Functions

```viper
// Basic function
func add(a: Number, b: Number) -> Number {
    return a + b;
}

// No return value
func printSum(a: Number, b: Number) {
    Viper.Terminal.SayNum(a + b);
}

// Default parameters
func greet(name: Text = "World") {
    Viper.Terminal.Say("Hello, ${name}!");
}

// Generic function
func first[T](list: List[T]) -> Option[T] {
    return list.isEmpty() ? None : Some(list[0]);  // list[0] panics if empty!
}
```

---

## Async/Await

> **Note:** Async/await is planned for future versions. Currently, programs run synchronously.

```viper
// Async function (future feature)
async func loadData(path: Text) -> Text {
    return Viper.IO.File.ReadAllText(path);
}

// Using async
async func main() {
    // Sequential
    Text config = await loadData("config.txt");
    Text users = await loadData("users.txt");
}
```

---

## Error Handling (No Exceptions!)

```viper
// Errors as values
value Result[T] = Ok(T) | Error(ErrorInfo)

func readFile(path: Text) -> Result[Text] {
    if !Viper.IO.File.Exists(path) {
        return Error(ErrorInfo("NOT_FOUND", "File not found"));
    }
    return Ok(Viper.IO.File.ReadAllText(path));
}

// Handle with pattern matching
match readFile("data.txt") {
    Ok(content) => process(content);
    Error(e) => Viper.Terminal.Say("Failed: ${e.message}");
}

// The ? operator works for BOTH Result and Option
func processFile(path: Text) -> Result[Data] {
    Text content = readFile(path)?;    // Propagates Error
    Data parsed = parse(content)?;     // Propagates Error
    return Ok(parsed);
}

// Panic for programmer errors only
func mustExist(id: Text) -> User {
    match findUser(id) {
        Some(user) => user;
        None => panic("User ${id} must exist");
    }
}
```

---

## Collections and Indexing

All collections are provided by the Viper.Collections.* runtime.

### Indexing Rules

- `collection.get(i)` — Returns the item (panics if out of bounds)
- `collection.Has(item)` — Check if item exists

### List (Viper.Collections.List)

```viper
List numbers = new List();
numbers.Add(1);
numbers.Add(2);
numbers.Add(3);

// Get by index
Integer first = numbers.get_Item(0);  // 1

// Check if contains
if numbers.Has(2) {
    Viper.Terminal.Say("Has 2");
}

// Get count
Integer count = numbers.Count;
```

### Map (Viper.Collections.Map)

```viper
Map ages = new Map();
ages.Set("alice", 30);
ages.Set("bob", 25);

// Get value (returns null if not found)
Integer aliceAge = ages.Get("alice");

// Check if key exists
if ages.Has("alice") {
    Viper.Terminal.Say("Alice is ${aliceAge}");
}

// Get all keys
List keys = ages.Keys();
```

### Bag (Viper.Collections.Bag) - String Sets

```viper
Bag fruits = new Bag();
fruits.Put("apple");
fruits.Put("banana");
fruits.Put("apple");  // No duplicates

if fruits.Has("apple") {
    Viper.Terminal.Say("Has apple");
}
```

---

## Optionals (T? = Option[T])

```viper
// T? is sugar for Option[T]
Text? maybe = null;        // Same as: Option[Text] = None
maybe = "Hello";           // Same as: Some("Hello")

// Pattern matching (preferred)
match maybe {
    Some(value) => Viper.Terminal.Say(value);
    None => Viper.Terminal.Say("Nothing");
}

// If-let (future feature)
if let value = maybe {
    Viper.Terminal.Say(value);  // value is Text, not Text?
}

// Optional chaining
Integer? length = user?.address?.street?.length();

// Null coalescing
Text name = user?.name ?? "Anonymous";

// ? operator works with Option
func getName(id: Text) -> Option[Text] {
    User? user = findUser(id)?;  // Returns None if not found
    return Some(user.name);
}
```

---

## Channels

> **Note:** Channels are planned for future versions with async support.

```viper
Channel[Message] channel = new Channel[Message](10);

// Send (returns Result[Void])
async {
    match channel.send(new Message("Hello")) {
        Ok => continue;
        Error(Closed) => Viper.Terminal.Say("Channel closed");
    }
}

// Receive (returns Option[T])
async {
    while let msg = channel.receive() {
        Viper.Terminal.Say(msg);  // Loops until channel closed
    }
}

// close() is idempotent
channel.close();
channel.close();  // Safe to call again
```

---

## Memory Management

### Reference Counting (Automatic)

```viper
// Values: copied, no refcounting
Point p1 = Point(x: 10, y: 20);
Point p2 = p1;  // Deep copy

// Entities: reference counted
Entity e1 = new Entity();
Entity e2 = e1;  // Refcount = 2
// Deallocated when refcount = 0
```

### Weak References (Break Cycles)

```viper
entity Node {
    Any value;
    List[Node] children;
    weak Node? parent;  // Weak doesn't increase refcount
}

// Reading weak refs returns optional
if node.parent != null {
    node.parent.updateChild(node);
}
```

---

## Visibility

**Default: private**. Use `expose` for public:

```viper
module MyModule;

entity Service {
    Connection database;        // Private by default
    hide Text secret;           // Extra explicit private

    expose func getUser() {     // Public API
        // ...
    }
}
```

---

## Complete Example: Task Manager

```viper
module TaskManager;

// Task entity with state
entity Task {
    Integer id;
    Text title;
    Integer done;

    func init(taskId: Integer, taskTitle: Text) {
        id = taskId;
        title = taskTitle;
        done = 0;
    }

    func markDone() {
        done = 1;
    }

    func display() {
        Text status = "[x]";
        if done == 0 {
            status = "[ ]";
        }
        Viper.Terminal.Print(status);
        Viper.Terminal.Print(" ");
        Viper.Terminal.PrintInt(id);
        Viper.Terminal.Print(": ");
        Viper.Terminal.Say(title);
    }
}

// Task manager with list storage
entity TaskManager {
    List tasks;
    Integer nextId;

    func init() {
        tasks = new List();
        nextId = 1;
    }

    func addTask(title: Text) {
        Task task = new Task();
        task.init(nextId, title);
        tasks.add(task);
        nextId = nextId + 1;
        Viper.Terminal.Say("Task added!");
    }

    func listTasks() {
        Viper.Terminal.Say("--- Tasks ---");
        Integer i = 0;
        while i < tasks.get_Count() {
            Task task = tasks.get(i);
            task.display();
            i = i + 1;
        }
    }

    func completeTask(taskId: Integer) {
        Integer i = 0;
        while i < tasks.get_Count() {
            Task task = tasks.get(i);
            if task.id == taskId {
                task.markDone();
                Viper.Terminal.Say("Task marked complete!");
                return;
            }
            i = i + 1;
        }
        Viper.Terminal.Say("Task not found.");
    }
}

// Main entry point
TaskManager manager;

func start() {
    manager = new TaskManager();
    manager.init();

    Viper.Terminal.Say("Task Manager - Commands: a)dd, l)ist, c)omplete, q)uit");

    Integer running = 1;
    while running == 1 {
        Viper.Terminal.Print("> ");
        String cmd = Viper.Terminal.GetKey();

        if cmd == "a" {
            Viper.Terminal.Print("Title: ");
            Text title = Viper.Terminal.ReadLine();
            manager.addTask(title);
        }
        if cmd == "l" {
            manager.listTasks();
        }
        if cmd == "c" {
            Viper.Terminal.Print("Task ID: ");
            Integer id = Viper.Terminal.ReadInt();
            manager.completeTask(id);
        }
        if cmd == "q" {
            running = 0;
        }
    }

    Viper.Terminal.Say("Goodbye!");
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

### Keywords (24 total)

```
async     await     break     continue   else
entity    expose    extends   final      for
func      guard     hide      if         implements
bind    interface match     module     new
override  return    super     value      weak
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
5. **Use Viper.* runtime for IO and collections**
6. **Everything private unless exposed**
7. **Pattern matching is primary control flow**
8. **Weak references break cycles**

---

**Status:** v0.1 RC1 - Final feedback before freeze!

**Learn in an hour. Master in a day. Build anything.**
