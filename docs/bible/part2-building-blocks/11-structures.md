# Chapter 11: Structures

So far, we've used simple types: numbers, strings, booleans. We've grouped them in arrays. But what if you need to represent something more complex — a person with a name and age, a point with x and y coordinates, a product with name, price, and quantity?

You could use separate variables:
```rust
var personName = "Alice";
var personAge = 30;
var personEmail = "alice@example.com";
```

But this falls apart fast. What if you have 100 people? You can't make 300 variables. And passing a person to a function means passing three separate arguments.

*Structures* solve this. They let you create your own types that group related data together.

---

## Defining a Structure

A structure is a template for grouping values:

```rust
value Point {
    x: f64;
    y: f64;
}
```

This defines a new type called `Point` with two *fields*: `x` and `y`, both floating-point numbers.

Now you can create instances of this structure:

```rust
var origin = Point { x: 0.0, y: 0.0 };
var position = Point { x: 10.5, y: 20.3 };
```

Each `Point` bundles two values together. You access fields with dot notation:

```rust
Viper.Terminal.Say(position.x);  // 10.5
Viper.Terminal.Say(position.y);  // 20.3

position.x = 15.0;  // Modify the x field
```

---

## Why Structures Matter

Structures change how you think about data. Instead of managing separate pieces, you work with coherent wholes:

```rust
// Without structures: scattered data
var name1 = "Alice";
var age1 = 30;
var name2 = "Bob";
var age2 = 25;

// With structures: unified data
value Person {
    name: string;
    age: i64;
}

var alice = Person { name: "Alice", age: 30 };
var bob = Person { name: "Bob", age: 25 };
```

You can make arrays of structures:

```rust
var people: [Person] = [
    Person { name: "Alice", age: 30 },
    Person { name: "Bob", age: 25 },
    Person { name: "Carol", age: 35 }
];

for person in people {
    Viper.Terminal.Say(person.name + " is " + person.age);
}
```

You can pass structures to functions:

```rust
func greet(person: Person) {
    Viper.Terminal.Say("Hello, " + person.name + "!");
}

greet(alice);
greet(bob);
```

And return them from functions:

```rust
func createPerson(name: string, age: i64) -> Person {
    return Person { name: name, age: age };
}

var dave = createPerson("Dave", 40);
```

---

## Designing Good Structures

**Group what belongs together.** A `Person` has name, age, email. A `Rectangle` has width and height. A `Song` has title, artist, duration.

**Keep it focused.** If a structure has too many fields, maybe it should be split. A `Person` probably shouldn't contain `homeAddress`, `workAddress`, `bankAccount`, `medicalHistory` — those are separate concepts.

**Name fields clearly.** Use `firstName` not `fn`, `birthYear` not `by`.

---

## Nested Structures

Structures can contain other structures:

```rust
value Address {
    street: string;
    city: string;
    zipCode: string;
}

value Person {
    name: string;
    age: i64;
    address: Address;
}

var alice = Person {
    name: "Alice",
    age: 30,
    address: Address {
        street: "123 Main St",
        city: "Springfield",
        zipCode: "12345"
    }
};

Viper.Terminal.Say(alice.address.city);  // Springfield
```

This lets you model complex, hierarchical data cleanly.

---

## Structures with Methods

Structures can have functions attached to them:

```rust
value Rectangle {
    width: f64;
    height: f64;

    func area() -> f64 {
        return self.width * self.height;
    }

    func perimeter() -> f64 {
        return 2 * (self.width + self.height);
    }

    func scale(factor: f64) {
        self.width *= factor;
        self.height *= factor;
    }
}

var rect = Rectangle { width: 10.0, height: 5.0 };
Viper.Terminal.Say(rect.area());       // 50
Viper.Terminal.Say(rect.perimeter());  // 30

rect.scale(2.0);
Viper.Terminal.Say(rect.area());       // 200
```

Inside a method, `self` refers to the current instance. `self.width` is this rectangle's width.

Methods bundle behavior with data. Instead of `calculateArea(rect)`, you write `rect.area()`. This keeps related code together.

---

## A Complete Example: Game Entities

Let's model a simple game with structures:

```rust
module GameDemo;

value Vec2 {
    x: f64;
    y: f64;

    func add(other: Vec2) -> Vec2 {
        return Vec2 { x: self.x + other.x, y: self.y + other.y };
    }

    func distance(other: Vec2) -> f64 {
        var dx = other.x - self.x;
        var dy = other.y - self.y;
        return Viper.Math.sqrt(dx * dx + dy * dy);
    }
}

value Player {
    name: string;
    position: Vec2;
    health: i64;
    score: i64;

    func isAlive() -> bool {
        return self.health > 0;
    }

    func move(direction: Vec2) {
        self.position = self.position.add(direction);
    }

    func takeDamage(amount: i64) {
        self.health -= amount;
        if self.health < 0 {
            self.health = 0;
        }
    }
}

value Enemy {
    position: Vec2;
    damage: i64;

    func distanceToPlayer(player: Player) -> f64 {
        return self.position.distance(player.position);
    }
}

func start() {
    var player = Player {
        name: "Hero",
        position: Vec2 { x: 0.0, y: 0.0 },
        health: 100,
        score: 0
    };

    var enemy = Enemy {
        position: Vec2 { x: 5.0, y: 3.0 },
        damage: 10
    };

    Viper.Terminal.Say("Game Start!");
    Viper.Terminal.Say("Player: " + player.name);
    Viper.Terminal.Say("Health: " + player.health);

    // Simulate movement
    player.move(Vec2 { x: 1.0, y: 0.5 });
    Viper.Terminal.Say("Moved to: (" + player.position.x + ", " + player.position.y + ")");

    // Check distance to enemy
    var dist = enemy.distanceToPlayer(player);
    Viper.Terminal.Say("Distance to enemy: " + dist);

    // Take damage
    if dist < 3.0 {
        player.takeDamage(enemy.damage);
        Viper.Terminal.Say("Hit! Health: " + player.health);
    }

    if player.isAlive() {
        Viper.Terminal.Say("Player survives!");
    } else {
        Viper.Terminal.Say("Game Over!");
    }
}
```

