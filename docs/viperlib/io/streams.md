# Streams & Buffers
> Stream, MemStream, LineReader, LineWriter, BinaryBuffer

**Part of [Viper Runtime Library](../README.md) › [Input & Output](README.md)**

---

## Viper.IO.Stream

Unified stream abstraction providing a common interface over file and memory streams.

**Type:** Instance class

**Constructors:**

- `Viper.IO.Stream.OpenFile(path, mode)` - Opens a file stream (wraps BinFile)
- `Viper.IO.Stream.OpenMemory()` - Opens a new in-memory stream (wraps MemStream)
- `Viper.IO.Stream.OpenBytes(bytes)` - Opens an in-memory stream initialized with data from a Bytes object
- `Viper.IO.Stream.FromBinFile(binFile)` - Wraps an existing BinFile object
- `Viper.IO.Stream.FromMemStream(memStream)` - Wraps an existing MemStream object

### Properties

| Property | Type    | Description                                               |
|----------|---------|-----------------------------------------------------------|
| `Type`   | Integer | Stream backing type: 0 = file, 1 = memory (read-only)    |
| `Pos`    | Integer | Current stream position (read/write)                      |
| `Len`    | Integer | Current data length in bytes (read-only)                  |
| `Eof`    | Boolean | True if at end of stream (read-only)                      |

### Methods

| Method              | Returns | Description                                      |
|---------------------|---------|--------------------------------------------------|
| `Read(count)`       | Bytes   | Read up to count bytes                           |
| `ReadAll()`         | Bytes   | Read all remaining bytes from current position   |
| `Write(bytes)`      | void    | Write bytes to stream                            |
| `ReadByte()`        | Integer | Read single byte (0-255) or -1 at EOF            |
| `WriteByte(value)`  | void    | Write single byte (0-255)                        |
| `Flush()`           | void    | Flush buffered writes                            |
| `Close()`           | void    | Close the stream and release resources           |
| `AsBinFile()`       | BinFile | Get the underlying BinFile (file streams only)   |
| `AsMemStream()`     | MemStream | Get the underlying MemStream (memory streams only) |
| `ToBytes()`         | Bytes   | Get all data as Bytes (memory streams only)      |

### Open Modes (for OpenFile)

| Mode   | Description                               |
|--------|-------------------------------------------|
| `"r"`  | Read only (file must exist)               |
| `"w"`  | Write only (creates or truncates)         |
| `"rw"` | Read and write (file must exist)          |
| `"a"`  | Append (creates if needed)                |

### Zia Example

> Stream is not yet fully accessible from Zia. `OpenFile`/`OpenMemory`/`OpenBytes` return untyped pointers, and `FromBinFile`/`FromMemStream` have runtime issues. Use `BinFile` or `MemStream` directly instead.

### BASIC Example

```basic
' Open a file stream
DIM fs AS OBJECT = Viper.IO.Stream.OpenFile("data.bin", "rw")

' Write some data
DIM data AS OBJECT = Viper.Collections.Bytes.FromString("Hello, Stream!")
fs.Write(data)

' Seek back and read (set Pos property to seek)
fs.Pos = 0
DIM readData AS OBJECT = fs.Read(14)
PRINT readData.ToStr()  ' Output: Hello, Stream!

fs.Close()

' Open a memory stream
DIM ms AS OBJECT = Viper.IO.Stream.OpenMemory()

' Write to memory
ms.Write(Viper.Collections.Bytes.FromString("In-memory data"))

' Get all data
DIM allData AS OBJECT = ms.ToBytes()
PRINT "Length:"; allData.Len

ms.Close()
```

### Polymorphic Processing Example

```basic
' Process data from any stream source
SUB ProcessStream(stream AS OBJECT)
    ' Read header
    DIM header AS OBJECT = stream.Read(4)
    PRINT "Header:"; header.ToHex()

    ' Read remaining data
    DO WHILE NOT stream.Eof
        DIM chunk AS OBJECT = stream.Read(1024)
        ProcessChunk(chunk)
    LOOP
END SUB

' Use with file
DIM fileStream AS OBJECT = Viper.IO.Stream.OpenFile("input.bin", "r")
ProcessStream(fileStream)
fileStream.Close()

' Use with memory (e.g., from network)
DIM memStream AS OBJECT = Viper.IO.Stream.OpenMemory()
memStream.Write(networkData)
memStream.Pos = 0
ProcessStream(memStream)
memStream.Close()
```

