# Advanced IO
> Archive (zip/tar), Compress (gzip/zstd/lz4), Watcher (filesystem events)

**Part of [Viper Runtime Library](../README.md) › [Input & Output](README.md)**

---

## Viper.IO.Archive

ZIP archive reader and writer for creating, reading, and extracting ZIP files.

**Type:** Instance class

**Constructors:**

- `Viper.IO.Archive.Open(path)` - Opens an existing ZIP archive for reading
- `Viper.IO.Archive.Create(path)` - Creates a new ZIP archive for writing
- `Viper.IO.Archive.FromBytes(data)` - Opens a ZIP archive from in-memory Bytes object

### Properties

| Property | Type   | Description                                        |
|----------|--------|----------------------------------------------------|
| `Path`   | String | Path to the archive file (read-only)               |
| `Count`  | Integer| Number of entries in the archive (read-only)       |
| `Names`  | Seq    | Sequence of entry names in the archive (read-only) |

### Reading Methods

| Method                  | Returns | Description                                               |
|-------------------------|---------|-----------------------------------------------------------|
| `Has(name)`             | Boolean | Returns true if the archive contains an entry with name   |
| `Read(name)`            | Bytes   | Reads entry content as binary data                        |
| `ReadStr(name)`         | String  | Reads entry content as UTF-8 string                       |
| `Extract(name, path)`   | void    | Extracts a single entry to the specified path             |
| `ExtractAll(dir)`       | void    | Extracts all entries to the specified directory           |
| `Info(name)`            | Map     | Returns metadata for an entry (size, compressedSize, crc) |

### Writing Methods

| Method                    | Returns | Description                                           |
|---------------------------|---------|-------------------------------------------------------|
| `Add(name, data)`         | void    | Adds binary data as an entry (with compression)       |
| `AddStr(name, text)`      | void    | Adds a string as an entry (with compression)          |
| `AddFile(name, path)`     | void    | Adds a file from disk as an entry                     |
| `AddDir(name)`            | void    | Adds a directory entry (name should end with `/`)     |
| `Finish()`                | void    | Finalizes the archive (required after writing)        |

### Static Methods

| Method           | Returns | Description                                      |
|------------------|---------|--------------------------------------------------|
| `IsZip(path)`    | Boolean | Returns true if the file appears to be a ZIP    |
| `IsZipBytes(data)`| Boolean| Returns true if the bytes appear to be ZIP data |

### Compression

The archive uses DEFLATE compression (method 8) by default for added entries. Small entries or entries that don't compress well use stored mode (method 0). The implementation reads archives with any combination of stored and deflate-compressed entries.

### Entry Name Rules

- Names must be relative (no leading `/` or drive letters)
- `..` path segments are rejected
- `.` segments are ignored
- Backslashes are normalized to `/`

Invalid names trap with `Archive: invalid entry name`.

### Info Map Keys

| Key              | Type    | Description                           |
|------------------|---------|---------------------------------------|
| `size`           | Integer | Uncompressed size in bytes            |
| `compressedSize` | Integer | Compressed size in bytes              |
| `crc`            | Integer | CRC32 checksum                        |
| `method`         | Integer | Compression method (0=stored, 8=deflate) |
| `isDir`          | Boolean | True if entry is a directory          |

### Zia Example

```rust
module ArchiveDemo;

bind Viper.Terminal;
bind Viper.IO.Archive as Arc;
bind Viper.IO.File as File;
bind Viper.Fmt as Fmt;

func start() {
    // Create a new archive
    var arc = Arc.Create("/tmp/backup.zip");
    arc.AddStr("hello.txt", "Hello from Zia!");
    arc.AddStr("data.txt", "Some data content");
    arc.AddDir("subdir/");
    arc.Finish();
    Say("Archive created");

    // Check if it's a valid ZIP
    Say("IsZip: " + Fmt.Bool(Arc.IsZip("/tmp/backup.zip")));

    // Open and read
    var reader = Arc.Open("/tmp/backup.zip");
    Say("Count: " + Fmt.Int(reader.get_Count()));
    Say("Has hello.txt: " + Fmt.Bool(reader.Has("hello.txt")));
    Say("Content: " + reader.ReadStr("hello.txt"));

    // Clean up
    File.Delete("/tmp/backup.zip");
}
```

### BASIC Example