This shows how structures model game concepts naturally. Each entity has its data and behaviors bundled together.

---

## The Three Languages

**ViperLang**
```rust
value Point {
    x: f64;
    y: f64;

    func distance(other: Point) -> f64 {
        var dx = other.x - self.x;
        var dy = other.y - self.y;
        return Viper.Math.sqrt(dx * dx + dy * dy);
    }
}

var p = Point { x: 3.0, y: 4.0 };
Viper.Terminal.Say(p.x);
```

**BASIC**
```basic
TYPE Point
    x AS DOUBLE
    y AS DOUBLE
END TYPE

DIM p AS Point
p.x = 3.0
p.y = 4.0

PRINT p.x
```

BASIC uses `TYPE` to define structures and doesn't support methods directly — you use regular SUBs and FUNCTIONs instead.

**Pascal**
```pascal
type
    Point = record
        x: Double;
        y: Double;
    end;

var p: Point;
begin
    p.x := 3.0;
    p.y := 4.0;
    WriteLn(p.x);
end.
```

Pascal uses `record` for structures. Methods require using objects (covered in Part III).

---

## Structures vs. Classes

Structures are great for simple data containers. But they have limitations:
- No inheritance (can't create specialized versions)
- No polymorphism (can't treat different types uniformly)
- Methods are simple (no virtual dispatch)

For more complex needs, we use *classes*, which we'll cover in Part III. For now, structures handle most cases.

---

## Common Patterns

### Factory function
```rust
func createPoint(x: f64, y: f64) -> Point {
    return Point { x: x, y: y };
}

var p = createPoint(3.0, 4.0);
```

### Default values
```rust
value Config {
    volume: i64;
    difficulty: string;
}

func defaultConfig() -> Config {
    return Config { volume: 50, difficulty: "normal" };
}
```

### Updating fields
```rust
var person = Person { name: "Alice", age: 30 };
person.age = 31;  // Happy birthday!
```

### Comparing structures
```rust
func pointsEqual(a: Point, b: Point) -> bool {
    return a.x == b.x && a.y == b.y;
}
```

---

## Common Mistakes

**Forgetting to initialize all fields:**
```rust
var p = Point { x: 5.0 };  // Error: y is not initialized
var p = Point { x: 5.0, y: 0.0 };  // Correct
```

**Confusing the type and an instance:**
```rust
Point.x = 5.0;  // Wrong: Point is the type, not an instance
var p = Point { x: 5.0, y: 3.0 };  // Create an instance
p.x = 5.0;  // Now you can access fields
```

**Copying when you want to modify:**
```rust
func birthday(person: Person) {
    person.age += 1;  // Modifies a copy, not the original!
}

var alice = Person { name: "Alice", age: 30 };
birthday(alice);
Viper.Terminal.Say(alice.age);  // Still 30!
```

When structures are passed to functions, they may be copied. To modify the original, return the modified version or use references (covered later).

---

## Summary

- Structures group related data under one name
- Define with `value Name { fields... }`
- Create instances with `TypeName { field: value, ... }`
- Access fields with `instance.field`
- Methods are functions defined inside structures, using `self`
- Structures can be nested inside other structures
- Use structures to model entities in your programs
- Keep structures focused on one concept

---

## Exercises

**Exercise 11.1**: Create a `Book` structure with title, author, and pageCount. Create an array of 3 books and print them.

**Exercise 11.2**: Create a `Circle` structure with radius. Add methods for `area()` and `circumference()`.

**Exercise 11.3**: Create a `Student` structure with name and an array of test scores. Add a method `average()` that returns the average score.

**Exercise 11.4**: Create `Date` structure with year, month, day. Add a method `isLeapYear()` that returns true if the year is a leap year.

**Exercise 11.5**: Create a `BankAccount` structure with ownerName and balance. Add methods `deposit(amount)` and `withdraw(amount)`. Prevent withdrawing more than the balance.

**Exercise 11.6** (Challenge): Create a mini contact book using structures. Store contacts with name, phone, and email. Support adding, listing, and searching contacts.

---

*We've created our own data types. But our programs are still in single files. Next, we learn to organize code across multiple files using modules.*

*[Continue to Chapter 12: Modules →](12-modules.md)*
