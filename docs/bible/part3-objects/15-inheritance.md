# Chapter 15: Inheritance

A dog is an animal. A savings account is a bank account. A button is a UI element. In the real world, things often have "is-a" relationships — one thing is a specialized version of another.

*Inheritance* lets you model these relationships in code. You create a base class with common functionality, then create derived classes that extend or customize it. This promotes code reuse and creates logical hierarchies.

---

## The Problem Without Inheritance

Imagine building a game with different enemies:

```viper
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

    func attack() -> i64 {
        return 5;
    }
}

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

    func attack() -> i64 {
        return 10;
    }
}
```

These entities are almost identical! The same fields, the same `move` method. Only the initial values and attack power differ. This duplication is wasteful and error-prone.

---

## Inheritance to the Rescue

With inheritance, we extract the common parts into a base entity:

```viper
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

    func attack() -> i64 {
        return 1;  // Default damage
    }
}

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
```

Now `Goblin` and `Orc` *extend* `Enemy`. They inherit all of `Enemy`'s fields and methods, then add their own specifics.

---

## Key Concepts

**Base entity (parent, superclass):** The entity being inherited from (`Enemy`).

**Derived entity (child, subclass):** The entity that inherits (`Goblin`, `Orc`).

**extends:** The keyword that creates inheritance.

**super:** Calls the parent entity's initializer or methods.

```viper
entity Goblin extends Enemy {
    expose func init(x: f64, y: f64) {
        super(x, y, 30, "Goblin");  // Call Enemy's initializer
    }
}
```

---

## What Gets Inherited

A derived entity automatically has:
- All fields from the base entity
- All methods from the base entity

```viper
var goblin = Goblin(10.0, 20.0);
goblin.move(5.0, 0.0);           // Inherited from Enemy
Viper.Terminal.Say(goblin.x);     // 15.0 (inherited field)
Viper.Terminal.Say(goblin.name);  // "Goblin"
Viper.Terminal.Say(goblin.attack());  // 5 (overridden method)
```

The goblin can `move` even though Goblin doesn't define a `move` method — it inherits from Enemy.

---

## Overriding Methods

When a derived entity defines a method that exists in the base entity, it *overrides* that method:

```viper
entity Enemy {
    func attack() -> i64 {
        return 1;  // Base implementation
    }
}

entity Orc extends Enemy {
    func attack() -> i64 {
        return 10;  // Override: orcs hit harder
    }
}
```

When you call `attack()` on an Orc, you get the Orc's version (10), not the Enemy's (1).

---

## Calling the Parent's Method

Sometimes you want to extend, not replace, the parent's behavior:

```viper
entity Enemy {
    func describe() {
        Viper.Terminal.Say("An enemy at (" + self.x + ", " + self.y + ")");
    }
}

entity Orc extends Enemy {
    func describe() {
        super.describe();  // Call parent's describe first
        Viper.Terminal.Say("It's a fearsome orc!");
    }
}

var orc = Orc(5.0, 3.0);
orc.describe();
// An enemy at (5, 3)
// It's a fearsome orc!
```

`super.describe()` invokes the parent's version, then the orc adds its own line.

---

## A Deeper Hierarchy

Inheritance can go multiple levels:

```viper
entity Animal {
    name: string;

    func speak() {
        Viper.Terminal.Say("...");
    }
}

entity Mammal extends Animal {
    func giveBirth() {
        Viper.Terminal.Say(self.name + " gives birth to live young");
    }
}

entity Dog extends Mammal {
    expose func init(name: string) {
        self.name = name;
    }

    func speak() {
        Viper.Terminal.Say(self.name + " says: Woof!");
    }

    func fetch() {
        Viper.Terminal.Say(self.name + " fetches the ball");
    }
}
```

A `Dog` is a `Mammal`, which is an `Animal`. Dogs inherit from both and can:
- `speak()` (overridden)
- `giveBirth()` (from Mammal)
- `fetch()` (new in Dog)

---

## A Complete Example: Shapes

The classic inheritance example — geometric shapes:

