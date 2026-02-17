# Text Processing

> String building, encoding/decoding, pattern matching, and text utilities.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Text.Codec](#vipertextcodec)
- [Viper.Text.Csv](#vipertextcsv)
- [Viper.Text.Uuid](#vipertextuuid)
- [Viper.Text.Html](#vipertexthtml)
- [Viper.Text.Json](#vipertextjson)
- [Viper.Text.JsonPath](#vipertextjsonpath)
- [Viper.Text.Markdown](#vipertextmarkdown)
- [Viper.Text.Pattern](#vipertextpattern)
- [Viper.Text.CompiledPattern](#vipertextcompiledpattern)
- [Viper.Text.Template](#vipertexttemplate)
- [Viper.Text.Toml](#vipertexttoml)
- [Viper.Text.StringBuilder](#vipertextstringbuilder)
- [Viper.Text.JsonStream](#vipertextjsonstream)
- [Viper.Text.Diff](#vipertextdiff)
- [Viper.Text.Ini](#vipertextini)
- [Viper.Text.NumberFormat](#vipertextnumberformat)
- [Viper.Text.Pluralize](#vipertextpluralize)
- [Viper.Text.Scanner](#vipertextscanner)
- [Viper.Text.TextWrapper](#vipertexttextwrapper)
- [Viper.Text.Version](#vipertextversion)
- [Viper.Data.Serialize](#viperdataserialize)
- [String.Like / String.LikeCI](#stringlike--stringlikeci)

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

### Zia Example

```zia
module CodecDemo;

bind Viper.Terminal;
bind Viper.Text.Codec as Codec;

func start() {
    Say("Base64: " + Codec.Base64Enc("Hello"));        // SGVsbG8=
    Say("Decoded: " + Codec.Base64Dec("SGVsbG8="));     // Hello
    Say("Hex: " + Codec.HexEnc("Hello"));               // 48656c6c6f
    Say("HexDec: " + Codec.HexDec("48656c6c6f"));       // Hello
    Say("UrlEnc: " + Codec.UrlEncode("hello world"));   // hello%20world
    Say("UrlDec: " + Codec.UrlDecode("hello%20world")); // hello world
}
```

### BASIC Example

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

### Zia Example

```zia
module CsvDemo;

bind Viper.Terminal;
bind Viper.Text.Csv as Csv;
bind Viper.Fmt as Fmt;

func start() {
    // Parse a CSV line into fields
    var fields = Csv.ParseLine("name,age,city");
    Say("Field count: " + Fmt.Int(fields.Len));

    // Format a CSV line with quoting
    var data = new Viper.Collections.Seq();
    data.Push(Viper.Box.Str("Hello, World"));
    data.Push(Viper.Box.Str("Simple"));
    var line = Csv.FormatLine(data);
    Say("Formatted: " + line);
}
```

### BASIC Example

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

## Viper.Text.Uuid

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
| `IsValid(guid)`    | `Boolean(String)` | Check if string is a valid UUID format |
| `ToBytes(guid)`    | `Bytes(String)`   | Convert UUID string to 16-byte array   |
| `FromBytes(bytes)` | `String(Bytes)`   | Convert 16-byte array to UUID string   |

### Notes

- Generated UUIDs follow UUID version 4 format (random)
- Format: `xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx` where:
    - `4` indicates version 4 (random UUID)
    - `y` is one of `8`, `9`, `a`, or `b` (variant indicator)
- All hex characters are lowercase
- Uses cryptographically secure random source where available (/dev/urandom on Unix, CryptGenRandom on Windows)
- `ToBytes()` traps if the UUID format is invalid
- `FromBytes()` traps if the Bytes object is not exactly 16 bytes

### Zia Example

```zia
module UuidDemo;

bind Viper.Terminal;
bind Viper.Text.Uuid as Uuid;
bind Viper.Fmt as Fmt;

func start() {
    var id = Uuid.New();
    Say("UUID: " + id);                                    // e.g. a1b2c3d4-...
    Say("Valid: " + Fmt.Bool(Uuid.IsValid(id)));            // true
    Say("Invalid: " + Fmt.Bool(Uuid.IsValid("not-uuid"))); // false
}
```

### BASIC Example

```basic
' Generate a new UUID
DIM id AS STRING = Viper.Text.Uuid.New()
PRINT id  ' Example: "550e8400-e29b-41d4-a716-446655440000"

' Check if a string is a valid UUID
DIM valid AS INTEGER = Viper.Text.Uuid.IsValid(id)
PRINT valid  ' Output: 1 (true)

DIM invalid AS INTEGER = Viper.Text.Uuid.IsValid("not-a-uuid")
PRINT invalid  ' Output: 0 (false)

' Get the empty/nil UUID
DIM empty AS STRING = Viper.Text.Uuid.Empty
PRINT empty  ' Output: "00000000-0000-0000-0000-000000000000"

' Convert to/from bytes for storage or transmission
DIM bytes AS OBJECT = Viper.Text.Uuid.ToBytes(id)
DIM restored AS STRING = Viper.Text.Uuid.FromBytes(bytes)
PRINT restored = id  ' Output: 1 (true)
```

---

## Viper.Text.Json

JSON parsing and formatting per ECMA-404/RFC 8259.

**Type:** Static utility class

### Methods

| Method              | Signature           | Description                                      |
|---------------------|---------------------|--------------------------------------------------|
| `Parse(text)`       | `Object(String)`    | Parse JSON string into a value tree              |
| `ParseObject(text)` | `Object(String)`    | Parse JSON, expecting an object (Map)            |
| `ParseArray(text)`  | `Object(String)`    | Parse JSON, expecting an array (Seq)             |
| `Format(value)`     | `String(Object)`    | Format value as compact JSON string              |
| `FormatPretty(v,n)` | `String(Object,Int)`| Format with indentation (n spaces)               |
| `IsValid(text)`     | `Boolean(String)`   | Check if string is valid JSON                    |
| `TypeOf(value)`     | `String(Object)`    | Get type: "null", "bool", "int", "float", "str", "array", "object" |

### Value Access

JSON values are returned as native Viper types:

| JSON Type   | Viper Type         | Access Method                      |
|-------------|--------------------|------------------------------------|
| null        | null               | Check with `value = NULL`          |
| boolean     | Boolean (boxed)    | `Viper.Unbox.I1(value)`            |
| number (int)| Integer (boxed)    | `Viper.Unbox.I64(value)`           |
| number (dec)| Double (boxed)     | `Viper.Unbox.F64(value)`           |
| string      | String (boxed)     | `Viper.Unbox.Str(value)`           |
| array       | Seq                | `value.Get(index)`, `value.Len`    |
| object      | Map                | `value.Get(key)`, `value.Keys()`   |

### Zia Example

```zia
module JsonDemo;

bind Viper.Terminal;
bind Viper.Text.Json as Json;
bind Viper.Fmt as Fmt;

func start() {
    var obj = Json.Parse("{\"name\":\"Viper\",\"version\":1}");
    Say("Type: " + Json.TypeOf(obj));                       // object
    Say("Valid: " + Fmt.Bool(Json.IsValid("{\"ok\":true}"))); // true
    Say(Json.Format(obj));                                    // {"name":"Viper","version":1}
}
```

### BASIC Example

```basic
' Parse JSON
DIM json AS STRING = "{""name"": ""Alice"", ""age"": 30, ""active"": true}"
DIM data AS OBJECT = Viper.Text.Json.Parse(json)

' Access values (data is a Map)
DIM name AS STRING = Viper.Unbox.Str(data.Get("name"))
DIM age AS INTEGER = Viper.Unbox.I64(data.Get("age"))
PRINT "Name: "; name   ' Output: Alice
PRINT "Age: "; age     ' Output: 30

' Check type
DIM valueType AS STRING = Viper.Text.Json.TypeOf(data.Get("active"))
PRINT valueType        ' Output: "bool"

' Format with pretty printing
DIM config AS OBJECT = Viper.Collections.Map.New()
config.Set("debug", Viper.Box.I1(1))
config.Set("port", Viper.Box.I64(8080))

DIM output AS STRING = Viper.Text.Json.FormatPretty(config, 2)
PRINT output
' Output:
' {
'   "debug": true,
'   "port": 8080
' }

' Validate JSON
IF Viper.Text.Json.IsValid(userInput) THEN
    DIM parsed AS OBJECT = Viper.Text.Json.Parse(userInput)
END IF
```

### Use Cases

- **API responses:** Parse JSON from web services
- **Configuration:** Read/write JSON config files
- **Data interchange:** Standard format for data exchange
- **Storage:** Serialize application state

---

## Viper.Text.Pattern

Regular expression pattern matching for text search and manipulation.

**Type:** Static utility class

### Methods

| Method                                | Signature                   | Description                                        |
|---------------------------------------|-----------------------------|----------------------------------------------------|
| `IsMatch(pattern, text)`              | `Boolean(String, String)`   | Test if pattern matches anywhere in text           |
| `Find(pattern, text)`                 | `String(String, String)`    | Find first match, or empty string if none          |
| `FindFrom(pattern, text, start)`      | `String(String, String, Integer)` | Find first match at or after start position  |
| `FindPos(pattern, text)`              | `Integer(String, String)`   | Find position of first match, or -1 if none        |
| `FindAll(pattern, text)`              | `Seq(String, String)`       | Find all non-overlapping matches                   |
| `Replace(pattern, text, replacement)` | `String(String, String, String)` | Replace all matches with replacement         |
| `ReplaceFirst(pattern, text, replacement)` | `String(String, String, String)` | Replace first match only                |
| `Split(pattern, text)`                | `Seq(String, String)`       | Split text by pattern matches                      |
| `Escape(text)`                        | `String(String)`            | Escape special regex characters for literal matching |

### Supported Regex Syntax

| Feature              | Syntax               | Description                                      |
|----------------------|----------------------|--------------------------------------------------|
| Literals             | `abc`                | Match literal characters                         |
| Dot                  | `.`                  | Match any character except newline               |
| Anchors              | `^` `$`              | Start/end of string                              |
| Character class      | `[abc]` `[a-z]`      | Match any character in set                       |
| Negated class        | `[^abc]` `[^0-9]`    | Match any character NOT in set                   |
| Shorthand: digit     | `\d` `\D`            | Digit `[0-9]` / non-digit                        |
| Shorthand: word      | `\w` `\W`            | Word char `[a-zA-Z0-9_]` / non-word              |
| Shorthand: space     | `\s` `\S`            | Whitespace / non-whitespace                      |
| Quantifier: star     | `*` `*?`             | Zero or more (greedy / non-greedy)               |
| Quantifier: plus     | `+` `+?`             | One or more (greedy / non-greedy)                |
| Quantifier: optional | `?` `??`             | Zero or one (greedy / non-greedy)                |
| Grouping             | `(abc)`              | Group subexpressions                             |
| Alternation          | `a\|b`               | Match either alternative                         |
| Escape               | `\\` `\.` `\*` etc.  | Match literal special character                  |

### NOT Supported

The following advanced regex features are not implemented:

- Backreferences (`\1`, `\2`, etc.)
- Lookahead/lookbehind (`(?=...)`, `(?<=...)`, etc.)
- Named groups (`(?P<name>...)`)
- Unicode categories (`\p{L}`, etc.)
- Possessive quantifiers (`*+`, `++`, etc.)
- Bounded quantifiers (`{n}`, `{n,m}`)

### Traps

- Invalid pattern syntax traps with a descriptive error message

### Zia Example

```zia
module PatternDemo;

bind Viper.Terminal;
bind Viper.Fmt as Fmt;

func start() {
    // Note: Pattern functions take (pattern, text) order
    Say("Match: " + Fmt.Bool(Viper.Text.Pattern.IsMatch("[a-z]+[0-9]+", "hello123")));
    Say("Find: " + Viper.Text.Pattern.Find("[0-9]+", "Price is $42.50"));
    Say("Replace: " + Viper.Text.Pattern.Replace("[0-9]+", "foo 123 bar 456", "#"));
}
```

### BASIC Example

```basic
' Match test - BASIC arg order is (text, pattern)
PRINT "Match: "; Viper.Text.Pattern.IsMatch("hello123", "[a-z]+[0-9]+")

' Find first match
PRINT "Find: "; Viper.Text.Pattern.Find("Price is $42.50", "[0-9]+")

' Replace all matches
PRINT "Replace: "; Viper.Text.Pattern.Replace("foo 123 bar 456", "[0-9]+", "#")
```

### Pattern Matching BASIC Example

```basic
' Basic matching
DIM text AS STRING = "Hello, World!"

' Check if pattern matches
IF Viper.Text.Pattern.IsMatch("\w+", text) THEN
    PRINT "Contains word characters"
END IF

' Find first match
DIM word AS STRING = Viper.Text.Pattern.Find("[A-Z][a-z]+", text)
PRINT word  ' Output: "Hello"

' Find position
DIM pos AS INTEGER = Viper.Text.Pattern.FindPos("World", text)
PRINT pos  ' Output: 7

' Find all matches
DIM words AS OBJECT = Viper.Text.Pattern.FindAll("\w+", text)
PRINT words.Count  ' Output: 2 (Hello, World)
```

### Replace Example

```basic
' Replace all digits with X
DIM result AS STRING = Viper.Text.Pattern.Replace("\d+", "abc123def456", "X")
PRINT result  ' Output: "abcXdefX"

' Replace first match only
result = Viper.Text.Pattern.ReplaceFirst("\d+", "abc123def456", "X")
PRINT result  ' Output: "abcXdef456"

' Remove all whitespace
result = Viper.Text.Pattern.Replace("\s+", "hello   world  test", "")
PRINT result  ' Output: "helloworldtest"
```

### Split Example

```basic
' Split by whitespace
DIM parts AS OBJECT = Viper.Text.Pattern.Split("\s+", "hello   world  test")
PRINT parts.Count  ' Output: 3
PRINT parts.Get(0) ' Output: "hello"
PRINT parts.Get(1) ' Output: "world"
PRINT parts.Get(2) ' Output: "test"

' Split by comma
parts = Viper.Text.Pattern.Split(",", "a,b,c,d")
PRINT parts.Count  ' Output: 4
```

### Escape Example

```basic
' Escape special characters for literal matching
DIM pattern AS STRING = Viper.Text.Pattern.Escape("file.txt")
PRINT pattern  ' Output: "file\.txt"

' Use escaped pattern to match literal dot
DIM matches AS INTEGER = Viper.Text.Pattern.IsMatch(pattern, "file.txt")
PRINT matches  ' Output: 1 (true)

' Without escaping, dot matches any character
matches = Viper.Text.Pattern.IsMatch("file.txt", "fileXtxt")
PRINT matches  ' Output: 1 (true - dot matched X)
```

### Email Validation Example

```basic
' Simple email pattern (not comprehensive)
DIM email_pattern AS STRING = "^\\w+@\\w+\\.\\w+$"

FUNCTION IsValidEmail(email AS STRING) AS BOOLEAN
    RETURN Viper.Text.Pattern.IsMatch(email_pattern, email)
END FUNCTION

PRINT IsValidEmail("user@example.com")  ' Output: 1 (true)
PRINT IsValidEmail("invalid-email")     ' Output: 0 (false)
```

### Performance Notes

- Compiled patterns are cached internally (LRU cache, 16 entries)
- Frequently used patterns avoid recompilation overhead
- For maximum performance with many operations, use consistent pattern strings
- For repeated operations with the same pattern, consider `CompiledPattern`

---

## Viper.Text.CompiledPattern

Pre-compiled regular expression for efficient repeated matching. Use this when applying the same pattern to multiple
strings to avoid recompilation overhead.

**Type:** Instance class
**Constructor:** `NEW Viper.Text.CompiledPattern(pattern)`

### Properties

| Property  | Type   | Description                                   |
|-----------|--------|-----------------------------------------------|
| `Pattern` | String | The original pattern string used to compile   |

### Methods

| Method                             | Signature                    | Description                                        |
|------------------------------------|------------------------------|----------------------------------------------------|
| `IsMatch(text)`                    | `Boolean(String)`            | Test if pattern matches anywhere in text           |
| `Find(text)`                       | `String(String)`             | Find first match, or empty string if none          |
| `FindFrom(text, start)`            | `String(String, Integer)`    | Find first match at or after start position        |
| `FindPos(text)`                    | `Integer(String)`            | Find position of first match, or -1 if none        |
| `FindAll(text)`                    | `Seq(String)`                | Find all non-overlapping matches                   |
| `Captures(text)`                   | `Seq(String)`                | Get capture groups from first match                |
| `CapturesFrom(text, start)`        | `Seq(String, Integer)`       | Get capture groups starting from position          |
| `Replace(text, replacement)`       | `String(String, String)`     | Replace all matches with replacement               |
| `ReplaceFirst(text, replacement)`  | `String(String, String)`     | Replace first match only                           |
| `Split(text)`                      | `Seq(String)`                | Split text by pattern matches                      |
| `SplitN(text, limit)`              | `Seq(String, Integer)`       | Split text with maximum number of parts            |

### Capture Groups

The `Captures` and `CapturesFrom` methods return a Seq containing:
- Index 0: The full match
- Index 1+: Captured groups in order of opening parentheses

If there is no match, an empty Seq is returned.

### Zia Example

> CompiledPattern is not yet available as a constructible type in Zia. Use the static `Viper.Text.Pattern` functions instead.

### BASIC Example

```basic
' Compile a pattern once for multiple uses
DIM numberPattern AS OBJECT = NEW Viper.Text.CompiledPattern("\d+")

' Process multiple strings efficiently
DIM texts AS OBJECT = NEW Viper.Collections.Seq()
texts.Push("abc123def")
texts.Push("foo456bar")
texts.Push("no digits here")

FOR i = 0 TO texts.Len - 1
    DIM text AS STRING = texts.Get(i)
    IF numberPattern.IsMatch(text) THEN
        PRINT "Found number: "; numberPattern.Find(text)
    ELSE
        PRINT "No number in: "; text
    END IF
NEXT
' Output:
' Found number: 123
' Found number: 456
' No number in: no digits here
```

### Capture Groups Example

```basic
' Pattern with capture groups
DIM datePattern AS OBJECT = NEW Viper.Text.CompiledPattern("(\d{4})-(\d{2})-(\d{2})")

DIM groups AS OBJECT = datePattern.Captures("Today is 2024-01-15")
IF groups.Len > 0 THEN
    PRINT "Full match: "; groups.Get(0)   ' Output: 2024-01-15
    PRINT "Year: "; groups.Get(1)         ' Output: 2024
    PRINT "Month: "; groups.Get(2)        ' Output: 01
    PRINT "Day: "; groups.Get(3)          ' Output: 15
END IF

' Email extraction
DIM emailPattern AS OBJECT = NEW Viper.Text.CompiledPattern("(\w+)@(\w+)\.(\w+)")
groups = emailPattern.Captures("Contact: user@example.com")
IF groups.Len > 0 THEN
    PRINT "User: "; groups.Get(1)         ' Output: user
    PRINT "Domain: "; groups.Get(2)       ' Output: example
    PRINT "TLD: "; groups.Get(3)          ' Output: com
END IF
```

### Split with Limit Example

```basic
DIM commaPattern AS OBJECT = NEW Viper.Text.CompiledPattern(",")

' Split all
DIM all AS OBJECT = commaPattern.Split("a,b,c,d,e")
PRINT all.Len  ' Output: 5

' Split with limit (max 3 parts)
DIM limited AS OBJECT = commaPattern.SplitN("a,b,c,d,e", 3)
PRINT limited.Len        ' Output: 3
PRINT limited.Get(0)     ' Output: a
PRINT limited.Get(1)     ' Output: b
PRINT limited.Get(2)     ' Output: c,d,e (rest in last element)
```

### When to Use CompiledPattern vs Pattern

| Scenario                            | Recommendation             |
|-------------------------------------|----------------------------|
| One-time pattern match              | Use `Pattern` (simpler)    |
| Same pattern on multiple strings    | Use `CompiledPattern`      |
| Pattern in a loop                   | Use `CompiledPattern`      |
| Need capture groups                 | Use `CompiledPattern`      |
| Dynamic pattern from user input     | Use `Pattern` (compiled once) |

### Performance Notes

- Compiling a pattern takes time proportional to pattern complexity
- Once compiled, matching is fast regardless of pattern complexity
- For patterns used more than 2-3 times, `CompiledPattern` is more efficient
- The internal `Pattern` class caches 16 patterns, but explicit `CompiledPattern` avoids cache thrashing

---

## Viper.Text.Template

Simple string templating with placeholder substitution.

**Type:** Static utility class

### Methods

| Method                                    | Signature                           | Description                                       |
|-------------------------------------------|-------------------------------------|---------------------------------------------------|
| `Render(template, values)`                | `String(String, Map)`               | Replace `{{key}}` placeholders with Map values    |
| `RenderSeq(template, values)`             | `String(String, Seq)`               | Replace `{{0}}`, `{{1}}` with Seq values          |
| `RenderWith(template, values, pre, suf)`  | `String(String, Map, String, String)` | Use custom delimiters instead of `{{` `}}`      |
| `Has(template, key)`                      | `Boolean(String, String)`           | Check if template contains placeholder for key   |
| `Keys(template)`                          | `Bag(String)`                       | Extract all unique placeholder keys               |
| `Escape(text)`                            | `String(String)`                    | Escape `{{` and `}}` for literal output           |

### Placeholder Syntax

- Default delimiters: `{{` and `}}`
- Whitespace inside placeholders is trimmed: `{{ name }}` = `{{name}}`
- Missing keys are left as-is: `{{unknown}}` outputs `{{unknown}}`
- Empty keys are left as-is: `{{}}` outputs `{{}}`
- Unclosed placeholders are left as-is: `{{name` outputs `{{name`

### Notes

- Map values must be boxed strings (created with boxing)
- Seq values must be boxed strings (created with boxing)
- Non-string boxed values in the Map/Seq are ignored (placeholder left as-is)
- Thread safe: all functions are stateless

### Traps

- `Render`: Traps if template or values is null
- `RenderSeq`: Traps if template or values is null
- `RenderWith`: Traps if template, values, prefix, or suffix is null; traps if prefix or suffix is empty

### Zia Example

```zia
module TemplateDemo;

bind Viper.Terminal;
bind Viper.Text.Json as Json;

func start() {
    var data = Json.Parse("{\"name\":\"Zia\",\"version\":\"1.0\"}");
    var result = Viper.Text.Template.Render("Hello {{name}} v{{version}}!", data);
    Say(result);  // Hello Zia v1.0!
}
```

### BASIC Example

```basic
' Use Json.Parse to create a map for template variables
DIM data AS OBJECT = Viper.Text.Json.Parse("{""name"":""Zia"",""version"":""1.0""}")
DIM result AS STRING = Viper.Text.Template.Render("Hello {{name}} v{{version}}!", data)
PRINT result  ' Output: Hello Zia v1.0!
```

### Basic BASIC Example

```basic
' Create a Map with values
DIM values AS OBJECT = Map.New()
values.Set("name", Viper.Box.Str("Alice"))
values.Set("count", Viper.Box.Str("5"))

' Render template
DIM result AS STRING = Viper.Text.Template.Render("Hello {{name}}, you have {{count}} messages.", values)
PRINT result  ' Output: "Hello Alice, you have 5 messages."

' Whitespace in placeholders is ignored
result = Viper.Text.Template.Render("Hello {{ name }}!", values)
PRINT result  ' Output: "Hello Alice!"

' Missing keys are left as-is
result = Viper.Text.Template.Render("Hello {{unknown}}!", values)
PRINT result  ' Output: "Hello {{unknown}}!"
```

### Positional Substitution Example

```basic
' Create a Seq with positional values
DIM values AS OBJECT = Seq.New()
values.Push(Viper.Box.Str("Alice"))
values.Push(Viper.Box.Str("Bob"))
values.Push(Viper.Box.Str("Charlie"))

' Use numeric indices
DIM result AS STRING = Viper.Text.Template.RenderSeq("{{0}} and {{1}} meet {{2}}", values)
PRINT result  ' Output: "Alice and Bob meet Charlie"

' Out of range indices are left as-is
result = Viper.Text.Template.RenderSeq("{{0}} and {{99}}", values)
PRINT result  ' Output: "Alice and {{99}}"
```

### Custom Delimiters Example

```basic
DIM values AS OBJECT = Map.New()
values.Set("name", Viper.Box.Str("Alice"))
values.Set("count", Viper.Box.Str("5"))

' Use dollar signs as delimiters
DIM result AS STRING = Viper.Text.Template.RenderWith("Hello $name$!", values, "$", "$")
PRINT result  ' Output: "Hello Alice!"

' Use ERB-style delimiters
result = Viper.Text.Template.RenderWith("<%= name %> has <%= count %> items", values, "<%=", "%>")
PRINT result  ' Output: "Alice has 5 items"

' Use percent delimiters
result = Viper.Text.Template.RenderWith("Hello %name%!", values, "%", "%")
PRINT result  ' Output: "Hello Alice!"
```

### Query Placeholder Keys Example

```basic
DIM template AS STRING = "Hello {{name}}, you have {{count}} messages from {{sender}}."

' Check if template has a specific placeholder
IF Viper.Text.Template.Has(template, "name") THEN
    PRINT "Template uses 'name' placeholder"
END IF

' Get all placeholder keys (as a Bag - unique values)
DIM keys AS OBJECT = Viper.Text.Template.Keys(template)
PRINT keys.Len()  ' Output: 3

' Iterate over keys (order not guaranteed)
' Contains: "name", "count", "sender"
```

### Escape Example

```basic
' Escape braces to output them literally
DIM text AS STRING = "Use {{name}} for placeholders"
DIM escaped AS STRING = Viper.Text.Template.Escape(text)
PRINT escaped  ' Output: "Use {{{{name}}}} for placeholders"

' The escaped text, when rendered, produces the original braces
DIM values AS OBJECT = Map.New()
DIM result AS STRING = Viper.Text.Template.Render(escaped, values)
PRINT result  ' Output: "Use {{name}} for placeholders"
```

### Use Cases

- Generating emails with dynamic content
- Building SQL queries (use with caution - prefer parameterized queries)
- Templating configuration files
- Generating reports with placeholder substitution
- Simple string interpolation without full template engines

---

## Viper.Text.Html

Tolerant HTML parser and utility functions for escaping, unescaping, tag stripping, link extraction, and text extraction.

**Type:** Static utility class

### Methods

| Method                       | Signature              | Description                                              |
|------------------------------|------------------------|----------------------------------------------------------|
| `Parse(html)`                | `Map(String)`          | Parse HTML into a tree of Map nodes                      |
| `ToText(html)`               | `String(String)`       | Strip tags and unescape entities to get plain text       |
| `Escape(text)`               | `String(String)`       | Escape HTML special characters (`<`, `>`, `&`, `"`, `'`) |
| `Unescape(text)`             | `String(String)`       | Unescape HTML entities (`&lt;`, `&gt;`, `&amp;`, etc.)   |
| `StripTags(html)`            | `String(String)`       | Remove all HTML tags (entities left as-is)               |
| `ExtractLinks(html)`         | `Seq(String)`          | Extract all `href` values from `<a>` tags                |
| `ExtractText(html, tagName)` | `Seq(String, String)`  | Extract text content of all matching tags                |

### Parse Tree Structure

`Parse()` returns a root Map node. Each node has the following keys:

| Key        | Type           | Description                            |
|------------|----------------|----------------------------------------|
| `tag`      | String         | Tag name (e.g., `"div"`, `"p"`)        |
| `text`     | String         | Text content of the element            |
| `attrs`    | Map            | Attribute name-value pairs             |
| `children` | Seq of Maps    | Child element nodes                    |

### Notes

- **Tolerant parser:** Handles malformed HTML without trapping. Unclosed tags, missing quotes, and other common issues are handled gracefully.
- **Entity support:** Handles named entities (`&lt;`, `&gt;`, `&amp;`, `&quot;`, `&apos;`, `&nbsp;`), numeric entities (`&#60;`, `&#x3C;`), and passes through unknown entities unchanged.
- **StripTags vs ToText:** `StripTags` removes tags but leaves entities as-is. `ToText` removes tags AND unescapes entities.
- All methods return empty string/empty Seq for NULL input.

### Zia Example

```zia
module HtmlDemo;

bind Viper.Terminal;
bind Viper.Text.Html as Html;

func start() {
    // Escape HTML special characters
    var escaped = Html.Escape("<script>alert('xss')</script>");
    Say("Escaped: " + escaped);

    // Unescape HTML entities
    var unescaped = Html.Unescape("&lt;div&gt;Hello&lt;/div&gt;");
    Say("Unescaped: " + unescaped);

    // Strip HTML tags
    var stripped = Html.StripTags("<p>Hello <b>World</b></p>");
    Say("Stripped: " + stripped);

    // Convert HTML to plain text
    var plain = Html.ToText("<p>Price: &lt;$10&gt;</p>");
    Say("Plain: " + plain);
}
```

### BASIC Example

```basic
' Escape user input for safe HTML display
DIM userInput AS STRING = "<script>alert('xss')</script>"
DIM safe AS STRING = Viper.Text.Html.Escape(userInput)
PRINT safe  ' Output: "&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;"

' Unescape HTML entities
DIM text AS STRING = Viper.Text.Html.Unescape("&lt;div&gt;Hello&lt;/div&gt;")
PRINT text  ' Output: "<div>Hello</div>"

' Strip tags to get raw text
DIM html AS STRING = "<p>Hello <b>World</b></p>"
DIM stripped AS STRING = Viper.Text.Html.StripTags(html)
PRINT stripped  ' Output: "Hello World"

' Convert HTML to plain text (strips tags + unescapes)
DIM plain AS STRING = Viper.Text.Html.ToText("<p>Price: &lt;$10&gt;</p>")
PRINT plain  ' Output: "Price: <$10>"

' Extract all links from HTML
DIM page AS STRING = "<a href=""https://example.com"">Link 1</a><a href=""/about"">About</a>"
DIM links AS OBJECT = Viper.Text.Html.ExtractLinks(page)
PRINT links.Len     ' Output: 2
PRINT links.Get(0)  ' Output: "https://example.com"
PRINT links.Get(1)  ' Output: "/about"

' Extract text from specific tags
DIM doc AS STRING = "<h1>Title</h1><p>Body</p><h1>Another</h1>"
DIM headings AS OBJECT = Viper.Text.Html.ExtractText(doc, "h1")
PRINT headings.Len     ' Output: 2
PRINT headings.Get(0)  ' Output: "Title"

' Parse HTML into a navigable tree
DIM tree AS OBJECT = Viper.Text.Html.Parse("<div class=""main""><p>Hello</p></div>")
' tree is a Map with keys: tag, text, attrs, children
```

### Use Cases

- **Web scraping:** Extract links and content from downloaded HTML
- **Security:** Escape user input before including in HTML output
- **Email processing:** Convert HTML emails to plain text
- **Content extraction:** Pull text from specific HTML elements
- **HTML cleanup:** Strip tags from rich text content

---

## Viper.Text.JsonPath

JSONPath-like query expressions for navigating parsed JSON objects. Works with objects returned by `Viper.Text.Json.Parse()`.

**Type:** Static utility class

### Methods

| Method                      | Signature                   | Description                                          |
|-----------------------------|-----------------------------|------------------------------------------------------|
| `Get(root, path)`           | `Object(Object, String)`    | Get value at path, or NULL if not found              |
| `GetOr(root, path, default)`| `Object(Object, String, Object)` | Get value at path, or default if not found      |
| `Has(root, path)`           | `Boolean(Object, String)`   | Check if path exists in the object                   |
| `Query(root, path)`         | `Seq(Object, String)`       | Get all values matching a wildcard path              |
| `GetStr(root, path)`        | `String(Object, String)`    | Get string value at path, or empty string            |
| `GetInt(root, path)`        | `Integer(Object, String)`   | Get integer value at path, or 0                      |

### Path Syntax

| Syntax           | Description                           | Example              |
|------------------|---------------------------------------|----------------------|
| `key`            | Access object property                | `"name"`             |
| `key1.key2`      | Nested property access                | `"user.name"`        |
| `key[0]`         | Array element by index                | `"items[0]"`         |
| `key1.key2[0].x` | Mixed object/array access             | `"users[0].name"`    |
| `key.*`          | Wildcard (all children)               | `"users.*.name"`     |

### Zia Example

```zia
module JsonPathDemo;

bind Viper.Terminal;
bind Viper.Text.Json as Json;
bind Viper.Text.JsonPath as JP;
bind Viper.Fmt as Fmt;

func start() {
    var data = Json.Parse("{\"user\": {\"name\": \"Alice\", \"age\": 30}}");

    // Get a string value by path
    var name = JP.GetStr(data, "user.name");
    Say("Name: " + name);

    // Check if a path exists
    Say("Has name: " + Fmt.Bool(JP.Has(data, "user.name")));
    Say("Has email: " + Fmt.Bool(JP.Has(data, "user.email")));
}
```

### BASIC Example

```basic
' Parse a JSON document
DIM json AS STRING = "{""user"": {""name"": ""Alice"", ""scores"": [95, 87, 92]}}"
DIM data AS OBJECT = Viper.Text.Json.Parse(json)

' Simple path access
DIM name AS STRING = Viper.Text.JsonPath.GetStr(data, "user.name")
PRINT name  ' Output: "Alice"

' Array access
DIM first AS INTEGER = Viper.Text.JsonPath.GetInt(data, "user.scores[0]")
PRINT first  ' Output: 95

' Check existence
IF Viper.Text.JsonPath.Has(data, "user.email") THEN
    PRINT "Has email"
ELSE
    PRINT "No email field"  ' Output: "No email field"
END IF

' Default values
DIM email AS OBJECT = Viper.Text.JsonPath.GetOr(data, "user.email", Viper.Box.Str("unknown"))
PRINT Viper.Unbox.Str(email)  ' Output: "unknown"

' Wildcard queries
DIM api AS STRING = "{""users"": [{""name"": ""Alice""}, {""name"": ""Bob""}]}"
DIM apiData AS OBJECT = Viper.Text.Json.Parse(api)
DIM names AS OBJECT = Viper.Text.JsonPath.Query(apiData, "users.*.name")
PRINT names.Len     ' Output: 2
```

### Use Cases

- **API responses:** Navigate deeply nested JSON without manual traversal
- **Configuration:** Access config values by dotted paths
- **Data extraction:** Query specific fields from complex JSON structures

---

## Viper.Text.Markdown

Basic Markdown to HTML conversion and text extraction. Supports common Markdown syntax including headers, bold, italic, links, code, lists, and paragraphs.

**Type:** Static utility class

### Methods

| Method                  | Signature        | Description                                    |
|-------------------------|------------------|------------------------------------------------|
| `ToHtml(markdown)`      | `String(String)` | Convert Markdown text to HTML                  |
| `ToText(markdown)`      | `String(String)` | Strip Markdown formatting to get plain text    |
| `ExtractLinks(markdown)`| `Seq(String)`    | Extract all URLs from Markdown links           |
| `ExtractHeadings(markdown)` | `Seq(String)` | Extract all heading texts from Markdown        |

### Supported Markdown Syntax

| Syntax                | HTML Output          | Description            |
|-----------------------|----------------------|------------------------|
| `# Heading`           | `<h1>Heading</h1>`  | Headings (h1-h6)       |
| `**bold**`            | `<strong>bold</strong>` | Bold text           |
| `*italic*`            | `<em>italic</em>`    | Italic text            |
| `` `code` ``          | `<code>code</code>`  | Inline code            |
| `[text](url)`         | `<a href="url">text</a>` | Links              |
| `- item`              | `<li>item</li>`      | Unordered list items   |
| Blank line             | `<p>...</p>`         | Paragraph breaks       |

### Zia Example

```zia
module MarkdownDemo;

bind Viper.Terminal;
bind Viper.Text.Markdown as Md;

func start() {
    var src = "# Hello\nThis is **bold** text.";

    // Convert Markdown to HTML
    var html = Md.ToHtml(src);
    Say("HTML: " + html);

    // Convert Markdown to plain text
    var plain = Md.ToText(src);
    Say("Text: " + plain);
}
```

### BASIC Example

```basic
' Convert Markdown to HTML
DIM md AS STRING = "# Hello" + CHR$(10) + "This is **bold** and *italic*."
DIM html AS STRING = Viper.Text.Markdown.ToHtml(md)
PRINT html
' Output: <h1>Hello</h1><p>This is <strong>bold</strong> and <em>italic</em>.</p>

' Convert to plain text
DIM plain AS STRING = Viper.Text.Markdown.ToText(md)
PRINT plain  ' Output: Hello This is bold and italic.

' Extract all links
DIM doc AS STRING = "Visit [Google](https://google.com) or [GitHub](https://github.com)"
DIM links AS OBJECT = Viper.Text.Markdown.ExtractLinks(doc)
PRINT links.Len     ' Output: 2
PRINT links.Get(0)  ' Output: "https://google.com"

' Extract headings for table of contents
DIM article AS STRING = "# Introduction" + CHR$(10) + "## Background" + CHR$(10) + "## Methods"
DIM headings AS OBJECT = Viper.Text.Markdown.ExtractHeadings(article)
PRINT headings.Len     ' Output: 3
PRINT headings.Get(0)  ' Output: "Introduction"
```

### Notes

- This is a basic Markdown converter, not a full CommonMark implementation
- Supports the most commonly used Markdown features
- Nested formatting (e.g., bold within italic) may not render correctly

### Use Cases

- **Documentation rendering:** Convert Markdown docs to HTML
- **Content preview:** Generate plain text previews from Markdown
- **Link extraction:** Gather all URLs from documentation
- **Table of contents:** Build TOC from headings

---

## Viper.Text.Toml

TOML (Tom's Obvious Minimal Language) configuration file parser and formatter.

**Type:** Static utility class

### Methods

| Method                | Signature              | Description                                    |
|-----------------------|------------------------|------------------------------------------------|
| `Parse(text)`         | `Map(String)`          | Parse TOML text into nested Maps               |
| `IsValid(text)`       | `Boolean(String)`      | Check if text is valid TOML                    |
| `Format(map)`         | `String(Map)`          | Format a Map as TOML text                      |
| `Get(root, keyPath)`  | `Object(Map, String)`  | Get value using dotted key path                |
| `GetStr(root, keyPath)` | `String(Map, String)` | Get string value using dotted key path, or empty string |

### Notes

- **Parse output:** Returns a Map where keys are strings and values are strings, Maps (for sections), or Seqs (for arrays).
- **Dotted paths:** `Get()` and `GetStr()` support dotted key paths like `"server.host"` to navigate into nested sections.
- **Format:** Converts a Map back to TOML text format.
- Invalid TOML returns NULL from `Parse()` rather than trapping.

### Zia Example

```zia
module TomlDemo;

bind Viper.Terminal;
bind Viper.Text.Toml as Toml;
bind Viper.Fmt as Fmt;

func start() {
    // Validate TOML strings
    Say("Valid: " + Fmt.Bool(Toml.IsValid("key = \"value\"")));
    Say("Invalid: " + Fmt.Bool(Toml.IsValid("= bad")));
}
```

### BASIC Example

```basic
' Parse TOML configuration
DIM config AS STRING = "[server]" + CHR$(10) + _
                       "host = ""localhost""" + CHR$(10) + _
                       "port = ""8080""" + CHR$(10) + _
                       "[database]" + CHR$(10) + _
                       "url = ""postgres://localhost/mydb"""

DIM data AS OBJECT = Viper.Text.Toml.Parse(config)

' Access nested values using dotted path
DIM host AS OBJECT = Viper.Text.Toml.Get(data, "server.host")
PRINT Viper.Unbox.Str(host)  ' Output: "localhost"

' Or use GetStr for convenience (returns string directly)
DIM port AS STRING = Viper.Text.Toml.GetStr(data, "server.port")
PRINT port  ' Output: "8080"

' Validate before parsing
IF Viper.Text.Toml.IsValid(userInput) THEN
    DIM parsed AS OBJECT = Viper.Text.Toml.Parse(userInput)
END IF

' Format back to TOML
DIM output AS STRING = Viper.Text.Toml.Format(data)
PRINT output
```

### Use Cases

- **Configuration files:** Parse application config in TOML format
- **Settings management:** Read/write user preferences
- **Build tools:** Parse project metadata files

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

### Zia Example

```zia
module StringBuilderDemo;

bind Viper.Terminal;
bind Viper.Text.StringBuilder as SB;

func start() {
    var sb = SB.New();
    SB.Append(sb, "Hello");
    SB.Append(sb, ", ");
    SB.Append(sb, "World!");
    Say("Result: " + SB.ToString(sb));  // Hello, World!
}
```

### BASIC Example

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

## Viper.Text.JsonStream

SAX-style streaming JSON parser for processing large or incremental JSON data without loading the entire document into memory. Uses a pull-based token stream.

**Type:** Instance class (requires `New(json)`)

### Constructor

| Method       | Signature             | Description                           |
|--------------|-----------------------|---------------------------------------|
| `New(json)`  | `JsonStream(String)`  | Create a streaming parser from a JSON string |

### Properties

| Property    | Type                  | Description                              |
|-------------|-----------------------|------------------------------------------|
| `Depth`     | `Integer` (read-only) | Current nesting depth (0 = top level)    |
| `TokenType` | `Integer` (read-only) | Current token type                       |

### Methods

| Method          | Signature        | Description                                      |
|-----------------|------------------|--------------------------------------------------|
| `Next()`        | `Integer()`      | Advance to next token; returns token type         |
| `HasNext()`     | `Boolean()`      | Check if more tokens remain                       |
| `StringValue()` | `String()`       | Get string value of current token (key/string)    |
| `NumberValue()` | `Double()`       | Get numeric value of current token                |
| `BoolValue()`   | `Boolean()`      | Get boolean value of current token                |
| `Skip()`        | `Void()`         | Skip current value (object, array, or primitive)  |
| `Error()`       | `String()`       | Get error message if token is ERROR               |

### Token Types

| Constant       | Value | Description      |
|----------------|-------|------------------|
| `TOK_NONE`         | 0  | No token yet              |
| `TOK_OBJECT_START` | 1  | `{` — object begins      |
| `TOK_OBJECT_END`   | 2  | `}` — object ends        |
| `TOK_ARRAY_START`  | 3  | `[` — array begins       |
| `TOK_ARRAY_END`    | 4  | `]` — array ends         |
| `TOK_KEY`          | 5  | Object key string         |
| `TOK_STRING`       | 6  | String value              |
| `TOK_NUMBER`       | 7  | Numeric value             |
| `TOK_BOOL`         | 8  | Boolean value             |
| `TOK_NULL`         | 9  | Null value                |
| `TOK_ERROR`        | 10 | Parse error               |
| `TOK_END`          | 11 | End of input              |

### Notes

- Pull-based: call `Next()` to advance to each token
- Use `Skip()` to efficiently skip over objects or arrays you don't need
- `Depth` tracks nesting level for structural navigation
- More memory-efficient than `Json.Parse()` for large documents
- Tokens are consumed in order — no random access

### Zia Example

```zia
module JsonStreamDemo;

bind Viper.Terminal;
bind JS = Viper.Text.JsonStream;

func start() {
    var s = JS.New("{\"name\":\"Alice\",\"age\":30,\"active\":true}");

    // Token-by-token parsing
    while JS.HasNext(s) {
        var tok = JS.Next(s);
        if tok == 5 { Say("Key: " + JS.StringValue(s)); }
        if tok == 6 { Say("String: " + JS.StringValue(s)); }
        if tok == 7 { SayNum(JS.NumberValue(s)); }
        if tok == 8 { SayBool(JS.BoolValue(s)); }
    }

    // Depth tracking
    var s2 = JS.New("{\"a\":{\"b\":1}}");
    JS.Next(s2);              // {
    SayInt(JS.Depth(s2));     // 1
    JS.Next(s2); JS.Next(s2); // key "a", {
    SayInt(JS.Depth(s2));     // 2

    // Skip containers
    var s3 = JS.New("{\"skip\":[1,2,3],\"keep\":\"yes\"}");
    JS.Next(s3);              // {
    JS.Next(s3);              // key "skip"
    JS.Next(s3);              // [ (start array)
    JS.Skip(s3);              // skip array contents
    JS.Next(s3);              // key "keep"
    JS.Next(s3);              // "yes"
    Say(JS.StringValue(s3));  // yes
}
```

### BASIC Example

```basic
' Stream through a JSON document
DIM json AS STRING = "{""name"": ""Alice"", ""scores"": [95, 87, 92]}"
DIM stream AS OBJECT = NEW Viper.Text.JsonStream(json)

DO WHILE stream.HasNext()
    DIM tok AS INTEGER = stream.Next()

    SELECT CASE tok
    CASE 5  ' TOK_KEY
        DIM key AS STRING = stream.StringValue()
        IF key = "scores" THEN
            ' Process the scores array
            stream.Next()  ' consume ARRAY_START
            DO WHILE stream.Next() <> 4  ' until ARRAY_END
                PRINT "Score: "; stream.NumberValue()
            LOOP
        END IF
    END SELECT
LOOP
```

### When to Use JsonStream vs Json

| Scenario                        | Recommendation              |
|---------------------------------|-----------------------------|
| Small JSON (< 100 KB)          | Use `Json.Parse()` (simpler)|
| Large JSON (> 1 MB)            | Use `JsonStream` (less memory) |
| Need only specific fields       | Use `JsonStream` with `Skip()` |
| Need full random access         | Use `Json.Parse()`          |
| Processing JSON line-by-line    | Use `JsonStream`            |

---

## Viper.Data.Serialize

Unified serialization interface for converting data between JSON, XML, YAML, TOML, and CSV formats. Provides format-agnostic parsing, formatting, validation, and round-trip conversion with auto-detection.

**Type:** Static utility class

### Format Constants

| Constant       | Value | Description      |
|----------------|-------|------------------|
| `FORMAT_JSON`  | 0     | JSON (RFC 8259)  |
| `FORMAT_XML`   | 1     | XML (subset)     |
| `FORMAT_YAML`  | 2     | YAML (1.2 subset)|
| `FORMAT_TOML`  | 3     | TOML (v1.0)      |
| `FORMAT_CSV`   | 4     | CSV (RFC 4180)   |

### Methods

| Method                              | Signature                        | Description                                    |
|-------------------------------------|----------------------------------|------------------------------------------------|
| `Parse(text, format)`               | `Object(String, Integer)`        | Parse text in specified format into a value     |
| `Format(value, format)`             | `String(Object, Integer)`        | Format value as compact text in given format    |
| `FormatPretty(value, format, indent)` | `String(Object, Integer, Integer)` | Format with indentation                    |
| `IsValid(text, format)`             | `Boolean(String, Integer)`       | Check if text is valid for given format         |
| `Detect(text)`                      | `Integer(String)`                | Auto-detect format from content (-1 if unknown) |
| `AutoParse(text)`                   | `Object(String)`                 | Parse by auto-detecting the format              |
| `Convert(text, from, to)`           | `String(String, Integer, Integer)` | Convert between formats                      |
| `FormatName(format)`                | `String(Integer)`                | Get format name ("json", "xml", etc.)           |
| `MimeType(format)`                  | `String(Integer)`                | Get MIME type ("application/json", etc.)        |
| `FormatFromName(name)`              | `Integer(String)`                | Look up format by name (case-insensitive)       |
| `Error()`                           | `String()`                       | Get last error message (empty if none)          |

### Notes

- **Auto-detection heuristics:** `{`/`[` → JSON, `<` → XML, `---` → YAML, `[section]`/`key = value` → TOML, commas → CSV
- `Parse()` returns NULL on error; check `Error()` for details
- `Convert()` is a convenience for `Format(Parse(text, from), to)`
- All returned strings are newly allocated
- Dispatches to the format-specific parsers (Json, Xml, Yaml, Toml, Csv) internally

### BASIC Example

```basic
' Parse JSON data
DIM json AS STRING = "{""name"": ""Alice"", ""age"": 30}"
DIM data AS OBJECT = Viper.Data.Serialize.Parse(json, 0)  ' FORMAT_JSON

' Convert JSON to TOML
DIM toml AS STRING = Viper.Data.Serialize.Convert(json, 0, 3)  ' JSON → TOML
PRINT toml

' Auto-detect and parse
DIM unknown AS STRING = "{""key"": ""value""}"
DIM detected AS INTEGER = Viper.Data.Serialize.Detect(unknown)
PRINT Viper.Data.Serialize.FormatName(detected)  ' Output: "json"

DIM parsed AS OBJECT = Viper.Data.Serialize.AutoParse(unknown)

' Pretty-print as JSON
DIM pretty AS STRING = Viper.Data.Serialize.FormatPretty(data, 0, 2)
PRINT pretty

' Validate before parsing
IF Viper.Data.Serialize.IsValid(userInput, 0) THEN
    DIM safe AS OBJECT = Viper.Data.Serialize.Parse(userInput, 0)
END IF
```

### Use Cases

- **Format conversion:** Convert config files between JSON/YAML/TOML
- **API flexibility:** Accept data in any supported format
- **Auto-detection:** Process files without knowing their format in advance
- **Validation:** Check input format before processing
- **Round-trip:** Parse → modify → reformat data

---

## Viper.Text.Diff

Line-based text differencing using the Myers diff algorithm. Computes changes between two strings and can produce unified diffs or apply patches.

**Type:** Static utility class

### Methods

| Method                       | Signature                    | Description                                              |
|------------------------------|------------------------------|----------------------------------------------------------|
| `CountChanges(a, b)`        | `Integer(String, String)`    | Count number of added + removed lines between two texts  |
| `Lines(a, b)`               | `Seq(String, String)`        | Compute line-by-line diff with `" "`, `"+"`, `"-"` prefixes |
| `Patch(original, diff)`     | `String(String, Seq)`        | Apply a diff (from `Lines`) to reconstruct modified text |
| `Unified(a, b, context)`    | `String(String, String, Integer)` | Produce unified diff format with context lines     |

### Notes

- Each entry in the `Lines` result is prefixed: `" "` (unchanged), `"+"` (added), `"-"` (removed)
- `Patch` takes the original text and a Seq of diff lines (as returned by `Lines`) and reconstructs the modified text
- `Unified` produces standard unified diff output similar to `diff -u`, with the specified number of context lines around each change
- All methods operate on line boundaries (splitting on newlines)

### Zia Example

```zia
module DiffDemo;

bind Viper.Terminal;
bind Viper.Text.Diff as Diff;
bind Viper.Fmt as Fmt;

func start() {
    var a = "hello world";
    var b = "hello there";
    Say("Changes: " + Fmt.Int(Diff.CountChanges(a, b)));  // 2

    var orig = "hello\nworld\nfoo";
    var modified = "hello\nthere\nfoo";
    var diff = Diff.Lines(orig, modified);
    Say("Diff lines: " + Fmt.Int(diff.Len));

    var unified = Diff.Unified(orig, modified, 1);
    Say(unified);

    // Round-trip: apply the diff to get modified text back
    var patched = Diff.Patch(orig, diff);
    Say("Patched: " + patched);  // hello\nthere\nfoo
}
```

### BASIC Example

```basic
' Count changes between two strings
DIM a AS STRING = "hello world"
DIM b AS STRING = "hello there"
DIM changes AS INTEGER = Viper.Text.Diff.CountChanges(a, b)
PRINT changes  ' Output: 2

' Compute line-by-line diff
DIM orig AS STRING = "hello" + CHR$(10) + "world" + CHR$(10) + "foo"
DIM modified AS STRING = "hello" + CHR$(10) + "there" + CHR$(10) + "foo"
DIM diff AS OBJECT = Viper.Text.Diff.Lines(orig, modified)
PRINT diff.Len  ' Output: 4 (one unchanged, one removed, one added, one unchanged)

' Produce unified diff
DIM unified AS STRING = Viper.Text.Diff.Unified(orig, modified, 1)
PRINT unified

' Apply patch to reconstruct modified text
DIM patched AS STRING = Viper.Text.Diff.Patch(orig, diff)
PRINT patched = modified  ' Output: 1 (true)
```

### Use Cases

- **Code review:** Show differences between file versions
- **Configuration auditing:** Detect changes in config files
- **Testing:** Compare expected vs actual output
- **Patching:** Apply diffs to transform text programmatically

---

## Viper.Text.Ini

INI configuration file parsing and manipulation. Parses standard INI format with `[section]` headers and `key=value` pairs into a nested Map structure.

**Type:** Static utility class

### Methods

| Method                          | Signature                          | Description                                              |
|---------------------------------|------------------------------------|----------------------------------------------------------|
| `Parse(text)`                   | `Map(String)`                      | Parse INI text into a Map of Maps (section -> key -> value) |
| `Format(doc)`                   | `String(Map)`                      | Format a Map of Maps back to INI text                    |
| `Get(doc, section, key)`        | `String(Map, String, String)`      | Get a value from a section, or empty string if not found |
| `Set(doc, section, key, value)` | `Void(Map, String, String, String)` | Set a value in a section (creates section if needed)    |
| `HasSection(doc, section)`      | `Boolean(Map, String)`             | Check if a section exists                                |
| `Sections(doc)`                 | `Seq(Map)`                         | Get all section names as a Seq                           |
| `Remove(doc, section, key)`     | `Boolean(Map, String, String)`     | Remove a key from a section; returns true if found       |

### Notes

- `Parse` returns a Map where each key is a section name and each value is a Map of key-value string pairs
- Entries that appear before any section header are stored under the empty string key `""`
- `Set` creates the section automatically if it does not already exist
- `Remove` returns true (1) if the key was found and removed, false (0) if not found

### Zia Example

```zia
module IniDemo;

bind Viper.Terminal;
bind Viper.Text.Ini as Ini;
bind Viper.Fmt as Fmt;

func start() {
    var text = "[app]\nname=MyApp\nversion=1.0\n[db]\nhost=localhost";
    var doc = Ini.Parse(text);

    Say("Name: " + Ini.Get(doc, "app", "name"));          // MyApp
    Say("Host: " + Ini.Get(doc, "db", "host"));            // localhost
    Say("Has app: " + Fmt.Bool(Ini.HasSection(doc, "app"))); // true

    Ini.Set(doc, "db", "port", "5432");
    Say(Ini.Format(doc));
}
```

### BASIC Example

```basic
' Parse an INI configuration string
DIM text AS STRING = "[app]" + CHR$(10) + "name=MyApp" + CHR$(10) + "version=1.0" + CHR$(10)
text = text + "[db]" + CHR$(10) + "host=localhost"
DIM doc AS OBJECT = Viper.Text.Ini.Parse(text)

' Read values
DIM name AS STRING = Viper.Text.Ini.Get(doc, "app", "name")
PRINT name  ' Output: "MyApp"

DIM host AS STRING = Viper.Text.Ini.Get(doc, "db", "host")
PRINT host  ' Output: "localhost"

' Missing keys return empty string
DIM missing AS STRING = Viper.Text.Ini.Get(doc, "db", "port")
PRINT missing  ' Output: ""

' Check if section exists
IF Viper.Text.Ini.HasSection(doc, "app") THEN
    PRINT "app section exists"
END IF

' List all sections
DIM sections AS OBJECT = Viper.Text.Ini.Sections(doc)
PRINT sections.Len  ' Output: 2

' Modify and format back to INI text
Viper.Text.Ini.Set(doc, "db", "port", "5432")
DIM output AS STRING = Viper.Text.Ini.Format(doc)
PRINT output

' Remove a key
DIM removed AS INTEGER = Viper.Text.Ini.Remove(doc, "db", "port")
PRINT removed  ' Output: 1 (true)
```

### Use Cases

- **Application config:** Read/write `.ini` or `.cfg` configuration files
- **Settings management:** Store and retrieve user preferences
- **Legacy interop:** Work with INI-format data from older systems

---

## Viper.Text.NumberFormat

Number formatting utilities for human-readable display of integers and floating-point values.

**Type:** Static utility class

### Methods

| Method                   | Signature              | Description                                         |
|--------------------------|------------------------|-----------------------------------------------------|
| `Bytes(n)`               | `String(Integer)`      | Format byte count as human-readable size (KB, MB, GB, etc.) |
| `Currency(amount, sym)`  | `String(Double, String)` | Format as currency with symbol and two decimal places |
| `Decimals(n, places)`    | `String(Double, Integer)` | Format number with specified decimal places        |
| `Ordinal(n)`             | `String(Integer)`      | Format integer as ordinal (1st, 2nd, 3rd, 4th...)  |
| `Pad(n, width)`          | `String(Integer, Integer)` | Zero-pad integer to minimum width               |
| `Percent(n)`             | `String(Double)`       | Format fraction as percentage (0.756 -> "75.6%")   |
| `Thousands(n, sep)`      | `String(Integer, String)` | Format integer with thousands separator          |
| `ToWords(n)`             | `String(Integer)`      | Convert integer to English words (supports up to trillions) |

### Notes

- `Bytes` uses binary units: B, KB, MB, GB, TB with two decimal places where appropriate
- `Currency` adds thousands separators and always shows two decimal places (e.g., `$1,234.56`)
- `Ordinal` handles special cases: 11th, 12th, 13th (not 11st, 12nd, 13th)
- `ToWords` produces hyphenated compound numbers (e.g., "forty-two", "one hundred twenty-three")

### Zia Example

```zia
module NumFmtDemo;

bind Viper.Terminal;
bind Viper.Text.NumberFormat as NF;

func start() {
    Say(NF.Bytes(1048576));            // 1.00 MB
    Say(NF.Currency(29.99, "$"));      // $29.99
    Say(NF.Decimals(3.14159, 2));      // 3.14
    Say(NF.Ordinal(3));                // 3rd
    Say(NF.Pad(7, 3));                 // 007
    Say(NF.Percent(0.756));            // 75.6%
    Say(NF.Thousands(1234567, ","));   // 1,234,567
    Say(NF.ToWords(42));               // forty-two
}
```

### BASIC Example

```basic
' Format bytes as human-readable sizes
PRINT Viper.Text.NumberFormat.Bytes(1048576)     ' Output: "1.00 MB"
PRINT Viper.Text.NumberFormat.Bytes(1073741824)  ' Output: "1.00 GB"

' Currency formatting
PRINT Viper.Text.NumberFormat.Currency(29.99, "$")   ' Output: "$29.99"
PRINT Viper.Text.NumberFormat.Currency(1234.5, "€")  ' Output: "€1,234.50"

' Control decimal places
PRINT Viper.Text.NumberFormat.Decimals(3.14159, 2)  ' Output: "3.14"
PRINT Viper.Text.NumberFormat.Decimals(3.14159, 4)  ' Output: "3.1416"

' Ordinal numbers
PRINT Viper.Text.NumberFormat.Ordinal(1)   ' Output: "1st"
PRINT Viper.Text.NumberFormat.Ordinal(2)   ' Output: "2nd"
PRINT Viper.Text.NumberFormat.Ordinal(3)   ' Output: "3rd"
PRINT Viper.Text.NumberFormat.Ordinal(11)  ' Output: "11th"

' Zero-padding
PRINT Viper.Text.NumberFormat.Pad(7, 3)    ' Output: "007"
PRINT Viper.Text.NumberFormat.Pad(42, 5)   ' Output: "00042"

' Percentage
PRINT Viper.Text.NumberFormat.Percent(0.756)  ' Output: "75.6%"

' Thousands separator
PRINT Viper.Text.NumberFormat.Thousands(1234567, ",")  ' Output: "1,234,567"
PRINT Viper.Text.NumberFormat.Thousands(1234567, ".")  ' Output: "1.234.567"

' Number to English words
PRINT Viper.Text.NumberFormat.ToWords(42)   ' Output: "forty-two"
PRINT Viper.Text.NumberFormat.ToWords(100)  ' Output: "one hundred"
```

### Use Cases

- **Reports:** Format numbers for display in tables and summaries
- **File sizes:** Show download sizes in human-readable units
- **Invoices:** Format currency amounts with proper symbols
- **Localization:** Apply locale-appropriate thousands separators

---

## Viper.Text.Pluralize

English noun pluralization and singularization. Handles common English rules, irregular forms, and uncountable nouns.

**Type:** Static utility class

### Methods

| Method              | Signature                | Description                                          |
|---------------------|--------------------------|------------------------------------------------------|
| `Plural(word)`      | `String(String)`         | Convert a singular noun to its plural form           |
| `Singular(word)`    | `String(String)`         | Convert a plural noun to its singular form           |
| `Count(n, word)`    | `String(Integer, String)` | Format a count with the correctly pluralized noun   |

### Notes

- Handles regular English pluralization rules (adding -s, -es, -ies, etc.)
- Knows common irregular forms: child/children, person/people, mouse/mice, ox/oxen, etc.
- `Count` automatically chooses singular or plural form based on the number (1 = singular, everything else = plural)
- Not a full NLP engine -- covers the approximately 95% common case for English nouns

### Zia Example

```zia
module PluralDemo;

bind Viper.Terminal;
bind Viper.Text.Pluralize as P;

func start() {
    Say(P.Plural("box"));        // boxes
    Say(P.Plural("child"));      // children
    Say(P.Plural("cat"));        // cats
    Say(P.Singular("boxes"));    // box
    Say(P.Singular("children")); // child
    Say(P.Count(1, "item"));     // 1 item
    Say(P.Count(5, "item"));     // 5 items
    Say(P.Count(0, "item"));     // 0 items
}
```

### BASIC Example

```basic
' Pluralize nouns
PRINT Viper.Text.Pluralize.Plural("box")    ' Output: "boxes"
PRINT Viper.Text.Pluralize.Plural("child")  ' Output: "children"
PRINT Viper.Text.Pluralize.Plural("cat")    ' Output: "cats"
PRINT Viper.Text.Pluralize.Plural("city")   ' Output: "cities"

' Singularize nouns
PRINT Viper.Text.Pluralize.Singular("boxes")    ' Output: "box"
PRINT Viper.Text.Pluralize.Singular("children") ' Output: "child"
PRINT Viper.Text.Pluralize.Singular("cats")     ' Output: "cat"

' Count with automatic pluralization
PRINT Viper.Text.Pluralize.Count(0, "item")  ' Output: "0 items"
PRINT Viper.Text.Pluralize.Count(1, "item")  ' Output: "1 item"
PRINT Viper.Text.Pluralize.Count(5, "item")  ' Output: "5 items"
PRINT Viper.Text.Pluralize.Count(1, "child") ' Output: "1 child"
PRINT Viper.Text.Pluralize.Count(3, "child") ' Output: "3 children"
```

### Use Cases

- **User-facing messages:** "You have 3 new messages" vs "You have 1 new message"
- **Report generation:** Dynamically format counts with correct noun forms
- **Template rendering:** Combine with Template class for grammatically correct output

---

## Viper.Text.Scanner

Stateful string scanner for lexing and parsing text. Maintains a position cursor and provides methods for peeking, reading, matching, and skipping characters and tokens.

**Type:** Instance class
**Constructor:** `New(text)` -- creates a scanner positioned at the start of the given text

### Properties

| Property    | Type    | Access     | Description                              |
|-------------|---------|------------|------------------------------------------|
| `Pos`       | Integer | Read/Write | Current byte position (0-indexed)        |
| `IsEnd`     | Boolean | Read-only  | True if at end of string                 |
| `Remaining` | Integer | Read-only  | Number of characters remaining           |
| `Len`       | Integer | Read-only  | Total length of the source string        |

### Methods

| Method                | Signature                  | Description                                                    |
|-----------------------|----------------------------|----------------------------------------------------------------|
| `Reset()`             | `Void()`                   | Reset position to beginning of string                          |
| `Peek()`              | `Integer()`                | Peek at current character without advancing (-1 if at end)     |
| `PeekAt(offset)`      | `Integer(Integer)`         | Peek at character at offset from current position              |
| `PeekStr(n)`          | `String(Integer)`          | Peek at next n characters as a string (without advancing)      |
| `Read()`              | `Integer()`                | Read current character and advance (-1 if at end)              |
| `ReadStr(n)`          | `String(Integer)`          | Read next n characters and advance                             |
| `ReadUntil(delim)`    | `String(Integer)`          | Read until delimiter character (not including it)              |
| `ReadUntilAny(chars)` | `String(String)`           | Read until any of the delimiter characters                     |
| `Match(c)`            | `Boolean(Integer)`         | Check if current position matches character (no advance)       |
| `MatchStr(s)`         | `Boolean(String)`          | Check if current position matches string (no advance)          |
| `Accept(c)`           | `Boolean(Integer)`         | Match and consume character if it matches                      |
| `AcceptStr(s)`        | `Boolean(String)`          | Match and consume string if it matches                         |
| `AcceptAny(chars)`    | `Boolean(String)`          | Match and consume any one of the given characters              |
| `Skip(n)`             | `Void(Integer)`            | Skip n characters                                              |
| `SkipWhitespace()`    | `Integer()`                | Skip whitespace; returns number of characters skipped          |
| `ReadIdent()`         | `String()`                 | Read an identifier (letter/underscore start, then alnum/underscore) |
| `ReadInt()`           | `String()`                 | Read an integer (optional sign + digits)                       |
| `ReadNumber()`        | `String()`                 | Read a number (integer or float)                               |
| `ReadQuoted(quote)`   | `String(Integer)`          | Read a quoted string (handles escapes); returns contents without quotes |
| `ReadLine()`          | `String()`                 | Read until end of line (not including newline)                  |

### Notes

- The scanner operates on byte positions; all character values are byte values (ASCII/Latin-1)
- `Match` and `MatchStr` test without advancing; `Accept` and `AcceptStr` advance only if matched
- `ReadIdent`, `ReadInt`, `ReadNumber`, and `ReadQuoted` return empty string if the current position does not start a valid token of that type
- Setting `Pos` to a value outside the valid range clamps it to the string boundaries

### Zia Example

```zia
module ScannerDemo;

bind Viper.Terminal;
bind Viper.Text.Scanner as Scanner;
bind Viper.Fmt as Fmt;

func start() {
    var sc = Scanner.New("hello 42 world");

    var ident = sc.ReadIdent();
    Say("Ident: " + ident);                         // hello
    sc.SkipWhitespace();
    var num = sc.ReadInt();
    Say("Int: " + num);                              // 42
    sc.SkipWhitespace();
    var rest = sc.ReadIdent();
    Say("Rest: " + rest);                            // world
    Say("AtEnd: " + Fmt.Bool(sc.IsEnd));             // true
    Say("Pos: " + Fmt.Int(sc.Pos));                  // 14
}
```

### BASIC Example

```basic
' Create a scanner for a string
DIM sc AS OBJECT = Viper.Text.Scanner.New("hello 42 world")

' Read an identifier
DIM ident AS STRING = sc.ReadIdent()
PRINT ident       ' Output: "hello"

' Skip whitespace
sc.SkipWhitespace()

' Read an integer
DIM num AS STRING = sc.ReadInt()
PRINT num          ' Output: "42"

' Skip whitespace and read another identifier
sc.SkipWhitespace()
DIM rest AS STRING = sc.ReadIdent()
PRINT rest         ' Output: "world"

' Check position and end state
PRINT sc.IsEnd     ' Output: 1 (true)
PRINT sc.Pos       ' Output: 14
PRINT sc.Len       ' Output: 14

' Reset and scan again
sc.Reset()
PRINT sc.Pos       ' Output: 0
PRINT sc.Remaining ' Output: 14

' Peek without advancing
DIM ch AS INTEGER = sc.Peek()
PRINT CHR$(ch)     ' Output: "h"
PRINT sc.Pos       ' Output: 0 (unchanged)

' Read characters one at a time
ch = sc.Read()
PRINT CHR$(ch)     ' Output: "h"
PRINT sc.Pos       ' Output: 1 (advanced)
```

### Parsing Example

```basic
' Parse a simple key=value format
DIM sc AS OBJECT = Viper.Text.Scanner.New("name=Alice age=30")

DO WHILE NOT sc.IsEnd
    sc.SkipWhitespace()
    IF sc.IsEnd THEN EXIT DO
    DIM key AS STRING = sc.ReadIdent()
    IF sc.Accept(61) THEN  ' 61 = ASCII '='
        DIM value AS STRING = sc.ReadUntilAny(" " + CHR$(10))
        PRINT key; " -> "; value
    END IF
LOOP
' Output:
' name -> Alice
' age -> 30
```

### Use Cases

- **Lexing:** Tokenize source code or structured text
- **Parsing:** Build simple parsers for custom data formats
- **Data extraction:** Pull structured fields from text
- **Protocol handling:** Parse simple text-based protocols

---

## Viper.Text.TextWrapper

Text wrapping, alignment, indentation, and truncation utilities for formatting text to specified widths.

**Type:** Static utility class

### Methods

| Method                            | Signature                        | Description                                              |
|-----------------------------------|----------------------------------|----------------------------------------------------------|
| `Wrap(text, width)`               | `String(String, Integer)`        | Wrap text to width, inserting newlines at word boundaries |
| `WrapLines(text, width)`          | `Seq(String, Integer)`           | Wrap text and return as a Seq of line strings             |
| `Fill(text, width)`               | `String(String, Integer)`        | Wrap text and join lines with single newlines             |
| `Indent(text, prefix)`            | `String(String, String)`         | Add prefix to the start of each line                     |
| `Dedent(text)`                    | `String(String)`                 | Remove common leading whitespace from all lines          |
| `Hang(text, prefix)`              | `String(String, String)`         | Indent all lines except the first (hanging indent)       |
| `Truncate(text, width)`           | `String(String, Integer)`        | Truncate to width with "..." suffix if needed            |
| `TruncateWith(text, width, suffix)` | `String(String, Integer, String)` | Truncate with custom suffix                           |
| `Shorten(text, width)`            | `String(String, Integer)`        | Shorten by replacing middle portion with "..."           |
| `Left(text, width)`               | `String(String, Integer)`        | Left-align text, padding with spaces to width            |
| `Right(text, width)`              | `String(String, Integer)`        | Right-align text, padding with spaces to width           |
| `Center(text, width)`             | `String(String, Integer)`        | Center text, padding with spaces to width                |
| `LineCount(text)`                 | `Integer(String)`                | Count the number of lines in text                        |
| `MaxLineLen(text)`                | `Integer(String)`                | Get the length of the longest line                       |

### Notes

- `Wrap` breaks at word boundaries (spaces); words longer than the width are not broken
- `Fill` is equivalent to `Wrap` but ensures single newlines between lines (normalizes whitespace)
- `Truncate` appends "..." only when the text exceeds the specified width; the total length including "..." equals width
- `Shorten` keeps the beginning and end of the text, replacing the middle with "..."

### Zia Example

```zia
module TextWrapDemo;

bind Viper.Terminal;
bind Viper.Text.TextWrapper as TW;
bind Viper.Fmt as Fmt;

func start() {
    var text = "The quick brown fox jumps over the lazy dog near the riverbank";

    // Wrap text at 20 characters
    Say(TW.Wrap(text, 20));

    // Center text
    Say("[" + TW.Center("hello", 20) + "]");

    // Truncate with ellipsis
    Say(TW.Truncate("This is a long sentence that needs truncating", 20));

    // Indent text
    var lines = "line one\nline two\nline three";
    Say(TW.Indent(lines, "  > "));

    // Line metrics
    Say("Lines: " + Fmt.Int(TW.LineCount(lines)));
    Say("MaxLen: " + Fmt.Int(TW.MaxLineLen(lines)));
}
```

### BASIC Example

```basic
' Wrap text at a specified width
DIM text AS STRING = "The quick brown fox jumps over the lazy dog near the riverbank"
DIM wrapped AS STRING = Viper.Text.TextWrapper.Wrap(text, 20)
PRINT wrapped
' Output (lines broken at word boundaries):
' The quick brown fox
' jumps over the lazy
' dog near the
' riverbank

' Get wrapped lines as a Seq
DIM lines AS OBJECT = Viper.Text.TextWrapper.WrapLines(text, 20)
PRINT lines.Len  ' Output: 4

' Text alignment
PRINT "["; Viper.Text.TextWrapper.Left("hello", 20); "]"    ' Output: [hello               ]
PRINT "["; Viper.Text.TextWrapper.Right("hello", 20); "]"   ' Output: [               hello]
PRINT "["; Viper.Text.TextWrapper.Center("hello", 20); "]"  ' Output: [       hello        ]

' Truncation
DIM long AS STRING = "This is a very long sentence"
PRINT Viper.Text.TextWrapper.Truncate(long, 15)              ' Output: "This is a ve..."
PRINT Viper.Text.TextWrapper.TruncateWith(long, 15, "~")     ' Output: "This is a very~"
PRINT Viper.Text.TextWrapper.Shorten(long, 20)               ' Output: "This is ...sentence"

' Indentation
DIM block AS STRING = "line one" + CHR$(10) + "line two" + CHR$(10) + "line three"
PRINT Viper.Text.TextWrapper.Indent(block, "  ")
' Output:
'   line one
'   line two
'   line three

' Dedent (remove common whitespace)
DIM indented AS STRING = "    hello" + CHR$(10) + "    world"
PRINT Viper.Text.TextWrapper.Dedent(indented)
' Output:
' hello
' world

' Hanging indent
PRINT Viper.Text.TextWrapper.Hang(block, "    ")
' Output:
' line one
'     line two
'     line three

' Line metrics
PRINT Viper.Text.TextWrapper.LineCount(block)    ' Output: 3
PRINT Viper.Text.TextWrapper.MaxLineLen(block)   ' Output: 10
```

### Use Cases

- **Console output:** Format text to terminal width
- **Reports:** Align columns and wrap long descriptions
- **Help text:** Format command-line help messages with indentation
- **Notifications:** Truncate long messages for display in limited space

---

## Viper.Text.Version

Semantic version (SemVer 2.0.0) parsing, comparison, and manipulation. Supports major.minor.patch format with optional pre-release and build metadata.

**Type:** Instance class
**Constructor:** `Parse(versionString)` -- parses a semver string and returns a Version object (NULL if invalid)

### Properties

| Property     | Type    | Access    | Description                                    |
|--------------|---------|-----------|------------------------------------------------|
| `Major`      | Integer | Read-only | Major version number                           |
| `Minor`      | Integer | Read-only | Minor version number                           |
| `Patch`      | Integer | Read-only | Patch version number                           |
| `Prerelease` | String  | Read-only | Pre-release identifier (empty if none)         |
| `Build`      | String  | Read-only | Build metadata string (empty if none)          |

### Methods

| Method            | Signature            | Description                                                 |
|-------------------|----------------------|-------------------------------------------------------------|
| `BumpMajor()`     | `String()`           | Bump major version, reset minor and patch to 0              |
| `BumpMinor()`     | `String()`           | Bump minor version, reset patch to 0                        |
| `BumpPatch()`     | `String()`           | Bump patch version                                          |
| `Cmp(other)`      | `Integer(Version)`   | Compare to another version: -1 (less), 0 (equal), 1 (greater) |
| `IsValid(str)`    | `Boolean(String)`    | Check if a string is a valid semantic version (static)      |
| `Satisfies(constraint)` | `Boolean(String)` | Check if version satisfies a constraint (e.g., ">=1.0.0", "^1.2", "~1.2.3") |
| `ToString()`      | `String()`           | Format version object back to string                        |

### Notes

- Follows SemVer 2.0.0: `MAJOR.MINOR.PATCH[-prerelease][+buildmetadata]`
- `Cmp` ignores build metadata per the SemVer specification; pre-release versions have lower precedence than the associated normal version
- `Satisfies` supports constraint operators: `>=`, `<=`, `>`, `<`, `=`, `^` (compatible), `~` (approximate)
- `BumpMajor`, `BumpMinor`, and `BumpPatch` return new version strings (they do not modify the original object)

### Zia Example

```zia
module VersionDemo;

bind Viper.Terminal;
bind Viper.Text.Version as Version;
bind Viper.Fmt as Fmt;

func start() {
    var v = Version.Parse("1.2.3-beta+build42");

    Say("Major: " + Fmt.Int(v.Major));          // 1
    Say("Minor: " + Fmt.Int(v.Minor));          // 2
    Say("Patch: " + Fmt.Int(v.Patch));          // 3
    Say("Pre: " + v.Prerelease);                // beta
    Say("Build: " + v.Build);                   // build42
    Say("String: " + v.ToString());             // 1.2.3-beta+build42

    Say("BumpMinor: " + v.BumpMinor());         // 1.3.0
    Say("BumpMajor: " + v.BumpMajor());         // 2.0.0

    Say("Valid: " + Fmt.Bool(Version.IsValid("1.0.0")));    // true
    Say("Invalid: " + Fmt.Bool(Version.IsValid("not.a.version"))); // false

    var v2 = Version.Parse("2.0.0");
    Say("Cmp: " + Fmt.Int(v.Cmp(v2)));         // -1
    Say("Satisfies: " + Fmt.Bool(v.Satisfies(">=1.0.0"))); // true
}
```

### BASIC Example

```basic
' Parse a semantic version string
DIM v AS OBJECT = Viper.Text.Version.Parse("1.2.3-beta+build42")

' Access version components
PRINT v.Major       ' Output: 1
PRINT v.Minor       ' Output: 2
PRINT v.Patch       ' Output: 3
PRINT v.Prerelease  ' Output: "beta"
PRINT v.Build       ' Output: "build42"
PRINT v.ToString()  ' Output: "1.2.3-beta+build42"

' Bump versions
PRINT v.BumpMajor()  ' Output: "2.0.0"
PRINT v.BumpMinor()  ' Output: "1.3.0"
PRINT v.BumpPatch()  ' Output: "1.2.4"

' Compare versions
DIM v2 AS OBJECT = Viper.Text.Version.Parse("2.0.0")
DIM cmp AS INTEGER = v.Cmp(v2)
PRINT cmp  ' Output: -1 (v < v2)

' Validate version strings
IF Viper.Text.Version.IsValid("1.0.0") THEN
    PRINT "Valid version"
END IF

IF NOT Viper.Text.Version.IsValid("not.a.version") THEN
    PRINT "Invalid version"
END IF

' Check version constraints
IF v.Satisfies(">=1.0.0") THEN
    PRINT "Meets minimum version"   ' Output: Meets minimum version
END IF

IF v.Satisfies("^1.2") THEN
    PRINT "Compatible with 1.2.x"   ' Output: Compatible with 1.2.x
END IF
```

### Use Cases

- **Dependency management:** Check if a library version meets requirements
- **Release automation:** Bump version numbers programmatically
- **Compatibility checks:** Verify version constraints before loading plugins
- **Version display:** Parse and format version strings for user-facing output

---

## String.Like / String.LikeCI

SQL-style LIKE pattern matching on strings. These are methods available on any String value, providing wildcard matching commonly used in database queries and filtering.

**Type:** String instance methods

### Methods

| Method              | Signature            | Description                                        |
|---------------------|----------------------|----------------------------------------------------|
| `Like(pattern)`     | `Boolean(String)`    | Case-sensitive SQL LIKE pattern matching            |
| `LikeCI(pattern)`   | `Boolean(String)`    | Case-insensitive SQL LIKE pattern matching          |

### Pattern Syntax

| Pattern | Description                                     | Example                          |
|---------|-------------------------------------------------|----------------------------------|
| `%`     | Matches any sequence of zero or more characters | `"hello".Like("%llo")` is true   |
| `_`     | Matches exactly one character                   | `"hello".Like("h_llo")` is true  |
| `\`     | Escape character for literal `%`, `_`, or `\`   | `"100%".Like("100\%")` is true   |

### Zia Example

```zia
module LikeDemo;

bind Viper.Terminal;
bind Viper.Fmt as Fmt;

func start() {
    // Wildcard matching
    Say(Fmt.Bool("hello".Like("h%")));          // true
    Say(Fmt.Bool("hello".Like("%llo")));         // true
    Say(Fmt.Bool("hello".Like("h_llo")));        // true
    Say(Fmt.Bool("hello".Like("world")));         // false

    // Case-insensitive matching
    Say(Fmt.Bool("Hello".LikeCI("hello")));      // true
    Say(Fmt.Bool("Hello".LikeCI("HELLO")));      // true
    Say(Fmt.Bool("Hello".LikeCI("h%")));         // true
}
```

### BASIC Example

```basic
' Basic wildcard matching
PRINT "hello".Like("h%")         ' Output: 1 (starts with h)
PRINT "hello".Like("%llo")       ' Output: 1 (ends with llo)
PRINT "hello".Like("%ell%")      ' Output: 1 (contains ell)
PRINT "hello".Like("h_llo")      ' Output: 1 (_ matches e)
PRINT "hello".Like("h__lo")      ' Output: 1 (__ matches el)
PRINT "hello".Like("world")      ' Output: 0 (no match)

' Case-insensitive matching
PRINT "Hello World".LikeCI("hello%")  ' Output: 1
PRINT "Hello World".LikeCI("HELLO%")  ' Output: 1
PRINT "Hello World".LikeCI("%WORLD")  ' Output: 1

' Escape special characters
PRINT "100%".Like("100\%")       ' Output: 1 (literal %)
PRINT "file_name".Like("file\_name")  ' Output: 1 (literal _)
```

### Use Cases

- **SQL query emulation:** Implement WHERE column LIKE pattern filtering
- **Filename matching:** Match files against user-specified patterns
- **Search filters:** Provide wildcard search in user interfaces
- **Data validation:** Check if strings match expected patterns

---

## See Also

- [Collections](collections.md) - `Seq`, `Map`, and `Bag` used with templates and CSV
- [Input/Output](io.md) - `File` and `LineReader` for reading text files
- [Cryptography](crypto.md) - `Hash` for checksums and authentication of text data
