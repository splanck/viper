# Files & Directories
> File, BinFile, TempFile, Dir, Path, Glob

**Part of [Viper Runtime Library](../README.md) › [Input & Output](README.md)**

---

## Viper.IO.File

File system operations.

**Type:** Static utility class

### Methods

| Method                        | Signature              | Description                                                                               |
|-------------------------------|------------------------|-------------------------------------------------------------------------------------------|
| `Exists(path)`                | `Boolean(String)`      | Returns true if the file exists                                                           |
| `ReadAllText(path)`           | `String(String)`       | Reads the entire file contents as a string                                                |
| `WriteAllText(path, content)` | `Void(String, String)` | Writes a string to a file (overwrites if exists)                                          |
| `Delete(path)`                | `Void(String)`         | Deletes a file                                                                            |
| `Copy(src, dst)`              | `Void(String, String)` | Copies a file from src to dst                                                             |
| `Move(src, dst)`              | `Void(String, String)` | Moves/renames a file from src to dst                                                      |
| `Size(path)`                  | `Integer(String)`      | Returns file size in bytes, or -1 if not found                                            |
| `ReadBytes(path)`             | `ptr(String)`          | Reads the entire file as a raw runtime buffer                                             |
| `WriteBytes(path, data)`      | `Void(String, ptr)`    | Writes a raw runtime buffer to a file                                                     |
| `ReadAllBytes(path)`          | `Bytes(String)`        | Reads the entire file as binary data (traps on I/O errors)                                |
| `WriteAllBytes(path, bytes)`  | `Void(String, Bytes)`  | Writes binary data to a file (overwrites; traps on I/O errors)                            |
| `ReadLines(path)`             | `ptr(String)`          | Reads the file as a raw runtime buffer of lines                                           |
| `WriteLines(path, lines)`     | `Void(String, Seq)`    | Writes a sequence of strings as lines                                                     |
| `Append(path, text)`          | `Void(String, String)` | Appends text to a file                                                                    |
| `AppendLine(path, text)`      | `Void(String, String)` | Appends text followed by `\n` to a file (creates if missing)                              |
| `ReadAllLines(path)`          | `Seq(String)`          | Reads file as a sequence of lines; strips `\n` / `\r\n` terminators (traps on I/O errors) |
| `Modified(path)`              | `Integer(String)`      | Returns file modification time as Unix timestamp                                          |
| `Touch(path)`                 | `Void(String)`         | Creates file or updates its modification time                                             |

### Notes

- `AppendLine` always appends a single `\n` byte (no platform newline normalization).
- `ReadAllLines` splits on `\n` and `\r\n` and does not include line endings in returned strings; a trailing line ending
  does not add an extra empty final line.
- `ReadAllBytes`, `WriteAllBytes`, and `ReadAllLines` trap (write a diagnostic to stderr and terminate) on I/O errors.
- `ReadBytes` and `ReadLines` return raw `ptr` values representing internal runtime buffer objects. They are intended for use with other low-level runtime functions and are not strongly-typed Zia objects. Similarly, `WriteBytes` accepts a raw `ptr` for the data parameter. For strongly-typed binary and line I/O, prefer `ReadAllBytes`, `WriteAllBytes`, and `ReadAllLines`.

### Zia Example

```rust
module FileDemo;

bind Viper.Terminal;
bind Viper.IO.File as File;
bind Viper.IO.Path as Path;
bind Viper.Fmt as Fmt;

func start() {
    // Path utilities
    var p = Path.Join("/tmp", "test.txt");
    Say("Path: " + p);
    Say("Ext: " + Path.Ext(p));
    Say("Name: " + Path.Name(p));
    Say("Dir: " + Path.Dir(p));
    Say("Stem: " + Path.Stem(p));

    // Write and read a file
    File.WriteAllText("/tmp/viper_test.txt", "Hello from Zia!");
    var content = File.ReadAllText("/tmp/viper_test.txt");
    Say("Content: " + content);
    Say("Exists: " + Fmt.Bool(File.Exists("/tmp/viper_test.txt")));

    // File size
    Say("Size: " + Fmt.Int(File.Size("/tmp/viper_test.txt")));

    // Clean up
    File.Delete("/tmp/viper_test.txt");
}
```

