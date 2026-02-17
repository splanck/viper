# Chapter 13: The Standard Library

Imagine you're building a house. You could make your own bricks, forge your own nails, mill your own lumber. Or you could go to the hardware store and buy materials that thousands of engineers have already perfected.

Programming works the same way. You *could* write your own code to calculate square roots, parse dates, generate random numbers, and encrypt data. Mathematicians, computer scientists, and engineers have spent decades solving these problems. Why solve them again?

The answer is: you shouldn't have to. That's what the standard library is for.

---

## What Is a Standard Library?

A *standard library* is a collection of pre-written code that comes bundled with a programming language. When you install Viper, you don't just get the ability to write `if` statements and `for` loops. You get thousands of lines of carefully tested, optimized code for common tasks.

Think of it as a toolbox that comes free with the language:

- Need to find the absolute value of a number? There's a function for that.
- Want to read a file from disk? Already written.
- Need to shuffle an array randomly? One line of code.
- Want to know what time it is? Just ask.

Every mainstream programming language has a standard library. Python is famous for its "batteries included" philosophy. Java has a massive standard library. C has a minimal one. Viper strikes a balance: comprehensive enough to be useful, organized enough to be learnable.

**Why does this matter?** Three reasons:

1. **You don't have to reinvent the wheel.** Someone already wrote code to format dates. It handles edge cases you haven't thought of. Use it.

2. **Standard library code is tested.** Millions of programs use these functions. Bugs get found and fixed. Your hand-written version won't have that testing.

3. **Other programmers know it.** When you use `Viper.Math.Sqrt()`, any Viper programmer knows what it does. Your custom `mySquareRoot()` function? They'd have to read your code.

The standard library is a superpower. Learning it makes you a dramatically more productive programmer.

---

## A Tour of Viper's Standard Library

Viper's standard library is organized into modules under the `Viper` namespace. Each module handles a different category of tasks:

| Module | Purpose | You'll Use It For |
|--------|---------|-------------------|
| `Viper.Collections` | Data structures | Lists, maps, sets |
| `Viper.Convert` | String-to-number conversion | Processing user input |
| `Viper.Crypto` | Hashing and encoding | Security, verification |
| `Viper.Environment` | System information | Config, command-line args |
| `Viper.Fmt` | String formatting | Creating output messages |
| `Viper.Graphics` | Low-level drawing | Games, visualizations |
| `Viper.GUI` | Widget-based UI | Desktop applications |
| `Viper.IO.Dir` | Directory operations | Navigating the filesystem |
| `Viper.IO.File` | File operations | Reading/writing data |
| `Viper.IO.Path` | Path manipulation | Building file paths safely |
| `Viper.Math` | Mathematical functions | Calculations, geometry |
| `Viper.Network` | TCP/UDP networking | Web clients, servers |
| `Viper.Math.Random` | Random number generation | Games, simulations, testing |
| `Viper.String` | Advanced string operations | Text processing |
| `Viper.Terminal` | Console input/output | User interaction, debugging |
| `Viper.Threads` | Concurrency | Parallel processing |
| `Viper.Time` | Date and time | Timestamps, scheduling |

Let's explore each one in depth.

---

## Viper.Terminal: Console I/O

You've used `Viper.Terminal` throughout this book. It's how your programs talk to users.

### Basic Input and Output

```rust
bind Viper.Terminal;

Say("Hello!");              // Print with newline
Print("No newline here");   // Print without newline
var input = ReadLine();     // Read a line of text
var char = GetKey();        // Read a single keypress
```

### When to Use Each

- **`Say()`**: Most output. Each message on its own line.
- **`Print()`**: When you want to build up a line piece by piece, or when prompting for input on the same line.
- **`ReadLine()`**: Getting text input from users.
- **`GetKey()`**: Games, menus, or "press any key" prompts.

### Terminal Control

For interactive applications, you can control the cursor and colors:

```rust
bind Viper.Terminal;

Clear();                    // Clear the screen
SetPosition(10, 5);         // Move cursor to column 10, row 5
SetColor(1, 0);             // Set foreground/background color codes
SetCursorVisible(0);        // Hide the blinking cursor
SetCursorVisible(1);        // Show it again
```

These are essential for building text-based games, progress bars, or any program where you want precise control over what the user sees.

### Practical Example: A Simple Menu

```rust
bind Viper.Terminal;
bind Viper.Convert as Convert;

func showMenu() -> Integer {
    Clear();
    Say("=== Main Menu ===");
    Say("1. New Game");
    Say("2. Load Game");
    Say("3. Options");
    Say("4. Quit");
    Print("Choose (1-4): ");

    var choice = ReadLine().Trim();
    return Convert.ToInt64(choice);
}
```

---

