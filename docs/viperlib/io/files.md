---
status: active
audience: public
last-verified: 2026-05-07
---

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
| `Exists(path)`                | `Boolean(String)`      | Returns true only if the path exists and is a regular file                               |
| `ReadAllText(path)`           | `String(String)`       | Reads the entire file contents as a string; traps on I/O errors                           |
| `WriteAllText(path, content)` | `Void(String, String)` | Atomically replaces a text file with new contents                                          |
| `Delete(path)`                | `Void(String)`         | Deletes a file; missing files are ignored, other failures trap                            |
| `Copy(src, dst)`              | `Void(String, String)` | Copies a file from src to dst; traps if `dst` already exists or both paths name the same file |
| `Move(src, dst)`              | `Void(String, String)` | Moves or renames a file; traps if `dst` already exists                                    |
| `MoveOver(src, dst)`          | `Void(String, String)` | Moves or renames a file, replacing `dst` when supported by the platform                   |
| `Size(path)`                  | `Integer(String)`      | Returns regular-file size in bytes, or -1 if not found or not a regular file              |
| `ReadBytes(path)`             | `Bytes(String)`        | Reads the entire file as binary data; traps on I/O errors                                |
| `WriteBytes(path, data)`      | `Void(String, Bytes)`  | Atomically replaces a file with binary data                                               |
| `ReadAllBytes(path)`          | `Bytes(String)`        | Reads the entire file as binary data (traps on I/O errors)                                |
| `WriteAllBytes(path, bytes)`  | `Void(String, Bytes)`  | Atomically replaces a file with binary data (traps on I/O errors)                         |
| `ReadLines(path)`             | `Seq(String)`          | Reads the file as a runtime sequence of lines; traps on I/O errors                        |
| `WriteLines(path, lines)`     | `Void(String, Seq(String))` | Atomically writes a sequence of strings as lines; traps on I/O errors                |
| `Append(path, text)`          | `Void(String, String)` | Appends text to a file; traps on I/O errors                                               |
| `AppendLine(path, text)`      | `Void(String, String)` | Appends text followed by `\n` to a file (creates if missing)                              |
| `ReadAllLines(path)`          | `Seq(String)`          | Reads file as a sequence of lines; strips `\n` / `\r\n` terminators (traps on I/O errors) |
| `Modified(path)`              | `Integer(String)`      | Returns regular-file modification time as Unix timestamp, or -1 if missing or not a file  |
| `Touch(path)`                 | `Void(String)`         | Creates file or updates its modification time; traps on I/O errors                        |

### Notes

- `AppendLine` always appends a single `\n` byte (no platform newline normalization).
- `Exists` returns false for directories; use `Dir.Exists` for directory checks.
- Path strings with embedded NUL bytes are rejected before reaching platform file APIs.
- `ReadAllText`, `ReadAllBytes`, and `ReadAllLines` require a regular file and trap on directories, special files, I/O errors, or unexpected short reads if the file changes while being read.
- `WriteAllText`, `WriteAllBytes`, `WriteBytes`, and `WriteLines` write to an exclusive temporary file in the destination directory and then replace the live file. Failed writes trap instead of silently leaving a partial overwrite behind. Temporary sidecar names are unpredictable and do not include process memory addresses.
- `Copy` and `Move` never overwrite an existing destination. `MoveOver` is the explicit replacement operation; it first attempts an in-place replace/rename and only falls back to copy-plus-delete when the source and destination are on different filesystems or volumes.
- `Copy` preserves regular-file permission bits and modification/access times where the platform exposes them. Cross-device `Move` uses the same copy path before deleting the source.
- `Size` and `Modified` return `-1` for missing paths, directories, and special files. A real zero-byte file still reports size `0`.
- `ReadAllLines` splits on `\n`, `\r`, and `\r\n` and does not include line endings in returned strings. Trailing empty lines are preserved.
- `WriteAllText`, `ReadAllText`, `ReadBytes`, `ReadAllBytes`, `WriteAllBytes`, `ReadAllLines`, `WriteBytes`, `WriteLines`, `Append`, and `Touch` trap on I/O errors.
- POSIX file descriptors and Windows CRT handles opened by the runtime are created as close-on-exec/non-inheritable where the platform API supports it.
- `ReadBytes`/`WriteBytes` use the typed `Viper.Collections.Bytes` runtime class. `ReadLines`/`WriteLines` use `Seq(String)`. The runtime keeps any raw buffer pointers internal.

