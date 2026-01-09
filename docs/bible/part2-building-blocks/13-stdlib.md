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

3. **Other programmers know it.** When you use `Viper.Math.sqrt()`, any Viper programmer knows what it does. Your custom `mySquareRoot()` function? They'd have to read your code.

The standard library is a superpower. Learning it makes you a dramatically more productive programmer.

---

## A Tour of Viper's Standard Library

Viper's standard library is organized into modules under the `Viper` namespace. Each module handles a different category of tasks:

| Module | Purpose | You'll Use It For |
|--------|---------|-------------------|
| `Viper.Terminal` | Console input/output | User interaction, debugging |
| `Viper.File` | File operations | Reading/writing data |
| `Viper.Dir` | Directory operations | Navigating the filesystem |
| `Viper.Path` | Path manipulation | Building file paths safely |
| `Viper.Parse` | String-to-number conversion | Processing user input |
| `Viper.Fmt` | String formatting | Creating output messages |
| `Viper.Math` | Mathematical functions | Calculations, geometry |
| `Viper.Random` | Random number generation | Games, simulations, testing |
| `Viper.Time` | Date and time | Timestamps, scheduling |
| `Viper.Environment` | System information | Config, command-line args |
| `Viper.Text` | Advanced string operations | Text processing |
| `Viper.Collections` | Data structures | Lists, maps, sets |
| `Viper.Network` | TCP/UDP networking | Web clients, servers |
| `Viper.Threads` | Concurrency | Parallel processing |
| `Viper.Crypto` | Hashing and encoding | Security, verification |

Let's explore each one in depth.

---

## Viper.Terminal: Console I/O

You've used `Viper.Terminal` throughout this book. It's how your programs talk to users.

### Basic Input and Output

```rust
Viper.Terminal.Say("Hello!");              // Print with newline
Viper.Terminal.Print("No newline here");   // Print without newline
var input = Viper.Terminal.ReadLine();     // Read a line of text
var char = Viper.Terminal.ReadKey();       // Read a single keypress
```

### When to Use Each

- **`Say()`**: Most output. Each message on its own line.
- **`Print()`**: When you want to build up a line piece by piece, or when prompting for input on the same line.
- **`ReadLine()`**: Getting text input from users.
- **`ReadKey()`**: Games, menus, or "press any key" prompts.

### Terminal Control

For interactive applications, you can control the cursor and colors:

```rust
Viper.Terminal.Clear();                    // Clear the screen
Viper.Terminal.SetCursor(10, 5);           // Move cursor to column 10, row 5
Viper.Terminal.SetColor("red", "black");   // Red text on black background
Viper.Terminal.ResetColor();               // Back to defaults
Viper.Terminal.HideCursor();               // Hide the blinking cursor
Viper.Terminal.ShowCursor();               // Show it again
```

These are essential for building text-based games, progress bars, or any program where you want precise control over what the user sees.

### Practical Example: A Simple Menu

```rust
func showMenu() -> i64 {
    Viper.Terminal.Clear();
    Viper.Terminal.Say("=== Main Menu ===");
    Viper.Terminal.Say("1. New Game");
    Viper.Terminal.Say("2. Load Game");
    Viper.Terminal.Say("3. Options");
    Viper.Terminal.Say("4. Quit");
    Viper.Terminal.Print("Choose (1-4): ");

    var choice = Viper.Terminal.ReadLine().trim();
    return Viper.Parse.Int(choice);
}
```

---

## Viper.Math: Mathematics

Math operations beyond basic arithmetic live in `Viper.Math`. These functions have been implemented by experts, optimized for speed, and tested for accuracy across edge cases.

### Basic Functions

```rust
Viper.Math.abs(-5);          // 5 (absolute value)
Viper.Math.min(3, 7);        // 3 (smaller of two)
Viper.Math.max(3, 7);        // 7 (larger of two)
Viper.Math.clamp(15, 0, 10); // 10 (constrain to range 0-10)
```

### Rounding

```rust
Viper.Math.floor(3.7);       // 3.0 (round down)
Viper.Math.ceil(3.2);        // 4.0 (round up)
Viper.Math.round(3.5);       // 4.0 (round to nearest)
Viper.Math.round(3.4);       // 3.0
```

