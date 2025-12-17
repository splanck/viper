# Text Processing

> String building, encoding/decoding, and text utilities.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Text.Codec](#vipertextcodec)
- [Viper.Text.Csv](#vipertextcsv)
- [Viper.Text.Guid](#vipertextguid)
- [Viper.Text.StringBuilder](#vipertextstringbuilder)

---

## Viper.Text.Codec

String-based encoding and decoding utilities for Base64, Hex, and URL encoding.

**Type:** Static utility class

### Methods

| Method           | Signature        | Description                              |
|------------------|------------------|------------------------------------------|
| `Base64Enc(str)` | `String(String)` | Base64-encode a string's bytes           |
| `Base64Dec(str)` | `String(String)` | Decode a Base64 string to original bytes |
| `HexEnc(str)`    | `String(String)` | Hex-encode a string's bytes (lowercase)  |
| `HexDec(str)`    | `String(String)` | Decode a hex string to original bytes    |
| `UrlEncode(str)` | `String(String)` | URL-encode a string (percent-encoding)   |
| `UrlDecode(str)` | `String(String)` | URL-decode a string                      |

### Notes

- All methods operate on strings (C strings without embedded null bytes)
- For binary data with null bytes, use `Bytes.ToBase64`/`Bytes.FromBase64` or `Bytes.ToHex`/`Bytes.FromHex`
- **URL Encoding:**
    - Unreserved characters (A-Z, a-z, 0-9, `-`, `_`, `.`, `~`) pass through unchanged
    - All other characters are encoded as `%XX` (lowercase hex)
    - Decoding treats `+` as space (form encoding convention)
- **Base64:** RFC 4648 standard alphabet with `=` padding
- **Hex:** Lowercase hex encoding (e.g., "Hello" → "48656c6c6f")
- Invalid input to `Base64Dec` or `HexDec` will trap

### Example

```basic
' URL encoding for query parameters
DIM original AS STRING = "key=value&name=John Doe"
DIM encoded AS STRING = Viper.Text.Codec.UrlEncode(original)
PRINT encoded  ' Output: "key%3dvalue%26name%3dJohn%20Doe"

DIM decoded AS STRING = Viper.Text.Codec.UrlDecode(encoded)
PRINT decoded = original  ' Output: 1 (true)

' Base64 encoding for data transmission
DIM data AS STRING = "Hello, World!"
DIM b64 AS STRING = Viper.Text.Codec.Base64Enc(data)
PRINT b64  ' Output: "SGVsbG8sIFdvcmxkIQ=="

DIM restored AS STRING = Viper.Text.Codec.Base64Dec(b64)
PRINT restored  ' Output: "Hello, World!"

' Hex encoding for display
DIM hex AS STRING = Viper.Text.Codec.HexEnc("ABC")
PRINT hex  ' Output: "414243"

DIM unhex AS STRING = Viper.Text.Codec.HexDec(hex)
PRINT unhex  ' Output: "ABC"
```

---

## Viper.Text.Csv

RFC 4180-compliant CSV parsing and formatting.

**Type:** Static utility class

### Methods

| Method                          | Signature             | Description                                                       |
|---------------------------------|-----------------------|-------------------------------------------------------------------|
| `ParseLine(line)`               | `Seq(String)`         | Parse a single CSV line into fields using comma delimiter         |
| `ParseLineWith(line, delim)`    | `Seq(String, String)` | Parse a single CSV line with custom delimiter                     |
| `Parse(text)`                   | `Seq(String)`         | Parse multi-line CSV text into rows (each row is a Seq of fields) |
| `ParseWith(text, delim)`        | `Seq(String, String)` | Parse multi-line CSV with custom delimiter                        |
| `FormatLine(fields)`            | `String(Seq)`         | Format a Seq of fields into a CSV line                            |
| `FormatLineWith(fields, delim)` | `String(Seq, String)` | Format fields with custom delimiter                               |
| `Format(rows)`                  | `String(Seq)`         | Format a Seq of rows into multi-line CSV text                     |
| `FormatWith(rows, delim)`       | `String(Seq, String)` | Format rows with custom delimiter                                 |

### Notes

- **RFC 4180 Compliance:**
    - Fields containing delimiters, quotes, or newlines are automatically quoted
    - Embedded quotes are escaped by doubling (`""`)
    - Newlines within quoted fields are preserved
    - Leading/trailing whitespace in fields is preserved
- Custom delimiter must be a single character
- Empty fields are supported (adjacent delimiters create empty strings)
- Parse functions return `Seq` objects (use `Count`, `Get(index)` to access)

### Example

```basic
' Parse a simple CSV line
DIM fields AS OBJECT = Viper.Text.Csv.ParseLine("name,age,city")
PRINT fields.Count  ' Output: 3
PRINT fields.Get(0) ' Output: "name"

' Parse with quoted fields
DIM row AS OBJECT = Viper.Text.Csv.ParseLine("\"John Doe\",30,\"New York\"")
PRINT row.Get(0)    ' Output: John Doe
PRINT row.Get(2)    ' Output: New York

' Handle embedded quotes (doubled)
DIM quoted AS OBJECT = Viper.Text.Csv.ParseLine("\"He said \"\"Hello\"\"\"")
PRINT quoted.Get(0) ' Output: He said "Hello"

' Parse multi-line CSV
DIM csv AS STRING = "name,age" + CHR$(10) + "Alice,25" + CHR$(10) + "Bob,30"
DIM rows AS OBJECT = Viper.Text.Csv.Parse(csv)
PRINT rows.Count    ' Output: 3

' Format fields into CSV
DIM data AS OBJECT = Viper.Collections.Seq.New()
data.Push("Hello, World")
data.Push("Simple")
DIM line AS STRING = Viper.Text.Csv.FormatLine(data)
PRINT line          ' Output: "Hello, World",Simple

' Use tab delimiter
DIM tsv AS OBJECT = Viper.Text.Csv.ParseLineWith("a	b	c", CHR$(9))
PRINT tsv.Count     ' Output: 3
```

### Use Cases

- Importing/exporting spreadsheet data
- Configuration files with structured data
- Data interchange between systems
- Log file parsing

---

## Viper.Text.Guid

UUID version 4 (random) generation and manipulation per RFC 4122.

**Type:** Static utility class

### Properties

| Property | Type   | Description                                                 |
|----------|--------|-------------------------------------------------------------|
| `Empty`  | String | Returns the nil UUID "00000000-0000-0000-0000-000000000000" |

### Methods

| Method             | Signature         | Description                            |
|--------------------|-------------------|----------------------------------------|
| `New()`            | `String()`        | Generate a new random UUID v4          |
| `IsValid(guid)`    | `Boolean(String)` | Check if string is a valid GUID format |
| `ToBytes(guid)`    | `Bytes(String)`   | Convert GUID string to 16-byte array   |
| `FromBytes(bytes)` | `String(Bytes)`   | Convert 16-byte array to GUID string   |

### Notes

- Generated GUIDs follow UUID version 4 format (random)
- Format: `xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx` where:
    - `4` indicates version 4 (random UUID)
    - `y` is one of `8`, `9`, `a`, or `b` (variant indicator)
- All hex characters are lowercase
- Uses cryptographically secure random source where available (/dev/urandom on Unix, CryptGenRandom on Windows)
- `ToBytes()` traps if the GUID format is invalid
- `FromBytes()` traps if the Bytes object is not exactly 16 bytes

### Example

```basic
' Generate a new GUID
DIM id AS STRING = Viper.Text.Guid.New()
PRINT id  ' Example: "550e8400-e29b-41d4-a716-446655440000"

' Check if a string is a valid GUID
DIM valid AS INTEGER = Viper.Text.Guid.IsValid(id)
PRINT valid  ' Output: 1 (true)

DIM invalid AS INTEGER = Viper.Text.Guid.IsValid("not-a-guid")
PRINT invalid  ' Output: 0 (false)

' Get the empty/nil GUID
DIM empty AS STRING = Viper.Text.Guid.Empty
PRINT empty  ' Output: "00000000-0000-0000-0000-000000000000"

' Convert to/from bytes for storage or transmission
DIM bytes AS OBJECT = Viper.Text.Guid.ToBytes(id)
DIM restored AS STRING = Viper.Text.Guid.FromBytes(bytes)
PRINT restored = id  ' Output: 1 (true)
```

---

## Viper.Text.StringBuilder

Mutable string builder for efficient string concatenation. Use when building strings incrementally to avoid creating
many intermediate string objects.

**Type:** Instance (opaque*)
**Constructor:** `NEW Viper.Text.StringBuilder()`

### Properties

| Property   | Type    | Description                              |
|------------|---------|------------------------------------------|
| `Length`   | Integer | Current length of the accumulated string |
| `Capacity` | Integer | Current buffer capacity                  |

### Methods

| Method             | Signature               | Description                                           |
|--------------------|-------------------------|-------------------------------------------------------|
| `Append(text)`     | `StringBuilder(String)` | Appends text and returns self for chaining            |
| `AppendLine(text)` | `StringBuilder(String)` | Appends text and then `\n`; returns self for chaining |
| `ToString()`       | `String()`              | Returns the accumulated string                        |
| `Clear()`          | `Void()`                | Clears the buffer                                     |

### Example

```basic
DIM sb AS Viper.Text.StringBuilder
sb = NEW Viper.Text.StringBuilder()

' Method chaining
sb.Append("Hello, ").Append("World!").Append(" How are you?")

PRINT sb.ToString()  ' Output: "Hello, World! How are you?"
PRINT sb.Length      ' Output: 28

' Append lines
sb.Clear()
sb.AppendLine("a").AppendLine("b")
PRINT sb.ToString()  ' Output: "a\nb\n"

sb.Clear()
PRINT sb.Length      ' Output: 0
```

### Performance Note

Use `StringBuilder` instead of repeated string concatenation in loops:

```basic
' Efficient: O(n) total
DIM sb AS Viper.Text.StringBuilder
sb = NEW Viper.Text.StringBuilder()
FOR i = 1 TO 1000
    sb.Append("item ")
NEXT i
result = sb.ToString()

' Inefficient: O(n^2) due to intermediate strings
DIM result AS STRING
result = ""
FOR i = 1 TO 1000
    result = result + "item "  ' Creates new string each iteration
NEXT i
```

