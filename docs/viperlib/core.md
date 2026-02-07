# Core Types

> Foundational types that form the basis of all Viper programs.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Object](#viperobject)
- [Viper.Box](#viperbox)
- [Viper.String](#viperstring)

---

## Viper.Object

Base class for all Viper reference types. Provides fundamental object operations.

**Type:** Base class (not instantiated directly)

### Instance Methods

| Method          | Signature         | Description                                    |
|-----------------|-------------------|------------------------------------------------|
| `Equals(other)` | `Boolean(Object)` | Compares this object with another for equality |
| `HashCode()` | `Integer()`       | Returns a hash code for the object             |
| `ToString()`    | `String()`        | Returns a string representation of the object  |

### Static Functions

| Function                             | Signature                 | Description                                               |
|--------------------------------------|---------------------------|-----------------------------------------------------------|
| `Viper.Object.RefEquals(a, b)` | `Boolean(Object, Object)` | Tests if two references point to the same object instance |

### Zia Example

> Object is the base type for all reference types. In Zia, it is implicit—most programs interact
> with concrete types rather than calling Object methods directly.

### BASIC Example

```basic
DIM obj1 AS Viper.Object
DIM obj2 AS Viper.Object

IF obj1.Equals(obj2) THEN
    PRINT "Objects are equal"
END IF

PRINT obj1.ToString()
PRINT obj1.HashCode()

' Static function call - check if same instance
IF Viper.Object.RefEquals(obj1, obj2) THEN
    PRINT "Same object instance"
END IF
```

---

## Viper.Box

Boxing helpers for storing primitive values in generic collections. Boxed values are heap objects with type tags.

**Type:** Static utility class

### Methods

| Method           | Signature           | Description                                        |
|------------------|---------------------|----------------------------------------------------|
| `I64(value)`     | `Object(Integer)`   | Box an integer                                     |
| `F64(value)`     | `Object(Double)`    | Box a double                                       |
| `I1(value)`      | `Object(Boolean)`   | Box a boolean                                      |
| `Str(value)`     | `Object(String)`    | Box a string                                       |
| `ToI64(box)`     | `Integer(Object)`   | Unbox integer (traps on wrong type)                |
| `ToF64(box)`     | `Double(Object)`    | Unbox double (traps on wrong type)                 |
| `ToI1(box)`      | `Boolean(Object)`   | Unbox boolean (traps on wrong type)                |
| `ToStr(box)`     | `String(Object)`    | Unbox string (traps on wrong type)                 |
| `Type(box)`      | `Integer(Object)`   | Return type tag (0=i64, 1=f64, 2=i1, 3=str)         |
| `EqI64(box,val)` | `Boolean(Object,Integer)` | Compare boxed value to integer                 |
| `EqF64(box,val)` | `Boolean(Object,Double)`  | Compare boxed value to double                  |
| `EqStr(box,val)` | `Boolean(Object,String)`  | Compare boxed value to string                  |

### Notes

- Type tags: 0 = integer, 1 = double, 2 = boolean, 3 = string.
- Unboxing with the wrong type traps with a runtime diagnostic.

### Zia Example

```zia
module BoxDemo;

bind Viper.Terminal;
bind Viper.Fmt as Fmt;

func start() {
    // Box an integer (type tag 0)
    var boxed = Viper.Box.I64(42);
    Say("Type: " + Fmt.Int(Viper.Box.Type(boxed)));   // 0
    Say("Value: " + Fmt.Int(Viper.Box.ToI64(boxed)));  // 42

    // Box a string (type tag 3)
    var sbox = Viper.Box.Str("hello");
    Say("String: " + Viper.Box.ToStr(sbox));           // hello

    // Box a float (type tag 1)
    var fbox = Viper.Box.F64(3.14);
    Say("Float type: " + Fmt.Int(Viper.Box.Type(fbox)));  // 1
}
```

### BASIC Example

```basic
DIM boxed AS OBJECT
boxed = Viper.Box.I64(42)

PRINT Viper.Box.Type(boxed)         ' Output: 0
PRINT Viper.Box.ToI64(boxed)        ' Output: 42
PRINT Viper.Box.EqI64(boxed, 42)    ' Output: true
```

---

## Viper.String

String manipulation class. In Viper, strings are immutable sequences of characters.

**Type:** Instance (opaque*)

### Properties

| Property  | Type    | Description                                    |
|-----------|---------|------------------------------------------------|
| `Length`  | Integer | Returns the number of characters in the string |
| `IsEmpty` | Boolean | Returns true if the string has zero length     |

### Methods

| Method                       | Signature                  | Description                                                                   |
|------------------------------|----------------------------|-------------------------------------------------------------------------------|
| `Substring(start, length)`   | `String(Integer, Integer)` | Extracts a portion of the string starting at `start` with `length` characters |
| `Concat(other)`              | `String(String)`           | Concatenates another string and returns the result                            |
| `Left(count)`                | `String(Integer)`          | Returns the leftmost `count` characters                                       |
| `Right(count)`               | `String(Integer)`          | Returns the rightmost `count` characters                                      |
| `Mid(start)`                 | `String(Integer)`          | Returns characters from `start` to the end (1-based index)                    |
| `MidLen(start, length)`      | `String(Integer, Integer)` | Returns `length` characters starting at `start` (1-based index)               |
| `Trim()`                     | `String()`                 | Removes leading and trailing whitespace                                       |
| `TrimStart()`                | `String()`                 | Removes leading whitespace                                                    |
| `TrimEnd()`                  | `String()`                 | Removes trailing whitespace                                                   |
| `ToUpper()`                  | `String()`                 | Converts all characters to uppercase                                          |
| `ToLower()`                  | `String()`                 | Converts all characters to lowercase                                          |
| `IndexOf(search)`            | `Integer(String)`          | Returns the position of `search` within the string, or -1 if not found        |
| `IndexOfFrom(start, search)` | `Integer(Integer, String)` | Searches for `search` starting at position `start`                            |
| `Chr(code)`                  | `String(Integer)`          | Returns a single-character string from an ASCII/Unicode code point            |
| `Asc()`                      | `Integer()`                | Returns the ASCII/Unicode code of the first character                         |

### Extended Methods

**Search & Match:**

| Method               | Signature         | Description                                  |
|----------------------|-------------------|----------------------------------------------|
| `StartsWith(prefix)` | `Boolean(String)` | Returns true if string starts with prefix    |
| `EndsWith(suffix)`   | `Boolean(String)` | Returns true if string ends with suffix      |
| `Has(needle)`        | `Boolean(String)` | Returns true if string contains needle       |
| `Count(needle)`      | `Integer(String)` | Counts non-overlapping occurrences of needle |

**Transformation:**

| Method                         | Signature                 | Description                                                     |
|--------------------------------|---------------------------|-----------------------------------------------------------------|
| `Replace(needle, replacement)` | `String(String, String)`  | Replaces all occurrences of needle with replacement             |
| `PadLeft(width, padChar)`      | `String(Integer, String)` | Pads string on left to reach width using first char of padChar  |
| `PadRight(width, padChar)`     | `String(Integer, String)` | Pads string on right to reach width using first char of padChar |
| `Repeat(count)`                | `String(Integer)`         | Repeats the string count times                                  |
| `Flip()`                       | `String()`                | Reverses the string (byte-level, ASCII-safe)                    |
| `Split(delimiter)`             | `Seq(String)`             | Splits string by delimiter into a Seq of strings                |

**Comparison:**

| Method             | Signature         | Description                                      |
|--------------------|-------------------|--------------------------------------------------|
| `Cmp(other)`       | `Integer(String)` | Compares strings, returns -1, 0, or 1            |
| `CmpNoCase(other)` | `Integer(String)` | Case-insensitive comparison, returns -1, 0, or 1 |

### Static Functions (Viper.String)

| Function                                       | Signature                  | Description                              |
|------------------------------------------------|----------------------------|------------------------------------------|
| `Viper.String.FromStr(text)`                   | `String(String)`           | Create a runtime string from text        |
| `Viper.String.FromSingle(value)`               | `String(Double)`           | Convert single-precision value to string |
| `Viper.String.Equals(a, b)`                    | `Boolean(String, String)`  | Compare two strings for equality         |
| `Viper.String.Join(separator, items)`          | `String(String, Seq)`      | Joins sequence of strings with separator |

### Conversion Functions (Viper.Convert)

| Function                                       | Signature                  | Description                              |
|------------------------------------------------|----------------------------|------------------------------------------|
| `Viper.Convert.ToString_Int(value)`            | `String(Integer)`          | Convert integer to string                |
| `Viper.Convert.ToString_Double(value)`         | `String(Double)`           | Convert double to string                 |
| `Viper.Convert.ToInt64(text)`                  | `Integer(String)`          | Parse string to integer                  |
| `Viper.Convert.ToDouble(text)`                 | `Double(String)`           | Parse string to double                   |

**Note:** `Flip()` performs byte-level reversal. It works correctly for ASCII strings but may produce invalid results
for multi-byte UTF-8 characters.

### Zia Example

```zia
module StringDemo;

bind Viper.Terminal;
bind Viper.String as Str;
bind Viper.Fmt as Fmt;

func start() {
    var s = "  Hello, World!  ";

    Say("Length: " + Fmt.Int(Str.Length(s)));               // 17
    Say(Str.Trim(s));                                       // Hello, World!
    Say(Str.ToUpper(s));                                    // HELLO, WORLD!
    Say(Str.ToLower(s));                                    // hello, world!
    Say("IndexOf: " + Fmt.Int(Str.IndexOf(s, "World")));    // 10
    Say("Has World: " + Fmt.Bool(Str.Has(s, "World")));     // true
    Say(Str.Replace(s, "World", "Zia"));                    // Hello, Zia!
}
```

### BASIC Example

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

### Extended Methods Zia Example

```zia
module StringExtDemo;

bind Viper.Terminal;
bind Viper.String as Str;
bind Viper.Fmt as Fmt;

func start() {
    var s = "hello world";

    // Search and match
    Say("Starts: " + Fmt.Bool(Str.StartsWith(s, "hello")));  // true
    Say("Ends: " + Fmt.Bool(Str.EndsWith(s, "world")));      // true
    Say("Has: " + Fmt.Bool(Str.Has(s, "lo wo")));            // true
    Say("Count l: " + Fmt.Int(Str.Count(s, "l")));           // 3

    // Transformation
    Say(Str.Replace(s, "world", "zia"));    // hello zia
    Say(Str.PadLeft("42", 5, "0"));         // 00042
    Say(Str.Repeat("ab", 3));               // ababab
    Say(Str.Flip("hello"));                 // olleh
}
```

### Extended Methods BASIC Example

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
PRINT Viper.String.Join("-", parts)   ' Output: "a-b-c"

' Comparison
PRINT "abc".Cmp("abd")                 ' Output: -1
PRINT "ABC".CmpNoCase("abc")           ' Output: 0
```

---

## See Also

- [Text Processing](text.md) - `StringBuilder` for efficient string building, `Pattern` for regex
- [Utilities](utilities.md) - `Fmt` for string formatting, `Parse` for string parsing
- [Collections](collections.md) - `Seq` for string lists, `Map` for string-keyed data