## Viper.Math: Mathematics

Math operations beyond basic arithmetic live in `Viper.Math`. These functions have been implemented by experts, optimized for speed, and tested for accuracy across edge cases.

### Basic Functions

```rust
bind Viper.Math as Math;

Math.Abs(-5.0);          // 5.0 (absolute value)
Math.AbsInt(-5);         // 5   (integer absolute value)
Math.MinInt(3, 7);       // 3 (smaller of two integers)
Math.MaxInt(3, 7);       // 7 (larger of two integers)
Math.ClampInt(15, 0, 10); // 10 (constrain to range 0-10)
```

### Rounding

```rust
bind Viper.Math as Math;

Math.Floor(3.7);       // 3.0 (round down)
Math.Ceil(3.2);        // 4.0 (round up)
Math.Round(3.5);       // 4.0 (round to nearest)
Math.Round(3.4);       // 3.0
```

### Powers and Roots

```rust
bind Viper.Math as Math;

Math.Sqrt(16.0);       // 4.0 (square root)
Math.Pow(2.0, 8.0);    // 256.0 (2 to the 8th power)
Math.Log(2.718);       // ~1.0 (natural logarithm)
Math.Log10(100.0);     // 2.0 (base-10 logarithm)
Math.Exp(1.0);         // ~2.718 (e^x)
```

### Trigonometry

```rust
bind Viper.Math as Math;

Math.Sin(0.0);         // 0.0
Math.Cos(0.0);         // 1.0
Math.Tan(0.0);         // 0.0
Math.Atan2(y, x);      // Angle from coordinates
```

### Constants

```rust
bind Viper.Math as Math;

Math.Pi;               // 3.14159265358979...
Math.E;                // 2.71828182845904...
```

### When Would You Use This?

**Game development:** Calculate distances, angles, movement vectors.

```rust
bind Viper.Math as Math;

// Distance between two points
func distance(x1: Number, y1: Number, x2: Number, y2: Number) -> Number {
    var dx = x2 - x1;
    var dy = y2 - y1;
    return Math.Sqrt(dx * dx + dy * dy);
}
```

**Scientific calculations:** Statistics, physics simulations, financial modeling.

```rust
bind Viper.Math as Math;

// Compound interest
func compoundInterest(principal: Number, rate: Number, years: Integer) -> Number {
    return principal * Math.Pow(1.0 + rate, years * 1.0);
}
```

**Graphics:** Smooth animations, circular motion, wave patterns.

```rust
bind Viper.Math as Math;

// Move in a circle
var angle = time * speed;
var x = centerX + radius * Math.Cos(angle);
var y = centerY + radius * Math.Sin(angle);
```

### You Don't Want to Write This Yourself

Computing square roots, trigonometric functions, and logarithms accurately is *hard*. These algorithms have been refined for decades. A naive square root implementation might:

- Be slow (iterating too many times)
- Be inaccurate (floating-point errors)
- Crash on edge cases (negative numbers, infinity)

The standard library handles all of this. Use it.

---

## Viper.Math.Random: Randomness

Games need dice rolls. Simulations need random data. Testing needs random inputs. `Viper.Math.Random` (aliased as `Random` below) provides it all.

### Basic Random Values

```rust
bind Viper.Math.Random as Random;

Random.Range(1, 100);  // Random integer from 1 to 100 (inclusive)
Random.Next();         // Random float from 0.0 to 1.0
Random.Dice(2);        // 1 or 2 — simulates a coin flip
```

### Working with Collections

```rust
bind Viper.Math.Random as Random;
bind Viper.Collections;

var deck = new List[Integer]();
var i = 1;
while i <= 10 {
    deck.Push(i);
    i = i + 1;
}
Random.Shuffle(deck);  // Shuffle in place
```

### Reproducible Randomness

For testing or game replays, you can *seed* the random number generator:

```rust
bind Viper.Math.Random as Random;

Random.Seed(12345);  // Same seed = same sequence of "random" numbers
```

This is crucial for debugging. If a bug only appears sometimes, set a seed to reproduce it reliably.

### Practical Examples

**Dice roll:**
```rust
bind Viper.Math.Random as Random;
bind Viper.Terminal;

var die = Random.Dice(6);
Say("You rolled: " + die);
```

**Coin flip:**
```rust
bind Viper.Math.Random as Random;
bind Viper.Terminal;

if Random.Chance(0.5) == 1 {
    Say("Heads!");
} else {
    Say("Tails!");
}
```

**Random enemy spawn:**
```rust
bind Viper.Math.Random as Random;

// Simple weighted random (for illustration)
var roll = Random.Range(1, 100);
if roll <= 50 {
    spawn("goblin");
} else if roll <= 80 {
    spawn("orc");
} else if roll <= 95 {
    spawn("troll");
} else {
    spawn("dragon");
}
```

