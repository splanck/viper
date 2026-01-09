# Appendix D: Runtime Library Reference

This reference documents the Viper Runtime Library, the standard modules available in every Viper program. Use this appendix to quickly look up function signatures, understand parameters, and see practical examples.

**How to use this reference:** Each entry includes the complete function signature, a brief explanation of its purpose, return type information, and at least one example. Edge cases and common errors are noted where applicable.

---

## Quick Navigation

- [Viper.Terminal](#viperterminal) - Console I/O
- [Viper.File](#viperfile) - File system operations
- [Viper.Math](#vipermath) - Mathematical functions
- [Viper.Time](#vipertime) - Time and date operations
- [Viper.Collections](#vipercollections) - Data structures
- [Viper.Network](#vipernetwork) - HTTP, TCP, UDP
- [Viper.JSON](#viperjson) - JSON parsing/generation
- [Viper.Threading](#viperthreading) - Concurrency primitives
- [Viper.Graphics](#vipergraphics) - Drawing and windows
- [Viper.Input](#viperinput) - Keyboard, mouse, gamepad
- [Viper.Crypto](#vipercrypto) - Hashing and encoding
- [Viper.Environment](#viperenvironment) - System information
- [Viper.Regex](#viperregex) - Regular expressions
- [Viper.Process](#viperprocess) - External processes
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
Viper.Terminal.Say("Hello, World!");
Viper.Terminal.Say("The answer is: " + 42);
Viper.Terminal.Say(3.14159);  // Prints "3.14159"
```

**When to use:** Use `Say` when you want each message on its own line. This is the standard choice for most output.

---

#### Write

```rust
func Write(message: string) -> void
```

Prints a message without a trailing newline. Subsequent output continues on the same line.

**Parameters:**
- `message` - The text to display

**Example:**
```rust
Viper.Terminal.Write("Loading");
for i in 0..5 {
    Viper.Time.sleep(500);
    Viper.Terminal.Write(".");
}
Viper.Terminal.Say(" done!");  // Output: Loading..... done!
```

**When to use:** Use `Write` for progress indicators, inline prompts, or when building output piece by piece.

---

#### SayError

```rust
func SayError(message: string) -> void
```

Prints a message to standard error (stderr) with a newline. Error messages go to a separate stream that can be redirected independently from normal output.

**Parameters:**
- `message` - The error message to display

**Example:**
```rust
if !Viper.File.exists(filename) {
    Viper.Terminal.SayError("Error: File not found: " + filename);
    Viper.Environment.exit(1);
}
```

**When to use:** Use `SayError` for error messages, warnings, and diagnostic output that should not be mixed with normal program output.

**Edge case:** On some terminals, stderr output may appear in a different color or be interleaved differently with stdout.

---

### Input Functions

#### Ask

```rust
func Ask(prompt: string) -> string
```

Displays a prompt and waits for the user to type a line of input. Returns when the user presses Enter.

**Parameters:**
- `prompt` - Text to display before waiting for input

**Returns:** The user's input as a string (without the trailing newline)

**Example:**
```rust
var name = Viper.Terminal.Ask("What is your name? ");
var ageStr = Viper.Terminal.Ask("How old are you? ");
var age = ageStr.toInt();

Viper.Terminal.Say("Hello, " + name + "! You are " + age + " years old.");
```

**When to use:** Use `Ask` for any situation where you need user input, from simple prompts to form-style data entry.

**Edge cases:**
- Returns an empty string if the user presses Enter without typing anything
- If input is redirected from a file and EOF is reached, returns an empty string

---

#### GetChar

```rust
func GetChar() -> string
```

Reads a single character from input without waiting for Enter. Useful for immediate keyboard response.

**Returns:** A single-character string

**Example:**
```rust
Viper.Terminal.Say("Continue? (y/n)");
var response = Viper.Terminal.GetChar();

if response == "y" or response == "Y" {
    Viper.Terminal.Say("Continuing...");
} else {
    Viper.Terminal.Say("Cancelled.");
}
```

**When to use:** Use `GetChar` for yes/no prompts, menu selections, or any time you want instant response to a single keystroke.

**Edge case:** Returns immediately without displaying the typed character. You may want to echo it back if the user should see what they typed.

---

#### GetKey

```rust
func GetKey() -> Key
```

Reads a single keypress, including special keys like arrows, function keys, and modifier combinations that `GetChar` cannot detect.

**Returns:** A `Key` object with properties for the key pressed

**Example:**
```rust
Viper.Terminal.Say("Use arrow keys to move, Q to quit");

loop {
    var key = Viper.Terminal.GetKey();

    match key.code {
        Key.UP -> Viper.Terminal.Say("Moving up"),
        Key.DOWN -> Viper.Terminal.Say("Moving down"),
        Key.LEFT -> Viper.Terminal.Say("Moving left"),
        Key.RIGHT -> Viper.Terminal.Say("Moving right"),
        Key.Q -> break,
        _ -> Viper.Terminal.Say("Unknown key")
    }
}
```

**When to use:** Use `GetKey` for games, TUI applications, or any program that needs to respond to arrow keys, function keys, or key combinations.

**Key codes:** `Key.UP`, `Key.DOWN`, `Key.LEFT`, `Key.RIGHT`, `Key.ENTER`, `Key.ESCAPE`, `Key.BACKSPACE`, `Key.TAB`, `Key.F1` through `Key.F12`, plus letter and number keys.

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
    Viper.Terminal.Clear();
    Viper.Terminal.Say("=== MAIN MENU ===");
    Viper.Terminal.Say("1. New Game");
    Viper.Terminal.Say("2. Load Game");
    Viper.Terminal.Say("3. Quit");
}
```

**When to use:** Use `Clear` at the start of new screens, menu transitions, or to refresh a full-screen display.

---

#### SetColor / ResetColor

```rust
func SetColor(color: Color) -> void
func ResetColor() -> void
```

Sets the text color for subsequent output. `ResetColor` returns to the terminal's default colors.

**Parameters:**
- `color` - A predefined color constant or custom `Color(r, g, b)`

**Available colors:** `Color.RED`, `Color.GREEN`, `Color.BLUE`, `Color.YELLOW`, `Color.CYAN`, `Color.MAGENTA`, `Color.WHITE`, `Color.BLACK`

**Example:**
```rust
Viper.Terminal.SetColor(Color.RED);
Viper.Terminal.Say("ERROR: Something went wrong!");
Viper.Terminal.ResetColor();

Viper.Terminal.SetColor(Color.GREEN);
Viper.Terminal.Say("SUCCESS: Operation completed.");
Viper.Terminal.ResetColor();
```

**When to use:** Use colors sparingly to highlight important information like errors (red), warnings (yellow), or success messages (green).

**Edge case:** Not all terminals support colors. On unsupported terminals, these functions have no effect.

---

#### MoveCursor

```rust
func MoveCursor(x: i64, y: i64) -> void
```

Positions the cursor at the specified column and row. Coordinates are 1-based (top-left is 1,1).

**Parameters:**
- `x` - Column number (1 = leftmost)
- `y` - Row number (1 = topmost)

**Example:**
```rust
// Draw a box around the screen edge
Viper.Terminal.Clear();

// Top border
Viper.Terminal.MoveCursor(1, 1);
Viper.Terminal.Write("+------------------------+");

// Side borders
for row in 2..10 {
    Viper.Terminal.MoveCursor(1, row);
    Viper.Terminal.Write("|");
    Viper.Terminal.MoveCursor(26, row);
    Viper.Terminal.Write("|");
}

// Bottom border
Viper.Terminal.MoveCursor(1, 10);
Viper.Terminal.Write("+------------------------+");
```

**When to use:** Use cursor positioning for TUI (text user interface) applications, games, or formatted displays.

---

#### HideCursor / ShowCursor

```rust
func HideCursor() -> void
func ShowCursor() -> void
```

Controls cursor visibility. Hiding the cursor creates a cleaner appearance for animations and games.

**Example:**
```rust
Viper.Terminal.HideCursor();

// Animate a spinner
var frames = ["|", "/", "-", "\\"];
for i in 0..20 {
    Viper.Terminal.Write("\r" + frames[i % 4] + " Loading...");
    Viper.Time.sleep(100);
}

Viper.Terminal.ShowCursor();
Viper.Terminal.Say("\rDone!          ");
```

**When to use:** Hide the cursor during animations or full-screen displays. Always remember to show it again before the program exits.

---

### Common Terminal Patterns

#### Progress Bar

```rust
func showProgress(current: i64, total: i64) {
    var percent = (current * 100) / total;
    var bars = percent / 5;  // 20 characters wide

    Viper.Terminal.Write("\r[");
    for i in 0..20 {
        if i < bars {
            Viper.Terminal.Write("=");
        } else {
            Viper.Terminal.Write(" ");
        }
    }
    Viper.Terminal.Write("] " + percent + "%");
}

// Usage
for i in 0..100 {
    showProgress(i, 100);
    Viper.Time.sleep(50);
}
Viper.Terminal.Say("");  // Newline when done
```

#### Simple Menu System

```rust
func showMenu(options: [string]) -> i64 {
    loop {
        Viper.Terminal.Clear();
        Viper.Terminal.Say("Select an option:");
        Viper.Terminal.Say("");

        for i, option in options.enumerate() {
            Viper.Terminal.Say("  " + (i + 1) + ". " + option);
        }

        Viper.Terminal.Say("");
        var choice = Viper.Terminal.Ask("Enter choice (1-" + options.len() + "): ");
        var num = choice.toInt();

        if num >= 1 and num <= options.len() {
            return num - 1;  // Return 0-based index
        }

        Viper.Terminal.Say("Invalid choice. Press any key...");
        Viper.Terminal.GetChar();
    }
}
```

---

## Viper.File

File system operations for reading, writing, and managing files and directories.

> **See also:** [Chapter 9: Files and Persistence](../part2-building-blocks/09-files.md) for comprehensive coverage of file operations.

---

### Reading Files

#### readText

```rust
func readText(path: string) -> string
```

Reads the entire contents of a text file as a string.

**Parameters:**
- `path` - Path to the file (relative or absolute)

**Returns:** The file contents as a string

**Example:**
```rust
var content = Viper.File.readText("config.txt");
Viper.Terminal.Say("File contents: " + content);
```

**When to use:** Use for small to medium text files (configuration files, data files, source code). For large files, consider `readLines` to process line by line.

**Edge cases:**
- Throws an error if the file does not exist
- Throws an error if the file cannot be read (permissions, locked)
- May produce unexpected results with binary files

---

#### readBytes

```rust
func readBytes(path: string) -> [u8]
```

Reads the entire contents of a file as raw bytes.

**Parameters:**
- `path` - Path to the file

**Returns:** Array of bytes (unsigned 8-bit integers)

**Example:**
```rust
var bytes = Viper.File.readBytes("image.png");
Viper.Terminal.Say("File size: " + bytes.len() + " bytes");

// Check PNG magic number
if bytes[0] == 0x89 and bytes[1] == 0x50 {
    Viper.Terminal.Say("Valid PNG file");
}
```

**When to use:** Use for binary files (images, archives, executables) or when you need precise byte-level access.

---

#### readLines

```rust
func readLines(path: string) -> [string]
```

Reads a text file and returns an array where each element is one line.

**Parameters:**
- `path` - Path to the file

**Returns:** Array of strings (lines without newline characters)

**Example:**
```rust
var lines = Viper.File.readLines("names.txt");

for i, line in lines.enumerate() {
    Viper.Terminal.Say("Line " + (i + 1) + ": " + line);
}
```

**When to use:** Use when processing line-based data (log files, CSV data, configuration with one entry per line).

---

### Writing Files

#### writeText

```rust
func writeText(path: string, content: string) -> void
```

Writes a string to a file, creating the file if it does not exist or overwriting it if it does.

**Parameters:**
- `path` - Path to the file
- `content` - Text to write

**Example:**
```rust
var data = "Name: Alice\nAge: 30\nCity: Boston";
Viper.File.writeText("person.txt", data);
```

**When to use:** Use to save text data, create configuration files, or export data.

**Edge cases:**
- Creates parent directories if they do not exist
- Overwrites existing content completely (use `appendText` to add to existing)
- Throws error if path is invalid or write permission denied

---

#### writeBytes

```rust
func writeBytes(path: string, data: [u8]) -> void
```

Writes raw bytes to a file.

**Parameters:**
- `path` - Path to the file
- `data` - Array of bytes to write

**Example:**
```rust
// Create a simple BMP file header
var header: [u8] = [0x42, 0x4D, ...];  // BMP magic number
Viper.File.writeBytes("output.bmp", header);
```

**When to use:** Use for binary file formats or when you have computed byte data to save.

---

#### appendText

```rust
func appendText(path: string, content: string) -> void
```

Adds text to the end of an existing file, or creates the file if it does not exist.

**Parameters:**
- `path` - Path to the file
- `content` - Text to append

**Example:**
```rust
// Simple logging
func log(message: string) {
    var timestamp = DateTime.now().format("YYYY-MM-DD HH:mm:ss");
    Viper.File.appendText("app.log", "[" + timestamp + "] " + message + "\n");
}

log("Application started");
log("Processing data...");
log("Application finished");
```

**When to use:** Use for log files, accumulating data over time, or any situation where you want to add to rather than replace existing content.

---

### File Operations

#### exists

```rust
func exists(path: string) -> bool
```

Checks whether a file or directory exists at the given path.

**Returns:** `true` if the path exists, `false` otherwise

**Example:**
```rust
var configPath = "settings.json";

if Viper.File.exists(configPath) {
    var config = Viper.File.readText(configPath);
    // Use existing config
} else {
    // Create default config
    Viper.File.writeText(configPath, "{}");
}
```

**When to use:** Always check existence before reading if the file might not exist. This prevents runtime errors.

---

#### delete

```rust
func delete(path: string) -> void
```

Deletes a file.

**Parameters:**
- `path` - Path to the file to delete

**Example:**
```rust
if Viper.File.exists("temp.txt") {
    Viper.File.delete("temp.txt");
    Viper.Terminal.Say("Temporary file cleaned up");
}
```

**Edge cases:**
- Throws error if the file does not exist
- Throws error if it is a directory (use `deleteDir` instead)
- Throws error if the file is locked or permission denied

---

#### rename

```rust
func rename(oldPath: string, newPath: string) -> void
```

Renames or moves a file.

**Parameters:**
- `oldPath` - Current path to the file
- `newPath` - New path for the file

**Example:**
```rust
// Rename a file
Viper.File.rename("draft.txt", "final.txt");

// Move to different directory
Viper.File.rename("temp/data.csv", "archive/data.csv");
```

**When to use:** Use for renaming files or moving them within the same filesystem.

**Edge case:** Moving across different drives/filesystems may fail on some operating systems.

---

#### copy

```rust
func copy(sourcePath: string, destPath: string) -> void
```

Creates a copy of a file.

**Parameters:**
- `sourcePath` - Path to the file to copy
- `destPath` - Path for the new copy

**Example:**
```rust
// Create backup before modifying
Viper.File.copy("config.json", "config.json.backup");

// Now safe to modify config.json
```

**When to use:** Use for backups, duplicating files, or when you need to preserve the original.

---

#### size

```rust
func size(path: string) -> i64
```

Returns the size of a file in bytes.

**Returns:** File size as a 64-bit integer

**Example:**
```rust
var bytes = Viper.File.size("video.mp4");
var megabytes = bytes / (1024 * 1024);
Viper.Terminal.Say("File size: " + megabytes + " MB");
```

---

#### modifiedTime

```rust
func modifiedTime(path: string) -> DateTime
```

Returns when the file was last modified.

**Returns:** A `DateTime` object

**Example:**
```rust
var modified = Viper.File.modifiedTime("document.txt");
var now = DateTime.now();

if now.diffDays(modified) > 30 {
    Viper.Terminal.Say("Warning: File is over 30 days old");
}
```

---

### Directory Operations

#### listDir

```rust
func listDir(path: string) -> [string]
```

Lists all files and subdirectories in a directory.

**Returns:** Array of names (not full paths)

**Example:**
```rust
var entries = Viper.File.listDir(".");

for entry in entries {
    if Viper.File.isDir(entry) {
        Viper.Terminal.Say("[DIR]  " + entry);
    } else {
        Viper.Terminal.Say("[FILE] " + entry);
    }
}
```

**Edge case:** Does not include "." or ".." entries.

---

#### createDir

```rust
func createDir(path: string) -> void
```

Creates a directory. Creates parent directories as needed.

**Example:**
```rust
Viper.File.createDir("output/reports/2024");
// Creates output/, output/reports/, and output/reports/2024/
```

---

#### deleteDir

```rust
func deleteDir(path: string) -> void
```

Deletes a directory and all its contents recursively.

**Example:**
```rust
if Viper.File.exists("temp_output") {
    Viper.File.deleteDir("temp_output");
}
```

**Warning:** This permanently deletes all files and subdirectories. Use with caution.

---

#### isDir / isFile

```rust
func isDir(path: string) -> bool
func isFile(path: string) -> bool
```

Checks whether a path points to a directory or a regular file.

**Example:**
```rust
if Viper.File.isDir(path) {
    // Process all files in directory
    for file in Viper.File.listDir(path) {
        processFile(Viper.File.join(path, file));
    }
} else if Viper.File.isFile(path) {
    processFile(path);
}
```

---

### Path Operations

#### join

```rust
func join(parts: ...string) -> string
```

Combines path components using the correct separator for the operating system.

**Example:**
```rust
var path = Viper.File.join("home", "alice", "documents", "report.txt");
// On Unix: "home/alice/documents/report.txt"
// On Windows: "home\alice\documents\report.txt"
```

**When to use:** Always use `join` instead of string concatenation with "/" or "\\". This ensures your code works on all operating systems.

---

#### basename

```rust
func basename(path: string) -> string
```

Returns just the filename portion of a path.

**Example:**
```rust
var filename = Viper.File.basename("/home/alice/report.txt");
// Returns: "report.txt"
```

---

#### dirname

```rust
func dirname(path: string) -> string
```

Returns the directory portion of a path.

**Example:**
```rust
var dir = Viper.File.dirname("/home/alice/report.txt");
// Returns: "/home/alice"
```

---

#### extension

```rust
func extension(path: string) -> string
```

Returns the file extension including the dot.

**Example:**
```rust
var ext = Viper.File.extension("photo.jpg");
// Returns: ".jpg"

// Common pattern: handling files by type
match Viper.File.extension(filename).toLower() {
    ".txt", ".md" -> handleTextFile(filename),
    ".jpg", ".png" -> handleImageFile(filename),
    ".json" -> handleJsonFile(filename),
    _ -> Viper.Terminal.Say("Unknown file type")
}
```

---

#### absolutePath

```rust
func absolutePath(relativePath: string) -> string
```

Converts a relative path to an absolute path.

**Example:**
```rust
var abs = Viper.File.absolutePath("../data/input.txt");
// Returns something like: "/home/alice/project/data/input.txt"
```

---

### Common File Patterns

#### Safe File Reading

```rust
func readFileSafe(path: string, defaultValue: string) -> string {
    if Viper.File.exists(path) {
        return Viper.File.readText(path);
    }
    return defaultValue;
}

var config = readFileSafe("config.txt", "default settings");
```

#### Processing All Files in Directory

```rust
func processDirectory(dirPath: string, ext: string) {
    for filename in Viper.File.listDir(dirPath) {
        var fullPath = Viper.File.join(dirPath, filename);

        if Viper.File.isFile(fullPath) and Viper.File.extension(filename) == ext {
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
Viper.Math.PI      // 3.14159265358979323846 - ratio of circle circumference to diameter
Viper.Math.E       // 2.71828182845904523536 - base of natural logarithm
Viper.Math.TAU     // 6.28318530717958647692 - 2*PI, full circle in radians
```

**Example:**
```rust
var radius = 5.0;
var circumference = 2.0 * Viper.Math.PI * radius;  // or TAU * radius
var area = Viper.Math.PI * radius * radius;
```

---

### Basic Functions

#### abs

```rust
func abs(x: Number) -> Number
```

Returns the absolute (non-negative) value.

**Example:**
```rust
Viper.Math.abs(-5)      // Returns: 5
Viper.Math.abs(5)       // Returns: 5
Viper.Math.abs(-3.14)   // Returns: 3.14
```

---

#### sign

```rust
func sign(x: Number) -> i64
```

Returns -1 for negative numbers, 0 for zero, 1 for positive numbers.

**Example:**
```rust
Viper.Math.sign(-42)    // Returns: -1
Viper.Math.sign(0)      // Returns: 0
Viper.Math.sign(42)     // Returns: 1

// Useful for direction
var direction = Viper.Math.sign(targetX - currentX);
currentX += direction * speed;
```

---

#### min / max

```rust
func min(a: Number, b: Number) -> Number
func max(a: Number, b: Number) -> Number
```

Returns the smaller or larger of two values.

**Example:**
```rust
Viper.Math.min(3, 7)    // Returns: 3
Viper.Math.max(3, 7)    // Returns: 7

// Keep value in bounds
var health = Viper.Math.max(0, health - damage);  // Never below 0
var health = Viper.Math.min(100, health + healing);  // Never above 100
```

---

#### clamp

```rust
func clamp(value: Number, min: Number, max: Number) -> Number
```

Constrains a value to a range. Equivalent to `max(min, min(max, value))`.

**Example:**
```rust
Viper.Math.clamp(150, 0, 100)   // Returns: 100
Viper.Math.clamp(-50, 0, 100)   // Returns: 0
Viper.Math.clamp(50, 0, 100)    // Returns: 50

// Common use: keep game entities on screen
playerX = Viper.Math.clamp(playerX, 0, screenWidth);
playerY = Viper.Math.clamp(playerY, 0, screenHeight);
```

---

### Rounding Functions

#### floor

```rust
func floor(x: f64) -> f64
```

Rounds down to the nearest integer (toward negative infinity).

**Example:**
```rust
Viper.Math.floor(3.7)   // Returns: 3.0
Viper.Math.floor(3.2)   // Returns: 3.0
Viper.Math.floor(-3.2)  // Returns: -4.0 (toward negative infinity)
```

---

#### ceil

```rust
func ceil(x: f64) -> f64
```

Rounds up to the nearest integer (toward positive infinity).

**Example:**
```rust
Viper.Math.ceil(3.2)    // Returns: 4.0
Viper.Math.ceil(3.0)    // Returns: 3.0
Viper.Math.ceil(-3.7)   // Returns: -3.0 (toward positive infinity)
```

---

#### round

```rust
func round(x: f64) -> f64
```

Rounds to the nearest integer. Halfway cases round away from zero.

**Example:**
```rust
Viper.Math.round(3.4)   // Returns: 3.0
Viper.Math.round(3.5)   // Returns: 4.0
Viper.Math.round(-3.5)  // Returns: -4.0
```

---

#### trunc

```rust
func trunc(x: f64) -> f64
```

Truncates toward zero (removes the fractional part).

**Example:**
```rust
Viper.Math.trunc(3.7)   // Returns: 3.0
Viper.Math.trunc(-3.7)  // Returns: -3.0 (toward zero, not down)
```

---

### Powers and Roots

#### sqrt

```rust
func sqrt(x: f64) -> f64
```

Returns the square root.

**Example:**
```rust
Viper.Math.sqrt(16.0)   // Returns: 4.0
Viper.Math.sqrt(2.0)    // Returns: 1.41421356...

// Distance between two points
func distance(x1: f64, y1: f64, x2: f64, y2: f64) -> f64 {
    var dx = x2 - x1;
    var dy = y2 - y1;
    return Viper.Math.sqrt(dx*dx + dy*dy);
}
```

**Edge case:** Returns `NaN` (Not a Number) for negative inputs.

---

#### cbrt

```rust
func cbrt(x: f64) -> f64
```

Returns the cube root. Unlike `sqrt`, works with negative numbers.

**Example:**
```rust
Viper.Math.cbrt(27.0)   // Returns: 3.0
Viper.Math.cbrt(-27.0)  // Returns: -3.0
```

---

#### pow

```rust
func pow(base: f64, exponent: f64) -> f64
```

Returns base raised to the power of exponent.

**Example:**
```rust
Viper.Math.pow(2.0, 10.0)   // Returns: 1024.0
Viper.Math.pow(10.0, 3.0)   // Returns: 1000.0
Viper.Math.pow(4.0, 0.5)    // Returns: 2.0 (same as sqrt)

// Compound interest
func futureValue(principal: f64, rate: f64, years: i64) -> f64 {
    return principal * Viper.Math.pow(1.0 + rate, years.toF64());
}
```

---

#### exp

```rust
func exp(x: f64) -> f64
```

Returns e raised to the power x.

**Example:**
```rust
Viper.Math.exp(1.0)     // Returns: 2.71828... (e)
Viper.Math.exp(0.0)     // Returns: 1.0
```

---

#### log / log10 / log2

```rust
func log(x: f64) -> f64      // Natural logarithm (base e)
func log10(x: f64) -> f64    // Base-10 logarithm
func log2(x: f64) -> f64     // Base-2 logarithm
```

**Example:**
```rust
Viper.Math.log(Viper.Math.E)   // Returns: 1.0
Viper.Math.log10(1000.0)       // Returns: 3.0
Viper.Math.log2(1024.0)        // Returns: 10.0

// Number of bits needed to represent a number
func bitsNeeded(n: i64) -> i64 {
    return Viper.Math.ceil(Viper.Math.log2(n.toF64() + 1.0)).toInt();
}
```

**Edge case:** Returns negative infinity for 0, `NaN` for negative inputs.

---

### Trigonometry

All trigonometric functions use radians (not degrees).

#### sin / cos / tan

```rust
func sin(x: f64) -> f64    // Sine
func cos(x: f64) -> f64    // Cosine
func tan(x: f64) -> f64    // Tangent
```

**Example:**
```rust
Viper.Math.sin(0.0)                   // Returns: 0.0
Viper.Math.sin(Viper.Math.PI / 2.0)   // Returns: 1.0
Viper.Math.cos(0.0)                   // Returns: 1.0
Viper.Math.cos(Viper.Math.PI)         // Returns: -1.0

// Circular motion
func circularPosition(angle: f64, radius: f64) -> (f64, f64) {
    var x = Viper.Math.cos(angle) * radius;
    var y = Viper.Math.sin(angle) * radius;
    return (x, y);
}
```

---

#### asin / acos / atan / atan2

```rust
func asin(x: f64) -> f64               // Arc sine (inverse)
func acos(x: f64) -> f64               // Arc cosine (inverse)
func atan(x: f64) -> f64               // Arc tangent (inverse)
func atan2(y: f64, x: f64) -> f64      // Arc tangent of y/x (handles quadrants)
```

**Example:**
```rust
// atan2 is usually what you want for angles between points
func angleTo(fromX: f64, fromY: f64, toX: f64, toY: f64) -> f64 {
    return Viper.Math.atan2(toY - fromY, toX - fromX);
}

var angle = angleTo(0.0, 0.0, 1.0, 1.0);  // 45 degrees = PI/4 radians
```

**Note:** `atan2(y, x)` correctly handles all four quadrants, unlike `atan(y/x)`.

---

#### Hyperbolic Functions

```rust
func sinh(x: f64) -> f64    // Hyperbolic sine
func cosh(x: f64) -> f64    // Hyperbolic cosine
func tanh(x: f64) -> f64    // Hyperbolic tangent
```

---

#### Angle Conversion

```rust
func toRadians(degrees: f64) -> f64
func toDegrees(radians: f64) -> f64
```

**Example:**
```rust
var rad = Viper.Math.toRadians(90.0);   // Returns: PI/2
var deg = Viper.Math.toDegrees(Viper.Math.PI);  // Returns: 180.0

// If you have user input in degrees
var userAngle = 45.0;  // degrees
var x = Viper.Math.cos(Viper.Math.toRadians(userAngle));
```

---

### Random Numbers

#### random

```rust
func random() -> f64
```

Returns a random floating-point number from 0.0 (inclusive) to 1.0 (exclusive).

**Example:**
```rust
var r = Viper.Math.random();  // e.g., 0.7231498...

// Random number in range [min, max)
func randomRange(min: f64, max: f64) -> f64 {
    return min + Viper.Math.random() * (max - min);
}

var temperature = randomRange(-10.0, 40.0);
```

---

#### randomInt

```rust
func randomInt(min: i64, max: i64) -> i64
```

Returns a random integer in the range [min, max] (inclusive on both ends).

**Example:**
```rust
var diceRoll = Viper.Math.randomInt(1, 6);
var coinFlip = Viper.Math.randomInt(0, 1);  // 0 or 1

// Random array element
func randomChoice<T>(arr: [T]) -> T {
    var index = Viper.Math.randomInt(0, arr.len() - 1);
    return arr[index];
}

var colors = ["red", "green", "blue"];
var picked = randomChoice(colors);
```

---

#### randomSeed

```rust
func randomSeed(seed: i64) -> void
```

Sets the random number generator seed. Same seed produces same sequence.

**Example:**
```rust
// Reproducible random sequence
Viper.Math.randomSeed(12345);
var a = Viper.Math.randomInt(1, 100);  // Always same value
var b = Viper.Math.randomInt(1, 100);  // Always same value

// Reset to get same sequence again
Viper.Math.randomSeed(12345);
var c = Viper.Math.randomInt(1, 100);  // c == a
```

**When to use:** Use for testing, reproducible simulations, or games with "daily challenges."

---

## Viper.Time

Time and date operations for timing, delays, and date manipulation.

---

### Current Time

#### millis / nanos

```rust
func millis() -> i64    // Milliseconds since epoch (Jan 1, 1970)
func nanos() -> i64     // Nanoseconds since epoch
```

**Example:**
```rust
// Measure execution time
var start = Viper.Time.millis();

// ... do some work ...

var elapsed = Viper.Time.millis() - start;
Viper.Terminal.Say("Operation took " + elapsed + " ms");
```

---

#### now

```rust
func now() -> DateTime
```

Returns the current date and time.

**Example:**
```rust
var now = Viper.Time.now();
Viper.Terminal.Say("Current time: " + now.format("YYYY-MM-DD HH:mm:ss"));
```

---

### Delays

#### sleep

```rust
func sleep(milliseconds: i64) -> void
```

Pauses execution for the specified duration.

**Example:**
```rust
Viper.Terminal.Say("Starting in 3...");
Viper.Time.sleep(1000);
Viper.Terminal.Say("2...");
Viper.Time.sleep(1000);
Viper.Terminal.Say("1...");
Viper.Time.sleep(1000);
Viper.Terminal.Say("Go!");

// Animation frame timing
var frameTime = 1000 / 60;  // ~16ms for 60 FPS
loop {
    var frameStart = Viper.Time.millis();

    updateGame();
    renderGame();

    var elapsed = Viper.Time.millis() - frameStart;
    if elapsed < frameTime {
        Viper.Time.sleep(frameTime - elapsed);
    }
}
```

---

### DateTime Type

The `DateTime` type represents a specific moment in time.

#### Properties

```rust
var dt = DateTime.now();

dt.year         // -> i64 (e.g., 2024)
dt.month        // -> i64 (1-12)
dt.day          // -> i64 (1-31)
dt.hour         // -> i64 (0-23)
dt.minute       // -> i64 (0-59)
dt.second       // -> i64 (0-59)
dt.dayOfWeek    // -> i64 (0=Sunday, 1=Monday, ..., 6=Saturday)
dt.dayOfYear    // -> i64 (1-366)
```

**Example:**
```rust
var dt = DateTime.now();

var dayNames = ["Sunday", "Monday", "Tuesday", "Wednesday",
                "Thursday", "Friday", "Saturday"];
Viper.Terminal.Say("Today is " + dayNames[dt.dayOfWeek]);

if dt.month == 12 and dt.day == 25 {
    Viper.Terminal.Say("Merry Christmas!");
}
```

---

#### parse

```rust
func DateTime.parse(str: string) -> DateTime
```

Parses a date/time string in ISO format.

**Example:**
```rust
var birthday = DateTime.parse("1990-05-15");
var meeting = DateTime.parse("2024-03-20T14:30:00");
```

**Supported formats:**
- `"YYYY-MM-DD"` - date only
- `"YYYY-MM-DDTHH:mm:ss"` - full ISO datetime
- `"YYYY-MM-DD HH:mm:ss"` - space separator also works

---

#### format

```rust
func format(pattern: string) -> string
```

Formats a DateTime as a string using a pattern.

**Pattern tokens:**
- `YYYY` - 4-digit year
- `MM` - 2-digit month (01-12)
- `DD` - 2-digit day (01-31)
- `HH` - 2-digit hour (00-23)
- `mm` - 2-digit minute (00-59)
- `ss` - 2-digit second (00-59)

**Example:**
```rust
var dt = DateTime.now();

dt.format("YYYY-MM-DD")           // "2024-03-15"
dt.format("MM/DD/YYYY")           // "03/15/2024"
dt.format("HH:mm")                // "14:30"
dt.format("YYYY-MM-DD HH:mm:ss")  // "2024-03-15 14:30:45"
```

---

#### Arithmetic

```rust
func addDays(n: i64) -> DateTime
func addHours(n: i64) -> DateTime
func addMinutes(n: i64) -> DateTime
func addSeconds(n: i64) -> DateTime
```

Returns a new DateTime with the specified amount added. Use negative values to subtract.

**Example:**
```rust
var now = DateTime.now();
var tomorrow = now.addDays(1);
var lastWeek = now.addDays(-7);
var inTwoHours = now.addHours(2);

// Due date reminder
var dueDate = DateTime.parse("2024-04-01");
var reminderDate = dueDate.addDays(-3);  // 3 days before
```

---

#### diffDays

```rust
func diffDays(other: DateTime) -> f64
```

Returns the difference between two DateTimes in days (can be fractional).

**Example:**
```rust
var start = DateTime.parse("2024-01-01");
var end = DateTime.parse("2024-12-31");
var days = end.diffDays(start);  // 365.0

// Days until deadline
var deadline = DateTime.parse("2024-06-15");
var daysLeft = deadline.diffDays(DateTime.now());
Viper.Terminal.Say("Days remaining: " + Viper.Math.ceil(daysLeft).toInt());
```

---

## Viper.Collections

Data structures beyond basic arrays for organizing and managing data.

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
func countWords(text: string) -> Map<string, i64> {
    var counts = Map<string, i64>.new();

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
    Viper.Terminal.Say(word + ": " + count);
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

Viper.Terminal.Say("Unique visitors: " + visitors.size);  // 2

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
    Viper.Terminal.Say("Processing: " + task);
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

func doAction(action: string) {
    undoStack.push(action);
    Viper.Terminal.Say("Did: " + action);
}

func undo() {
    if !undoStack.isEmpty() {
        var action = undoStack.pop();
        Viper.Terminal.Say("Undoing: " + action);
    } else {
        Viper.Terminal.Say("Nothing to undo");
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
    name: string,
    priority: i64  // Lower number = higher priority
}

func compareTasks(a: Task, b: Task) -> i64 {
    return a.priority - b.priority;
}

var taskQueue = PriorityQueue<Task>.new(compareTasks);

taskQueue.enqueue(Task { name: "Low priority task", priority: 10 });
taskQueue.enqueue(Task { name: "Critical bug fix", priority: 1 });
taskQueue.enqueue(Task { name: "Medium task", priority: 5 });

while !taskQueue.isEmpty() {
    var task = taskQueue.dequeue();
    Viper.Terminal.Say("Processing: " + task.name);
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

Simple HTTP client for web requests.

#### Request Methods

```rust
func Http.get(url: string) -> Response
func Http.post(url: string, options: RequestOptions) -> Response
func Http.put(url: string, options: RequestOptions) -> Response
func Http.delete(url: string) -> Response
```

**RequestOptions fields:**
- `body` - Request body (string or object for JSON)
- `headers` - Map of header names to values

**Response fields:**
- `ok` - `bool` (true if status 200-299)
- `statusCode` - `i64`
- `body` - `string`
- `headers` - `Map<string, string>`

**Example:**
```rust
// Simple GET request
var response = Http.get("https://api.example.com/users");

if response.ok {
    var data = JSON.parse(response.body);
    for user in data.asArray() {
        Viper.Terminal.Say(user["name"].asString());
    }
} else {
    Viper.Terminal.SayError("Request failed: " + response.statusCode);
}

// POST with JSON body
var response = Http.post("https://api.example.com/users", {
    body: { name: "Alice", email: "alice@example.com" },
    headers: { "Content-Type": "application/json" }
});
```

**Edge cases:**
- Network errors throw exceptions
- Timeouts default to 30 seconds
- Redirects are followed automatically (up to 10)

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
Viper.Terminal.Say(response);

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
Viper.Terminal.Say("Server listening on port 8080");

while true {
    var client = server.accept();
    Viper.Terminal.Say("Client connected");

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
Viper.Terminal.Say("Received: " + packet.data + " from " + packet.address);
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
func getStringOr(json: JsonValue, key: string, default: string) -> string {
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
func loadConfig(path: string) -> JsonValue {
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
    Viper.Terminal.Say("Hello from thread!");
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
Viper.Terminal.Say("Sum: " + result);
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

Viper.Terminal.Say("Count: " + counter.get());  // Always 10000
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
        Viper.Time.sleep(100);
    }
    channel.close();
});

// Consumer (main thread)
loop {
    var msg = channel.tryReceive();
    if msg == nil {
        break;
    }
    Viper.Terminal.Say("Received: " + msg);
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
    Viper.Time.sleep(16);  // ~60 FPS
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
zoom = Viper.Math.clamp(zoom, 0.1, 5.0);
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
    if Viper.Math.abs(moveX) < 0.15 { moveX = 0.0; }
    if Viper.Math.abs(moveY) < 0.15 { moveY = 0.0; }

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
Viper.Terminal.Say("Hash: " + hash);
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
Viper.Terminal.Say(encoded);  // "SGVsbG8sIFdvcmxkIQ=="

var decoded = Viper.Crypto.base64Decode(encoded);
var text = String.fromBytes(decoded);  // "Hello, World!"
```

---

## Viper.Environment

System information and environment variables.

---

### Environment Variables

```rust
func Viper.Environment.get(name: string) -> string?
func Viper.Environment.get(name: string, default: string) -> string
func Viper.Environment.set(name: string, value: string) -> void
func Viper.Environment.getAll() -> Map<string, string>
```

**Example:**
```rust
// Get with default
var dbHost = Viper.Environment.get("DATABASE_HOST", "localhost");
var dbPort = Viper.Environment.get("DATABASE_PORT", "5432");

// Check if running in production
var env = Viper.Environment.get("NODE_ENV", "development");
if env == "production" {
    // Enable production settings
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
Viper.Terminal.Say("OS: " + Viper.Environment.os);
Viper.Terminal.Say("Architecture: " + Viper.Environment.arch);
Viper.Terminal.Say("CPU cores: " + Viper.Environment.cpuCount);

// Platform-specific paths
var configPath = Viper.File.join(Viper.Environment.homeDir, ".myapp", "config.json");
```

---

### Process Control

```rust
Viper.Environment.args         // [string] - command line arguments
func Viper.Environment.exit(code: i64) -> void
```

**Example:**
```rust
// Command line argument handling
var args = Viper.Environment.args;

if args.len() < 2 {
    Viper.Terminal.Say("Usage: program <filename>");
    Viper.Environment.exit(1);
}

var filename = args[1];
processFile(filename);
Viper.Environment.exit(0);  // Success
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
    Viper.Terminal.Say("Year: " + match.groups[1]);   // "2024"
    Viper.Terminal.Say("Month: " + match.groups[2]);  // "03"
    Viper.Terminal.Say("Day: " + match.groups[3]);    // "15"
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
Viper.Terminal.Say(result.stdout);

if result.exitCode != 0 {
    Viper.Terminal.SayError("Command failed: " + result.stderr);
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
Viper.Terminal.Say("Result: " + output);  // "4"

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
assertClose(Viper.Math.PI, 3.14159, 0.00001);

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

import Calculator;

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

*[Back to Table of Contents](../README.md) | [Prev: Appendix C](c-pascal-reference.md) | [Next: Appendix E: Error Messages](e-error-messages.md)*
