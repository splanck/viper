# Chapter 9: Files and Persistence

Everything we've built so far disappears when the program ends. Run it again, and it starts fresh — no memory of what happened before.

Real programs need to remember things: your documents, game saves, settings, databases. They read data created by other programs. They produce files that outlive them.

This chapter teaches you to work with the file system — the persistent storage that survives when your program stops. File operations are fundamental to almost every real-world application, yet they're also where many things can go wrong. We'll learn not just how to read and write files, but how to do so safely and correctly.

---

## What IS a File?

Before we dive into code, let's understand what files actually are. This conceptual foundation will help you make better decisions about when and how to use them.

### The Two Types of Computer Memory

Your computer has two fundamentally different types of memory:

**RAM (Random Access Memory)** — This is where your program lives while it's running. When you create a variable like `var name = "Alice"`, that string is stored in RAM. RAM is:
- **Fast** — accessing data takes nanoseconds (billionths of a second)
- **Temporary** — everything in RAM disappears when power is lost
- **Limited** — a typical computer has 8-32 GB of RAM
- **Expensive** — costs roughly $5 per gigabyte

**Storage (Hard Drive or SSD)** — This is where files live. Your documents, photos, programs, and operating system are all stored here. Storage is:
- **Slower** — accessing data takes microseconds to milliseconds (thousands of times slower than RAM)
- **Permanent** — data survives power loss, restarts, even moving the drive to another computer
- **Abundant** — a typical computer has 256 GB to 2+ TB of storage
- **Cheap** — costs roughly $0.05-0.10 per gigabyte

### Files Bridge the Gap

A file is simply a named collection of data stored on permanent storage. When you save a document in a word processor, it writes the document's contents to a file. When you open that document later (even days or years later), it reads the contents back from that file.

Think of it like this:
- RAM is like your desk — you can work with things quickly, but when you leave for the day, you need to put things away
- Storage is like a filing cabinet — slower to access, but things stay there until you deliberately remove them
- Files are like the folders in that cabinet — each has a name, and contains your actual data

### Why We Need Files

Programs need files for several reasons:

**Persistence** — The most obvious reason. When a user creates something (a document, game save, drawing), they expect it to exist tomorrow. Without files, every time you closed Notepad, your text would vanish. Every time you quit a game, you'd have to start over.

**Data sharing** — Files allow different programs to exchange data. You might export data from a spreadsheet as a CSV file, then import it into a database program. You might download a PDF that was created by software you've never heard of.

**Configuration** — Programs need to remember your preferences. Dark mode. Font size. Last opened file. These settings are stored in configuration files so they persist between runs.

**Large data sets** — Sometimes you need to work with more data than fits in RAM. A video editing program doesn't load your entire 50 GB movie into RAM — it reads pieces from the file as needed.

**Logging and debugging** — Programs write log files to record what they did. When something goes wrong, you can examine the log to understand what happened.

---

## Understanding File Paths

Before you can read or write a file, you need to tell the computer which file you mean. This is done through *paths* — addresses that specify where a file lives in the file system.

### The File System Hierarchy

Files are organized in a tree structure of directories (also called folders). At the top is the *root* directory, and everything branches down from there.

On Linux and Mac, the root is simply `/`:

```
/
├── home/
│   └── alice/
│       ├── Documents/
│       │   ├── report.txt
│       │   └── notes.txt
│       └── Pictures/
│           └── vacation.jpg
├── etc/
│   └── config.ini
└── usr/
    └── bin/
        └── viper
```

On Windows, each drive has its own root (C:\, D:\, etc.):

```
C:\
├── Users\
│   └── Alice\
│       ├── Documents\
│       │   ├── report.txt
│       │   └── notes.txt
│       └── Pictures\
│           └── vacation.jpg
└── Program Files\
    └── Viper\
        └── viper.exe
```

### Absolute Paths

An *absolute path* specifies the complete location of a file, starting from the root. It works no matter what directory you're currently "in."

**Linux/Mac examples:**
```rust
"/home/alice/Documents/report.txt"
"/etc/config.ini"
"/usr/bin/viper"
```

**Windows examples:**
```rust
"C:\\Users\\Alice\\Documents\\report.txt"
"C:\\Program Files\\Viper\\viper.exe"
```