### Zia Example

```rust
module FileDemo;

bind Viper.Terminal;
bind Viper.IO.File as File;
bind Viper.IO.Path as Path;
bind Viper.Text.Fmt as Fmt;

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
data.Set(0, 72)  ' H
data.Set(1, 105)  ' i
data.Set(2, 33)  ' !
data.Set(3, 0)  ' null byte

' Write binary file
Viper.IO.File.WriteAllBytes("test.bin", data)

' Read binary file
DIM loaded AS Bytes
loaded = Viper.IO.File.ReadAllBytes("test.bin")
PRINT "Size:"; loaded.Length()  ' Output: Size: 4
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
FOR i = 0 TO readLines.Length() - 1
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

' Replace an existing destination explicitly
Viper.IO.File.MoveOver("new_data.txt", "data.txt")

' Use Viper.IO.Dir.Move for directories; file moves require a regular-file source.

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
| `"rb"` | Read only binary alias                    |
| `"w"`  | Write only (creates or truncates)         |
| `"wb"` | Write only binary alias                   |
| `"rw"` | Read and write (file must exist)          |
| `"r+"` | Read and write alias                      |
| `"a"`  | Append (creates if needed, writes at end) |
| `"ab"` | Append binary alias                       |

`Close()` and `Flush()` trap if the platform reports a delayed write or close failure.
`BinFile` also normalizes the required C stdio transition between reads and writes on `"rw"`/`"r+"` streams, so switching direction does not rely on undefined buffered-stdio state.

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
bind Viper.Text.Fmt as Fmt;

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
bf.WriteByte(202)
bf.WriteByte(254)
bf.WriteByte(186)
bf.WriteByte(190)

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
bf.WriteByte(255)

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

- `Dir()` returns the platform temporary directory (e.g., `/tmp` on Unix, `%TEMP%` on Windows). On POSIX, an invalid relative, missing, or non-directory `TMPDIR` value is ignored and `/tmp` is used instead.
- `Path` and `PathWithPrefix` only generate paths -- they do not create files on disk
- `Create` and `CreateDir` actually create the file or directory on disk
- `PathWithExt("v", ".txt")` produces a path like `/tmp/v_<unique>.txt`
- Prefixes and extensions are filename fragments: path separators, drive separators, and traversal syntax are rejected.
- Prefixes and extensions also reject embedded NUL bytes; generated names are never truncated at a hidden NUL.
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
| `RemoveAll(path)`  | `Void(String)`         | Recursively removes a directory and all its contents without following symlinks into targets |
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

`Make()` and `MakeAll()` are idempotent for existing directories, but they trap if the target path or any intermediate path component already exists as a non-directory. `MakeAll()` follows host path semantics: `/` is the separator on POSIX, while both `/` and `\` are accepted on Windows.
`Files()` excludes symbolic links to files on POSIX. `RemoveAll()` removes a top-level symlink itself and does not recurse into the linked directory. On POSIX, recursive removal uses file-descriptor-relative traversal so a concurrently swapped symlink component cannot redirect deletion outside the requested tree.

### Zia Example

```rust
module DirDemo;

bind Viper.Terminal;
bind Viper.IO.Dir as Dir;
bind Viper.Text.Fmt as Fmt;

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
FOR i = 0 TO entries.Length - 1
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
- `RemoveAll()` ignores an already-missing top-level directory, but traps if any existing child cannot be removed
- `RemoveAll()` refuses protected targets such as the filesystem root, `.`, `..`, and the current working directory
- `Move()` traps if the source directory is missing or the destination already exists
- `SetCurrent()` traps if the directory doesn't exist

Use `Exists()` to check before performing operations that may fail.

### Listing Functions

The three listing functions return `Seq` objects containing entry names (not full paths):

| Function      | Returns          | Includes                      |
|---------------|------------------|-------------------------------|
| `List(path)`  | All entries      | Files and subdirectories      |
| `Files(path)` | Files only       | Regular files, no directories |
| `Dirs(path)`  | Directories only | Subdirectories, no files      |

The `ListSeq()`/`FilesSeq()`/`DirsSeq()` variants are equivalent Seq-returning aliases.
Use the `*Seq` forms when a frontend or toolchain stage wants the explicit suffix.