### Powers and Roots

```rust
Viper.Math.sqrt(16.0);       // 4.0 (square root)
Viper.Math.pow(2.0, 8.0);    // 256.0 (2 to the 8th power)
Viper.Math.log(2.718);       // ~1.0 (natural logarithm)
Viper.Math.log10(100.0);     // 2.0 (base-10 logarithm)
Viper.Math.exp(1.0);         // ~2.718 (e^x)
```

### Trigonometry

```rust
Viper.Math.sin(0.0);         // 0.0
Viper.Math.cos(0.0);         // 1.0
Viper.Math.tan(0.0);         // 0.0
Viper.Math.atan2(y, x);      // Angle from coordinates
```

### Constants

```rust
Viper.Math.PI;               // 3.14159265358979...
Viper.Math.E;                // 2.71828182845904...
```

### When Would You Use This?

**Game development:** Calculate distances, angles, movement vectors.

```rust
// Distance between two points
func distance(x1: f64, y1: f64, x2: f64, y2: f64) -> f64 {
    var dx = x2 - x1;
    var dy = y2 - y1;
    return Viper.Math.sqrt(dx * dx + dy * dy);
}
```

**Scientific calculations:** Statistics, physics simulations, financial modeling.

```rust
// Compound interest
func compoundInterest(principal: f64, rate: f64, years: i64) -> f64 {
    return principal * Viper.Math.pow(1.0 + rate, years.toFloat());
}
```

**Graphics:** Smooth animations, circular motion, wave patterns.

```rust
// Move in a circle
var angle = time * speed;
var x = centerX + radius * Viper.Math.cos(angle);
var y = centerY + radius * Viper.Math.sin(angle);
```

### You Don't Want to Write This Yourself

Computing square roots, trigonometric functions, and logarithms accurately is *hard*. These algorithms have been refined for decades. A naive square root implementation might:

- Be slow (iterating too many times)
- Be inaccurate (floating-point errors)
- Crash on edge cases (negative numbers, infinity)

The standard library handles all of this. Use it.

---

## Viper.Random: Randomness

Games need dice rolls. Simulations need random data. Testing needs random inputs. `Viper.Random` provides it all.

### Basic Random Values

```rust
Viper.Random.int(1, 100);       // Random integer from 1 to 100 (inclusive)
Viper.Random.float();           // Random float from 0.0 to 1.0
Viper.Random.bool();            // Random true or false
```

### Working with Collections

```rust
var options = ["rock", "paper", "scissors"];
var choice = Viper.Random.choice(options);  // Random element

var deck = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
Viper.Random.shuffle(deck);                 // Shuffle in place
```

### Reproducible Randomness

For testing or game replays, you can *seed* the random number generator:

```rust
Viper.Random.seed(12345);  // Same seed = same sequence of "random" numbers
```

This is crucial for debugging. If a bug only appears sometimes, set a seed to reproduce it reliably.

### Practical Examples

**Dice roll:**
```rust
var die = Viper.Random.int(1, 6);
Viper.Terminal.Say("You rolled: " + die);
```

**Coin flip:**
```rust
if Viper.Random.bool() {
    Viper.Terminal.Say("Heads!");
} else {
    Viper.Terminal.Say("Tails!");
}
```

**Random enemy spawn:**
```rust
var enemies = ["goblin", "orc", "troll", "dragon"];
var weights = [50, 30, 15, 5];  // More common to less common

// Simple weighted random (for illustration)
var roll = Viper.Random.int(1, 100);
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
func generatePassword(length: i64) -> string {
    var chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%";
    var password = "";

    for i in 0..length {
        var index = Viper.Random.int(0, chars.length - 1);
        password += chars[index];
    }

    return password;
}
```

---

## Viper.Time: Date and Time

Time is surprisingly complex. Leap years, time zones, daylight saving, calendar systems. The standard library handles the complexity so you don't have to.

### Getting the Current Time

```rust
var now = Viper.Time.now();

Viper.Terminal.Say("Year: " + now.year);
Viper.Terminal.Say("Month: " + now.month);
Viper.Terminal.Say("Day: " + now.day);
Viper.Terminal.Say("Hour: " + now.hour);
Viper.Terminal.Say("Minute: " + now.minute);
Viper.Terminal.Say("Second: " + now.second);
```

