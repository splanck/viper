# Chapter 11: Structures

You're building a game. Your player has a name, a health value, an x position, a y position, and a score. So you create five variables:

```rust
var playerName = "Hero";
var playerHealth = 100;
var playerX = 50.0;
var playerY = 30.0;
var playerScore = 0;
```

Now you need an enemy. Five more variables:

```rust
var enemyName = "Goblin";
var enemyHealth = 30;
var enemyX = 100.0;
var enemyY = 45.0;
var enemyScore = 0;  // Wait, enemies don't have scores...
```

You add a second enemy. Five more. A third enemy. Five more. Then you realize: you need to pass all this information to a function that handles combat. Your function signature becomes a nightmare:

```rust
func attack(attackerName: String, attackerHealth: Integer, attackerX: Number, attackerY: Number,
            defenderName: String, defenderHealth: Integer, defenderX: Number, defenderY: Number,
            ...) {
    // This is getting out of hand
}
```

And when you call it:

```rust
attack(playerName, playerHealth, playerX, playerY, enemyName, enemyHealth, enemyX, enemyY, ...);
```

Did you pass the arguments in the right order? Is `playerY` really the fourth argument, or was it the third? If you swap `attackerX` and `attackerY`, the compiler won't catch it — they're both `Number`. Your bug silently corrupts your game.

Then you need to return the updated health values. But functions can only return one thing. So you resort to ugly workarounds, or you give up and use global variables, and your code becomes a tangled mess.

There must be a better way.

There is. *Structures* let you create your own types that bundle related data together. Instead of five scattered variables describing a player, you have one cohesive `Player`. Instead of passing eight separate values to a function, you pass two: an attacker and a defender. Instead of hoping you got the order right, the compiler checks that you're passing a `Player` where a `Player` is expected.

This chapter introduces structures — one of the most important concepts in programming. Structures are the foundation of organizing data, and they set the stage for object-oriented programming in Part III.

---

## The Problem: Related Data Falls Apart

Let's look more closely at what goes wrong when we use separate variables for related data.

### Problem 1: No Connection Between Variables

Consider tracking a point in 2D space:

```rust
var pointX = 10.5;
var pointY = 20.3;
```

These two variables are intimately related — they describe the same point. But the language doesn't know that. To you, `pointX` and `pointY` form a conceptual unit. To the compiler, they're just two independent floating-point numbers that happen to have similar names.

This disconnect causes problems. What if you accidentally write:

```rust
var pointX = 10.5;
var pointY = 20.3;
var pointZ = 15.0;  // Wait, is this 3D now? Or is this a different point?
```

Nothing enforces that points have exactly two coordinates, or that `pointX` and `pointY` belong together while `pointZ` is something else.

### Problem 2: Functions Become Unwieldy

Every function that works with a point needs two parameters:

```rust
bind Viper.Math as Math;

func distance(x1: Number, y1: Number, x2: Number, y2: Number) -> Number {
    var dx = x2 - x1;
    var dy = y2 - y1;
    return Math.Sqrt(dx * dx + dy * dy);
}
```

That's four parameters for two points. What about three points?

```rust
func triangleArea(x1: Number, y1: Number, x2: Number, y2: Number, x3: Number, y3: Number) -> Number {
    // Six parameters!
    ...
}
```

This quickly becomes unmanageable. And it's easy to make mistakes:

```rust
// Did I mean distance from A to B, or B to A?
// Are these in the right order?
var d = distance(ax, ay, bx, by);
var d = distance(ax, bx, ay, by);  // Oops! Compiler doesn't catch this.
```

### Problem 3: Collections Become Impossible

How do you create an array of points with separate variables?

```rust
// Array of x coordinates
var xs = [10.5, 20.0, 35.5, 40.0];

// Array of y coordinates
var ys = [20.3, 15.0, 25.5, 30.0];
```

Now you have two parallel arrays that must stay synchronized. If you sort one, you must sort the other in exactly the same way. If you add to one, you must add to the other. If you remove from one... you see the pattern. This is fragile and error-prone.

What you really want is an array of points — where each point is a single, indivisible unit.

### Problem 4: It Doesn't Scale

Imagine a contact list application. Each contact has:
- First name
- Last name
- Email
- Phone number
- Street address
- City
- State
- Zip code
- Birthday
- Notes

That's ten pieces of information per contact. With 100 contacts, you'd need 1,000 variables or 10 parallel arrays. Adding a new field like "company name" means updating everything.

This approach simply doesn't work for real programs.

---

## The Solution: Grouping Data with Structures

A *structure* (sometimes called a *struct*, *record*, or *value type*) lets you create a new type that bundles multiple pieces of data together. Here's our point:

```rust
value Point {
    x: Number;
    y: Number;
}
```

This defines a new type called `Point`. It has two *fields*: `x` and `y`, both floating-point numbers. The `value` keyword tells Viper we're defining a structure.

Now we can create points:

```rust
var origin = Point { x: 0.0, y: 0.0 };
var position = Point { x: 10.5, y: 20.3 };
```

Each variable holds a complete point — both coordinates bundled together. You access individual fields with dot notation:

```rust
bind Viper.Terminal;

Say(position.x);  // 10.5
Say(position.y);  // 20.3
```

And you can modify fields:

```rust
position.x = 15.0;
position.y = 25.0;
```

### Mental Model: A Box with Labeled Compartments

Think of a structure as a box with multiple compartments, each labeled with a name. A `Point` box has two compartments: one labeled "x" and one labeled "y". When you create a `Point`, you're filling in the compartments with values.

