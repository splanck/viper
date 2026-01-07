# Chapter 13: The Standard Library

You don't have to build everything from scratch. Viper comes with a *standard library* — a collection of pre-written modules for common tasks. Learning what's available saves you from reinventing the wheel and lets you focus on what makes your program unique.

This chapter surveys the major modules. We won't cover everything — there's too much. Instead, we'll hit the highlights so you know where to look.

---

## Overview

The standard library is organized into modules under the `Viper` namespace:

| Module | Purpose |
|--------|---------|
| `Viper.Terminal` | Console input/output |
| `Viper.File` | File operations |
| `Viper.Dir` | Directory operations |
| `Viper.Path` | Path manipulation |
| `Viper.Parse` | String-to-number conversion |
| `Viper.Fmt` | String formatting |
| `Viper.Math` | Mathematical functions |
| `Viper.Random` | Random number generation |
| `Viper.Time` | Date and time |
| `Viper.Environment` | Environment variables, arguments |
| `Viper.Text` | Advanced string operations |
| `Viper.Collections` | Lists, maps, sets |
| `Viper.Network` | TCP/UDP networking |
| `Viper.Threads` | Concurrency |
| `Viper.Crypto` | Hashing, encoding |

Let's explore the most important ones.

---

## Viper.Terminal — Console I/O

You've used this throughout the book:

```viper
Viper.Terminal.Say("Hello!");              // Print with newline
Viper.Terminal.Print("No newline");        // Print without newline
let input = Viper.Terminal.ReadLine();     // Read a line
let char = Viper.Terminal.ReadKey();       // Read single keypress
```

Terminal control:
```viper
Viper.Terminal.Clear();                    // Clear screen
Viper.Terminal.SetCursor(10, 5);           // Move cursor to column 10, row 5
Viper.Terminal.SetColor("red", "black");   // Red text, black background
Viper.Terminal.ResetColor();               // Back to defaults
Viper.Terminal.HideCursor();
Viper.Terminal.ShowCursor();
```

These are essential for interactive console applications and games.

---

## Viper.Math — Mathematics

Standard mathematical functions:

```viper
Viper.Math.abs(-5);         // 5 (absolute value)
Viper.Math.min(3, 7);       // 3
Viper.Math.max(3, 7);       // 7
Viper.Math.clamp(15, 0, 10); // 10 (constrain to range)

Viper.Math.floor(3.7);      // 3.0 (round down)
Viper.Math.ceil(3.2);       // 4.0 (round up)
Viper.Math.round(3.5);      // 4.0 (round to nearest)

Viper.Math.sqrt(16.0);      // 4.0 (square root)
Viper.Math.pow(2.0, 8.0);   // 256.0 (power)
Viper.Math.log(2.718);      // ~1.0 (natural log)
Viper.Math.exp(1.0);        // ~2.718 (e^x)

Viper.Math.sin(0.0);        // 0.0
Viper.Math.cos(0.0);        // 1.0
Viper.Math.tan(0.0);        // 0.0
Viper.Math.atan2(y, x);     // Angle from coordinates
```

Constants:
```viper
Viper.Math.PI;              // 3.14159...
Viper.Math.E;               // 2.71828...
```

---

## Viper.Random — Randomness

```viper
Viper.Random.int(1, 100);      // Random integer 1-100 (inclusive)
Viper.Random.float();          // Random float 0.0-1.0
Viper.Random.bool();           // Random true or false
Viper.Random.choice(array);    // Random element from array
Viper.Random.shuffle(array);   // Shuffle array in place
```

Seeding for reproducibility:
```viper
Viper.Random.seed(12345);      // Same seed = same sequence
```

Example — dice roll:
```viper
let die = Viper.Random.int(1, 6);
Viper.Terminal.Say("You rolled: " + die);
```

---

## Viper.Time — Date and Time

Current time:
```viper
let now = Viper.Time.now();
Viper.Terminal.Say(now.year);
Viper.Terminal.Say(now.month);
Viper.Terminal.Say(now.day);
Viper.Terminal.Say(now.hour);
Viper.Terminal.Say(now.minute);
Viper.Terminal.Say(now.second);
```

