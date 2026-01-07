# Chapter 12: Modules

Your programs are growing. The game demo in Chapter 11 was already getting long. Real programs are thousands of files with millions of lines. How do developers manage that?

The answer is *modules* — self-contained units of code that can be developed, tested, and understood independently. Modules let you split a large program into manageable pieces, share code between projects, and hide internal details behind clean interfaces.

---

## What Is a Module?

A module is a file containing related code: functions, structures, constants. Every ViperLang file is a module.

Here's a simple module:

```viper
// file: math_utils.viper
module MathUtils;

func square(x: f64) -> f64 {
    return x * x;
}

func cube(x: f64) -> f64 {
    return x * x * x;
}

const PI = 3.14159265358979;
```

This module provides math utilities. Other code can *import* this module to use its functions and constants.

---

## Importing Modules

To use code from another module:

```viper
// file: main.viper
module Main;

import MathUtils;

func start() {
    let x = 5.0;
    Viper.Terminal.Say("Square: " + MathUtils.square(x));
    Viper.Terminal.Say("Cube: " + MathUtils.cube(x));
    Viper.Terminal.Say("Pi: " + MathUtils.PI);
}
```

The `import MathUtils` statement makes that module's contents available. You access them with the module name prefix: `MathUtils.square`.

---

## Importing Specific Items

If you only need certain items, import them directly:

```viper
import MathUtils { square, PI };

func start() {
    Viper.Terminal.Say(square(5.0));  // No prefix needed
    Viper.Terminal.Say(PI);
}
```

This imports only `square` and `PI`, and you can use them without the module prefix.

You can even rename imports:

```viper
import MathUtils { square as sq };

func start() {
    Viper.Terminal.Say(sq(5.0));
}
```

---

## Public and Private

Not everything in a module should be visible to outsiders. Use `pub` to mark what's public:

```viper
// file: counter.viper
module Counter;

let count = 0;  // Private: only this module can see it

pub func increment() {
    count += 1;
}

pub func decrement() {
    count -= 1;
}

pub func get() -> i64 {
    return count;
}

func reset() {  // Private: internal use only
    count = 0;
}
```

The `count` variable and `reset` function are internal implementation details. Other modules can only use the public functions.

```viper
// file: main.viper
import Counter;

func start() {
    Counter.increment();
    Counter.increment();
    Viper.Terminal.Say(Counter.get());  // 2

    // Counter.count = 100;  // Error: count is private
    // Counter.reset();      // Error: reset is private
}
```

This is called *encapsulation*: hiding internal details and exposing only what's needed. It lets you change the internals without breaking code that uses your module.

---

## Organizing Files

A typical project structure:

```
my_project/
├── main.viper       # Entry point
├── player.viper     # Player module
├── enemy.viper      # Enemy module
├── graphics.viper   # Graphics module
└── utils/
    ├── math.viper   # Math utilities
    └── random.viper # Random utilities
```

Modules in subdirectories:

```viper
import utils.math;     // Import utils/math.viper
import utils.random;   // Import utils/random.viper

func start() {
    let x = utils.math.square(5.0);
}
```

---

## The Standard Library

Viper comes with a rich standard library organized into modules:

```viper
import Viper.Terminal;   // Terminal I/O
import Viper.File;       // File operations
import Viper.Math;       // Mathematical functions
import Viper.Parse;      // String parsing
import Viper.Fmt;        // Formatting
import Viper.Time;       // Date and time
import Viper.Random;     // Random numbers
```

You've been using `Viper.Terminal.Say()` all along — that's accessing the `Say` function from the `Viper.Terminal` module.

The standard library is pre-imported, so you don't need explicit import statements for common modules. But understanding that they're modules helps you know where to look for functionality.

---

## Module Dependencies

Modules can import other modules, creating a dependency graph:

```
main.viper
    └── imports game.viper
        ├── imports player.viper
        │   └── imports physics.viper
        └── imports enemy.viper
            └── imports physics.viper
```

Both `player` and `enemy` import `physics`. That's fine — each gets access to the same physics code. The physics module is only loaded once.

**Avoid circular dependencies:** If A imports B and B imports A, you have a problem. Restructure your code so dependencies flow in one direction.

---

## A Complete Example: Modular Game

Let's refactor our game demo into modules:

**vec2.viper** — Vector math
```viper
module Vec2;

pub struct Vec2 {
    x: f64;
    y: f64;
}

pub func create(x: f64, y: f64) -> Vec2 {
    return Vec2 { x: x, y: y };
}

pub func add(a: Vec2, b: Vec2) -> Vec2 {
    return Vec2 { x: a.x + b.x, y: a.y + b.y };
}

pub func distance(a: Vec2, b: Vec2) -> f64 {
    let dx = b.x - a.x;
    let dy = b.y - a.y;
    return Viper.Math.sqrt(dx * dx + dy * dy);
}

pub func zero() -> Vec2 {
    return Vec2 { x: 0.0, y: 0.0 };
}
```