### Wrapping Existing Streams

```basic
' Wrap an existing BinFile
DIM bf AS OBJECT = Viper.IO.BinFile.Open("data.bin", "r")
DIM wrapped AS OBJECT = Viper.IO.Stream.FromBinFile(bf)

' Use the unified interface
DIM data AS OBJECT = wrapped.Read(100)

' The wrapped stream does NOT own the BinFile
' You must close the original BinFile separately
wrapped.Close()  ' Releases wrapper only
bf.Close()       ' Closes the actual file

' Wrap an existing MemStream
DIM ms AS OBJECT = Viper.IO.MemStream.New()
ms.WriteStr("test data")
ms.Seek(0)

DIM wrappedMem AS OBJECT = Viper.IO.Stream.FromMemStream(ms)
DIM str AS STRING = wrappedMem.Read(9).ToStr()
```

### Use Cases

- **Abstraction:** Write code that works with both files and memory
- **Testing:** Use memory streams in tests, files in production
- **Buffering:** Read network data into memory stream, process uniformly
- **Interoperability:** Wrap existing BinFile/MemStream for unified access

### Stream vs BinFile vs MemStream

| Feature           | Stream                         | BinFile        | MemStream           |
|-------------------|--------------------------------|----------------|---------------------|
| Unified interface | Yes                            | No             | No                  |
| File backing      | Via OpenFile/FromBinFile       | Yes            | No                  |
| Memory backing    | Via OpenMemory/OpenBytes/FromMemStream | No     | Yes                 |
| Polymorphic use   | Yes                            | No             | No                  |
| Ownership control | Yes (From* vs Open*)           | Always owns    | Always owns         |
| Best for          | Polymorphic I/O code           | Direct file I/O| In-memory buffering |

Use Stream when:
- You need code that works with both files and memory
- You want to abstract the data source from processing logic
- You're building libraries that accept stream-like inputs

Use BinFile/MemStream directly when:
- You know the specific backing type
- You need type-specific features
- Maximum performance is critical

---

## Viper.IO.MemStream

In-memory binary stream for reading and writing raw bytes with auto-expanding buffer.

**Type:** Instance class

**Constructors:**

- `Viper.IO.MemStream.New()` - Creates an empty stream with default capacity
- `Viper.IO.MemStream.NewCapacity(capacity)` - Creates an empty stream with specified initial capacity
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

### Zia Example

```rust
module MemStreamDemo;

bind Viper.Terminal;
bind Viper.IO.MemStream as MS;
bind Viper.Fmt as Fmt;

func start() {
    // Create a new memory stream
    var ms = MS.New();

    // Write various data types
    ms.WriteI32(12345);       // 4 bytes
    ms.WriteF64(3.14159);     // 8 bytes
    ms.WriteStr("Hello");     // 5 bytes

    Say("Length: " + Fmt.Int(ms.get_Len()));   // 17

    // Seek back to start and read
    ms.Seek(0);
    Say("Int: " + Fmt.Int(ms.ReadI32()));      // 12345
    Say("Float: " + Fmt.Num(ms.ReadF64()));    // 3.14159
    Say("Str: " + ms.ReadStr(5));              // Hello

    // Clear and reuse
    ms.Clear();
    Say("After clear: " + Fmt.Int(ms.get_Len()));  // 0
}
```

> **Note:** MemStream properties (`Pos`, `Len`, `Capacity`) use the get_/set_ pattern; access them as `ms.get_Len()`, `ms.get_Pos()`, `ms.set_Pos(n)` in Zia.

### BASIC Example

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
DIM ms AS OBJECT = Viper.IO.MemStream.NewCapacity(1024)

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

### Zia Example

