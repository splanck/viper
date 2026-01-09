# Chapter 9: Files and Persistence

Everything we've built so far disappears when the program ends. Run it again, and it starts fresh — no memory of what happened before.

Real programs need to remember things: your documents, game saves, settings, databases. They read data created by other programs. They produce files that outlive them.

This chapter teaches you to work with the file system — the persistent storage that survives when your program stops.

---

## Why Files?

When a program runs, it works with data in *memory* — fast, temporary, gone when power is lost. Files live on *storage* (hard drives, SSDs) — slower, but permanent.

Files let you:
- Save data for later (game progress, documents, logs)
- Share data between programs (export to CSV, import from JSON)
- Process large amounts of data (too much to fit in memory)
- Keep configuration (user preferences, settings)

---

## Reading a File

The simplest way to read a file is to load it all at once:

```rust
var content = Viper.File.readText("message.txt");
Viper.Terminal.Say(content);
```

This reads the entire file into a string. For small files (under a few megabytes), this is fine.

If the file doesn't exist, you'll get an error. We'll learn to handle that in Chapter 10.

---

## Writing a File

To save text to a file:

```rust
var content = "Hello, File!\nThis is line two.";
Viper.File.writeText("output.txt", content);
Viper.Terminal.Say("File written!");
```

This creates `output.txt` (or overwrites it if it exists) with your content.

The `\n` creates line breaks. Without them, everything would be on one line.

---

## Appending to a File

Sometimes you want to add to a file without erasing what's already there:

```rust
Viper.File.appendText("log.txt", "Event occurred at 10:00\n");
```

Each call adds to the end of the file. This is perfect for logs.

---

## Checking If a File Exists

Before reading, you might want to check:

```rust
if Viper.File.exists("config.txt") {
    var config = Viper.File.readText("config.txt");
    Viper.Terminal.Say("Loaded config");
} else {
    Viper.Terminal.Say("No config file found, using defaults");
}
```

---

## Reading Line by Line

For larger files, reading everything at once might use too much memory. Reading line by line is more efficient:

```rust
var lines = Viper.File.readLines("data.txt");

for line in lines {
    Viper.Terminal.Say("Line: " + line);
}
```

`readLines` returns an array of strings, one per line.

You can also process lines one at a time (more memory-efficient for huge files):

```rust
var reader = Viper.File.openRead("huge.txt");

while reader.hasMore() {
    var line = reader.readLine();
    // Process line...
}

reader.close();
```

Always close files when done. This frees up system resources.

---

## Working with Paths

Paths describe where files live. They can be:

**Relative** — relative to the current directory:
```rust
"data.txt"           // In current directory
"data/scores.txt"    // In 'data' subdirectory
"../config.txt"      // In parent directory
```

**Absolute** — full path from the root:
```rust
"/home/alice/documents/report.txt"  // Linux/Mac
"C:\\Users\\Alice\\Documents\\report.txt"  // Windows
```

Use forward slashes even on Windows — Viper handles the conversion.

---

## Directories

Files are organized in directories (folders). You can work with them too:

```rust
// Check if directory exists
if Viper.Dir.exists("saves") {
    ...
}

// Create a directory
Viper.Dir.create("output");

// List files in a directory
var files = Viper.Dir.listFiles("data");
for file in files {
    Viper.Terminal.Say(file);
}
```

---

## Path Manipulation

The `Viper.Path` module helps work with file paths:

```rust
var path = "/home/alice/documents/report.txt";

Viper.Path.fileName(path);     // "report.txt"
Viper.Path.extension(path);    // ".txt"
Viper.Path.directory(path);    // "/home/alice/documents"
Viper.Path.join("data", "scores.txt");  // "data/scores.txt"
```

This is better than string manipulation — it handles different operating systems correctly.

---

## A Complete Example: Note Keeper

Let's build a simple notes application:

```rust
module NoteKeeper;

final NOTES_FILE = "notes.txt";

func loadNotes() -> [string] {
    if !Viper.File.exists(NOTES_FILE) {
        return [];
    }
    return Viper.File.readLines(NOTES_FILE);
}

func saveNotes(notes: [string]) {
    var content = notes.join("\n");
    Viper.File.writeText(NOTES_FILE, content);
}

func displayNotes(notes: [string]) {
    if notes.length == 0 {
        Viper.Terminal.Say("No notes yet.");
        return;
    }

    Viper.Terminal.Say("=== Your Notes ===");
    for i in 0..notes.length {
        Viper.Terminal.Say((i + 1) + ". " + notes[i]);
    }
}

func start() {
    var notes = loadNotes();

    Viper.Terminal.Say("Note Keeper");
    Viper.Terminal.Say("Commands: add, list, delete, quit");
    Viper.Terminal.Say("");

    while true {
        Viper.Terminal.Print("> ");
        var command = Viper.Terminal.ReadLine().trim().lower();

        if command == "quit" {
            Viper.Terminal.Say("Goodbye!");
            break;
        } else if command == "list" {
            displayNotes(notes);
        } else if command == "add" {
            Viper.Terminal.Print("Enter note: ");
            var note = Viper.Terminal.ReadLine();
            notes.push(note);
            saveNotes(notes);
            Viper.Terminal.Say("Note added!");
        } else if command == "delete" {
            displayNotes(notes);
            Viper.Terminal.Print("Delete which number? ");
            var num = Viper.Parse.Int(Viper.Terminal.ReadLine());
            if num >= 1 && num <= notes.length {
                notes.removeAt(num - 1);
                saveNotes(notes);
                Viper.Terminal.Say("Note deleted!");
            } else {
                Viper.Terminal.Say("Invalid number.");
            }
        } else {
            Viper.Terminal.Say("Unknown command. Try: add, list, delete, quit");
        }
    }
}
```

This program:
- Loads existing notes from a file when it starts
- Saves notes after each change
- Persists between runs — quit and restart, your notes are still there

---

## Binary Files

Text files are human-readable. Binary files store raw data — more compact, but not readable by humans.

```rust
// Write binary data
var data: [byte] = [72, 101, 108, 108, 111];  // ASCII for "Hello";
Viper.File.writeBytes("data.bin", data);

// Read binary data
var bytes = Viper.File.readBytes("data.bin");
for b in bytes {
    Viper.Terminal.Say(b);
}
```

Binary files are used for images, audio, compiled programs, and efficient data storage. We'll use them more when working with graphics and games.

---

## Common File Patterns

### Configuration file
```rust
func loadConfig() -> string {
    if Viper.File.exists("config.ini") {
        return Viper.File.readText("config.ini");
    }
    // Return defaults
    return "volume=50\ndifficulty=normal";
}
```

### Log file
```rust
func log(message: string) {
    var timestamp = Viper.Time.now().toString();
    var entry = "[" + timestamp + "] " + message + "\n";
    Viper.File.appendText("app.log", entry);
}
```

### Processing a data file
```rust
var lines = Viper.File.readLines("scores.csv");
var total = 0;

for line in lines {
    var parts = line.split(",");
    var score = Viper.Parse.Int(parts[1]);
    total += score;
}

Viper.Terminal.Say("Total: " + total);
```

---

## The Three Languages

**ViperLang**
```rust
// Read
var content = Viper.File.readText("file.txt");

// Write
Viper.File.writeText("file.txt", "Hello!");

// Append
Viper.File.appendText("file.txt", "More text\n");

// Check existence
if Viper.File.exists("file.txt") { ... }
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
```

BASIC uses file numbers (#1, #2, etc.) and requires explicit OPEN/CLOSE.

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
end.
```

Pascal uses file variables and procedures like AssignFile, Reset, Rewrite, Append.

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
```

**Overwriting when you meant to append:**
```rust
Viper.File.writeText("log.txt", "Entry 1\n");
Viper.File.writeText("log.txt", "Entry 2\n");  // Oops! Entry 1 is gone

Viper.File.writeText("log.txt", "Entry 1\n");
Viper.File.appendText("log.txt", "Entry 2\n");  // Correct
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
// Forgot reader.close()!  File stays locked
```

When using the stream APIs, always close. The simple `readText`/`writeText` functions handle this automatically.

---

## Summary

- Files persist data beyond program lifetime
- `Viper.File.readText/writeText` for simple file operations
- `Viper.File.appendText` adds to existing files
- `Viper.File.readLines` reads as an array of lines
- `Viper.File.exists` checks before reading
- `Viper.Dir` works with directories
- `Viper.Path` manipulates file paths safely
- Close file handles when using stream APIs
- Use relative paths for portability

---

## Exercises

**Exercise 9.1**: Write a program that saves your name to a file, then reads it back and greets you.

**Exercise 9.2**: Write a program that reads a file and counts how many lines it contains.

**Exercise 9.3**: Write a program that copies one file to another (read the original, write to the copy).

**Exercise 9.4**: Modify the Note Keeper to also save timestamps with each note.

**Exercise 9.5**: Write a program that lists all `.txt` files in the current directory.

**Exercise 9.6** (Challenge): Write a simple address book that stores names and phone numbers in a file, with commands to add, search, list, and delete contacts.

---

*We can now save and load data. But what happens when things go wrong — the file doesn't exist, the network fails, the input is invalid? Next, we learn to handle errors gracefully.*

*[Continue to Chapter 10: Errors and Recovery →](10-errors.md)*
