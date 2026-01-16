# Viper Archiver (varc) - Implementation Plan

## Overview

A command-line archive utility written in Zia that:
- **Archives**: Combines multiple files/directories into a single archive
- **Compresses**: Uses a custom compression format (based on DEFLATE)
- **Encrypts**: Uses password-based encryption with HMAC-SHA256 stream cipher
- **Reverses all operations**: Extract, decompress, and decrypt

## File Format: `.varc` (Viper Archive)

### Format Specification

```
+------------------+
| MAGIC (8 bytes)  |  "VIPERARC"
+------------------+
| VERSION (1 byte) |  0x01
+------------------+
| FLAGS (1 byte)   |  bit 0: compressed, bit 1: encrypted
+------------------+
| HEADER_SIZE (4b) |  Size of header (little-endian u32)
+------------------+
| SALT (16 bytes)  |  Only present if encrypted (random salt for PBKDF2)
+------------------+
| IV (16 bytes)    |  Only present if encrypted (initialization vector)
+------------------+
| ENTRY_COUNT (4b) |  Number of entries (little-endian u32)
+------------------+
| ENTRY HEADERS    |  Variable length, one per file
+------------------+
| DATA BLOCKS      |  Compressed/encrypted file data
+------------------+
| CHECKSUM (32b)   |  SHA-256 of all preceding data
+------------------+

Entry Header Format (per file):
+------------------+
| NAME_LEN (2b)    |  Length of filename (u16)
+------------------+
| NAME (variable)  |  UTF-8 filename (relative path)
+------------------+
| ORIG_SIZE (8b)   |  Original uncompressed size (u64)
+------------------+
| COMP_SIZE (8b)   |  Compressed size in archive (u64)
+------------------+
| OFFSET (8b)      |  Offset to data block from start of DATA BLOCKS
+------------------+
| MODTIME (8b)     |  Modification time (Unix timestamp)
+------------------+
| CRC32 (4b)       |  CRC32 of original uncompressed data
+------------------+
| FLAGS (1b)       |  bit 0: is_directory
+------------------+
```

### Custom Compression Format

The compression uses DEFLATE (via `Viper.IO.Compress.Deflate`) but wrapped in a custom container:

```
+------------------+
| BLOCK_TYPE (1b)  |  0x00=stored, 0x01=deflate
+------------------+
| BLOCK_SIZE (4b)  |  Size of this block
+------------------+
| DATA (variable)  |  Raw or deflated data
+------------------+
```

Multiple blocks may be chained for large files (64KB block size default).

### Encryption Scheme

Since Zia lacks native AES, we implement a **HMAC-SHA256 stream cipher**:

1. **Key Derivation**: PBKDF2-SHA256 with 100,000 iterations
   - Input: password + salt (16 random bytes)
   - Output: 32-byte key

2. **Stream Generation** (CTR-like mode):
   - Generate keystream blocks: `HMAC-SHA256(key, IV || counter)`
   - XOR keystream with plaintext to produce ciphertext
   - Counter increments for each 32-byte block

3. **Authentication**:
   - Final SHA-256 checksum covers encrypted data
   - HMAC of header provides tamper detection

## Command-Line Interface

### Usage

```
varc <command> [options] <archive> [files...]

Commands:
  create, c     Create a new archive
  extract, x    Extract files from archive
  list, l       List archive contents
  test, t       Test archive integrity
  add, a        Add files to existing archive
  delete, d     Delete files from archive

Options:
  -o, --output <dir>     Output directory for extraction (default: current)
  -p, --password <pass>  Password for encryption/decryption
  -P, --password-file    Read password from file
  -l, --level <0-9>      Compression level (default: 6, 0=store only)
  -e, --encrypt          Enable encryption (prompts for password if not given)
  -r, --recursive        Include subdirectories recursively
  -v, --verbose          Verbose output
  -q, --quiet            Suppress non-error output
  -f, --force            Overwrite existing files without prompting
  -n, --dry-run          Show what would be done without doing it
  --no-compress          Store files without compression
  --strip <n>            Strip n leading path components on extraction
  --include <pattern>    Only include files matching pattern
  --exclude <pattern>    Exclude files matching pattern
  -h, --help             Show help message
  --version              Show version information
```

### Examples

```bash
# Create archive from files
varc create backup.varc file1.txt file2.txt

# Create compressed archive from directory
varc create -r project.varc ./src/

# Create encrypted archive
varc create -e -p "secret123" secure.varc sensitive/

# Extract all files
varc extract backup.varc

# Extract to specific directory
varc extract -o ./restored/ backup.varc

# Extract encrypted archive
varc extract -p "secret123" secure.varc

# List archive contents
varc list backup.varc

# List with verbose info (sizes, dates)
varc list -v backup.varc

# Test archive integrity
varc test backup.varc

# Add files to existing archive
varc add backup.varc newfile.txt

# Delete files from archive
varc delete backup.varc oldfile.txt
```