### BASIC Example

```basic
DIM filename AS STRING
filename = "data.txt"

' Write to file
Viper.IO.File.WriteAllText(filename, "Hello, World!")

' Check if file exists
IF Viper.IO.File.Exists(filename) THEN
    ' Read contents
    DIM content AS STRING
    content = Viper.IO.File.ReadAllText(filename)
    PRINT content  ' Output: "Hello, World!"

    ' Delete file
    Viper.IO.File.Delete(filename)
END IF
```

### Binary File Example

```basic
' Create binary data
DIM data AS Bytes
data = Viper.Collections.Bytes.New(4)
data.Set(0, &H48)  ' H
data.Set(1, &H69)  ' i
data.Set(2, &H21)  ' !
data.Set(3, &H00)  ' null byte

' Write binary file
Viper.IO.File.WriteAllBytes("test.bin", data)

' Read binary file
DIM loaded AS Bytes
loaded = Viper.IO.File.ReadAllBytes("test.bin")
PRINT "Size:"; loaded.Len()  ' Output: Size: 4
```

### Line-by-Line Example

```basic
' Write lines to file
DIM lines AS Seq
lines = Viper.Collections.Seq.New()
lines.Push("First line")
lines.Push("Second line")
lines.Push("Third line")
Viper.IO.File.WriteLines("output.txt", lines)

' Read lines from file
DIM readLines AS Seq
readLines = Viper.IO.File.ReadAllLines("output.txt")
FOR i = 0 TO readLines.Len() - 1
    PRINT readLines.Get(i)
NEXT i

' Append to file
Viper.IO.File.Append("output.txt", "Appended text")
```

### File Management Example

```basic
' Copy file
Viper.IO.File.Copy("source.txt", "backup.txt")

' Move/rename file
Viper.IO.File.Move("old_name.txt", "new_name.txt")

' Get file info
DIM size AS INTEGER
size = Viper.IO.File.Size("data.txt")
PRINT "File size:"; size; "bytes"

DIM mtime AS INTEGER
mtime = Viper.IO.File.Modified("data.txt")
PRINT "Modified:"; mtime

' Create empty file or update timestamp
Viper.IO.File.Touch("marker.txt")
```

---

## Viper.IO.BinFile

Binary file stream for reading and writing raw bytes with random access capabilities.

**Type:** Instance class

**Constructor:** `Viper.IO.BinFile.Open(path, mode)`

### Open Modes

| Mode   | Description                               |
|--------|-------------------------------------------|
| `"r"`  | Read only (file must exist)               |
| `"w"`  | Write only (creates or truncates)         |
| `"rw"` | Read and write (file must exist)          |
| `"a"`  | Append (creates if needed, writes at end) |

### Properties

| Property | Type    | Description                                   |
|----------|---------|-----------------------------------------------|
| `Pos`    | Integer | Current file position (read-only)             |
| `Size`   | Integer | Total file size in bytes (read-only)          |
| `Eof`    | Boolean | True if at end of file (read-only)            |

### Methods

| Method                        | Returns | Description                                                            |
|-------------------------------|---------|------------------------------------------------------------------------|
| `Close()`                     | void    | Close the file and release resources                                   |
| `Read(bytes, offset, count)`  | Integer | Read up to count bytes into Bytes object at offset; returns bytes read |
| `Write(bytes, offset, count)` | void    | Write count bytes from Bytes object starting at offset                 |
| `ReadByte()`                  | Integer | Read single byte (0-255) or -1 at EOF                                  |
| `WriteByte(value)`            | void    | Write single byte (0-255)                                              |
| `Seek(offset, origin)`        | Integer | Seek to position; returns new position                                 |
| `Flush()`                     | void    | Flush buffered writes to disk                                          |

### Seek Origins

| Origin | Description                       |
|--------|-----------------------------------|
| `0`    | From beginning of file (SEEK_SET) |
| `1`    | From current position (SEEK_CUR)  |
| `2`    | From end of file (SEEK_END)       |

### Zia Example