Formatting:
```viper
let formatted = now.format("YYYY-MM-DD HH:mm:ss");
// "2024-03-15 14:30:00"
```

Timing:
```viper
let start = Viper.Time.millis();
// ... do something ...
let elapsed = Viper.Time.millis() - start;
Viper.Terminal.Say("Took " + elapsed + " ms");
```

Delays:
```viper
Viper.Time.sleep(1000);  // Pause for 1000 milliseconds (1 second)
```

---

## Viper.Environment — System Info

Command-line arguments:
```viper
let args = Viper.Environment.args();
for arg in args {
    Viper.Terminal.Say("Arg: " + arg);
}
```

Environment variables:
```viper
let home = Viper.Environment.get("HOME");
let path = Viper.Environment.get("PATH");

if Viper.Environment.has("DEBUG") {
    // Debug mode enabled
}
```

System info:
```viper
Viper.Environment.os();           // "windows", "macos", "linux"
Viper.Environment.homeDir();      // User's home directory
Viper.Environment.currentDir();   // Current working directory
```

---

## Viper.Fmt — Formatting

Create formatted strings:
```viper
let msg = Viper.Fmt.format("Hello, {}!", "World");
// "Hello, World!"

let info = Viper.Fmt.format("{} is {} years old", "Alice", 30);
// "Alice is 30 years old"
```

Number formatting:
```viper
Viper.Fmt.format("{:.2}", 3.14159);   // "3.14" (2 decimal places)
Viper.Fmt.format("{:05}", 42);        // "00042" (pad to 5 digits)
Viper.Fmt.format("{:>10}", "hi");     // "        hi" (right-align)
Viper.Fmt.format("{:<10}", "hi");     // "hi        " (left-align)
```

---

## Viper.Text — String Utilities

Beyond basic string methods, advanced text operations:

```viper
Viper.Text.padLeft("42", 5, '0');    // "00042"
Viper.Text.padRight("hi", 5, ' ');   // "hi   "
Viper.Text.repeat("ab", 3);          // "ababab"
Viper.Text.reverse("hello");         // "olleh"

Viper.Text.isDigit('5');             // true
Viper.Text.isLetter('A');            // true
Viper.Text.isWhitespace(' ');        // true

Viper.Text.lines("a\nb\nc");         // ["a", "b", "c"]
Viper.Text.words("hello world");     // ["hello", "world"]
```

---

## Viper.Collections — Data Structures

Beyond basic arrays, the collections module provides:

**List** — dynamic array:
```viper
let list = Viper.Collections.List.new();
list.add("one");
list.add("two");
Viper.Terminal.Say(list.get(0));     // "one"
Viper.Terminal.Say(list.size());     // 2
list.remove(0);
```

**Map** — key-value pairs:
```viper
let map = Viper.Collections.Map.new();
map.set("name", "Alice");
map.set("age", "30");
Viper.Terminal.Say(map.get("name")); // "Alice"

if map.has("email") {
    // ...
}

for key in map.keys() {
    Viper.Terminal.Say(key + ": " + map.get(key));
}
```

**Set** — unique values:
```viper
let set = Viper.Collections.Set.new();
set.add("apple");
set.add("banana");
set.add("apple");  // Ignored, already exists
Viper.Terminal.Say(set.size());  // 2
```

---

## Viper.Parse — Parsing

Convert strings to values:
```viper
Viper.Parse.Int("42");          // 42
Viper.Parse.Float("3.14");      // 3.14
Viper.Parse.Bool("true");       // true

// With error handling
try {
    let num = Viper.Parse.Int("not a number");
} catch e {
    Viper.Terminal.Say("Parse failed");
}
```

---

## Viper.File / Viper.Dir / Viper.Path

We covered these in Chapter 9:

```viper
// File operations
Viper.File.readText("file.txt");
Viper.File.writeText("file.txt", content);
Viper.File.exists("file.txt");

// Directory operations
Viper.Dir.create("mydir");
Viper.Dir.listFiles("mydir");
Viper.Dir.exists("mydir");

// Path manipulation
Viper.Path.join("dir", "file.txt");
Viper.Path.fileName("/path/to/file.txt");
Viper.Path.extension("/path/to/file.txt");
```

---

## Viper.Crypto — Cryptography

Hashing:
```viper
let hash = Viper.Crypto.md5("hello");
let sha = Viper.Crypto.sha256("hello");
```

Encoding:
```viper
let encoded = Viper.Crypto.base64Encode("hello");
let decoded = Viper.Crypto.base64Decode(encoded);
```

GUIDs:
```viper
let id = Viper.Crypto.guid();  // Unique identifier
```

---

## Putting It Together

Here's a program that uses multiple standard library modules:

```viper
module StdlibDemo;

func start() {
    // Environment
    let args = Viper.Environment.args();
    Viper.Terminal.Say("Running on: " + Viper.Environment.os());

    // Random
    let lucky = Viper.Random.int(1, 100);
    Viper.Terminal.Say("Lucky number: " + lucky);

    // Math
    let angle = Viper.Math.PI / 4.0;
    Viper.Terminal.Say("sin(45°) = " + Viper.Math.sin(angle));

    // Time
    let now = Viper.Time.now();
    Viper.Terminal.Say("Current time: " + now.format("HH:mm:ss"));

    // Formatting
    let price = 19.99;
    Viper.Terminal.Say(Viper.Fmt.format("Price: ${:.2}", price));

    // Collections
    let scores = Viper.Collections.Map.new();
    scores.set("Alice", "95");
    scores.set("Bob", "87");

    Viper.Terminal.Say("Scores:");
    for name in scores.keys() {
        Viper.Terminal.Say("  " + name + ": " + scores.get(name));
    }

    // Text utilities
    let msg = "  hello world  ";
    Viper.Terminal.Say("[" + msg.trim() + "]");
    Viper.Terminal.Say("Reversed: " + Viper.Text.reverse("viper"));

    // Crypto
    let hash = Viper.Crypto.sha256("password123");
    Viper.Terminal.Say("Hash: " + hash.substring(0, 16) + "...");
}
```

---

## Finding More

The standard library is extensive. When you need something:

1. Check the appendices of this book (Appendix D)
2. Look at `Viper.` in your editor — auto-complete often helps
3. Search the Viper documentation online

Most common tasks have standard library solutions. Before writing something yourself, check if it already exists.

---

## Summary

The Viper standard library provides:

| Category | Modules |
|----------|---------|
| I/O | Terminal, File, Dir, Path |
| Numbers | Math, Random, Parse |
| Text | Text, Fmt |
| Time | Time |
| Data | Collections (List, Map, Set) |
| System | Environment |
| Security | Crypto |
| Network | Network (covered in Part IV) |
| Threads | Threads (covered in Part IV) |

Learning the standard library is as important as learning the language itself. It's the difference between building with bricks and building with pre-fabricated components.

---

## Exercises

**Exercise 13.1**: Write a program that prints the current date and time in a nice format, along with a random "quote of the day" from an array of quotes.

**Exercise 13.2**: Use Viper.Math to calculate the hypotenuse of a right triangle given the two sides.

**Exercise 13.3**: Create a simple stopwatch: print the time, wait 5 seconds, print the time again, show the difference.

**Exercise 13.4**: Use Viper.Collections.Map to create a simple word frequency counter that counts how many times each word appears in a sentence.

**Exercise 13.5**: Write a program that takes a filename as a command-line argument and prints the SHA256 hash of its contents.

**Exercise 13.6** (Challenge): Create a simple password generator that generates random passwords of a specified length with letters, numbers, and symbols.

---

*We've finished Part II! You now have the building blocks: strings, files, error handling, structures, modules, and the standard library.*

*Part III introduces object-oriented programming — a powerful way to model complex systems with objects that combine data and behavior.*

*[Continue to Part III: Thinking in Objects →](../part3-objects/14-objects.md)*