```
    +------ Point -------+
    |  x:    |  y:       |
    |  10.5  |  20.3     |
    +--------+-----------+
```

The box travels as a unit. When you pass a `Point` to a function, you're passing the entire box, not separate compartments.

### Mental Model: A Form with Fields

Another way to think about it: a structure is like a paper form. A "Person" form has blanks for name, age, and email. When you fill out the form, all the information stays together on the same piece of paper. You file the form, hand it to someone, or put it in a stack — always as one complete document.

```
    +------------------------+
    |   PERSON FORM          |
    |                        |
    |   Name:  _Alice_____   |
    |   Age:   _30________   |
    |   Email: _alice@..._   |
    |                        |
    +------------------------+
```

### Mental Model: A Row in a Database

If you've seen a database or spreadsheet, think of a structure as defining what columns exist, and an instance (a specific structure value) as one row of data:

```
Table: Points
+--------+--------+
|   x    |   y    |
+--------+--------+
|  0.0   |  0.0   |  <- origin
|  10.5  |  20.3  |  <- position
|  -5.0  |  12.0  |  <- another point
+--------+--------+
```

The structure definition (`value Point { x: Number; y: Number; }`) defines the columns. Each actual `Point` value is one row.

---

## Real-World Examples: Why Grouping Matters

Before we dive into syntax details, let's see why structures match how we think about the world.

### A Person Has Multiple Attributes

In real life, you don't think of a person as "a name floating in space, plus an age floating somewhere else, plus an email living in another dimension." A person is a unified whole with various attributes. Alice is not three separate things — she's one person who has a name (Alice), an age (30), and an email (alice@example.com).

```rust
value Person {
    name: String;
    age: Integer;
    email: String;
}

var alice = Person { name: "Alice", age: 30, email: "alice@example.com" };
```

Now `alice` is one thing — a person — that you can store, pass around, and work with as a unit.

### A Rectangle Is Defined by Its Dimensions

A rectangle isn't "a width" and "a height." It's a shape that has both properties:

```rust
value Rectangle {
    width: Number;
    height: Number;
}

var screen = Rectangle { width: 1920.0, height: 1080.0 };
```

### A Date Combines Year, Month, and Day

January 15, 2024 isn't three separate numbers. It's a date — one conceptual unit:

```rust
value Date {
    year: Integer;
    month: Integer;
    day: Integer;
}

var birthday = Date { year: 1990, month: 7, day: 4 };
```

### A Color Mixes Red, Green, and Blue

A color like "coral" isn't separate red, green, and blue values. It's one color composed of those components:

```rust
value Color {
    red: Integer;
    green: Integer;
    blue: Integer;
}

var coral = Color { red: 255, green: 127, blue: 80 };
```

The pattern is universal: whenever multiple pieces of data describe a single concept, they belong in a structure.

---

## Creating and Using Structures

### Defining a Structure

Use the `value` keyword to define a new structure type:

```rust
value TypeName {
    field1: Type1;
    field2: Type2;
    // ... more fields
}
```

Each field has a name and a type. Fields are separated by semicolons.

```rust
value Book {
    title: String;
    author: String;
    pageCount: Integer;
    price: Number;
}
```

### Creating Instances

To create a value of your structure type, use the type name followed by field values in braces:

```rust
var myBook = Book {
    title: "The Viper Programming Guide",
    author: "Jane Developer",
    pageCount: 450,
    price: 29.99
};
```

You must provide values for all fields. The order doesn't matter, but every field must be set:

```rust
// These are equivalent:
var book1 = Book { title: "A", author: "B", pageCount: 100, price: 9.99 };
var book2 = Book { author: "B", price: 9.99, title: "A", pageCount: 100 };
```

### Accessing Fields

Use dot notation to read field values:

```rust
bind Viper.Terminal;

Say(myBook.title);      // "The Viper Programming Guide"
Say(myBook.author);     // "Jane Developer"
Say(myBook.pageCount);  // 450
Say(myBook.price);      // 29.99
```

### Modifying Fields

Use dot notation to change field values:

```rust
myBook.price = 24.99;  // Sale price!
myBook.pageCount = 475;  // Added an appendix
```

---

## Value Semantics: What Happens When You Copy?

One of the most important concepts to understand about structures is *value semantics*. When you assign one structure variable to another, or pass a structure to a function, you create a *copy* of the entire structure. Changes to the copy don't affect the original.

### Assignment Creates a Copy

```rust
value Point {
    x: Number;
    y: Number;
}

var p1 = Point { x: 10.0, y: 20.0 };
var p2 = p1;  // p2 is a COPY of p1

p2.x = 99.0;  // Modify p2

bind Viper.Terminal;

Say(p1.x);  // 10.0 - p1 is unchanged!
Say(p2.x);  // 99.0 - only p2 changed
```

This is different from how some languages handle objects, where assignment creates a reference (an alias) to the same underlying data. With value semantics, each variable has its own independent copy.

### Mental Model: Photocopying a Document

Think of structure assignment like photocopying a document. If you have a form filled out with information and you photocopy it, you now have two independent pieces of paper. Writing on the photocopy doesn't change the original. They started with the same information but are now completely separate.

```
Original p1:          After p2 = p1:           After p2.x = 99:
+-----------+         +-----------+            +-----------+
| x: 10.0   |         | x: 10.0   | p1         | x: 10.0   | p1
| y: 20.0   |         | y: 20.0   |            | y: 20.0   |
+-----------+         +-----------+            +-----------+
                      +-----------+            +-----------+
                      | x: 10.0   | p2 (copy)  | x: 99.0   | p2
                      | y: 20.0   |            | y: 20.0   |
                      +-----------+            +-----------+
```