```rust
module BinFileDemo;

bind Viper.Terminal;
bind Viper.IO.BinFile as BF;
bind Viper.IO.File as File;
bind Viper.Fmt as Fmt;

func start() {
    // Write binary data
    var bf = BF.Open("/tmp/data.bin", "w");
    bf.WriteByte(0xCA);
    bf.WriteByte(0xFE);
    bf.WriteByte(0xBA);
    bf.WriteByte(0xBE);
    bf.Close();

    // Read binary data
    bf = BF.Open("/tmp/data.bin", "r");
    Say("Size: " + Fmt.Int(bf.get_Size()));

    var b1 = bf.ReadByte();
    var b2 = bf.ReadByte();
    Say("Byte 1: " + Fmt.Int(b1));   // 202 (0xCA)
    Say("Byte 2: " + Fmt.Int(b2));   // 254 (0xFE)

    // Seek back to start
    bf.Seek(0, 0);
    Say("After seek: " + Fmt.Int(bf.ReadByte()));
    bf.Close();

    File.Delete("/tmp/data.bin");
}
```

> **Note:** BinFile properties (`Pos`, `Size`, `Eof`) use the get_/set_ pattern; access them as `bf.get_Size()`, `bf.get_Pos()`, `bf.get_Eof()` in Zia.

### BASIC Example

```basic
' Write binary data
DIM bf AS OBJECT = Viper.IO.BinFile.Open("data.bin", "w")

' Write individual bytes
bf.WriteByte(&HCA)
bf.WriteByte(&HFE)
bf.WriteByte(&HBA)
bf.WriteByte(&HBE)

' Write from a Bytes object
DIM data AS OBJECT = NEW Viper.Collections.Bytes(4)
data.Set(0, 1)
data.Set(1, 2)
data.Set(2, 3)
data.Set(3, 4)
bf.Write(data, 0, 4)

bf.Close()

' Read binary data
bf = Viper.IO.BinFile.Open("data.bin", "r")

' Check file size
PRINT bf.Size                 ' Output: 8

' Read byte by byte
PRINT HEX(bf.ReadByte())      ' Output: CA
PRINT HEX(bf.ReadByte())      ' Output: FE

' Seek to position
bf.Seek(0, 0)                 ' Back to start

' Read into a Bytes buffer
DIM buffer AS OBJECT = NEW Viper.Collections.Bytes(8)
DIM bytesRead AS INTEGER = bf.Read(buffer, 0, 8)
PRINT bytesRead               ' Output: 8

' Check for end of file
PRINT bf.Eof                  ' Output: 1

bf.Close()

' Read/write mode for random access
bf = Viper.IO.BinFile.Open("data.bin", "rw")

' Seek to position 4 and overwrite
bf.Seek(4, 0)
bf.WriteByte(&HFF)

bf.Close()
```

### Use Cases

- **Binary file formats:** Read/write structured binary data
- **Random access:** Seek to arbitrary positions in files
- **Large files:** Process files too large to load entirely into memory
- **Low-level I/O:** Direct byte-level file manipulation
- **Database files:** Read/write fixed-record binary databases

---

## Viper.IO.TempFile

Temporary file and directory creation utilities. Generates unique paths in the system temporary directory with optional prefixes and extensions.

**Type:** Static utility class

### Methods

| Method                         | Signature              | Description                                                        |
|--------------------------------|------------------------|--------------------------------------------------------------------|
| `Dir()`                        | `String()`             | Returns the system temporary directory path                        |
| `Path()`                       | `String()`             | Generate a unique temporary file path (with `.tmp` extension)      |
| `PathWithPrefix(prefix)`       | `String(String)`       | Generate a unique temporary file path with a prefix                |
| `PathWithExt(prefix, ext)`     | `String(String, String)` | Generate a unique temporary file path with prefix and extension  |
| `Create()`                     | `String()`             | Create an empty temporary file and return its path                 |
| `CreateWithPrefix(prefix)`     | `String(String)`       | Create an empty temporary file with prefix and return its path     |
| `CreateDir()`                  | `String()`             | Create a temporary directory and return its path                   |
| `CreateDirWithPrefix(prefix)`  | `String(String)`       | Create a temporary directory with prefix and return its path       |