```rust
module LineReaderDemo;

bind Viper.Terminal;
bind Viper.IO.LineReader as LR;
bind Viper.IO.File as File;

func start() {
    // Create a test file
    File.WriteAllText("/tmp/lr_test.txt", "Line 1\nLine 2\nLine 3\n");

    // Read line by line
    var reader = LR.Open("/tmp/lr_test.txt");
    Say(reader.Read());    // Line 1
    Say(reader.Read());    // Line 2
    Say(reader.Read());    // Line 3
    reader.Close();

    File.Delete("/tmp/lr_test.txt");
}
```

### BASIC Example

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

### Zia Example

```rust
module LineWriterDemo;

bind Viper.Terminal;
bind Viper.IO.LineWriter as LW;
bind Viper.IO.File as File;

func start() {
    // Write lines to a file
    var writer = LW.Open("/tmp/lw_test.txt");
    writer.WriteLn("First line");
    writer.WriteLn("Second line");
    writer.Write("No newline here");
    writer.Close();

    // Verify contents
    Say(File.ReadAllText("/tmp/lw_test.txt"));

    File.Delete("/tmp/lw_test.txt");
}
```

> **Note:** `LineWriter.Append()` is not currently accessible from Zia. Use `File.Append()` or `File.AppendLine()` for append operations.

### BASIC Example

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

## Viper.IO.BinaryBuffer

Positioned binary read/write buffer for constructing and parsing binary data in memory. Maintains an internal cursor position that advances automatically with each read or write.

**Type:** Instance class

**Constructors:**

- `Viper.IO.BinaryBuffer.New()` - Creates an empty buffer with default capacity
- `Viper.IO.BinaryBuffer.NewCap(capacity)` - Creates an empty buffer with specified initial capacity
- `Viper.IO.BinaryBuffer.FromBytes(data)` - Creates a buffer initialized with data from a Bytes object

### Properties

| Property | Type    | Access     | Description                   |
|----------|---------|------------|-------------------------------|
| `Pos`    | Integer | Read/Write | Current buffer position       |
| `Len`    | Integer | Read-only  | Current data length in bytes  |

### Write Methods

| Method              | Returns | Description                                      |
|---------------------|---------|--------------------------------------------------|
| `WriteByte(v)`      | void    | Write a single byte (0-255) at current position  |
| `WriteI16LE(v)`     | void    | Write 16-bit integer (little-endian)             |
| `WriteI16BE(v)`     | void    | Write 16-bit integer (big-endian)                |
| `WriteI32LE(v)`     | void    | Write 32-bit integer (little-endian)             |
| `WriteI32BE(v)`     | void    | Write 32-bit integer (big-endian)                |
| `WriteI64LE(v)`     | void    | Write 64-bit integer (little-endian)             |
| `WriteI64BE(v)`     | void    | Write 64-bit integer (big-endian)                |
| `WriteStr(s)`       | void    | Write string as UTF-8 bytes                      |
| `WriteBytes(b)`     | void    | Write all bytes from a Bytes object              |

### Read Methods

| Method              | Returns | Description                                      |
|---------------------|---------|--------------------------------------------------|
| `ReadByte()`        | Integer | Read a single byte (0-255) at current position   |
| `ReadI16LE()`       | Integer | Read 16-bit integer (little-endian)              |
| `ReadI16BE()`       | Integer | Read 16-bit integer (big-endian)                 |
| `ReadI32LE()`       | Integer | Read 32-bit integer (little-endian)              |
| `ReadI32BE()`       | Integer | Read 32-bit integer (big-endian)                 |
| `ReadI64LE()`       | Integer | Read 64-bit integer (little-endian)              |
| `ReadI64BE()`       | Integer | Read 64-bit integer (big-endian)                 |
| `ReadStr()`         | String  | Read a length-prefixed string                    |
| `ReadBytes(count)`  | Bytes   | Read count bytes into a new Bytes object         |

### Control Methods

| Method       | Returns | Description                              |
|--------------|---------|------------------------------------------|
| `ToBytes()`  | Bytes   | Copy all data to a new Bytes object      |
| `Reset()`    | void    | Reset buffer to empty state (Pos=0, Len=0) |

### Zia Example

