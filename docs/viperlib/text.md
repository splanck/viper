# Text Processing

> String building, encoding/decoding, pattern matching, and text utilities.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Text.Codec](#vipertextcodec)
- [Viper.Text.Csv](#vipertextcsv)
- [Viper.Text.Guid](#vipertextguid)
- [Viper.Text.Html](#vipertexthtml)
- [Viper.Text.Json](#vipertextjson)
- [Viper.Text.JsonPath](#vipertextjsonpath)
- [Viper.Text.Markdown](#vipertextmarkdown)
- [Viper.Text.Pattern](#vipertextpattern)
- [Viper.Text.CompiledPattern](#vipertextcompiledpattern)
- [Viper.Text.Template](#vipertexttemplate)
- [Viper.Text.Toml](#vipertexttoml)
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

### Zia Example

```zia
module GuidDemo;

bind Viper.Terminal;
bind Viper.Text.Guid as Guid;
bind Viper.Fmt as Fmt;

func start() {
    var id = Guid.New();
    Say("GUID: " + id);                                    // e.g. a1b2c3d4-...
    Say("Valid: " + Fmt.Bool(Guid.IsValid(id)));            // true
    Say("Invalid: " + Fmt.Bool(Guid.IsValid("not-guid"))); // false
}
```

### BASIC Example

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

### Notes

- **Parse output:** Returns a Map where keys are strings and values are strings, Maps (for sections), or Seqs (for arrays).
- **Dotted paths:** `Get()` supports dotted key paths like `"server.host"` to navigate into nested sections.
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

## See Also

- [Collections](collections.md) - `Seq`, `Map`, and `Bag` used with templates and CSV
- [Input/Output](io.md) - `File` and `LineReader` for reading text files
- [Cryptography](crypto.md) - `Hash` for checksums and authentication of text data