### Formatting Dates

Dates need to be displayed in different formats depending on context:

```rust
var now = Viper.Time.now();

// ISO format (international standard)
Viper.Terminal.Say(now.format("YYYY-MM-DD"));        // 2024-03-15

// With time
Viper.Terminal.Say(now.format("YYYY-MM-DD HH:mm:ss")); // 2024-03-15 14:30:45

// Human-friendly
Viper.Terminal.Say(now.format("MMMM D, YYYY"));      // March 15, 2024

// Time only
Viper.Terminal.Say(now.format("h:mm A"));            // 2:30 PM
```

Common format codes:
- `YYYY` - 4-digit year
- `MM` - 2-digit month (01-12)
- `DD` - 2-digit day (01-31)
- `HH` - 24-hour hours (00-23)
- `hh` - 12-hour hours (01-12)
- `mm` - Minutes (00-59)
- `ss` - Seconds (00-59)
- `A` - AM/PM

### Measuring Elapsed Time

For performance measurement or timing games:

```rust
var start = Viper.Time.millis();

// Do some work...
processData();

var elapsed = Viper.Time.millis() - start;
Viper.Terminal.Say("Processing took " + elapsed + " ms");
```

### Delays and Pauses

```rust
Viper.Terminal.Say("Loading...");
Viper.Time.sleep(2000);  // Pause for 2000 milliseconds (2 seconds)
Viper.Terminal.Say("Done!");
```

### Practical Example: Simple Stopwatch

```rust
func stopwatch() {
    Viper.Terminal.Say("Press Enter to start...");
    Viper.Terminal.ReadLine();

    var start = Viper.Time.millis();
    Viper.Terminal.Say("Stopwatch running. Press Enter to stop.");
    Viper.Terminal.ReadLine();

    var elapsed = Viper.Time.millis() - start;
    var seconds = elapsed / 1000.0;

    Viper.Terminal.Say(Viper.Fmt.format("Elapsed: {:.2} seconds", seconds));
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
$ viper myprogram.viper input.txt --verbose
```

You can access those arguments:

```rust
var args = Viper.Environment.args();

for arg in args {
    Viper.Terminal.Say("Argument: " + arg);
}
// Output:
// Argument: myprogram.viper
// Argument: input.txt
// Argument: --verbose
```

### Environment Variables

Operating systems have configuration through environment variables:

```rust
var home = Viper.Environment.get("HOME");       // /Users/alice
var path = Viper.Environment.get("PATH");       // System PATH
var editor = Viper.Environment.get("EDITOR");   // Preferred text editor

// Check if a variable exists
if Viper.Environment.has("DEBUG") {
    // Debug mode is enabled
    enableVerboseLogging();
}
```

### System Information

```rust
var os = Viper.Environment.os();             // "windows", "macos", or "linux"
var home = Viper.Environment.homeDir();      // User's home directory
var cwd = Viper.Environment.currentDir();    // Current working directory
```

### Practical Example: Cross-Platform Configuration

```rust
func getConfigPath() -> string {
    var os = Viper.Environment.os();
    var home = Viper.Environment.homeDir();

    if os == "windows" {
        return Viper.Path.join(home, "AppData", "Local", "MyApp", "config.json");
    } else if os == "macos" {
        return Viper.Path.join(home, "Library", "Application Support", "MyApp", "config.json");
    } else {
        return Viper.Path.join(home, ".config", "myapp", "config.json");
    }
}
```

---

## Viper.Fmt: String Formatting

Concatenating strings with `+` gets messy for complex output. `Viper.Fmt` makes it cleaner.

### Basic Formatting

```rust
var name = "Alice";
var score = 95;

// Instead of:
var msg = "Player " + name + " scored " + score + " points!";

// Use:
var msg = Viper.Fmt.format("Player {} scored {} points!", name, score);
// "Player Alice scored 95 points!"
```

The `{}` placeholders are replaced by arguments in order.

### Number Formatting

