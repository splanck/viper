# Core Types

> Foundational types that form the basis of all Viper programs.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Object](#viperobject)
- [Viper.String](#viperstring)

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

