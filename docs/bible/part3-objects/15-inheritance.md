# Chapter 15: Inheritance

A dog is an animal. A savings account is a bank account. A button is a UI element. In the real world, things often have "is-a" relationships - one thing is a specialized version of another. When we say "a dog is an animal," we mean that dogs share all the fundamental characteristics of animals (they breathe, they move, they eat) while also having their own unique traits (they bark, they fetch, they wag their tails).

*Inheritance* lets you model these relationships in code. You create a base entity with common functionality, then create derived entities that extend or customize it. When used well, inheritance promotes code reuse, creates logical hierarchies, and makes your programs easier to understand and maintain. When misused, it can create tangled dependencies and rigid code that's hard to change.

This chapter will teach you not just *how* to use inheritance, but *when* to use it - and equally important, when not to.

---

## The Problem Without Inheritance

Imagine building a game with different enemies. You start with a goblin:

```rust
entity Goblin {
    x: f64;
    y: f64;
    health: i64;
    name: string;

    expose func init(x: f64, y: f64) {
        self.x = x;
        self.y = y;
        self.health = 30;
        self.name = "Goblin";
    }

    func move(dx: f64, dy: f64) {
        self.x += dx;
        self.y += dy;
    }

    func takeDamage(amount: i64) {
        self.health -= amount;
        if self.health <= 0 {
            Viper.Terminal.Say(self.name + " has been defeated!");
        }
    }

    func attack() -> i64 {
        return 5;
    }
}
```

Great! Now you need an orc:

```rust
entity Orc {
    x: f64;
    y: f64;
    health: i64;
    name: string;

    expose func init(x: f64, y: f64) {
        self.x = x;
        self.y = y;
        self.health = 50;
        self.name = "Orc";
    }

    func move(dx: f64, dy: f64) {
        self.x += dx;
        self.y += dy;
    }

    func takeDamage(amount: i64) {
        self.health -= amount;
        if self.health <= 0 {
            Viper.Terminal.Say(self.name + " has been defeated!");
        }
    }

    func attack() -> i64 {
        return 10;
    }
}
```

Do you see the problem? These entities are almost identical! The same fields, the same `move` method, the same `takeDamage` method. Only the initial values and attack power differ.

Now imagine you need to add dragons, skeletons, trolls, and giant spiders. Each one copies the same code. And when you discover a bug in `takeDamage` or want to add a new feature to movement, you have to change every single enemy entity. Miss one, and you have inconsistent behavior. This duplication is wasteful, error-prone, and a maintenance nightmare.

There has to be a better way.

---

## Understanding the "Is-A" Relationship

Before we dive into the solution, let's think about what these entities have in common. A goblin, an orc, a dragon - they're all *enemies*. In fact, we could say:

- A Goblin *is an* Enemy
- An Orc *is an* Enemy
- A Dragon *is an* Enemy

This "is-a" relationship is the key insight behind inheritance. When you can truthfully say "X is a Y," that's a signal that inheritance might be appropriate. The specialized type (Goblin) shares all the characteristics of the general type (Enemy) while adding its own specific traits.

This isn't just a programming trick - it reflects how we naturally categorize the world. A poodle is a dog. A dog is a mammal. A mammal is an animal. Each level adds specificity while inheriting the properties of the levels above.

---

## Inheritance to the Rescue

With inheritance, we extract the common parts into a *base entity*:

```rust
entity Enemy {
    x: f64;
    y: f64;
    health: i64;
    name: string;

    expose func init(x: f64, y: f64, health: i64, name: string) {
        self.x = x;
        self.y = y;
        self.health = health;
        self.name = name;
    }

    func move(dx: f64, dy: f64) {
        self.x += dx;
        self.y += dy;
    }

    func takeDamage(amount: i64) {
        self.health -= amount;
        if self.health <= 0 {
            Viper.Terminal.Say(self.name + " has been defeated!");
        }
    }

    func attack() -> i64 {
        return 1;  // Default damage
    }
}
```

Now we can create specialized enemies that *extend* this base:

```rust
entity Goblin extends Enemy {
    expose func init(x: f64, y: f64) {
        super(x, y, 30, "Goblin");
    }

    func attack() -> i64 {
        return 5;
    }
}

entity Orc extends Enemy {
    expose func init(x: f64, y: f64) {
        super(x, y, 50, "Orc");
    }

    func attack() -> i64 {
        return 10;
    }
}

entity Dragon extends Enemy {
    expose func init(x: f64, y: f64) {
        super(x, y, 200, "Dragon");
    }

    func attack() -> i64 {
        return 35;
    }

    func breatheFire() {
        Viper.Terminal.Say("The dragon unleashes a torrent of flame!");
    }
}
```

Look how much cleaner this is! Each specialized enemy only defines what makes it unique. The common functionality lives in one place (Enemy), so fixing a bug or adding a feature only requires changing one entity.

---

## The Parent-Child Relationship

When one entity extends another, we create a *parent-child* relationship (also called a *superclass-subclass* relationship):

**Parent entity (also called base entity or superclass):** The entity being inherited from. In our example, `Enemy` is the parent.

**Child entity (also called derived entity or subclass):** The entity that inherits. `Goblin`, `Orc`, and `Dragon` are children of `Enemy`.

The `extends` keyword establishes this relationship:

```rust
entity Goblin extends Enemy {
    // Goblin is the child, Enemy is the parent
}
```

### What Gets Inherited

When an entity extends another, it automatically receives:

1. **All fields from the parent** - A Goblin has `x`, `y`, `health`, and `name` even though we didn't declare them in Goblin.

2. **All methods from the parent** - A Goblin can `move()` and `takeDamage()` even though we didn't define those methods in Goblin.

Let's see this in action:

```rust
var goblin = Goblin(10.0, 20.0);

// Using inherited fields
Viper.Terminal.Say(goblin.x);       // 10.0
Viper.Terminal.Say(goblin.name);    // "Goblin"

// Using inherited methods
goblin.move(5.0, 0.0);
Viper.Terminal.Say(goblin.x);       // 15.0

goblin.takeDamage(10);
Viper.Terminal.Say(goblin.health);  // 20

// Using overridden method
Viper.Terminal.Say(goblin.attack()); // 5 (not 1!)
```

The goblin can do everything an enemy can do, plus whatever specialized behavior we add.

### What Gets Overridden

A child entity can *override* methods from its parent - providing its own implementation that replaces the parent's. In our example, `Goblin` overrides `attack()` to return 5 instead of the parent's default of 1.

A child entity can also *add* new fields and methods that don't exist in the parent. The `Dragon` entity adds a `breatheFire()` method that only dragons have.

```rust
var dragon = Dragon(0.0, 0.0);
dragon.breatheFire();  // Works!

var goblin = Goblin(0.0, 0.0);
goblin.breatheFire();  // Error! Goblins can't breathe fire
```

---

## The `super` Keyword

The `super` keyword is how child entities communicate with their parent. It has two important uses.

### Calling the Parent Initializer

When you create a child entity, you typically need to initialize the parent's fields. The `super()` call in an initializer invokes the parent's initializer:

```rust
entity Enemy {
    x: f64;
    y: f64;
    health: i64;
    name: string;

    expose func init(x: f64, y: f64, health: i64, name: string) {
        self.x = x;
        self.y = y;
        self.health = health;
        self.name = name;
    }
}

entity Goblin extends Enemy {
    expose func init(x: f64, y: f64) {
        super(x, y, 30, "Goblin");  // Call Enemy's initializer
    }
}
```

Here, `Goblin`'s initializer takes only `x` and `y` (the goblin's position), then calls `super()` with those plus the goblin-specific values for health and name. This pattern is common - child initializers often have simpler signatures because some values are predetermined.

Think of it like filling out a form. The parent entity says "I need these four pieces of information." The child entity says "I already know two of them, so I only need to ask for the other two."

### Calling Parent Methods

Sometimes you want to *extend* the parent's behavior rather than completely replace it. The `super.methodName()` syntax lets you call the parent's version of a method:

```rust
entity Enemy {
    func describe() {
        Viper.Terminal.Say("An enemy named " + self.name);
        Viper.Terminal.Say("  Position: (" + self.x + ", " + self.y + ")");
        Viper.Terminal.Say("  Health: " + self.health);
    }
}

entity Dragon extends Enemy {
    fireBreaths: i64;

    expose func init(x: f64, y: f64) {
        super(x, y, 200, "Dragon");
        self.fireBreaths = 3;
    }

    func describe() {
        super.describe();  // First, do everything Enemy.describe() does
        Viper.Terminal.Say("  Fire breaths remaining: " + self.fireBreaths);
    }
}

var dragon = Dragon(100.0, 50.0);
dragon.describe();
// An enemy named Dragon
//   Position: (100, 50)
//   Health: 200
//   Fire breaths remaining: 3
```

