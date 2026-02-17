# Appendix D: Runtime Library Reference

This reference documents the Viper Runtime Library, the standard modules available in every Viper program. Use this appendix to quickly look up function signatures, understand parameters, and see practical examples.

**How to use this reference:** Each entry includes the complete function signature, a brief explanation of its purpose, return type information, and at least one example. Edge cases and common errors are noted where applicable.

---

## Quick Navigation

- [Viper.Terminal](#viperterminal) - Console I/O
- [Viper.IO.File](#viperio-file) - File system operations
- [Viper.Math](#vipermath) - Mathematical functions
- [Viper.Time](#vipertime) - Time and date operations
- [Viper.Collections](#vipercollections) - Data structures
- [Viper.Network](#vipernetwork) - HTTP, TCP, UDP
- [Viper.Text.Json](#vipertextjson) - JSON parsing/generation
- [Viper.Threads](#viperthreads) - Concurrency primitives
- [Viper.Graphics](#vipergraphics) - Drawing and windows
- [Viper.GUI](#vipergui) - Widget-based user interfaces
- [Viper.Input](#viperinput) - Keyboard, mouse, gamepad
- [Viper.Crypto](#vipercrypto) - Hashing and encoding
- [Viper.Environment](#viperenvironment) - System information
- [Viper.Text.Pattern](#vipertextpattern) - Regular expressions
- [Viper.Exec](#viperexec) - External processes
- [Viper.Test](#vipertest) - Testing utilities

---

## Viper.Terminal

Console input and output operations for text-based programs. This is the most fundamental module - you will use it in nearly every program.

> **See also:** [Chapter 2: Your First Program](../part1-foundations/02-first-program.md) for an introduction to terminal output.

---

### Output Functions

#### Say

```rust
func Say(message: string) -> void
```

Prints a message to standard output followed by a newline. This is your primary output function for displaying text to users.

**Parameters:**
- `message` - The text to display (any type; non-strings are automatically converted)

**Example:**
```rust
Terminal.Say("Hello, World!");
Terminal.Say("The answer is: " + 42);
Terminal.Say(3.14159);  // Prints "3.14159"
```

**When to use:** Use `Say` when you want each message on its own line. This is the standard choice for most output.

---

#### Print

```rust
func Print(message: string) -> void
```

Prints a message without a trailing newline. Subsequent output continues on the same line.

**Parameters:**
- `message` - The text to display

**Example:**
```rust
Terminal.Print("Loading");
for i in 0..5 {
    Time.Clock.Sleep(500);
    Terminal.Print(".");
}
Terminal.Say(" done!");  // Output: Loading..... done!
```

**When to use:** Use `Print` for progress indicators, inline prompts, or when building output piece by piece.

---

#### SayError

```rust
func SayError(message: string) -> void
```

> **Note:** `SayError` is planned but not yet available in all runtime configurations. For error output today, use `Terminal.Say` with an error prefix.

Prints a message to standard error (stderr) with a newline. Error messages go to a separate stream that can be redirected independently from normal output.

**Parameters:**
- `message` - The error message to display

**Example:**
```rust
bind Viper.IO;
bind Viper.Terminal;

if IO.File.Exists(filename) == false {
    Terminal.Say("Error: File not found: " + filename);
    Viper.Environment.exit(1);
}
```

**When to use:** Use for error messages, warnings, and diagnostic output.

---

### Input Functions

#### Ask

```rust
func Ask(prompt: string) -> string?
```

Displays a prompt and waits for the user to type a line of input. Returns when the user presses Enter.

**Parameters:**
- `prompt` - Text to display before waiting for input

**Returns:** The user's input as a nullable string (without the trailing newline), or `null` if EOF is reached

**Example:**
```rust
bind Viper.Terminal;
bind Viper.Convert;

var name = Terminal.Ask("What is your name? ");
var ageStr = Terminal.Ask("How old are you? ");
var age = ToInt(ageStr);

Terminal.Say("Hello, " + name + "! You are " + age + " years old.");
```

**When to use:** Use `Ask` for any situation where you need user input, from simple prompts to form-style data entry.

**Edge cases:**
- Returns an empty string if the user presses Enter without typing anything
- If input is redirected from a file and EOF is reached, returns `null`

---

#### ReadLine

```rust
func ReadLine() -> string?
```

Reads a line of input from the user without displaying a prompt. Returns when the user presses Enter.

**Returns:** The user's input as a nullable string (without the trailing newline), or `null` if EOF is reached

**Example:**
```rust
bind Viper.Terminal;
bind Viper.Convert;

Terminal.Print("Enter your name: ");
var name = Terminal.ReadLine();

Terminal.Print("Enter your age: ");
var ageStr = Terminal.ReadLine();
var age = ToInt(ageStr);

Terminal.Say("Hello, " + name + "! You are " + age + " years old.");
```

**When to use:** Use `ReadLine` when you want to display the prompt yourself using `Print` before reading input. Use `Ask` when you want to display the prompt and read input in a single call.

**Edge cases:**
- Returns an empty string if the user presses Enter without typing anything
- If input is redirected from a file and EOF is reached, returns `null`

---

#### InKey

```rust
func InKey() -> string
```

Reads a single character from input without waiting for Enter. Useful for immediate keyboard response.

**Returns:** A single-character string

**Example:**
```rust
Terminal.Say("Continue? (y/n)");
var response = Terminal.InKey();

if response == "y" or response == "Y" {
    Terminal.Say("Continuing...");
} else {
    Terminal.Say("Cancelled.");
}
```

**When to use:** Use `InKey` for yes/no prompts, menu selections, or any time you want instant response to a single keystroke.

**Edge case:** Returns immediately without displaying the typed character. You may want to echo it back if the user should see what they typed.

---

#### GetKey

```rust
func GetKey() -> string
```

Reads a single keypress and returns it as a string. Can detect special keys like arrows and function keys, which are returned as named strings (e.g., `"UP"`, `"DOWN"`, `"LEFT"`, `"RIGHT"`).

**Returns:** A string representing the key pressed

**Example:**
```rust
Terminal.Say("Use arrow keys to move, Q to quit");

loop {
    var key = Terminal.GetKey();

    if key == "UP" {
        Terminal.Say("Moving up");
    } else if key == "DOWN" {
        Terminal.Say("Moving down");
    } else if key == "LEFT" {
        Terminal.Say("Moving left");
    } else if key == "RIGHT" {
        Terminal.Say("Moving right");
    } else if key == "q" or key == "Q" {
        break;
    } else {
        Terminal.Say("Unknown key");
    }
}
```

**When to use:** Use `GetKey` for games, TUI applications, or any program that needs to respond to arrow keys, function keys, or key combinations.

**Related:** `GetKeyTimeout(ms: i64) -> string` — Same as `GetKey` but returns an empty string if no key is pressed within the specified timeout in milliseconds.

---

### Terminal Formatting

#### Clear

```rust
func Clear() -> void
```

Clears the entire terminal screen and moves the cursor to the top-left corner.

**Example:**
```rust
func displayMenu() {
    Terminal.Clear();
    Terminal.Say("=== MAIN MENU ===");
    Terminal.Say("1. New Game");
    Terminal.Say("2. Load Game");
    Terminal.Say("3. Quit");
}
```

**When to use:** Use `Clear` at the start of new screens, menu transitions, or to refresh a full-screen display.

---

#### SetColor

```rust
func SetColor(foreground: i64, background: i64) -> void
```

Sets the foreground and background text color for subsequent output using integer color codes.

**Parameters:**
- `foreground` - Foreground color code (integer)
- `background` - Background color code (integer)

**Example:**
```rust
Terminal.SetColor(1, 0);   // Set foreground color 1, background color 0
Terminal.Say("Colored text");
```

**When to use:** Use colors sparingly to highlight important information like errors, warnings, or success messages.

**Edge case:** Not all terminals support colors. On unsupported terminals, this function may have no effect.

---

#### SetPosition

```rust
func SetPosition(x: i64, y: i64) -> void
```

Positions the cursor at the specified column and row.

**Parameters:**
- `x` - Column number
- `y` - Row number

**Example:**
```rust
// Draw a box around the screen edge
Terminal.Clear();

// Top border
Terminal.SetPosition(1, 1);
Terminal.Print("+------------------------+");

// Side borders
for row in 2..10 {
    Terminal.SetPosition(1, row);
    Terminal.Print("|");
    Terminal.SetPosition(26, row);
    Terminal.Print("|");
}

// Bottom border
Terminal.SetPosition(1, 10);
Terminal.Print("+------------------------+");
```

**When to use:** Use cursor positioning for TUI (text user interface) applications, games, or formatted displays.

---

#### SetCursorVisible

```rust
func SetCursorVisible(visible: i64) -> void
```

Controls cursor visibility. Hiding the cursor (passing 0) creates a cleaner appearance for animations and games. Pass 1 to show the cursor again.

**Parameters:**
- `visible` - 0 to hide the cursor, 1 to show it

**Example:**
```rust
Terminal.SetCursorVisible(0);  // Hide cursor

// Animate a spinner
var frames = ["|", "/", "-", "\\"];
for i in 0..20 {
    Terminal.Print("\r" + frames[i % 4] + " Loading...");
    Time.Clock.Sleep(100);
}

Terminal.SetCursorVisible(1);  // Show cursor again
Terminal.Say("\rDone!          ");
```

**When to use:** Hide the cursor during animations or full-screen displays. Always remember to show it again (pass 1) before the program exits.

---

### Common Terminal Patterns

#### Progress Bar

```rust
func showProgress(current: Integer, total: Integer) {
    var percent = (current * 100) / total;
    var bars = percent / 5;  // 20 characters wide

    Terminal.Print("\r[");
    for i in 0..20 {
        if i < bars {
            Terminal.Print("=");
        } else {
            Terminal.Print(" ");
        }
    }
    Terminal.Print("] " + percent + "%");
}

// Usage
for i in 0..100 {
    showProgress(i, 100);
    Time.Clock.Sleep(50);
}
Terminal.Say("");  // Newline when done
```

#### Simple Menu System

```rust
bind Viper.Terminal;
bind Viper.Convert;

func showMenu(options: [String]) -> Integer {
    loop {
        Terminal.Clear();
        Terminal.Say("Select an option:");
        Terminal.Say("");

        var i = 0;
        for option in options {
            Terminal.Say("  " + (i + 1) + ". " + option);
            i = i + 1;
        }

        Terminal.Say("");
        var choice = Terminal.Ask("Enter choice: ");
        var num = ToInt(choice);

        if num >= 1 and num <= options.length {
            return num - 1;  // Return 0-based index
        }

        Terminal.Say("Invalid choice. Press any key...");
        Terminal.InKey();
    }
}
```

---

## Viper.IO.File

File system operations for reading, writing, and managing files and directories. Bind as `Viper.IO` and use the `IO.File.*` prefix.

```rust
bind Viper.IO;
```

> **See also:** [Chapter 9: Files and Persistence](../part2-building-blocks/09-files.md) for comprehensive coverage of file operations.

---

### Reading Files

#### ReadAllText

```rust
func ReadAllText(path: string) -> string
```

Reads the entire contents of a text file as a string.

**Parameters:**
- `path` - Path to the file (relative or absolute)

**Returns:** The file contents as a string

**Example:**
```rust
bind Viper.IO;
bind Viper.Terminal;

var content = IO.File.ReadAllText("config.txt");
Terminal.Say("File contents: " + content);
```

**When to use:** Use for small to medium text files (configuration files, data files, source code). For large files, consider `ReadAllLines` to process line by line.

**Edge cases:**
- Throws an error if the file does not exist
- Throws an error if the file cannot be read (permissions, locked)
- May produce unexpected results with binary files

---

#### ReadAllBytes

```rust
func ReadAllBytes(path: string) -> [Byte]
```

Reads the entire contents of a file as raw bytes.

**Parameters:**
- `path` - Path to the file

**Returns:** Array of bytes

**Example:**
```rust
bind Viper.IO;
bind Viper.Terminal;

var bytes = IO.File.ReadAllBytes("image.png");
Terminal.Say("File size: " + bytes.length + " bytes");
```

**When to use:** Use for binary files (images, archives, executables) or when you need precise byte-level access.

---

#### ReadAllLines

```rust
func ReadAllLines(path: string) -> [String]
```

Reads a text file and returns an array where each element is one line.

**Parameters:**
- `path` - Path to the file

**Returns:** Array of strings (lines without newline characters)

**Example:**
```rust
bind Viper.IO;
bind Viper.Terminal;

var lines = IO.File.ReadAllLines("names.txt");

for line in lines {
    Terminal.Say(line);
}
```

**When to use:** Use when processing line-based data (log files, CSV data, configuration with one entry per line).

---

### Writing Files

#### WriteAllText

```rust
func WriteAllText(path: string, content: string) -> void
```

Writes a string to a file, creating the file if it does not exist or overwriting it if it does.

**Parameters:**
- `path` - Path to the file
- `content` - Text to write

**Example:**
```rust
bind Viper.IO;

var data = "Name: Alice\nAge: 30\nCity: Boston";
IO.File.WriteAllText("person.txt", data);
```

**When to use:** Use to save text data, create configuration files, or export data.

**Edge cases:**
- Overwrites existing content completely (use `Append` to add to existing)
- Throws error if path is invalid or write permission denied

---

#### WriteAllBytes

```rust
func WriteAllBytes(path: string, data: [Byte]) -> void
```

Writes raw bytes to a file.

**Parameters:**
- `path` - Path to the file
- `data` - Array of bytes to write

**Example:**
```rust
bind Viper.IO;

var data = IO.File.ReadAllBytes("source.bin");
IO.File.WriteAllBytes("copy.bin", data);
```

**When to use:** Use for binary file formats or when you have computed byte data to save.

---

#### Append / AppendLine

```rust
func Append(path: string, content: string) -> void
func AppendLine(path: string, content: string) -> void
```

Adds text to the end of an existing file, or creates the file if it does not exist. `Append` writes the text as-is; `AppendLine` appends a newline after the content.

**Parameters:**
- `path` - Path to the file
- `content` - Text to append

**Example:**
```rust
bind Viper.IO;

// Simple logging
func log(message: String) {
    IO.File.AppendLine("app.log", message);
}

log("Application started");
log("Processing data...");
log("Application finished");
```

**When to use:** Use for log files, accumulating data over time, or any situation where you want to add to rather than replace existing content.

---

### File Operations

#### Exists

```rust
func Exists(path: string) -> Boolean
```

Checks whether a file or directory exists at the given path.

**Returns:** `true` if the path exists, `false` otherwise

**Example:**
```rust
bind Viper.IO;

var configPath = "settings.json";

if IO.File.Exists(configPath) {
    var config = IO.File.ReadAllText(configPath);
    // Use existing config
} else {
    // Create default config
    IO.File.WriteAllText(configPath, "{}");
}
```

**When to use:** Always check existence before reading if the file might not exist. This prevents runtime errors.

---

#### Delete

```rust
func Delete(path: string) -> void
```

Deletes a file.

**Parameters:**
- `path` - Path to the file to delete

**Example:**
```rust
bind Viper.IO;
bind Viper.Terminal;

if IO.File.Exists("temp.txt") {
    IO.File.Delete("temp.txt");
    Terminal.Say("Temporary file cleaned up");
}
```

**Edge cases:**
- Throws error if the file does not exist
- Throws error if the file is locked or permission denied

---

#### Move

```rust
func Move(oldPath: string, newPath: string) -> void
```

Renames or moves a file.

**Parameters:**
- `oldPath` - Current path to the file
- `newPath` - New path for the file

**Example:**
```rust
bind Viper.IO;

// Rename a file
IO.File.Move("draft.txt", "final.txt");

// Move to different directory
IO.File.Move("temp/data.csv", "archive/data.csv");
```

**When to use:** Use for renaming files or moving them within the same filesystem.

**Edge case:** Moving across different drives/filesystems may fail on some operating systems.

---

#### Copy

```rust
func Copy(sourcePath: string, destPath: string) -> void
```

Creates a copy of a file.

**Parameters:**
- `sourcePath` - Path to the file to copy
- `destPath` - Path for the new copy

**Example:**
```rust
bind Viper.IO;

// Create backup before modifying
IO.File.Copy("config.json", "config.json.backup");

// Now safe to modify config.json
```

**When to use:** Use for backups, duplicating files, or when you need to preserve the original.

---

#### Size

```rust
func Size(path: string) -> Integer
```

Returns the size of a file in bytes.

**Returns:** File size as an integer

**Example:**
```rust
bind Viper.IO;
bind Viper.Terminal;

var bytes = IO.File.Size("video.mp4");
var megabytes = bytes / (1024 * 1024);
Terminal.Say("File size: " + megabytes + " MB");
```

---

#### Modified

```rust
func Modified(path: string) -> Integer
```

Returns when the file was last modified as a Unix timestamp (seconds since epoch).

**Returns:** Unix timestamp as an integer

**Example:**
```rust
bind Viper.IO;
bind Viper.Terminal;

var modTime = IO.File.Modified("document.txt");
Terminal.Say("Last modified: " + modTime);
```

---

### Directory Operations

Directory operations use the `IO.Dir` prefix (part of `bind Viper.IO`).

#### IO.Dir.List

```rust
func List(path: string) -> [String]
```

Lists all files and subdirectories in a directory.

**Returns:** Array of entry names (not full paths)

**Example:**
```rust
bind Viper.IO;
bind Viper.Terminal;

var entries = IO.Dir.List(".");

for entry in entries {
    Terminal.Say(entry);
}
```

**Edge case:** Does not include "." or ".." entries.

---

#### IO.Dir.Make

```rust
func Make(path: string) -> void
```

Creates a directory. Creates parent directories as needed.

**Example:**
```rust
bind Viper.IO;

IO.Dir.Make("output/reports/2024");
// Creates output/, output/reports/, and output/reports/2024/
```

---

### Path Operations

Path operations use the `IO.Path` prefix (part of `bind Viper.IO`).

#### IO.Path.Join

```rust
func Join(left: string, right: string) -> string
```

Combines two path components using the correct separator for the operating system.

**Example:**
```rust
bind Viper.IO;

var path = IO.Path.Join("home/alice", "report.txt");
// Returns: "home/alice/report.txt"
```

**When to use:** Use `IO.Path.Join` instead of string concatenation with "/" or "\\". This ensures your code works on all operating systems.

---

#### IO.Path.Name

```rust
func Name(path: string) -> string
```

Returns just the filename portion of a path.

**Example:**
```rust
bind Viper.IO;

var filename = IO.Path.Name("/home/alice/report.txt");
// Returns: "report.txt"
```

---

#### IO.Path.Dir

```rust
func Dir(path: string) -> string
```

Returns the directory portion of a path.

**Example:**
```rust
bind Viper.IO;

var dir = IO.Path.Dir("/home/alice/report.txt");
// Returns: "/home/alice"
```

---

#### IO.Path.Ext

```rust
func Ext(path: string) -> string
```

Returns the file extension including the dot.

**Example:**
```rust
bind Viper.IO;

var ext = IO.Path.Ext("photo.jpg");
// Returns: ".jpg"
```

---

#### IO.Path.Abs

```rust
func Abs(path: string) -> string
```

Converts a relative path to an absolute path.

**Example:**
```rust
bind Viper.IO;

var abs = IO.Path.Abs("../data/input.txt");
// Returns something like: "/home/alice/project/data/input.txt"
```

---

### Common File Patterns

#### Safe File Reading

```rust
bind Viper.IO;

func readFileSafe(path: String, defaultValue: String) -> String {
    if IO.File.Exists(path) {
        return IO.File.ReadAllText(path);
    }
    return defaultValue;
}

var config = readFileSafe("config.txt", "default settings");
```

#### Processing All Files in Directory

```rust
bind Viper.IO;

func processDirectory(dirPath: String, ext: String) {
    for filename in IO.Dir.List(dirPath) {
        var fullPath = IO.Path.Join(dirPath, filename);

        if IO.Path.Ext(filename) == ext {
            processFile(fullPath);
        }
    }
}

processDirectory("data", ".csv");
```

---

## Viper.Math

Mathematical functions and constants for numeric computation.

---

### Constants

```rust
Math.PI      // 3.14159265358979323846 - ratio of circle circumference to diameter
Math.E       // 2.71828182845904523536 - base of natural logarithm
Math.TAU     // 6.28318530717958647692 - 2*PI, full circle in radians
```

**Example:**
```rust
var radius = 5.0;
var circumference = 2.0 * Math.PI * radius;  // or TAU * radius
var area = Math.PI * radius * radius;
```

---

### Basic Functions

#### Abs / AbsInt

```rust
func Abs(x: Number) -> Number
func AbsInt(x: Integer) -> Integer
```

Returns the absolute (non-negative) value. Use `Abs` for floating-point numbers, `AbsInt` for integers.

**Example:**
```rust
bind Viper.Math;

Math.Abs(-3.14)     // Returns: 3.14
Math.AbsInt(-5)     // Returns: 5
```

---

#### Min / Max / MinInt / MaxInt

```rust
func Min(a: Number, b: Number) -> Number
func Max(a: Number, b: Number) -> Number
func MinInt(a: Integer, b: Integer) -> Integer
func MaxInt(a: Integer, b: Integer) -> Integer
```

Returns the smaller or larger of two values.

**Example:**
```rust
bind Viper.Math;

Math.Min(3.0, 7.0)    // Returns: 3.0
Math.Max(3.0, 7.0)    // Returns: 7.0
Math.MinInt(3, 7)     // Returns: 3

// Keep value in bounds
var health = Math.MaxInt(0, health - damage);  // Never below 0
health = Math.MinInt(100, health + healing);   // Never above 100
```

---

#### Clamp / ClampInt

```rust
func Clamp(value: Number, min: Number, max: Number) -> Number
func ClampInt(value: Integer, min: Integer, max: Integer) -> Integer
```

Constrains a value to a range. Equivalent to `Max(min, Min(max, value))`.

**Example:**
```rust
bind Viper.Math;

Math.Clamp(150.0, 0.0, 100.0)   // Returns: 100.0
Math.Clamp(-50.0, 0.0, 100.0)   // Returns: 0.0
Math.Clamp(50.0, 0.0, 100.0)    // Returns: 50.0

// Common use: keep game entities on screen
playerX = Math.ClampInt(playerX, 0, screenWidth);
playerY = Math.ClampInt(playerY, 0, screenHeight);
```

---

### Rounding Functions

#### Floor

```rust
func Floor(x: Number) -> Number
```

Rounds down to the nearest integer (toward negative infinity).

**Example:**
```rust
bind Viper.Math;

Math.Floor(3.7)   // Returns: 3.0
Math.Floor(3.2)   // Returns: 3.0
Math.Floor(-3.2)  // Returns: -4.0 (toward negative infinity)
```

---

#### Ceil

```rust
func Ceil(x: Number) -> Number
```

Rounds up to the nearest integer (toward positive infinity).

**Example:**
```rust
bind Viper.Math;

Math.Ceil(3.2)    // Returns: 4.0
Math.Ceil(3.0)    // Returns: 3.0
Math.Ceil(-3.7)   // Returns: -3.0 (toward positive infinity)
```

---

#### Round

```rust
func Round(x: Number) -> Number
```

Rounds to the nearest integer. Halfway cases round away from zero.

**Example:**
```rust
bind Viper.Math;

Math.Round(3.4)   // Returns: 3.0
Math.Round(3.5)   // Returns: 4.0
Math.Round(-3.5)  // Returns: -4.0
```

---

#### Trunc

```rust
func Trunc(x: Number) -> Number
```

Truncates toward zero (removes the fractional part).

**Example:**
```rust
bind Viper.Math;

Math.Trunc(3.7)   // Returns: 3.0
Math.Trunc(-3.7)  // Returns: -3.0 (toward zero, not down)
```

---

### Powers and Roots

#### Sqrt

```rust
func Sqrt(x: Number) -> Number
```

Returns the square root.

**Example:**
```rust
bind Viper.Math;

Math.Sqrt(16.0)   // Returns: 4.0
Math.Sqrt(2.0)    // Returns: 1.41421356...

// Distance between two points
func distance(x1: Number, y1: Number, x2: Number, y2: Number) -> Number {
    var dx = x2 - x1;
    var dy = y2 - y1;
    return Math.Sqrt(dx*dx + dy*dy);
}
```

**Edge case:** Returns `NaN` (Not a Number) for negative inputs.

---

#### Pow

```rust
func Pow(base: Number, exponent: Number) -> Number
```

Returns base raised to the power of exponent.

**Example:**
```rust
bind Viper.Math;

Math.Pow(2.0, 10.0)   // Returns: 1024.0
Math.Pow(10.0, 3.0)   // Returns: 1000.0
Math.Pow(4.0, 0.5)    // Returns: 2.0 (same as Sqrt)
```

---

#### Exp

```rust
func Exp(x: Number) -> Number
```

Returns e raised to the power x.

**Example:**
```rust
bind Viper.Math;

Math.Exp(1.0)     // Returns: 2.71828... (e)
Math.Exp(0.0)     // Returns: 1.0
```

---

#### Log / Log10 / Log2

```rust
func Log(x: Number) -> Number      // Natural logarithm (base e)
func Log10(x: Number) -> Number    // Base-10 logarithm
func Log2(x: Number) -> Number     // Base-2 logarithm
```

**Example:**
```rust
bind Viper.Math;

Math.Log(Math.E)      // Returns: 1.0
Math.Log10(1000.0)    // Returns: 3.0
Math.Log2(1024.0)     // Returns: 10.0
```

**Edge case:** Returns negative infinity for 0, `NaN` for negative inputs.

---

### Trigonometry

All trigonometric functions use radians (not degrees).

#### Sin / Cos / Tan

```rust
func Sin(x: Number) -> Number    // Sine
func Cos(x: Number) -> Number    // Cosine
func Tan(x: Number) -> Number    // Tangent
```

**Example:**
```rust
bind Viper.Math;

Math.Sin(0.0)              // Returns: 0.0
Math.Sin(Math.PI / 2.0)   // Returns: 1.0
Math.Cos(0.0)              // Returns: 1.0
Math.Cos(Math.PI)          // Returns: -1.0
```

---

#### Asin / Acos / Atan / Atan2

```rust
func Asin(x: Number) -> Number               // Arc sine (inverse)
func Acos(x: Number) -> Number               // Arc cosine (inverse)
func Atan(x: Number) -> Number               // Arc tangent (inverse)
func Atan2(y: Number, x: Number) -> Number   // Arc tangent of y/x (handles quadrants)
```

**Example:**
```rust
bind Viper.Math;

// Atan2 is usually what you want for angles between points
func angleTo(fromX: Number, fromY: Number, toX: Number, toY: Number) -> Number {
    return Math.Atan2(toY - fromY, toX - fromX);
}

var angle = angleTo(0.0, 0.0, 1.0, 1.0);  // 45 degrees = PI/4 radians
```

**Note:** `Atan2(y, x)` correctly handles all four quadrants, unlike `Atan(y/x)`.

---

#### Hyperbolic Functions

```rust
func Sinh(x: Number) -> Number    // Hyperbolic sine
func Cosh(x: Number) -> Number    // Hyperbolic cosine
func Tanh(x: Number) -> Number    // Hyperbolic tangent
```

---

#### Angle Conversion

```rust
func toRadians(degrees: f64) -> f64
func toDegrees(radians: f64) -> f64
```

**Example:**
```rust
var rad = Math.toRadians(90.0);   // Returns: PI/2
var deg = Math.toDegrees(Math.PI);  // Returns: 180.0

// If you have user input in degrees
var userAngle = 45.0;  // degrees
var x = Math.cos(Math.toRadians(userAngle));
```

---

### Random Numbers

Random functions are under `Viper.Math.Random`. Bind as `bind Viper.Math;` and use the `Math.Random.*` prefix.

#### Math.Random.Next

```rust
func Next() -> Number
```

Returns a random floating-point number from 0.0 (inclusive) to 1.0 (exclusive).

**Example:**
```rust
bind Viper.Math;

var r = Math.Random.Next();  // e.g., 0.7231498...
```

---

#### Math.Random.Range

```rust
func Range(min: Integer, max: Integer) -> Integer
```

Returns a random integer in the range [min, max] (inclusive on both ends).

**Example:**
```rust
bind Viper.Math;

var diceRoll = Math.Random.Range(1, 6);
var coinFlip = Math.Random.Range(0, 1);  // 0 or 1
```

---

#### Math.Random.Seed

```rust
func Seed(seed: Integer) -> void
```

Sets the random number generator seed. Same seed produces the same sequence.

**Example:**
```rust
bind Viper.Math;

// Reproducible random sequence
Math.Random.Seed(12345);
var a = Math.Random.Range(1, 100);  // Always same value
Math.Random.Seed(12345);
var b = Math.Random.Range(1, 100);  // b == a
```

**When to use:** Use for testing, reproducible simulations, or games with "daily challenges."

---

## Viper.Time

Time and date operations for timing, delays, and date manipulation.

---

### Current Time

#### Time.DateTime.Now / Time.DateTime.NowMs

```rust
func Now() -> Integer       // Unix timestamp in seconds
func NowMs() -> Integer     // Unix timestamp in milliseconds
```

Returns the current time as a Unix timestamp.

**Example:**
```rust
bind Viper.Time;
bind Viper.Terminal;

// Measure execution time
var start = Time.DateTime.NowMs();

// ... do some work ...

var elapsed = Time.DateTime.NowMs() - start;
Terminal.Say("Operation took " + elapsed + " ms");
```

---

### Delays

#### Time.SleepMs

```rust
func SleepMs(milliseconds: Integer) -> void
```

Pauses execution for the specified duration in milliseconds.

**Example:**
```rust
bind Viper.Time;
bind Viper.Terminal;

Terminal.Say("Starting in 3...");
Time.SleepMs(1000);
Terminal.Say("2...");
Time.SleepMs(1000);
Terminal.Say("1...");
Time.SleepMs(1000);
Terminal.Say("Go!");
```

---

### Time.DateTime Functions

`DateTime` values are Unix timestamps stored as integers. All functions are under `Time.DateTime.*`.

#### Time.DateTime.Now / Time.DateTime.NowMs

```rust
func Now() -> Integer       // Seconds since Unix epoch
func NowMs() -> Integer     // Milliseconds since Unix epoch
```

**Example:**
```rust
bind Viper.Time;

var ts = Time.DateTime.Now();
```

---

#### Time.DateTime.Year / Month / Day / Hour / Minute / Second

```rust
func Year(ts: Integer) -> Integer
func Month(ts: Integer) -> Integer
func Day(ts: Integer) -> Integer
func Hour(ts: Integer) -> Integer
func Minute(ts: Integer) -> Integer
func Second(ts: Integer) -> Integer
func DayOfWeek(ts: Integer) -> Integer    // 0=Sunday ... 6=Saturday
```

**Example:**
```rust
bind Viper.Time;
bind Viper.Terminal;

var ts = Time.DateTime.Now();
Terminal.Say("Year: " + Time.DateTime.Year(ts));
Terminal.Say("Month: " + Time.DateTime.Month(ts));
Terminal.Say("Day: " + Time.DateTime.Day(ts));
```

---

#### Time.DateTime.Format

```rust
func Format(ts: Integer, pattern: string) -> string
```

Formats a timestamp as a human-readable string using a pattern.

**Example:**
```rust
bind Viper.Time;

var ts = Time.DateTime.Now();
var formatted = Time.DateTime.Format(ts, "YYYY-MM-DD HH:mm:ss");
```

---

#### Time.DateTime.ParseISO

```rust
func ParseISO(str: string) -> Integer
```

Parses an ISO 8601 date/time string into a Unix timestamp.

**Example:**
```rust
bind Viper.Time;

var ts = Time.DateTime.ParseISO("2024-03-20T14:30:00");
```

---

#### Time.DateTime.AddDays / AddSeconds

```rust
func AddDays(ts: Integer, n: Integer) -> Integer
func AddSeconds(ts: Integer, n: Integer) -> Integer
```

Returns a new timestamp with the specified amount added.

**Example:**
```rust
bind Viper.Time;

var now = Time.DateTime.Now();
var tomorrow = Time.DateTime.AddDays(now, 1);
var lastWeek = Time.DateTime.AddDays(now, -7);
```

---

#### Time.DateTime.Diff

```rust
func Diff(ts1: Integer, ts2: Integer) -> Integer
```

Returns the difference between two timestamps in seconds.

**Example:**
```rust
bind Viper.Time;

var start = Time.DateTime.Now();
// ... do work ...
var elapsed = Time.DateTime.Diff(Time.DateTime.Now(), start);
```

---

## Viper.Collections

Data structures beyond basic arrays for organizing and managing data. Bind as `bind Viper.Collections;` and use the `Collections.*` prefix.

> **Note on API style:** The Collections runtime API is function-based, not object-oriented. Rather than `map.get(key)`, use `Collections.Map.Get(map, key)`. The function signatures shown below describe the conceptual interface; prefix all calls with `Collections.Map.*`, `Collections.List.*`, `Collections.Set.*`, etc.

> **See also:** [Chapter 6: Collections](../part1-foundations/06-collections.md) for array fundamentals.

---

### Map

A key-value store with fast lookup. Also known as a dictionary or hash map.

```rust
var map = Map<string, i64>.new();
```

#### Methods

```rust
map.set(key, value)     // Add or update a key-value pair
map.get(key)            // -> value? (nil if not found)
map.has(key)            // -> bool
map.delete(key)         // Remove a key
map.clear()             // Remove all entries
map.size                // -> i64 (number of entries)
map.keys()              // -> [KeyType]
map.values()            // -> [ValueType]
```

**Example:**
```rust
// Word frequency counter
func countWords(text: String) -> Map<String, Integer> {
    var counts = Map<String, Integer>.new();

    for word in text.split(" ") {
        var lower = word.toLower();
        if counts.has(lower) {
            counts.set(lower, counts.get(lower) + 1);
        } else {
            counts.set(lower, 1);
        }
    }

    return counts;
}

var text = "the quick brown fox jumps over the lazy dog";
var freq = countWords(text);

for word, count in freq {
    Terminal.Say(word + ": " + count);
}
```

**When to use:** Use Map when you need to associate values with keys and look them up quickly. Perfect for caches, configurations, counters, and indexing.

---

### Set

An unordered collection of unique values.

```rust
var set = Set<string>.new();
```

#### Methods

```rust
set.add(item)           // Add an item
set.contains(item)      // -> bool
set.remove(item)        // Remove an item
set.clear()             // Remove all items
set.size                // -> i64

// Set operations
set.union(other)        // -> Set (items in either)
set.intersection(other) // -> Set (items in both)
set.difference(other)   // -> Set (items in this but not other)
```

**Example:**
```rust
// Find unique visitors
var visitors = Set<string>.new();

visitors.add("alice");
visitors.add("bob");
visitors.add("alice");  // Duplicate ignored

Terminal.Say("Unique visitors: " + visitors.size);  // 2

// Set operations
var admins = Set<string>.new();
admins.add("alice");
admins.add("charlie");

var adminVisitors = visitors.intersection(admins);  // {"alice"}
var nonAdminVisitors = visitors.difference(admins); // {"bob"}
```

**When to use:** Use Set when you only care about unique membership, not values or counts.

---

### Queue

First-in, first-out (FIFO) collection.

```rust
var queue = Queue<string>.new();
```

#### Methods

```rust
queue.enqueue(item)     // Add to back
queue.dequeue()         // -> item? (remove from front, nil if empty)
queue.peek()            // -> item? (see front without removing)
queue.isEmpty()         // -> bool
queue.size              // -> i64
```

**Example:**
```rust
// Task queue
var tasks = Queue<string>.new();

tasks.enqueue("Send email");
tasks.enqueue("Update database");
tasks.enqueue("Generate report");

while !tasks.isEmpty() {
    var task = tasks.dequeue();
    Terminal.Say("Processing: " + task);
    // Process task...
}
```

**When to use:** Use Queue for task queues, BFS algorithms, or any situation where items should be processed in arrival order.

---

### Stack

Last-in, first-out (LIFO) collection.

```rust
var stack = Stack<string>.new();
```

#### Methods

```rust
stack.push(item)        // Add to top
stack.pop()             // -> item? (remove from top, nil if empty)
stack.peek()            // -> item? (see top without removing)
stack.isEmpty()         // -> bool
stack.size              // -> i64
```

**Example:**
```rust
// Undo system
var undoStack = Stack<string>.new();

func doAction(action: String) {
    undoStack.push(action);
    Terminal.Say("Did: " + action);
}

func undo() {
    if !undoStack.isEmpty() {
        var action = undoStack.pop();
        Terminal.Say("Undoing: " + action);
    } else {
        Terminal.Say("Nothing to undo");
    }
}

doAction("Type 'Hello'");
doAction("Type ' World'");
undo();  // Undoing: Type ' World'
```

**When to use:** Use Stack for undo/redo, DFS algorithms, parsing expressions, or tracking nested operations.

---

### PriorityQueue

A queue where items are dequeued by priority, not arrival order.

```rust
var pq = PriorityQueue<Task>.new(compareFn);
```

#### Methods

```rust
pq.enqueue(item)        // Add item (sorted by priority)
pq.dequeue()            // -> item? (highest priority)
pq.peek()               // -> item? (see highest priority)
pq.isEmpty()            // -> bool
pq.size                 // -> i64
```

**Example:**
```rust
struct Task {
    name: String,
    priority: Integer  // Lower number = higher priority
}

func compareTasks(a: Task, b: Task) -> Integer {
    return a.priority - b.priority;
}

var taskQueue = PriorityQueue<Task>.new(compareTasks);

taskQueue.enqueue(Task { name: "Low priority task", priority: 10 });
taskQueue.enqueue(Task { name: "Critical bug fix", priority: 1 });
taskQueue.enqueue(Task { name: "Medium task", priority: 5 });

while !taskQueue.isEmpty() {
    var task = taskQueue.dequeue();
    Terminal.Say("Processing: " + task.name);
}
// Output:
// Processing: Critical bug fix
// Processing: Medium task
// Processing: Low priority task
```

**When to use:** Use PriorityQueue for task scheduling, Dijkstra's algorithm, event simulation, or any situation where priority matters.

---

## Viper.Network

Networking operations for HTTP requests, TCP connections, and UDP communication.

> **See also:** [Chapter 22: Networking](../part4-applications/22-networking.md) for comprehensive networking coverage.

---

### HTTP

Simple HTTP client for web requests. Bind as `bind Viper.Network;` and use `Network.Http.*`.

#### Network.Http.Get / Post

```rust
func Get(url: string) -> string    // Returns response body as text
func Post(url: string, body: string) -> string
func GetBytes(url: string) -> [Byte]
func PostBytes(url: string, body: [Byte]) -> [Byte]
func Download(url: string, destPath: string) -> Boolean
```

**Example:**
```rust
bind Viper.Network;
bind Viper.Terminal;

// Simple GET request
var body = Network.Http.Get("https://api.example.com/data");
Terminal.Say(body);

// POST with a body
var result = Network.Http.Post("https://api.example.com/users", "name=Alice");
Terminal.Say(result);
```

**Edge cases:**
- Returns the response body as a string
- Network errors may throw or return an empty string

---

### TCP

Stream-based network connections.

#### Client

```rust
var socket = TcpSocket.connect(host: string, port: i64) -> TcpSocket

socket.write(data: string) -> void
socket.read(bufferSize: i64) -> string
socket.readLine() -> string
socket.close() -> void
```

**Example:**
```rust
// Simple HTTP client (low-level)
var socket = TcpSocket.connect("example.com", 80);

socket.write("GET / HTTP/1.1\r\n");
socket.write("Host: example.com\r\n");
socket.write("\r\n");

var response = socket.read(4096);
Terminal.Say(response);

socket.close();
```

#### Server

```rust
var server = TcpServer.listen(port: i64) -> TcpServer

server.accept() -> TcpSocket     // Blocks until client connects
server.close() -> void
```

**Example:**
```rust
// Echo server
var server = TcpServer.listen(8080);
Terminal.Say("Server listening on port 8080");

while true {
    var client = server.accept();
    Terminal.Say("Client connected");

    var data = client.readLine();
    client.write("Echo: " + data + "\n");
    client.close();
}
```

---

### UDP

Connectionless datagram communication.

```rust
var socket = UdpSocket.create() -> UdpSocket

socket.send(data: string, address: string, port: i64) -> void
socket.receive() -> { data: string, address: string, port: i64 }
socket.bind(port: i64) -> void     // For receiving
socket.close() -> void
```

**Example:**
```rust
// UDP sender
var sender = UdpSocket.create();
sender.send("Hello!", "127.0.0.1", 9000);
sender.close();

// UDP receiver
var receiver = UdpSocket.create();
receiver.bind(9000);

var packet = receiver.receive();
Terminal.Say("Received: " + packet.data + " from " + packet.address);
receiver.close();
```

---

## Viper.JSON

JSON parsing and generation for data interchange.

> **See also:** [Chapter 23: Data Formats](../part4-applications/23-data-formats.md) for working with JSON and other formats.

---

### Parsing

```rust
func JSON.parse(jsonString: string) -> JsonValue
```

Parses a JSON string into a `JsonValue` object.

**JsonValue methods:**
```rust
value.asString() -> string
value.asInt() -> i64
value.asFloat() -> f64
value.asBool() -> bool
value.asArray() -> [JsonValue]
value.asObject() -> Map<string, JsonValue>
value[key] -> JsonValue           // Object property access
value[index] -> JsonValue         // Array index access
```

**Example:**
```rust
var json = `{
    "name": "Alice",
    "age": 30,
    "hobbies": ["reading", "gaming"],
    "address": {
        "city": "Boston",
        "zip": "02101"
    }
}`;

var data = JSON.parse(json);

var name = data["name"].asString();           // "Alice"
var age = data["age"].asInt();                // 30
var firstHobby = data["hobbies"][0].asString();  // "reading"
var city = data["address"]["city"].asString();   // "Boston"
```

**Edge cases:**
- Throws error on invalid JSON
- Accessing missing keys returns `nil`
- Type mismatches (e.g., `asInt()` on a string) throw errors

---

### Creating JSON

```rust
func JSON.object() -> JsonObject
func JSON.array() -> JsonArray
```

**JsonObject methods:**
```rust
obj.set(key: string, value: any) -> void
obj.toString() -> string          // Compact JSON
obj.toPrettyString() -> string    // Formatted with indentation
```

**JsonArray methods:**
```rust
arr.add(value: any) -> void
arr.toString() -> string
arr.toPrettyString() -> string
```

**Example:**
```rust
// Building JSON programmatically
var user = JSON.object();
user.set("name", "Bob");
user.set("age", 25);

var hobbies = JSON.array();
hobbies.add("sports");
hobbies.add("music");
user.set("hobbies", hobbies);

var json = user.toPrettyString();
// {
//   "name": "Bob",
//   "age": 25,
//   "hobbies": ["sports", "music"]
// }
```

---

### Common JSON Patterns

#### Safe Property Access

```rust
func getStringOr(json: JsonValue, key: String, default: String) -> String {
    var value = json[key];
    if value != nil {
        return value.asString();
    }
    return default;
}

var name = getStringOr(data, "nickname", "Anonymous");
```

#### Config File Loading

```rust
func loadConfig(path: String) -> JsonValue {
    if Viper.File.exists(path) {
        var content = Viper.File.readText(path);
        return JSON.parse(content);
    }

    // Return default config
    var defaults = JSON.object();
    defaults.set("theme", "light");
    defaults.set("volume", 80);
    return defaults;
}
```

---

## Viper.Threading

Concurrency primitives for parallel execution.

> **See also:** [Chapter 24: Concurrency](../part4-applications/24-concurrency.md) for comprehensive multithreading coverage.

---

### Threads

#### spawn

```rust
func Thread.spawn(fn: func() -> T) -> Thread<T>
```

Creates and starts a new thread running the given function.

**Thread methods:**
```rust
thread.join() -> void      // Wait for thread to complete
thread.result() -> T       // Wait and get return value
```

**Example:**
```rust
// Simple thread
var thread = Thread.spawn(func() {
    Terminal.Say("Hello from thread!");
});
thread.join();

// Thread with return value
var thread = Thread.spawn(func() -> i64 {
    var sum = 0;
    for i in 0..1000000 {
        sum += i;
    }
    return sum;
});

// Do other work while thread runs...

var result = thread.result();
Terminal.Say("Sum: " + result);
```

---

### Mutex

Mutual exclusion lock for protecting shared data.

```rust
var mutex = Mutex.create() -> Mutex

mutex.lock() -> void
mutex.unlock() -> void
mutex.synchronized(fn: func()) -> void
```

**Example:**
```rust
var counter = 0;
var mutex = Mutex.create();

// WRONG: Race condition
var threads = [];
for i in 0..10 {
    threads.push(Thread.spawn(func() {
        for j in 0..1000 {
            counter += 1;  // Not thread-safe!
        }
    }));
}

// CORRECT: With mutex
var threads = [];
for i in 0..10 {
    threads.push(Thread.spawn(func() {
        for j in 0..1000 {
            mutex.lock();
            counter += 1;
            mutex.unlock();
        }
    }));
}

// BETTER: Using synchronized
mutex.synchronized(func() {
    counter += 1;
    // Automatically unlocked when function returns
});
```

**Warning:** Always pair `lock()` with `unlock()`. Use `synchronized` when possible to avoid forgetting.

---

### Atomics

Lock-free atomic operations for simple counters and flags.

```rust
var atomic = Atomic<i64>.create(initialValue: i64) -> Atomic<i64>

atomic.get() -> i64
atomic.set(value: i64) -> void
atomic.increment() -> i64          // Returns old value
atomic.decrement() -> i64          // Returns old value
atomic.add(n: i64) -> i64          // Returns old value
atomic.compareAndSwap(expected: i64, new: i64) -> bool
```

**Example:**
```rust
var counter = Atomic<i64>.create(0);

// Safe to call from multiple threads
var threads = [];
for i in 0..10 {
    threads.push(Thread.spawn(func() {
        for j in 0..1000 {
            counter.increment();
        }
    }));
}

for thread in threads {
    thread.join();
}

Terminal.Say("Count: " + counter.get());  // Always 10000
```

**When to use:** Use Atomics for simple counters or flags. Use Mutex for more complex shared state.

---

### Channels

Type-safe communication between threads.

```rust
var channel = Channel<T>.create() -> Channel<T>

channel.send(value: T) -> void     // Blocks if buffer full
channel.receive() -> T             // Blocks until value available
channel.tryReceive() -> T?         // Returns nil if empty
channel.close() -> void
```

**Example:**
```rust
var channel = Channel<string>.create();

// Producer thread
var producer = Thread.spawn(func() {
    for i in 0..5 {
        channel.send("Message " + i);
        Time.Clock.Sleep(100);
    }
    channel.close();
});

// Consumer (main thread)
loop {
    var msg = channel.tryReceive();
    if msg == nil {
        break;
    }
    Terminal.Say("Received: " + msg);
}

producer.join();
```

**When to use:** Use Channels for producer-consumer patterns, work distribution, or coordinating between threads.

---

### Thread Pool

Manages a pool of worker threads for efficient task execution.

```rust
var pool = ThreadPool.create(numWorkers: i64) -> ThreadPool

pool.submit(fn: func()) -> void
pool.submitWithResult(fn: func() -> T) -> Future<T>
pool.waitAll() -> void
pool.shutdown() -> void
```

**Future methods:**
```rust
future.get() -> T                  // Blocks until result ready
future.isReady() -> bool
```

**Example:**
```rust
var pool = ThreadPool.create(4);  // 4 worker threads

// Submit many tasks
var futures = [];
for url in imageUrls {
    futures.push(pool.submitWithResult(func() {
        return Http.get(url);
    }));
}

// Collect results
for future in futures {
    var response = future.get();
    processImage(response.body);
}

pool.shutdown();
```

---

## Viper.Graphics

Drawing and window management for visual applications.

> **See also:** [Chapter 19: Graphics and Games](../part4-applications/19-graphics.md) and [Chapter 21: Game Project](../part4-applications/21-game-project.md).

---

### Canvas

The drawing surface and window.

```rust
var canvas = Canvas(width: i64, height: i64) -> Canvas

canvas.setTitle(title: string) -> void
canvas.isOpen() -> bool
canvas.show() -> void              // Display buffer contents
canvas.waitForClose() -> void      // Block until window closed
canvas.clear() -> void             // Clear with current color
```

**Example:**
```rust
var canvas = Canvas(800, 600);
canvas.setTitle("My Application");

while canvas.isOpen() {
    canvas.clear();

    // Draw frame...

    canvas.show();
    Time.Clock.Sleep(16);  // ~60 FPS
}
```

---

### Colors

```rust
canvas.setColor(color: Color) -> void

// Color constructors
Color(r: i64, g: i64, b: i64)          // RGB (0-255 each)
Color(r: i64, g: i64, b: i64, a: i64)  // RGBA with alpha

// Predefined colors
Color.RED, Color.GREEN, Color.BLUE
Color.WHITE, Color.BLACK
Color.YELLOW, Color.CYAN, Color.MAGENTA
```

---

### Drawing Shapes

```rust
// Rectangles
canvas.fillRect(x: f64, y: f64, width: f64, height: f64) -> void
canvas.drawRect(x: f64, y: f64, width: f64, height: f64) -> void

// Circles
canvas.fillCircle(centerX: f64, centerY: f64, radius: f64) -> void
canvas.drawCircle(centerX: f64, centerY: f64, radius: f64) -> void

// Ellipses
canvas.fillEllipse(x: f64, y: f64, width: f64, height: f64) -> void

// Lines and polygons
canvas.drawLine(x1: f64, y1: f64, x2: f64, y2: f64) -> void
canvas.drawPolygon(points: [(f64, f64)]) -> void
canvas.fillPolygon(points: [(f64, f64)]) -> void

// Pixels
canvas.setPixel(x: i64, y: i64) -> void
```

**Example:**
```rust
// Draw a simple scene
canvas.setColor(Color(135, 206, 235));  // Sky blue
canvas.fillRect(0, 0, 800, 400);

canvas.setColor(Color(34, 139, 34));    // Forest green
canvas.fillRect(0, 400, 800, 200);

canvas.setColor(Color.YELLOW);
canvas.fillCircle(700, 80, 50);         // Sun

canvas.setColor(Color(139, 69, 19));    // Brown
canvas.fillRect(350, 350, 100, 150);    // House body

canvas.setColor(Color.RED);
canvas.fillPolygon([
    (300, 350), (400, 250), (500, 350)  // Roof
]);
```

---

### Text

```rust
canvas.setFont(name: string, size: i64) -> void
canvas.drawText(x: f64, y: f64, text: string) -> void
```

**Example:**
```rust
canvas.setFont("Arial", 24);
canvas.setColor(Color.WHITE);
canvas.drawText(10, 30, "Score: " + score);
```

---

### Images

```rust
var image = Image.load(path: string) -> Image

canvas.drawImage(image: Image, x: f64, y: f64) -> void
canvas.drawImageScaled(image: Image, x: f64, y: f64, width: f64, height: f64) -> void
```

**Example:**
```rust
var playerSprite = Image.load("assets/player.png");

// Draw at position
canvas.drawImage(playerSprite, playerX, playerY);

// Draw scaled
canvas.drawImageScaled(playerSprite, 100, 100, 64, 64);
```

---

## Viper.GUI

Widget-based user interface library for building desktop applications with buttons, text inputs, checkboxes, and other standard UI components.

> **See also:** [Chapter 19: Graphics and Games](../part4-applications/19-graphics.md) for low-level graphics.

---

### App

The GUI application framework that manages a window, widget tree, and event loop.

```rust
var app = new App(title: string, width: i64, height: i64) -> App

app.ShouldClose -> bool         // Read-only: true when window should close
app.Root -> Widget              // Read-only: root container for adding widgets
app.Poll() -> void              // Process events and update widget states
app.Render() -> void            // Clear, draw all widgets, flip buffer
app.SetFont(font: Font, size: f64) -> void  // Set default font
app.Destroy() -> void           // Clean up resources
```

**Example:**
```rust
bind Viper.GUI.*

var app = new App("My Application", 800, 600)
Theme.SetDark()

var button = new Button(app.Root, "Click Me")

while !app.ShouldClose {
    app.Poll()
    if button.WasClicked() {
        // Handle click
    }
    app.Render()
}

app.Destroy()
```

---

### Font

Font loading for text rendering in widgets.

```rust
var font = Font.Load(path: string) -> Font

font.Destroy() -> void
```

**Example:**
```rust
var font = Font.Load("assets/arial.ttf")
app.SetFont(font, 14.0)
```

---

### Widget (Base Class)

Common functionality for all widgets.

```rust
widget.Destroy() -> void
widget.SetVisible(visible: bool) -> void
widget.SetEnabled(enabled: bool) -> void
widget.SetSize(width: i64, height: i64) -> void
widget.SetPosition(x: i64, y: i64) -> void
widget.AddChild(child: Widget) -> void

// State queries (polling-based events)
widget.IsHovered() -> bool
widget.IsPressed() -> bool
widget.IsFocused() -> bool
widget.WasClicked() -> bool    // True if clicked this frame
```

---

### Label

Text display widget.

```rust
var label = new Label(parent: Widget, text: string) -> Label

label.SetText(text: string) -> void
label.SetFont(font: Font, size: f64) -> void
label.SetColor(color: i64) -> void  // ARGB format: 0xAARRGGBB
```

**Example:**
```rust
var label = new Label(app.Root, "Hello, World!")
label.SetColor(0xFFFFFFFF)  // White text
```

---

### Button

Clickable button widget.

```rust
var button = new Button(parent: Widget, text: string) -> Button

button.SetText(text: string) -> void
button.SetFont(font: Font, size: f64) -> void
button.SetStyle(style: i64) -> void  // 0=default, 1=primary, 2=secondary, 3=danger, 4=text
button.WasClicked() -> bool
button.IsHovered() -> bool
button.IsPressed() -> bool
```

**Example:**
```rust
var btn = new Button(app.Root, "Submit")
btn.SetSize(120, 32)
btn.SetStyle(1)  // Primary style

if btn.WasClicked() {
    // Handle submission
}
```

---

### TextInput

Single-line text input field.

```rust
var input = new TextInput(parent: Widget) -> TextInput

input.Text -> string              // Read-only: current text content
input.SetText(text: string) -> void
input.SetPlaceholder(text: string) -> void
input.SetFont(font: Font, size: f64) -> void
input.IsFocused() -> bool
```

**Example:**
```rust
var nameInput = new TextInput(app.Root)
nameInput.SetPlaceholder("Enter your name...")
nameInput.SetSize(200, 28)

// Later, read the value
var name = nameInput.Text
```

---

### Checkbox

Toggleable checkbox with label.

```rust
var checkbox = new Checkbox(parent: Widget, text: string) -> Checkbox

checkbox.SetChecked(checked: bool) -> void
checkbox.IsChecked() -> bool
checkbox.SetText(text: string) -> void
```

**Example:**
```rust
var rememberMe = new Checkbox(app.Root, "Remember me")

if rememberMe.IsChecked() {
    // Save login
}
```

---

### Dropdown

Dropdown selection widget.

```rust
var dropdown = new Dropdown(parent: Widget) -> Dropdown

dropdown.Selected -> i64          // Read-only: selected index (-1 if none)
dropdown.SelectedText -> string   // Read-only: selected item text
dropdown.AddItem(text: string) -> i64  // Returns index
dropdown.RemoveItem(index: i64) -> void
dropdown.Clear() -> void
dropdown.SetSelected(index: i64) -> void
dropdown.SetPlaceholder(text: string) -> void
```

**Example:**
```rust
var colorPicker = new Dropdown(app.Root)
colorPicker.SetPlaceholder("Select a color...")
colorPicker.AddItem("Red")
colorPicker.AddItem("Green")
colorPicker.AddItem("Blue")

var selected = colorPicker.SelectedText  // "Red", "Green", "Blue", or ""
```

---

### Slider

Horizontal or vertical slider for numeric values.

```rust
var slider = new Slider(parent: Widget, horizontal: bool) -> Slider

slider.Value -> f64               // Read-only: current value
slider.SetValue(value: f64) -> void
slider.SetRange(min: f64, max: f64) -> void
slider.SetStep(step: f64) -> void  // 0 for continuous
```

**Example:**
```rust
var volume = new Slider(app.Root, true)  // Horizontal
volume.SetRange(0.0, 100.0)
volume.SetValue(50.0)
volume.SetSize(200, 24)

var currentVolume = volume.Value
```

---

### ProgressBar

Progress indicator widget.

```rust
var progress = new ProgressBar(parent: Widget) -> ProgressBar

progress.Value -> f64             // Read-only: current value (0.0 to 1.0)
progress.SetValue(value: f64) -> void
```

**Example:**
```rust
var loadingBar = new ProgressBar(app.Root)
loadingBar.SetSize(300, 20)
loadingBar.SetValue(0.75)  // 75% complete
```

---

### ListBox

Scrollable list of selectable items.

```rust
var list = new ListBox(parent: Widget) -> ListBox

list.Selected -> object           // Read-only: selected item handle
list.AddItem(text: string) -> object  // Returns item handle
list.RemoveItem(item: object) -> void
list.Clear() -> void
list.Select(item: object) -> void
```

**Example:**
```rust
var fileList = new ListBox(app.Root)
fileList.SetSize(200, 300)

var item1 = fileList.AddItem("document.txt")
var item2 = fileList.AddItem("image.png")
var item3 = fileList.AddItem("data.json")

fileList.Select(item1)
```

---

### RadioGroup / RadioButton

Mutually exclusive selection buttons.

```rust
var group = new RadioGroup() -> RadioGroup
var radio = new RadioButton(parent: Widget, text: string, group: RadioGroup) -> RadioButton

radio.IsSelected() -> bool
radio.SetSelected(selected: bool) -> void
group.Destroy() -> void
```

**Example:**
```rust
var sizeGroup = new RadioGroup()
var small = new RadioButton(app.Root, "Small", sizeGroup)
var medium = new RadioButton(app.Root, "Medium", sizeGroup)
var large = new RadioButton(app.Root, "Large", sizeGroup)

medium.SetSelected(true)  // Default selection

if large.IsSelected() {
    // Large size chosen
}
```

---

### Spinner

Numeric input with increment/decrement buttons.

```rust
var spinner = new Spinner(parent: Widget) -> Spinner

spinner.Value -> f64              // Read-only: current value
spinner.SetValue(value: f64) -> void
spinner.SetRange(min: f64, max: f64) -> void
spinner.SetStep(step: f64) -> void
spinner.SetDecimals(decimals: i64) -> void
```

**Example:**
```rust
var quantity = new Spinner(app.Root)
quantity.SetRange(1.0, 100.0)
quantity.SetStep(1.0)
quantity.SetDecimals(0)
quantity.SetValue(1.0)
```

---

### Image

Image display widget.

```rust
var image = new Image(parent: Widget) -> Image

image.SetPixels(pixels: Pixels, width: i64, height: i64) -> void
image.Clear() -> void
image.SetScaleMode(mode: i64) -> void  // 0=none, 1=fit, 2=fill, 3=stretch
image.SetOpacity(opacity: f64) -> void  // 0.0 to 1.0
```

---

### Layout Containers

Containers for organizing widgets.

```rust
// Vertical layout (top to bottom)
var vbox = new VBox() -> VBox
vbox.SetSpacing(spacing: f64) -> void
vbox.SetPadding(padding: f64) -> void
vbox.AddChild(widget: Widget) -> void

// Horizontal layout (left to right)
var hbox = new HBox() -> HBox
hbox.SetSpacing(spacing: f64) -> void
hbox.SetPadding(padding: f64) -> void
hbox.AddChild(widget: Widget) -> void
```

**Example:**
```rust
var form = new VBox()
form.SetSpacing(8.0)
form.SetPadding(16.0)
app.Root.AddChild(form)

var nameLabel = new Label(form, "Name:")
var nameInput = new TextInput(form)
var submitBtn = new Button(form, "Submit")
```

---

### ScrollView

Scrollable container for large content.

```rust
var scroll = new ScrollView(parent: Widget) -> ScrollView

scroll.SetScroll(x: f64, y: f64) -> void
scroll.SetContentSize(width: f64, height: f64) -> void
scroll.AddChild(widget: Widget) -> void
```

---

### SplitPane

Resizable split container.

```rust
var split = new SplitPane(parent: Widget, horizontal: bool) -> SplitPane

split.First -> Widget             // Read-only: first pane
split.Second -> Widget            // Read-only: second pane
split.SetPosition(position: f64) -> void  // 0.0 to 1.0
```

**Example:**
```rust
var split = new SplitPane(app.Root, true)  // Horizontal split
split.SetPosition(0.3)  // 30% / 70% split

var sidebar = new VBox()
split.First.AddChild(sidebar)

var content = new VBox()
split.Second.AddChild(content)
```

---

### TreeView

Hierarchical tree display.

```rust
var tree = new TreeView(parent: Widget) -> TreeView

tree.AddNode(parent: object, text: string) -> object  // parent=null for root
tree.RemoveNode(node: object) -> void
tree.Clear() -> void
tree.Expand(node: object) -> void
tree.Collapse(node: object) -> void
tree.Select(node: object) -> void
tree.SetFont(font: Font, size: f64) -> void
```

**Example:**
```rust
var tree = new TreeView(app.Root)
var root = tree.AddNode(null, "Project")
var src = tree.AddNode(root, "src")
tree.AddNode(src, "main.zia")
tree.AddNode(src, "utils.zia")
var docs = tree.AddNode(root, "docs")
tree.AddNode(docs, "README.md")

tree.Expand(root)
```

---

### TabBar

Tabbed interface container.

```rust
var tabs = new TabBar(parent: Widget) -> TabBar

tabs.AddTab(title: string, closable: bool) -> Tab
tabs.RemoveTab(tab: Tab) -> void
tabs.SetActive(tab: Tab) -> void
```

```rust
// Tab object
tab.SetTitle(title: string) -> void
tab.SetModified(modified: bool) -> void  // Shows indicator
```

---

### CodeEditor

Multi-line code editor with syntax highlighting support.

```rust
var editor = new CodeEditor(parent: Widget) -> CodeEditor

editor.Text -> string             // Read-only: current content
editor.LineCount -> i64           // Read-only: number of lines
editor.SetText(text: string) -> void
editor.SetCursor(line: i64, col: i64) -> void
editor.ScrollToLine(line: i64) -> void
editor.IsModified() -> bool
editor.ClearModified() -> void
editor.SetFont(font: Font, size: f64) -> void
```

---

### Theme

Global theme control.

```rust
Theme.SetDark() -> void   // Dark theme (light text on dark background)
Theme.SetLight() -> void  // Light theme (dark text on light background)
```

**Example:**
```rust
// Set theme before creating widgets
Theme.SetDark()

var app = new App("Dark Mode App", 800, 600)
// All widgets will use dark theme colors
```

---

## Viper.Input

Input handling for interactive and game applications.

> **See also:** [Chapter 20: Input Handling](../part4-applications/20-input.md).

---

### Keyboard

```rust
Input.isKeyDown(key: Key) -> bool           // Currently held
Input.wasKeyPressed(key: Key) -> bool       // Just pressed this frame
Input.wasKeyReleased(key: Key) -> bool      // Just released this frame
```

**Key constants:**
- Letters: `Key.A` through `Key.Z`
- Numbers: `Key.NUM_0` through `Key.NUM_9`
- Arrows: `Key.UP`, `Key.DOWN`, `Key.LEFT`, `Key.RIGHT`
- Special: `Key.SPACE`, `Key.ENTER`, `Key.ESCAPE`, `Key.TAB`, `Key.BACKSPACE`
- Modifiers: `Key.SHIFT`, `Key.CTRL`, `Key.ALT`
- Function keys: `Key.F1` through `Key.F12`

**Example:**
```rust
// Game loop input
while canvas.isOpen() {
    // Movement
    if Input.isKeyDown(Key.LEFT) {
        playerX -= 5;
    }
    if Input.isKeyDown(Key.RIGHT) {
        playerX += 5;
    }

    // Jump (only on initial press)
    if Input.wasKeyPressed(Key.SPACE) and onGround {
        playerVelocityY = -15;
        onGround = false;
    }

    // Pause toggle
    if Input.wasKeyPressed(Key.ESCAPE) {
        isPaused = !isPaused;
    }

    canvas.show();
}
```

---

### Mouse

```rust
Input.mouseX() -> f64
Input.mouseY() -> f64
Input.isMouseDown(button: MouseButton) -> bool
Input.wasMousePressed(button: MouseButton) -> bool
Input.wasMouseReleased(button: MouseButton) -> bool
Input.mouseScroll() -> f64                  // Wheel delta
```

**MouseButton constants:** `MouseButton.LEFT`, `MouseButton.RIGHT`, `MouseButton.MIDDLE`

**Example:**
```rust
// Drawing application
if Input.isMouseDown(MouseButton.LEFT) {
    canvas.setPixel(Input.mouseX().toInt(), Input.mouseY().toInt());
}

// Zoom with scroll wheel
var zoom = 1.0;
zoom += Input.mouseScroll() * 0.1;
zoom = Math.clamp(zoom, 0.1, 5.0);
```

---

### Game Controller

```rust
Input.isControllerConnected(index: i64) -> bool
Input.controllerAxis(index: i64, axis: Axis) -> f64       // -1.0 to 1.0
Input.isControllerButtonDown(index: i64, button: ControllerButton) -> bool
```

**Axis constants:** `Axis.LEFT_X`, `Axis.LEFT_Y`, `Axis.RIGHT_X`, `Axis.RIGHT_Y`, `Axis.LEFT_TRIGGER`, `Axis.RIGHT_TRIGGER`

**ControllerButton constants:** `ControllerButton.A`, `ControllerButton.B`, `ControllerButton.X`, `ControllerButton.Y`, `ControllerButton.START`, `ControllerButton.SELECT`, etc.

**Example:**
```rust
if Input.isControllerConnected(0) {
    var moveX = Input.controllerAxis(0, Axis.LEFT_X);
    var moveY = Input.controllerAxis(0, Axis.LEFT_Y);

    // Dead zone handling
    if Math.abs(moveX) < 0.15 { moveX = 0.0; }
    if Math.abs(moveY) < 0.15 { moveY = 0.0; }

    playerX += moveX * speed;
    playerY += moveY * speed;

    if Input.isControllerButtonDown(0, ControllerButton.A) {
        jump();
    }
}
```

---

## Viper.Crypto

Cryptographic hashing and encoding functions.

---

### Hashing

```rust
func Viper.Crypto.md5(data: string) -> string      // 32-char hex string
func Viper.Crypto.sha1(data: string) -> string     // 40-char hex string
func Viper.Crypto.sha256(data: string) -> string   // 64-char hex string
func Viper.Crypto.sha512(data: string) -> string   // 128-char hex string
```

**Example:**
```rust
var password = "secretpassword";
var hash = Viper.Crypto.sha256(password);
Terminal.Say("Hash: " + hash);
// Hash: 2bb80d537b1da3e38bd30361aa855686bde0eacd7162fef6a25fe97bf527a25b

// File integrity check
var content = Viper.File.readText("important.dat");
var checksum = Viper.Crypto.sha256(content);
```

**Note:** MD5 and SHA1 are considered weak for security purposes. Use SHA256 or SHA512 for security-sensitive applications.

---

### Random Bytes

```rust
func Viper.Crypto.randomBytes(length: i64) -> [u8]
func Viper.Crypto.randomHex(length: i64) -> string
```

**Example:**
```rust
// Generate secure token
var token = Viper.Crypto.randomHex(32);  // 64-character hex string

// Generate random key
var keyBytes = Viper.Crypto.randomBytes(16);  // 16 random bytes
```

---

### Encoding

```rust
// Base64
func Viper.Crypto.base64Encode(data: [u8]) -> string
func Viper.Crypto.base64Decode(encoded: string) -> [u8]

// Hexadecimal
func Viper.Crypto.hexEncode(data: [u8]) -> string
func Viper.Crypto.hexDecode(hex: string) -> [u8]
```

**Example:**
```rust
var original = "Hello, World!";
var bytes = original.toBytes();

// Base64
var encoded = Viper.Crypto.base64Encode(bytes);
Terminal.Say(encoded);  // "SGVsbG8sIFdvcmxkIQ=="

var decoded = Viper.Crypto.base64Decode(encoded);
var text = String.fromBytes(decoded);  // "Hello, World!"
```

---

## Viper.Environment

System information and environment variables.

---

### Environment Variables

```rust
func Viper.Environment.GetVariable(name: string) -> string
func Viper.Environment.HasVariable(name: string) -> Boolean
func Viper.Environment.SetVariable(name: string, value: string) -> void
```

**Example:**
```rust
bind Viper.Environment;
bind Viper.Terminal;

if Environment.HasVariable("DATABASE_HOST") {
    var dbHost = Environment.GetVariable("DATABASE_HOST");
    Terminal.Say("Connecting to: " + dbHost);
}
```

---

### System Information

```rust
Viper.Environment.os           // "windows", "macos", "linux"
Viper.Environment.arch         // "x64", "arm64"
Viper.Environment.cpuCount     // Number of CPU cores
Viper.Environment.homeDir      // User's home directory
Viper.Environment.currentDir   // Current working directory
Viper.Environment.tempDir      // System temp directory
```

**Example:**
```rust
Terminal.Say("OS: " + Viper.Environment.os);
Terminal.Say("Architecture: " + Viper.Environment.arch);
Terminal.Say("CPU cores: " + Viper.Environment.cpuCount);

// Platform-specific paths (use IO.Path.Join for path construction)
// var configPath = IO.Path.Join(homeDir, "config.json");
```

---

### Process Control

```rust
func Viper.Environment.GetArgumentCount() -> Integer
func Viper.Environment.GetArgument(index: Integer) -> string
func Viper.Environment.GetCommandLine() -> string
func Viper.Environment.EndProgram(code: Integer) -> void
```

**Example:**
```rust
bind Viper.Environment;
bind Viper.Terminal;

var count = Environment.GetArgumentCount();

if count < 2 {
    Terminal.Say("Usage: program <filename>");
    Environment.EndProgram(1);
}

var filename = Environment.GetArgument(1);
processFile(filename);
Environment.EndProgram(0);  // Success
```

---

## Viper.Regex

Regular expressions for pattern matching and text manipulation.

---

### Creating Patterns

```rust
func Regex.compile(pattern: string) -> Regex
```

**Example:**
```rust
var emailPattern = Regex.compile("[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}");
var phonePattern = Regex.compile("\\d{3}-\\d{3}-\\d{4}");
```

**Note:** Backslashes must be escaped in strings (`\\d` not `\d`).

---

### Matching

```rust
regex.matches(text: string) -> bool
regex.find(text: string) -> Match?        // First match
regex.findAll(text: string) -> [Match]    // All matches
```

**Match properties:**
```rust
match.value      // The matched text
match.start      // Start index in original string
match.end        // End index
match.groups     // [string] - capture groups
```

**Example:**
```rust
var pattern = Regex.compile("(\\d{4})-(\\d{2})-(\\d{2})");

if pattern.matches("2024-03-15") {
    var match = pattern.find("2024-03-15");
    Terminal.Say("Year: " + match.groups[1]);   // "2024"
    Terminal.Say("Month: " + match.groups[2]);  // "03"
    Terminal.Say("Day: " + match.groups[3]);    // "15"
}
```

---

### Replacing

```rust
regex.replace(text: string, replacement: string) -> string      // First match
regex.replaceAll(text: string, replacement: string) -> string   // All matches
```

**Example:**
```rust
var pattern = Regex.compile("\\s+");  // One or more whitespace

var text = "Hello    World   !";
var normalized = pattern.replaceAll(text, " ");
// "Hello World !"

// Censoring words
var censor = Regex.compile("bad|ugly|wrong");
var cleaned = censor.replaceAll(input, "****");
```

---

### Splitting

```rust
regex.split(text: string) -> [string]
```

**Example:**
```rust
var pattern = Regex.compile("[,;\\s]+");  // Comma, semicolon, or whitespace

var items = pattern.split("apple, banana; cherry  orange");
// ["apple", "banana", "cherry", "orange"]
```

---

### Common Patterns

```rust
// Email validation
var email = Regex.compile("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");

// URL
var url = Regex.compile("https?://[\\w.-]+(?:/[\\w./-]*)?");

// Phone (US format)
var phone = Regex.compile("\\(?\\d{3}\\)?[-. ]?\\d{3}[-. ]?\\d{4}");

// IP address
var ip = Regex.compile("\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}");

// Integers
var integer = Regex.compile("-?\\d+");

// Floating point
var float = Regex.compile("-?\\d+\\.?\\d*");
```

---

## Viper.Process

Running and controlling external processes.

---

### Simple Execution

```rust
func Process.run(command: string, args: [string]) -> ProcessResult
func Process.run(command: string, args: [string], options: ProcessOptions) -> ProcessResult
```

**ProcessResult fields:**
```rust
result.exitCode   // i64 - process exit code (0 usually means success)
result.stdout     // string - standard output
result.stderr     // string - standard error
```

**ProcessOptions fields:**
```rust
{
    cwd: string,              // Working directory
    env: Map<string, string>, // Environment variables
    timeout: i64              // Timeout in milliseconds
}
```

**Example:**
```rust
// Simple command
var result = Process.run("ls", ["-la"]);
Terminal.Say(result.stdout);

if result.exitCode != 0 {
    Terminal.SayError("Command failed: " + result.stderr);
}

// With options
var result = Process.run("npm", ["install"], {
    cwd: "/path/to/project",
    timeout: 60000
});
```

---

### Background Processes

```rust
func Process.spawn(command: string, args: [string]) -> Process

process.write(input: string) -> void
process.read() -> string
process.readLine() -> string
process.kill() -> void
process.wait() -> i64        // Returns exit code
```

**Example:**
```rust
// Interactive process
var process = Process.spawn("python3", []);

process.write("print(2 + 2)\n");
var output = process.readLine();
Terminal.Say("Result: " + output);  // "4"

process.write("exit()\n");
process.wait();

// Long-running background process
var server = Process.spawn("./server", ["--port", "8080"]);

// ... do other work ...

// Shut down when done
server.kill();
```

---

## Viper.Test

Testing utilities for writing automated tests.

> **See also:** [Chapter 27: Testing](../part5-mastery/27-testing.md) for comprehensive testing strategies.

---

### Assertions

```rust
assert condition;
assert condition, "message";
assertEqual(actual, expected);
assertNotEqual(a, b);
assertNull(value);
assertNotNull(value);
assertClose(actual, expected, tolerance);
assertThrows(fn: func());
assertContains(collection, item);
assertEmpty(collection);
assertLength(collection, length);
```

**Example:**
```rust
// Basic assertions
assert 2 + 2 == 4;
assert isValid, "Value should be valid";

// Equality
assertEqual(calculateSum([1, 2, 3]), 6);
assertNotEqual(result, nil);

// Null checks
assertNull(findUser("nonexistent"));
assertNotNull(findUser("alice"));

// Floating point comparison
assertClose(Math.PI, 3.14159, 0.00001);

// Exception testing
assertThrows(func() {
    divide(10, 0);  // Should throw
});

// Collection assertions
assertContains(users, "alice");
assertEmpty(errors);
assertLength(results, 5);
```

---

### Test Structure

```rust
test "description" {
    // test code
}

setup {
    // runs before each test
}

teardown {
    // runs after each test
}
```

**Example:**
```rust
module Calculator_Test;

bind Calculator;

var calc: Calculator;

setup {
    calc = Calculator.new();
}

teardown {
    calc.reset();
}

test "addition returns correct sum" {
    assertEqual(calc.add(2, 3), 5);
    assertEqual(calc.add(-1, 1), 0);
    assertEqual(calc.add(0, 0), 0);
}

test "division by zero throws error" {
    assertThrows(func() {
        calc.divide(10, 0);
    });
}

test "memory stores last result" {
    calc.add(5, 3);
    assertEqual(calc.memory, 8);
}
```

---

### Running Tests

From the command line:

```bash
viper test                    # Run all tests
viper test Calculator_Test    # Run specific test module
viper test --verbose          # Show detailed output
```

---

## Quick Reference Cards

### String Methods (Built-in)

```rust
str.len()                     // -> i64
str.charAt(index)             // -> string (single char)
str.substring(start, end)     // -> string
str.toLower()                 // -> string
str.toUpper()                 // -> string
str.trim()                    // -> string
str.split(delimiter)          // -> [string]
str.contains(substring)       // -> bool
str.startsWith(prefix)        // -> bool
str.endsWith(suffix)          // -> bool
str.replace(old, new)         // -> string
str.indexOf(substring)        // -> i64 (-1 if not found)
str.toInt()                   // -> i64
str.toFloat()                 // -> f64
str.toBytes()                 // -> [u8]
```

### Array Methods (Built-in)

```rust
arr.len()                     // -> i64
arr.push(item)                // Add to end
arr.pop()                     // -> item? (remove from end)
arr.first()                   // -> item?
arr.last()                    // -> item?
arr.contains(item)            // -> bool
arr.indexOf(item)             // -> i64 (-1 if not found)
arr.slice(start, end)         // -> [T]
arr.reverse()                 // -> [T]
arr.sort()                    // -> [T]
arr.sort(compareFn)           // -> [T]
arr.map(fn)                   // -> [U]
arr.filter(fn)                // -> [T]
arr.reduce(fn, initial)       // -> U
arr.forEach(fn)               // -> void
arr.enumerate()               // -> [(i64, T)]
arr.join(separator)           // -> string
```

---

*[Back to Table of Contents](../README.md) | [Prev: Appendix B](b-basic-reference.md) | [Next: Appendix E: Error Messages](e-error-messages.md)*