**Password generator:**
```rust
bind Viper.Math.Random as Random;

func generatePassword(length: Integer) -> String {
    var chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%";
    var password = "";

    var i = 0;
    while i < length {
        var index = Random.Range(0, chars.Length - 1);
        password = password + chars[index];
        i = i + 1;
    }

    return password;
}
```

---

## Viper.Time: Date and Time

Time is surprisingly complex. Leap years, time zones, daylight saving, calendar systems. The standard library handles the complexity so you don't have to.

### Getting the Current Time

```rust
bind Viper.Time;
bind Viper.Terminal;
bind Viper.Fmt as Fmt;

var dt = Time.DateTime.Now();

Say("Year: "   + Fmt.Int(Time.DateTime.Year(dt)));
Say("Month: "  + Fmt.Int(Time.DateTime.Month(dt)));
Say("Day: "    + Fmt.Int(Time.DateTime.Day(dt)));
Say("Hour: "   + Fmt.Int(Time.DateTime.Hour(dt)));
Say("Minute: " + Fmt.Int(Time.DateTime.Minute(dt)));
Say("Second: " + Fmt.Int(Time.DateTime.Second(dt)));
```

### Formatting Dates

Dates need to be displayed in different formats depending on context:

```rust
bind Viper.Time;
bind Viper.Fmt as Fmt;
bind Viper.Terminal;

var dt = Time.DateTime.Now();
var y  = Time.DateTime.Year(dt);
var mo = Time.DateTime.Month(dt);
var d  = Time.DateTime.Day(dt);
var h  = Time.DateTime.Hour(dt);
var mi = Time.DateTime.Minute(dt);
var s  = Time.DateTime.Second(dt);

// ISO format (international standard)
Say(Fmt.IntPad(y, 4, "0") + "-" + Fmt.IntPad(mo, 2, "0") + "-" + Fmt.IntPad(d, 2, "0"));
// 2024-03-15

// With time
Say(Fmt.IntPad(y, 4, "0") + "-" + Fmt.IntPad(mo, 2, "0") + "-" + Fmt.IntPad(d, 2, "0") +
    " " + Fmt.IntPad(h, 2, "0") + ":" + Fmt.IntPad(mi, 2, "0") + ":" + Fmt.IntPad(s, 2, "0"));
// 2024-03-15 14:30:45
```

### Measuring Elapsed Time

For performance measurement or timing games:

```rust
bind Viper.Time;
bind Viper.Terminal;
bind Viper.Fmt as Fmt;

var start = Time.Clock.Ticks();

// Do some work...
processData();

var elapsed = Time.Clock.Ticks() - start;
Say("Processing took " + Fmt.Int(elapsed) + " ms");
```

### Delays and Pauses

```rust
bind Viper.Terminal;
bind Viper.Time;

Say("Loading...");
Time.Clock.Sleep(2000);  // Pause for 2000 milliseconds (2 seconds)
Say("Done!");
```

### Practical Example: Simple Stopwatch

```rust
bind Viper.Terminal;
bind Viper.Time;
bind Viper.Fmt as Fmt;

func stopwatch() {
    Say("Press Enter to start...");
    ReadLine();

    var start = Time.Clock.Ticks();
    Say("Stopwatch running. Press Enter to stop.");
    ReadLine();

    var elapsed = Time.Clock.Ticks() - start;
    var seconds = elapsed * 1.0 / 1000.0;

    Say("Elapsed: " + Fmt.NumFixed(seconds, 2) + " seconds");
}
```

### Why You Don't Want to Write This Yourself

Date and time code is notoriously bug-prone:

- Is 2024 a leap year? (Yes. 2100? No. 2000? Yes.)
- How many days in February? (Depends on the year.)
- When does daylight saving time start? (Depends on the country, and it changes.)
- What time is it in Tokyo right now? (Depends on when you ask.)

The standard library has been battle-tested against these edge cases. Trust it.

---

## Viper.Environment: System Information

Your program doesn't run in isolation. It runs on a specific computer, in a specific directory, perhaps launched with command-line arguments. `Viper.Environment` gives you access to this context.

### Command-Line Arguments

When someone runs your program from the terminal with arguments:

```bash
$ zia myprogram.zia input.txt --verbose
```

You can access those arguments:

```rust
bind Viper.Environment;
bind Viper.Terminal;

var count = GetArgumentCount();

for i in 0..count {
    Say("Argument: " + GetArgument(i));
}
// Output:
// Argument: input.txt
// Argument: --verbose
```

### Environment Variables

Operating systems have configuration through environment variables:

```rust
bind Viper.Environment;

var home = GetVariable("HOME");       // /Users/alice
var path = GetVariable("PATH");       // System PATH
var editor = GetVariable("EDITOR");   // Preferred text editor

// Check if a variable exists
if HasVariable("DEBUG") {
    // Debug mode is enabled
    enableVerboseLogging();
}
```

### System Information

```rust
bind Viper.Environment;
bind Viper.Machine;

var osName = GetOS();         // "windows", "macos", or "linux"
var home = GetVariable("HOME");  // User's home directory
```

### Practical Example: Cross-Platform Configuration

```rust
bind Viper.Environment;
bind Viper.IO.Path;

func getConfigPath() -> String {
    var osName = GetVariable("OS");
    var home = GetVariable("HOME");

    if osName == "windows" {
        return join(home, "AppData", "Local", "MyApp", "config.json");
    } else if osName == "macos" {
        return join(home, "Library", "Application Support", "MyApp", "config.json");
    } else {
        return join(home, ".config", "myapp", "config.json");
    }
}
```

---

## Viper.Fmt: String Formatting

Concatenating strings with `+` gets messy when mixing numbers and text. `Viper.Fmt` makes formatting cleaner.

### Basic Formatting

```rust
bind Viper.Fmt as Fmt;
bind Viper.Terminal;

var name = "Alice";
var score = 95;

// Format values to strings
Say("Player " + name + " scored " + Fmt.Int(score) + " points!");
// "Player Alice scored 95 points!"

// Fixed decimal places
Say(Fmt.NumFixed(3.14159, 2));   // "3.14"
Say(Fmt.NumFixed(3.14159, 4));   // "3.1416"
```

### Number Formatting

```rust
bind Viper.Fmt as Fmt;

// Decimal places
Fmt.NumFixed(3.14159, 2);    // "3.14"
Fmt.NumFixed(3.14159, 4);    // "3.1416"

// Zero padding
Fmt.IntPad(42, 5, "0");      // "00042"
Fmt.IntPad(1234, 8, "0");    // "00001234"

// Hex, binary, octal
Fmt.Hex(255);                // "ff"
Fmt.Bin(10);                 // "1010"
Fmt.Oct(8);                  // "10"
```

### Practical Example: Formatted Table

```rust
bind Viper.Terminal;
bind Viper.Fmt as Fmt;

func printScoreboard(players: [Player]) {
    Say("Name            Score");
    Say("--------------------------");

    var i = 0;
    while i < players.Length {
        var p = players[i];
        Say(p.name + "  " + Fmt.Int(p.score));
        i = i + 1;
    }
}

// Output:
// Name            Score
// --------------------------
// Alice  950
// Bob  875
// Charlie  1200
```

---

## Viper.String: String Utilities

Beyond basic concatenation, `Viper.String` provides advanced text operations as instance methods on strings.

### Padding and Alignment

```rust
// PadLeft, PadRight, and Repeat are instance methods on String values
var padded = "42".PadLeft(5, "0");    // "00042"
var rpad   = "hi".PadRight(5, " ");   // "hi   "
```

### Repetition and Reversal

```rust
var dashes = "-".Repeat(40);          // A line of dashes
var rev    = "hello".Flip();          // "olleh"
```

### Text Searching and Splitting

```rust
// String methods for searching
var s = "Hello, World!";
var idx  = s.IndexOf("World");       // 7
var has  = s.Has("Hello");           // true
var up   = s.ToUpper();              // "HELLO, WORLD!"
var low  = s.ToLower();              // "hello, world!"
var trim = "  hi  ".Trim();          // "hi"

// Split into parts
var parts = "a,b,c".Split(",");      // Seq of ["a", "b", "c"]
```

### Practical Example: Validating User Input

```rust
func isValidUsername(username: String) -> Boolean {
    // Must be 3-20 characters
    if username.Length < 3 || username.Length > 20 {
        return false;
    }

    // Must start with a letter (A-Z or a-z)
    var first = username.Substring(0, 1);
    if first.ToLower() == first.ToUpper() {
        // Not a letter — both cases are same for non-alpha chars
        return false;
    }

    return true;
}
```

---

## Viper.Collections: Data Structures

Arrays are great, but sometimes you need more specialized data structures.

### List: Dynamic Array

Unlike fixed arrays, lists grow automatically:

```rust
bind Viper.Collections;
bind Viper.Terminal;
bind Viper.Fmt as Fmt;

var list = new List[String]();

list.Push("first");
list.Push("second");
list.Push("third");

Say(list.Get(0));             // "first"
Say(Fmt.Int(list.Len));       // 3

list.RemoveAt(0);             // Remove first element
list.Insert(1, "inserted");   // Insert at position 1
list.Clear();                 // Remove all elements
```