```rust
// Decimal places
Viper.Fmt.format("{:.2}", 3.14159);    // "3.14"
Viper.Fmt.format("{:.4}", 3.14159);    // "3.1416"

// Zero padding
Viper.Fmt.format("{:05}", 42);         // "00042"
Viper.Fmt.format("{:08}", 1234);       // "00001234"

// Alignment
Viper.Fmt.format("{:>10}", "hi");      // "        hi" (right-align)
Viper.Fmt.format("{:<10}", "hi");      // "hi        " (left-align)
Viper.Fmt.format("{:^10}", "hi");      // "    hi    " (center)
```

### Practical Example: Formatted Table

```rust
func printScoreboard(players: [Player]) {
    Viper.Terminal.Say(Viper.Fmt.format("{:<15} {:>10}", "Name", "Score"));
    Viper.Terminal.Say("-".repeat(26));

    for player in players {
        Viper.Terminal.Say(Viper.Fmt.format("{:<15} {:>10}", player.name, player.score));
    }
}

// Output:
// Name                 Score
// --------------------------
// Alice                  950
// Bob                    875
// Charlie               1200
```

---

## Viper.Text: String Utilities

Beyond basic string methods, `Viper.Text` provides advanced text operations.

### Padding and Alignment

```rust
Viper.Text.padLeft("42", 5, '0');     // "00042"
Viper.Text.padRight("hi", 5, ' ');    // "hi   "
Viper.Text.padCenter("hi", 8, '-');   // "---hi---"
```

### Repetition and Reversal

```rust
Viper.Text.repeat("ab", 3);           // "ababab"
Viper.Text.repeat("-", 40);           // A line of dashes
Viper.Text.reverse("hello");          // "olleh"
```

### Character Classification

```rust
Viper.Text.isDigit('5');              // true
Viper.Text.isDigit('a');              // false
Viper.Text.isLetter('A');             // true
Viper.Text.isWhitespace(' ');         // true
Viper.Text.isWhitespace('\t');        // true
Viper.Text.isAlphanumeric('a');       // true
Viper.Text.isAlphanumeric('5');       // true
Viper.Text.isAlphanumeric('!');       // false
```

### Text Splitting

```rust
Viper.Text.lines("a\nb\nc");          // ["a", "b", "c"]
Viper.Text.words("hello world");      // ["hello", "world"]
```

### Practical Example: Validating User Input

```rust
func isValidUsername(username: string) -> bool {
    // Must be 3-20 characters
    if username.length < 3 || username.length > 20 {
        return false;
    }

    // Must start with a letter
    if !Viper.Text.isLetter(username[0]) {
        return false;
    }

    // All characters must be alphanumeric or underscore
    for i in 0..username.length {
        var c = username[i];
        if !Viper.Text.isAlphanumeric(c) && c != '_' {
            return false;
        }
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
var list = Viper.Collections.List.new();

list.add("first");
list.add("second");
list.add("third");

Viper.Terminal.Say(list.get(0));      // "first"
Viper.Terminal.Say(list.size());      // 3

list.remove(0);                        // Remove first element
list.insert(1, "inserted");            // Insert at position 1
list.clear();                          // Remove all elements
```

**When to use:** When you don't know how many elements you'll have, or when you need to add/remove frequently.

### Map: Key-Value Pairs

Maps store associations between keys and values:

```rust
var scores = Viper.Collections.Map.new();

scores.set("Alice", "950");
scores.set("Bob", "875");
scores.set("Charlie", "1200");

Viper.Terminal.Say(scores.get("Alice"));   // "950"
Viper.Terminal.Say(scores.size());         // 3

if scores.has("David") {
    Viper.Terminal.Say("David is in the game");
} else {
    Viper.Terminal.Say("David hasn't played yet");
}

// Iterate over all entries
for key in scores.keys() {
    Viper.Terminal.Say(key + ": " + scores.get(key));
}

scores.remove("Bob");
```

**When to use:** When you need to look things up by name, ID, or any other key. Faster than searching an array.

### Set: Unique Values

Sets store unique values with no duplicates:

```rust
var tags = Viper.Collections.Set.new();

tags.add("important");
tags.add("urgent");
tags.add("important");  // Ignored - already exists

Viper.Terminal.Say(tags.size());      // 2
Viper.Terminal.Say(tags.has("urgent")); // true

tags.remove("urgent");
```