## Implementation Structure

### Module Organization

```
demos/zia/varc/
  main.zia           # Entry point, CLI parsing
  archive.zia        # Archive format reading/writing
  compress.zia       # Custom compression wrapper
  encrypt.zia        # HMAC-SHA256 stream cipher
  fileutil.zia       # File/directory utilities
  cli.zia            # Command-line argument parsing
  format.zia         # Binary format encoding/decoding
```

### Core Data Structures

```zia
// Archive entry metadata
entity ArchiveEntry {
    name: String,
    origSize: Integer,
    compSize: Integer,
    offset: Integer,
    modTime: Integer,
    crc32: Integer,
    isDirectory: Boolean
}

// Archive state
entity Archive {
    path: String,
    version: Integer,
    flags: Integer,
    entries: List,
    salt: Object,      // Bytes
    iv: Object,        // Bytes
    key: Object        // Bytes (derived key, if encrypted)
}

// CLI options
entity Options {
    command: String,
    archivePath: String,
    files: List,
    outputDir: String,
    password: String,
    level: Integer,
    encrypt: Boolean,
    recursive: Boolean,
    verbose: Boolean,
    quiet: Boolean,
    force: Boolean,
    dryRun: Boolean,
    noCompress: Boolean,
    stripComponents: Integer,
    includePattern: String,
    excludePattern: String
}
```

### Key Functions

#### main.zia
```zia
func main() -> Integer
func printUsage()
func printVersion()
```

#### cli.zia
```zia
func parseArgs() -> Options
func validateOptions(opts: Options) -> Boolean
func promptPassword(prompt: String) -> String
```

#### archive.zia
```zia
func createArchive(opts: Options) -> Boolean
func extractArchive(opts: Options) -> Boolean
func listArchive(opts: Options) -> Boolean
func testArchive(opts: Options) -> Boolean
func addToArchive(opts: Options) -> Boolean
func deleteFromArchive(opts: Options) -> Boolean

func readArchiveHeader(data: Object) -> Archive
func writeArchiveHeader(archive: Archive) -> Object
func readEntryHeaders(data: Object, count: Integer) -> List
func writeEntryHeaders(entries: List) -> Object
```

#### compress.zia
```zia
func compressBlock(data: Object, level: Integer) -> Object
func decompressBlock(data: Object) -> Object
func compressFile(path: String, level: Integer) -> Object
func decompressToFile(data: Object, path: String)
```

#### encrypt.zia
```zia
func deriveKey(password: String, salt: Object) -> Object
func generateSalt() -> Object
func generateIV() -> Object
func encryptData(data: Object, key: Object, iv: Object) -> Object
func decryptData(data: Object, key: Object, iv: Object) -> Object
func generateKeystream(key: Object, iv: Object, length: Integer) -> Object
```

#### fileutil.zia
```zia
func collectFiles(paths: List, recursive: Boolean) -> List
func matchPattern(name: String, pattern: String) -> Boolean
func ensureDirectory(path: String)
func relativePath(base: String, full: String) -> String
func formatSize(bytes: Integer) -> String
func formatDate(timestamp: Integer) -> String
```

#### format.zia
```zia
func writeU16(stream: Object, value: Integer)
func writeU32(stream: Object, value: Integer)
func writeU64(stream: Object, value: Integer)
func writeBytes(stream: Object, data: Object)
func writeString(stream: Object, str: String)

func readU16(stream: Object) -> Integer
func readU32(stream: Object) -> Integer
func readU64(stream: Object) -> Integer
func readBytes(stream: Object, length: Integer) -> Object
func readString(stream: Object, length: Integer) -> String
```

## Implementation Phases

### Phase 1: Core Infrastructure
1. Set up module structure
2. Implement CLI argument parsing (`cli.zia`)
3. Implement binary format helpers (`format.zia`)
4. Implement file utilities (`fileutil.zia`)
5. Basic tests

### Phase 2: Archive Creation (Uncompressed, Unencrypted)
1. Implement `createArchive()` - store-only mode
2. Implement header writing
3. Implement file collection and traversal
4. Test with single files and directories

### Phase 3: Extraction
1. Implement `readArchiveHeader()`
2. Implement `extractArchive()`
3. Implement `listArchive()`
4. Test round-trip (create then extract)

### Phase 4: Compression
1. Implement block-based compression (`compress.zia`)
2. Integrate with archive creation
3. Integrate with extraction
4. Test compression ratio and correctness

### Phase 5: Encryption
1. Implement key derivation (`deriveKey()`)
2. Implement HMAC-SHA256 stream cipher
3. Integrate with archive creation
4. Integrate with extraction
5. Test encryption/decryption correctness