**When to use:** When you don't know how many elements you'll have, or when you need to add/remove frequently.

### Map: Key-Value Pairs

Maps store associations between keys and values:

```rust
bind Viper.Collections;
bind Viper.Terminal;
bind Viper.Fmt as Fmt;

var scores = new Map[String, Integer]();

scores.SetInt("Alice", 950);
scores.SetInt("Bob", 875);
scores.SetInt("Charlie", 1200);

Say(Fmt.Int(scores.GetInt("Alice")));  // 950
Say(Fmt.Int(scores.Len));              // 3

if scores.Has("David") {
    Say("David is in the game");
} else {
    Say("David hasn't played yet");
}

// Iterate over all entries
var keys = scores.Keys();
var i = 0;
while i < keys.Len {
    var key = keys.Get(i);
    Say(key + ": " + Fmt.Int(scores.GetInt(key)));
    i = i + 1;
}

scores.Remove("Bob");
```

**When to use:** When you need to look things up by name, ID, or any other key. Faster than searching an array.

### Set: Unique Values

Sets store unique string values with no duplicates. Use `Viper.Collections.Bag` for string sets:

```rust
bind Viper.Collections;
bind Viper.Terminal;
bind Viper.Fmt as Fmt;

var tags = new Bag();

tags.Put("important");
tags.Put("urgent");
tags.Put("important");  // Ignored - already exists

Say(Fmt.Int(tags.Len));       // 2
Say(Fmt.Bool(tags.Has("urgent")));  // true

tags.Drop("urgent");
```

**When to use:** When you need to track unique items, check membership quickly, or remove duplicates.

### Practical Example: Word Frequency Counter

```rust
bind Viper.Collections;
bind Viper.Convert as Convert;
bind Viper.Terminal;
bind Viper.Fmt as Fmt;

func countWords(text: String) -> Map {
    var frequency = new Map[String, Integer]();
    var words = text.ToLower().Split(" ");

    var i = 0;
    while i < words.Len {
        var word = words.Get(i).Trim();
        if word.Length > 0 {
            if frequency.Has(word) {
                var count = frequency.GetInt(word);
                frequency.SetInt(word, count + 1);
            } else {
                frequency.SetInt(word, 1);
            }
        }
        i = i + 1;
    }

    return frequency;
}

// Usage:
var text = "the quick brown fox jumps over the lazy dog the fox";
var counts = countWords(text);

var keys = counts.Keys();
var i = 0;
while i < keys.Len {
    var word = keys.Get(i);
    Say(word + ": " + Fmt.Int(counts.GetInt(word)));
    i = i + 1;
}
// Output:
// the: 3
// quick: 1
// brown: 1
// fox: 2
// ...
```

---

## Viper.Convert: Parsing Values

Converting strings to other types is so common it gets its own module.

### Basic Parsing

```rust
bind Viper.Convert as Convert;

Convert.ToInt64("42");        // 42
Convert.ToDouble("3.14");     // 3.14
Convert.ToString_Int(42);     // "42"
Convert.ToString_Double(3.14); // "3.14"
```

### Error Handling

Parsing can fail. Handle it gracefully:

```rust
bind Viper.Convert as Convert;
bind Viper.Terminal;

try {
    var num = Convert.ToInt64("not a number");
} catch e {
    Say("Invalid input - please enter a number");
}
```

### Practical Example: Robust Input Function

```rust
bind Viper.Terminal;
bind Viper.Convert as Convert;

func getNumber(prompt: String) -> Integer {
    while true {
        Print(prompt);
        var input = ReadLine().Trim();

        try {
            return Convert.ToInt64(input);
        } catch e {
            Say("Please enter a valid number.");
        }
    }
}

// Usage:
var age = getNumber("Enter your age: ");
```

---

## Viper.IO.File / Viper.IO.Dir / Viper.IO.Path

We covered file operations in Chapter 9, but let's review the key patterns.

### File Operations

```rust
bind Viper.IO.File;

// Reading
var content = readText("data.txt");
var lines = readLines("data.txt");
var bytes = readBytes("image.png");

// Writing
writeText("output.txt", "Hello, World!");
appendText("log.txt", "New entry\n");
writeBytes("copy.png", bytes);

// Checking
if exists("config.json") {
    // Load configuration
}

// Deleting
delete("temp.txt");
```

### Directory Operations

```rust
bind Viper.IO.Dir;

create("output");
createAll("output/reports/2024");  // Creates all intermediate dirs

var files = listFiles("data");
var dirs = listDirs("data");

if exists("backup") {
    // Directory exists
}

delete("temp");
```

### Path Manipulation

The **critical** module for working with file paths:

