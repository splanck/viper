# Chapter 16: Interfaces

Inheritance says "this thing *is* a specific type." But sometimes you care about what something *can do*, not what it *is*.

A dog can be drawn on screen. So can a spaceship. So can a health bar. They're completely different things, but they share an ability: they can be drawn. How do you write code that works with "anything drawable"?

*Interfaces* solve this. An interface defines a set of methods that classes must implement. It's a contract: "I promise I can do these things."

---

## What Is an Interface?

An interface declares methods without implementing them:

```viper
interface Drawable {
    func draw();
}
```

This says: "Anything that is Drawable must have a `draw()` method." It doesn't say *how* to draw — that's up to each class.

Classes *implement* interfaces:

```viper
class Circle implements Drawable {
    radius: f64;

    func draw() {
        Viper.Terminal.Say("Drawing a circle with radius " + self.radius);
    }
}

class Rectangle implements Drawable {
    width: f64;
    height: f64;

    func draw() {
        Viper.Terminal.Say("Drawing a rectangle " + self.width + "x" + self.height);
    }
}
```

Both classes are Drawable. They each provide their own implementation of `draw()`.

---

## Using Interfaces

The power comes from treating different classes uniformly:

```viper
func renderScene(items: [Drawable]) {
    for item in items {
        item.draw();
    }
}

let circle = Circle { radius: 5.0 };
let rect = Rectangle { width: 10.0, height: 3.0 };

let scene: [Drawable] = [circle, rect];
renderScene(scene);
// Drawing a circle with radius 5
// Drawing a rectangle 10x3
```

The `renderScene` function doesn't know or care about the specific types. It just knows they're Drawable, so it can call `draw()` on each one.

---

## Interface vs. Inheritance

**Inheritance**: "is-a" relationship, shares implementation
```viper
class Dog extends Animal { ... }
// A dog IS an animal, inherits animal's code
```

**Interface**: "can-do" relationship, shares behavior contract
```viper
class Dog implements Drawable { ... }
// A dog CAN BE drawn, must implement draw()
```

Key differences:
- A class can extend only one class
- A class can implement many interfaces
- Inheritance gives you code; interfaces give you contracts

---

## Multiple Interfaces

A class can implement several interfaces:

```viper
interface Drawable {
    func draw();
}

interface Movable {
    func move(dx: f64, dy: f64);
}

interface Clickable {
    func onClick();
}

class Button implements Drawable, Movable, Clickable {
    x: f64;
    y: f64;
    label: string;

    func draw() {
        Viper.Terminal.Say("Drawing button: " + self.label);
    }

    func move(dx: f64, dy: f64) {
        self.x += dx;
        self.y += dy;
    }

    func onClick() {
        Viper.Terminal.Say("Button clicked: " + self.label);
    }
}
```

The Button is Drawable AND Movable AND Clickable. It can be used anywhere any of those interfaces is expected:

```viper
func drawAll(items: [Drawable]) { ... }
func moveAll(items: [Movable]) { ... }
func handleClick(item: Clickable) { ... }

let btn = Button { x: 0.0, y: 0.0, label: "OK" };
drawAll([btn]);
handleClick(btn);
```

---

## Interfaces with Inheritance

You can combine inheritance and interfaces:

```viper
class Enemy {
    health: i64;

    func takeDamage(amount: i64) {
        self.health -= amount;
    }
}

interface Drawable {
    func draw();
}

interface Attackable {
    func attack() -> i64;
}

class Goblin extends Enemy implements Drawable, Attackable {
    func draw() {
        Viper.Terminal.Say("Drawing a goblin");
    }

    func attack() -> i64 {
        return 5;
    }
}
```

The Goblin is an Enemy (inheritance) and is also Drawable and Attackable (interfaces).

---

## Interface Segregation

Prefer small, focused interfaces over large ones:

```viper
// Bad: one big interface
interface GameEntity {
    func draw();
    func move(dx: f64, dy: f64);
    func attack() -> i64;
    func takeDamage(amount: i64);
    func save();
    func load();
}

// Good: small focused interfaces
interface Drawable {
    func draw();
}

interface Movable {
    func move(dx: f64, dy: f64);
}

interface Combatant {
    func attack() -> i64;
    func takeDamage(amount: i64);
}

interface Saveable {
    func save();
    func load();
}
```

With small interfaces:
- A tree can be Drawable but not Movable
- A player can be Movable and Combatant but a decoration is just Drawable
- Classes implement only what they need

---

## A Complete Example: Plugin System

Interfaces are perfect for plugin systems where you don't know what implementations will exist:

```viper
module PluginSystem;

// Interface that all plugins must implement
interface Plugin {
    func getName() -> string;
    func execute(input: string) -> string;
}

// Some plugins
class UppercasePlugin implements Plugin {
    func getName() -> string {
        return "Uppercase";
    }

    func execute(input: string) -> string {
        return input.upper();
    }
}

class ReversePlugin implements Plugin {
    func getName() -> string {
        return "Reverse";
    }

    func execute(input: string) -> string {
        return Viper.Text.reverse(input);
    }
}

class RepeatPlugin implements Plugin {
    times: i64;

    constructor(times: i64) {
        self.times = times;
    }

    func getName() -> string {
        return "Repeat x" + self.times;
    }

    func execute(input: string) -> string {
        return Viper.Text.repeat(input, self.times);
    }
}

// Plugin manager doesn't need to know about specific plugins
class PluginManager {
    plugins: [Plugin];

    constructor() {
        self.plugins = [];
    }

    func register(plugin: Plugin) {
        self.plugins.push(plugin);
        Viper.Terminal.Say("Registered: " + plugin.getName());
    }

    func process(input: string) -> string {
        let result = input;
        for plugin in self.plugins {
            result = plugin.execute(result);
        }
        return result;
    }

    func listPlugins() {
        Viper.Terminal.Say("Installed plugins:");
        for plugin in self.plugins {
            Viper.Terminal.Say("  - " + plugin.getName());
        }
    }
}

func start() {
    let manager = PluginManager();

    // Register some plugins
    manager.register(UppercasePlugin());
    manager.register(ReversePlugin());
    manager.register(RepeatPlugin(2));

    manager.listPlugins();

    // Process text through all plugins
    let input = "hello";
    let output = manager.process(input);

    Viper.Terminal.Say("Input: " + input);
    Viper.Terminal.Say("Output: " + output);
    // Output: OLLEHOLLEH (uppercased, reversed, repeated twice)
}
```

The PluginManager works with any Plugin without knowing the specific types. You could add new plugins without modifying the manager.

---

## The Three Languages

**ViperLang**
```viper
interface Printable {
    func print();
}

class Document implements Printable {
    func print() {
        Viper.Terminal.Say("Printing document");
    }
}
```

**BASIC**
```basic
INTERFACE Printable
    SUB Print()
END INTERFACE

CLASS Document IMPLEMENTS Printable
    SUB Print()
        PRINT "Printing document"
    END SUB
END CLASS
```

**Pascal**
```pascal
type
    IPrintable = interface
        procedure Print;
    end;

    TDocument = class(TInterfacedObject, IPrintable)
        procedure Print;
    end;

procedure TDocument.Print;
begin
    WriteLn('Printing document');
end;
```

Pascal prefixes interfaces with `I` by convention.

---

## Common Patterns

### Strategy pattern
```viper
interface SortStrategy {
    func sort(items: [i64]) -> [i64];
}

class QuickSort implements SortStrategy {
    func sort(items: [i64]) -> [i64] { ... }
}

class MergeSort implements SortStrategy {
    func sort(items: [i64]) -> [i64] { ... }
}

// Use any sorting strategy
func processData(data: [i64], strategy: SortStrategy) {
    let sorted = strategy.sort(data);
    ...
}
```

### Observer pattern
```viper
interface Observer {
    func onEvent(event: string);
}

class Subject {
    observers: [Observer];

    func subscribe(obs: Observer) {
        self.observers.push(obs);
    }

    func notify(event: string) {
        for obs in self.observers {
            obs.onEvent(event);
        }
    }
}
```

---

## When to Use Interfaces

**Use interfaces when:**
- Different classes need to be treated uniformly
- You're designing a plugin/extension system
- You want to define contracts without implementation
- You need multiple inheritance of behavior

**Use inheritance when:**
- Classes share actual implementation code
- There's a clear "is-a" relationship
- You want to reuse parent code

Often you'll use both together: inheritance for shared code, interfaces for shared contracts.

---

## Summary

- *Interfaces* define method contracts without implementation
- Classes *implement* interfaces by providing the methods
- A class can implement multiple interfaces
- Interfaces enable polymorphism — treating different classes uniformly
- Use small, focused interfaces
- Interfaces are great for plugin systems and loose coupling
- Combine with inheritance as needed

---

## Exercises

**Exercise 16.1**: Create an `Comparable` interface with a `compareTo(other) -> i64` method. Implement it for a `Person` class (compare by age).

**Exercise 16.2**: Create `Readable` and `Writable` interfaces. Create a `File` class that implements both.

**Exercise 16.3**: Create a simple command pattern: `Command` interface with `execute()` and `undo()`. Create `AddCommand`, `DeleteCommand`, `MoveCommand`.

**Exercise 16.4**: Create `Serializable` interface with `toJson() -> string` and `fromJson(s: string)`. Implement for a simple data class.

**Exercise 16.5**: Create a filter system: `Filter` interface with `matches(item) -> bool`. Create `AgeFilter`, `NameFilter`, etc. Write code that applies multiple filters.

**Exercise 16.6** (Challenge): Build a simple event system: `EventListener` interface, `EventDispatcher` class that manages listeners and dispatches events.

---

*We've seen interfaces as contracts. Next, we put it all together with polymorphism — writing code that works with many types through common interfaces.*

*[Continue to Chapter 17: Polymorphism →](17-polymorphism.md)*