### Phase 6: Advanced Features
1. Add file to existing archive
2. Delete file from archive
3. Pattern matching (include/exclude)
4. Progress reporting
5. Integrity testing (`testArchive()`)

### Phase 7: Polish
1. Error handling and user-friendly messages
2. Edge cases (empty archives, large files, Unicode names)
3. Performance optimization
4. Documentation and help text

## Security Considerations

1. **Password Handling**:
   - Never store passwords in plain text
   - Clear password from memory after key derivation
   - Support reading password from file for automation

2. **Encryption Limitations**:
   - HMAC-SHA256 stream cipher is non-standard
   - Suitable for personal use, not high-security applications
   - Document this limitation clearly

3. **Integrity**:
   - CRC32 per-file for corruption detection
   - SHA-256 for full archive integrity
   - HMAC for authentication when encrypted

## Zia Runtime Dependencies

Functions used from Viper runtime:

### File I/O
- `Viper.IO.File.ReadAllBytes(path) -> Bytes`
- `Viper.IO.File.WriteAllBytes(path, bytes)`
- `Viper.IO.File.ReadAllText(path) -> String`
- `Viper.IO.File.Exists(path) -> Boolean`
- `Viper.IO.File.Delete(path)`
- `Viper.IO.File.Size(path) -> Integer`
- `Viper.IO.File.Modified(path) -> Integer`

### Directory
- `Viper.IO.Dir.Exists(path) -> Boolean`
- `Viper.IO.Dir.Make(path)`
- `Viper.IO.Dir.MakeAll(path)`
- `Viper.IO.Dir.Entries(path) -> Seq`

### Path
- `Viper.IO.Path.Join(a, b) -> String`
- `Viper.IO.Path.Dir(path) -> String`
- `Viper.IO.Path.Name(path) -> String`

### Bytes
- `Viper.Collections.Bytes.New(size) -> Bytes`
- `Viper.Collections.Bytes.Get(bytes, index) -> Integer`
- `Viper.Collections.Bytes.Set(bytes, index, value)`
- `Viper.Collections.Bytes.get_Len(bytes) -> Integer`
- `Viper.Collections.Bytes.Slice(bytes, start, len) -> Bytes`
- `Viper.Collections.Bytes.Copy(src, srcOff, dst, dstOff, len)`
- `Viper.Collections.Bytes.FromStr(str) -> Bytes`
- `Viper.Collections.Bytes.ToStr(bytes) -> String`

### Compression
- `Viper.IO.Compress.Deflate(bytes) -> Bytes`
- `Viper.IO.Compress.DeflateLvl(bytes, level) -> Bytes`
- `Viper.IO.Compress.Inflate(bytes) -> Bytes`

### Cryptography
- `Viper.Crypto.Hash.SHA256Bytes(bytes) -> String`
- `Viper.Crypto.Hash.CRC32Bytes(bytes) -> Integer`
- `Viper.Crypto.Hash.HmacSHA256Bytes(key, msg) -> String`
- `Viper.Crypto.KeyDerive.Pbkdf2SHA256(pass, salt, iters, keyLen) -> Bytes`
- `Viper.Crypto.Rand.Bytes(count) -> Bytes`

### MemStream (for buffer building)
- `Viper.IO.MemStream.New() -> MemStream`
- `Viper.IO.MemStream.WriteU8(stream, value)`
- `Viper.IO.MemStream.WriteBytes(stream, bytes)`
- `Viper.IO.MemStream.ToBytes(stream) -> Bytes`
- `Viper.IO.MemStream.get_Pos(stream) -> Integer`
- `Viper.IO.MemStream.set_Pos(stream, pos)`

### Environment
- `Viper.Environment.GetArgumentCount() -> Integer`
- `Viper.Environment.GetArgument(index) -> String`
- `Viper.Environment.EndProgram(code)`

### Collections
- `Viper.Collections.List.New() -> List`
- `Viper.Collections.List.Add(list, item)`
- `Viper.Collections.List.get_Count(list) -> Integer`
- `Viper.Collections.List.get_Item(list, index) -> Object`

## Testing Strategy

1. **Unit Tests**: Each module tested independently
2. **Integration Tests**: Full create/extract cycles
3. **Compatibility Tests**: Various file types and sizes
4. **Edge Cases**: Empty files, deep directories, special characters
5. **Performance Tests**: Large archives (>1GB)
6. **Security Tests**: Wrong password, corrupted archive, tampered data

## Notes

- The custom format is intentionally simple for educational purposes
- For production use, consider using the built-in `Viper.IO.Archive` (zip-based)
- The HMAC-SHA256 stream cipher is secure for confidentiality but is non-standard
- Block size of 64KB balances memory usage and compression efficiency