By calling `super.describe()` first, the dragon gets all of the enemy's standard description, then adds its own dragon-specific information. This is much better than copying the parent's code - if the parent's `describe()` changes, the dragon automatically gets the update.

This pattern - doing what the parent does, then adding more - is extremely common and useful. You'll see it frequently in real code.

---

## Overriding Methods: When and Why

*Overriding* is when a child entity provides its own implementation of a method that exists in the parent. The child's version *replaces* the parent's version (for instances of the child type).

### Why Override?

You override methods when the child needs different behavior than the parent provides. Common reasons include:

1. **Specialization** - The child does the same thing, but differently. A dragon's attack deals more damage than the default.

2. **Extension** - The child does everything the parent does, plus more. The dragon's `describe()` adds fire breath information.

3. **Correction** - The parent's default behavior isn't appropriate for this child. Perhaps most enemies make a sound when attacked, but ghosts are silent.

### The `override` Keyword

In Viper, when you define a method in a child entity with the same name as a parent method, it automatically overrides. However, for clarity and safety, you can use the `override` keyword to make your intentions explicit:

```rust
entity Enemy {
    func attack() -> i64 {
        return 1;
    }
}

entity Orc extends Enemy {
    override func attack() -> i64 {
        return 10;
    }
}
```

Using `override` is good practice because:

1. **It documents your intent** - Anyone reading the code immediately knows this method exists in the parent.

2. **It catches errors** - If you misspell the method name or the parent's method signature changes, the compiler can warn you that your "override" doesn't actually override anything.

3. **It makes refactoring safer** - When you rename a method in the parent, you'll get errors showing you all the places that were overriding it.

### Accidental Overriding

Without the `override` keyword, you might accidentally override a parent method:

```rust
entity Vehicle {
    func turn(degrees: f64) {
        // Rotate the vehicle
    }
}

entity Car extends Vehicle {
    func turn(degrees: f64) {
        // Oops! We meant to add a new method for turn signals
        // But we accidentally overrode the steering!
    }
}
```

Using `override` explicitly helps catch these mistakes. If you add `override` but there's no parent method to override, the compiler tells you. If you meant to create a new method, you know you need a different name.

---

## Inheritance Hierarchies

Inheritance can extend multiple levels, creating a *hierarchy* or *tree* of related entities:

```rust
entity Animal {
    name: string;

    func breathe() {
        Viper.Terminal.Say(self.name + " is breathing");
    }

    func speak() {
        Viper.Terminal.Say("...");
    }
}

entity Mammal extends Animal {
    furColor: string;

    func nurse() {
        Viper.Terminal.Say(self.name + " is nursing its young");
    }
}

entity Dog extends Mammal {
    breed: string;

    expose func init(name: string, breed: string) {
        self.name = name;
        self.breed = breed;
        self.furColor = "brown";
    }

    override func speak() {
        Viper.Terminal.Say(self.name + " says: Woof!");
    }

    func fetch() {
        Viper.Terminal.Say(self.name + " fetches the ball");
    }
}
```

A `Dog` inherits from `Mammal`, which inherits from `Animal`. This means a dog has:
- From `Animal`: `name`, `breathe()`, `speak()` (overridden)
- From `Mammal`: `furColor`, `nurse()`
- Its own: `breed`, `fetch()`

```rust
var fido = Dog("Fido", "Golden Retriever");
fido.breathe();  // From Animal: "Fido is breathing"
fido.nurse();    // From Mammal: "Fido is nursing its young"
fido.speak();    // Overridden: "Fido says: Woof!"
fido.fetch();    // Dog's own: "Fido fetches the ball"
```

### When Hierarchies Get Too Deep

While hierarchies can extend as deep as you need, deeper isn't always better. Consider this hierarchy:

```
Entity
  └── LivingThing
        └── Animal
              └── Vertebrate
                    └── Mammal
                          └── Carnivore
                                └── Feline
                                      └── DomesticCat
                                            └── Tabby
                                                  └── OrangeTabby
                                                        └── OrangeTabbyWithWhitePaws
```