### Function Parameters Are Copies

When you pass a structure to a function, the function receives a copy:

```rust
bind Viper.Terminal;

func tryToModify(point: Point) {
    point.x = 999.0;  // Modifies the local copy
    Say("Inside function: " + point.x);  // 999.0
}

var original = Point { x: 10.0, y: 20.0 };
tryToModify(original);
Say("After function: " + original.x);  // 10.0 - unchanged!
```

The function can modify its copy all it wants, but the original in the calling code is unaffected. This is often what you want — it prevents functions from accidentally corrupting your data.

### Returning Modified Structures

If a function needs to modify a structure and have the caller see the changes, return the modified version:

```rust
bind Viper.Terminal;

func moveRight(point: Point, amount: Number) -> Point {
    point.x = point.x + amount;
    return point;
}

var p = Point { x: 10.0, y: 20.0 };
p = moveRight(p, 5.0);  // Replace p with the returned copy
Say(p.x);  // 15.0
```

This pattern is explicit: "give me a modified copy" rather than "secretly change my data."

### Why Value Semantics?

Value semantics have important benefits:

1. **Predictability.** You always know that your data won't change unless you explicitly change it or reassign it.

2. **Safety.** Functions can't corrupt your data by accident. If you pass a structure to a function, you know it can't mess with your copy.

3. **Simplicity.** You don't need to think about "is this a reference or a value?" — structures are always values.

4. **Concurrency.** When each copy is independent, you don't have to worry about two parts of your program fighting over the same data.

The tradeoff is that copying takes time and memory, especially for large structures. For most programs, this is negligible. When it matters, there are techniques to handle it (references, covered later).

---

## Methods: Functions That Belong to Values

So far, we've defined structures with data. But behavior is just as important. A `Rectangle` should know how to calculate its own area. A `Point` should know how to compute the distance to another point.

You can define *methods* inside a structure — functions that operate on that structure's data:

```rust
value Rectangle {
    width: Number;
    height: Number;

    func area() -> Number {
        return self.width * self.height;
    }

    func perimeter() -> Number {
        return 2 * (self.width + self.height);
    }
}
```

Now you can call these methods on any `Rectangle`:

```rust
bind Viper.Terminal;

var rect = Rectangle { width: 10.0, height: 5.0 };
Say(rect.area());       // 50.0
Say(rect.perimeter());  // 30.0
```

### The `self` Keyword

Inside a method, `self` refers to the specific instance the method was called on. When you write `rect.area()`, inside the `area` method, `self` is `rect`. So `self.width` is `rect.width` (10.0) and `self.height` is `rect.height` (5.0).

```rust
var small = Rectangle { width: 3.0, height: 2.0 };
var large = Rectangle { width: 100.0, height: 50.0 };

small.area();  // Inside: self is small, returns 6.0
large.area();  // Inside: self is large, returns 5000.0
```

### Methods Keep Code Organized

Without methods, you'd write standalone functions:

```rust
func rectangleArea(rect: Rectangle) -> Number {
    return rect.width * rect.height;
}

var area = rectangleArea(rect);
```

With methods:

```rust
var area = rect.area();
```

Methods have advantages:

1. **Discoverability.** When you type `rect.`, your editor can show you all available methods. With standalone functions, you have to remember function names.

2. **Organization.** Methods live with the data they operate on. The `area` method is inside `Rectangle`, where it belongs.

3. **Natural syntax.** `rect.area()` reads like "rectangle's area" — subject, then verb. It's how we talk about things.

4. **Encapsulation.** Methods have direct access to fields via `self`, so they can work with the data intimately.

### Methods Can Take Parameters

Methods can have parameters in addition to the implicit `self`:

```rust
bind Viper.Math as Math;
bind Viper.Terminal;

value Point {
    x: Number;
    y: Number;

    func distance(other: Point) -> Number {
        var dx = other.x - self.x;
        var dy = other.y - self.y;
        return Math.Sqrt(dx * dx + dy * dy);
    }

    func midpoint(other: Point) -> Point {
        return Point {
            x: (self.x + other.x) / 2,
            y: (self.y + other.y) / 2
        };
    }
}

var a = Point { x: 0.0, y: 0.0 };
var b = Point { x: 3.0, y: 4.0 };

Say(a.distance(b));  // 5.0 (3-4-5 right triangle)

var mid = a.midpoint(b);
Say(mid.x);  // 1.5
Say(mid.y);  // 2.0
```

### Methods Can Modify `self`

Methods can change the structure's fields:

```rust
value Counter {
    count: Integer;

    func increment() {
        self.count = self.count + 1;
    }

    func reset() {
        self.count = 0;
    }

    func add(amount: Integer) {
        self.count = self.count + amount;
    }
}

bind Viper.Terminal;

var c = Counter { count: 0 };
c.increment();
c.increment();
c.increment();
Say(c.count);  // 3

c.Add(10);
Say(c.count);  // 13

c.reset();
Say(c.count);  // 0
```

### Methods Can Return New Instances

A common pattern is methods that return a new structure rather than modifying the original:

```rust
value Point {
    x: Number;
    y: Number;

    func add(other: Point) -> Point {
        return Point {
            x: self.x + other.x,
            y: self.y + other.y
        };
    }

    func scale(factor: Number) -> Point {
        return Point {
            x: self.x * factor,
            y: self.y * factor
        };
    }
}

var p = Point { x: 2.0, y: 3.0 };
var v = Point { x: 1.0, y: 1.0 };

bind Viper.Terminal;

var moved = p.Add(v);  // New point at (3.0, 4.0)
var bigger = p.scale(2.0);  // New point at (4.0, 6.0)

// Original p is unchanged!
Say(p.x);  // 2.0
```