```basic
' Create a new archive
DIM arc AS OBJECT = Viper.IO.Archive.Create("backup.zip")

' Add files with different methods
arc.AddStr("readme.txt", "This is a readme file.")
arc.Add("data.bin", Viper.IO.File.ReadAllBytes("data.bin"))
arc.AddFile("config.json", "config.json")
arc.AddDir("logs/")

' Finalize the archive
arc.Finish()

' Read an existing archive
arc = Viper.IO.Archive.Open("backup.zip")

PRINT "Entries:"; arc.Count

' List all entries
DIM names AS OBJECT = arc.Names
FOR i = 0 TO names.Len - 1
    PRINT names.Get(i)
NEXT i

' Read specific entry
IF arc.Has("readme.txt") THEN
    DIM content AS STRING = arc.ReadStr("readme.txt")
    PRINT content
END IF

' Get entry information
DIM info AS OBJECT = arc.Info("data.bin")
PRINT "Size:"; info.Get("size")
PRINT "Compressed:"; info.Get("compressedSize")
```

### Extraction Example

```basic
' Extract a single file
DIM arc AS OBJECT = Viper.IO.Archive.Open("archive.zip")

' Extract one entry to a specific path
arc.Extract("docs/manual.pdf", "/home/user/manual.pdf")

' Extract all entries to a directory
arc.ExtractAll("/home/user/extracted")
```

### In-Memory Archive Example

```basic
' Work with ZIP data in memory (e.g., from network)
DIM zipData AS OBJECT = DownloadZip("https://example.com/data.zip")

' Check if it's valid ZIP data
IF Viper.IO.Archive.IsZipBytes(zipData) THEN
    DIM arc AS OBJECT = Viper.IO.Archive.FromBytes(zipData)

    ' Process entries
    DIM names AS OBJECT = arc.Names
    FOR i = 0 TO names.Len - 1
        DIM content AS OBJECT = arc.Read(names.Get(i))
        ProcessEntry(names.Get(i), content)
    NEXT i
END IF
```

### Validation Example

```basic
' Check if a file is a valid ZIP before opening
IF Viper.IO.Archive.IsZip("download.zip") THEN
    DIM arc AS OBJECT = Viper.IO.Archive.Open("download.zip")
    PRINT "Valid ZIP with"; arc.Count; "entries"
ELSE
    PRINT "Not a valid ZIP file"
END IF
```

### Error Handling

Archive operations trap on errors:

- `Open()` traps if file doesn't exist or isn't a valid ZIP
- `Read()`/`ReadStr()` trap if entry doesn't exist
- `Extract()` traps if entry doesn't exist or destination is unwritable
- `Finish()` must be called before the archive file is valid
- Invalid or corrupted entries may trap during reading

### ZIP Format Support

- **Supported formats:** ZIP32 (standard ZIP format)
- **Compression methods:** Stored (0), Deflate (8)
- **Features:** Directory entries, file attributes, CRC32 validation
- **Limitations:** ZIP64 not supported, encryption not supported

### Use Cases

- **File distribution:** Create ZIP archives for downloading
- **Data backup:** Archive multiple files into a single compressed file
- **In-memory processing:** Work with ZIP data without disk I/O
- **Extraction:** Unpack downloaded or received ZIP files
- **Integration:** Read/write ZIP files for interoperability with other tools

---

## Viper.IO.Compress

DEFLATE and GZIP compression/decompression utilities with zero external dependencies.

**Type:** Static utility class

### Methods

| Method                  | Signature              | Description                                               |
|-------------------------|------------------------|-----------------------------------------------------------|
| `Deflate(data)`         | `Bytes(Bytes)`         | Compress bytes using DEFLATE (default level 6)            |
| `DeflateLvl(data, lvl)` | `Bytes(Bytes, Integer)`| Compress bytes using DEFLATE with specified level (1-9)   |
| `Inflate(data)`         | `Bytes(Bytes)`         | Decompress DEFLATE-compressed bytes                       |
| `Gzip(data)`            | `Bytes(Bytes)`         | Compress bytes using GZIP format (default level 6)        |
| `GzipLvl(data, lvl)`    | `Bytes(Bytes, Integer)`| Compress bytes using GZIP with specified level (1-9)      |
| `Gunzip(data)`          | `Bytes(Bytes)`         | Decompress GZIP-compressed bytes                          |
| `DeflateStr(text)`      | `Bytes(String)`        | Compress string using DEFLATE                             |
| `InflateStr(data)`      | `String(Bytes)`        | Decompress DEFLATE-compressed bytes to string             |
| `GzipStr(text)`         | `Bytes(String)`        | Compress string using GZIP format                         |
| `GunzipStr(data)`       | `String(Bytes)`        | Decompress GZIP-compressed bytes to string                |

### Compression Levels

| Level | Speed     | Compression | Use Case                     |
|-------|-----------|-------------|------------------------------|
| 1     | Fastest   | Minimal     | Real-time compression        |
| 6     | Balanced  | Good        | General purpose (default)    |
| 9     | Slowest   | Maximum     | Archival, bandwidth-limited  |

### DEFLATE vs GZIP

| Format  | Description                                                        |
|---------|--------------------------------------------------------------------|
| DEFLATE | Raw compressed data stream (RFC 1951)                              |
| GZIP    | DEFLATE with header, CRC32, and size footer (RFC 1952)             |

