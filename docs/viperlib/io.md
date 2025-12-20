# Input/Output

> File system operations and stream-based I/O.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.IO.BinFile](#viperiobinfile)
- [Viper.IO.Dir](#viperiodir)
- [Viper.IO.File](#viperiofile)
- [Viper.IO.LineReader](#viperiolinereader)
- [Viper.IO.LineWriter](#viperiolinewriter)
- [Viper.IO.MemStream](#viperiomemstream)
- [Viper.IO.Path](#viperiopath)
- [Viper.IO.Watcher](#viperiowatcher)

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

| Property | Type    | Description                          |
|----------|---------|--------------------------------------|
| `Pos`    | Integer | Current file position (read-only)    |
| `Size`   | Integer | Total file size in bytes (read-only) |
| `Eof`    | Boolean | True if at end of file (read-only)   |

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
| `ReadBytes(path)`             | `Bytes(String)`        | Reads the entire file as binary data                                                      |
| `WriteBytes(path, bytes)`     | `Void(String, Bytes)`  | Writes binary data to a file                                                              |
| `ReadAllBytes(path)`          | `Bytes(String)`        | Reads the entire file as binary data (traps on I/O errors)                                |
| `WriteAllBytes(path, bytes)`  | `Void(String, Bytes)`  | Writes binary data to a file (overwrites; traps on I/O errors)                            |
| `ReadLines(path)`             | `Seq(String)`          | Reads the file as a sequence of lines                                                     |
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

## Viper.IO.LineReader

Line-by-line text file reader with support for multiple line ending conventions.

**Type:** Instance class

**Constructor:** `Viper.IO.LineReader.Open(path)`

### Line Endings

LineReader automatically handles all common line ending formats:

| Format | Characters | Description      |
|--------|------------|------------------|
| LF     | `\n`       | Unix/Linux/macOS |
| CR     | `\r`       | Classic Mac      |
| CRLF   | `\r\n`     | Windows          |

### Properties

| Property | Type    | Description                        |
|----------|---------|------------------------------------|
| `Eof`    | Boolean | True if at end of file (read-only) |

### Methods

| Method       | Returns | Description                                                  |
|--------------|---------|--------------------------------------------------------------|
| `Close()`    | void    | Close the file and release resources                         |
| `Read()`     | String  | Read one line (without newline); returns empty string at EOF |
| `ReadChar()` | Integer | Read single character (0-255) or -1 at EOF                   |
| `PeekChar()` | Integer | View next character without consuming (0-255 or -1)          |
| `ReadAll()`  | String  | Read all remaining content as a string                       |

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

Buffered text file writer with configurable line endings.

**Type:** Instance class

**Constructors:**

- `Viper.IO.LineWriter.Open(path)` - Create or overwrite file
- `Viper.IO.LineWriter.Append(path)` - Open for appending

### Properties

| Property  | Type   | Description                                           |
|-----------|--------|-------------------------------------------------------|
| `NewLine` | String | Line ending string (read/write, defaults to platform) |

### Methods

| Method          | Returns | Description                          |
|-----------------|---------|--------------------------------------|
| `Close()`       | void    | Close the file and release resources |
| `Write(text)`   | void    | Write string without newline         |
| `WriteLn(text)` | void    | Write string followed by newline     |
| `WriteChar(ch)` | void    | Write single character (0-255)       |
| `Flush()`       | void    | Flush buffered output to disk        |

### Platform Newlines

| Platform         | Default NewLine |
|------------------|-----------------|
| Windows          | `\r\n` (CRLF)   |
| Unix/Linux/macOS | `\n` (LF)       |

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

## Viper.IO.MemStream

In-memory binary stream for reading and writing raw bytes with auto-expanding buffer.

**Type:** Instance class

**Constructors:**

- `Viper.IO.MemStream.New()` - Creates an empty stream with default capacity
- `Viper.IO.MemStream.New(capacity)` - Creates an empty stream with specified initial capacity
- `Viper.IO.MemStream.FromBytes(bytes)` - Creates a stream initialized with data from a Bytes object

### Properties

| Property   | Type    | Access     | Description                      |
|------------|---------|------------|----------------------------------|
| `Pos`      | Integer | Read/Write | Current stream position          |
| `Len`      | Integer | Read-only  | Current data length in bytes     |
| `Capacity` | Integer | Read-only  | Current buffer capacity in bytes |

### Methods

| Method            | Returns | Description                                      |
|-------------------|---------|--------------------------------------------------|
| `ReadI8()`        | Integer | Read signed 8-bit integer                        |
| `WriteI8(value)`  | void    | Write signed 8-bit integer                       |
| `ReadU8()`        | Integer | Read unsigned 8-bit integer                      |
| `WriteU8(value)`  | void    | Write unsigned 8-bit integer                     |
| `ReadI16()`       | Integer | Read signed 16-bit integer (little-endian)       |
| `WriteI16(value)` | void    | Write signed 16-bit integer (little-endian)      |
| `ReadU16()`       | Integer | Read unsigned 16-bit integer (little-endian)     |
| `WriteU16(value)` | void    | Write unsigned 16-bit integer (little-endian)    |
| `ReadI32()`       | Integer | Read signed 32-bit integer (little-endian)       |
| `WriteI32(value)` | void    | Write signed 32-bit integer (little-endian)      |
| `ReadU32()`       | Integer | Read unsigned 32-bit integer (little-endian)     |
| `WriteU32(value)` | void    | Write unsigned 32-bit integer (little-endian)    |
| `ReadI64()`       | Integer | Read signed 64-bit integer (little-endian)       |
| `WriteI64(value)` | void    | Write signed 64-bit integer (little-endian)      |
| `ReadF32()`       | Float   | Read 32-bit float (IEEE 754, little-endian)      |
| `WriteF32(value)` | void    | Write 32-bit float (IEEE 754, little-endian)     |
| `ReadF64()`       | Float   | Read 64-bit float (IEEE 754, little-endian)      |
| `WriteF64(value)` | void    | Write 64-bit float (IEEE 754, little-endian)     |
| `ReadBytes(n)`    | Bytes   | Read n bytes into a new Bytes object             |
| `WriteBytes(b)`   | void    | Write all bytes from a Bytes object              |
| `ReadStr(n)`      | String  | Read n bytes as a UTF-8 string                   |
| `WriteStr(s)`     | void    | Write string as UTF-8 bytes                      |
| `ToBytes()`       | Bytes   | Copy all data to a new Bytes object              |
| `Clear()`         | void    | Reset stream to empty state (Pos=0, Len=0)       |
| `Seek(pos)`       | void    | Set position absolutely                          |
| `Skip(n)`         | void    | Move position forward by n bytes                 |

### Byte Order

All multi-byte integers and floats use **little-endian** byte order. This matches the native byte order on x86/x64 and ARM processors, and is the standard for most binary file formats.

### Buffer Behavior

- **Auto-expansion:** Writing beyond current capacity automatically grows the buffer
- **Gap filling:** Writing past the current length fills the gap with zeros
- **Read traps:** Reading past the end of data traps with an error

### Example

```basic
' Create a new memory stream
DIM ms AS OBJECT = Viper.IO.MemStream.New()

' Write various data types
ms.WriteI32(12345)       ' 4 bytes
ms.WriteF64(3.14159)     ' 8 bytes
ms.WriteStr("Hello")     ' 5 bytes

PRINT "Length:"; ms.Len  ' Output: 17

' Seek back to start to read
ms.Seek(0)

' Read the data back
DIM intVal AS INTEGER = ms.ReadI32()
DIM floatVal AS FLOAT = ms.ReadF64()
DIM strVal AS STRING = ms.ReadStr(5)

PRINT intVal     ' Output: 12345
PRINT floatVal   ' Output: 3.14159
PRINT strVal     ' Output: Hello
```

### Binary Protocol Example

```basic
' Create a packet with header and payload
DIM packet AS OBJECT = Viper.IO.MemStream.New()

' Write packet header
packet.WriteU8(&HCA)     ' Magic byte 1
packet.WriteU8(&HFE)     ' Magic byte 2
packet.WriteU16(1)       ' Version
packet.WriteU32(0)       ' Payload length (placeholder)

' Remember header end position
DIM headerEnd AS INTEGER = packet.Pos

' Write payload
packet.WriteStr("Hello, World!")

' Calculate and update payload length
DIM payloadLen AS INTEGER = packet.Len - headerEnd
packet.Seek(4)           ' Position of length field
packet.WriteU32(payloadLen)

' Get final packet as bytes
DIM data AS OBJECT = packet.ToBytes()
PRINT "Packet size:"; data.Len  ' Output: 21
```

### FromBytes Example

```basic
' Load binary data into a stream for parsing
DIM rawData AS OBJECT = Viper.IO.File.ReadAllBytes("data.bin")
DIM ms AS OBJECT = Viper.IO.MemStream.FromBytes(rawData)

' Parse the binary format
DIM magic AS INTEGER = ms.ReadU32()
DIM count AS INTEGER = ms.ReadI16()

FOR i AS INTEGER = 0 TO count - 1
    DIM value AS FLOAT = ms.ReadF64()
    PRINT "Value"; i; ":"; value
NEXT i
```

### Preallocated Capacity Example

```basic
' Preallocate buffer for known size
DIM ms AS OBJECT = Viper.IO.MemStream.New(1024)

PRINT "Initial capacity:"; ms.Capacity  ' Output: 1024
PRINT "Initial length:"; ms.Len         ' Output: 0

' Write data - no reallocation needed if under capacity
FOR i AS INTEGER = 0 TO 99
    ms.WriteI32(i)
NEXT i

PRINT "Final length:"; ms.Len  ' Output: 400
```

### Use Cases

- **Binary protocols:** Build and parse network packets, file formats
- **Serialization:** Convert structured data to/from bytes
- **Buffer management:** Accumulate binary data before writing to file
- **Testing:** Create test data for binary file parsers
- **Data transformation:** Convert between formats in memory

### Comparison with BinFile

| Feature           | MemStream            | BinFile                    |
|-------------------|----------------------|----------------------------|
| Storage           | In-memory buffer     | Disk file                  |
| Auto-expand       | Yes                  | No (file grows on write)   |
| Random access     | Yes (via Pos/Seek)   | Yes (via Seek)             |
| Persistence       | No                   | Yes                        |
| Max size          | Limited by memory    | Limited by disk            |
| Performance       | Very fast            | Slower (disk I/O)          |

Use MemStream when:
- Building binary data in memory before writing to file or network
- Parsing binary data already loaded in memory
- Performance is critical and data fits in memory

Use BinFile when:
- Working with files that are too large for memory
- Data must persist across program runs
- Random access to large files is needed

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

## Viper.IO.Watcher

Cross-platform file system watcher for monitoring files and directories for changes.

**Type:** Instance class

**Constructor:** `Viper.IO.Watcher.New(path)` - Creates a watcher for the specified file or directory

### Event Types

| Constant        | Value | Description                         |
|-----------------|-------|-------------------------------------|
| `EVENT_NONE`    | 0     | No event (returned by Poll timeout) |
| `EVENT_CREATED` | 1     | File or directory was created       |
| `EVENT_MODIFIED`| 2     | File was modified                   |
| `EVENT_DELETED` | 3     | File or directory was deleted       |
| `EVENT_RENAMED` | 4     | File or directory was renamed       |

### Properties

| Property      | Type    | Description                                    |
|---------------|---------|------------------------------------------------|
| `Path`        | String  | The path being watched (read-only)             |
| `IsWatching`  | Boolean | True if actively watching (read-only)          |
| `EVENT_NONE`  | Integer | Event constant: No event (static, read-only)   |
| `EVENT_CREATED` | Integer | Event constant: Created (static, read-only)  |
| `EVENT_MODIFIED`| Integer | Event constant: Modified (static, read-only) |
| `EVENT_DELETED` | Integer | Event constant: Deleted (static, read-only)  |
| `EVENT_RENAMED` | Integer | Event constant: Renamed (static, read-only)  |

### Methods

| Method          | Returns | Description                                              |
|-----------------|---------|----------------------------------------------------------|
| `Start()`       | void    | Begin watching for file system events                    |
| `Stop()`        | void    | Stop watching for events                                 |
| `Poll()`        | Integer | Check for event (non-blocking); returns event type or 0  |
| `PollFor(ms)`   | Integer | Wait up to ms milliseconds for event; returns event type |
| `EventPath()`   | String  | Get the path of the file that triggered the last event   |
| `EventType()`   | Integer | Get the type of the last polled event                    |

### Platform Implementation

| Platform | Backend API                |
|----------|----------------------------|
| Linux    | inotify                    |
| macOS    | kqueue                     |
| Windows  | ReadDirectoryChangesW      |

### Example

```basic
' Watch a directory for changes
DIM watcher AS OBJECT = Viper.IO.Watcher.New("/home/user/documents")

' Start watching
watcher.Start()

' Check if we're watching
PRINT "Watching:"; watcher.IsWatching  ' Output: 1

' Main event loop
DO
    ' Poll with 1 second timeout
    DIM event AS INTEGER = watcher.PollFor(1000)

    IF event <> watcher.EVENT_NONE THEN
        DIM path AS STRING = watcher.EventPath()

        SELECT CASE event
            CASE watcher.EVENT_CREATED
                PRINT "Created: "; path
            CASE watcher.EVENT_MODIFIED
                PRINT "Modified: "; path
            CASE watcher.EVENT_DELETED
                PRINT "Deleted: "; path
            CASE watcher.EVENT_RENAMED
                PRINT "Renamed: "; path
        END SELECT
    END IF
LOOP UNTIL shouldStop

' Stop watching
watcher.Stop()
```

### Non-Blocking Example

```basic
' Watch a single file
DIM watcher AS OBJECT = Viper.IO.Watcher.New("/home/user/config.txt")
watcher.Start()

' Non-blocking poll in a game loop
DO
    ' Do other work...
    ProcessGame()

    ' Quick non-blocking check for file changes
    DIM event AS INTEGER = watcher.Poll()
    IF event = watcher.EVENT_MODIFIED THEN
        PRINT "Config file changed, reloading..."
        ReloadConfig()
    END IF
LOOP

watcher.Stop()
```

### Use Cases

- **Hot reload:** Watch config files and reload when changed
- **Build systems:** Trigger rebuilds when source files change
- **File sync:** Monitor directories for new files to process
- **Development tools:** Auto-refresh on file changes
- **Backup software:** Detect changes to back up

### Notes

- Creating a watcher traps if the path does not exist
- The watcher must be started with `Start()` before events can be received
- `Poll()` returns immediately with `EVENT_NONE` if no event is pending
- `PollFor(ms)` waits up to the specified milliseconds for an event
- After receiving an event, use `EventPath()` and `EventType()` to get details
- Multiple events may be queued; call `Poll()` repeatedly to drain them
- Platform-specific behavior may vary slightly for edge cases

