# Chapter 17: Polymorphism

You've seen pieces of this already: calling `draw()` on different shapes, processing different plugins through the same interface. Now we name it and explore it fully.

*Polymorphism* means "many forms." In programming, it means one interface, multiple implementations. You write code once that works with many different types.

---

## The Power of Polymorphism

Consider a game loop that updates everything:

```viper
interface Updatable {
    func update(deltaTime: f64);
}

class Player implements Updatable {
    func update(deltaTime: f64) {
        // Move based on input
    }
}

class Enemy implements Updatable {
    func update(deltaTime: f64) {
        // Chase the player
    }
}

class Particle implements Updatable {
    func update(deltaTime: f64) {
        // Fade and move
    }
}

class Bullet implements Updatable {
    func update(deltaTime: f64) {
        // Fly forward
    }
}
```

Now the game loop:

```viper
let entities: [Updatable] = [player, enemy1, enemy2, particle1, bullet1, ...];

while running {
    let dt = getDeltaTime();
    for entity in entities {
        entity.update(dt);
    }
}
```

One loop handles players, enemies, particles, bullets — anything Updatable. Adding a new entity type doesn't require changing the loop. That's polymorphism.

---

## Types of Polymorphism

### Subtype polymorphism (what we've been doing)

Use a base class or interface to treat different types uniformly:

```viper
class Animal {
    func speak() { ... }
}

class Dog extends Animal {
    func speak() {
        Viper.Terminal.Say("Woof!");
    }
}

class Cat extends Animal {
    func speak() {
        Viper.Terminal.Say("Meow!");
    }
}

func makeAllSpeak(animals: [Animal]) {
    for animal in animals {
        animal.speak();  // Calls the right version automatically
    }
}
```

### Method overloading

Same method name, different parameters:

```viper
class Printer {
    func print(text: string) {
        Viper.Terminal.Say(text);
    }

    func print(number: i64) {
        Viper.Terminal.Say(number.toString());
    }

    func print(items: [string]) {
        for item in items {
            Viper.Terminal.Say(item);
        }
    }
}

let p = Printer();
p.print("hello");        // Calls first version
p.print(42);             // Calls second version
p.print(["a", "b"]);     // Calls third version
```

The compiler chooses the right method based on argument types.

---

## Virtual Methods and Dynamic Dispatch

When you override a method, the correct version is called based on the actual object type at runtime:

```viper
class Shape {
    func area() -> f64 {
        return 0.0;
    }
}

class Circle extends Shape {
    radius: f64;

    func area() -> f64 {
        return Viper.Math.PI * self.radius * self.radius;
    }
}

let shape: Shape = Circle { radius: 5.0 };
Viper.Terminal.Say(shape.area());  // 78.54... (Circle's version)
```

Even though the variable type is `Shape`, the object is a `Circle`, so `Circle.area()` is called. This is *dynamic dispatch* — the decision happens at runtime.

---

## Polymorphism in Practice

### Collections of mixed types

```viper
interface Drawable {
    func draw();
}

let scene: [Drawable] = [
    Player(),
    Enemy(),
    Tree(),
    House(),
    Particle(),
    UI_Button()
];

for item in scene {
    item.draw();  // Each draws itself appropriately
}
```

### Factory patterns

```viper
interface Enemy {
    func attack();
}

class Goblin implements Enemy { ... }
class Orc implements Enemy { ... }
class Dragon implements Enemy { ... }

func createEnemy(level: i64) -> Enemy {
    if level < 5 {
        return Goblin();
    } else if level < 10 {
        return Orc();
    } else {
        return Dragon();
    }
}

// Caller doesn't know the specific type
let enemy = createEnemy(7);
enemy.attack();
```

### Callback systems

```viper
interface Callback {
    func onComplete(result: string);
}

class Logger implements Callback {
    func onComplete(result: string) {
        Viper.File.appendText("log.txt", result + "\n");
    }
}

class Display implements Callback {
    func onComplete(result: string) {
        Viper.Terminal.Say("Result: " + result);
    }
}

func doAsyncWork(callback: Callback) {
    // ... do work ...
    callback.onComplete("Done!");
}
```

---

## A Complete Example: Drawing System

Let's build a complete rendering system using polymorphism:

```viper
module DrawingSystem;

interface Drawable {
    func draw();
    func getBounds() -> Rect;
}

struct Rect {
    x: f64;
    y: f64;
    width: f64;
    height: f64;
}

class Circle implements Drawable {
    x: f64;
    y: f64;
    radius: f64;
    color: string;

    constructor(x: f64, y: f64, radius: f64, color: string) {
        self.x = x;
        self.y = y;
        self.radius = radius;
        self.color = color;
    }

    func draw() {
        Viper.Terminal.Say("Drawing " + self.color + " circle at (" +
            self.x + ", " + self.y + ") radius " + self.radius);
    }

    func getBounds() -> Rect {
        return Rect {
            x: self.x - self.radius,
            y: self.y - self.radius,
            width: self.radius * 2,
            height: self.radius * 2
        };
    }
}

class Rectangle implements Drawable {
    x: f64;
    y: f64;
    width: f64;
    height: f64;
    color: string;

    constructor(x: f64, y: f64, w: f64, h: f64, color: string) {
        self.x = x;
        self.y = y;
        self.width = w;
        self.height = h;
        self.color = color;
    }

    func draw() {
        Viper.Terminal.Say("Drawing " + self.color + " rectangle at (" +
            self.x + ", " + self.y + ") size " + self.width + "x" + self.height);
    }

    func getBounds() -> Rect {
        return Rect { x: self.x, y: self.y, width: self.width, height: self.height };
    }
}

class Text implements Drawable {
    x: f64;
    y: f64;
    content: string;

    constructor(x: f64, y: f64, content: string) {
        self.x = x;
        self.y = y;
        self.content = content;
    }

    func draw() {
        Viper.Terminal.Say("Drawing text '" + self.content + "' at (" +
            self.x + ", " + self.y + ")");
    }

    func getBounds() -> Rect {
        let width = self.content.length * 8.0;  // Approximate
        return Rect { x: self.x, y: self.y, width: width, height: 16.0 };
    }
}

// Group of drawables
class Group implements Drawable {
    children: [Drawable];

    constructor() {
        self.children = [];
    }

    func add(item: Drawable) {
        self.children.push(item);
    }

    func draw() {
        for child in self.children {
            child.draw();
        }
    }

    func getBounds() -> Rect {
        // Calculate bounding box of all children
        if self.children.length == 0 {
            return Rect { x: 0.0, y: 0.0, width: 0.0, height: 0.0 };
        }

        let first = self.children[0].getBounds();
        let minX = first.x;
        let minY = first.y;
        let maxX = first.x + first.width;
        let maxY = first.y + first.height;

        for i in 1..self.children.length {
            let b = self.children[i].getBounds();
            if b.x < minX { minX = b.x; }
            if b.y < minY { minY = b.y; }
            if b.x + b.width > maxX { maxX = b.x + b.width; }
            if b.y + b.height > maxY { maxY = b.y + b.height; }
        }

        return Rect { x: minX, y: minY, width: maxX - minX, height: maxY - minY };
    }
}

func start() {
    // Create a scene
    let scene = Group();

    scene.add(Rectangle(10.0, 10.0, 100.0, 50.0, "blue"));
    scene.add(Circle(80.0, 35.0, 20.0, "red"));
    scene.add(Text(120.0, 30.0, "Hello!"));

    // Create a nested group
    let icons = Group();
    icons.add(Circle(200.0, 100.0, 10.0, "green"));
    icons.add(Circle(220.0, 100.0, 10.0, "yellow"));
    scene.add(icons);

    // Draw everything
    Viper.Terminal.Say("=== Drawing Scene ===");
    scene.draw();

    // Get total bounds
    let bounds = scene.getBounds();
    Viper.Terminal.Say("");
    Viper.Terminal.Say("Scene bounds: " + bounds.width + "x" + bounds.height);
}
```

Notice that `Group` is itself `Drawable`, so groups can contain groups. This is the *composite pattern* — treating individuals and compositions uniformly through polymorphism.

---

## The Three Languages

**ViperLang**
```viper
interface Animal {
    func speak();
}

class Dog implements Animal {
    func speak() { ... }
}

let animals: [Animal] = [Dog(), Cat()];
for a in animals {
    a.speak();
}
```

**BASIC**
```basic
INTERFACE Animal
    SUB Speak()
END INTERFACE

CLASS Dog IMPLEMENTS Animal
    SUB Speak()
        ...
    END SUB
END CLASS

DIM animals() AS Animal = [NEW Dog(), NEW Cat()]
FOR EACH a IN animals
    a.Speak()
NEXT
```

**Pascal**
```pascal
type
    IAnimal = interface
        procedure Speak;
    end;

    TDog = class(TInterfacedObject, IAnimal)
        procedure Speak;
    end;

var
    animals: array of IAnimal;
    a: IAnimal;
begin
    animals := [TDog.Create, TCat.Create];
    for a in animals do
        a.Speak;
end.
```

---

## Benefits of Polymorphism

1. **Flexibility**: Add new types without changing existing code
2. **Simplicity**: Write one loop/function that handles many types
3. **Extensibility**: Design systems that can grow
4. **Testability**: Swap real implementations for test doubles
5. **Loose coupling**: Code depends on interfaces, not concrete types

---

## Summary

- *Polymorphism* means many forms — one interface, many implementations
- Use base classes or interfaces to treat different types uniformly
- *Dynamic dispatch* calls the right method based on actual type at runtime
- *Method overloading* provides different implementations for different parameter types
- Polymorphism enables factory patterns, callback systems, and plugin architectures
- Collections can hold mixed types through common interfaces
- The composite pattern treats individuals and groups uniformly

---

## Exercises

**Exercise 17.1**: Create a `Shape` hierarchy where you can store Circle, Rectangle, and Triangle in one array and call `area()` on each.

**Exercise 17.2**: Create an audio system: `SoundSource` interface with `play()`, implemented by `MusicTrack`, `SoundEffect`, `Ambient`. Write a mixer that plays multiple sources.

**Exercise 17.3**: Create a notification system: `Notifier` interface with `send(message)`, implemented by `EmailNotifier`, `SMSNotifier`, `PushNotifier`.

**Exercise 17.4**: Implement the composite pattern: create a `FileSystemItem` interface, with `File` and `Folder` implementations. Folders contain items and report their total size.

**Exercise 17.5**: Create a simple scene graph: `Node` with `children`, where each node can be `Visible` or `Invisible`, and drawing respects visibility.

**Exercise 17.6** (Challenge): Build a simple expression evaluator: `Expression` interface with `evaluate() -> f64`, implemented by `Number`, `Add`, `Multiply`, `Variable`. Create an expression tree and evaluate it.

---

*We've mastered the core of OOP. Next, we look at common patterns — recurring solutions to recurring problems that experienced programmers use every day.*

*[Continue to Chapter 18: Design Patterns →](18-patterns.md)*
