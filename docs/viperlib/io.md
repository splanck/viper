# Input/Output

> File system operations and stream-based I/O.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.IO.File](#viperiofile)
- [Viper.IO.Path](#viperiopath)
- [Viper.IO.Dir](#viperiodir)
- [Viper.IO.BinFile](#viperiobinfile)
- [Viper.IO.LineReader](#viperiolinereader)
- [Viper.IO.LineWriter](#viperiolinewriter)

---

## Viper.IO.File

File system operations.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Exists(path)` | `Boolean(String)` | Returns true if the file exists |
| `ReadAllText(path)` | `String(String)` | Reads the entire file contents as a string |
| `WriteAllText(path, content)` | `Void(String, String)` | Writes a string to a file (overwrites if exists) |
| `Delete(path)` | `Void(String)` | Deletes a file |
| `Copy(src, dst)` | `Void(String, String)` | Copies a file from src to dst |
| `Move(src, dst)` | `Void(String, String)` | Moves/renames a file from src to dst |
| `Size(path)` | `Integer(String)` | Returns file size in bytes, or -1 if not found |
| `ReadBytes(path)` | `Bytes(String)` | Reads the entire file as binary data |
| `WriteBytes(path, bytes)` | `Void(String, Bytes)` | Writes binary data to a file |
| `ReadAllBytes(path)` | `Bytes(String)` | Reads the entire file as binary data (traps on I/O errors) |
| `WriteAllBytes(path, bytes)` | `Void(String, Bytes)` | Writes binary data to a file (overwrites; traps on I/O errors) |
| `ReadLines(path)` | `Seq(String)` | Reads the file as a sequence of lines |
| `WriteLines(path, lines)` | `Void(String, Seq)` | Writes a sequence of strings as lines |
| `Append(path, text)` | `Void(String, String)` | Appends text to a file |
| `AppendLine(path, text)` | `Void(String, String)` | Appends text followed by `\n` to a file (creates if missing) |
| `ReadAllLines(path)` | `Seq(String)` | Reads file as a sequence of lines; strips `\n` / `\r\n` terminators (traps on I/O errors) |
| `Modified(path)` | `Integer(String)` | Returns file modification time as Unix timestamp |
| `Touch(path)` | `Void(String)` | Creates file or updates its modification time |

### Notes

- `AppendLine` always appends a single `\n` byte (no platform newline normalization).
- `ReadAllLines` splits on `\n` and `\r\n` and does not include line endings in returned strings; a trailing line ending does not add an extra empty final line.
- `ReadAllBytes`, `WriteAllBytes`, and `ReadAllLines` trap (write a diagnostic to stderr and terminate) on I/O errors.

### Example

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

## Viper.IO.Path

---

## Viper.IO.Path

Cross-platform path manipulation utilities. All functions work with both Unix (`/`) and Windows (`\`) path separators.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Join(a, b)` | `String(String, String)` | Joins two path components with the platform separator |
| `Dir(path)` | `String(String)` | Returns the directory portion of a path |
| `Name(path)` | `String(String)` | Returns the filename portion of a path |
| `Stem(path)` | `String(String)` | Returns the filename without extension |
| `Ext(path)` | `String(String)` | Returns the file extension (including the dot) |
| `WithExt(path, ext)` | `String(String, String)` | Replaces the extension of a path |
| `IsAbs(path)` | `Boolean(String)` | Returns true if the path is absolute |
| `Abs(path)` | `String(String)` | Converts a relative path to absolute |
| `Norm(path)` | `String(String)` | Normalizes a path (removes `.`, `..`, duplicate separators) |
| `Sep()` | `String()` | Returns the platform-specific path separator |

### Example

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

| Behavior | Unix | Windows |
|----------|------|---------|
| Path separator | `/` | `\` |
| Absolute path detection | Starts with `/` | Starts with `C:\` or `\\` |
| Example absolute path | `/home/user` | `C:\Users\user` |

### Use Cases

- **Building file paths:** Use `Join()` to create paths safely
- **Extracting components:** Use `Dir()`, `Name()`, `Stem()`, `Ext()` to parse paths
- **Changing extensions:** Use `WithExt()` to replace file extensions
- **Cleaning paths:** Use `Norm()` to clean up user-provided paths
- **Portable code:** Use `Sep()` for platform-specific separators

---

## Viper.IO.Dir

---

## Viper.IO.Dir

Cross-platform directory operations for creating, removing, listing, and navigating directories.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Exists(path)` | `Boolean(String)` | Returns true if the directory exists |
| `Make(path)` | `Void(String)` | Creates a single directory (parent must exist) |
| `MakeAll(path)` | `Void(String)` | Creates a directory and all parent directories |
| `Remove(path)` | `Void(String)` | Removes an empty directory |
| `RemoveAll(path)` | `Void(String)` | Recursively removes a directory and all its contents |
| `Entries(path)` | `Seq(String)` | Returns directory entries (files + subdirectories); traps if the directory does not exist |
| `List(path)` | `Seq(String)` | Returns all entries in a directory (excluding `.` and `..`) |
| `ListSeq(path)` | `Seq(String)` | Seq-returning alias of `List(path)` (same semantics) |
| `Files(path)` | `Seq(String)` | Returns only files in a directory (no subdirectories) |
| `FilesSeq(path)` | `Seq(String)` | Seq-returning alias of `Files(path)` (same semantics) |
| `Dirs(path)` | `Seq(String)` | Returns only subdirectories in a directory |
| `DirsSeq(path)` | `Seq(String)` | Seq-returning alias of `Dirs(path)` (same semantics) |
| `Current()` | `String()` | Returns the current working directory |
| `SetCurrent(path)` | `Void(String)` | Changes the current working directory |
| `Move(src, dst)` | `Void(String, String)` | Moves/renames a directory |

**Note:** `Entries()`, `List()`, `Files()`, and `Dirs()` return entry names (not full paths). Use `Viper.IO.Path.Join(dir, name)` to build full paths when needed.

### Example

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

| Function | Returns | Includes |
|----------|---------|----------|
| `List(path)` | All entries | Files and subdirectories |
| `Files(path)` | Files only | Regular files, no directories |
| `Dirs(path)` | Directories only | Subdirectories, no files |

The `ListSeq()`/`FilesSeq()`/`DirsSeq()` variants are equivalent Seq-returning aliases for these legacy `ptr(str)` APIs.
Use the `*Seq` forms when a frontend or toolchain stage requires an object-typed `Seq` result explicitly.

```basic
DIM names AS Viper.Collections.Seq
names = Viper.IO.Dir.ListSeq("/home/user")
PRINT names.Len
```

All listing functions exclude `.` and `..` entries. If the directory doesn't exist or can't be read, an empty sequence is returned.

### Use Cases

- **File management:** List, copy, move, and delete directories
- **Build systems:** Create output directories with `MakeAll()`
- **Cleanup:** Remove temporary directories with `RemoveAll()`
- **Navigation:** Get and set the working directory
- **Filtering:** Separate files from subdirectories with `Files()` and `Dirs()`

---

## Viper.IO.BinFile

---

## Viper.IO.BinFile

Binary file stream for reading and writing raw bytes with random access capabilities.

**Type:** Instance class

**Constructor:** `Viper.IO.BinFile.Open(path, mode)`

### Open Modes

| Mode | Description |
|------|-------------|
| `"r"` | Read only (file must exist) |
| `"w"` | Write only (creates or truncates) |
| `"rw"` | Read and write (file must exist) |
| `"a"` | Append (creates if needed, writes at end) |

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Pos` | Integer | Current file position (read-only) |
| `Size` | Integer | Total file size in bytes (read-only) |
| `Eof` | Boolean | True if at end of file (read-only) |

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `Close()` | void | Close the file and release resources |
| `Read(bytes, offset, count)` | Integer | Read up to count bytes into Bytes object at offset; returns bytes read |
| `Write(bytes, offset, count)` | void | Write count bytes from Bytes object starting at offset |
| `ReadByte()` | Integer | Read single byte (0-255) or -1 at EOF |
| `WriteByte(value)` | void | Write single byte (0-255) |
| `Seek(offset, origin)` | Integer | Seek to position; returns new position |
| `Flush()` | void | Flush buffered writes to disk |

### Seek Origins

| Origin | Description |
|--------|-------------|
| `0` | From beginning of file (SEEK_SET) |
| `1` | From current position (SEEK_CUR) |
| `2` | From end of file (SEEK_END) |

### Example

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

## Viper.IO.LineReader

---

## Viper.IO.LineReader

Line-by-line text file reader with support for multiple line ending conventions.

**Type:** Instance class

**Constructor:** `Viper.IO.LineReader.Open(path)`

### Line Endings

LineReader automatically handles all common line ending formats:

| Format | Characters | Description |
|--------|------------|-------------|
| LF | `\n` | Unix/Linux/macOS |
| CR | `\r` | Classic Mac |
| CRLF | `\r\n` | Windows |

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Eof` | Boolean | True if at end of file (read-only) |

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `Close()` | void | Close the file and release resources |
| `Read()` | String | Read one line (without newline); returns empty string at EOF |
| `ReadChar()` | Integer | Read single character (0-255) or -1 at EOF |
| `PeekChar()` | Integer | View next character without consuming (0-255 or -1) |
| `ReadAll()` | String | Read all remaining content as a string |

### Example

```basic
' Read a file line by line
DIM reader AS OBJECT = Viper.IO.LineReader.Open("data.txt")

DO WHILE NOT reader.Eof
    DIM line AS STRING = reader.Read()
    IF NOT reader.Eof THEN
        PRINT line
    END IF
LOOP

reader.Close()

' Character-by-character reading
reader = Viper.IO.LineReader.Open("chars.txt")

DO WHILE NOT reader.Eof
    DIM ch AS INTEGER = reader.ReadChar()
    IF ch >= 0 THEN
        PRINT CHR(ch);
    END IF
LOOP

reader.Close()

' Peek at next character without consuming
reader = Viper.IO.LineReader.Open("peek.txt")

' Peek and read
DIM nextChar AS INTEGER = reader.PeekChar()
PRINT "Next char will be: "; CHR(nextChar)

DIM actualChar AS INTEGER = reader.ReadChar()
PRINT "Read char: "; CHR(actualChar)   ' Same as peeked

reader.Close()

' Read entire remaining file content
reader = Viper.IO.LineReader.Open("large.txt")

' Skip first line
DIM header AS STRING = reader.Read()

' Read everything else
DIM content AS STRING = reader.ReadAll()
PRINT "Remaining content length: "; LEN(content)

reader.Close()
```

### Use Cases

- **Text file processing:** Process files line by line
- **Log file reading:** Parse log files with various line endings
- **Configuration parsing:** Read config files line by line
- **Character-level parsing:** Build custom parsers with PeekChar/ReadChar
- **Cross-platform files:** Handle files with different line ending conventions

---

## Viper.IO.LineWriter

---

## Viper.IO.LineWriter

Buffered text file writer with configurable line endings.

**Type:** Instance class

**Constructors:**
- `Viper.IO.LineWriter.Open(path)` - Create or overwrite file
- `Viper.IO.LineWriter.Append(path)` - Open for appending

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `NewLine` | String | Line ending string (read/write, defaults to platform) |

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `Close()` | void | Close the file and release resources |
| `Write(text)` | void | Write string without newline |
| `WriteLn(text)` | void | Write string followed by newline |
| `WriteChar(ch)` | void | Write single character (0-255) |
| `Flush()` | void | Flush buffered output to disk |

### Platform Newlines

| Platform | Default NewLine |
|----------|-----------------|
| Windows | `\r\n` (CRLF) |
| Unix/Linux/macOS | `\n` (LF) |

### Example

```basic
' Write text to a file
DIM writer AS OBJECT = Viper.IO.LineWriter.Open("output.txt")

' Write lines with automatic newline
writer.WriteLn("First line")
writer.WriteLn("Second line")

' Write without newline
writer.Write("No ")
writer.Write("newline ")
writer.Write("here")
writer.WriteLn("")  ' Add newline at end

writer.Close()

' Append to existing file
writer = Viper.IO.LineWriter.Append("output.txt")
writer.WriteLn("Appended line")
writer.Close()

' Custom line endings (Windows-style)
writer = Viper.IO.LineWriter.Open("windows.txt")
writer.NewLine = CHR(13) + CHR(10)  ' CRLF
writer.WriteLn("Windows line ending")
writer.Close()

' Unix-style line endings
writer = Viper.IO.LineWriter.Open("unix.txt")
writer.NewLine = CHR(10)  ' LF only
writer.WriteLn("Unix line ending")
writer.Close()

' Write individual characters
writer = Viper.IO.LineWriter.Open("chars.txt")
FOR i AS INTEGER = 65 TO 90  ' A-Z
    writer.WriteChar(i)
NEXT
writer.Close()
```

### Use Cases

- **Text file generation:** Create configuration files, reports, logs
- **Cross-platform output:** Control line endings for target platform
- **Log writing:** Append entries to log files
- **Data export:** Write CSV, TSV, or other text formats
- **Code generation:** Generate source code with proper line endings

---

## Viper.Graphics.Canvas