### Notes

- `Dir()` returns the platform temporary directory (e.g., `/tmp` on Unix, `%TEMP%` on Windows)
- `Path` and `PathWithPrefix` only generate paths -- they do not create files on disk
- `Create` and `CreateDir` actually create the file or directory on disk
- `PathWithExt("v", ".txt")` produces a path like `/tmp/v_<unique>.txt`
- Temporary files and directories are not automatically cleaned up; the caller is responsible for deletion

### Zia Example

```rust
module TempFileDemo;

bind Viper.Terminal;
bind Viper.IO.TempFile as TempFile;
bind Viper.IO.File as File;

func start() {
    Say("Temp dir: " + TempFile.Dir());

    // Generate a unique path (does not create the file)
    var p = TempFile.Path();
    Say("Temp path: " + p);

    // Generate with prefix and extension
    var p2 = TempFile.PathWithExt("viper", ".txt");
    Say("Custom path: " + p2);

    // Create an actual temp file
    var created = TempFile.Create();
    Say("Created: " + created);
    File.Delete(created);
}
```

### BASIC Example

```basic
' Get the system temp directory
PRINT "Temp dir: "; Viper.IO.TempFile.Dir()   ' Output: /tmp (or platform equivalent)

' Generate unique temp file paths (no file created)
DIM p AS STRING = Viper.IO.TempFile.Path()
PRINT "Path: "; p   ' Output: /tmp/<unique>.tmp

DIM p2 AS STRING = Viper.IO.TempFile.PathWithPrefix("myapp")
PRINT "Prefixed: "; p2   ' Output: /tmp/myapp_<unique>.tmp

DIM p3 AS STRING = Viper.IO.TempFile.PathWithExt("v", ".txt")
PRINT "With ext: "; p3   ' Output: /tmp/v_<unique>.txt

' Create an actual temp file on disk
DIM f AS STRING = Viper.IO.TempFile.Create()
PRINT "Created file: "; f
Viper.IO.File.WriteAllText(f, "temp data")
Viper.IO.File.Delete(f)

' Create a temp directory
DIM d AS STRING = Viper.IO.TempFile.CreateDir()
PRINT "Created dir: "; d
Viper.IO.Dir.RemoveAll(d)

' Create a temp directory with prefix
DIM d2 AS STRING = Viper.IO.TempFile.CreateDirWithPrefix("build")
PRINT "Build dir: "; d2
Viper.IO.Dir.RemoveAll(d2)
```

---

## Viper.IO.Dir

Cross-platform directory operations for creating, removing, listing, and navigating directories.

**Type:** Static utility class

### Methods

| Method             | Signature              | Description                                                                               |
|--------------------|------------------------|-------------------------------------------------------------------------------------------|
| `Exists(path)`     | `Boolean(String)`      | Returns true if the directory exists                                                      |
| `Make(path)`       | `Void(String)`         | Creates a single directory (parent must exist)                                            |
| `MakeAll(path)`    | `Void(String)`         | Creates a directory and all parent directories                                            |
| `Remove(path)`     | `Void(String)`         | Removes an empty directory                                                                |
| `RemoveAll(path)`  | `Void(String)`         | Recursively removes a directory and all its contents                                      |
| `Entries(path)`    | `Seq(String)`          | Returns directory entries (files + subdirectories); traps if the directory does not exist |
| `List(path)`       | `Seq(String)`          | Returns all entries in a directory (excluding `.` and `..`)                               |
| `ListSeq(path)`    | `Seq(String)`          | Seq-returning alias of `List(path)` (same semantics)                                      |
| `Files(path)`      | `Seq(String)`          | Returns only files in a directory (no subdirectories)                                     |
| `FilesSeq(path)`   | `Seq(String)`          | Seq-returning alias of `Files(path)` (same semantics)                                     |
| `Dirs(path)`       | `Seq(String)`          | Returns only subdirectories in a directory                                                |
| `DirsSeq(path)`    | `Seq(String)`          | Seq-returning alias of `Dirs(path)` (same semantics)                                      |
| `Current()`        | `String()`             | Returns the current working directory                                                     |
| `SetCurrent(path)` | `Void(String)`         | Changes the current working directory                                                     |
| `Move(src, dst)`   | `Void(String, String)` | Moves/renames a directory                                                                 |

