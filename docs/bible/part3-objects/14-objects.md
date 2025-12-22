# Chapter 14: Objects and Classes

In Chapter 11, we learned about structures — grouping data together. But structures have limitations. What if you want to create specialized versions? What if you want different types to share common behavior? What if you want to hide implementation details behind a stable interface?

*Object-oriented programming* (OOP) answers these questions. It's a way of thinking about programs as collections of interacting *objects* — entities that combine data and behavior, that can be created from templates, and that can relate to each other in flexible ways.

This chapter introduces the fundamentals: classes and objects.

---

## From Structures to Classes

A structure groups data:
```viper
struct Rectangle {
    width: f64;
    height: f64;
}
```

A *class* groups data *and* behavior, with more power:
```viper
class Rectangle {
    width: f64;
    height: f64;

    constructor(width: f64, height: f64) {
        self.width = width;
        self.height = height;
    }

    func area() -> f64 {
        return self.width * self.height;
    }

    func perimeter() -> f64 {
        return 2 * (self.width + self.height);
    }
}
```

The differences:
- `class` instead of `struct`
- A *constructor* that creates new instances
- Methods that operate on the object

---

## Creating Objects

A class is a template. An *object* (or *instance*) is a specific thing created from that template:

```viper
let rect1 = Rectangle(10.0, 5.0);
let rect2 = Rectangle(3.0, 4.0);

Viper.Terminal.Say(rect1.area());  // 50
Viper.Terminal.Say(rect2.area());  // 12
```

Each rectangle is a separate object with its own data. Calling `area()` on `rect1` uses `rect1`'s width and height.

---

## The Constructor

The constructor is a special method that runs when you create a new object:

```viper
class Person {
    name: string;
    age: i64;

    constructor(name: string, age: i64) {
        self.name = name;
        self.age = age;
    }

    constructor(name: string) {
        self.name = name;
        self.age = 0;  // Default age
    }
}

let alice = Person("Alice", 30);
let baby = Person("Baby");
```

You can have multiple constructors with different parameters. This is called *overloading*.

Constructors initialize the object. Use them to set required fields and perform any setup.

---

## self: The Current Object

Inside a method, `self` refers to the object the method was called on:

```viper
class Counter {
    count: i64;

    constructor() {
        self.count = 0;
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
counter.increment();
Viper.Terminal.Say(counter.getCount());  // 2
```

When you call `counter.increment()`, inside that method `self` is `counter`. When you call `counter.getCount()`, `self.count` returns `counter`'s count.

---

## Encapsulation: Public and Private

Not all parts of a class should be accessible from outside. Use `private` to hide internal details:

```viper
class BankAccount {
    private balance: f64;
    ownerName: string;

    constructor(owner: string, initialDeposit: f64) {
        self.ownerName = owner;
        self.balance = initialDeposit;
    }

    func deposit(amount: f64) {
        if amount > 0 {
            self.balance += amount;
        }
    }

    func withdraw(amount: f64) -> bool {
        if amount > 0 && amount <= self.balance {
            self.balance -= amount;
            return true;
        }
        return false;
    }

    func getBalance() -> f64 {
        return self.balance;
    }
}
```

The `balance` field is private — outside code can't access it directly:

```viper
let account = BankAccount("Alice", 100.0);
account.deposit(50.0);
Viper.Terminal.Say(account.getBalance());  // 150

// account.balance = 1000000;  // Error: balance is private
```

This protects the account's integrity. You can't set an arbitrary balance — you must go through `deposit` and `withdraw`, which enforce the rules.

---

## Methods: Behavior

Methods define what objects can do. They're functions that belong to a class:

```viper
class Circle {
    radius: f64;

    constructor(radius: f64) {
        self.radius = radius;
    }

    func area() -> f64 {
        return Viper.Math.PI * self.radius * self.radius;
    }

    func circumference() -> f64 {
        return 2 * Viper.Math.PI * self.radius;
    }

    func scale(factor: f64) {
        self.radius *= factor;
    }

    func diameter() -> f64 {
        return self.radius * 2;
    }
}
```

Methods can:
- Read the object's data (`area` uses `self.radius`)
- Modify the object's data (`scale` changes `self.radius`)
- Take parameters (`scale` takes a factor)
- Return values (`area` returns a number)
- Call other methods on the same object

---

## A Complete Example: Todo List

Let's build a todo list with classes:

```viper
module TodoApp;

class TodoItem {
    private text: string;
    private done: bool;

    constructor(text: string) {
        self.text = text;
        self.done = false;
    }

    func getText() -> string {
        return self.text;
    }

    func isDone() -> bool {
        return self.done;
    }

    func markDone() {
        self.done = true;
    }

    func markUndone() {
        self.done = false;
    }

    func toString() -> string {
        let status = "";
        if self.done {
            status = "[X]";
        } else {
            status = "[ ]";
        }
        return status + " " + self.text;
    }
}

class TodoList {
    private items: [TodoItem];
    private name: string;

    constructor(name: string) {
        self.name = name;
        self.items = [];
    }

    func add(text: string) {
        self.items.push(TodoItem(text));
    }

    func markDone(index: i64) {
        if index >= 0 && index < self.items.length {
            self.items[index].markDone();
        }
    }

    func remove(index: i64) {
        if index >= 0 && index < self.items.length {
            self.items.removeAt(index);
        }
    }

    func display() {
        Viper.Terminal.Say("=== " + self.name + " ===");
        if self.items.length == 0 {
            Viper.Terminal.Say("  (empty)");
            return;
        }

        for i in 0..self.items.length {
            Viper.Terminal.Say("  " + (i + 1) + ". " + self.items[i].toString());
        }
    }

    func countRemaining() -> i64 {
        let count = 0;
        for item in self.items {
            if !item.isDone() {
                count += 1;
            }
        }
        return count;
    }
}

func start() {
    let todos = TodoList("My Tasks");

    todos.add("Learn ViperLang");
    todos.add("Build a project");
    todos.add("Read the documentation");

    todos.display();
    // === My Tasks ===
    //   1. [ ] Learn ViperLang
    //   2. [ ] Build a project
    //   3. [ ] Read the documentation

    todos.markDone(0);
    todos.display();
    // === My Tasks ===
    //   1. [X] Learn ViperLang
    //   2. [ ] Build a project
    //   3. [ ] Read the documentation

    Viper.Terminal.Say("Remaining: " + todos.countRemaining());
    // Remaining: 2
}
```

Notice how:
- `TodoItem` manages individual tasks
- `TodoList` manages the collection
- Each class handles its own responsibilities
- Private fields protect internal state
- Methods provide controlled access

---

## The Three Languages

**ViperLang**
```viper
class Dog {
    name: string;

    constructor(name: string) {
        self.name = name;
    }

    func bark() {
        Viper.Terminal.Say(self.name + " says woof!");
    }
}

let dog = Dog("Rex");
dog.bark();
```

**BASIC**
```basic
CLASS Dog
    PUBLIC name AS STRING

    CONSTRUCTOR(n AS STRING)
        name = n
    END CONSTRUCTOR

    SUB Bark()
        PRINT name; " says woof!"
    END SUB
END CLASS

DIM dog AS Dog
dog = NEW Dog("Rex")
dog.Bark()
```

**Pascal**
```pascal
type
    Dog = class
    public
        name: string;
        constructor Create(n: string);
        procedure Bark;
    end;

constructor Dog.Create(n: string);
begin
    name := n;
end;

procedure Dog.Bark;
begin
    WriteLn(name, ' says woof!');
end;

var dog: Dog;
begin
    dog := Dog.Create('Rex');
    dog.Bark;
end.
```

---

## Class Design Guidelines

**Model real concepts.** A `Dog` class, a `BankAccount` class, a `Player` class — things you can point to and describe.

**Keep classes focused.** A class should have one primary responsibility. If a class is doing too many things, split it.

**Hide internals.** Make fields private by default. Expose only what others need. This lets you change implementation without breaking code that uses the class.

**Name methods as verbs.** `deposit`, `withdraw`, `bark`, `display` — methods do things.

**Prefer small methods.** Each method should do one thing well. Long methods probably need to be split.

---

## Common Mistakes

**Forgetting self:**
```viper
class Counter {
    count: i64;

    func increment() {
        count += 1;  // Error: should be self.count
    }
}
```

**Public fields that should be private:**
```viper
class BankAccount {
    balance: f64;  // Bad: anyone can modify directly
}

let account = BankAccount();
account.balance = -1000;  // Oops, negative balance!
```

**Monster classes:**
```viper
// Don't do this — one class doing everything
class Game {
    player: ...;
    enemies: ...;
    graphics: ...;
    sound: ...;
    input: ...;
    network: ...;
    // 50 methods for all these different things
}

// Better: separate classes for each concern
class Player { ... }
class EnemyManager { ... }
class Renderer { ... }
class SoundSystem { ... }
```

---

## Summary

- A *class* is a template that defines data (fields) and behavior (methods)
- An *object* (instance) is created from a class
- *Constructors* initialize new objects
- `self` refers to the current object inside methods
- *Private* fields/methods hide internal details
- *Encapsulation* protects object state from invalid modifications
- Good class design: one responsibility, hidden internals, clear interface

---

## Exercises

**Exercise 14.1**: Create a `Counter` class with `increment()`, `decrement()`, `reset()`, and `getValue()` methods.

**Exercise 14.2**: Create a `Temperature` class that stores a temperature in Celsius and has methods `toFahrenheit()` and `toKelvin()`.

**Exercise 14.3**: Create a `Stopwatch` class with `start()`, `stop()`, and `elapsed()` methods.

**Exercise 14.4**: Create a `Deck` class that represents a deck of playing cards with `shuffle()` and `draw()` methods.

**Exercise 14.5**: Create a `ShoppingCart` class that stores items with prices, and has methods to add items, remove items, and calculate total.

**Exercise 14.6** (Challenge): Create a simple text-based RPG character: `Character` class with name, health, attack, and methods for attacking, taking damage, and leveling up.

---

*We can create our own types with behavior. But what if we want specialized versions — a `Cat` that's a kind of `Animal`, a `SavingsAccount` that's a kind of `BankAccount`? Next, we learn about inheritance.*

*[Continue to Chapter 15: Inheritance →](15-inheritance.md)*