Use GZIP when:
- Interoperating with `.gz` files or HTTP gzip encoding
- Data integrity verification is needed (CRC32)

Use DEFLATE when:
- Building custom formats with your own framing
- Minimal overhead is required
- Used as part of another format (ZIP, PNG, etc.)

### Zia Example

```rust
module CompressDemo;

bind Viper.Terminal;
bind Viper.IO.Compress as Comp;

func start() {
    // Compress a string with DEFLATE
    var compressed = Comp.DeflateStr("Hello, World! This is a test of compression.");
    Say("Compressed");

    // Decompress back to string
    var restored = Comp.InflateStr(compressed);
    Say("Restored: " + restored);

    // GZIP compression (compatible with gzip command-line tool)
    var gzipped = Comp.GzipStr("Gzip compressed data");
    var gunzipped = Comp.GunzipStr(gzipped);
    Say("Gzip roundtrip: " + gunzipped);
}
```

### BASIC Example

```basic
' Compress binary data
DIM original AS OBJECT = Viper.Collections.Bytes.FromString("Hello, World!")
DIM compressed AS OBJECT = Viper.IO.Compress.Deflate(original)
DIM restored AS OBJECT = Viper.IO.Compress.Inflate(compressed)

PRINT "Original:"; original.Len     ' Output: 13
PRINT "Compressed:"; compressed.Len ' Output: varies
PRINT "Restored:"; restored.Len     ' Output: 13

' Compress with higher compression level
DIM maxCompressed AS OBJECT = Viper.IO.Compress.DeflateLvl(original, 9)

' GZIP format (compatible with gzip command-line tool)
DIM gzipped AS OBJECT = Viper.IO.Compress.Gzip(original)
DIM gunzipped AS OBJECT = Viper.IO.Compress.Gunzip(gzipped)

' Verify GZIP magic bytes
PRINT HEX(gzipped.Get(0)); HEX(gzipped.Get(1))  ' Output: 1F8B
```

### String Convenience Methods

```basic
' Compress a string directly
DIM text AS STRING = "The quick brown fox jumps over the lazy dog."
DIM compressed AS OBJECT = Viper.IO.Compress.GzipStr(text)

' Decompress back to string
DIM restored AS STRING = Viper.IO.Compress.GunzipStr(compressed)
PRINT restored  ' Output: The quick brown fox jumps over the lazy dog.

' DEFLATE string variants
DIM deflated AS OBJECT = Viper.IO.Compress.DeflateStr(text)
DIM inflated AS STRING = Viper.IO.Compress.InflateStr(deflated)
```

### File Compression Example

```basic
' Compress a file to .gz format
DIM content AS OBJECT = Viper.IO.File.ReadAllBytes("data.txt")
DIM compressed AS OBJECT = Viper.IO.Compress.Gzip(content)
Viper.IO.File.WriteAllBytes("data.txt.gz", compressed)

' Decompress a .gz file
DIM gzData AS OBJECT = Viper.IO.File.ReadAllBytes("archive.gz")
DIM original AS OBJECT = Viper.IO.Compress.Gunzip(gzData)
Viper.IO.File.WriteAllBytes("archive", original)
```

### Error Handling

Compression traps on:
- Null input data
- Invalid compression level (must be 1-9)
- Invalid or truncated compressed data
- CRC32 mismatch in GZIP decompression
- Corrupted DEFLATE stream

### Implementation Notes

- Zero external dependencies (no zlib required)
- Implements RFC 1951 (DEFLATE) and RFC 1952 (GZIP)
- Currently uses stored blocks for compression (correct but not optimal)
- Decompression supports all DEFLATE block types (stored, fixed Huffman, dynamic Huffman)

### Use Cases

- **File compression:** Create and read `.gz` files
- **HTTP compression:** Handle gzip-encoded HTTP responses
- **Data storage:** Reduce storage space for text and binary data
- **Network transfer:** Reduce bandwidth for data transmission
- **Archive formats:** Build custom compressed file formats

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

### Zia Example

> Watcher is not yet available from Zia. The constructor name `new()` conflicts with the Zia `new` keyword, and `Poll`/`PollFor` are not exported. Use BASIC for file system watching.

### BASIC Example

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

---


## Viper.IO.JsonStream

Streaming JSON processor for incremental JSON parsing and generation.

**Type:** Instance class

**Constructor:** `Viper.IO.JsonStream.New()`

**Note:** The full method API for `JsonStream` depends on the runtime implementation. Refer to the runtime source (`rt_json_stream.c`) for the complete interface. This class is documented here for discoverability.

---


## See Also

- [Files & Directories](files.md)
- [Streams & Buffers](streams.md)
- [Input & Output Overview](README.md)
- [Viper Runtime Library](../README.md)