```viper
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

    func area() -> f64 {
        return self.width * self.height;
    }

    func describe() {
        super.describe();
        Viper.Terminal.Say("  Rectangle: " + self.width + " x " + self.height);
        Viper.Terminal.Say("  Area: " + self.area());
    }
}

entity Circle extends Shape {
    radius: f64;

    expose func init(x: f64, y: f64, radius: f64) {
        super(x, y);
        self.radius = radius;
    }

    func area() -> f64 {
        return Viper.Math.PI * self.radius * self.radius;
    }

    func describe() {
        super.describe();
        Viper.Terminal.Say("  Circle: radius " + self.radius);
        Viper.Terminal.Say("  Area: " + self.area());
    }
}

func start() {
    var rect = Rectangle(0.0, 0.0, 10.0, 5.0);
    var circle = Circle(20.0, 20.0, 7.0);

    rect.describe();
    // Shape at (0, 0)
    //   Rectangle: 10 x 5
    //   Area: 50

    circle.describe();
    // Shape at (20, 20)
    //   Circle: radius 7
    //   Area: 153.938...

    rect.move(5.0, 5.0);
    rect.describe();
    // Shape at (5, 5)
    //   Rectangle: 10 x 5
    //   Area: 50
}
```

Both Rectangle and Circle share `move` (inherited unchanged), but have their own `area` and `describe` implementations.

---

## The Three Languages

**ViperLang**
```viper
entity Animal {
    func speak() { ... }
}

entity Dog extends Animal {
    func speak() {
        Viper.Terminal.Say("Woof!");
    }
}
```

**BASIC**
```basic
CLASS Animal
    SUB Speak()
        ...
    END SUB
END CLASS

CLASS Dog EXTENDS Animal
    SUB Speak()
        PRINT "Woof!"
    END SUB
END CLASS
```

**Pascal**
```pascal
type
    Animal = class
        procedure Speak; virtual;
    end;

    Dog = class(Animal)
        procedure Speak; override;
    end;

procedure Animal.Speak;
begin
    { ... }
end;

procedure Dog.Speak;
begin
    WriteLn('Woof!');
end;
```

Pascal requires `virtual` on the base method and `override` on derived methods.

---

## When to Use Inheritance

**Use inheritance for "is-a" relationships:**
- A Dog is an Animal ✓
- A SavingsAccount is a BankAccount ✓
- A Button is a UIElement ✓

**Don't use inheritance for "has-a" relationships:**
- A Car has an Engine ✗ (use composition instead)
- A Person has an Address ✗

```viper
// Wrong: Car is not a type of Engine
entity Car extends Engine { ... }

// Right: Car contains an Engine
entity Car {
    engine: Engine;
}
```

---

## Inheritance Pitfalls

**Deep hierarchies get confusing:**
```
Animal
  └── Mammal
        └── Carnivore
              └── Feline
                    └── HouseCat
                          └── TabbyWithWhitePaws
```

Prefer shallow hierarchies. If you're more than 3 levels deep, reconsider your design.

**Tight coupling:** Derived entities depend on base entity internals. Changing the base can break derived entities.

**Inflexibility:** You can only inherit from one entity. If you need behavior from multiple sources, you'll want interfaces (next chapter).

---

## Summary

- *Inheritance* lets entities extend other entities
- Use `extends` to create a derived entity
- Derived entities inherit fields and methods
- `super` calls the parent's initializer or methods
- Override methods to provide specialized behavior
- Use inheritance for "is-a" relationships
- Prefer shallow hierarchies
- Don't force inheritance where composition is more appropriate

---

## Exercises

**Exercise 15.1**: Create a `Vehicle` class with `speed` and `move()`. Create `Car` and `Bicycle` subclasses with different move implementations.

**Exercise 15.2**: Create an `Employee` class. Create `Manager` and `Developer` subclasses with different `work()` methods and additional fields.

**Exercise 15.3**: Create a `BankAccount` base class. Create `SavingsAccount` (adds interest) and `CheckingAccount` (allows overdraft) subclasses.

**Exercise 15.4**: Extend the Shapes example with `Triangle` and `Square` classes.

**Exercise 15.5**: Create an `Appliance` class hierarchy: `Appliance` → `KitchenAppliance` → `Toaster`, `Blender`, etc.

**Exercise 15.6** (Challenge): Create a simple game enemy hierarchy with different behaviors: `Enemy` → `RangedEnemy`, `MeleeEnemy`, each with subclasses like `Archer`, `Mage`, `Knight`, `Berserker`.

---

*Inheritance creates "is-a" relationships. But what if you want to define what something can do, without specifying how? Next, we learn about interfaces — contracts that classes can fulfill.*

*[Continue to Chapter 16: Interfaces →](16-interfaces.md)*