**When to use:** When you need to track unique items, check membership quickly, or remove duplicates.

### Practical Example: Word Frequency Counter

```rust
func countWords(text: string) -> Map {
    var frequency = Viper.Collections.Map.new();
    var words = text.lower().split(" ");

    for word in words {
        word = word.trim();
        if word.length == 0 {
            continue;
        }

        if frequency.has(word) {
            var count = Viper.Parse.Int(frequency.get(word));
            frequency.set(word, (count + 1).toString());
        } else {
            frequency.set(word, "1");
        }
    }

    return frequency;
}

// Usage:
var text = "the quick brown fox jumps over the lazy dog the fox";
var counts = countWords(text);

for word in counts.keys() {
    Viper.Terminal.Say(word + ": " + counts.get(word));
}
// Output:
// the: 3
// quick: 1
// brown: 1
// fox: 2
// ...
```

---

## Viper.Parse: Parsing Values

Converting strings to other types is so common it gets its own module.

### Basic Parsing

```rust
Viper.Parse.Int("42");             // 42
Viper.Parse.Float("3.14");         // 3.14
Viper.Parse.Bool("true");          // true
Viper.Parse.Bool("false");         // false
```

### Error Handling

Parsing can fail. Handle it gracefully:

```rust
try {
    var num = Viper.Parse.Int("not a number");
} catch e {
    Viper.Terminal.Say("Invalid input - please enter a number");
}
```

### Practical Example: Robust Input Function

```rust
func getNumber(prompt: string) -> i64 {
    while true {
        Viper.Terminal.Print(prompt);
        var input = Viper.Terminal.ReadLine().trim();

        try {
            return Viper.Parse.Int(input);
        } catch e {
            Viper.Terminal.Say("Please enter a valid number.");
        }
    }
}

// Usage:
var age = getNumber("Enter your age: ");
```

---

## Viper.File / Viper.Dir / Viper.Path

We covered file operations in Chapter 9, but let's review the key patterns.

### File Operations

```rust
// Reading
var content = Viper.File.readText("data.txt");
var lines = Viper.File.readLines("data.txt");
var bytes = Viper.File.readBytes("image.png");

// Writing
Viper.File.writeText("output.txt", "Hello, World!");
Viper.File.appendText("log.txt", "New entry\n");
Viper.File.writeBytes("copy.png", bytes);

// Checking
if Viper.File.exists("config.json") {
    // Load configuration
}

// Deleting
Viper.File.delete("temp.txt");
```

### Directory Operations

```rust
Viper.Dir.create("output");
Viper.Dir.createAll("output/reports/2024");  // Creates all intermediate dirs

var files = Viper.Dir.listFiles("data");
var dirs = Viper.Dir.listDirs("data");

if Viper.Dir.exists("backup") {
    // Directory exists
}

Viper.Dir.delete("temp");
```

### Path Manipulation

The **critical** module for working with file paths:

```rust
// Join paths safely (handles OS-specific separators)
var path = Viper.Path.join("users", "alice", "documents", "file.txt");
// On Windows: users\alice\documents\file.txt
// On macOS/Linux: users/alice/documents/file.txt

// Extract components
Viper.Path.fileName("/path/to/file.txt");      // "file.txt"
Viper.Path.extension("/path/to/file.txt");     // ".txt"
Viper.Path.directory("/path/to/file.txt");     // "/path/to"
Viper.Path.baseName("/path/to/file.txt");      // "file" (no extension)

// Normalize paths
Viper.Path.normalize("a/b/../c");              // "a/c"

// Check if absolute
Viper.Path.isAbsolute("/usr/bin");             // true
Viper.Path.isAbsolute("relative/path");        // false
```

### Why Path.join() Matters

Never concatenate paths with `+`:

```rust
// BAD - breaks on different operating systems
var path = dir + "/" + filename;

// GOOD - works everywhere
var path = Viper.Path.join(dir, filename);
```