This style keeps the original value intact, which can prevent bugs and make code easier to reason about.

---

## Nested Structures: Values Containing Values

Structures can contain other structures. This lets you build complex data models from simpler pieces.

### Building Hierarchies

Consider modeling an address:

```rust
value Address {
    street: String;
    city: String;
    state: String;
    zipCode: String;
}
```

Now a `Person` can include an `Address`:

```rust
value Person {
    name: String;
    age: Integer;
    home: Address;
}

var alice = Person {
    name: "Alice",
    age: 30,
    home: Address {
        street: "123 Main St",
        city: "Springfield",
        state: "IL",
        zipCode: "62701"
    }
};
```

Access nested fields with chained dots:

```rust
bind Viper.Terminal;

Say(alice.name);           // "Alice"
Say(alice.home.city);      // "Springfield"
Say(alice.home.zipCode);   // "62701"
```

### Why Nest?

Nesting provides several benefits:

1. **Reusability.** The `Address` type can be used for home address, work address, billing address, shipping address — anywhere you need an address.

2. **Organization.** Instead of `Person` having seven fields (name, age, street, city, state, zipCode, country), it has three meaningful groups: name, age, and address.

3. **Clarity.** `person.home.city` is clearer than `personHomeCity` or trying to remember which of several city fields is which.

### A Game Example

Let's model a game with nested structures:

```rust
bind Viper.Math as Math;

value Vec2 {
    x: Number;
    y: Number;

    func add(other: Vec2) -> Vec2 {
        return Vec2 { x: self.x + other.x, y: self.y + other.y };
    }

    func distance(other: Vec2) -> Number {
        var dx = other.x - self.x;
        var dy = other.y - self.y;
        return Math.Sqrt(dx * dx + dy * dy);
    }
}

value Health {
    current: Integer;
    maximum: Integer;

    func percentage() -> Number {
        return (self.current * 100) / self.maximum;
    }

    func isDead() -> Boolean {
        return self.current <= 0;
    }

    func heal(amount: Integer) {
        self.current = self.current + amount;
        if self.current > self.maximum {
            self.current = self.maximum;
        }
    }

    func damage(amount: Integer) {
        self.current = self.current - amount;
        if self.current < 0 {
            self.current = 0;
        }
    }
}

value Player {
    name: String;
    position: Vec2;
    health: Health;
    score: Integer;

    func isAlive() -> Boolean {
        return !self.health.isDead();
    }

    func move(direction: Vec2) {
        self.position = self.position.Add(direction);
    }

    func takeDamage(amount: Integer) {
        bind Viper.Terminal;
        self.health.damage(amount);
        if self.health.isDead() {
            Say(self.name + " has been defeated!");
        }
    }
}
```

Now creating a player is clean and structured:

```rust
var hero = Player {
    name: "Hero",
    position: Vec2 { x: 0.0, y: 0.0 },
    health: Health { current: 100, maximum: 100 },
    score: 0
};

bind Viper.Terminal;

// Use nested methods
hero.move(Vec2 { x: 5.0, y: 3.0 });
hero.takeDamage(25);
Say(hero.health.percentage());  // 75.0
```

### Modifying Nested Fields

You can modify nested fields directly:

```rust
alice.home.city = "Chicago";
alice.home.zipCode = "60601";

hero.position.x = 100.0;
hero.health.current = 50;
```

---

## Default Values and Initialization

When creating structures, you must provide values for all fields. But sometimes you want sensible defaults.

### Factory Functions

Create a function that returns a structure with default values:

```rust
value Config {
    volume: Integer;
    difficulty: String;
    fullscreen: Boolean;
    musicEnabled: Boolean;
}

func defaultConfig() -> Config {
    return Config {
        volume: 50,
        difficulty: "normal",
        fullscreen: false,
        musicEnabled: true
    };
}

// Use the defaults
var settings = defaultConfig();

// Or customize after
var mySettings = defaultConfig();
mySettings.difficulty = "hard";
mySettings.fullscreen = true;
```

### Multiple Factory Functions

You can have different functions for different scenarios:

```rust
value Rectangle {
    width: Number;
    height: Number;
}

func square(size: Number) -> Rectangle {
    return Rectangle { width: size, height: size };
}

func goldenRectangle(width: Number) -> Rectangle {
    return Rectangle { width: width, height: width / 1.618 };
}

func screen(resolution: String) -> Rectangle {
    if resolution == "720p" {
        return Rectangle { width: 1280.0, height: 720.0 };
    } else if resolution == "1080p" {
        return Rectangle { width: 1920.0, height: 1080.0 };
    } else if resolution == "4K" {
        return Rectangle { width: 3840.0, height: 2160.0 };
    }
    return Rectangle { width: 800.0, height: 600.0 };
}

var s = square(100.0);
var g = goldenRectangle(500.0);
var display = screen("1080p");
```

### Initializer Methods

Some structures benefit from having an `init` method pattern:

```rust
value Circle {
    centerX: Number;
    centerY: Number;
    radius: Number;
}

func createCircle(x: Number, y: Number, r: Number) -> Circle {
    return Circle { centerX: x, centerY: y, radius: r };
}

func circleAtOrigin(radius: Number) -> Circle {
    return Circle { centerX: 0.0, centerY: 0.0, radius: radius };
}

func unitCircle() -> Circle {
    return Circle { centerX: 0.0, centerY: 0.0, radius: 1.0 };
}
```