**Note:** `Entries()`, `List()`, `Files()`, and `Dirs()` return entry names (not full paths). Use
`Viper.IO.Path.Join(dir, name)` to build full paths when needed.

### Zia Example

```rust
module DirDemo;

bind Viper.Terminal;
bind Viper.IO.Dir as Dir;
bind Viper.Fmt as Fmt;

func start() {
    // Current working directory
    Say("CWD: " + Dir.Current());

    // Check if directory exists
    Say("Exists /tmp: " + Fmt.Bool(Dir.Exists("/tmp")));

    // Create nested directories (like mkdir -p)
    Dir.MakeAll("/tmp/viper_demo/sub/deep");
    Say("Created: " + Fmt.Bool(Dir.Exists("/tmp/viper_demo/sub/deep")));

    // Clean up
    Dir.RemoveAll("/tmp/viper_demo");
    Say("Removed: " + Fmt.Bool(!Dir.Exists("/tmp/viper_demo")));
}
```

### BASIC Example

```basic
' Check if a directory exists
IF Viper.IO.Dir.Exists("/home/user/documents") THEN
    PRINT "Documents folder exists"
END IF

' Create a new directory
Viper.IO.Dir.Make("/home/user/newdir")

' Create nested directories (like mkdir -p)
Viper.IO.Dir.MakeAll("/home/user/a/b/c/d")

' List all entries in a directory
DIM entries AS Viper.Collections.Seq
entries = Viper.IO.Dir.List("/home/user")
FOR i = 0 TO entries.Len - 1
    PRINT entries.Get(i)
NEXT i

' List directory entries (files + subdirectories); traps if the directory is missing
DIM all_entries AS Viper.Collections.Seq
all_entries = Viper.IO.Dir.Entries("/home/user")

' List only files (no subdirectories)
DIM files AS Viper.Collections.Seq
files = Viper.IO.Dir.Files("/home/user")

' List only subdirectories
DIM subdirs AS Viper.Collections.Seq
subdirs = Viper.IO.Dir.Dirs("/home/user")

' Get and change current working directory
DIM cwd AS STRING
cwd = Viper.IO.Dir.Current()
PRINT "Current directory: "; cwd

Viper.IO.Dir.SetCurrent("/home/user/projects")
PRINT "New directory: "; Viper.IO.Dir.Current()

' Restore original directory
Viper.IO.Dir.SetCurrent(cwd)

' Move/rename a directory
Viper.IO.Dir.Move("/home/user/oldname", "/home/user/newname")

' Remove an empty directory
Viper.IO.Dir.Remove("/home/user/emptydir")

' Recursively remove a directory and all its contents
' WARNING: This permanently deletes files!
Viper.IO.Dir.RemoveAll("/home/user/tempdir")
```

### Error Handling

Directory operations trap on errors:

- `Make()` traps if the parent directory doesn't exist or creation fails
- `Remove()` traps if the directory is not empty or doesn't exist
- `RemoveAll()` silently ignores non-existent directories
- `SetCurrent()` traps if the directory doesn't exist

Use `Exists()` to check before performing operations that may fail.

### Listing Functions

The three listing functions return `Seq` objects containing entry names (not full paths):

| Function      | Returns          | Includes                      |
|---------------|------------------|-------------------------------|
| `List(path)`  | All entries      | Files and subdirectories      |
| `Files(path)` | Files only       | Regular files, no directories |
| `Dirs(path)`  | Directories only | Subdirectories, no files      |

The `ListSeq()`/`FilesSeq()`/`DirsSeq()` variants are equivalent Seq-returning aliases for these legacy `ptr(str)` APIs.
Use the `*Seq` forms when a frontend or toolchain stage requires an object-typed `Seq` result explicitly.

```basic
DIM names AS Viper.Collections.Seq
names = Viper.IO.Dir.ListSeq("/home/user")
PRINT names.Len
```

