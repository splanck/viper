# Viper Runtime Library Reference

> **Version:** 0.1.2
> **Status:** Pre-Alpha — API subject to change

The Viper Runtime Library provides a set of built-in classes and utilities available to all Viper programs. These classes are implemented in C and exposed through the IL runtime system.

---

## Table of Contents

- [Namespace Overview](#namespace-overview)
- [Viper.String](#viperstring)
- [Viper.Object](#viperobject)
- [Viper.Text.StringBuilder](#vipertextstringbuilder)
- [Viper.Text.Codec](#vipertextcodec)
- [Viper.Text.Guid](#vipertextguid)
- [Viper.Crypto.Hash](#vipercryptohash)
- [Viper.Bits](#viperbits)
- [Viper.Collections.List](#vipercollectionslist)
- [Viper.Collections.Map](#vipercollectionsmap)
- [Viper.Collections.Seq](#vipercollectionsseq)
- [Viper.Collections.Stack](#vipercollectionsstack)
- [Viper.Collections.Queue](#vipercollectionsqueue)
- [Viper.Collections.Bytes](#vipercollectionsbytes)
- [Viper.Collections.Bag](#vipercollectionsbag)
- [Viper.Collections.Ring](#vipercollectionsring)
- [Viper.Math](#vipermath)
- [Viper.Terminal](#viperterminal)
- [Viper.Convert](#viperconvert)
- [Viper.Random](#viperrandom)
- [Viper.Environment](#viperenvironment)
- [Viper.Exec](#viperexec)
- [Viper.Machine](#vipermachine)
- [Viper.DateTime](#viperdatetime)
- [Viper.IO.File](#viperiofile)
- [Viper.IO.Path](#viperiopath)
- [Viper.IO.Dir](#viperiodir)
- [Viper.IO.BinFile](#viperiobinfile)
- [Viper.IO.LineReader](#viperiolinereader)
- [Viper.IO.LineWriter](#viperiolinewriter)
- [Viper.Graphics.Canvas](#vipergraphicscanvas)
- [Viper.Graphics.Color](#vipergraphicscolor)
- [Viper.Graphics.Pixels](#vipergraphicspixels)
- [Viper.Time.Clock](#vipertimeclock)
- [Viper.Vec2](#vipervec2)
- [Viper.Vec3](#vipervec3)
- [Viper.Diagnostics.Assert](#viperdiagnosticsassert)
- [Viper.Diagnostics.Stopwatch](#viperdiagnosticsstopwatch)
- [Runtime Architecture](#runtime-architecture)

---

## Namespace Overview

### Viper (Root Namespace)

| Class | Type | Description |
|-------|------|-------------|
| `String` | Instance | Immutable string with manipulation methods |
| `Object` | Base class | Root type for all reference types |
| `Bits` | Static | Bit manipulation utilities (shifts, rotates, counting) |
| `Math` | Static | Mathematical functions (trig, pow, abs, etc.) |
| `Terminal` | Static | Terminal input/output |
| `Convert` | Static | Type conversion utilities |
| `Random` | Static | Random number generation |
| `Environment` | Static | Command-line arguments and environment |
| `Exec` | Static | External command execution |
| `Machine` | Static | System information queries |
| `DateTime` | Static | Date and time operations |
| `Vec2` | Instance | 2D vector math (positions, directions, physics) |
| `Vec3` | Instance | 3D vector math (positions, directions, physics) |

### Viper.Text

| Class | Type | Description |
|-------|------|-------------|
| `StringBuilder` | Instance | Mutable string builder for efficient concatenation |
| `Codec` | Static | Base64, Hex, and URL encoding/decoding |
| `Guid` | Static | UUID version 4 generation and manipulation |

### Viper.Collections

| Class | Type | Description |
|-------|------|-------------|
| `List` | Instance | Dynamic array of objects |
| `Map` | Instance | String-keyed hash map with Keys/Values iteration |
| `Seq` | Instance | Dynamic sequence (growable array) with stack/queue operations |
| `Stack` | Instance | LIFO (last-in-first-out) collection |
| `Queue` | Instance | FIFO (first-in-first-out) collection |
| `Bytes` | Instance | Efficient byte array for binary data |
| `Bag` | Instance | String set with set operations (union, intersection, difference) |
| `Ring` | Instance | Fixed-size circular buffer (overwrites oldest when full) |

### Viper.Crypto

| Class | Type | Description |
|-------|------|-------------|
| `Hash` | Static | Cryptographic hashes (MD5, SHA1, SHA256) and checksums (CRC32) |

### Viper.IO

| Class | Type | Description |
|-------|------|-------------|
| `File` | Static | File system operations (read, write, delete) |
| `Path` | Static | Cross-platform path manipulation utilities |
| `Dir` | Static | Directory operations (create, remove, list) |
| `BinFile` | Instance | Binary file stream for random access I/O |
| `LineReader` | Instance | Line-by-line text file reading |
| `LineWriter` | Instance | Buffered text file writing |

### Viper.Graphics

| Class | Type | Description |
|-------|------|-------------|
| `Canvas` | Instance | 2D graphics canvas with drawing primitives |
| `Color` | Static | Color creation utilities |
| `Pixels` | Instance | Software image buffer for pixel manipulation |

### Viper.Time

| Class | Type | Description |
|-------|------|-------------|
| `Clock` | Static | Basic timing utilities (sleep, ticks) |

### Viper.Diagnostics

| Class | Type | Description |
|-------|------|-------------|
| `Assert` | Static | Trap when a condition fails |
| `Stopwatch` | Instance | High-precision timing for benchmarking |

### Class Types

- **Instance** — Requires instantiation with `NEW` before use
- **Static** — Methods called directly on the class name (no instantiation)
- **Base class** — Cannot be instantiated directly; inherited by other types

---

## Viper.String

String manipulation class. In Viper, strings are immutable sequences of characters.

**Type:** Instance (opaque*)

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Length` | Integer | Returns the number of characters in the string |
| `IsEmpty` | Boolean | Returns true if the string has zero length |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Substring(start, length)` | `String(Integer, Integer)` | Extracts a portion of the string starting at `start` with `length` characters |
| `Concat(other)` | `String(String)` | Concatenates another string and returns the result |
| `Left(count)` | `String(Integer)` | Returns the leftmost `count` characters |
| `Right(count)` | `String(Integer)` | Returns the rightmost `count` characters |
| `Mid(start)` | `String(Integer)` | Returns characters from `start` to the end (1-based index) |
| `MidLen(start, length)` | `String(Integer, Integer)` | Returns `length` characters starting at `start` (1-based index) |
| `Trim()` | `String()` | Removes leading and trailing whitespace |
| `TrimStart()` | `String()` | Removes leading whitespace |
| `TrimEnd()` | `String()` | Removes trailing whitespace |
| `ToUpper()` | `String()` | Converts all characters to uppercase |
| `ToLower()` | `String()` | Converts all characters to lowercase |
| `IndexOf(search)` | `Integer(String)` | Returns the position of `search` within the string, or -1 if not found |
| `IndexOfFrom(start, search)` | `Integer(Integer, String)` | Searches for `search` starting at position `start` |
| `Chr(code)` | `String(Integer)` | Returns a single-character string from an ASCII/Unicode code point |
| `Asc()` | `Integer()` | Returns the ASCII/Unicode code of the first character |

### Extended Methods

**Search & Match:**

| Method | Signature | Description |
|--------|-----------|-------------|
| `StartsWith(prefix)` | `Boolean(String)` | Returns true if string starts with prefix |
| `EndsWith(suffix)` | `Boolean(String)` | Returns true if string ends with suffix |
| `Has(needle)` | `Boolean(String)` | Returns true if string contains needle |
| `Count(needle)` | `Integer(String)` | Counts non-overlapping occurrences of needle |

**Transformation:**

| Method | Signature | Description |
|--------|-----------|-------------|
| `Replace(needle, replacement)` | `String(String, String)` | Replaces all occurrences of needle with replacement |
| `PadLeft(width, padChar)` | `String(Integer, String)` | Pads string on left to reach width using first char of padChar |
| `PadRight(width, padChar)` | `String(Integer, String)` | Pads string on right to reach width using first char of padChar |
| `Repeat(count)` | `String(Integer)` | Repeats the string count times |
| `Flip()` | `String()` | Reverses the string (byte-level, ASCII-safe) |
| `Split(delimiter)` | `Seq(String)` | Splits string by delimiter into a Seq of strings |

**Comparison:**

| Method | Signature | Description |
|--------|-----------|-------------|
| `Cmp(other)` | `Integer(String)` | Compares strings, returns -1, 0, or 1 |
| `CmpNoCase(other)` | `Integer(String)` | Case-insensitive comparison, returns -1, 0, or 1 |

### Static Functions (Viper.Strings)

| Function | Signature | Description |
|----------|-----------|-------------|
| `Viper.Strings.Join(separator, items)` | `String(String, Seq)` | Joins sequence of strings with separator |

**Note:** `Flip()` performs byte-level reversal. It works correctly for ASCII strings but may produce invalid results for multi-byte UTF-8 characters.

### Example

```basic
DIM s AS STRING
s = "  Hello, World!  "

PRINT s.Length          ' Output: 17
PRINT s.Trim()          ' Output: "Hello, World!"
PRINT s.ToUpper()       ' Output: "  HELLO, WORLD!  "
PRINT s.Left(7).Trim()  ' Output: "Hello,"
PRINT s.IndexOf("World") ' Output: 9

DIM code AS INTEGER
code = s.Trim().Asc()   ' code = 72 (ASCII for 'H')
```

### Extended Methods Example

```basic
DIM s AS STRING
s = "hello world"

' Search and match
PRINT s.StartsWith("hello")  ' Output: true
PRINT s.EndsWith("world")    ' Output: true
PRINT s.Has("lo wo")         ' Output: true
PRINT s.Count("l")           ' Output: 3

' Transformation
PRINT s.Replace("world", "universe")  ' Output: "hello universe"
PRINT "42".PadLeft(5, "0")            ' Output: "00042"
PRINT "hi".PadRight(5, ".")           ' Output: "hi..."
PRINT "ab".Repeat(3)                   ' Output: "ababab"
PRINT "hello".Flip()                   ' Output: "olleh"

' Split and join
DIM parts AS Viper.Collections.Seq
parts = "a,b,c".Split(",")
PRINT parts.Len                        ' Output: 3
PRINT Viper.Strings.Join("-", parts)   ' Output: "a-b-c"

' Comparison
PRINT "abc".Cmp("abd")                 ' Output: -1
PRINT "ABC".CmpNoCase("abc")           ' Output: 0
```

---

## Viper.Object

Base class for all Viper reference types. Provides fundamental object operations.

**Type:** Base class (not instantiated directly)

### Instance Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Equals(other)` | `Boolean(Object)` | Compares this object with another for equality |
| `GetHashCode()` | `Integer()` | Returns a hash code for the object |
| `ToString()` | `String()` | Returns a string representation of the object |

### Static Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `Viper.Object.ReferenceEquals(a, b)` | `Boolean(Object, Object)` | Tests if two references point to the same object instance |

### Example

```basic
DIM obj1 AS Viper.Object
DIM obj2 AS Viper.Object

IF obj1.Equals(obj2) THEN
    PRINT "Objects are equal"
END IF

PRINT obj1.ToString()
PRINT obj1.GetHashCode()

' Static function call - check if same instance
IF Viper.Object.ReferenceEquals(obj1, obj2) THEN
    PRINT "Same object instance"
END IF
```

---

## Viper.Text.StringBuilder

Mutable string builder for efficient string concatenation. Use when building strings incrementally to avoid creating many intermediate string objects.

**Type:** Instance (opaque*)
**Constructor:** `NEW Viper.Text.StringBuilder()`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Length` | Integer | Current length of the accumulated string |
| `Capacity` | Integer | Current buffer capacity |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Append(text)` | `StringBuilder(String)` | Appends text and returns self for chaining |
| `AppendLine(text)` | `StringBuilder(String)` | Appends text and then `\n`; returns self for chaining |
| `ToString()` | `String()` | Returns the accumulated string |
| `Clear()` | `Void()` | Clears the buffer |

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

---

## Viper.Text.Codec

String-based encoding and decoding utilities for Base64, Hex, and URL encoding.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Base64Enc(str)` | `String(String)` | Base64-encode a string's bytes |
| `Base64Dec(str)` | `String(String)` | Decode a Base64 string to original bytes |
| `HexEnc(str)` | `String(String)` | Hex-encode a string's bytes (lowercase) |
| `HexDec(str)` | `String(String)` | Decode a hex string to original bytes |
| `UrlEncode(str)` | `String(String)` | URL-encode a string (percent-encoding) |
| `UrlDecode(str)` | `String(String)` | URL-decode a string |

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

## Viper.Text.Guid

UUID version 4 (random) generation and manipulation per RFC 4122.

**Type:** Static utility class

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Empty` | String | Returns the nil UUID "00000000-0000-0000-0000-000000000000" |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `String()` | Generate a new random UUID v4 |
| `IsValid(guid)` | `Boolean(String)` | Check if string is a valid GUID format |
| `ToBytes(guid)` | `Bytes(String)` | Convert GUID string to 16-byte array |
| `FromBytes(bytes)` | `String(Bytes)` | Convert 16-byte array to GUID string |

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

## Viper.Crypto.Hash

Cryptographic hash functions and checksums for strings and binary data.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `CRC32(str)` | `Integer(String)` | Compute CRC32 checksum of a string |
| `CRC32Bytes(bytes)` | `Integer(Bytes)` | Compute CRC32 checksum of a Bytes object |
| `MD5(str)` | `String(String)` | Compute MD5 hash of a string |
| `MD5Bytes(bytes)` | `String(Bytes)` | Compute MD5 hash of a Bytes object |
| `SHA1(str)` | `String(String)` | Compute SHA1 hash of a string |
| `SHA1Bytes(bytes)` | `String(Bytes)` | Compute SHA1 hash of a Bytes object |
| `SHA256(str)` | `String(String)` | Compute SHA256 hash of a string |
| `SHA256Bytes(bytes)` | `String(Bytes)` | Compute SHA256 hash of a Bytes object |

### Notes

- All hash outputs (MD5, SHA1, SHA256) are lowercase hex strings
- CRC32 returns an integer (the raw checksum value)
- MD5 returns a 32-character hex string
- SHA1 returns a 40-character hex string
- SHA256 returns a 64-character hex string
- **Security Warning:** MD5 and SHA1 are cryptographically broken and should NOT be used for security-sensitive applications. Use SHA256 or better for security purposes. These are provided for checksums, fingerprinting, and legacy compatibility.

### Example

```basic
' Compute checksums and hashes
DIM data AS STRING = "Hello, World!"

' CRC32 checksum (returns integer)
DIM crc AS INTEGER = Viper.Crypto.Hash.CRC32(data)
PRINT crc  ' Output: some integer value

' MD5 hash (32 hex characters)
DIM md5 AS STRING = Viper.Crypto.Hash.MD5(data)
PRINT md5  ' Output: "65a8e27d8879283831b664bd8b7f0ad4"

' SHA1 hash (40 hex characters)
DIM sha1 AS STRING = Viper.Crypto.Hash.SHA1(data)
PRINT sha1  ' Output: "0a0a9f2a6772942557ab5355d76af442f8f65e01"

' SHA256 hash (64 hex characters)
DIM sha256 AS STRING = Viper.Crypto.Hash.SHA256(data)
PRINT sha256  ' Output: "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f"

' Hash binary data using Bytes variants
DIM bytes AS OBJECT = NEW Viper.Collections.Bytes()
bytes.WriteString("Hello")
DIM hash AS STRING = Viper.Crypto.Hash.SHA256Bytes(bytes)
PRINT hash
```

---

## Viper.Bits

Bit manipulation utilities for working with 64-bit integers at the bit level.

**Type:** Static (no instantiation required)

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `And(a, b)` | `i64(i64, i64)` | Bitwise AND |
| `Or(a, b)` | `i64(i64, i64)` | Bitwise OR |
| `Xor(a, b)` | `i64(i64, i64)` | Bitwise XOR |
| `Not(val)` | `i64(i64)` | Bitwise NOT (complement) |
| `Shl(val, count)` | `i64(i64, i64)` | Logical shift left |
| `Shr(val, count)` | `i64(i64, i64)` | Arithmetic shift right (sign-extended) |
| `Ushr(val, count)` | `i64(i64, i64)` | Logical shift right (zero-fill) |
| `Rotl(val, count)` | `i64(i64, i64)` | Rotate left |
| `Rotr(val, count)` | `i64(i64, i64)` | Rotate right |
| `Count(val)` | `i64(i64)` | Population count (number of 1 bits) |
| `LeadZ(val)` | `i64(i64)` | Count leading zeros |
| `TrailZ(val)` | `i64(i64)` | Count trailing zeros |
| `Flip(val)` | `i64(i64)` | Reverse all 64 bits |
| `Swap(val)` | `i64(i64)` | Byte swap (endian swap) |
| `Get(val, bit)` | `i1(i64, i64)` | Get bit at position (0-63) |
| `Set(val, bit)` | `i64(i64, i64)` | Set bit at position |
| `Clear(val, bit)` | `i64(i64, i64)` | Clear bit at position |
| `Toggle(val, bit)` | `i64(i64, i64)` | Toggle bit at position |

### Method Details

#### Shift Operations

- **Shl** — Logical shift left. Shifts bits left, filling with zeros on the right.
- **Shr** — Arithmetic shift right. Shifts bits right, preserving the sign bit (sign-extended).
- **Ushr** — Logical shift right. Shifts bits right, filling with zeros on the left.

Shift counts are clamped: negative counts or counts >= 64 return 0 (for Shl/Ushr) or the sign bit extended (for Shr with negative values).

#### Rotate Operations

- **Rotl** — Rotate left. Bits shifted out on the left wrap around to the right.
- **Rotr** — Rotate right. Bits shifted out on the right wrap around to the left.

Rotate counts are normalized to 0-63 (count MOD 64).

#### Bit Counting

- **Count** — Population count (popcount). Returns the number of 1 bits.
- **LeadZ** — Count leading zeros. Returns 64 for zero, 0 for negative values.
- **TrailZ** — Count trailing zeros. Returns 64 for zero.

#### Single Bit Operations

All single-bit operations accept bit positions 0-63. Out-of-range positions return the input unchanged (for Set/Clear/Toggle) or false (for Get).

### Example

```basic
' Basic bitwise operations
DIM a AS INTEGER = &HFF
DIM b AS INTEGER = &H0F
PRINT Viper.Bits.And(a, b)  ' 15 (&H0F)
PRINT Viper.Bits.Or(a, b)   ' 255 (&HFF)
PRINT Viper.Bits.Xor(a, b)  ' 240 (&HF0)

' Shift operations
DIM val AS INTEGER = 1
PRINT Viper.Bits.Shl(val, 4)   ' 16
PRINT Viper.Bits.Shr(16, 2)    ' 4

' Count set bits
DIM mask AS INTEGER = &HFF
PRINT Viper.Bits.Count(mask)   ' 8

' Work with individual bits
DIM flags AS INTEGER = 0
flags = Viper.Bits.Set(flags, 0)    ' Set bit 0
flags = Viper.Bits.Set(flags, 3)    ' Set bit 3
PRINT Viper.Bits.Get(flags, 0)      ' True
PRINT Viper.Bits.Get(flags, 1)      ' False
flags = Viper.Bits.Toggle(flags, 3) ' Toggle bit 3 off
PRINT flags                          ' 1

' Endian conversion
DIM big AS INTEGER = &H0102030405060708
DIM little AS INTEGER = Viper.Bits.Swap(big)
' little = &H0807060504030201
```

---

## Viper.Collections.List

Dynamic array that grows automatically. Stores object references.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.List()`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Count` | Integer | Number of items in the list |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(item)` | `Void(Object)` | Appends an item to the end of the list |
| `Clear()` | `Void()` | Removes all items from the list |
| `Has(item)` | `Boolean(Object)` | Returns true if the list contains the object (reference equality) |
| `Find(item)` | `Integer(Object)` | Returns index of the first matching object, or `-1` if not found |
| `Insert(index, item)` | `Void(Integer, Object)` | Inserts the item at `index` (0..Count); `index == Count` appends; traps if out of range |
| `Remove(item)` | `Boolean(Object)` | Removes the first matching object (reference equality); returns true if removed |
| `RemoveAt(index)` | `Void(Integer)` | Removes the item at the specified index |
| `get_Item(index)` | `Object(Integer)` | Gets the item at the specified index |
| `set_Item(index, value)` | `Void(Integer, Object)` | Sets the item at the specified index |

### Example

```basic
DIM list AS Viper.Collections.List
list = NEW Viper.Collections.List()

' Add items
DIM a AS Object = NEW Viper.Collections.List()
DIM b AS Object = NEW Viper.Collections.List()
DIM c AS Object = NEW Viper.Collections.List()

list.Add(a)
list.Add(c)
list.Insert(1, b)          ' [a, b, c]

PRINT list.Find(b)         ' Output: 1

IF list.Has(a) THEN
  PRINT 1                  ' Output: 1 (true)
END IF

IF list.Remove(a) THEN
  PRINT list.Count         ' Output: 2
END IF
PRINT list.Find(a)         ' Output: -1

' Clear all
list.Clear()
```

---

## Viper.Collections.Map

A key-value dictionary with string keys. Provides O(1) average-case lookup, insertion, and deletion.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Map()`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Len` | Integer | Number of key-value pairs in the map |
| `IsEmpty` | Boolean | Returns true if the map has no entries |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Set(key, value)` | `Void(String, Object)` | Add or update key-value pair |
| `Get(key)` | `Object(String)` | Get value for key (returns NULL if not found) |
| `GetOr(key, defaultValue)` | `Object(String, Object)` | Get value for key, or return `defaultValue` if missing (does not insert) |
| `Has(key)` | `Boolean(String)` | Check if key exists |
| `SetIfMissing(key, value)` | `Boolean(String, Object)` | Insert key-value pair only when missing; returns true if inserted |
| `Remove(key)` | `Boolean(String)` | Remove key-value pair; returns true if found |
| `Clear()` | `Void()` | Remove all entries |
| `Keys()` | `Seq()` | Get sequence of all keys |
| `Values()` | `Seq()` | Get sequence of all values |

### Example

```basic
DIM scores AS Viper.Collections.Map
scores = NEW Viper.Collections.Map()

' Add entries
scores.Set("Alice", 95)
scores.Set("Bob", 87)
scores.Set("Carol", 92)

PRINT scores.Len      ' Output: 3
PRINT scores.IsEmpty  ' Output: False

' Check existence and get value
IF scores.Has("Alice") THEN
    PRINT "Alice's score: "; scores.Get("Alice")
END IF

' Get-or-default without inserting
PRINT scores.GetOr("Dave", 0)   ' Output: 0 (and "Dave" is still missing)

' Insert only if missing
IF scores.SetIfMissing("Bob", 123) THEN
    PRINT "Inserted Bob"
ELSE
    PRINT "Bob already exists"
END IF

' Update existing entry
scores.Set("Bob", 91)

' Remove an entry
IF scores.Remove("Carol") THEN
    PRINT "Removed Carol"
END IF

' Iterate over keys
DIM names AS Viper.Collections.Seq
names = scores.Keys()
FOR i = 0 TO names.Len - 1
    PRINT names.Get(i)
NEXT i

' Clear all
scores.Clear()
PRINT scores.IsEmpty  ' Output: True
```

### Use Cases

- **Configuration storage:** Store key-value settings
- **Caching:** Cache computed values by key
- **Lookup tables:** Map identifiers to objects
- **Counting:** Count occurrences by key

---

## Viper.Collections.Seq

Dynamic sequence (growable array) with stack and queue operations. Viper's primary growable collection type, supporting push/pop, insert/remove, and slicing operations.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Seq()` or `Viper.Collections.Seq.WithCapacity(cap)`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Len` | Integer | Number of elements currently in the sequence |
| `Cap` | Integer | Current allocated capacity |
| `IsEmpty` | Boolean | Returns true if the sequence has zero elements |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Get(index)` | `Object(Integer)` | Returns the element at the specified index (0-based) |
| `Set(index, value)` | `Void(Integer, Object)` | Sets the element at the specified index |
| `Push(value)` | `Void(Object)` | Appends an element to the end |
| `PushAll(other)` | `Void(Seq)` | Appends all elements of `other` onto this sequence (self-appends double the sequence) |
| `Pop()` | `Object()` | Removes and returns the last element |
| `Peek()` | `Object()` | Returns the last element without removing it |
| `First()` | `Object()` | Returns the first element |
| `Last()` | `Object()` | Returns the last element |
| `Insert(index, value)` | `Void(Integer, Object)` | Inserts an element at the specified position |
| `Remove(index)` | `Object(Integer)` | Removes and returns the element at the specified position |
| `Clear()` | `Void()` | Removes all elements |
| `Find(value)` | `Integer(Object)` | Returns the index of a value, or -1 if not found |
| `Has(value)` | `Boolean(Object)` | Returns true if the sequence contains the value |
| `Reverse()` | `Void()` | Reverses the elements in place |
| `Shuffle()` | `Void()` | Shuffles the elements in place (deterministic when `Viper.Random.Seed` is set) |
| `Slice(start, end)` | `Seq(Integer, Integer)` | Returns a new sequence with elements from start (inclusive) to end (exclusive) |
| `Clone()` | `Seq()` | Returns a shallow copy of the sequence |

### Example

```basic
DIM seq AS Viper.Collections.Seq
seq = NEW Viper.Collections.Seq()

' Add elements (stack-like)
seq.Push(item1)
seq.Push(item2)
seq.Push(item3)

PRINT seq.Len      ' Output: 3
PRINT seq.IsEmpty  ' Output: False

' Access elements by index
DIM first AS Object
first = seq.Get(0)

' Modify by index
seq.Set(1, newItem)

' Stack operations
DIM last AS Object
last = seq.Pop()   ' Removes and returns item3
last = seq.Peek()  ' Returns item2 without removing

' Insert and remove at arbitrary positions
seq.Insert(0, itemAtStart)  ' Insert at beginning
seq.Remove(1)               ' Remove second element

' Search
IF seq.Has(someItem) THEN
    PRINT "Found at index: "; seq.Find(someItem)
END IF

' Slicing
DIM slice AS Viper.Collections.Seq
slice = seq.Slice(1, 3)  ' Elements at indices 1 and 2

' Clone for independent copy
DIM copy AS Viper.Collections.Seq
copy = seq.Clone()

' Reverse in place
seq.Reverse()

' Push all elements from another sequence
DIM other AS Viper.Collections.Seq
other = NEW Viper.Collections.Seq()
other.Push(item4)
other.Push(item5)
seq.PushAll(other)

' Deterministic shuffle (Random.Seed influences Shuffle)
Viper.Random.Seed(1)
seq.Shuffle()

' Clear all
seq.Clear()
```

### Creating with Initial Capacity

For better performance when the size is known in advance:

```basic
DIM seq AS Viper.Collections.Seq
seq = Viper.Collections.Seq.WithCapacity(1000)

' No reallocations needed for first 1000 pushes
FOR i = 1 TO 1000
    seq.Push(items(i))
NEXT i
```

### Use Cases

- **Stack:** Use `Push()` and `Pop()` for LIFO operations
- **Queue:** Use `Push()` to add and `Remove(0)` to dequeue (FIFO)
- **Dynamic Array:** Use `Get()`/`Set()` for random access
- **Slicing:** Use `Slice()` to extract sub-sequences

---

## Viper.Collections.Stack

A LIFO (last-in-first-out) collection. Elements are added and removed from the top.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Stack()`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Len` | Integer | Number of elements on the stack |
| `IsEmpty` | Boolean | Returns true if the stack has no elements |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Push(value)` | `Void(Object)` | Add element to top of stack |
| `Pop()` | `Object()` | Remove and return top element (traps if empty) |
| `Peek()` | `Object()` | Return top element without removing (traps if empty) |
| `Clear()` | `Void()` | Remove all elements |

### Example

```basic
DIM stack AS Viper.Collections.Stack
stack = NEW Viper.Collections.Stack()

' Push elements onto the stack
stack.Push("first")
stack.Push("second")
stack.Push("third")

PRINT stack.Len      ' Output: 3
PRINT stack.IsEmpty  ' Output: False

' Pop returns elements in LIFO order
PRINT stack.Pop()    ' Output: "third"
PRINT stack.Peek()   ' Output: "second" (still on stack)
PRINT stack.Len      ' Output: 2

' Clear the stack
stack.Clear()
PRINT stack.IsEmpty  ' Output: True
```

### Use Cases

- **Undo/Redo:** Push actions to track history, pop to undo
- **Expression parsing:** Track operators and operands
- **Backtracking algorithms:** Store states to return to
- **Function call simulation:** Track return addresses

---

## Viper.Collections.Queue

A FIFO (first-in-first-out) collection. Elements are added at the back and removed from the front. Implemented as a circular buffer for O(1) add and take operations.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Queue()`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Len` | Integer | Number of elements in the queue |
| `IsEmpty` | Boolean | Returns true if the queue has no elements |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(value)` | `Void(Object)` | Add element to back of queue |
| `Take()` | `Object()` | Remove and return front element (traps if empty) |
| `Peek()` | `Object()` | Return front element without removing (traps if empty) |
| `Clear()` | `Void()` | Remove all elements |

### Example

```basic
DIM queue AS Viper.Collections.Queue
queue = NEW Viper.Collections.Queue()

' Add elements to the queue
queue.Add("first")
queue.Add("second")
queue.Add("third")

PRINT queue.Len      ' Output: 3
PRINT queue.IsEmpty  ' Output: False

' Take returns elements in FIFO order
PRINT queue.Take()   ' Output: "first"
PRINT queue.Peek()   ' Output: "second" (still in queue)
PRINT queue.Len      ' Output: 2

' Clear the queue
queue.Clear()
PRINT queue.IsEmpty  ' Output: True
```

### Use Cases

- **Task scheduling:** Process tasks in the order they arrive
- **Breadth-first search:** Track nodes to visit
- **Message passing:** Handle messages in arrival order
- **Print queues:** Process print jobs sequentially

---

## Viper.Collections.Bytes

An efficient byte array for binary data. More memory-efficient than Seq for byte manipulation.

**Type:** Instance (obj)
**Constructors:**
- `NEW Viper.Collections.Bytes(length)` - Create zero-filled byte array
- `Viper.Collections.Bytes.FromStr(str)` - Create from string (UTF-8 bytes)
- `Viper.Collections.Bytes.FromHex(hex)` - Create from hexadecimal string
- `Viper.Collections.Bytes.FromBase64(b64)` - Decode RFC 4648 Base64 string (traps on invalid input)

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Len` | Integer | Number of bytes |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Get(index)` | `Integer(Integer)` | Get byte value (0-255) at index |
| `Set(index, value)` | `Void(Integer, Integer)` | Set byte value at index (clamped to 0-255) |
| `Slice(start, end)` | `Bytes(Integer, Integer)` | Create new byte array from range [start, end) |
| `Copy(dstOffset, src, srcOffset, count)` | `Void(...)` | Copy bytes between arrays |
| `ToStr()` | `String()` | Convert to string (interprets as UTF-8) |
| `ToHex()` | `String()` | Convert to lowercase hexadecimal string |
| `ToBase64()` | `String()` | Convert to RFC 4648 Base64 string (A-Z a-z 0-9 + /, with '=' padding) |
| `Fill(value)` | `Void(Integer)` | Set all bytes to value |
| `Find(value)` | `Integer(Integer)` | Find first occurrence (-1 if not found) |
| `Clone()` | `Bytes()` | Create independent copy |

### Example

```basic
' Create a 4-byte array and set values
DIM data AS Viper.Collections.Bytes
data = NEW Viper.Collections.Bytes(4)
data.Set(0, &HDE)
data.Set(1, &HAD)
data.Set(2, &HBE)
data.Set(3, &HEF)

PRINT data.ToHex()  ' Output: "deadbeef"
PRINT data.Len      ' Output: 4

' Create from hex string
DIM copy AS Viper.Collections.Bytes
copy = Viper.Collections.Bytes.FromHex("cafebabe")
PRINT copy.Get(0)   ' Output: 202 (0xCA)

' Create from string
DIM text AS Viper.Collections.Bytes
text = Viper.Collections.Bytes.FromStr("Hello")
PRINT text.Len      ' Output: 5
PRINT text.Get(0)   ' Output: 72 (ASCII 'H')

' Base64 encode/decode (RFC 4648)
PRINT text.ToBase64()  ' Output: "SGVsbG8="
DIM decoded AS Viper.Collections.Bytes
decoded = Viper.Collections.Bytes.FromBase64("SGVsbG8=")
PRINT decoded.ToStr()  ' Output: "Hello"

' Slice and copy
DIM slice AS Viper.Collections.Bytes
slice = data.Slice(1, 3)  ' Bytes at indices 1 and 2
PRINT slice.Len           ' Output: 2

' Find a byte
PRINT data.Find(&HBE)     ' Output: 2

' Fill with a value
data.Fill(0)
PRINT data.ToHex()        ' Output: "00000000"
```

### Use Cases

- **Binary file parsing:** Read and manipulate binary file formats
- **Network protocols:** Pack and unpack protocol messages
- **Cryptography:** Handle raw byte sequences
- **Image data:** Manipulate raw pixel data
- **Base encoding:** Convert between binary and text representations

---

## Viper.Collections.Bag

A set data structure for storing unique strings. Efficiently handles membership testing, set operations (union, intersection, difference), and enumeration.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Bag()`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Len` | Integer | Number of strings in the bag |
| `IsEmpty` | Boolean | True if bag contains no strings |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Put(str)` | `Boolean(String)` | Add a string; returns true if new, false if already present |
| `Drop(str)` | `Boolean(String)` | Remove a string; returns true if removed, false if not found |
| `Has(str)` | `Boolean(String)` | Check if string is in the bag |
| `Clear()` | `Void()` | Remove all strings from the bag |
| `Items()` | `Seq()` | Get all strings as a Seq (order undefined) |
| `Merge(other)` | `Bag(Bag)` | Return new bag with union of both bags |
| `Common(other)` | `Bag(Bag)` | Return new bag with intersection of both bags |
| `Diff(other)` | `Bag(Bag)` | Return new bag with elements in this but not other |

### Notes

- Strings are stored by value (copied into the bag)
- Order of strings returned by `Items()` is not guaranteed (hash table)
- Set operations (`Merge`, `Common`, `Diff`) return new bags; originals are unchanged
- Uses FNV-1a hash function for O(1) average-case operations
- Automatically resizes when load factor exceeds 75%

### Example

```basic
' Create and populate a bag
DIM fruits AS OBJECT = NEW Viper.Collections.Bag()
fruits.Put("apple")
fruits.Put("banana")
fruits.Put("cherry")
PRINT fruits.Len           ' Output: 3

' Duplicate add returns false
DIM wasNew AS INTEGER = fruits.Put("apple")
PRINT wasNew               ' Output: 0 (already present)

' Membership testing
PRINT fruits.Has("banana") ' Output: 1 (true)
PRINT fruits.Has("grape")  ' Output: 0 (false)

' Remove an element
DIM removed AS INTEGER = fruits.Drop("banana")
PRINT removed              ' Output: 1 (was removed)
PRINT fruits.Has("banana") ' Output: 0 (no longer present)

' Set operations
DIM bagA AS OBJECT = NEW Viper.Collections.Bag()
bagA.Put("a")
bagA.Put("b")
bagA.Put("c")

DIM bagB AS OBJECT = NEW Viper.Collections.Bag()
bagB.Put("b")
bagB.Put("c")
bagB.Put("d")

' Union: elements in either bag
DIM merged AS OBJECT = bagA.Merge(bagB)
PRINT merged.Len           ' Output: 4 (a, b, c, d)

' Intersection: elements in both bags
DIM common AS OBJECT = bagA.Common(bagB)
PRINT common.Len           ' Output: 2 (b, c)

' Difference: elements in A but not B
DIM diff AS OBJECT = bagA.Diff(bagB)
PRINT diff.Len             ' Output: 1 (a only)

' Enumerate all elements
DIM items AS OBJECT = fruits.Items()
FOR i AS INTEGER = 0 TO items.Len - 1
    PRINT items.Get(i)
NEXT
```

### Use Cases

- **Deduplication:** Track unique values encountered
- **Membership testing:** Fast O(1) lookup for string membership
- **Set mathematics:** Compute unions, intersections, and differences
- **Tag systems:** Manage collections of unique tags or labels
- **Visited tracking:** Track visited items in algorithms

---

## Viper.Collections.Ring

A fixed-size circular buffer (ring buffer). When full, pushing new elements automatically overwrites the oldest elements.

**Type:** Instance class

**Constructor:** `NEW Viper.Collections.Ring(capacity)`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Len` | `Integer` | Number of elements currently stored |
| `Cap` | `Integer` | Maximum capacity (fixed at creation) |
| `IsEmpty` | `Boolean` | True if ring has no elements |
| `IsFull` | `Boolean` | True if ring is at capacity |

### Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `Push(item)` | void | Add item; overwrites oldest if full |
| `Pop()` | Object | Remove and return oldest item (NULL if empty) |
| `Peek()` | Object | Return oldest item without removing (NULL if empty) |
| `Get(index)` | Object | Get item by logical index (0 = oldest) |
| `Clear()` | void | Remove all elements |

### Example

```basic
' Create a ring buffer with capacity 3
DIM recent AS OBJECT = NEW Viper.Collections.Ring(3)

' Push some values
recent.Push("first")
recent.Push("second")
recent.Push("third")
PRINT recent.Len        ' Output: 3
PRINT recent.IsFull     ' Output: 1 (true)

' Push when full overwrites oldest
recent.Push("fourth")
PRINT recent.Len        ' Output: 3 (still 3)
PRINT recent.Peek()     ' Output: second (first was overwritten)

' Get by index (0 = oldest)
PRINT recent.Get(0)     ' Output: second
PRINT recent.Get(1)     ' Output: third
PRINT recent.Get(2)     ' Output: fourth

' Pop removes oldest (FIFO)
DIM oldest AS STRING = recent.Pop()
PRINT oldest            ' Output: second
PRINT recent.Len        ' Output: 2

' Interleaved push/pop
recent.Push("fifth")
PRINT recent.Pop()      ' Output: third
PRINT recent.Pop()      ' Output: fourth
PRINT recent.Pop()      ' Output: fifth
PRINT recent.IsEmpty    ' Output: 1 (true)
```

### Use Cases

- **Recent history:** Keep N most recent log entries, commands, or events
- **Sliding window:** Process data in fixed-size windows
- **Bounded caching:** Cache with automatic eviction of oldest entries
- **Event buffering:** Buffer events with guaranteed memory bounds
- **Audio/video buffering:** Fixed-size media sample buffers

---

## Viper.Math

Mathematical functions and constants.

**Type:** Static utility class

### Constants

| Property | Type | Description |
|----------|------|-------------|
| `Pi` | `Double` | π (3.14159265358979...) |
| `E` | `Double` | Euler's number (2.71828182845904...) |
| `Tau` | `Double` | τ = 2π (6.28318530717958...) |

### Basic Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Abs(x)` | `Double(Double)` | Absolute value of a floating-point number |
| `AbsInt(x)` | `Integer(Integer)` | Absolute value of an integer |
| `Sqrt(x)` | `Double(Double)` | Square root |
| `Pow(base, exp)` | `Double(Double, Double)` | Raises base to the power of exp |
| `Exp(x)` | `Double(Double)` | e raised to the power x |
| `Sgn(x)` | `Double(Double)` | Sign of x: -1, 0, or 1 |
| `SgnInt(x)` | `Integer(Integer)` | Sign of integer x: -1, 0, or 1 |

### Trigonometric Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sin(x)` | `Double(Double)` | Sine (radians) |
| `Cos(x)` | `Double(Double)` | Cosine (radians) |
| `Tan(x)` | `Double(Double)` | Tangent (radians) |
| `Atan(x)` | `Double(Double)` | Arctangent (returns radians) |
| `Atan2(y, x)` | `Double(Double, Double)` | Arctangent of y/x (returns radians, respects quadrant) |
| `Asin(x)` | `Double(Double)` | Arc sine (returns radians) |
| `Acos(x)` | `Double(Double)` | Arc cosine (returns radians) |

### Hyperbolic Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sinh(x)` | `Double(Double)` | Hyperbolic sine |
| `Cosh(x)` | `Double(Double)` | Hyperbolic cosine |
| `Tanh(x)` | `Double(Double)` | Hyperbolic tangent |

### Logarithmic Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Log(x)` | `Double(Double)` | Natural logarithm (base e) |
| `Log10(x)` | `Double(Double)` | Base-10 logarithm |
| `Log2(x)` | `Double(Double)` | Base-2 logarithm |

### Rounding Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Floor(x)` | `Double(Double)` | Largest integer less than or equal to x |
| `Ceil(x)` | `Double(Double)` | Smallest integer greater than or equal to x |
| `Round(x)` | `Double(Double)` | Round to nearest integer |
| `Trunc(x)` | `Double(Double)` | Truncate toward zero |

### Min/Max Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Min(a, b)` | `Double(Double, Double)` | Smaller of two floating-point values |
| `Max(a, b)` | `Double(Double, Double)` | Larger of two floating-point values |
| `MinInt(a, b)` | `Integer(Integer, Integer)` | Smaller of two integers |
| `MaxInt(a, b)` | `Integer(Integer, Integer)` | Larger of two integers |
| `Clamp(val, lo, hi)` | `Double(Double, Double, Double)` | Constrain value to range [lo, hi] |
| `ClampInt(val, lo, hi)` | `Integer(Integer, Integer, Integer)` | Constrain integer to range [lo, hi] |

### Utility Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `FMod(x, y)` | `Double(Double, Double)` | Floating-point remainder of x/y |
| `Lerp(a, b, t)` | `Double(Double, Double, Double)` | Linear interpolation: a + t*(b-a) |
| `Wrap(val, lo, hi)` | `Double(Double, Double, Double)` | Wrap value to range [lo, hi) |
| `WrapInt(val, lo, hi)` | `Integer(Integer, Integer, Integer)` | Wrap integer to range [lo, hi) |
| `Hypot(x, y)` | `Double(Double, Double)` | Hypotenuse: sqrt(x² + y²) |

### Angle Conversion

| Method | Signature | Description |
|--------|-----------|-------------|
| `Deg(radians)` | `Double(Double)` | Convert radians to degrees |
| `Rad(degrees)` | `Double(Double)` | Convert degrees to radians |

### Example

```basic
' Using constants
PRINT Viper.Math.Pi              ' Output: 3.14159265358979
PRINT Viper.Math.E               ' Output: 2.71828182845905

' Basic math
PRINT Viper.Math.Sqrt(16)        ' Output: 4.0
PRINT Viper.Math.Pow(2, 10)      ' Output: 1024.0
PRINT Viper.Math.Abs(-42.5)      ' Output: 42.5

' Rounding
PRINT Viper.Math.Floor(2.7)      ' Output: 2.0
PRINT Viper.Math.Ceil(2.1)       ' Output: 3.0
PRINT Viper.Math.Round(2.5)      ' Output: 3.0
PRINT Viper.Math.Trunc(-2.7)     ' Output: -2.0

' Trigonometry (radians)
PRINT Viper.Math.Sin(Viper.Math.Pi / 2)  ' Output: 1.0
PRINT Viper.Math.Cos(0)                   ' Output: 1.0

' Angle conversion
PRINT Viper.Math.Deg(Viper.Math.Pi)      ' Output: 180.0
PRINT Viper.Math.Rad(90)                  ' Output: 1.5707963...

' Min/Max/Clamp
PRINT Viper.Math.MaxInt(10, 20)          ' Output: 20
PRINT Viper.Math.Clamp(15, 0, 10)        ' Output: 10.0

' Lerp and Wrap
PRINT Viper.Math.Lerp(0, 100, 0.5)       ' Output: 50.0
PRINT Viper.Math.Wrap(370, 0, 360)       ' Output: 10.0

' Geometry
PRINT Viper.Math.Hypot(3, 4)             ' Output: 5.0
```

---

## Viper.Terminal

Terminal input and output operations.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `PrintStr(text)` | `Void(String)` | Writes text to standard output |
| `PrintI64(value)` | `Void(I64)` | Writes an integer to standard output |
| `PrintF64(value)` | `Void(F64)` | Writes a floating-point number to standard output |
| `ReadLine()` | `String()` | Reads a line of text from standard input |

### Example

```basic
Viper.Terminal.PrintStr("What is your name?")
Viper.Terminal.PrintStr(CHR$(10))  ' Newline
DIM name AS STRING
name = Viper.Terminal.ReadLine()
Viper.Terminal.PrintStr("Hello, " + name + "!")
Viper.Terminal.PrintStr(CHR$(10))
```

### Note

For most BASIC programs, the `PRINT` and `INPUT` statements are more convenient. Use `Viper.Terminal` when you need explicit control or are working at the IL level.

### Backward Compatibility

`Viper.Console.*` names are retained as aliases for backward compatibility. New code should use `Viper.Terminal.*`.

---

## Viper.Convert

Type conversion utilities.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `ToInt64(text)` | `Integer(String)` | Parses a string as a 64-bit integer |
| `ToDouble(text)` | `Double(String)` | Parses a string as a double-precision float |
| `ToString_Int(value)` | `String(Integer)` | Converts an integer to its string representation |
| `ToString_Double(value)` | `String(Double)` | Converts a double to its string representation |

### Example

```basic
DIM s AS STRING
s = "12345"

DIM n AS INTEGER
n = Viper.Convert.ToInt64(s)
PRINT n + 1  ' Output: 12346

DIM d AS DOUBLE
d = Viper.Convert.ToDouble("3.14159")
PRINT d * 2  ' Output: 6.28318

' Convert back to string
PRINT Viper.Convert.ToString_Int(42)      ' Output: "42"
PRINT Viper.Convert.ToString_Double(2.5)  ' Output: "2.5"
```

---

## Viper.Random

Random number generation.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Seed(value)` | `Void(Integer)` | Seeds the random number generator |
| `Next()` | `Double()` | Returns a random double in the range [0.0, 1.0) |
| `NextInt(max)` | `Integer(Integer)` | Returns a random integer in the range [0, max) |

### Example

```basic
' Seed with current time for different sequences each run
Viper.Random.Seed(12345)

' Random float between 0 and 1
DIM r AS DOUBLE
r = Viper.Random.Next()
PRINT r  ' Output: 0.123... (varies)

' Random integer 0-99
DIM n AS INTEGER
n = Viper.Random.NextInt(100)
PRINT n  ' Output: 0-99 (varies)

' Simulate dice roll (1-6)
DIM die AS INTEGER
die = Viper.Random.NextInt(6) + 1
PRINT "You rolled: "; die
```

---

## Viper.Environment

Command-line arguments and environment access.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `GetArgumentCount()` | `Integer()` | Returns the number of command-line arguments |
| `GetArgument(index)` | `String(Integer)` | Returns the argument at the specified index (0-based) |
| `GetCommandLine()` | `String()` | Returns the full command line as a single string |
| `GetVariable(name)` | `String(String)` | Returns the value of an environment variable, or `""` when missing |
| `HasVariable(name)` | `Boolean(String)` | Returns `TRUE` when the environment variable exists |
| `IsNative()` | `Boolean()` | Returns `TRUE` when running native code, `FALSE` when running in the VM |
| `SetVariable(name, value)` | `Void(String, String)` | Sets or overwrites an environment variable (empty value allowed) |
| `EndProgram(code)` | `Void(Integer)` | Terminates the program with the provided exit code |

### Example

```basic
' Program invoked as: ./program arg1 arg2 arg3

DIM count AS INTEGER
count = Viper.Environment.GetArgumentCount()
PRINT "Argument count: "; count  ' Output: 3

FOR i = 0 TO count - 1
    PRINT "Arg "; i; ": "; Viper.Environment.GetArgument(i)
NEXT i
' Output:
' Arg 0: arg1
' Arg 1: arg2
' Arg 2: arg3

PRINT "Full command: "; Viper.Environment.GetCommandLine()

' Environment variables
DIM name AS STRING
DIM value AS STRING
name = "VIPER_SAMPLE_ENV"
value = Viper.Environment.GetVariable(name)
IF Viper.Environment.HasVariable(name) THEN
    PRINT name; " is set to "; value
ELSE
    PRINT name; " is not set"
END IF

Viper.Environment.SetVariable(name, "abc")
PRINT "Updated value: "; Viper.Environment.GetVariable(name)

' Process exit
' Viper.Environment.EndProgram(7)
```

---

## Viper.Exec

External command execution for running system commands and capturing output.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Run(program)` | `Integer(String)` | Execute program, wait for completion, return exit code |
| `RunArgs(program, args)` | `Integer(String, Seq)` | Execute program with arguments, return exit code |
| `Capture(program)` | `String(String)` | Execute program, capture and return stdout |
| `CaptureArgs(program, args)` | `String(String, Seq)` | Execute program with arguments, capture stdout |
| `Shell(command)` | `Integer(String)` | Run command through system shell, return exit code |
| `ShellCapture(command)` | `String(String)` | Run command through shell, capture stdout |

### Security Warning

**Shell injection vulnerability:** The `Shell` and `ShellCapture` methods pass commands directly to the system shell (`/bin/sh -c` on Unix, `cmd /c` on Windows). Never pass unsanitized user input to these functions. If you need to run a command with user-provided data, use `RunArgs` or `CaptureArgs` instead, which safely handle arguments without shell interpretation.

```basic
' DANGEROUS - shell injection risk:
userInput = "file.txt; rm -rf /"
Viper.Exec.Shell("cat " + userInput)  ' DO NOT DO THIS

' SAFE - use RunArgs with explicit arguments:
DIM args AS OBJECT = Viper.Collections.Seq.New()
args.Push(userInput)
Viper.Exec.RunArgs("/bin/cat", args)  ' Arguments are passed directly
```

### Example

```basic
' Simple command execution
DIM exitCode AS INTEGER
exitCode = Viper.Exec.Shell("echo Hello World")
PRINT "Exit code: "; exitCode

' Capture command output
DIM output AS STRING
output = Viper.Exec.ShellCapture("date")
PRINT "Current date: "; output

' Execute program with arguments
DIM args AS OBJECT = Viper.Collections.Seq.New()
args.Push("-l")
args.Push("-a")
exitCode = Viper.Exec.RunArgs("/bin/ls", args)

' Capture output with arguments
DIM result AS STRING
args = Viper.Collections.Seq.New()
args.Push("--version")
result = Viper.Exec.CaptureArgs("/usr/bin/python3", args)
PRINT "Python version: "; result

' Check if command succeeded
IF Viper.Exec.Shell("test -f /etc/passwd") = 0 THEN
    PRINT "File exists"
ELSE
    PRINT "File does not exist"
END IF

' Run a script and check result
exitCode = Viper.Exec.Shell("./myscript.sh")
IF exitCode <> 0 THEN
    PRINT "Script failed with exit code: "; exitCode
END IF
```

### Platform Notes

- **Run/RunArgs/Capture/CaptureArgs**: Execute programs directly using `posix_spawn` (Unix) or `CreateProcess` (Windows). Arguments are passed without shell interpretation.
- **Shell/ShellCapture**: Use `/bin/sh -c` on Unix or `cmd /c` on Windows. Commands are interpreted by the shell.
- Exit codes: 0 typically indicates success. Negative values indicate the process was terminated by a signal (Unix) or failed to start.
- Capture functions return empty string if the program fails to start.

---

## Viper.Machine

System information queries providing read-only access to machine properties.

**Type:** Static utility class

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `OS` | `String` | Operating system name: `"linux"`, `"macos"`, `"windows"`, or `"unknown"` |
| `OSVer` | `String` | Operating system version string (e.g., `"14.2.1"` on macOS) |
| `Host` | `String` | Machine hostname |
| `User` | `String` | Current username |
| `Home` | `String` | Path to user's home directory |
| `Temp` | `String` | Path to system temporary directory |
| `Cores` | `Integer` | Number of logical CPU cores |
| `MemTotal` | `Integer` | Total RAM in bytes |
| `MemFree` | `Integer` | Available RAM in bytes |
| `Endian` | `String` | Byte order: `"little"` or `"big"` |

### Example

```basic
' Operating system information
PRINT "OS: "; Viper.Machine.OS
PRINT "Version: "; Viper.Machine.OSVer

' User and host
PRINT "User: "; Viper.Machine.User
PRINT "Host: "; Viper.Machine.Host

' Directory paths
PRINT "Home: "; Viper.Machine.Home
PRINT "Temp: "; Viper.Machine.Temp

' Hardware information
PRINT "CPU Cores: "; Viper.Machine.Cores
PRINT "Total RAM: "; Viper.Machine.MemTotal / 1073741824; " GB"
PRINT "Free RAM: "; Viper.Machine.MemFree / 1073741824; " GB"

' System characteristics
PRINT "Byte Order: "; Viper.Machine.Endian

' Conditional behavior based on OS
IF Viper.Machine.OS = "macos" THEN
    PRINT "Running on macOS"
ELSEIF Viper.Machine.OS = "linux" THEN
    PRINT "Running on Linux"
ELSEIF Viper.Machine.OS = "windows" THEN
    PRINT "Running on Windows"
END IF

' Check available memory before large allocation
DIM requiredMem AS INTEGER = 1073741824  ' 1 GB
IF Viper.Machine.MemFree > requiredMem THEN
    PRINT "Sufficient memory available"
ELSE
    PRINT "Warning: Low memory"
END IF
```

### Platform Notes

- **OS**: Returns lowercase platform identifier. Compile-time detection.
- **OSVer**: On macOS reads `kern.osproductversion` via sysctl. On Linux reads `/etc/os-release` VERSION_ID. Falls back to `uname` release string.
- **Host**: Uses `gethostname()` on Unix, `GetComputerName()` on Windows.
- **User**: Uses `getpwuid()` on Unix, `GetUserName()` on Windows, with fallback to environment variables.
- **Home**: Uses `HOME` environment variable on Unix, `USERPROFILE` on Windows.
- **Temp**: Uses `TMPDIR`/`TMP`/`TEMP` environment variables on Unix (defaulting to `/tmp`), `GetTempPath()` on Windows.
- **Cores**: Returns logical (hyper-threaded) core count via `sysconf(_SC_NPROCESSORS_ONLN)` on Unix, `GetSystemInfo()` on Windows.
- **MemTotal/MemFree**: On macOS uses `sysctl` and `host_statistics64`. On Linux uses `sysinfo()`. On Windows uses `GlobalMemoryStatusEx()`.
- **Endian**: Runtime detection via union trick. Most modern systems are little-endian.

---

## Viper.DateTime

Date and time operations. Timestamps are Unix timestamps (seconds since January 1, 1970 UTC).

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Now()` | `Integer()` | Returns the current Unix timestamp (seconds) |
| `NowMs()` | `Integer()` | Returns the current timestamp in milliseconds |
| `Year(timestamp)` | `Integer(Integer)` | Extracts the year from a timestamp |
| `Month(timestamp)` | `Integer(Integer)` | Extracts the month (1-12) from a timestamp |
| `Day(timestamp)` | `Integer(Integer)` | Extracts the day of month (1-31) from a timestamp |
| `Hour(timestamp)` | `Integer(Integer)` | Extracts the hour (0-23) from a timestamp |
| `Minute(timestamp)` | `Integer(Integer)` | Extracts the minute (0-59) from a timestamp |
| `Second(timestamp)` | `Integer(Integer)` | Extracts the second (0-59) from a timestamp |
| `DayOfWeek(timestamp)` | `Integer(Integer)` | Returns day of week (0=Sunday, 6=Saturday) |
| `Format(timestamp, format)` | `String(Integer, String)` | Formats a timestamp using strftime-style format |
| `ToISO(timestamp)` | `String(Integer)` | Returns ISO 8601 formatted string |
| `Create(y, m, d, h, min, s)` | `Integer(Integer...)` | Creates a timestamp from components |
| `AddSeconds(timestamp, seconds)` | `Integer(Integer, Integer)` | Adds seconds to a timestamp |
| `AddDays(timestamp, days)` | `Integer(Integer, Integer)` | Adds days to a timestamp |
| `Diff(t1, t2)` | `Integer(Integer, Integer)` | Returns the difference in seconds between two timestamps |

### Example

```basic
' Get current time
DIM now AS INTEGER
now = Viper.DateTime.Now()

' Extract components
PRINT "Year: "; Viper.DateTime.Year(now)
PRINT "Month: "; Viper.DateTime.Month(now)
PRINT "Day: "; Viper.DateTime.Day(now)
PRINT "Hour: "; Viper.DateTime.Hour(now)

' Format as string
PRINT Viper.DateTime.Format(now, "%Y-%m-%d %H:%M:%S")
' Output: "2025-12-06 14:30:00"

PRINT Viper.DateTime.ToISO(now)
' Output: "2025-12-06T14:30:00"

' Create a specific date
DIM birthday AS INTEGER
birthday = Viper.DateTime.Create(1990, 6, 15, 0, 0, 0)

' Date arithmetic
DIM tomorrow AS INTEGER
tomorrow = Viper.DateTime.AddDays(now, 1)

DIM nextWeek AS INTEGER
nextWeek = Viper.DateTime.AddDays(now, 7)

' Calculate difference
DIM age_seconds AS INTEGER
age_seconds = Viper.DateTime.Diff(now, birthday)
```

### Format Specifiers

| Specifier | Description | Example |
|-----------|-------------|---------|
| `%Y` | 4-digit year | 2025 |
| `%m` | 2-digit month | 01-12 |
| `%d` | 2-digit day | 01-31 |
| `%H` | 24-hour hour | 00-23 |
| `%M` | Minute | 00-59 |
| `%S` | Second | 00-59 |
| `%A` | Full weekday name | Monday |
| `%B` | Full month name | January |

---

## Viper.Time.Clock

Basic timing utilities for sleeping and measuring elapsed time.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sleep(ms)` | `Void(Integer)` | Pause execution for the specified number of milliseconds |
| `Ticks()` | `Integer()` | Returns monotonic time in milliseconds since an unspecified epoch |
| `TicksUs()` | `Integer()` | Returns monotonic time in microseconds since an unspecified epoch |

### Notes

- `Ticks()` and `TicksUs()` return monotonic, non-decreasing values suitable for measuring elapsed time
- The epoch (starting point) is unspecified - only use these functions for measuring durations, not absolute time
- `TicksUs()` provides microsecond resolution for high-precision timing
- `Sleep(0)` returns immediately without sleeping
- Negative values passed to `Sleep()` are treated as 0

### Example

```basic
' Measure execution time
DIM startMs AS INTEGER = Viper.Time.Clock.Ticks()

' Do some work...
FOR i = 1 TO 1000000
    ' busy loop
NEXT

DIM endMs AS INTEGER = Viper.Time.Clock.Ticks()
PRINT "Elapsed time: "; endMs - startMs; " ms"

' High-precision timing with microseconds
DIM startUs AS INTEGER = Viper.Time.Clock.TicksUs()
' ... fast operation ...
DIM endUs AS INTEGER = Viper.Time.Clock.TicksUs()
PRINT "Elapsed: "; endUs - startUs; " microseconds"

' Sleep for a short delay
Viper.Time.Clock.Sleep(100)  ' Sleep for 100ms
```

---

## Viper.Vec2

2D vector math for positions, directions, velocities, and physics calculations.

**Type:** Instance (obj)
**Constructor:** `Viper.Vec2.New(x, y)` or `Viper.Vec2.Zero()` or `Viper.Vec2.One()`

### Static Constructors

| Method | Signature | Description |
|--------|-----------|-------------|
| `New(x, y)` | `obj(f64, f64)` | Create a new vector with given components |
| `Zero()` | `obj()` | Create a vector at origin (0, 0) |
| `One()` | `obj()` | Create a vector (1, 1) |

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `X` | `f64` | X component (read-only) |
| `Y` | `f64` | Y component (read-only) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(other)` | `obj(obj)` | Add two vectors: self + other |
| `Sub(other)` | `obj(obj)` | Subtract vectors: self - other |
| `Mul(scalar)` | `obj(f64)` | Multiply by scalar: self * s |
| `Div(scalar)` | `obj(f64)` | Divide by scalar: self / s |
| `Neg()` | `obj()` | Negate vector: -self |
| `Dot(other)` | `f64(obj)` | Dot product of two vectors |
| `Cross(other)` | `f64(obj)` | 2D cross product (scalar z-component) |
| `Len()` | `f64()` | Length (magnitude) of vector |
| `LenSq()` | `f64()` | Squared length (avoids sqrt) |
| `Norm()` | `obj()` | Normalize to unit length |
| `Dist(other)` | `f64(obj)` | Distance to another point |
| `Lerp(other, t)` | `obj(obj, f64)` | Linear interpolation (t=0 returns self, t=1 returns other) |
| `Angle()` | `f64()` | Angle in radians (atan2(y, x)) |
| `Rotate(angle)` | `obj(f64)` | Rotate by angle in radians |

### Notes

- Vectors are immutable - all operations return new vectors
- `Norm()` returns zero vector if input has zero length
- `Div()` traps on division by zero
- `Cross()` returns the scalar z-component of the 3D cross product (treating 2D vectors as 3D with z=0)
- Angles are in radians; use `Viper.Math.Rad()` and `Viper.Math.Deg()` for conversion

### Example

```basic
' Create vectors
DIM pos AS OBJECT = Viper.Vec2.New(100.0, 200.0)
DIM vel AS OBJECT = Viper.Vec2.New(5.0, -3.0)

' Move position by velocity
pos = pos.Add(vel)
PRINT "Position: ("; pos.X; ", "; pos.Y; ")"

' Calculate distance
DIM target AS OBJECT = Viper.Vec2.New(150.0, 180.0)
DIM dist AS DOUBLE = pos.Dist(target)
PRINT "Distance to target: "; dist

' Normalize to get direction
DIM dir AS OBJECT = vel.Norm()
PRINT "Direction: ("; dir.X; ", "; dir.Y; ")"
PRINT "Direction length: "; dir.Len()  ' Should be 1.0

' Rotate a vector 90 degrees
DIM right AS OBJECT = Viper.Vec2.New(1.0, 0.0)
DIM up AS OBJECT = right.Rotate(3.14159265 / 2.0)
PRINT "Rotated: ("; up.X; ", "; up.Y; ")"  ' (0, 1)

' Linear interpolation for smooth movement
DIM start AS OBJECT = Viper.Vec2.Zero()
DIM endpoint AS OBJECT = Viper.Vec2.New(100.0, 100.0)
DIM midpoint AS OBJECT = start.Lerp(endpoint, 0.5)
PRINT "Midpoint: ("; midpoint.X; ", "; midpoint.Y; ")"  ' (50, 50)

' Dot product to check perpendicularity
DIM a AS OBJECT = Viper.Vec2.New(1.0, 0.0)
DIM b AS OBJECT = Viper.Vec2.New(0.0, 1.0)
IF a.Dot(b) = 0.0 THEN
    PRINT "Vectors are perpendicular"
END IF
```

---

## Viper.Vec3

3D vector math for positions, directions, velocities, and physics calculations in 3D space.

**Type:** Instance (obj)
**Constructor:** `Viper.Vec3.New(x, y, z)` or `Viper.Vec3.Zero()` or `Viper.Vec3.One()`

### Static Constructors

| Method | Signature | Description |
|--------|-----------|-------------|
| `New(x, y, z)` | `obj(f64, f64, f64)` | Create a new vector with given components |
| `Zero()` | `obj()` | Create a vector at origin (0, 0, 0) |
| `One()` | `obj()` | Create a vector (1, 1, 1) |

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `X` | `f64` | X component (read-only) |
| `Y` | `f64` | Y component (read-only) |
| `Z` | `f64` | Z component (read-only) |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(other)` | `obj(obj)` | Add two vectors: self + other |
| `Sub(other)` | `obj(obj)` | Subtract vectors: self - other |
| `Mul(scalar)` | `obj(f64)` | Multiply by scalar: self * s |
| `Div(scalar)` | `obj(f64)` | Divide by scalar: self / s |
| `Neg()` | `obj()` | Negate vector: -self |
| `Dot(other)` | `f64(obj)` | Dot product of two vectors |
| `Cross(other)` | `obj(obj)` | Cross product (returns Vec3) |
| `Len()` | `f64()` | Length (magnitude) of vector |
| `LenSq()` | `f64()` | Squared length (avoids sqrt) |
| `Norm()` | `obj()` | Normalize to unit length |
| `Dist(other)` | `f64(obj)` | Distance to another point |
| `Lerp(other, t)` | `obj(obj, f64)` | Linear interpolation (t=0 returns self, t=1 returns other) |

### Notes

- Vectors are immutable - all operations return new vectors
- `Norm()` returns zero vector if input has zero length
- `Div()` traps on division by zero
- `Cross()` returns a Vec3 perpendicular to both input vectors (right-hand rule)
- The cross product formula: a × b = (ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx)

### Example

```basic
' Create 3D vectors
DIM pos AS OBJECT = Viper.Vec3.New(100.0, 200.0, 50.0)
DIM vel AS OBJECT = Viper.Vec3.New(5.0, -3.0, 2.0)

' Move position by velocity
pos = pos.Add(vel)
PRINT "Position: ("; pos.X; ", "; pos.Y; ", "; pos.Z; ")"

' Calculate distance in 3D
DIM target AS OBJECT = Viper.Vec3.New(150.0, 180.0, 60.0)
DIM dist AS DOUBLE = pos.Dist(target)
PRINT "Distance to target: "; dist

' Normalize to get direction
DIM dir AS OBJECT = vel.Norm()
PRINT "Direction length: "; dir.Len()  ' Should be 1.0

' Cross product for surface normals
DIM edge1 AS OBJECT = Viper.Vec3.New(1.0, 0.0, 0.0)
DIM edge2 AS OBJECT = Viper.Vec3.New(0.0, 1.0, 0.0)
DIM normal AS OBJECT = edge1.Cross(edge2)
PRINT "Normal: ("; normal.X; ", "; normal.Y; ", "; normal.Z; ")"  ' (0, 0, 1)

' Verify cross product is perpendicular
PRINT "Dot with edge1: "; normal.Dot(edge1)  ' 0
PRINT "Dot with edge2: "; normal.Dot(edge2)  ' 0

' Linear interpolation for smooth 3D movement
DIM start AS OBJECT = Viper.Vec3.Zero()
DIM endpoint AS OBJECT = Viper.Vec3.New(100.0, 100.0, 100.0)
DIM midpoint AS OBJECT = start.Lerp(endpoint, 0.5)
PRINT "Midpoint: ("; midpoint.X; ", "; midpoint.Y; ", "; midpoint.Z; ")"  ' (50, 50, 50)
```

---

## Viper.Diagnostics.Assert

Runtime assertion helper that terminates execution when a required condition is
not satisfied.

**Type:** Static function

### Signature

| Function | Signature | Description |
|----------|-----------|-------------|
| `Viper.Diagnostics.Assert` | `Void(Boolean, String)` | Traps when @p condition is false; uses default message when empty |

### Notes

- When `condition` is zero/false, the runtime traps (prints to stderr and exits with status 1).
- An empty string or `""` message is replaced with `"Assertion failed"` to keep diagnostics informative.

### Example

```basic
DIM count AS INTEGER
count = 3

' Passes: execution continues
Viper.Diagnostics.Assert(count > 0, "count must be positive")

' Fails: terminates with the provided message
Viper.Diagnostics.Assert(count < 0, "boom")
```

---

## Viper.Diagnostics.Stopwatch

High-precision stopwatch for benchmarking and performance measurement. Supports pause/resume timing with nanosecond resolution.

**Type:** Instance class (requires `New()` or `StartNew()`)

### Constructors

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `Stopwatch()` | Create a new stopped stopwatch |
| `StartNew()` | `Stopwatch()` | Create and immediately start a new stopwatch |

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `ElapsedMs` | `Integer` (read-only) | Total elapsed time in milliseconds |
| `ElapsedUs` | `Integer` (read-only) | Total elapsed time in microseconds |
| `ElapsedNs` | `Integer` (read-only) | Total elapsed time in nanoseconds |
| `IsRunning` | `Boolean` (read-only) | True if stopwatch is currently running |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Start()` | `Void()` | Start or resume timing (no effect if already running) |
| `Stop()` | `Void()` | Pause timing, preserving accumulated time |
| `Reset()` | `Void()` | Stop and clear all accumulated time |
| `Restart()` | `Void()` | Reset and start in one atomic operation |

### Notes

- Stopwatch provides nanosecond resolution on supported platforms
- Time accumulates across multiple Start/Stop cycles until Reset
- Reading `ElapsedMs`/`ElapsedUs`/`ElapsedNs` while running returns current elapsed time
- `Start()` has no effect if already running (doesn't reset)
- `Stop()` has no effect if already stopped

### Example

```basic
' Create and start a stopwatch
DIM sw AS OBJECT = Viper.Diagnostics.Stopwatch.StartNew()

' Code to benchmark
FOR i = 1 TO 1000000
    DIM x AS INTEGER = i * i
NEXT

sw.Stop()
PRINT "Elapsed: "; sw.ElapsedMs; " ms"
PRINT "Elapsed: "; sw.ElapsedUs; " us"
PRINT "Elapsed: "; sw.ElapsedNs; " ns"

' Resume timing for additional work
sw.Start()
FOR i = 1 TO 500000
    DIM x AS INTEGER = i * i
NEXT
sw.Stop()
PRINT "Total: "; sw.ElapsedMs; " ms"

' Reset and restart
sw.Restart()
' More benchmarking...
sw.Stop()
PRINT "New timing: "; sw.ElapsedMs; " ms"
```

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

2D graphics canvas for visual applications and games.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.Canvas(title, width, height)`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Width` | Integer | Canvas width in pixels |
| `Height` | Integer | Canvas height in pixels |
| `ShouldClose` | Integer | Non-zero if the user requested to close the canvas |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Flip()` | `Void()` | Presents the back buffer and displays drawn content |
| `Clear(color)` | `Void(Integer)` | Clears the canvas with a solid color |
| `Line(x1, y1, x2, y2, color)` | `Void(Integer...)` | Draws a line between two points |
| `Box(x, y, w, h, color)` | `Void(Integer...)` | Draws a filled rectangle |
| `Frame(x, y, w, h, color)` | `Void(Integer...)` | Draws a rectangle outline |
| `Disc(cx, cy, r, color)` | `Void(Integer...)` | Draws a filled circle |
| `Ring(cx, cy, r, color)` | `Void(Integer...)` | Draws a circle outline |
| `Plot(x, y, color)` | `Void(Integer, Integer, Integer)` | Sets a single pixel |
| `Poll()` | `Integer()` | Polls for input events; returns event type (0 = none) |
| `KeyHeld(keycode)` | `Integer(Integer)` | Returns non-zero if the specified key is held down |

### Color Format

Colors are specified as 32-bit integers in `0x00RRGGBB` format:
- Red: `0x00FF0000`
- Green: `0x0000FF00`
- Blue: `0x000000FF`
- White: `0x00FFFFFF`
- Black: `0x00000000`

Use `Viper.Graphics.Color.RGB()` or `Viper.Graphics.Color.RGBA()` to create colors from components.

### Example

```basic
' Create a canvas
DIM canvas AS Viper.Graphics.Canvas
canvas = NEW Viper.Graphics.Canvas("My Game", 800, 600)

' Main loop
DO WHILE canvas.ShouldClose = 0
    ' Poll events
    canvas.Poll()

    ' Clear to black
    canvas.Clear(&H00000000)

    ' Draw a red filled rectangle
    canvas.Box(100, 100, 200, 150, &H00FF0000)

    ' Draw a blue filled circle
    canvas.Disc(400, 300, 50, &H000000FF)

    ' Draw a green line
    canvas.Line(0, 0, 800, 600, &H0000FF00)

    ' Draw a white rectangle outline
    canvas.Frame(50, 50, 100, 100, &H00FFFFFF)

    ' Draw a yellow circle outline
    canvas.Ring(600, 200, 40, &H00FFFF00)

    ' Present
    canvas.Flip()
LOOP
```

---

## Viper.Graphics.Color

Color utility functions for graphics operations.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `RGB(r, g, b)` | `Integer(Integer, Integer, Integer)` | Creates a color value from red, green, blue components (0-255 each) |
| `RGBA(r, g, b, a)` | `Integer(Integer, Integer, Integer, Integer)` | Creates a color with alpha from red, green, blue, alpha components (0-255 each) |

### Example

```basic
DIM red AS INTEGER
red = Viper.Graphics.Color.RGB(255, 0, 0)

DIM green AS INTEGER
green = Viper.Graphics.Color.RGB(0, 255, 0)

DIM blue AS INTEGER
blue = Viper.Graphics.Color.RGB(0, 0, 255)

DIM purple AS INTEGER
purple = Viper.Graphics.Color.RGB(128, 0, 128)

DIM semiTransparent AS INTEGER
semiTransparent = Viper.Graphics.Color.RGBA(255, 0, 0, 128)  ' 50% transparent red

' Use with graphics canvas
canvas.Box(10, 10, 100, 100, red)
canvas.Disc(200, 200, 50, purple)
```

---

## Viper.Graphics.Pixels

Software image buffer for direct pixel manipulation. Use for procedural texture generation, image processing, or custom rendering.

**Type:** Instance class

**Constructor:** `NEW Viper.Graphics.Pixels(width, height)`

Creates a new pixel buffer initialized to transparent black (0x00000000).

### Properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| `Width` | Integer | Read | Width of the buffer in pixels |
| `Height` | Integer | Read | Height of the buffer in pixels |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Get(x, y)` | `Integer(Integer, Integer)` | Get pixel color at (x, y) as packed RGBA (0xRRGGBBAA). Returns 0 if out of bounds |
| `Set(x, y, color)` | `Void(Integer, Integer, Integer)` | Set pixel color at (x, y). Silently ignores out of bounds |
| `Fill(color)` | `Void(Integer)` | Fill entire buffer with a color |
| `Clear()` | `Void()` | Clear buffer to transparent black (0x00000000) |
| `Copy(dx, dy, src, sx, sy, w, h)` | `Void(Integer, Integer, Pixels, Integer, Integer, Integer, Integer)` | Copy a rectangle from source to this buffer |
| `Clone()` | `Pixels()` | Create a deep copy of this buffer |
| `ToBytes()` | `Bytes()` | Convert to raw bytes (RGBA, row-major) |

### Static Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `FromBytes(width, height, bytes)` | `Pixels(Integer, Integer, Bytes)` | Create from raw bytes (RGBA, row-major) |

### Color Format

Colors are stored as packed 32-bit RGBA integers in the format `0xRRGGBBAA`:
- `RR` - Red component (0-255)
- `GG` - Green component (0-255)
- `BB` - Blue component (0-255)
- `AA` - Alpha component (0-255, where 255 = opaque)

Use `Viper.Graphics.Color.RGBA()` to create colors.

### Example

```basic
DIM pixels AS Viper.Graphics.Pixels
pixels = NEW Viper.Graphics.Pixels(256, 256)

' Create a gradient
DIM x AS INTEGER
DIM y AS INTEGER
FOR y = 0 TO 255
    FOR x = 0 TO 255
        ' Red increases left-to-right, green increases top-to-bottom
        DIM r AS INTEGER = x
        DIM g AS INTEGER = y
        DIM color AS INTEGER = Viper.Graphics.Color.RGB(r, g, 0)
        pixels.Set(x, y, color)
    NEXT x
NEXT y

' Copy a region
DIM copy AS Viper.Graphics.Pixels
copy = NEW Viper.Graphics.Pixels(64, 64)
copy.Copy(0, 0, pixels, 100, 100, 64, 64)

' Clone the entire buffer
DIM backup AS Viper.Graphics.Pixels
backup = pixels.Clone()

' Convert to bytes for serialization
DIM data AS Viper.Collections.Bytes
data = pixels.ToBytes()
```

### Notes

- Pixel data is stored in row-major order (row 0 first, then row 1, etc.)
- Out-of-bounds reads return 0 (transparent black)
- Out-of-bounds writes are silently ignored
- The `Copy` method automatically clips to buffer boundaries
- `ToBytes` returns 4 bytes per pixel (width × height × 4 total bytes)

---

## Runtime Architecture

### Overview

The Viper runtime is defined in `src/il/runtime/runtime.def` using X-macros. This single source of truth generates:

- `RuntimeNameMap.inc` — Maps canonical names to C symbols
- `RuntimeClasses.inc` — OOP class catalog for the type system

### RT_FUNC Syntax

```
RT_FUNC(id, c_symbol, "canonical_name", "signature")
```

- **id**: Unique C++ identifier used in generated code
- **c_symbol**: The C function name (rt_* prefix by convention)
- **canonical_name**: The Viper namespace path (e.g., "Viper.Math.Sin")
- **signature**: IL type signature using type abbreviations

### RT_CLASS Syntax

```
RT_CLASS_BEGIN("canonical_name", type_id, "layout", ctor_id)
    RT_PROP("name", "type", getter_id, setter_id_or_none)
    RT_METHOD("name", "signature", target_id)
RT_CLASS_END()
```

Classes define the OOP interface exposed to Viper languages. Method signatures omit the receiver (arg0).

### Type Abbreviations

| Abbrev | Type | Size |
|--------|------|------|
| `void` | No value | 0 |
| `i1` | Boolean | 1 bit |
| `i8` | Signed byte | 8 bits |
| `i16` | Short integer | 16 bits |
| `i32` | Integer | 32 bits |
| `i64` | Long integer | 64 bits |
| `f32` | Single float | 32 bits |
| `f64` | Double float | 64 bits |
| `str` | String | pointer |
| `obj` | Object | pointer |
| `ptr` | Raw pointer | pointer |

### Quick Reference

| Class | Description |
|-------|-------------|
| `Viper.Object` | Base class with Equals, GetHashCode, ToString |
| `Viper.String` | String manipulation (Substring, Trim, Replace, etc.) |
| `Viper.Strings` | Static string utilities (Join, FromInt, etc.) |
| `Viper.Math` | Math functions (Sin, Cos, Sqrt, etc.) and constants (Pi, E) |
| `Viper.Terminal` | Terminal I/O (Say, Print, Ask, ReadLine) |
| `Viper.Convert` | Type conversion (ToInt, ToDouble) |
| `Viper.Environment` | Command-line args, environment variables, process exit |
| `Viper.Random` | Random number generation |
| `Viper.Collections.Seq` | Dynamic array with Push, Pop, Get, Set |
| `Viper.Collections.Stack` | LIFO with Push, Pop, Peek |
| `Viper.Collections.Queue` | FIFO with Add, Take, Peek |
| `Viper.Collections.Map` | String-keyed dictionary |
| `Viper.Collections.Bytes` | Efficient byte array |
| `Viper.Collections.Bag` | String set with union, intersection, difference |
| `Viper.Collections.Ring` | Fixed-size circular buffer (overwrites oldest) |
| `Viper.Collections.List` | Dynamic list of objects |
| `Viper.IO.File` | File read/write/copy/delete |
| `Viper.IO.Dir` | Directory create/list/delete |
| `Viper.IO.Path` | Path join/split/normalize |
| `Viper.IO.BinFile` | Binary file stream with random access |
| `Viper.IO.LineReader` | Line-by-line text file reading |
| `Viper.IO.LineWriter` | Buffered text file writing |
| `Viper.Text.StringBuilder` | Efficient string concatenation |
| `Viper.Text.Codec` | Base64, Hex, URL encoding/decoding |
| `Viper.Text.Guid` | UUID v4 generation |
| `Viper.Crypto.Hash` | MD5, SHA1, SHA256, CRC32 hashing |
| `Viper.Graphics.Canvas` | Window and 2D drawing |
| `Viper.Graphics.Color` | RGB/RGBA color creation |
| `Viper.Graphics.Pixels` | Software image buffer for pixel manipulation |
| `Viper.Time.Clock` | Sleep and tick counting |
| `Viper.DateTime` | Date/time creation and formatting |
| `Viper.Diagnostics.Stopwatch` | Benchmarking timer |

---

## Type Reference

| Viper Type | IL Type | Description |
|------------|---------|-------------|
| `Integer` | `i64` | 64-bit signed integer |
| `Double` | `f64` | 64-bit floating point |
| `Boolean` | `i1` | Boolean (0 or 1) |
| `String` | `str` | Immutable string |
| `Object` | `obj` | Reference to any object |
| `Void` | `void` | No return value |

---

## See Also

- [IL Guide](il-guide.md) — Viper Intermediate Language specification
- [BASIC Reference](basic-reference.md) — BASIC language reference
- [Getting Started](getting-started.md) — Build and run your first program

---

*Viper Runtime Library Reference v0.1.2*
*This is pre-alpha documentation. APIs may change without notice.*