Notice Windows uses backslashes (`\`), but since backslash is also the escape character in strings, you need to write `\\` to get a single backslash.

**Good news:** Viper handles this automatically. You can use forward slashes everywhere, and Viper will convert them appropriately for the operating system:

```rust
// This works on ALL operating systems
var path = "C:/Users/Alice/Documents/report.txt";
var content = Viper.File.readText(path);
```

### Relative Paths

A *relative path* specifies a location relative to the "current working directory" — typically the directory where your program is running.

```rust
"data.txt"              // In the current directory
"data/scores.txt"       // In a 'data' subdirectory
"../config.txt"         // In the parent directory
"../shared/data.txt"    // Up one level, then into 'shared'
```

The special names have meanings:
- `.` means "current directory"
- `..` means "parent directory"

If your program is running from `/home/alice/myprogram/`, then:
- `"data.txt"` refers to `/home/alice/myprogram/data.txt`
- `"data/scores.txt"` refers to `/home/alice/myprogram/data/scores.txt`
- `"../config.txt"` refers to `/home/alice/config.txt`

### When to Use Which

**Use relative paths when:**
- Files belong to your program (data files shipped with your app)
- You want your program to be portable (work in different locations)
- You're working with files the user provides in the current location

**Use absolute paths when:**
- You need to access a specific system location
- Working with user-specified paths (which users typically enter as absolute)
- You need certainty about which file you're accessing

**A warning about relative paths:** They depend on the current working directory, which can vary based on how the program was launched. A program started from the command line might have a different working directory than one started by double-clicking an icon.

### Path Manipulation

The `Viper.Path` module helps work with paths safely:

```rust
var path = "/home/alice/documents/report.txt";

Viper.Path.fileName(path);     // "report.txt"
Viper.Path.extension(path);    // ".txt"
Viper.Path.directory(path);    // "/home/alice/documents"
Viper.Path.baseName(path);     // "report" (name without extension)

// Building paths safely
var dir = "/home/alice";
var file = "documents/report.txt";
var full = Viper.Path.join(dir, file);  // "/home/alice/documents/report.txt"
```

Always use `Viper.Path.join` instead of string concatenation:

```rust
// Bad: might produce "/home/alice//documents" or wrong separators
var path = directory + "/" + filename;

// Good: handles edge cases correctly
var path = Viper.Path.join(directory, filename);
```

### Cross-Platform Considerations

If you're writing software that might run on different operating systems, keep these differences in mind:

| Aspect | Windows | Linux/Mac |
|--------|---------|-----------|
| Path separator | `\` (backslash) | `/` (forward slash) |
| Root | `C:\`, `D:\`, etc. | `/` |
| Case sensitivity | Not case-sensitive | Case-sensitive |
| Home directory | `C:\Users\Name` | `/home/name` (Linux) or `/Users/Name` (Mac) |

**Case sensitivity matters!** On Linux, `Report.txt` and `report.txt` are different files. On Windows, they're the same. If you develop on Windows and deploy on Linux (common for web servers), this can cause surprising bugs.

To get standard locations portably:

```rust
var home = Viper.Environment.homeDir();           // User's home directory
var temp = Viper.Environment.tempDir();           // Temporary file directory
var cwd = Viper.Environment.currentDir();         // Current working directory
```

---

## How File Operations Work

When you read or write a file, several things happen behind the scenes. Understanding this helps you write more efficient and correct code.

### Opening a File

Before you can read or write, the file must be *opened*. Opening a file:

1. Tells the operating system you want to access this file
2. Checks that the file exists (for reading) or can be created (for writing)
3. Checks that you have permission to perform the operation
4. Creates a *file handle* — a connection between your program and the file
5. Sets up a buffer for efficient reading/writing

### Reading Data

When you read from a file:

1. Your program requests data from the operating system
2. The OS checks if the data is already in its cache (frequently accessed data is kept in RAM)
3. If not cached, the OS tells the hard drive to retrieve the data
4. The data travels from the drive, through the operating system, into your program's memory
5. Your program processes the data

### Writing Data

When you write to a file:

1. Your program sends data to the operating system
2. The OS typically buffers this data (holds it temporarily in RAM)
3. At some point — when the buffer fills up, when you close the file, or when you explicitly request it — the OS writes the buffered data to the actual storage device
4. The drive physically stores the data

**Important:** Because of buffering, data might not be on disk immediately after you write it. If power is lost before the buffer is flushed, that data can be lost.

### Closing a File

Closing a file is crucial:

1. Flushes any buffered writes to disk
2. Releases the file handle back to the operating system
3. Allows other programs to access the file
4. Frees up system resources

**Always close files when you're done.** Failing to close files can cause:
- Data loss (unflushed buffers)
- Resource exhaustion (too many open files)
- Files being locked (other programs can't access them)
- Memory leaks

---

## File Modes: Read, Write, Append

When you open a file, you specify what you intend to do with it. This is called the file *mode*.

### Read Mode

Read mode opens an existing file for reading. The file must already exist — if it doesn't, you'll get an error.

```rust
var content = Viper.File.readText("data.txt");
```

The file is opened, read from beginning to end, and then closed automatically. Your program receives the contents as a string.

### Write Mode

Write mode creates a new file or overwrites an existing one. This is destructive — if the file exists, its contents are erased and replaced.

```rust
Viper.File.writeText("output.txt", "Hello, World!");
```

**Be careful!** Write mode doesn't ask for confirmation. If `output.txt` contained your life's work, it's now gone forever (unless you have backups).

### Append Mode

Append mode adds data to the end of an existing file, or creates a new file if it doesn't exist. Existing content is preserved.

```rust
Viper.File.appendText("log.txt", "New entry\n");
```

Each call adds to the end. The file grows over time. This is perfect for logs, history files, and accumulating data.

### Choosing the Right Mode

| Scenario | Mode | Function |
|----------|------|----------|
| Reading a configuration file | Read | `readText` |
| Saving user data | Write | `writeText` |
| Adding to a log | Append | `appendText` |
| Replacing a corrupted file | Write | `writeText` |
| Recording each transaction | Append | `appendText` |

---

## Reading Files: Step by Step

Let's explore file reading in detail, from simple to advanced techniques.

### Simple Reading: Load Everything at Once

The simplest approach loads the entire file into memory:

```rust
var content = Viper.File.readText("message.txt");
Viper.Terminal.Say(content);
```

**What happens:**
1. Viper opens `message.txt` for reading
2. Reads all bytes from beginning to end
3. Converts bytes to a string
4. Closes the file
5. Returns the string to your program

**When to use:** Small files (under a few megabytes). This is simple and convenient.

**When to avoid:** Large files. If you load a 4 GB file, your program needs 4+ GB of RAM just for that string. Your computer might slow down or crash.

### Reading Lines into an Array

Often you want to process a file line by line. `readLines` returns an array of strings:

```rust
var lines = Viper.File.readLines("data.txt");

Viper.Terminal.Say("File has " + lines.length + " lines");

for line in lines {
    Viper.Terminal.Say("Line: " + line);
}
```

**What happens:**
1. The entire file is loaded
2. Split at line break characters (`\n` or `\r\n`)
3. Each line becomes an element in the array

This is still loading everything at once, but having lines as separate strings is often more convenient.

### Streaming: One Line at a Time

For large files, read one line at a time:

```rust
var reader = Viper.File.openRead("huge.txt");

while reader.hasMore() {
    var line = reader.readLine();
    // Process this one line
    Viper.Terminal.Say(line);
}

reader.close();  // Don't forget!
```

**What happens:**
1. File is opened but not fully loaded
2. Each `readLine()` fetches just one line into memory
3. You process that line, then it can be garbage collected
4. Only one line is in memory at any time

**When to use:** Files too large to fit in memory. Processing millions of records. When you might stop early (don't need the whole file).

### Reading with a Position

Sometimes you need to jump to a specific location:

```rust
var reader = Viper.File.openRead("data.bin");

// Skip to position 100
reader.seek(100);

// Read 50 bytes from that position
var data = reader.readBytes(50);

reader.close();
```

This is more common with binary files (like reading specific parts of an image or database file).

---

## Writing Files: Step by Step

Writing is where things can go wrong in ways that lose data. Let's understand how to write safely.

### Simple Writing

```rust
var content = "Hello, File!\nThis is line two.\nAnd line three.";
Viper.File.writeText("output.txt", content);
Viper.Terminal.Say("File written!");
```

**What happens:**
1. Viper checks if the file exists (if so, it will be overwritten!)
2. Creates or opens the file for writing
3. Writes all the bytes
4. Closes the file
5. The file now contains exactly what you wrote

The `\n` creates line breaks. Without them, everything would be on one line.

### Appending Data

```rust
Viper.File.appendText("log.txt", "Event 1 occurred\n");
Viper.File.appendText("log.txt", "Event 2 occurred\n");
Viper.File.appendText("log.txt", "Event 3 occurred\n");
```

After these three calls, `log.txt` contains:
```
Event 1 occurred
Event 2 occurred
Event 3 occurred
```

Append is safer than write because you can't accidentally erase data.

### Streaming: Write Incrementally

For large amounts of data, or when building output over time:

```rust
var writer = Viper.File.openWrite("output.txt");

writer.writeLine("Header line");

for i in 0..1000 {
    writer.writeLine("Data line " + i);
}

writer.close();  // CRUCIAL: flushes buffered data to disk
```

**Why streaming matters:**
- You don't need to build a huge string in memory first
- Data is written in chunks as you go
- For very large output, this is the only practical approach

### Ensuring Data is Written

Because of buffering, calling `writeLine` doesn't guarantee the data is on disk. To force data to disk:

```rust
var writer = Viper.File.openWrite("critical.txt");

writer.writeLine("Important data");
writer.flush();  // Force buffered data to disk now

writer.writeLine("More data");
writer.close();  // Also flushes before closing
```

Use `flush()` when you absolutely need data persisted immediately (like before a risky operation). But don't overuse it — flushing after every line is slow.

---

## Text Files vs Binary Files

Not all files are text. Understanding the difference helps you choose the right approach.

### What Are Text Files?

Text files contain human-readable characters. When you open them in Notepad, you see... text.

Examples: `.txt`, `.csv`, `.json`, `.html`, `.py`, `.md`

A text file containing "Hello" looks like this in bytes:
```
72  101  108  108  111
H   e    l    l    o
```

Each byte represents a character code (usually ASCII or UTF-8).

**Line endings matter:** Different operating systems use different characters:
- Linux/Mac: `\n` (line feed, byte 10)
- Windows: `\r\n` (carriage return + line feed, bytes 13 and 10)

Viper handles this automatically when you use `readLines` and `writeLine`.

### What Are Binary Files?

Binary files contain raw bytes that aren't meant to be read as text. They might be images, audio, compiled programs, or custom data formats.

Examples: `.jpg`, `.png`, `.mp3`, `.exe`, `.zip`, `.dat`

If you open a binary file in Notepad, you see gibberish — because the bytes aren't character codes, they're data in some other format.

A simple example — storing numbers efficiently:

```rust
// Store the number 1000000 as text: needs 7 bytes ("1000000")
// Store the number 1000000 as binary: needs 4 bytes (the actual bits)

// Write binary data
var data: [byte] = [0x40, 0x42, 0x0F, 0x00];  // 1000000 in little-endian
Viper.File.writeBytes("number.bin", data);

// Read binary data
var bytes = Viper.File.readBytes("number.bin");
```

### When to Use Each

**Use text files when:**
- Humans need to read/edit the file (configuration, logs, source code)
- You need to work with many different programs (CSV, JSON, XML)
- Exact byte-level precision isn't critical
- Files are relatively small

**Use binary files when:**
- Storing non-text data (images, audio, video)
- Efficiency matters (binary is more compact)
- You need exact byte-level control
- Working with existing binary formats (ZIP files, databases)

### Working with Binary Files

```rust
// Writing binary data
var data: [byte] = [0x89, 0x50, 0x4E, 0x47];  // PNG header bytes
Viper.File.writeBytes("header.bin", data);

// Reading binary data
var bytes = Viper.File.readBytes("image.png");
Viper.Terminal.Say("File size: " + bytes.length + " bytes");

// Check for PNG signature
if bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47 {
    Viper.Terminal.Say("This is a PNG file!");
}
```

Binary file handling is more complex and usually requires understanding the specific file format. We'll use binary files more in later chapters when working with graphics and game data.

---

## Checking If Files and Directories Exist

Before reading, you might want to check if a file exists:

```rust
if Viper.File.exists("config.txt") {
    var config = Viper.File.readText("config.txt");
    Viper.Terminal.Say("Loaded config");
} else {
    Viper.Terminal.Say("No config file found, using defaults");
}
```

### The Race Condition Problem

There's a subtle issue with check-then-read:

```rust
if Viper.File.exists("data.txt") {
    // Another program could delete the file RIGHT HERE
    var content = Viper.File.readText("data.txt");  // Might fail!
}
```

Between checking and reading, another program (or user) could delete the file. This is called a *race condition*.

For critical code, consider using try-catch instead:

```rust
try {
    var content = Viper.File.readText("data.txt");
    // Process content...
} catch e: FileNotFound {
    Viper.Terminal.Say("File not found, using defaults");
}
```

This is atomic — there's no gap where the file could disappear.

For most programs, the check-then-read approach is fine. Race conditions mainly matter for server software or when multiple programs access the same files.

---

## Working with Directories

Files are organized in directories. Viper provides tools to work with them.

### Checking and Creating Directories

```rust
// Check if directory exists
if Viper.Dir.exists("saves") {
    Viper.Terminal.Say("Saves directory found");
}

// Create a directory
Viper.Dir.create("output");

// Create nested directories (creates parent directories as needed)
Viper.Dir.createAll("output/data/processed");
```

### Listing Directory Contents

```rust
// List all files in a directory
var files = Viper.Dir.listFiles("data");
for file in files {
    Viper.Terminal.Say(file);
}

// List subdirectories
var dirs = Viper.Dir.listDirs("projects");
for dir in dirs {
    Viper.Terminal.Say("Directory: " + dir);
}

// List everything (files and directories)
var all = Viper.Dir.list("documents");
```

### Practical Example: Finding All Text Files

```rust
func findTextFiles(directory: string) {
    var entries = Viper.Dir.list(directory);

    for entry in entries {
        var path = Viper.Path.join(directory, entry);

        if Viper.Dir.exists(path) {
            // It's a directory, recurse into it
            findTextFiles(path);
        } else if entry.endsWith(".txt") {
            Viper.Terminal.Say("Found: " + path);
        }
    }
}

findTextFiles("documents");
```

---

## Error Handling for File Operations

Files are a major source of errors. The file might not exist. You might not have permission. The disk might be full. The path might be invalid.

### Common File Errors

**File not found:**
```rust
// The file doesn't exist
var content = Viper.File.readText("nonexistent.txt");
// Error: File not found: nonexistent.txt
```

**Permission denied:**
```rust
// You don't have permission to read/write this file
var content = Viper.File.readText("/etc/shadow");
// Error: Permission denied: /etc/shadow
```

**Disk full:**
```rust
// No space left on the storage device
Viper.File.writeText("huge.txt", massiveData);
// Error: No space left on device
```

**Path is a directory:**
```rust
// Trying to read a directory as a file
var content = Viper.File.readText("my_folder");
// Error: Is a directory: my_folder
```

**Invalid path:**
```rust
// Path contains invalid characters or is malformed
var content = Viper.File.readText("file\0name.txt");
// Error: Invalid path
```

### Handling Errors Gracefully

Never let file errors crash your program unexpectedly. Handle them:

```rust
func loadConfig() -> string {
    try {
        return Viper.File.readText("config.txt");
    } catch e: FileNotFound {
        Viper.Terminal.Say("Config file not found, creating default...");
        var defaultConfig = "theme=light\nvolume=50";
        Viper.File.writeText("config.txt", defaultConfig);
        return defaultConfig;
    } catch e: PermissionDenied {
        Viper.Terminal.Say("Cannot read config file: permission denied");
        Viper.Terminal.Say("Using built-in defaults");
        return "theme=light\nvolume=50";
    } catch e {
        Viper.Terminal.Say("Unexpected error reading config: " + e.message);
        return "theme=light\nvolume=50";
    }
}
```

### User-Friendly Error Messages

Don't show raw error messages to users. Translate them:

```rust
func openUserFile(path: string) -> string {
    try {
        return Viper.File.readText(path);
    } catch e: FileNotFound {
        Viper.Terminal.Say("Sorry, the file '" + path + "' doesn't exist.");
        Viper.Terminal.Say("Please check the filename and try again.");
        return "";
    } catch e: PermissionDenied {
        Viper.Terminal.Say("Sorry, you don't have permission to open this file.");
        Viper.Terminal.Say("Try running the program as administrator, or choose a different file.");
        return "";
    } catch e {
        Viper.Terminal.Say("Sorry, couldn't open the file.");
        Viper.Terminal.Say("Technical details: " + e.message);
        return "";
    }
}
```

---

## Safe File Writing Practices

Writing files is risky. If something goes wrong mid-write, you might end up with a corrupted file — half old data, half new data, completely unusable.

### The Danger of Direct Overwriting

```rust
// Dangerous: if this fails mid-write, data.txt is corrupted
Viper.File.writeText("data.txt", newData);
```

If power fails, or your program crashes, or the disk has an error during the write, `data.txt` might be left in a broken state.

### Safe Writing with Temporary Files

The professional approach:

```rust
func safeWrite(filename: string, content: string) {
    var tempFile = filename + ".tmp";

    // Step 1: Write to a temporary file
    Viper.File.writeText(tempFile, content);

    // Step 2: Delete the old file (if it exists)
    if Viper.File.exists(filename) {
        Viper.File.delete(filename);
    }

    // Step 3: Rename temp file to real name
    Viper.File.rename(tempFile, filename);
}
```

Why is this safer?
- If step 1 fails, the original file is untouched
- Rename (step 3) is usually atomic on most systems — it either happens completely or not at all
- You never have a half-written file

### Backup Before Modifying

For critical data, keep a backup:

```rust
func writeWithBackup(filename: string, content: string) {
    // Create backup of existing file
    if Viper.File.exists(filename) {
        var backupName = filename + ".backup";
        Viper.File.copy(filename, backupName);
    }

    // Now write the new content
    Viper.File.writeText(filename, content);
}
```

If something goes wrong, the user still has their `.backup` file.

### Avoiding Accidental Overwrites

**Confirm before overwriting:**
```rust
func saveFile(filename: string, content: string) {
    if Viper.File.exists(filename) {
        Viper.Terminal.Print("File exists. Overwrite? (y/n): ");
        var response = Viper.Terminal.ReadLine().trim().lower();
        if response != "y" {
            Viper.Terminal.Say("Save cancelled.");
            return;
        }
    }
    Viper.File.writeText(filename, content);
    Viper.Terminal.Say("File saved.");
}
```

**Generate unique names:**
```rust
func uniqueFilename(base: string, ext: string) -> string {
    var filename = base + ext;
    var counter = 1;

    while Viper.File.exists(filename) {
        filename = base + "_" + counter + ext;
        counter += 1;
    }

    return filename;
}

// "report.txt", "report_1.txt", "report_2.txt", ...
var name = uniqueFilename("report", ".txt");
```

---

## Practical File Patterns

Let's look at real-world examples of how files are used.

### Configuration Files

Programs often store settings in configuration files:

```rust
module Config;

final CONFIG_FILE = "settings.cfg";

// Simple key=value format
func loadConfig() -> Map<string, string> {
    var config = Map<string, string>();

    if !Viper.File.exists(CONFIG_FILE) {
        return config;  // Empty config if file doesn't exist
    }

    var lines = Viper.File.readLines(CONFIG_FILE);

    for line in lines {
        line = line.trim();

        // Skip empty lines and comments
        if line.length == 0 || line.startsWith("#") {
            continue;
        }

        var parts = line.split("=");
        if parts.length == 2 {
            var key = parts[0].trim();
            var value = parts[1].trim();
            config[key] = value;
        }
    }

    return config;
}

func saveConfig(config: Map<string, string>) {
    var lines: [string] = [];
    lines.push("# Application settings");
    lines.push("# Edit with care!");
    lines.push("");

    for key in config.keys() {
        lines.push(key + "=" + config[key]);
    }

    var content = lines.join("\n");
    Viper.File.writeText(CONFIG_FILE, content);
}

// Usage
func start() {
    var config = loadConfig();

    // Get setting with default
    var theme = config.getOrDefault("theme", "light");
    var volume = Viper.Parse.Int(config.getOrDefault("volume", "50"));

    Viper.Terminal.Say("Theme: " + theme);
    Viper.Terminal.Say("Volume: " + volume);

    // Change a setting
    config["theme"] = "dark";
    saveConfig(config);
}
```

Example `settings.cfg`:
```
# Application settings
# Edit with care!

theme=dark
volume=75
lastFile=/home/alice/documents/report.txt
```

### Log Files

Recording events for debugging and auditing:

```rust
module Logger;

final LOG_FILE = "application.log";

func log(level: string, message: string) {
    var timestamp = Viper.Time.now().format("YYYY-MM-DD HH:mm:ss");
    var entry = "[" + timestamp + "] [" + level + "] " + message + "\n";
    Viper.File.appendText(LOG_FILE, entry);
}

func info(message: string) {
    log("INFO", message);
}

func warning(message: string) {
    log("WARNING", message);
}

func error(message: string) {
    log("ERROR", message);
}

// Usage
func processData() {
    Logger.info("Starting data processing");

    try {
        // ... do work ...
        Logger.info("Processed 1000 records");
    } catch e {
        Logger.error("Processing failed: " + e.message);
    }
}
```

Example log output:
```
[2024-03-15 10:23:45] [INFO] Application started
[2024-03-15 10:23:45] [INFO] Loading configuration
[2024-03-15 10:23:46] [INFO] Starting data processing
[2024-03-15 10:23:47] [INFO] Processed 1000 records
[2024-03-15 10:25:12] [WARNING] Disk space low
[2024-03-15 10:30:00] [ERROR] Connection timeout
```

### Save Games

Games need to persist player progress:

```rust
module GameSave;

final SAVE_DIR = "saves";

struct SaveData {
    playerName: string,
    level: i64,
    score: i64,
    health: i64,
    inventory: [string],
    position: (f64, f64)
}

func save(data: SaveData, slot: i64) {
    // Ensure save directory exists
    if !Viper.Dir.exists(SAVE_DIR) {
        Viper.Dir.create(SAVE_DIR);
    }

    var filename = Viper.Path.join(SAVE_DIR, "save_" + slot + ".dat");

    var lines: [string] = [];
    lines.push("name=" + data.playerName);
    lines.push("level=" + data.level);
    lines.push("score=" + data.score);
    lines.push("health=" + data.health);
    lines.push("inventory=" + data.inventory.join(","));
    lines.push("position=" + data.position.0 + "," + data.position.1);

    Viper.File.writeText(filename, lines.join("\n"));
    Viper.Terminal.Say("Game saved to slot " + slot);
}

func load(slot: i64) -> SaveData? {
    var filename = Viper.Path.join(SAVE_DIR, "save_" + slot + ".dat");

    if !Viper.File.exists(filename) {
        Viper.Terminal.Say("No save found in slot " + slot);
        return null;
    }

    var lines = Viper.File.readLines(filename);
    var data = SaveData();

    for line in lines {
        var parts = line.split("=");
        if parts.length != 2 { continue; }

        var key = parts[0];
        var value = parts[1];

        if key == "name" { data.playerName = value; }
        else if key == "level" { data.level = Viper.Parse.Int(value); }
        else if key == "score" { data.score = Viper.Parse.Int(value); }
        else if key == "health" { data.health = Viper.Parse.Int(value); }
        else if key == "inventory" { data.inventory = value.split(","); }
        else if key == "position" {
            var coords = value.split(",");
            data.position = (Viper.Parse.Float(coords[0]), Viper.Parse.Float(coords[1]));
        }
    }

    Viper.Terminal.Say("Game loaded from slot " + slot);
    return data;
}

func listSaves() -> [i64] {
    var saves: [i64] = [];

    if !Viper.Dir.exists(SAVE_DIR) {
        return saves;
    }

    var files = Viper.Dir.listFiles(SAVE_DIR);
    for file in files {
        if file.startsWith("save_") && file.endsWith(".dat") {
            var numStr = file.substring(5, file.length - 9);
            saves.push(Viper.Parse.Int(numStr));
        }
    }

    return saves;
}
```

### Data Export (CSV)

Exporting data for spreadsheets or other programs:

```rust
func exportToCSV(filename: string, headers: [string], rows: [[string]]) {
    var lines: [string] = [];

    // Header row
    lines.push(headers.join(","));

    // Data rows
    for row in rows {
        // Escape commas and quotes in values
        var escapedRow: [string] = [];
        for value in row {
            if value.contains(",") || value.contains("\"") {
                value = "\"" + value.replace("\"", "\"\"") + "\"";
            }
            escapedRow.push(value);
        }
        lines.push(escapedRow.join(","));
    }

    Viper.File.writeText(filename, lines.join("\n"));
    Viper.Terminal.Say("Exported " + rows.length + " rows to " + filename);
}

// Usage
var headers = ["Name", "Age", "City"];
var data = [
    ["Alice", "25", "New York"],
    ["Bob", "30", "Los Angeles"],
    ["Charlie", "35", "Chicago"]
];

exportToCSV("people.csv", headers, data);
```

### Data Import (CSV)

Reading data from CSV files:

```rust
func importFromCSV(filename: string) -> [[string]] {
    var rows: [[string]] = [];
    var lines = Viper.File.readLines(filename);

    for line in lines {
        if line.trim().length == 0 { continue; }

        // Simple split (doesn't handle quoted commas)
        var values = line.split(",");
        rows.push(values);
    }

    return rows;
}

// Usage
var data = importFromCSV("people.csv");
var headers = data[0];

Viper.Terminal.Say("Columns: " + headers.join(", "));
Viper.Terminal.Say("Data rows: " + (data.length - 1));
```

---

## A Complete Example: Note Keeper

Let's build a full application that demonstrates proper file handling:

```rust
module NoteKeeper;

final NOTES_FILE = "notes.txt";
final BACKUP_FILE = "notes.txt.backup";

// Load notes from file
func loadNotes() -> [string] {
    if !Viper.File.exists(NOTES_FILE) {
        return [];
    }

    try {
        var lines = Viper.File.readLines(NOTES_FILE);
        // Filter out empty lines
        var notes: [string] = [];
        for line in lines {
            if line.trim().length > 0 {
                notes.push(line);
            }
        }
        return notes;
    } catch e {
        Viper.Terminal.Say("Warning: Could not load notes: " + e.message);

        // Try backup
        if Viper.File.exists(BACKUP_FILE) {
            Viper.Terminal.Say("Attempting to load from backup...");
            try {
                return Viper.File.readLines(BACKUP_FILE);
            } catch e2 {
                Viper.Terminal.Say("Backup also failed.");
            }
        }

        return [];
    }
}

// Save notes to file (with backup)
func saveNotes(notes: [string]) -> bool {
    // Create backup of existing file
    if Viper.File.exists(NOTES_FILE) {
        try {
            Viper.File.copy(NOTES_FILE, BACKUP_FILE);
        } catch e {
            Viper.Terminal.Say("Warning: Could not create backup");
        }
    }

    // Write to temporary file first
    var tempFile = NOTES_FILE + ".tmp";
    try {
        var content = notes.join("\n");
        Viper.File.writeText(tempFile, content);

        // Delete old file and rename temp to real
        if Viper.File.exists(NOTES_FILE) {
            Viper.File.delete(NOTES_FILE);
        }
        Viper.File.rename(tempFile, NOTES_FILE);

        return true;
    } catch e {
        Viper.Terminal.Say("Error saving notes: " + e.message);

        // Clean up temp file if it exists
        if Viper.File.exists(tempFile) {
            Viper.File.delete(tempFile);
        }

        return false;
    }
}

// Display all notes
func displayNotes(notes: [string]) {
    if notes.length == 0 {
        Viper.Terminal.Say("No notes yet.");
        Viper.Terminal.Say("Use 'add' to create your first note!");
        return;
    }

    Viper.Terminal.Say("");
    Viper.Terminal.Say("=== Your Notes ===");
    for i in 0..notes.length {
        Viper.Terminal.Say((i + 1) + ". " + notes[i]);
    }
    Viper.Terminal.Say("==================");
}

// Show help
func showHelp() {
    Viper.Terminal.Say("");
    Viper.Terminal.Say("Available commands:");
    Viper.Terminal.Say("  list   - Show all notes");
    Viper.Terminal.Say("  add    - Add a new note");
    Viper.Terminal.Say("  delete - Delete a note by number");
    Viper.Terminal.Say("  edit   - Edit an existing note");
    Viper.Terminal.Say("  clear  - Delete all notes");
    Viper.Terminal.Say("  help   - Show this help");
    Viper.Terminal.Say("  quit   - Exit the program");
    Viper.Terminal.Say("");
}

func start() {
    var notes = loadNotes();

    Viper.Terminal.Say("===================");
    Viper.Terminal.Say("   Note Keeper");
    Viper.Terminal.Say("===================");
    Viper.Terminal.Say("Type 'help' for commands");
    Viper.Terminal.Say("");

    if notes.length > 0 {
        Viper.Terminal.Say("Loaded " + notes.length + " existing notes.");
    }

    while true {
        Viper.Terminal.Print("> ");
        var command = Viper.Terminal.ReadLine().trim().lower();

        if command == "quit" || command == "exit" {
            Viper.Terminal.Say("Goodbye!");
            break;

        } else if command == "help" {
            showHelp();

        } else if command == "list" {
            displayNotes(notes);

        } else if command == "add" {
            Viper.Terminal.Print("Enter note: ");
            var note = Viper.Terminal.ReadLine().trim();

            if note.length == 0 {
                Viper.Terminal.Say("Note cannot be empty.");
            } else {
                notes.push(note);
                if saveNotes(notes) {
                    Viper.Terminal.Say("Note added!");
                }
            }

        } else if command == "delete" {
            if notes.length == 0 {
                Viper.Terminal.Say("No notes to delete.");
            } else {
                displayNotes(notes);
                Viper.Terminal.Print("Delete which number? (0 to cancel): ");
                var input = Viper.Terminal.ReadLine().trim();

                try {
                    var num = Viper.Parse.Int(input);
                    if num == 0 {
                        Viper.Terminal.Say("Cancelled.");
                    } else if num >= 1 && num <= notes.length {
                        var deleted = notes[num - 1];
                        notes.removeAt(num - 1);
                        if saveNotes(notes) {
                            Viper.Terminal.Say("Deleted: " + deleted);
                        }
                    } else {
                        Viper.Terminal.Say("Invalid number. Enter 1-" + notes.length);
                    }
                } catch e {
                    Viper.Terminal.Say("Please enter a number.");
                }
            }

        } else if command == "edit" {
            if notes.length == 0 {
                Viper.Terminal.Say("No notes to edit.");
            } else {
                displayNotes(notes);
                Viper.Terminal.Print("Edit which number? (0 to cancel): ");
                var input = Viper.Terminal.ReadLine().trim();

                try {
                    var num = Viper.Parse.Int(input);
                    if num == 0 {
                        Viper.Terminal.Say("Cancelled.");
                    } else if num >= 1 && num <= notes.length {
                        Viper.Terminal.Say("Current: " + notes[num - 1]);
                        Viper.Terminal.Print("New text: ");
                        var newNote = Viper.Terminal.ReadLine().trim();

                        if newNote.length == 0 {
                            Viper.Terminal.Say("Note cannot be empty. Use 'delete' to remove.");
                        } else {
                            notes[num - 1] = newNote;
                            if saveNotes(notes) {
                                Viper.Terminal.Say("Note updated!");
                            }
                        }
                    } else {
                        Viper.Terminal.Say("Invalid number. Enter 1-" + notes.length);
                    }
                } catch e {
                    Viper.Terminal.Say("Please enter a number.");
                }
            }

        } else if command == "clear" {
            if notes.length == 0 {
                Viper.Terminal.Say("Already empty.");
            } else {
                Viper.Terminal.Print("Delete ALL " + notes.length + " notes? (yes/no): ");
                var confirm = Viper.Terminal.ReadLine().trim().lower();

                if confirm == "yes" {
                    notes = [];
                    if saveNotes(notes) {
                        Viper.Terminal.Say("All notes deleted.");
                    }
                } else {
                    Viper.Terminal.Say("Cancelled.");
                }
            }

        } else if command.length == 0 {
            // Empty input, just show prompt again
            continue;

        } else {
            Viper.Terminal.Say("Unknown command: '" + command + "'");
            Viper.Terminal.Say("Type 'help' for available commands.");
        }
    }
}
```

This program demonstrates:
- Loading data from files with error handling
- Saving with backups for safety
- Using temporary files to prevent corruption
- User-friendly error messages
- Input validation
- Confirmation for destructive operations

---

## The Three Languages

**Zia**
```rust
// Read
var content = Viper.File.readText("file.txt");

// Write
Viper.File.writeText("file.txt", "Hello!");

// Append
Viper.File.appendText("file.txt", "More text\n");

// Check existence
if Viper.File.exists("file.txt") { ... }

// Read lines
var lines = Viper.File.readLines("file.txt");

// Stream reading
var reader = Viper.File.openRead("file.txt");
while reader.hasMore() {
    var line = reader.readLine();
}
reader.close();
```

**BASIC**
```basic
' Read
OPEN "file.txt" FOR INPUT AS #1
DIM content AS STRING
LINE INPUT #1, content
CLOSE #1

' Write
OPEN "file.txt" FOR OUTPUT AS #1
PRINT #1, "Hello!"
CLOSE #1

' Append
OPEN "file.txt" FOR APPEND AS #1
PRINT #1, "More text"
CLOSE #1

' Check existence (varies by BASIC dialect)
IF DIR$("file.txt") <> "" THEN
    PRINT "File exists"
END IF
```

BASIC uses file numbers (#1, #2, etc.) and requires explicit OPEN/CLOSE. The FOR clause specifies the mode: INPUT (read), OUTPUT (write), APPEND (append).

**Pascal**
```pascal
var
    f: TextFile;
    line: string;
begin
    { Read }
    AssignFile(f, 'file.txt');
    Reset(f);
    ReadLn(f, line);
    CloseFile(f);

    { Write }
    AssignFile(f, 'file.txt');
    Rewrite(f);
    WriteLn(f, 'Hello!');
    CloseFile(f);

    { Append }
    AssignFile(f, 'file.txt');
    Append(f);
    WriteLn(f, 'More text');
    CloseFile(f);

    { Check existence }
    if FileExists('file.txt') then
        WriteLn('File exists');
end.
```

Pascal uses file variables and procedures like AssignFile, Reset (open for reading), Rewrite (open for writing), and Append.

---

## Common Mistakes

**Forgetting the file might not exist:**
```rust
// Crashes if file doesn't exist
var content = Viper.File.readText("maybe.txt");

// Better: check first
if Viper.File.exists("maybe.txt") {
    var content = Viper.File.readText("maybe.txt");
}

// Or: use try-catch
try {
    var content = Viper.File.readText("maybe.txt");
} catch e: FileNotFound {
    Viper.Terminal.Say("File not found");
}
```

**Overwriting when you meant to append:**
```rust
Viper.File.writeText("log.txt", "Entry 1\n");
Viper.File.writeText("log.txt", "Entry 2\n");  // Oops! Entry 1 is gone

// Correct: use append for logs
Viper.File.writeText("log.txt", "Entry 1\n");
Viper.File.appendText("log.txt", "Entry 2\n");  // Both entries preserved
```

**Hardcoding paths:**
```rust
// Bad: only works on your machine
var file = "C:\\Users\\Alice\\Documents\\data.txt";

// Better: use relative paths
var file = "data/scores.txt";

// Or build paths dynamically
var home = Viper.Environment.homeDir();
var file = Viper.Path.join(home, "Documents", "data.txt");
```

**Not closing files:**
```rust
var reader = Viper.File.openRead("file.txt");
// ... use reader ...
// Forgot reader.close()!  File stays locked, resources leak

// Always close, even if errors occur:
var reader = Viper.File.openRead("file.txt");
try {
    // ... use reader ...
} finally {
    reader.close();
}
```

**Assuming writes are immediate:**
```rust
var writer = Viper.File.openWrite("important.txt");
writer.writeLine("Critical data");
// Power fails here - data might be lost!

// Better: flush critical data
var writer = Viper.File.openWrite("important.txt");
writer.writeLine("Critical data");
writer.flush();  // Force to disk
```

**Not handling paths cross-platform:**
```rust
// Bad: backslash doesn't work on Mac/Linux
var path = "data\\files\\scores.txt";

// Good: forward slash works everywhere (Viper converts as needed)
var path = "data/files/scores.txt";

// Best: use Path.join
var path = Viper.Path.join("data", "files", "scores.txt");
```

---

## Summary

- **Files provide persistence** — data that survives program termination
- **RAM is fast but temporary; storage is slow but permanent**
- **Paths specify file locations** — absolute (full path) or relative (from current directory)
- **File modes determine behavior** — read, write (overwrite), append (add to end)
- **Always close files** when using stream APIs to prevent resource leaks
- **Handle errors gracefully** — files can fail in many ways
- **Use temporary files and backups** for safe writing
- **Text files are human-readable; binary files store raw data**
- `Viper.File.readText/writeText/appendText` for simple operations
- `Viper.File.openRead/openWrite` for streaming large files
- `Viper.Dir` for working with directories
- `Viper.Path` for manipulating paths safely and portably

---

## Exercises

**Exercise 9.1**: Write a program that saves your name to a file, then reads it back and greets you. If the file exists, it should greet you; if not, it should ask for your name and save it.

**Exercise 9.2**: Write a program that reads a file and reports statistics: number of lines, number of words, number of characters, and the longest line.

**Exercise 9.3**: Write a program that copies one file to another. It should ask for the source filename, check that it exists, ask for the destination filename, warn if it already exists, and then perform the copy.

**Exercise 9.4**: Extend the Note Keeper to save timestamps with each note (e.g., "[2024-03-15 10:30] Buy groceries").

**Exercise 9.5**: Write a program that lists all `.txt` files in the current directory and its subdirectories, showing the file path and size for each.

**Exercise 9.6**: Write a simple "find and replace" program that reads a file, replaces all occurrences of one string with another, and writes the result. It should create a backup of the original file first.

**Exercise 9.7** (Challenge): Write a simple address book that stores names, phone numbers, and emails in a file. Include commands to add, search (by any field), list, edit, and delete contacts. Handle all file errors gracefully.

**Exercise 9.8** (Challenge): Write a program that can merge two sorted files. Each input file contains one number per line in ascending order. The output file should contain all numbers from both files, still in ascending order. Do this efficiently without loading both entire files into memory.

---

*We can now save and load data. But what happens when things go wrong — the file doesn't exist, the network fails, the input is invalid? Chapter 10 dives deeper into handling errors gracefully, building on the error handling we've introduced here.*

*[Continue to Chapter 10: Errors and Recovery](10-errors.md)*