Windows uses backslashes (`\`), Unix uses forward slashes (`/`). `Path.join()` handles this automatically.

---

## Viper.Crypto: Security and Encoding

For hashing, encoding, and unique identifiers.

### Hashing

Hashing converts data into a fixed-size fingerprint. The same input always produces the same hash, but you can't reverse a hash back to the original.

```rust
var hash = Viper.Crypto.md5("hello");      // 32-character hex string
var sha = Viper.Crypto.sha256("hello");    // 64-character hex string
```

**When to use:**
- Verifying file integrity (did this file change?)
- Storing passwords (never store plain text!)
- Creating cache keys
- Detecting duplicates

### Encoding

Base64 encoding converts binary data to text:

```rust
var encoded = Viper.Crypto.base64Encode("Hello, World!");
// "SGVsbG8sIFdvcmxkIQ=="

var decoded = Viper.Crypto.base64Decode(encoded);
// "Hello, World!"
```

**When to use:**
- Embedding binary data in text formats (JSON, XML)
- Basic data obfuscation (not security!)
- Email attachments

### Unique Identifiers

GUIDs (Globally Unique Identifiers) are guaranteed-unique strings:

```rust
var id = Viper.Crypto.guid();
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
func hashPassword(password: string, salt: string) -> string {
    // Combine password with salt to prevent rainbow table attacks
    var salted = password + salt;
    return Viper.Crypto.sha256(salted);
}

func checkPassword(input: string, salt: string, storedHash: string) -> bool {
    var inputHash = hashPassword(input, salt);
    return inputHash == storedHash;
}

// Registration:
var salt = Viper.Crypto.guid();  // Random salt for this user
var hash = hashPassword(userPassword, salt);
// Store both hash and salt in database

// Login:
if checkPassword(inputPassword, storedSalt, storedHash) {
    Viper.Terminal.Say("Login successful!");
}
```

---

## Putting It All Together

Here's a complete program using multiple standard library modules:

```rust
module StdlibDemo;

func start() {
    Viper.Terminal.Say("=== Viper Standard Library Demo ===");
    Viper.Terminal.Say("");

    // Environment
    Viper.Terminal.Say("System Information:");
    Viper.Terminal.Say("  OS: " + Viper.Environment.os());
    Viper.Terminal.Say("  Home: " + Viper.Environment.homeDir());
    Viper.Terminal.Say("");

    // Time
    var now = Viper.Time.now();
    Viper.Terminal.Say("Current Time:");
    Viper.Terminal.Say("  " + now.format("MMMM D, YYYY h:mm A"));
    Viper.Terminal.Say("");

    // Math
    Viper.Terminal.Say("Math Demo:");
    var angle = Viper.Math.PI / 4.0;
    Viper.Terminal.Say(Viper.Fmt.format("  sin(45 deg) = {:.4}", Viper.Math.sin(angle)));
    Viper.Terminal.Say(Viper.Fmt.format("  sqrt(2) = {:.4}", Viper.Math.sqrt(2.0)));
    Viper.Terminal.Say("");

    // Random
    Viper.Terminal.Say("Random Numbers:");
    for i in 0..5 {
        var roll = Viper.Random.int(1, 6);
        Viper.Terminal.Say("  Dice roll: " + roll);
    }
    Viper.Terminal.Say("");

    // Collections
    var scores = Viper.Collections.Map.new();
    scores.set("Alice", "950");
    scores.set("Bob", "875");
    scores.set("Charlie", "1200");

    Viper.Terminal.Say("Leaderboard:");
    for name in scores.keys() {
        Viper.Terminal.Say(Viper.Fmt.format("  {:<10} {:>6} points", name, scores.get(name)));
    }
    Viper.Terminal.Say("");

    // Crypto
    var message = "Hello, Viper!";
    Viper.Terminal.Say("Crypto Demo:");
    Viper.Terminal.Say("  Message: " + message);
    Viper.Terminal.Say("  SHA256: " + Viper.Crypto.sha256(message).substring(0, 16) + "...");
    Viper.Terminal.Say("  Base64: " + Viper.Crypto.base64Encode(message));
    Viper.Terminal.Say("");

    // Performance measurement
    Viper.Terminal.Say("Performance Test:");
    var start = Viper.Time.millis();

    var sum = 0.0;
    for i in 0..100000 {
        sum += Viper.Math.sqrt(i.toFloat());
    }

    var elapsed = Viper.Time.millis() - start;
    Viper.Terminal.Say(Viper.Fmt.format("  100,000 square roots in {} ms", elapsed));

    Viper.Terminal.Say("");
    Viper.Terminal.Say("Demo complete!");
}
```

---

## Things You Don't Need to Reinvent

Here are some things the standard library does that would be *very hard* to write yourself:

### Accurate Square Roots

A naive approach:

```rust
// DON'T DO THIS - slow and possibly inaccurate
func naiveSqrt(n: f64) -> f64 {
    var guess = n / 2.0;
    for i in 0..100 {
        guess = (guess + n / guess) / 2.0;
    }
    return guess;
}
```

The standard library implementation is faster, handles edge cases (0, negative numbers, infinity), and is accurate to the last bit.

### Correct Date Formatting

```rust
// DON'T DO THIS - buggy and incomplete
func formatDate(year: i64, month: i64, day: i64) -> string {
    var monthNames = ["Jan", "Feb", "Mar", ...];  // All 12 months
    return monthNames[month - 1] + " " + day + ", " + year;
}
```

What about time zones? Localization? 12-hour vs 24-hour time? The standard library handles it all.

### Cryptographic Hashing

```rust
// DON'T DO THIS - SHA256 is complex
func sha256(input: string) -> string {
    // This is literally hundreds of lines of careful bit manipulation
    // with specific constants and transformations.
    // One mistake = security vulnerability.
}
```

Cryptography is easy to get wrong. Use the standard library.

### Random Number Generation

```rust
// DON'T DO THIS - not actually random
func badRandom() -> i64 {
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
- `something.length` or `something.size()` for size
- `something.contains()` for membership
- `something.toString()` for string conversion

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
// For Viper.File, start with:
Viper.File.readText()
Viper.File.writeText()
Viper.File.exists()

// Learn the advanced stuff when you need it
```

### Read Examples

Examples teach faster than API reference lists. This chapter is full of examples. Seek out more in documentation and tutorials.

### Experiment in a Scratch File

Create a `test.viper` file and try things:

```rust
module Test;

func start() {
    // Experiment here
    var x = Viper.Math.sqrt(2.0);
    Viper.Terminal.Say(x);

    // What happens if...
    var y = Viper.Math.sqrt(-1.0);  // Error? NaN?
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
func getInt(prompt: string) -> i64 {
    while true {
        Viper.Terminal.Print(prompt);
        try {
            return Viper.Parse.Int(Viper.Terminal.ReadLine().trim());
        } catch e {
            Viper.Terminal.Say("Invalid input. Please enter a number.");
        }
    }
}
```

### Pattern: Read Config with Default

```rust
func getConfig(key: string, defaultValue: string) -> string {
    var envValue = Viper.Environment.get(key);
    if envValue != null && envValue.length > 0 {
        return envValue;
    }
    return defaultValue;
}
```

### Pattern: Safe File Read

```rust
func readFileSafe(path: string) -> string {
    if !Viper.File.exists(path) {
        return "";
    }

    try {
        return Viper.File.readText(path);
    } catch e {
        Viper.Terminal.Say("Error reading " + path + ": " + e.message);
        return "";
    }
}
```

### Pattern: Measure Performance

```rust
func timed(name: string, operation: func()) {
    var start = Viper.Time.millis();
    operation();
    var elapsed = Viper.Time.millis() - start;
    Viper.Terminal.Say(name + " took " + elapsed + " ms");
}

// Usage:
timed("Sort", func() {
    sortLargeArray(data);
});
```

### Pattern: Build a Path

```rust
var logFile = Viper.Path.join(
    Viper.Environment.homeDir(),
    ".myapp",
    "logs",
    Viper.Time.now().format("YYYY-MM-DD") + ".log"
);
```

---

## Summary

The Viper standard library provides:

| Category | Modules | Key Functions |
|----------|---------|---------------|
| I/O | Terminal, File, Dir, Path | Say, ReadLine, readText, join |
| Numbers | Math, Random, Parse | sqrt, sin, int, Int/Float |
| Text | Text, Fmt | format, padLeft, isDigit |
| Time | Time | now, format, millis, sleep |
| Data | Collections | List, Map, Set |
| System | Environment | args, get, os |
| Security | Crypto | sha256, base64Encode, guid |

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