```rust
bind Viper.IO.Path;

// Join paths safely (handles OS-specific separators)
var path = join("users", "alice", "documents", "file.txt");
// On Windows: users\alice\documents\file.txt
// On macOS/Linux: users/alice/documents/file.txt

// Extract components
fileName("/path/to/file.txt");      // "file.txt"
extension("/path/to/file.txt");     // ".txt"
directory("/path/to/file.txt");     // "/path/to"
baseName("/path/to/file.txt");      // "file" (no extension)

// Normalize paths
normalize("a/b/../c");              // "a/c"

// Check if absolute
isAbsolute("/usr/bin");             // true
isAbsolute("relative/path");        // false
```

### Why Path.Join() Matters

Never concatenate paths with `+`:

```rust
bind Viper.IO.Path;

// BAD - breaks on different operating systems
var path = dir + "/" + filename;

// GOOD - works everywhere
var path = join(dir, filename);
```

Windows uses backslashes (`\`), Unix uses forward slashes (`/`). `Path.Join()` handles this automatically.

---

## Viper.Crypto: Security and Encoding

For hashing, encoding, and unique identifiers.

### Hashing

Hashing converts data into a fixed-size fingerprint. The same input always produces the same hash, but you can't reverse a hash back to the original.

```rust
bind Viper.Crypto.Hash as Hash;

var hash = Hash.MD5("hello");      // 32-character hex string
var sha = Hash.SHA256("hello");    // 64-character hex string
```

**When to use:**
- Verifying file integrity (did this file change?)
- Storing passwords (never store plain text!)
- Creating cache keys
- Detecting duplicates

### Encoding

Base64 encoding converts binary data to text. This lives in `Viper.Codec`:

```rust
bind Viper.Codec;

var encoded = Base64Encode("Hello, World!");
// "SGVsbG8sIFdvcmxkIQ=="

var decoded = Base64Decode(encoded);
// "Hello, World!"
```

**When to use:**
- Embedding binary data in text formats (JSON, XML)
- Basic data obfuscation (not security!)
- Email attachments

### Unique Identifiers

GUIDs (Globally Unique Identifiers) are guaranteed-unique strings:

```rust
bind Viper.Guid;

var id = New();
// "550e8400-e29b-41d4-a716-446655440000"
```

**When to use:**
- Database primary keys
- Session IDs
- Tracking unique objects
- File names that won't collide

### Practical Example: Password Storage

**Never store passwords in plain text.** Hash them:

```rust
bind Viper.Crypto.Hash as Hash;
bind Viper.Guid;
bind Viper.Terminal;

func hashPassword(password: String, salt: String) -> String {
    // Combine password with salt to prevent rainbow table attacks
    var salted = password + salt;
    return Hash.SHA256(salted);
}

func checkPassword(input: String, salt: String, storedHash: String) -> Boolean {
    var inputHash = hashPassword(input, salt);
    return inputHash == storedHash;
}

// Registration:
var salt = New();  // Random salt for this user
var hash = hashPassword(userPassword, salt);
// Store both hash and salt in database

// Login:
if checkPassword(inputPassword, storedSalt, storedHash) {
    Say("Login successful!");
}
```

---

## Putting It All Together

Here's a complete program using multiple standard library modules:

```rust
module StdlibDemo;

bind Viper.Terminal;
bind Viper.Environment;
bind Viper.Time;
bind Viper.Math as Math;
bind Viper.Fmt as Fmt;
bind Viper.Math.Random as Random;
bind Viper.Collections;
bind Viper.Crypto.Hash as Hash;

func start() {
    Say("=== Viper Standard Library Demo ===");
    Say("");

    // Environment
    Say("System Information:");
    Say("  Home: " + GetVariable("HOME"));
    Say("");

    // Time
    var dt = Time.DateTime.Now();
    Say("Current Time:");
    Say("  Year: " + Fmt.Int(Time.DateTime.Year(dt)));
    Say("  Month: " + Fmt.Int(Time.DateTime.Month(dt)));
    Say("");

    // Math
    Say("Math Demo:");
    var angle = Math.Pi / 4.0;
    Say("  sin(45 deg) = " + Fmt.NumFixed(Math.Sin(angle), 4));
    Say("  sqrt(2) = " + Fmt.NumFixed(Math.Sqrt(2.0), 4));
    Say("");

    // Random
    Say("Random Numbers:");
    var i = 0;
    while i < 5 {
        var roll = Random.Dice(6);
        Say("  Dice roll: " + Fmt.Int(roll));
        i = i + 1;
    }
    Say("");

    // Collections
    var scores = new Map[String, Integer]();
    scores.SetInt("Alice", 950);
    scores.SetInt("Bob", 875);
    scores.SetInt("Charlie", 1200);

    Say("Leaderboard:");
    var keys = scores.Keys();
    var j = 0;
    while j < keys.Len {
        var name = keys.Get(j);
        Say("  " + name + ": " + Fmt.Int(scores.GetInt(name)) + " points");
        j = j + 1;
    }
    Say("");

    // Crypto
    var message = "Hello, Viper!";
    Say("Crypto Demo:");
    Say("  Message: " + message);
    Say("  SHA256: " + Hash.SHA256(message).Substring(0, 16) + "...");
    Say("");

    // Performance measurement
    Say("Performance Test:");
    var startMs = Time.Clock.Ticks();

    var sum = 0.0;
    var k = 0;
    while k < 100000 {
        sum = sum + Math.Sqrt(k * 1.0);
        k = k + 1;
    }

    var elapsed = Time.Clock.Ticks() - startMs;
    Say("  100,000 square roots in " + Fmt.Int(elapsed) + " ms");

    Say("");
    Say("Demo complete!");
}
```

---

## Things You Don't Need to Reinvent

Here are some things the standard library does that would be *very hard* to write yourself:

### Accurate Square Roots

A naive approach:

```rust
// DON'T DO THIS - slow and possibly inaccurate
func naiveSqrt(n: Number) -> Number {
    var guess = n / 2.0;
    var i = 0;
    while i < 100 {
        guess = (guess + n / guess) / 2.0;
        i = i + 1;
    }
    return guess;
}
```

The standard library implementation is faster, handles edge cases (0, negative numbers, infinity), and is accurate to the last bit.

### Correct Date Formatting

```rust
// DON'T DO THIS - buggy and incomplete
func formatDate(year: Integer, month: Integer, day: Integer) -> String {
    var monthNames = ["Jan", "Feb", "Mar", ...];  // All 12 months
    return monthNames[month - 1] + " " + day + ", " + year;
}
```

What about time zones? Localization? 12-hour vs 24-hour time? The standard library handles it all.

### Cryptographic Hashing

```rust
// DON'T DO THIS - SHA256 is complex
func sha256(input: String) -> String {
    // This is literally hundreds of lines of careful bit manipulation
    // with specific constants and transformations.
    // One mistake = security vulnerability.
}
```

Cryptography is easy to get wrong. Use the standard library.

### Random Number Generation

```rust
// DON'T DO THIS - not actually random
func badRandom() -> Integer {
    // Most simple approaches produce predictable sequences
    // or have statistical biases.
}
```

Good random number generators are mathematically complex. The standard library uses proven algorithms.

---

## How to Discover Standard Library Features

You won't memorize every function. Here's how to find what you need:

### 1. Explore in Your Editor

Most editors offer autocomplete. Type `Viper.` and see what modules appear. Type `Viper.Math.` and see available functions.

### 2. Read the Documentation

The official Viper documentation covers every module. Keep it bookmarked. Appendix D of this book provides a quick reference.

### 3. Guess Intelligently

Standard libraries follow conventions:
- `something.Len` or `something.Size()` for size
- `something.Contains()` or `something.Has()` for membership
- `Fmt.Int(n)` or `Fmt.Num(x)` for number-to-string conversion

If a function exists, it probably has the name you'd expect.

### 4. Search Before You Write

Before implementing something:
1. Think: "Is this a common problem?"
2. If yes, search: "Viper [what you need]"
3. Check if the standard library has it

You'll be surprised how often the answer is "yes, there's a function for that."

---

## Tips for Learning New APIs

When you encounter a new standard library module:

### Start with the Basics

Don't try to learn every function. Start with the most common operations:

```rust
bind Viper.IO.File;

// For File, start with:
readText()
writeText()
exists()

// Learn the advanced stuff when you need it
```

### Read Examples

Examples teach faster than API reference lists. This chapter is full of examples. Seek out more in documentation and tutorials.

### Experiment in a Scratch File

Create a `test.zia` file and try things:

```rust
module Test;

bind Viper.Math as Math;
bind Viper.Terminal;
bind Viper.Fmt as Fmt;

func start() {
    // Experiment here
    var x = Math.Sqrt(2.0);
    Say(Fmt.NumFixed(x, 4));

    // What happens if...
    var y = Math.Sqrt(-1.0);  // Error? NaN?
    Say(Fmt.NumFixed(y, 4));
}
```

Run it, see what happens, modify, repeat.

### Check Edge Cases

Once you understand the basics, explore edge cases:

- What happens with empty strings?
- What happens with negative numbers?
- What happens with null?
- What happens with very large values?

Understanding edge cases prevents bugs.

---

## Common Patterns

Some standard library patterns appear constantly. Learn these by heart.

### Pattern: Safe User Input

```rust
bind Viper.Terminal;
bind Viper.Convert as Convert;

func getInt(prompt: String) -> Integer {
    while true {
        Print(prompt);
        try {
            return Convert.ToInt64(ReadLine().Trim());
        } catch e {
            Say("Invalid input. Please enter a number.");
        }
    }
}
```

### Pattern: Read Config with Default

```rust
bind Viper.Environment;

func getConfig(key: String, defaultValue: String) -> String {
    if HasVariable(key) {
        var envValue = GetVariable(key);
        if envValue.Length > 0 {
            return envValue;
        }
    }
    return defaultValue;
}
```

### Pattern: Safe File Read

```rust
bind Viper.IO.File;
bind Viper.Terminal;

func readFileSafe(path: String) -> String {
    if !exists(path) {
        return "";
    }

    try {
        return readText(path);
    } catch e {
        Say("Error reading " + path + ": " + e.message);
        return "";
    }
}
```

### Pattern: Measure Performance

```rust
bind Viper.Time;
bind Viper.Terminal;
bind Viper.Fmt as Fmt;

func timed(name: String, operation: func()) {
    var start = Time.Clock.Ticks();
    operation();
    var elapsed = Time.Clock.Ticks() - start;
    Say(name + " took " + Fmt.Int(elapsed) + " ms");
}

// Usage:
timed("Sort", func() {
    sortLargeArray(data);
});
```

### Pattern: Build a Path

```rust
bind Viper.IO.Path;
bind Viper.Environment;
bind Viper.Time;
bind Viper.Fmt as Fmt;

var dt = Time.DateTime.Now();
var dateStr = Fmt.IntPad(Time.DateTime.Year(dt), 4, "0") + "-" +
              Fmt.IntPad(Time.DateTime.Month(dt), 2, "0") + "-" +
              Fmt.IntPad(Time.DateTime.Day(dt), 2, "0");

var logFile = join(
    GetVariable("HOME"),
    ".myapp",
    "logs",
    dateStr + ".log"
);
```

---

## Summary

The Viper standard library provides:

| Category | Modules | Key Functions |
|----------|---------|---------------|
| I/O | Terminal, IO.File, IO.Dir, IO.Path | Say, ReadLine, readText, join |
| Numbers | Math, Math.Random, Convert | Math.Sqrt, Math.Sin, Random.Range, Convert.ToInt64 |
| Text | String, Fmt | String.Trim, String.Split, Fmt.Int, Fmt.NumFixed |
| Time | Time | Time.DateTime.Now, Time.Clock.Ticks, Time.Clock.Sleep |
| Data | Collections | List.Push, Map.SetInt, Bag.Put |
| System | Environment | GetArgument, GetVariable, GetOS |
| Security | Crypto.Hash, Codec, Guid | Hash.SHA256, Hash.MD5, New |

The standard library is your first resort when you need functionality. It's tested, optimized, and familiar to other programmers. Learning it is as important as learning the language syntax.

Think of the standard library as your tool belt. You don't build a hammer every time you need to drive a nail. You reach for the tool that's already there.

---

## Exercises

**Exercise 13.1**: Write a program that prints the current date in three different formats: ISO (2024-03-15), US (March 15, 2024), and EU (15/03/2024).

**Exercise 13.2**: Use `Viper.Math` to calculate the distance between two points (x1, y1) and (x2, y2) using the Pythagorean theorem.

**Exercise 13.3**: Create a simple stopwatch: prompt to start, wait for user input, then display elapsed time in seconds and milliseconds.

**Exercise 13.4**: Use `Viper.Collections.Map` to build a word frequency counter. Given a sentence, count how many times each word appears.

**Exercise 13.5**: Write a program that takes a filename as a command-line argument and prints its SHA256 hash. If the file doesn't exist, print a helpful error message.

**Exercise 13.6**: Build a function that validates an email address (must contain @, must have text before and after @, domain must contain a dot).

**Exercise 13.7**: Create a password generator that accepts length as an argument and generates a random password with lowercase, uppercase, digits, and symbols.

**Exercise 13.8** (Challenge): Build a simple file backup utility. It should:
- Take a source file path
- Create a backup directory if it doesn't exist
- Copy the file with a timestamp in the name (e.g., `file_2024-03-15_143022.txt`)
- Print a summary of what it did

**Exercise 13.9** (Challenge): Create a "quote of the day" program that:
- Stores quotes in a file (one per line)
- Uses today's date as a seed for the random number generator
- Displays a "random" quote that stays the same all day
- Shows a different quote tomorrow

---

*We've finished Part II! You now have the building blocks: strings, files, error handling, structures, modules, and the standard library.*

*Part III introduces object-oriented programming — a powerful way to model complex systems with objects that combine data and behavior.*

*[Continue to Part III: Thinking in Objects](../part3-objects/14-objects.md)*