This eleven-level hierarchy has several problems:

1. **Hard to understand** - To understand what an OrangeTabbyWithWhitePaws can do, you need to examine eleven different entity definitions.

2. **Fragile** - A change to any level might affect everything below it. Changing Carnivore could break your OrangeTabbyWithWhitePaws.

3. **Inflexible** - What if you have a cat that's both a house pet and a therapy animal? The rigid hierarchy doesn't accommodate this.

4. **Wasted levels** - Many of these levels probably don't add meaningful behavior. They exist "just in case" rather than serving a real purpose.

**A good rule of thumb: Keep hierarchies to three levels or fewer when possible.** If you find yourself going deeper, ask whether composition (having objects contain other objects) might work better than inheritance.

---

## A Complete Example: Shapes

Let's build a classic example that demonstrates inheritance well - geometric shapes:

```rust
module Shapes;

entity Shape {
    x: f64;
    y: f64;

    expose func init(x: f64, y: f64) {
        self.x = x;
        self.y = y;
    }

    func area() -> f64 {
        return 0.0;  // Base shapes have no area
    }

    func perimeter() -> f64 {
        return 0.0;  // Base shapes have no perimeter
    }

    func describe() {
        Viper.Terminal.Say("Shape at (" + self.x + ", " + self.y + ")");
    }

    func move(dx: f64, dy: f64) {
        self.x += dx;
        self.y += dy;
    }
}

entity Rectangle extends Shape {
    width: f64;
    height: f64;

    expose func init(x: f64, y: f64, width: f64, height: f64) {
        super(x, y);
        self.width = width;
        self.height = height;
    }

    override func area() -> f64 {
        return self.width * self.height;
    }

    override func perimeter() -> f64 {
        return 2.0 * (self.width + self.height);
    }

    override func describe() {
        super.describe();
        Viper.Terminal.Say("  Type: Rectangle");
        Viper.Terminal.Say("  Dimensions: " + self.width + " x " + self.height);
        Viper.Terminal.Say("  Area: " + self.area());
        Viper.Terminal.Say("  Perimeter: " + self.perimeter());
    }
}

entity Circle extends Shape {
    radius: f64;

    expose func init(x: f64, y: f64, radius: f64) {
        super(x, y);
        self.radius = radius;
    }

    override func area() -> f64 {
        return Viper.Math.PI * self.radius * self.radius;
    }

    override func perimeter() -> f64 {
        return 2.0 * Viper.Math.PI * self.radius;
    }

    override func describe() {
        super.describe();
        Viper.Terminal.Say("  Type: Circle");
        Viper.Terminal.Say("  Radius: " + self.radius);
        Viper.Terminal.Say("  Area: " + self.area());
        Viper.Terminal.Say("  Circumference: " + self.perimeter());
    }

    func diameter() -> f64 {
        return self.radius * 2.0;
    }
}

entity Square extends Rectangle {
    expose func init(x: f64, y: f64, size: f64) {
        super(x, y, size, size);
    }

    override func describe() {
        Viper.Terminal.Say("Shape at (" + self.x + ", " + self.y + ")");
        Viper.Terminal.Say("  Type: Square");
        Viper.Terminal.Say("  Size: " + self.width);
        Viper.Terminal.Say("  Area: " + self.area());
        Viper.Terminal.Say("  Perimeter: " + self.perimeter());
    }
}

func start() {
    var rect = Rectangle(0.0, 0.0, 10.0, 5.0);
    var circle = Circle(20.0, 20.0, 7.0);
    var square = Square(10.0, 10.0, 4.0);

    rect.describe();
    Viper.Terminal.Say("");

    circle.describe();
    Viper.Terminal.Say("");

    square.describe();
    Viper.Terminal.Say("");

    // All shapes can move - inherited unchanged
    rect.move(5.0, 5.0);
    circle.move(-3.0, 2.0);
    square.move(1.0, 1.0);

    Viper.Terminal.Say("After moving:");
    Viper.Terminal.Say("Rectangle is at (" + rect.x + ", " + rect.y + ")");
    Viper.Terminal.Say("Circle is at (" + circle.x + ", " + circle.y + ")");
    Viper.Terminal.Say("Square is at (" + square.x + ", " + square.y + ")");
}
```