All listing functions exclude `.` and `..` entries. If the directory doesn't exist or can't be read, an empty sequence
is returned.

### Use Cases

- **File management:** List, copy, move, and delete directories
- **Build systems:** Create output directories with `MakeAll()`
- **Cleanup:** Remove temporary directories with `RemoveAll()`
- **Navigation:** Get and set the working directory
- **Filtering:** Separate files from subdirectories with `Files()` and `Dirs()`

---

## Viper.IO.Path

Cross-platform path manipulation utilities. All functions work with both Unix (`/`) and Windows (`\`) path separators.

**Type:** Static utility class

### Methods

| Method               | Signature                | Description                                                 |
|----------------------|--------------------------|-------------------------------------------------------------|
| `Join(a, b)`         | `String(String, String)` | Joins two path components with the platform separator       |
| `Dir(path)`          | `String(String)`         | Returns the directory portion of a path                     |
| `Name(path)`         | `String(String)`         | Returns the filename portion of a path                      |
| `Stem(path)`         | `String(String)`         | Returns the filename without extension                      |
| `Ext(path)`          | `String(String)`         | Returns the file extension (including the dot)              |
| `WithExt(path, ext)` | `String(String, String)` | Replaces the extension of a path                            |
| `IsAbs(path)`        | `Boolean(String)`        | Returns true if the path is absolute                        |
| `Abs(path)`          | `String(String)`         | Converts a relative path to absolute                        |
| `Norm(path)`         | `String(String)`         | Normalizes a path (removes `.`, `..`, duplicate separators) |
| `Sep()`              | `String()`               | Returns the platform-specific path separator                |

### Zia Example

```rust
module PathDemo;

bind Viper.Terminal;
bind Viper.IO.Path as Path;
bind Viper.Fmt as Fmt;

func start() {
    var p = "/home/user/documents/report.txt";

    // Extract path components
    Say("Dir:  " + Path.Dir(p));    // /home/user/documents
    Say("Name: " + Path.Name(p));   // report.txt
    Say("Stem: " + Path.Stem(p));   // report
    Say("Ext:  " + Path.Ext(p));    // .txt

    // Join paths
    Say("Join: " + Path.Join("/home/user", "downloads"));

    // Replace extension
    Say("WithExt: " + Path.WithExt(p, ".md"));

    // Check if absolute
    Say("IsAbs: " + Fmt.Bool(Path.IsAbs(p)));
    Say("IsAbs relative: " + Fmt.Bool(Path.IsAbs("foo/bar")));

    // Normalize paths
    Say("Norm: " + Path.Norm("/foo//bar/../baz"));

    // Platform separator
    Say("Sep: " + Path.Sep());
}
```

### BASIC Example

```basic
DIM path AS STRING
path = "/home/user/documents/report.txt"

' Extract path components
PRINT Viper.IO.Path.Dir(path)   ' Output: "/home/user/documents"
PRINT Viper.IO.Path.Name(path)  ' Output: "report.txt"
PRINT Viper.IO.Path.Stem(path)  ' Output: "report"
PRINT Viper.IO.Path.Ext(path)   ' Output: ".txt"

' Join paths
DIM newPath AS STRING
newPath = Viper.IO.Path.Join("/home/user", "downloads")
PRINT newPath  ' Output: "/home/user/downloads"

' Replace extension
DIM mdPath AS STRING
mdPath = Viper.IO.Path.WithExt(path, ".md")
PRINT mdPath  ' Output: "/home/user/documents/report.md"

' Check if absolute
PRINT Viper.IO.Path.IsAbs(path)      ' Output: true
PRINT Viper.IO.Path.IsAbs("foo/bar") ' Output: false

' Normalize paths
PRINT Viper.IO.Path.Norm("/foo//bar/../baz")  ' Output: "/foo/baz"
PRINT Viper.IO.Path.Norm("./a/b/../c")        ' Output: "a/c"

' Get platform separator
PRINT Viper.IO.Path.Sep()  ' Output: "/" on Unix, "\" on Windows
```

### Path Normalization

The `Norm()` function performs the following transformations:

- Removes redundant separators (`//` becomes `/`)
- Resolves `.` components (current directory)
- Resolves `..` components (parent directory) where possible
- Returns `.` for an empty result
- Preserves leading `..` in relative paths

### Platform Differences

| Behavior                | Unix            | Windows                   |
|-------------------------|-----------------|---------------------------|
| Path separator          | `/`             | `\`                       |
| Absolute path detection | Starts with `/` | Starts with `C:\` or `\\` |
| Example absolute path   | `/home/user`    | `C:\Users\user`           |

### Use Cases

- **Building file paths:** Use `Join()` to create paths safely
- **Extracting components:** Use `Dir()`, `Name()`, `Stem()`, `Ext()` to parse paths
- **Changing extensions:** Use `WithExt()` to replace file extensions
- **Cleaning paths:** Use `Norm()` to clean up user-provided paths
- **Portable code:** Use `Sep()` for platform-specific separators

---

## Viper.IO.Glob

File globbing utilities for matching file paths against wildcard patterns and finding files by pattern.

**Type:** Static utility class

### Methods

| Method                        | Signature                  | Description                                                     |
|-------------------------------|----------------------------|-----------------------------------------------------------------|
| `Match(path, pattern)`        | `Boolean(String, String)`  | Returns true if the path matches the glob pattern               |
| `Files(dir, pattern)`         | `Seq(String, String)`      | Returns files in a directory matching the pattern               |
| `FilesRecursive(dir, pattern)` | `Seq(String, String)`     | Returns files in a directory and subdirectories matching pattern |
| `Entries(dir, pattern)`       | `Seq(String, String)`      | Returns all entries (files + dirs) matching the pattern          |

### Pattern Syntax

| Pattern | Description                        | Example                |
|---------|------------------------------------|------------------------|
| `*`     | Matches any sequence of characters | `*.txt` matches `a.txt` |
| `?`     | Matches a single character         | `?.c` matches `a.c`    |
| `[abc]` | Matches any character in brackets  | `[abc].txt`            |

### Notes

- **Parameter order is (path, pattern)** for `Match`, not (pattern, path)
- `Files` returns only regular files, not directories
- `FilesRecursive` descends into all subdirectories
- `Entries` returns both files and directories that match the pattern
- All listing methods return a `Seq` of full path strings
- Patterns are matched against the filename component, not the full path (for `Files`/`Entries`)

### Zia Example

```rust
module GlobDemo;

bind Viper.Terminal;
bind Viper.IO.Glob as Glob;
bind Viper.Fmt as Fmt;

func start() {
    Say("Match txt: " + Fmt.Bool(Glob.Match("hello.txt", "*.txt")));   // true
    Say("Match c: " + Fmt.Bool(Glob.Match("hello.c", "*.txt")));       // false
}
```

### BASIC Example

```basic
' Match file paths against patterns
PRINT Viper.IO.Glob.Match("hello.txt", "*.txt")    ' Output: 1
PRINT Viper.IO.Glob.Match("hello.c", "*.txt")      ' Output: 0
PRINT Viper.IO.Glob.Match("test.bas", "*.bas")      ' Output: 1
PRINT Viper.IO.Glob.Match("readme.md", "read*")     ' Output: 1

' Find all .txt files in a directory
DIM txtFiles AS OBJECT = Viper.IO.Glob.Files("/home/user/docs", "*.txt")
FOR i = 0 TO txtFiles.Len - 1
    PRINT txtFiles.Get(i)
NEXT i

' Find all .bas files recursively
DIM basFiles AS OBJECT = Viper.IO.Glob.FilesRecursive("/home/user/projects", "*.bas")
PRINT "Found "; basFiles.Len; " BASIC files"

' Find all entries (files + dirs) matching a pattern
DIM entries AS OBJECT = Viper.IO.Glob.Entries("/home/user", "test*")
FOR i = 0 TO entries.Len - 1
    PRINT entries.Get(i)
NEXT i
```

---


## See Also

- [Streams & Buffers](streams.md)
- [Advanced IO](advanced.md)
- [Input & Output Overview](README.md)
- [Viper Runtime Library](../README.md)