```rust
module BinaryBufferDemo;

bind Viper.Terminal;
bind Viper.IO.BinaryBuffer as BB;
bind Viper.Fmt as Fmt;

func start() {
    // Create a new buffer
    var buf = BB.New();

    // Write various data types
    buf.WriteByte(0xFF);
    buf.WriteI32LE(12345);
    buf.WriteI32BE(67890);
    buf.WriteStr("Hello");

    Say("Length: " + Fmt.Int(buf.get_Len()));

    // Seek back to start and read
    buf.set_Pos(0);
    Say("Byte: " + Fmt.Int(buf.ReadByte()));        // 255
    Say("I32LE: " + Fmt.Int(buf.ReadI32LE()));       // 12345
    Say("I32BE: " + Fmt.Int(buf.ReadI32BE()));       // 67890

    // Export to Bytes
    var data = buf.ToBytes();
    Say("Exported len: " + Fmt.Int(data.Len));
}
```

> **Note:** BinaryBuffer properties (`Pos`, `Len`) use the get_/set_ pattern in Zia; access them as `buf.get_Len()`, `buf.get_Pos()`, `buf.set_Pos(n)`.

### BASIC Example

```basic
' Create a new binary buffer
DIM buf AS OBJECT = Viper.IO.BinaryBuffer.New()

' Write various data types
buf.WriteByte(&HCA)
buf.WriteI16LE(1000)
buf.WriteI32BE(123456)
buf.WriteI64LE(9876543210)
buf.WriteStr("Hello")
buf.WriteBytes(Viper.Collections.Bytes.FromHex("deadbeef"))

PRINT "Length:"; buf.Len

' Seek back and read
buf.Pos = 0
PRINT "Byte:"; buf.ReadByte()       ' Output: 202
PRINT "I16LE:"; buf.ReadI16LE()     ' Output: 1000
PRINT "I32BE:"; buf.ReadI32BE()     ' Output: 123456
PRINT "I64LE:"; buf.ReadI64LE()     ' Output: 9876543210

' Export to Bytes
DIM data AS OBJECT = buf.ToBytes()
PRINT "Exported:"; data.Len

' Reset and reuse
buf.Reset()
PRINT "After reset:"; buf.Len       ' Output: 0
```

### Preallocated Capacity Example

```basic
' Preallocate buffer for a known packet size
DIM buf AS OBJECT = Viper.IO.BinaryBuffer.NewCap(256)

' Build a binary protocol packet
buf.WriteI16BE(&HCAFE)         ' Magic number
buf.WriteI32BE(1)              ' Version
buf.WriteStr("payload data")

DIM packet AS OBJECT = buf.ToBytes()
```

### FromBytes Example

```basic
' Parse binary data received from network
DIM rawData AS OBJECT = Viper.IO.File.ReadAllBytes("packet.bin")
DIM buf AS OBJECT = Viper.IO.BinaryBuffer.FromBytes(rawData)

' Parse the binary format
DIM magic AS INTEGER = buf.ReadI16BE()
DIM version AS INTEGER = buf.ReadI32BE()
DIM payload AS STRING = buf.ReadStr()
```

### BinaryBuffer vs MemStream

| Feature           | BinaryBuffer            | MemStream                    |
|-------------------|-------------------------|------------------------------|
| Endianness        | Both LE and BE methods  | Little-endian only           |
| Float support     | No                      | Yes (F32, F64)               |
| Auto-expand       | Yes                     | Yes                          |
| Random access     | Yes (via Pos)           | Yes (via Pos/Seek)           |
| Best for          | Network protocols, mixed-endian formats | Structured binary data, IEEE floats |

Use BinaryBuffer when:
- You need both big-endian and little-endian operations
- Building network protocol packets (many protocols use big-endian)
- Parsing binary formats with mixed byte orders

Use MemStream when:
- Working with floating-point data (F32, F64)
- All data is little-endian
- You need unsigned integer operations (U8, U16, U32)

### Use Cases

- **Network protocols:** Build and parse TCP/UDP packets with big-endian fields
- **File format parsing:** Read binary headers with mixed byte orders
- **Serialization:** Encode structured data for storage or transmission
- **Database pages:** Read and write fixed-format binary records

---


## See Also

- [Files & Directories](files.md)
- [Advanced IO](advanced.md)
- [Input & Output Overview](README.md)
- [Viper Runtime Library](../README.md)