Notice several things about this design:

- **Shared behavior**: All shapes inherit `move()` without changes.
- **Specialized behavior**: Each shape provides its own `area()`, `perimeter()`, and `describe()`.
- **Extended behavior**: `describe()` calls `super.describe()` to get common functionality, then adds specifics.
- **Additional behavior**: Circle adds `diameter()` which only circles have.
- **Multi-level inheritance**: Square extends Rectangle (a square is a rectangle where width equals height).

---

## Common Inheritance Patterns

Certain patterns appear repeatedly when using inheritance. Learning to recognize them will help you design better hierarchies.

### The Template Method Pattern

In this pattern, the parent defines the *structure* of an algorithm, but lets children fill in specific steps:

```rust
entity Report {
    title: string;

    func generate() {
        self.printHeader();
        self.printContent();
        self.printFooter();
    }

    func printHeader() {
        Viper.Terminal.Say("=== " + self.title + " ===");
        Viper.Terminal.Say("");
    }

    func printContent() {
        // Children override this
        Viper.Terminal.Say("(No content)");
    }

    func printFooter() {
        Viper.Terminal.Say("");
        Viper.Terminal.Say("=== End of Report ===");
    }
}

entity SalesReport extends Report {
    totalSales: f64;
    itemsSold: i64;

    expose func init(sales: f64, items: i64) {
        self.title = "Sales Report";
        self.totalSales = sales;
        self.itemsSold = items;
    }

    override func printContent() {
        Viper.Terminal.Say("Total Sales: $" + self.totalSales);
        Viper.Terminal.Say("Items Sold: " + self.itemsSold);
        Viper.Terminal.Say("Average Price: $" + (self.totalSales / self.itemsSold));
    }
}

entity InventoryReport extends Report {
    items: i64;
    lowStock: i64;

    expose func init(items: i64, lowStock: i64) {
        self.title = "Inventory Report";
        self.items = items;
        self.lowStock = lowStock;
    }

    override func printContent() {
        Viper.Terminal.Say("Total Items: " + self.items);
        Viper.Terminal.Say("Low Stock Alerts: " + self.lowStock);
        if self.lowStock > 5 {
            Viper.Terminal.Say("WARNING: Many items need restocking!");
        }
    }
}
```

The parent (`Report`) defines the template: header, then content, then footer. Children only need to override `printContent()` to create their specialized reports. The structure is consistent; only the content varies.

### The Specialization Pattern

This pattern creates progressively more specific versions of a concept:

```rust
entity Account {
    balance: f64;
    accountNumber: string;

    func deposit(amount: f64) {
        self.balance += amount;
    }

    func withdraw(amount: f64) -> bool {
        if amount <= self.balance {
            self.balance -= amount;
            return true;
        }
        return false;
    }
}

entity SavingsAccount extends Account {
    interestRate: f64;

    expose func init(accountNumber: string, initialDeposit: f64, rate: f64) {
        self.accountNumber = accountNumber;
        self.balance = initialDeposit;
        self.interestRate = rate;
    }

    func addInterest() {
        var interest = self.balance * self.interestRate;
        self.deposit(interest);
        Viper.Terminal.Say("Added $" + interest + " in interest");
    }

    override func withdraw(amount: f64) -> bool {
        // Savings accounts might have minimum balance requirements
        if self.balance - amount < 100.0 {
            Viper.Terminal.Say("Cannot withdraw: would go below $100 minimum");
            return false;
        }
        return super.withdraw(amount);
    }
}

entity CheckingAccount extends Account {
    overdraftLimit: f64;

    expose func init(accountNumber: string, initialDeposit: f64, limit: f64) {
        self.accountNumber = accountNumber;
        self.balance = initialDeposit;
        self.overdraftLimit = limit;
    }

    override func withdraw(amount: f64) -> bool {
        // Checking accounts can overdraft up to a limit
        if amount <= self.balance + self.overdraftLimit {
            self.balance -= amount;
            if self.balance < 0.0 {
                Viper.Terminal.Say("Warning: Account overdrawn by $" + (-self.balance));
            }
            return true;
        }
        Viper.Terminal.Say("Cannot withdraw: would exceed overdraft limit");
        return false;
    }
}
```

