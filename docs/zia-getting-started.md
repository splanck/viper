---
status: active
audience: public
last-updated: 2026-01-15
---

# Zia — Getting Started

Learn Zia by example. For a complete reference, see **[Zia Reference](zia-reference.md)**.

> **What is Zia?**
> A modern, statically-typed language with C-like syntax, first-class entities (reference types), value types,
> generics, and module bindings. It runs on Viper's VM and can be compiled to native code.

---

## Table of Contents

1. [Your First Program](#1-your-first-program)
2. [Variables and Types](#2-variables-and-types)
3. [Control Flow](#3-control-flow)
4. [Functions](#4-functions)
5. [Entity Types (Classes)](#5-entity-types-classes)
6. [Value Types (Structs)](#6-value-types-structs)
7. [Generic Collections](#7-generic-collections)
8. [Modules and Bindings](#8-modules-and-bindings)
9. [Working with the Runtime](#9-working-with-the-runtime)
10. [Example Project](#10-example-project)
11. [Where to Go Next](#11-where-to-go-next)

---

## 1. Your First Program

Create a file named `hello.zia`:

```viper
module Hello;

func start() {
    Viper.Terminal.Say("Hello, World!");
}
```

Run it:

```bash
./build/src/tools/zia/zia hello.zia
```

**Key points:**

- Every file starts with a `module` declaration
- `start()` is the entry point (like `main()` in C)
- Use `Viper.Terminal.Say()` for console output with newline
- Statements end with semicolons; blocks use `{ }`
- Comments use `//` for single-line and `/* */` for multi-line

---

## 2. Variables and Types

### Variable Declaration

Use `var` for mutable variables and `final` for constants:

```viper
var x = 42;              // Type inferred as Integer
var name: String = "Alice";  // Explicit type
final PI = 3.14159;      // Immutable constant
```

### Built-in Types

| Type | Description | Example |
|------|-------------|---------|
| `Integer` | 64-bit signed integer | `42`, `-17`, `0xFF` |
| `Number` | 64-bit floating-point | `3.14`, `1e-5` |
| `String` | UTF-8 string | `"hello"`, `"line\n"` |
| `Boolean` | True or false | `true`, `false` |

### String Interpolation

Embed expressions in strings with `${...}`:

```viper
var name = "Alice";
var age = 30;
Viper.Terminal.Say("${name} is ${age} years old");
```

---

## 3. Control Flow

### If Statements

```viper
if score > 100 {
    Viper.Terminal.Say("High score!");
} else if score > 50 {
    Viper.Terminal.Say("Good score");
} else {
    Viper.Terminal.Say("Try again");
}
```

Note: Parentheses around conditions are optional.

### While Loops

```viper
var i = 0;
while i < 10 {
    Viper.Terminal.PrintInt(i);
    i = i + 1;
}
```

### For Loops

C-style for loops:

```viper
for (var i = 0; i < 10; i = i + 1) {
    Viper.Terminal.PrintInt(i);
}
```

### For-In Loops (Ranges)

```viper
// Exclusive range: 0 to 9
for i in 0..10 {
    Viper.Terminal.PrintInt(i);
}

// Inclusive range: 0 to 10
for i in 0..=10 {
    Viper.Terminal.PrintInt(i);
}
```

---

## 4. Functions

### Basic Functions

```viper
func greet(name: String) {
    Viper.Terminal.Say("Hello, ${name}!");
}

func add(a: Integer, b: Integer) -> Integer {
    return a + b;
}

func start() {
    greet("World");
    var sum = add(3, 4);
    Viper.Terminal.SayInt(sum);  // 7
}
```

### Function Features

- Parameters require type annotations
- Return type follows `->` (omit for void functions)
- Functions can call other functions defined in the same module

---

## 5. Entity Types (Classes)

Entities are reference types with identity, inheritance, and methods.

### Defining an Entity

```viper
entity Player {
    String name;
    Integer health;
    Integer score;

    func init(playerName: String) {
        name = playerName;
        health = 100;
        score = 0;
    }

    func takeDamage(amount: Integer) {
        health = health - amount;
        if health < 0 {
            health = 0;
        }
    }

    func addScore(points: Integer) {
        score = score + points;
    }

    func isAlive() -> Integer {
        if health > 0 {
            return 1;
        }
        return 0;
    }

    func getHealth() -> Integer {
        return health;
    }
}
```

### Using Entities

```viper
func start() {
    // Create a new entity instance
    var player = new Player();
    player.init("Alice");

    // Call methods
    player.addScore(100);
    player.takeDamage(25);

    Viper.Terminal.Print("Health: ");
    Viper.Terminal.SayInt(player.getHealth());  // 75
}
```

**Key points:**

- Use `new EntityName()` to create instances
- Fields are accessed with dot notation
- `init()` is the conventional initializer method
- Methods can access fields directly (implicit `self`)

---

## 6. Value Types (Structs)

Value types have copy semantics — assignments create copies.

```viper
value Point {
    Integer x;
    Integer y;

    func init(px: Integer, py: Integer) {
        x = px;
        y = py;
    }

    func distanceFromOrigin() -> Number {
        return Viper.Math.Sqrt(x * x + y * y);
    }
}
```

---

## 7. Generic Collections

Zia supports generic collections from the runtime library.

### Lists

```viper
// Create a list of integers
var numbers: List[Integer] = new List[Integer]();
numbers.add(10);
numbers.add(20);
numbers.add(30);

// Access elements
var first = numbers.get(0);  // 10

// Iterate
var i = 0;
while i < numbers.size() {
    Viper.Terminal.SayInt(numbers.get(i));
    i = i + 1;
}
```

### Entity Lists

```viper
// List of entity instances
var players: List[Player] = new List[Player]();

var p1 = new Player();
p1.init("Alice");
players.add(p1);

var p2 = new Player();
p2.init("Bob");
players.add(p2);
```

---

## 8. Modules and Bindings

### Module Declaration

Every file starts with a module declaration:

```viper
module MyGame;
```

### Binding Other Modules

Bind other `.zia` files to use their types and functions:

```viper
module Game;

bind "./entities";    // binds entities.zia from same directory
bind "./utils";       // binds utils.zia
bind "./config" as C; // bind with alias

func start() {
    var player = new Player();  // Player defined in entities.zia
    player.init("Hero");
}
```

**Bind path rules:**

- `"./foo"` — Relative path, adds `.zia` extension
- `"../bar"` — Parent directory relative path
- `"foo"` — Same directory, adds `.zia` extension

---

## 9. Working with the Runtime

Zia programs have access to the full Viper Runtime Library.

### Terminal I/O

```viper
// Output
Viper.Terminal.Say("Hello");           // Print with newline
Viper.Terminal.Print("No newline");    // Print without newline
Viper.Terminal.SayInt(42);             // Print integer with newline
Viper.Terminal.PrintInt(42);           // Print integer without newline

// Input
var key = Viper.Terminal.GetKey();             // Wait for key press
var keyTimeout = Viper.Terminal.GetKeyTimeout(100);  // With timeout (ms)

// Terminal control
Viper.Terminal.Clear();                // Clear screen
Viper.Terminal.SetPosition(row, col);  // Move cursor
Viper.Terminal.SetColor(fg, bg);       // Set foreground/background (0-15)
Viper.Terminal.SetCursorVisible(0);    // Hide cursor
```

### Color Codes

| Code | Color |
|------|-------|
| 0 | Black |
| 1 | Red |
| 2 | Green |
| 3 | Yellow |
| 4 | Blue |
| 5 | Magenta |
| 6 | Cyan |
| 7 | White |
| 8-15 | Bright variants |

### Time Functions

```viper
Viper.Time.SleepMs(500);  // Sleep for 500 milliseconds
```

### Math Functions

```viper
var abs = Viper.Math.AbsInt(-42);   // 42
var sqrt = Viper.Math.Sqrt(16.0);   // 4.0
var rand = Viper.Random.NextInt(100);  // Random 0-99
```

---

## 10. Example Project

Here's a complete mini-game demonstrating Zia features:

```viper
module GuessGame;

var secretNumber = 0;
var guessCount = 0;
var gameOver = 0;

func start() {
    Viper.Terminal.Say("=== Number Guessing Game ===");
    Viper.Terminal.Say("I'm thinking of a number between 1 and 100.");

    // Generate random number 1-100
    secretNumber = Viper.Random.NextInt(100) + 1;
    guessCount = 0;
    gameOver = 0;

    while gameOver == 0 {
        Viper.Terminal.Print("Your guess: ");

        // For simplicity, we'll use a fixed sequence
        // In a real game, you'd read user input
        var guess = getNextGuess();

        guessCount = guessCount + 1;

        if guess == secretNumber {
            Viper.Terminal.Say("Correct! You got it in ${guessCount} guesses!");
            gameOver = 1;
        } else if guess < secretNumber {
            Viper.Terminal.Say("Too low!");
        } else {
            Viper.Terminal.Say("Too high!");
        }
    }
}

func getNextGuess() -> Integer {
    // Placeholder - would normally read from input
    return 50;
}
```

For more complete examples, see the `demos/zia/` directory:
- `frogger/main.zia` — Full Frogger game with entities and collision detection
- `ladders/` — Donkey Kong-style platformer with multiple entities

---

## 11. Where to Go Next

**Language Documentation:**

- [Zia Reference](zia-reference.md) — Complete language specification
- [Runtime Library](viperlib/README.md) — All available classes and methods

**Examples:**

- `demos/zia/frogger/` — Complete Frogger game example
- `demos/zia/ladders/` — Donkey Kong-style platformer

**Related Guides:**

- [IL Guide](il-guide.md) — Understand the compiled output
- [Frontend How-To](frontend-howto.md) — How frontends work

---

## Quick Reference

### Program Structure

```viper
module ModuleName;

bind "./other";

// Global variables
var globalVar = 0;

// Entity types
entity MyEntity {
    Integer field;
    func method() { }
}

// Value types
value MyValue {
    Integer field;
}

// Functions
func myFunction(param: Type) -> ReturnType {
    return value;
}

// Entry point
func start() {
    // Program starts here
}
```

### Operators

| Category | Operators |
|----------|-----------|
| Arithmetic | `+`, `-`, `*`, `/`, `%` |
| Comparison | `==`, `!=`, `<`, `<=`, `>`, `>=` |
| Logical | `&&`, `||`, `!` |
| Bitwise | `&`, `|`, `^`, `~` |
| Assignment | `=` |
