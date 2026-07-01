---
status: active
audience: public
last-verified: 2026-05-13
---

# Core Types

> Foundational types that form the basis of all Viper programs.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Core.Box](#vipercorebox)
- [Viper.Core.Diagnostics](#vipercorediagnostics)
- [Viper.Core.MessageBus](#vipercoremessagebus)
- [Viper.Core.Object](#vipercoreobject)
- [Viper.Core.Parse](#vipercoreparse)
- [Viper.String](#viperstring)

---

## Viper.Core.Object

Base class for all Viper reference types. Provides fundamental object operations.

**Type:** Base class (not instantiated directly)

### Instance Methods

| Method          | Signature         | Description                                    |
|-----------------|-------------------|------------------------------------------------|
| `Equals(other)` | `Boolean(Object)` | Compares this object with another for equality |
| `HashCode()`    | `Integer()`       | Returns a hash code for the object             |
| `IsNull()`      | `Boolean()`       | Returns true if this reference is null         |
| `IsNull(obj)`   | `Boolean(Object)` | Static null test that is safe for null inputs  |
| `ToString()`    | `String()`        | Returns a string representation of the object  |
| `TypeId()`      | `Integer()`       | Returns a numeric type identifier for the object's runtime type |
| `TypeName()`    | `String()`        | Returns the runtime type name of the object    |
| `RefEquals(a, b)` | `Boolean(Object, Object)` | Class method equivalent of `Viper.Core.Object.RefEquals(a, b)` |

### Static Functions

| Function                                    | Signature                 | Description                                               |
|---------------------------------------------|---------------------------|-----------------------------------------------------------|
| `Viper.Core.Object.RefEquals(a, b)` | `Boolean(Object, Object)` | Tests if two references point to the same object instance |

### Zia Example

> Object is the base type for all reference types. In Zia, it is implicit—most programs interact
> with concrete types rather than calling Object methods directly.

`TypeId()` returns stable built-in identifiers for strings, boxes, boxed value types, `Viper.Option`, `Viper.Core.MessageBus`, and `MessageBus.Callback` objects in addition to user/runtime class IDs.
Runtime string handles compare and hash by byte content through `Equals` and `HashCode`, so distinct handles with identical bytes behave as equal object keys.
`TypeName()` and `ToString()` report useful built-in names for opaque runtime objects such as boxed value types, options, message buses, and callback wrappers instead of falling back to a generic `Object` label.

### BASIC Example

```basic
DIM obj1 AS Viper.Core.Object
DIM obj2 AS Viper.Core.Object

IF obj1.Equals(obj2) THEN
    PRINT "Objects are equal"
END IF

PRINT obj1.ToString()
PRINT obj1.HashCode()
PRINT obj1.TypeName()   ' Runtime type name
PRINT obj1.TypeId()     ' Numeric type ID
PRINT obj1.IsNull()     ' True if null reference
PRINT Viper.Core.Object.IsNull(obj1)  ' Static null-safe form

' Static function call - check if same instance
IF Viper.Core.Object.RefEquals(obj1, obj2) THEN
    PRINT "Same object instance"
END IF
```

---

## Viper.Core.Box

Boxing helpers for storing primitive values in generic collections. Boxed values are heap objects with type tags.

**Type:** Static utility class

### Methods

| Method              | Signature                 | Description                                            |
|---------------------|---------------------------|--------------------------------------------------------|
| `I64(value)`        | `Object(Integer)`         | Box an integer                                         |
| `F64(value)`        | `Object(Double)`          | Box a double                                           |
| `I1(value)`         | `Object(Boolean)`         | Box a boolean                                          |
| `Str(value)`        | `Object(String)`          | Box a string                                           |
| `ToI64(box)`        | `Integer(Object)`         | Unbox integer (traps on wrong type)                    |
| `ToF64(box)`        | `Double(Object)`          | Unbox double (traps on wrong type)                     |
| `ToI1(box)`         | `Boolean(Object)`         | Unbox boolean (traps on wrong type)                    |
| `ToStr(box)`        | `String(Object)`          | Unbox string as a retained result (traps on wrong type) |
| `ToI64Option(box)`  | `Option<Integer>(Object)` | Return `Some(integer)` or `None` on wrong type                    |
| `ToF64Option(box)`  | `Option<Double>(Object)`  | Return `Some(double)` or `None` on wrong type                     |
| `ToI1Option(box)`   | `Option<Boolean>(Object)` | Return `Some(boolean)` or `None` on wrong type                    |
| `ToStrOption(box)`  | `Option<String>(Object)`  | Return `Some(string)` or `None` on wrong type                     |
| `Type(box)`         | `Integer(Object)`         | Return type tag (0=i64, 1=f64, 2=i1, 3=str)           |
| `EqI64(box,val)`    | `Boolean(Object,Integer)` | Compare boxed value to integer                         |
| `EqF64(box,val)`    | `Boolean(Object,Double)`  | Compare boxed value to double                          |
| `EqStr(box,val)`    | `Boolean(Object,String)`  | Compare boxed value to string                          |
| `ValueType(size)`   | `Object(Integer)`         | Allocate a heap-owned boxed value-type payload          |
| `ValueTypeAddField(obj, offset, kind, retainNow)` | `Void(Object, Integer, Integer, Boolean)` | Register a managed field inside a boxed value type |

### Notes

- Type tags: 0 = integer, 1 = double, 2 = boolean, 3 = string.
- Unboxing with the wrong type traps with a runtime diagnostic.
- `ToStr` returns an owned/retained string; generated code releases it like other string-returning runtime calls.
- String box helpers validate non-null string handles and trap invalid foreign pointers instead of retaining or comparing arbitrary memory.
- The `To*Option` forms do not trap for type mismatch and return managed `Option` values, so Zia and BASIC never need output pointers.
- Boxed values report `Viper.Core.Box` through `Viper.Core.Object.TypeName` and use value equality/hash semantics for `Object.Equals` and collection lookup.
- Floating-point box hashes canonicalize `+0.0`/`-0.0` and all NaN payloads. Boxed NaN values compare equal to other boxed NaNs so collection hashing and equality stay compatible.
- `ValueType(size)` is used by the compiler when boxing structs. Size `0` is valid and creates a managed empty value-type object; negative sizes trap.
- `Viper.Core.ValueType` is the catalog/introspection class for boxed value-type payloads. The compiler copies the inline payload into heap storage, then registers managed object/string fields with `ValueTypeAddField`/`ValueType.AddField` so boxed structs retain referenced values, participate in GC traversal, and release fields when finalized. Registering the same offset with the same field kind is idempotent and does not touch the current slot's reference count; registering the same offset with a different kind traps. When `retainNow` is true, the current slot value is validated before it is retained. If the value-type object already has a finalizer, managed-field cleanup chains it instead of replacing it. User code normally does not call `ValueTypeAddField` directly.

### Zia Example

```rust
module BoxDemo;

bind Viper.Terminal;
bind Viper.Text.Fmt as Fmt;

func start() {
    // Box an integer (type tag 0)
    var boxed = Viper.Core.Box.I64(42);
    Say("Type: " + Fmt.Int(Viper.Core.Box.Type(boxed)));   // 0
    Say("Value: " + Fmt.Int(Viper.Core.Box.ToI64(boxed)));  // 42

    // Box a string (type tag 3)
    var sbox = Viper.Core.Box.Str("hello");
    Say("String: " + Viper.Core.Box.ToStr(sbox));           // hello

    // Box a float (type tag 1)
    var fbox = Viper.Core.Box.F64(3.14);
    Say("Float type: " + Fmt.Int(Viper.Core.Box.Type(fbox)));  // 1
}
```

### BASIC Example

```basic
DIM boxed AS OBJECT
boxed = Viper.Core.Box.I64(42)

PRINT Viper.Core.Box.Type(boxed)         ' Output: 0
PRINT Viper.Core.Box.ToI64(boxed)        ' Output: 42
PRINT Viper.Core.Box.EqI64(boxed, 42)    ' Output: true
```

---

## Viper.Core.Diagnostics

Assertion and trap utilities for program correctness checks. All methods trap (abort with message) on failure.

**Type:** Static utility class

### Methods

| Method                      | Signature                          | Description                                               |
|-----------------------------|------------------------------------|-----------------------------------------------------------|
| `Assert(cond, msg)`         | `Void(Boolean, String)`            | Trap with `msg` if `cond` is false                        |
| `AssertEq(a, b, msg)`       | `Void(Integer, Integer, String)`   | Trap if `a` and `b` are not equal                         |
| `AssertNeq(a, b, msg)`      | `Void(Integer, Integer, String)`   | Trap if `a` and `b` are equal                             |
| `AssertEqNum(a, b, msg)`    | `Void(Double, Double, String)`     | Trap if two numbers are not equal within relative epsilon |
| `AssertEqStr(a, b, msg)`    | `Void(String, String, String)`     | Trap if two strings are not equal                         |
| `AssertNull(obj, msg)`      | `Void(Object, String)`             | Trap if `obj` is not null                                 |
| `AssertNotNull(obj, msg)`   | `Void(Object, String)`             | Trap if `obj` is null                                     |
| `AssertFail(msg)`           | `Void(String)`                     | Unconditional trap with `msg`                             |
| `AssertGt(a, b, msg)`       | `Void(Integer, Integer, String)`   | Trap if `a` is not greater than `b`                       |
| `AssertLt(a, b, msg)`       | `Void(Integer, Integer, String)`   | Trap if `a` is not less than `b`                          |
| `AssertGte(a, b, msg)`      | `Void(Integer, Integer, String)`   | Trap if `a` is not greater than or equal to `b`           |
| `AssertLte(a, b, msg)`      | `Void(Integer, Integer, String)`   | Trap if `a` is not less than or equal to `b`              |
| `Trap(msg)`                 | `Void(String)`                     | Trigger a runtime trap with the given message             |

### Notes

- All assertion failures terminate the program via the runtime trap mechanism (equivalent to a bounds-check failure).
- `Trap` is an unconditional halt; prefer `AssertFail` when the intent is a named assertion failure.
- `Trap(msg)` accepts a managed `String` handle. Embedded NUL bytes in the message are preserved for validation and escaped in the diagnostic path.
- `AssertEqStr` compares full runtime string contents, including embedded NUL bytes, and escapes non-printable bytes in failure messages. Invalid string handles produce a trap diagnostic instead of a native crash.
- These are intended for invariant checking during development and internal consistency validation.

### Zia Example

```rust
module DiagnosticsDemo;

bind Viper.Terminal;
bind Viper.Core.Diagnostics as Diag;
bind Viper.Text.Fmt as Fmt;

func divide(a: Integer, b: Integer) -> Integer {
    Diag.Assert(b != 0, "divide: divisor must be non-zero");
    return a / b;
}

func start() {
    Say(Fmt.Int(divide(10, 2)));   // 5

    var obj = Viper.Core.Box.I64(42);
    Diag.AssertNotNull(obj, "box must not be null");

    Diag.AssertEqStr("hello", "hello", "strings must match");
    Diag.AssertGt(5, 3, "5 must be > 3");
}
```

### BASIC Example

```basic
' Basic assertions
Viper.Core.Diagnostics.Assert(x > 0, "x must be positive")
Viper.Core.Diagnostics.AssertEqStr(name, "Alice", "unexpected name")

' Null checks
DIM obj AS OBJECT = GetSomething()
Viper.Core.Diagnostics.AssertNotNull(obj, "GetSomething returned null")

' Ordering assertions
Viper.Core.Diagnostics.AssertGte(score, 0, "score out of range")
Viper.Core.Diagnostics.AssertLte(score, 100, "score out of range")

' Unconditional trap
IF unrecoverableError THEN
    Viper.Core.Diagnostics.Trap("fatal: unrecoverable error in pipeline")
END IF
```

---

## Viper.Core.Parse

Safe string parsing utilities. Methods return `Option`, validation booleans, or default values rather than trapping on bad input.

**Type:** Static utility class

### Methods

| Method                      | Signature                           | Description                                                        |
|-----------------------------|-------------------------------------|--------------------------------------------------------------------|
| `TryInt(s)`                 | `Option<Integer>(String)`           | Parse integer; returns `None` if invalid                           |
| `TryNum(s)`                 | `Option<Double>(String)`            | Parse double; returns `None` if invalid                            |
| `TryBool(s)`                | `Option<Boolean>(String)`           | Parse boolean; returns `None` if invalid                           |
| `IntOr(s, default)`         | `Integer(String, Integer)`          | Parse `s` as integer; return `default` on failure                  |
| `NumOr(s, default)`         | `Double(String, Double)`            | Parse `s` as double; return `default` on failure                   |
| `BoolOr(s, default)`        | `Boolean(String, Boolean)`          | Parse `s` as boolean; return `default` on failure                  |
| `IsInt(s)`                  | `Boolean(String)`                   | Return true if `s` is a valid integer (no side effects)            |
| `IsNum(s)`                  | `Boolean(String)`                   | Return true if `s` is a valid number (no side effects)             |
| `IntRadix(s, radix, default)` | `Integer(String, Integer, Integer)` | Parse `s` in the given radix (2–36); return `default` on failure |

### Notes

- `TryInt`, `TryNum`, and `TryBool` return managed `Option` values. The lower-level C output-pointer helpers remain runtime-internal.
- Null input is treated as parse failure: `Try*` returns `None`, `Is*` returns false, and `*Or`/`IntRadix` returns the supplied default.
- `IntRadix` supports bases 2 through 36 (e.g., 16 for hex, 2 for binary). Leading `+` and `-` signs are accepted for radix 10 only; non-decimal radices parse unsigned 64-bit bit patterns so formatted hex/binary values can round-trip.
- Leading/trailing ASCII whitespace is accepted; non-whitespace trailing characters and embedded NUL bytes are rejected.
- Numeric parsing accepts explicit `NaN`, `Inf`, `+Inf`, and `-Inf` spellings. Decimal overflow and non-finite decimal results are rejected; finite underflow to zero or a subnormal value is accepted.
### Parse.TryNum and Parse.TryInt Example

```rust
var n = Parse.TryNum("3.14")          // Some(3.14)
var bad = Parse.TryNum("abc")         // None
if bad.get_IsNone() then
    Say("Not a number")
end if

var i = Parse.TryInt("42")            // Some(42)
var badInt = Parse.TryInt("xyz")      // None
```

### Zia Example

```rust
module ParseDemo;

bind Viper.Terminal;
bind Viper.Core.Parse as Parse;
bind Viper.Text.Fmt as Fmt;

func start() {
    // Safe integer parsing with default
    var n = Parse.IntOr("42", 0);
    Say("Parsed: " + Fmt.Int(n));         // 42

    var bad = Parse.IntOr("abc", -1);
    Say("Bad input: " + Fmt.Int(bad));    // -1

    // Validation without parsing
    Say(Fmt.Bool(Parse.IsInt("123")));    // true
    Say(Fmt.Bool(Parse.IsInt("12.5")));   // false

    // Hex parsing
    var hex = Parse.IntRadix("FF", 16, 0);
    Say("0xFF = " + Fmt.Int(hex));        // 255

    // Float parsing
    var f = Parse.NumOr("3.14", 0.0);
    Say("Float: " + Fmt.Num(f));          // 3.14
}
```

### BASIC Example

```basic
' Safe parsing with defaults
DIM n AS INTEGER = Viper.Core.Parse.IntOr(userInput, 0)
DIM f AS DOUBLE  = Viper.Core.Parse.NumOr(userInput, 0.0)
DIM b AS INTEGER = Viper.Core.Parse.BoolOr(userInput, 0)

' Validation before use
IF Viper.Core.Parse.IsInt(inputStr) THEN
    DIM value AS INTEGER = Viper.Core.Parse.IntOr(inputStr, 0)
    PRINT "Value: "; value
ELSE
    PRINT "Not a valid integer"
END IF

' Radix parsing (hex, binary, etc.)
DIM hexVal AS INTEGER = Viper.Core.Parse.IntRadix("1A3F", 16, 0)
PRINT "Hex 1A3F = "; hexVal    ' Output: 6719

DIM binVal AS INTEGER = Viper.Core.Parse.IntRadix("1010", 2, 0)
PRINT "Bin 1010 = "; binVal    ' Output: 10
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
| `IndexOf(search)`            | `Integer(String)`          | Returns the 1-based position of `search`, or 0 if not found                   |
| `IndexOfFrom(start, search)` | `Integer(Integer, String)` | Searches from 1-based position `start`; returns 0 if not found                |
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
| `Lines()`                      | `Seq(String)`             | Splits into logical lines on `\n`, dropping a trailing `\r` (CRLF→LF); segment count matches `Split("\n")` |

**Case Conversion:**

| Method              | Signature    | Description                                                    |
|---------------------|-------------|----------------------------------------------------------------|
| `Capitalize()`      | `String()`  | Capitalize first character, lowercase the rest                 |
| `Title()`           | `String()`  | Capitalize the first character of each word                    |
| `CamelCase()`       | `String()`  | Convert to camelCase                                           |
| `PascalCase()`      | `String()`  | Convert to PascalCase                                          |
| `SnakeCase()`       | `String()`  | Convert to snake_case                                          |
| `KebabCase()`       | `String()`  | Convert to kebab-case                                          |
| `ScreamingSnake()`  | `String()`  | Convert to SCREAMING_SNAKE_CASE                                |

**Additional Search:**

| Method                | Signature          | Description                                               |
|-----------------------|--------------------|-----------------------------------------------------------|
| `LastIndexOf(search)` | `Integer(String)`  | Returns the last position of `search`, or -1 if not found |
| `RemovePrefix(prefix)`| `String(String)`   | Removes prefix if present, otherwise returns original     |
| `RemoveSuffix(suffix)`| `String(String)`   | Removes suffix if present, otherwise returns original     |
| `TrimChar(chars)`     | `String(String)`   | Removes specified characters from both ends               |
| `Slug()`              | `String()`         | Convert to URL-friendly slug form                         |

**String Distance:**

| Method                | Signature          | Description                                              |
|-----------------------|--------------------|----------------------------------------------------------|
| `Levenshtein(other)`  | `Integer(String)`  | Compute Levenshtein edit distance between two strings    |
| `Jaro(other)`         | `Double(String)`   | Compute Jaro similarity score (0.0 to 1.0)              |
| `JaroWinkler(other)`  | `Double(String)`   | Compute Jaro-Winkler similarity score (0.0 to 1.0)     |
| `Hamming(other)`      | `Integer(String)`  | Compute Hamming distance (strings must be equal length)  |

**Pattern Matching:**

| Method           | Signature         | Description                                              |
|------------------|-------------------|----------------------------------------------------------|
| `Like(pattern)`  | `Boolean(String)` | Wildcard match against pattern (`*` = any, `?` = one char) |
| `LikeCI(pattern)` | `Boolean(String)` | Case-insensitive wildcard match                         |

**Comparison:**

| Method             | Signature         | Description                                      |
|--------------------|-------------------|--------------------------------------------------|
| `Cmp(other)`       | `Integer(String)` | Compares strings, returns -1, 0, or 1            |
| `CmpNoCase(other)` | `Integer(String)` | Case-insensitive comparison, returns -1, 0, or 1 |

### Static Functions (Viper.String)

| Function                                       | Signature                  | Description                                                      |
|------------------------------------------------|----------------------------|------------------------------------------------------------------|
| `Viper.String.Equals(a, b)`                    | `Boolean(String, String)`  | Compare two strings for equality                                 |
| `Viper.String.FromI16(value)`                  | `String(Integer)`          | Convert a 16-bit integer to string                               |
| `Viper.String.FromI32(value)`                  | `String(Integer)`          | Convert a 32-bit integer to string                               |
| `Viper.String.FromSingle(value)`               | `String(Double)`           | Convert a double formatted as single-precision (f32) to string   |
| `Viper.String.FromStr(text)`                   | `String(String)`           | Create a runtime string from text                                |
| `Viper.String.Join(separator, items)`          | `String(String, Seq)`      | Joins sequence of strings with separator                         |
| `Viper.String.SplitFields(text)`                | `Seq(String)`                   | Split by whitespace/CSV-style fields into a managed sequence |

### Conversion Functions (Viper.Core.Convert)

| Function                                       | Signature                  | Description                              |
|------------------------------------------------|----------------------------|------------------------------------------|
| `Viper.Core.Convert.ToString_Int(value)`            | `String(Integer)`          | Convert integer to string                |
| `Viper.Core.Convert.ToString_Double(value)`         | `String(Double)`           | Convert double to round-trip string      |
| `Viper.Core.Convert.ToInt64(text)`                  | `Integer(String)`          | Parse string to integer (traps on failure) |
| `Viper.Core.Convert.ToDouble(text)`                 | `Double(String)`           | Parse string to double, including `NaN` / `Inf` / `-Inf` (traps on failure) |
| `Viper.Core.Convert.NumToInt(value)`                | `Integer(Number)`          | Convert floating-point Number to Integer (truncates/clamps) |

**Note:** `Convert.NumToInt(3.7)` returns `3`, `NaN` returns `0`, and out-of-range values clamp to the nearest signed 64-bit endpoint. This is distinct from `Convert.ToInt64(str)` which parses from a string.

**Note:** `Flip()` performs byte-level reversal. It works correctly for ASCII strings but may produce invalid results
for multi-byte UTF-8 characters.

### Zia Example

```rust
module StringDemo;

bind Viper.Terminal;
bind Viper.String as Str;
bind Viper.Text.Fmt as Fmt;

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

```rust
module StringExtDemo;

bind Viper.Terminal;
bind Viper.String as Str;
bind Viper.Text.Fmt as Fmt;

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
PRINT parts.Length                        ' Output: 3
PRINT Viper.String.Join("-", parts)   ' Output: "a-b-c"

' Comparison
PRINT "abc".Cmp("abd")                 ' Output: -1
PRINT "ABC".CmpNoCase("abc")           ' Output: 0
```

---

## Viper.Text.Char

Static ASCII character-classification helpers for identifier scanning (completion triggers,
word selection, tokenization). Each takes a string and classifies its **first character**, so it
drops directly into a char-by-char scan; an empty string or a non-ASCII leading character returns
`false`.

| Method                     | Signature       | Description                                              |
|----------------------------|-----------------|---------------------------------------------------------|
| `IsIdentifierStart(ch)`    | `Boolean(String)` | First character can start an identifier (ASCII letter or `_`) |
| `IsIdentifierPart(ch)`     | `Boolean(String)` | First character can continue an identifier (ASCII letter, digit, or `_`) |
| `IsAlnum(ch)`              | `Boolean(String)` | First character is ASCII alphanumeric (letter or digit) |

```rust
// Zia — scan an identifier instead of hand-writing the character ranges.
bind Char = Viper.Text.Char;
if Char.IsIdentifierStart(Str.MidLen(text, 1, 1)) {
    // ... consume Char.IsIdentifierPart(...) characters
}
```

---

## Viper.Core.MessageBus

In-process publish/subscribe message bus for decoupled communication between components. Subscribers register interest in named topics and receive published data via callbacks.

**Type:** Instance class (requires `New()`)

### Constructor

| Method  | Signature      | Description                    |
|---------|----------------|--------------------------------|
| `New()` | `MessageBus()` | Create a new empty message bus |

### Properties

| Property             | Type                  | Description                                      |
|----------------------|-----------------------|--------------------------------------------------|
| `TotalSubscriptions` | `Integer` (read-only) | Total number of active subscriptions across all topics |

### Methods

| Method                     | Signature                     | Description                                                     |
|----------------------------|-------------------------------|-----------------------------------------------------------------|
| `Callback(fn)`             | `Object(Function)`            | Wrap a frontend function reference as a managed handler |
| `Subscribe(topic, handler)` | `Integer(String, Object)`    | Subscribe a handler to a topic; returns subscription ID         |
| `Unsubscribe(id)`          | `Boolean(Integer)`           | Remove a subscription by ID; returns true if found              |
| `Publish(topic, data)`     | `Integer(String, Object)`    | Publish data to all subscribers of a topic; returns count notified |
| `SubscriberCount(topic)`   | `Integer(String)`            | Returns the number of subscribers for a topic                   |
| `Topics()`                 | `Seq()`                      | Returns a Seq of all topic names with active subscribers        |
| `ClearTopic(topic)`        | `Void(String)`               | Remove all subscribers for a specific topic                     |
| `Clear()`                  | `Void()`                     | Remove all subscriptions from all topics                        |

### Notes

- `Subscribe` returns a unique integer ID that can be used with `Unsubscribe`
- `Publish` invokes all handlers for the given topic synchronously; returns the number of handlers called
- Handler functions receive the published data as their argument
- Publish uses a stable subscriber snapshot; unsubscribes during a handler affect later publishes, not the in-flight one
- Publish retains managed string handles and object/array payloads for the duration of dispatch so one handler cannot free the payload before later handlers run. Raw string byte pointers are borrowed.
- Subscribe accepts a managed callback returned by `Callback(&handler)` in Zia or `Callback(ADDRESSOF Handler)` in BASIC. Native callback pointers stay inside the runtime bridge.
- Topic matching is byte-length aware; topic names containing embedded NUL bytes remain distinct.
- `Topics()` returns an owning `Seq` of copied topic strings; the result remains valid after the bus is cleared or destroyed.
- `Unsubscribe`, `ClearTopic`, and `Clear` remove empty topic records, so a later `Topics()` call reports only active topics.
- If a handler traps during `Publish`, the in-flight snapshot is released before the trap is re-raised.
- MessageBus instances are typed runtime objects, participate in GC traversal for retained handlers, serialize public operations with an internal lock, and retain the bus for the duration of each public operation so callbacks can release their last external bus reference safely.

### Zia Example

```rust
module MessageBusDemo;

bind Viper.Terminal;
bind Viper.Core.MessageBus as MessageBus;
bind Viper.Text.Fmt as Fmt;

func start() {
    var bus = MessageBus.New();
    Say("Total subscriptions: " + Fmt.Int(bus.get_TotalSubscriptions())); // 0
    Say("Subscriber count for 'test': " + Fmt.Int(bus.SubscriberCount("test"))); // 0
}
```

### BASIC Example

```basic
' Create a new message bus
DIM bus AS OBJECT = Viper.Core.MessageBus.New()

' Check initial state
PRINT "Total subscriptions: "; bus.TotalSubscriptions  ' Output: 0
PRINT "Subscribers for 'test': "; bus.SubscriberCount("test")  ' Output: 0

' Subscribe and publish use managed callbacks created with MessageBus.Callback(ADDRESSOF Handler).
```

---

## See Also

- [Text Processing](text/README.md) - `StringBuilder` for efficient string building, `Pattern` for regex
- [Utilities](utilities.md) - `Fmt` for string formatting, `Parse` for string parsing
- [Collections](collections/README.md) - `Seq` for string lists, `Map` for string-keyed data