---

## Design Principles: Making Good Structures

### What Should Be a Structure?

**Group data that belongs together.** If values are always used together, they belong in a structure. A point always has x and y together. A person always has name and age together (in your program's context).

**Model real concepts.** Structures should represent things you can name: a Point, a Person, a Rectangle, a Date, a Color, a Transaction. If you can't name it clearly, maybe it shouldn't be a structure.

**Avoid grouping unrelated data.** Don't create a structure just because you have several variables. A `Config` that holds volume, player name, high score, and current level is probably mixing unrelated concerns.

### How Many Fields?

**Small is usually better.** Most well-designed structures have 2-6 fields. If you have 15 fields, consider whether some should be grouped into nested structures.

Too many fields:
```rust
// This is unwieldy
value Person {
    firstName: String;
    lastName: String;
    birthYear: Integer;
    birthMonth: Integer;
    birthDay: Integer;
    streetAddress: String;
    city: String;
    state: String;
    zipCode: String;
    country: String;
    phoneCountryCode: String;
    phoneAreaCode: String;
    phoneNumber: String;
    // ... and so on
}
```

Better with nesting:
```rust
value Date {
    year: Integer;
    month: Integer;
    day: Integer;
}

value Address {
    street: String;
    city: String;
    state: String;
    zipCode: String;
    country: String;
}

value Phone {
    countryCode: String;
    areaCode: String;
    number: String;
}

value Person {
    firstName: String;
    lastName: String;
    birthday: Date;
    address: Address;
    phone: Phone;
}
```

### Name Fields Clearly

**Be descriptive.** Use `firstName`, not `fn`. Use `birthYear`, not `by`. Use `screenWidth`, not `sw`.

**Be consistent.** If you use `width` and `height` in one structure, don't use `w` and `h` in another.

**Avoid abbreviations** unless they're universally understood (`x`, `y` for coordinates are fine).

### Structure vs. Separate Variables

When should you use a structure versus keeping variables separate?

**Use a structure when:**
- The data represents a single concept
- The data is always passed together
- The data would be stored together in a database row
- You'll have multiple instances (multiple people, multiple rectangles)
- You want methods that operate on the data

**Use separate variables when:**
- The data represents different concepts (program name, version number, author — related to your app but not to each other)
- The values are rarely used together
- You'll only ever have one instance
- The scope is very local (temporary loop variables)

---

## Practical Examples

Let's see structures in action with several complete examples.

### Example: Playing Cards

```rust
bind Viper.Convert as Convert;
bind Viper.Terminal;

value Card {
    suit: String;    // "Hearts", "Diamonds", "Clubs", "Spades"
    rank: String;    // "2"-"10", "J", "Q", "K", "A"

    func display() -> String {
        return self.rank + " of " + self.suit;
    }

    func value() -> Integer {
        if self.rank == "A" {
            return 11;
        } else if self.rank == "K" || self.rank == "Q" || self.rank == "J" {
            return 10;
        } else {
            return Convert.ToInt64(self.rank);
        }
    }
}

func createDeck() -> [Card] {
    var deck: [Card] = [];
    var suits = ["Hearts", "Diamonds", "Clubs", "Spades"];
    var ranks = ["2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"];

    for suit in suits {
        for rank in ranks {
            deck.Push(Card { suit: suit, rank: rank });
        }
    }

    return deck;
}

func start() {
    var deck = createDeck();
    Say("Deck has " + deck.Length + " cards");  // 52

    // Show some cards
    Say(deck[0].display());   // "2 of Hearts"
    Say(deck[51].display());  // "A of Spades"
}
```

### Example: 2D Geometry

```rust
bind Viper.Math as Math;
bind Viper.Terminal;

value Point {
    x: Number;
    y: Number;

    func toString() -> String {
        return "(" + self.x + ", " + self.y + ")";
    }

    func distance(other: Point) -> Number {
        var dx = other.x - self.x;
        var dy = other.y - self.y;
        return Math.Sqrt(dx * dx + dy * dy);
    }

    func midpoint(other: Point) -> Point {
        return Point {
            x: (self.x + other.x) / 2,
            y: (self.y + other.y) / 2
        };
    }
}

value Rectangle {
    topLeft: Point;
    width: Number;
    height: Number;

    func area() -> Number {
        return self.width * self.height;
    }

    func perimeter() -> Number {
        return 2 * (self.width + self.height);
    }

    func center() -> Point {
        return Point {
            x: self.topLeft.x + self.width / 2,
            y: self.topLeft.y + self.height / 2
        };
    }

    func bottomRight() -> Point {
        return Point {
            x: self.topLeft.x + self.width,
            y: self.topLeft.y + self.height
        };
    }

    func contains(point: Point) -> Boolean {
        return point.x >= self.topLeft.x &&
               point.x <= self.topLeft.x + self.width &&
               point.y >= self.topLeft.y &&
               point.y <= self.topLeft.y + self.height;
    }
}

func start() {
    var rect = Rectangle {
        topLeft: Point { x: 10.0, y: 20.0 },
        width: 100.0,
        height: 50.0
    };

    Say("Area: " + rect.area());         // 5000.0
    Say("Center: " + rect.center().toString());  // (60.0, 45.0)

    var testPoint = Point { x: 50.0, y: 40.0 };
    if rect.Contains(testPoint) {
        Say("Point is inside rectangle");
    }
}
```

### Example: Student Grades

```rust
bind Viper.Terminal;

value Student {
    name: String;
    grades: [Integer];

    func average() -> Number {
        if self.grades.Length == 0 {
            return 0.0;
        }
        var sum = 0;
        for grade in self.grades {
            sum = sum + grade;
        }
        return sum / self.grades.Length;
    }

    func highest() -> Integer {
        if self.grades.Length == 0 {
            return 0;
        }
        var max = self.grades[0];
        for grade in self.grades {
            if grade > max {
                max = grade;
            }
        }
        return max;
    }

    func lowest() -> Integer {
        if self.grades.Length == 0 {
            return 0;
        }
        var min = self.grades[0];
        for grade in self.grades {
            if grade < min {
                min = grade;
            }
        }
        return min;
    }

    func letterGrade() -> String {
        var avg = self.average();
        if avg >= 90 {
            return "A";
        } else if avg >= 80 {
            return "B";
        } else if avg >= 70 {
            return "C";
        } else if avg >= 60 {
            return "D";
        }
        return "F";
    }

    func addGrade(grade: Integer) {
        self.grades.Push(grade);
    }

    func report() {
        Say("Student: " + self.name);
        Say("  Grades: " + self.grades.Length);
        Say("  Average: " + self.average());
        Say("  Highest: " + self.highest());
        Say("  Lowest: " + self.lowest());
        Say("  Letter: " + self.letterGrade());
    }
}

func start() {
    var alice = Student { name: "Alice", grades: [92, 88, 95, 87, 91] };
    var bob = Student { name: "Bob", grades: [78, 82, 75, 80, 79] };

    alice.report();
    Say("");
    bob.report();

    // Add a new grade
    bob.addGrade(90);
    Say("");
    Say("After Bob's new test:");
    bob.report();
}
```

### Example: Simple Inventory System

```rust
bind Viper.Terminal;

value Item {
    name: String;
    weight: Number;
    value: Integer;

    func toString() -> String {
        return self.name + " (weight: " + self.weight + ", value: " + self.value + ")";
    }
}

value Inventory {
    items: [Item];
    maxWeight: Number;

    func currentWeight() -> Number {
        var total = 0.0;
        for item in self.items {
            total = total + item.weight;
        }
        return total;
    }

    func totalValue() -> Integer {
        var total = 0;
        for item in self.items {
            total = total + item.value;
        }
        return total;
    }

    func canAdd(item: Item) -> Boolean {
        return self.currentWeight() + item.weight <= self.maxWeight;
    }

    func add(item: Item) -> Boolean {
        if self.canAdd(item) {
            self.items.Push(item);
            return true;
        }
        return false;
    }

    func display() {
        Say("=== Inventory ===");
        if self.items.Length == 0 {
            Say("  (empty)");
        } else {
            for item in self.items {
                Say("  - " + item.toString());
            }
        }
        Say("Weight: " + self.currentWeight() + " / " + self.maxWeight);
        Say("Value: " + self.totalValue());
    }
}

func start() {
    var backpack = Inventory {
        items: [],
        maxWeight: 50.0
    };

    var sword = Item { name: "Iron Sword", weight: 10.0, value: 100 };
    var shield = Item { name: "Wooden Shield", weight: 8.0, value: 50 };
    var potion = Item { name: "Health Potion", weight: 0.5, value: 25 };
    var armor = Item { name: "Heavy Armor", weight: 40.0, value: 500 };

    backpack.Add(sword);
    backpack.Add(shield);
    backpack.Add(potion);
    backpack.Add(potion);
    backpack.Add(potion);

    backpack.display();

    Say("");
    if backpack.Add(armor) {
        Say("Added armor!");
    } else {
        Say("Can't add armor - too heavy!");
    }
}
```

---

## A Complete Example: Game Entities

Let's put everything together in a more complex game example:

```rust
module GameDemo;

bind Viper.Math as Math;
bind Viper.Terminal;

value Vec2 {
    x: Number;
    y: Number;

    func add(other: Vec2) -> Vec2 {
        return Vec2 { x: self.x + other.x, y: self.y + other.y };
    }

    func subtract(other: Vec2) -> Vec2 {
        return Vec2 { x: self.x - other.x, y: self.y - other.y };
    }

    func scale(factor: Number) -> Vec2 {
        return Vec2 { x: self.x * factor, y: self.y * factor };
    }

    func length() -> Number {
        return Math.Sqrt(self.x * self.x + self.y * self.y);
    }

    func distance(other: Vec2) -> Number {
        return self.subtract(other).Length();
    }

    func toString() -> String {
        return "(" + self.x + ", " + self.y + ")";
    }
}

value Stats {
    health: Integer;
    maxHealth: Integer;
    attack: Integer;
    defense: Integer;

    func healthPercent() -> Integer {
        return (self.health * 100) / self.maxHealth;
    }

    func isAlive() -> Boolean {
        return self.health > 0;
    }

    func takeDamage(amount: Integer) -> Integer {
        var actualDamage = amount - self.defense;
        if actualDamage < 1 {
            actualDamage = 1;  // Minimum 1 damage
        }
        self.health = self.health - actualDamage;
        if self.health < 0 {
            self.health = 0;
        }
        return actualDamage;
    }

    func heal(amount: Integer) {
        self.health = self.health + amount;
        if self.health > self.maxHealth {
            self.health = self.maxHealth;
        }
    }
}

value Player {
    name: String;
    position: Vec2;
    stats: Stats;
    score: Integer;
    level: Integer;

    func move(direction: Vec2) {
        self.position = self.position.Add(direction);
    }

    func attack(target: Enemy) -> Integer {
        var damage = target.stats.takeDamage(self.stats.attack);
        if !target.stats.isAlive() {
            self.score = self.score + target.pointValue;
            Say(self.name + " defeated " + target.name + "!");
            Say("  +" + target.pointValue + " points");
        }
        return damage;
    }

    func statusReport() {
        Say("=== " + self.name + " ===");
        Say("  Level: " + self.level);
        Say("  Position: " + self.position.toString());
        Say("  Health: " + self.stats.health + "/" + self.stats.maxHealth);
        Say("  Attack: " + self.stats.attack);
        Say("  Defense: " + self.stats.defense);
        Say("  Score: " + self.score);
    }
}

value Enemy {
    name: String;
    position: Vec2;
    stats: Stats;
    pointValue: Integer;

    func distanceTo(player: Player) -> Number {
        return self.position.distance(player.position);
    }

    func canAttack(player: Player) -> Boolean {
        return self.distanceTo(player) < 2.0;
    }

    func attack(player: Player) -> Integer {
        return player.stats.takeDamage(self.stats.attack);
    }
}

func createPlayer(name: String) -> Player {
    return Player {
        name: name,
        position: Vec2 { x: 0.0, y: 0.0 },
        stats: Stats {
            health: 100,
            maxHealth: 100,
            attack: 15,
            defense: 5
        },
        score: 0,
        level: 1
    };
}

func createGoblin(x: Number, y: Number) -> Enemy {
    return Enemy {
        name: "Goblin",
        position: Vec2 { x: x, y: y },
        stats: Stats {
            health: 30,
            maxHealth: 30,
            attack: 8,
            defense: 2
        },
        pointValue: 10
    };
}

func createOrc(x: Number, y: Number) -> Enemy {
    return Enemy {
        name: "Orc",
        position: Vec2 { x: x, y: y },
        stats: Stats {
            health: 50,
            maxHealth: 50,
            attack: 12,
            defense: 5
        },
        pointValue: 25
    };
}

func start() {
    Say("=== Adventure Game Demo ===");
    Say("");

    var hero = createPlayer("Hero");
    var goblin = createGoblin(3.0, 0.0);
    var orc = createOrc(5.0, 2.0);

    hero.statusReport();

    Say("");
    Say("A goblin appears!");

    // Move toward goblin
    hero.move(Vec2 { x: 2.0, y: 0.0 });
    Say("Hero moves to " + hero.position.toString());
    Say("Distance to goblin: " + goblin.distanceTo(hero));

    // Combat
    Say("");
    Say("Combat begins!");

    while hero.stats.isAlive() && goblin.stats.isAlive() {
        // Hero attacks
        var damage = hero.attack(goblin);
        Say("Hero deals " + damage + " damage to Goblin");

        if goblin.stats.isAlive() {
            // Goblin attacks back
            damage = goblin.attack(hero);
            Say("Goblin deals " + damage + " damage to Hero");
        }

        Say("  Hero HP: " + hero.stats.health + " | Goblin HP: " + goblin.stats.health);
    }

    Say("");
    hero.statusReport();
}
```

This example demonstrates:
- Nested structures (`Player` contains `Vec2` and `Stats`)
- Methods that operate on the data they contain
- Factory functions for creating instances with sensible defaults
- Structures interacting with each other
- Clear separation of concerns (position logic in `Vec2`, health logic in `Stats`)

---

## The Two Languages

**Zia**
```rust
bind Viper.Math as Math;
bind Viper.Terminal;

value Point {
    x: Number;
    y: Number;

    func distance(other: Point) -> Number {
        var dx = other.x - self.x;
        var dy = other.y - self.y;
        return Math.Sqrt(dx * dx + dy * dy);
    }
}

var p = Point { x: 3.0, y: 4.0 };
Say(p.x);
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

---

## Structures vs. Classes (Preview)

Structures with value semantics are great for simple data containers. But they have limitations:

- **No inheritance.** You can't create a `ColoredPoint` that's a special kind of `Point`.
- **No polymorphism.** You can't write code that works with "any shape" and pass rectangles, circles, and triangles.
- **Methods are simple.** There's no way to override a method in a "child" type.
- **Copying can be expensive.** For large structures passed frequently, making copies has a cost.

For more complex needs, we use *entities* (similar to classes in other languages), which we'll cover in Part III. Entities have reference semantics (assigning creates an alias, not a copy), support inheritance and polymorphism, and offer more flexibility.

For now, structures handle most cases beautifully. They're simpler, safer, and sufficient for organizing data in the majority of programs. When you truly need the power of entities, you'll know — and we'll be ready to teach you.

---

## Common Patterns

### Factory Functions

Create instances with validated or computed values:

```rust
bind Viper.Math as Math;

func createPoint(x: Number, y: Number) -> Point {
    return Point { x: x, y: y };
}

func pointFromAngle(angle: Number, distance: Number) -> Point {
    return Point {
        x: distance * Math.Cos(angle),
        y: distance * Math.Sin(angle)
    };
}
```

### Default Configurations

```rust
value Config {
    volume: Integer;
    difficulty: String;
}

func defaultConfig() -> Config {
    return Config { volume: 50, difficulty: "normal" };
}

func hardConfig() -> Config {
    return Config { volume: 70, difficulty: "hard" };
}
```

### Builder Pattern

For structures with many optional fields, build them step by step:

```rust
value Character {
    name: String;
    health: Integer;
    attack: Integer;
    defense: Integer;
    speed: Integer;
}

func baseCharacter(name: String) -> Character {
    return Character {
        name: name,
        health: 100,
        attack: 10,
        defense: 10,
        speed: 10
    };
}

// Then customize:
var warrior = baseCharacter("Warrior");
warrior.health = 150;
warrior.attack = 20;
warrior.defense = 15;
warrior.speed = 5;

var rogue = baseCharacter("Rogue");
rogue.health = 80;
rogue.attack = 25;
rogue.defense = 5;
rogue.speed = 20;
```

### Comparing Structures

```rust
func pointsEqual(a: Point, b: Point) -> Boolean {
    return a.x == b.x && a.y == b.y;
}

func pointsNearlyEqual(a: Point, b: Point, tolerance: Number) -> Boolean {
    return a.distance(b) < tolerance;
}
```

### Converting to String

```rust
bind Viper.Terminal;

value Person {
    name: String;
    age: Integer;

    func toString() -> String {
        return self.name + " (age " + self.age + ")";
    }
}

var p = Person { name: "Alice", age: 30 };
Say(p.toString());  // "Alice (age 30)"
```

---

## Common Mistakes

### Forgetting to Initialize All Fields

```rust
var p = Point { x: 5.0 };  // Error: y is not initialized
var p = Point { x: 5.0, y: 0.0 };  // Correct
```

Every field must have a value. If you want "optional" fields, use default values in factory functions.

### Confusing the Type and an Instance

```rust
Point.x = 5.0;  // Wrong: Point is the type, not an instance
var p = Point { x: 5.0, y: 3.0 };  // Create an instance
p.x = 5.0;  // Now you can access fields
```

`Point` is a blueprint. `p` is an actual point made from that blueprint.

### Expecting Changes to Persist Through Functions

```rust
bind Viper.Terminal;

func birthday(person: Person) {
    person.age = person.age + 1;  // Modifies a copy!
}

var alice = Person { name: "Alice", age: 30 };
birthday(alice);
Say(alice.age);  // Still 30!
```

Remember value semantics: the function gets a copy. To actually update:

```rust
bind Viper.Terminal;

func birthday(person: Person) -> Person {
    person.age = person.age + 1;
    return person;
}

var alice = Person { name: "Alice", age: 30 };
alice = birthday(alice);  // Assign the returned copy back
Say(alice.age);  // 31
```

### Misspelling Field Names

```rust
var p = Point { x: 5.0, Y: 3.0 };  // Error: 'Y' is not a field (it's 'y')
```

Field names are case-sensitive. The compiler will catch this.

### Modifying Through the Wrong Variable

```rust
var p1 = Point { x: 10.0, y: 20.0 };
var p2 = p1;  // Copy
p2.x = 99.0;
// Don't expect p1 to change!
```

After copying, `p1` and `p2` are completely independent.

---

## Summary

Structures are fundamental to organizing data in programs. Here's what you've learned:

**Core Concepts:**
- Structures bundle related data under a single name
- Define with `value TypeName { fields... }`
- Create instances with `TypeName { field: value, ... }`
- Access fields with `instance.field`

**Value Semantics:**
- Assignment creates a copy
- Function parameters are copies
- Changes to copies don't affect originals
- Return modified copies to share changes

**Methods:**
- Functions defined inside structures
- Use `self` to access the current instance
- Can read fields, modify fields, take parameters, return values
- Keep behavior with the data it operates on

**Nested Structures:**
- Structures can contain other structures
- Build complex data models from simple pieces
- Access nested fields with chained dots

**Design Principles:**
- Group data that belongs together
- Model real concepts
- Keep structures focused (2-6 fields typical)
- Name fields clearly
- Use factory functions for default values

**When to Use Structures:**
- Multiple pieces of data describe one concept
- Data is always used together
- You'll have multiple instances
- You want methods on the data

---

## Exercises

**Exercise 11.1**: Create a `Book` structure with title, author, and pageCount. Create an array of 3 books and print information about each one.

**Exercise 11.2**: Create a `Circle` structure with radius. Add methods for `area()`, `circumference()`, and `diameter()`. Create several circles and test the methods.

**Exercise 11.3**: Create a `Student` structure with name and an array of test scores. Add methods `average()`, `highest()`, `lowest()`, and `letterGrade()`.

**Exercise 11.4**: Create a `Date` structure with year, month, day. Add methods:
- `isLeapYear()` - returns true if the year is a leap year
- `daysInMonth()` - returns the number of days in the month
- `toString()` - returns something like "March 15, 2024"

**Exercise 11.5**: Create a `BankAccount` structure with ownerName and balance. Add methods:
- `deposit(amount)` - adds to balance (if amount > 0)
- `withdraw(amount)` - subtracts from balance (if sufficient funds)
- `transfer(other, amount)` - moves money to another account
- `statement()` - prints account information

**Exercise 11.6**: Create nested structures for an address book:
- `Address` with street, city, state, zipCode
- `Contact` with name, phone, email, and address
- Methods to display contact information nicely

**Exercise 11.7** (Challenge): Create a simple card game:
- `Card` structure with suit and rank
- `Hand` structure with an array of cards
- Methods to add cards, remove cards, calculate total value
- A function that creates and shuffles a deck

**Exercise 11.8** (Challenge): Create a mini contact manager:
- Store contacts with name, phone, and email
- Support adding contacts
- Support listing all contacts
- Support searching by name
- Support updating a contact's information

---

*We've learned to create our own data types — a major milestone in programming. But our programs still live in single files, and we're limited to what we write ourselves. Next, we learn to organize code across multiple files and use code written by others through modules.*

*[Continue to Chapter 12: Modules ->](12-modules.md)*