Each child specializes the parent for a particular use case. SavingsAccount adds interest and minimum balance requirements. CheckingAccount allows overdrafts. Both are still accounts - they can deposit and withdraw - but with different rules.

---

## Inheritance vs Composition: Knowing When to Choose

Inheritance is a powerful tool, but it's not always the right tool. *Composition* - having one entity contain instances of other entities - is often a better choice.

### The "Is-A" vs "Has-A" Test

Use inheritance for "is-a" relationships:
- A Dog *is an* Animal - Use inheritance
- A SavingsAccount *is a* BankAccount - Use inheritance
- A Button *is a* UIElement - Use inheritance

Use composition for "has-a" relationships:
- A Car *has an* Engine - Use composition
- A Person *has an* Address - Use composition
- A Computer *has a* CPU - Use composition

```rust
// WRONG: A car is not a type of engine!
entity Car extends Engine {
    // This makes no sense
}

// RIGHT: A car contains an engine
entity Car {
    engine: Engine;
    transmission: Transmission;
    wheels: array<Wheel>;

    func start() {
        self.engine.ignite();
    }
}
```

### When Composition Is Better

Even when "is-a" seems to apply, composition might still be better. Here are signs that composition is the right choice:

**1. You only need part of the parent's functionality**

```rust
// If Rectangle only needs the position from Shape, not area/perimeter methods:
entity Rectangle {
    position: Point;  // Composition
    width: f64;
    height: f64;
}
```

**2. You need to change behavior at runtime**

Inheritance relationships are fixed at compile time. Composition relationships can change:

```rust
entity Character {
    weapon: Weapon;  // Can swap weapons during the game

    func attack() {
        self.weapon.use();
    }

    func equipWeapon(newWeapon: Weapon) {
        self.weapon = newWeapon;  // Behavior changes!
    }
}
```

With inheritance, a `SwordCharacter` is always a sword character. With composition, a character can switch from sword to bow to magic staff.

**3. You need functionality from multiple sources**

Viper (like many languages) only allows inheriting from one parent. But you can compose as many entities as you want:

```rust
// Can't do this:
entity FlyingCar extends Car, Aircraft { }  // Error!

// Do this instead:
entity FlyingCar {
    carParts: CarMechanics;
    flightSystem: AircraftControls;

    func drive() {
        self.carParts.operate();
    }

    func fly() {
        self.flightSystem.operate();
    }
}
```

**4. The hierarchy doesn't feel natural**

If you find yourself creating odd inheritance relationships to reuse code, that's a sign to use composition:

```rust
// Awkward: A Window is not really a Rectangle in the UI sense
entity Window extends Rectangle { }

// Better: A Window has a rectangular frame
entity Window {
    frame: Rectangle;
    title: string;
    content: View;
}
```

### The Classic Example: Stack and Array

Should a Stack extend Array? After all, a stack uses array-like storage...

```rust
// Tempting but WRONG
entity Stack extends Array {
    func push(item) {
        self.append(item);
    }

    func pop() {
        return self.removeLast();
    }
}
```

This seems convenient, but now Stack inherits ALL of Array's methods. Users can call `stack.insertAt(index)` or `stack.removeAt(index)`, which violates how stacks work. A stack is not an array - it just uses an array internally.

```rust
// RIGHT: Stack contains an array
entity Stack {
    items: array<any>;

    expose func init() {
        self.items = [];
    }

    func push(item) {
        self.items.append(item);
    }

    func pop() {
        return self.items.removeLast();
    }

    func peek() {
        return self.items[self.items.length - 1];
    }

    func isEmpty() -> bool {
        return self.items.length == 0;
    }
}
```

Now Stack only exposes stack operations. The internal array is hidden.

---

## Design Principles for Inheritance

Using inheritance well requires understanding some fundamental principles. These will help you avoid common pitfalls.

### The Liskov Substitution Principle (In Plain Language)

Named after computer scientist Barbara Liskov, this principle says:

> **If you have code that works with a parent type, it should also work correctly with any child type.**

In other words, you should be able to substitute a child object anywhere a parent object is expected, and everything should still work correctly.

Let's see what this means practically:

```rust
func processEnemy(enemy: Enemy) {
    enemy.move(10.0, 0.0);
    var damage = enemy.attack();
    enemy.takeDamage(5);
    // This code should work with ANY enemy - Goblin, Orc, Dragon, etc.
}

// All of these should work:
processEnemy(Goblin(0.0, 0.0));   // Works!
processEnemy(Orc(0.0, 0.0));      // Works!
processEnemy(Dragon(0.0, 0.0));   // Works!
```

This seems obvious, but it's easy to violate. Consider:

```rust
entity Bird {
    func fly() {
        Viper.Terminal.Say("Flying through the air");
    }
}

entity Penguin extends Bird {
    override func fly() {
        // Penguins can't fly!
        Viper.Terminal.Say("Error: Penguins cannot fly");
        // Or worse: crash the program
    }
}

func migrateSouth(bird: Bird) {
    bird.fly();  // Expects this to work!
}

migrateSouth(Penguin(...));  // Broken! Violates substitutability
```

The problem: Penguin inherits from Bird, but can't fulfill Bird's contract. Code that expects birds to fly will break with penguins.

Better designs:
- Don't make Penguin extend Bird
- Have Bird and Penguin both extend a non-flying Animal
- Use interfaces to separate "things that fly" from "things that are birds"

### The Fragile Base Class Problem

The *fragile base class problem* occurs when changes to a parent class break child classes in unexpected ways.

```rust
entity MediaPlayer {
    func play() {
        self.preparePlayback();
        self.startPlayback();
    }

    func preparePlayback() {
        Viper.Terminal.Say("Preparing...");
    }

    func startPlayback() {
        Viper.Terminal.Say("Playing...");
    }
}

entity VideoPlayer extends MediaPlayer {
    override func play() {
        self.loadVideo();
        super.play();
    }

    func loadVideo() {
        Viper.Terminal.Say("Loading video...");
    }
}
```

This works fine. But what if someone modifies MediaPlayer?

```rust
entity MediaPlayer {
    func play() {
        // Changed: now calls preparePlayback differently
        self.startPlayback();  // Moved before prepare!
        self.preparePlayback();
    }
    // ...
}
```

Suddenly VideoPlayer might break. Its author assumed a certain order of operations that no longer holds.

**Ways to avoid the fragile base class problem:**

1. **Keep inheritance hierarchies shallow** - Fewer levels mean fewer places for breakage.

2. **Prefer composition** - Objects that contain other objects are less affected by changes.

3. **Document what children can rely on** - If children override methods, document which behaviors are guaranteed to be preserved.

4. **Favor overriding "hooks" rather than core methods** - Provide specific extension points rather than letting children override anything.

---

## The Three Languages

Different Viper language styles express inheritance differently:

**ViperLang**
```rust
entity Animal {
    name: string;

    func speak() {
        Viper.Terminal.Say("...");
    }
}

entity Dog extends Animal {
    expose func init(name: string) {
        self.name = name;
    }

    override func speak() {
        Viper.Terminal.Say(self.name + " says: Woof!");
    }
}
```

**BASIC**
```basic
CLASS Animal
    DIM Name AS STRING

    SUB Speak()
        PRINT "..."
    END SUB
END CLASS

CLASS Dog EXTENDS Animal
    SUB Init(n AS STRING)
        Name = n
    END SUB

    SUB Speak()
        PRINT Name + " says: Woof!"
    END SUB
END CLASS
```

**Pascal**
```pascal
type
    Animal = class
        name: string;
        procedure Speak; virtual;
    end;

    Dog = class(Animal)
        constructor Init(n: string);
        procedure Speak; override;
    end;

procedure Animal.Speak;
begin
    WriteLn('...');
end;

constructor Dog.Init(n: string);
begin
    name := n;
end;

procedure Dog.Speak;
begin
    WriteLn(name, ' says: Woof!');
end;
```

Note that Pascal requires explicit `virtual` on base methods (indicating they can be overridden) and `override` on derived methods. This extra verbosity catches errors at compile time.

---

## Guidelines for Good Inheritance

Here are practical guidelines for using inheritance effectively:

### Do

- **Use inheritance for genuine "is-a" relationships** where the child truly is a specialized version of the parent.

- **Keep hierarchies shallow** - Aim for 2-3 levels maximum in most cases.

- **Override methods to specialize, not to disable** - If you're overriding a method to do nothing or throw an error, your hierarchy is probably wrong.