```basic
DIM names AS Viper.Collections.Seq
names = Viper.IO.Dir.ListSeq("/home/user")
PRINT names.Length
```

All listing functions exclude `.` and `..` entries. If the directory doesn't exist or can't be read, an empty sequence
is returned. `Entries()` is the strict variant: it traps on open, read, or close errors.

### Use Cases

- **File management:** List, copy, move, and delete directories
- **Build systems:** Create output directories with `MakeAll()`
- **Cleanup:** Remove temporary directories with `RemoveAll()`
- **Navigation:** Get and set the working directory
- **Filtering:** Separate files from subdirectories with `Files()` and `Dirs()`

---

## Viper.IO.Path

Cross-platform path manipulation utilities. On Windows, both `/` and `\` are treated as separators. On POSIX, `/` is the only path separator; backslash is treated as an ordinary filename byte. `Path.Join(a, b)` still accepts either slash as a join-boundary convenience, but `Dir`, `Name`, `Norm`, and absolute-path checks follow host filesystem semantics.

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
| `ExeDir()`           | `String()`               | Returns the directory containing the running executable     |
| `DataDir(app)`       | `String(String)`         | Per-user writable data directory for `app` (created on demand) |
| `Norm(path)`         | `String(String)`         | Normalizes a path (removes `.`, `..`, duplicate separators) |
| `Sep()`              | `String()`               | Returns the platform-specific path separator                |

`DataDir(app)` resolves the OS-conventional per-user location and creates the
directory (including parents) if needed, so settings and save files can be
written immediately:

- **Windows:** `%APPDATA%\<app>`
- **macOS:** `~/Library/Application Support/<app>`
- **Linux:** `$XDG_DATA_HOME/<app>` when set, else `~/.local/share/<app>`

The app name must be alphanumeric/dash/underscore (max 64 chars); anything
else traps. Repeated calls return the same absolute path.

### Zia Example

```rust
module PathDemo;

bind Viper.Terminal;
bind Viper.IO.Path as Path;
bind Viper.Text.Fmt as Fmt;

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
| Absolute path detection | Starts with `/` | Starts with `C:\`, `\\`, or root-relative `\` |
| Example absolute path   | `/home/user`    | `C:\Users\user`           |

Windows drive-relative paths such as `C:logs\app.txt` are not absolute. `Path.Norm()` preserves the `C:` relative prefix instead of converting it to `C:\`, and `Path.Join()` treats drive-rooted, UNC, and root-relative right-hand paths as absolute.
On POSIX, `Path.Name("a\\b.txt")` returns `"a\\b.txt"` and `Path.Dir("a\\b.txt")` returns `"."`, because the backslash is part of the filename rather than a directory separator.
`Path.WithExt("", "txt")` returns `.txt`, matching the behavior of applying an extension to an empty stem.

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
- Null path or pattern inputs return false for `Match` and an empty sequence for listing helpers
- Listing helpers return an empty sequence for paths or patterns containing embedded NUL bytes.
- `Files` returns only regular files, not directories
- `FilesRecursive` descends into all subdirectories and traps if recursion exceeds the runtime depth guard.
- `Entries` returns both files and directories that match the pattern
- All listing methods return a `Seq` of full path strings
- Patterns are matched against the filename component, not the full path (for `Files`/`Entries`)
- On Windows, both `/` and `\` are treated as path separators for `*`, `?`, `**`, and literal separator matching.
- On POSIX, only `/` is a path separator for glob matching; backslash is matched as a normal character.
- `**` matching is memoized, so very deep path strings are handled without a fixed recursion attempt limit.

### Zia Example

```rust
module GlobDemo;

bind Viper.Terminal;
bind Viper.IO.Glob as Glob;
bind Viper.Text.Fmt as Fmt;

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
FOR i = 0 TO txtFiles.Length - 1
    PRINT txtFiles.Get(i)
NEXT i

' Find all .bas files recursively
DIM basFiles AS OBJECT = Viper.IO.Glob.FilesRecursive("/home/user/projects", "*.bas")
PRINT "Found "; basFiles.Length; " BASIC files"

' Find all entries (files + dirs) matching a pattern
DIM entries AS OBJECT = Viper.IO.Glob.Entries("/home/user", "test*")
FOR i = 0 TO entries.Length - 1
    PRINT entries.Get(i)