**player.viper** — Player entity
```viper
module Player;

import Vec2;

pub struct Player {
    name: string;
    position: Vec2.Vec2;
    health: i64;
    score: i64;
}

pub func create(name: string) -> Player {
    return Player {
        name: name,
        position: Vec2.zero(),
        health: 100,
        score: 0
    };
}

pub func isAlive(player: Player) -> bool {
    return player.health > 0;
}

pub func move(player: Player, direction: Vec2.Vec2) -> Player {
    return Player {
        name: player.name,
        position: Vec2.add(player.position, direction),
        health: player.health,
        score: player.score
    };
}

pub func takeDamage(player: Player, amount: i64) -> Player {
    let newHealth = player.health - amount;
    if newHealth < 0 {
        newHealth = 0;
    }
    return Player {
        name: player.name,
        position: player.position,
        health: newHealth,
        score: player.score
    };
}
```

**enemy.viper** — Enemy entity
```viper
module Enemy;

import Vec2;

pub struct Enemy {
    position: Vec2.Vec2;
    damage: i64;
}

pub func create(x: f64, y: f64, damage: i64) -> Enemy {
    return Enemy {
        position: Vec2.create(x, y),
        damage: damage
    };
}
```

**main.viper** — Game entry point
```viper
module Main;

import Vec2;
import Player;
import Enemy;

func start() {
    Viper.Terminal.Say("=== Modular Game Demo ===");

    let player = Player.create("Hero");
    let enemy = Enemy.create(5.0, 3.0, 10);

    Viper.Terminal.Say("Player: " + player.name);
    Viper.Terminal.Say("Health: " + player.health);

    // Move player
    let direction = Vec2.create(1.0, 0.5);
    player = Player.move(player, direction);
    Viper.Terminal.Say("Position: (" + player.position.x + ", " + player.position.y + ")");

    // Check combat
    let dist = Vec2.distance(player.position, enemy.position);
    Viper.Terminal.Say("Distance to enemy: " + dist);

    if dist < 5.0 {
        player = Player.takeDamage(player, enemy.damage);
        Viper.Terminal.Say("Hit! Health: " + player.health);
    }

    if Player.isAlive(player) {
        Viper.Terminal.Say("Player survives!");
    } else {
        Viper.Terminal.Say("Game Over!");
    }
}
```

Now each concept lives in its own file. You can work on player logic without touching enemy code. You can test Vec2 in isolation. The main file is short and focused on game flow.

---

## The Three Languages

**ViperLang**
```viper
// Defining
module MyModule;
pub func hello() { ... }

// Importing
import MyModule;
import MyModule { hello };
import OtherModule { something as alias };
```

**BASIC**
```basic
' BASIC uses NAMESPACE and USING
NAMESPACE MyModule
    PUBLIC SUB Hello()
        ...
    END SUB
END NAMESPACE

' Using
USING MyModule
CALL Hello()
```

**Pascal**
```pascal
{ Pascal uses units }
unit MyModule;

interface
    procedure Hello;

implementation
    procedure Hello;
    begin
        ...
    end;

end.

{ Using }
uses MyModule;
begin
    Hello;
end.
```

Pascal separates `interface` (what's visible) from `implementation` (the code).

---

## Module Design Guidelines

**One concept per module.** A `Player` module for player logic, an `Enemy` module for enemies. Don't cram unrelated things together.

**Minimize the public interface.** Expose only what others need. More private details mean more freedom to change internals later.

**Avoid circular dependencies.** If two modules depend on each other, factor out the shared parts into a third module.

**Name modules clearly.** `math_utils` not `mu`. `player` not `p1`.

**Keep modules focused.** If a module is getting too large, split it.

---

## Summary

- Modules organize code into separate, reusable files
- `module Name;` declares a module
- `import ModuleName;` brings in another module
- `import ModuleName { item };` imports specific items
- `pub` marks functions and structures as public
- Items without `pub` are private (internal only)
- The standard library is organized into modules
- Good module design means one concept per module, minimal public interfaces, and no circular dependencies

---

## Exercises

**Exercise 12.1**: Create a `StringUtils` module with functions `repeat(s, n)` (repeat string n times) and `reverse(s)` (reverse a string). Use it from another file.

**Exercise 12.2**: Split the calculator from Chapter 10 into modules: one for parsing input, one for calculations, one for display.

**Exercise 12.3**: Create a `Constants` module with mathematical and physical constants (PI, E, SPEED_OF_LIGHT, etc.). Mark them all public.

**Exercise 12.4**: Create a `Stack` module that provides a stack data structure with `push`, `pop`, `peek`, and `isEmpty` functions. Keep the internal array private.

**Exercise 12.5**: Create a simple logging module with functions `info(msg)`, `warn(msg)`, `error(msg)` that format messages differently.

**Exercise 12.6** (Challenge): Create a multi-module address book with separate modules for Contact, Storage (file I/O), and UI (terminal interaction).

---

*We can now organize large programs. Next, we survey the entire standard library — what Viper gives you for free.*

*[Continue to Chapter 13: The Standard Library →](13-stdlib.md)*