- **Call `super` when extending behavior** - Add to the parent's functionality rather than completely replacing it when appropriate.

- **Use the `override` keyword** to make your intentions clear and catch errors.

- **Document what children can rely on** - If the parent provides methods children might override, explain what guarantees they can depend on.

### Don't

- **Don't use inheritance just to reuse code** - If there's no "is-a" relationship, use composition instead.

- **Don't inherit from a class just because it has a method you need** - Copy the code or use composition.

- **Don't create deep hierarchies** - They're hard to understand and maintain.

- **Don't override methods to completely change their meaning** - A bird's `fly()` should still be about flying, not swimming.

- **Don't expose internal implementation through inheritance** - Children shouldn't depend on parent's private details.

---

## Summary

Inheritance is a fundamental object-oriented concept that lets you model "is-a" relationships:

- Use `extends` to create a child entity that inherits from a parent
- Child entities automatically receive all fields and methods from the parent
- Use `super()` to call the parent's initializer
- Use `super.methodName()` to call the parent's version of a method
- Override methods to provide specialized behavior for the child
- Use the `override` keyword to make overriding explicit and catch errors

Inheritance should be used thoughtfully:

- Only use it for true "is-a" relationships
- Keep hierarchies shallow (2-3 levels)
- Prefer composition for "has-a" relationships
- Respect the Liskov Substitution Principle - children should be substitutable for parents
- Be aware of the fragile base class problem

Inheritance is powerful but easy to misuse. When you're unsure, composition is usually the safer choice. In the next chapter, we'll learn about interfaces, which provide another way to create relationships between entities - based on what they *can do* rather than what they *are*.

---

## Exercises

**Exercise 15.1 - Vehicle Hierarchy**: Create a `Vehicle` entity with `speed` and `position` fields, and `move()` and `stop()` methods. Create `Car` and `Bicycle` child entities with different speed limits and movement behaviors.

**Exercise 15.2 - Employee System**: Create an `Employee` base entity with `name`, `salary`, and `work()` method. Create `Manager` (has team members, can `holdMeeting()`) and `Developer` (has programming languages, can `writeCode()`) child entities with specialized `work()` implementations.

**Exercise 15.3 - Bank Accounts**: Create a `BankAccount` base entity with `balance`, `deposit()`, and `withdraw()`. Create `SavingsAccount` (earns interest, has minimum balance) and `CheckingAccount` (allows overdraft up to a limit) with appropriate specializations.

**Exercise 15.4 - Shape Extensions**: Extend the Shapes example with `Triangle` (given three side lengths) and `Ellipse` (given two radii) entities.

**Exercise 15.5 - Template Method**: Create a `Game` entity with a `play()` method that calls `setup()`, `playRound()` (in a loop), and `declareWinner()`. Create `TicTacToe` and `GuessTheNumber` games that override these methods.

**Exercise 15.6 - Composition Refactoring**: Take the following (bad) inheritance hierarchy and refactor it to use composition:
```rust
entity Person { }
entity Employee extends Person { }
entity Manager extends Employee { }
entity CEOWithCompanyCar extends Manager { }  // This is getting silly!
```

**Exercise 15.7 (Challenge) - Game Enemy System**: Design and implement an enemy system for a game with:
- Base `Enemy` with position, health, and basic attack
- `MeleeEnemy` and `RangedEnemy` specializations
- Specific enemies: `Knight`, `Berserker` (melee), `Archer`, `Mage` (ranged)
- Each enemy type should have unique abilities and appropriate `attack()` implementations
- Think carefully about your hierarchy depth!

**Exercise 15.8 (Design Exercise)**: For each scenario, decide whether inheritance or composition is more appropriate, and explain why:
- A `SmartPhone` that can make calls like a phone and browse the web like a computer
- A `Penguin` in a bird simulation program
- A `Square` in relation to `Rectangle`
- A `LoggingDatabaseConnection` that adds logging to database queries

---

*Inheritance creates "is-a" relationships and lets child entities specialize their parents. But what if you want to define what something can do, without specifying how? What if you want a function that can work with any entity that has a certain capability? Next, we learn about interfaces - contracts that entities can fulfill, enabling powerful abstractions.*

*[Continue to Chapter 16: Interfaces](16-interfaces.md)*