NEXT i
```

---

## Viper.Workspace.FileIndex

Workspace file inventory helper for IDEs and editor tools.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Enumerate(root, extensionsCsv, excludesCsv, includeDirs)` | `Seq(String, String, String, Boolean)` | Recursively enumerate workspace entries under `root` |
| `Page(root, extensionsCsv, excludesCsv, includeDirs, offset, limit)` | `Map(String, String, String, Boolean, Integer, Integer)` | Return one bounded page of workspace entries |
| `Status(root, extensionsCsv, excludesCsv, includeDirs)` | `Map(String, String, String, Boolean)` | Return traversal status without materializing every entry |
| `ShouldIgnore(root, relativePath, patternsCsv)` | `Boolean(String, String, String)` | Return whether a relative path is ignored by hard excludes, `.gitignore`, or explicit patterns |

`Enumerate` returns a `Seq` of `Map` records. Each record includes `path`, `relativePath`, `name`, `extension`, `kind`, `isDirectory`, `id`, `size`, and `modified`.

`Page` returns a `Map` with `entries`, `offset`, `limit`, `emitted`, `nextOffset`, `scanned`, `done`, `truncated`, `maxEntries`, and `diagnostics`. Each entry has the same shape as `Enumerate`. Use `nextOffset` until `done` is true to process large workspaces without allocating every row at once.

`Status` returns a `Map` with `valid`, `root`, `entryCount`, `maxEntries`, `truncated`, and `diagnostics`. It uses the same filters and ignore rules as `Enumerate`, but it is intended for IDE progress/status surfaces and large-workspace guardrails where allocating every entry would be unnecessary.

### Notes

- Hard excludes include `.git`, `.hg`, `.svn`, `.viper`, `.viper-cache`, `build`, `cmake-build-*`, `node_modules`, and `.DS_Store`.
- `.gitignore` support is intentionally a documented subset: blank/comment lines, directory suffixes, `*`, `?`, `**`, and `!` negation are supported.
- `extensionsCsv` may contain values such as `.zia,.json,.png`; an empty list includes all regular files.
- The `id` field is a stable 63-bit hash of the canonical path for quick UI identity, not a persistent filesystem inode.

---

## Viper.Workspace.WorkspaceWatcher

Batch wrapper over `Viper.IO.Watcher` for per-frame IDE polling.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `PollBatch(watcher, maxEvents)` | `Seq(Object, Integer)` | Drain up to `maxEvents` queued watcher events into structured maps |

Each event map includes `path`, `typeName`, `type`, and `requiresRescan`. `requiresRescan` is true for overflow events so an IDE can discard incremental state and re-enumerate the workspace.

`Viper.IO.Watcher` remains non-recursive; use one watcher per watched directory or pair this helper with a workspace index rescan policy.

---

## Viper.Project.Manifest

Parser for `viper.project` and editor-expanded project manifests.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `ParseText(text)` | `Map(String)` | Parse manifest text into a structured map |
| `ParseFile(path)` | `Map(String)` | Read and parse a manifest file |

The returned map includes `valid`, `name`, `version`, `language`, `entry`, `defaultScene`, `sourceGlobs`, `excludes`, `assetRoots`, `sceneRoots`, `runConfigs`, `buildConfigs`, and `diagnostics`.

### Supported Directives

```text
project Demo
lang zia
entry src/main.zia
sources src
exclude build
asset-root assets
scene-root scenes
default-scene scenes/level.json

[run.play]
entry src/main.zia
args --dev, --scene=one
```

Unknown directives or sections keep safe defaults and add diagnostic records instead of trapping.

---

## Viper.Workspace.Edit

Transactional multi-file text edit validation and application.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Validate(edits)` | `Map(Seq)` | Check edit records without writing files |
| `Apply(edits)` | `Map(Seq)` | Validate and apply edits all-or-nothing |

Each edit record is a `Map` with `file`, `startLine`, `startColumn`, `endLine`, `endColumn`, `newText`, and optional `expectedMtime`. Line and column values are 1-based. The result map includes `success`, `applied`, and `diagnostics`.

Validation rejects missing files, invalid ranges, overlapping edits in the same file, and stale `expectedMtime` values. `Apply` writes all changed files only after validation succeeds and restores original contents if a later write fails.

---


## See Also

- [Streams & Buffers](streams.md)
- [Advanced IO](advanced.md)
- [Input & Output Overview](README.md)
- [Viper Runtime Library](../README.md)
