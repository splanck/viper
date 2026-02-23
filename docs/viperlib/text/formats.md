# Data Formats
> Json, JsonPath, JsonStream, Csv, Toml, Ini, Xml, Yaml, and serialization

**Part of [Viper Runtime Library](../README.md) › [Text Processing](README.md)**

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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
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

## Viper.Data.Xml

XML document model with a mutable node tree. Supports parsing, navigation, attribute access, child manipulation, and
XPath-lite path queries. All node values are opaque objects.

**Type:** Static utility class

### Parsing and Validation

| Method             | Signature                | Description                                     |
|--------------------|--------------------------|--------------------------------------------------|
| `Parse(xml)`       | `Object(String)`         | Parse an XML string; returns root node or NULL  |
| `Error()`          | `String()`               | Last parse/operation error (empty if none)      |
| `IsValid(xml)`     | `Boolean(String)`        | True if the string is well-formed XML            |

### Node Creation

| Method              | Signature                | Description                              |
|---------------------|--------------------------|------------------------------------------|
| `Element(tag)`      | `Object(String)`         | Create a new element node with a tag     |
| `Text(content)`     | `Object(String)`         | Create a text node                       |
| `Comment(text)`     | `Object(String)`         | Create a comment node                    |
| `Cdata(text)`       | `Object(String)`         | Create a CDATA section node              |

### Node Information

| Method               | Signature                | Description                                         |
|----------------------|--------------------------|-----------------------------------------------------|
| `NodeType(n)`        | `String(Object)`         | Node type: "element", "text", "comment", "cdata"    |
| `Tag(n)`             | `String(Object)`         | Tag name (element nodes only)                       |
| `Content(n)`         | `String(Object)`         | Raw text content of a text/comment/cdata node       |
| `TextContent(n)`     | `String(Object)`         | All descendant text concatenated (recursive)        |

### Attributes

| Method                    | Signature                         | Description                               |
|---------------------------|-----------------------------------|-------------------------------------------|
| `Attr(n, name)`           | `String(Object, String)`          | Attribute value (empty string if absent)  |
| `HasAttr(n, name)`        | `Boolean(Object, String)`         | True if attribute exists                  |
| `SetAttr(n, name, value)` | `Void(Object, String, String)`    | Set or add attribute                      |
| `RemoveAttr(n, name)`     | `Void(Object, String)`            | Remove attribute                          |
| `AttrNames(n)`            | `Seq(Object)`                     | Sequence of attribute name strings        |

### Navigation

| Method                  | Signature                    | Description                                        |
|-------------------------|------------------------------|----------------------------------------------------|
| `Children(n)`           | `Seq(Object)`                | All child nodes as a sequence                      |
| `ChildCount(n)`         | `Integer(Object)`            | Number of child nodes                              |
| `ChildAt(n, i)`         | `Object(Object, Integer)`    | Child at index i (0-based)                         |
| `Child(n, tag)`         | `Object(Object, String)`     | First child element with given tag, or NULL        |
| `ChildrenByTag(n, tag)` | `Seq(Object, String)`        | All child elements with given tag                  |
| `Parent(n)`             | `Object(Object)`             | Parent node, or NULL for root                      |
| `Root(n)`               | `Object(Object)`             | Document root starting from any node               |

### Search

| Method            | Signature                | Description                                             |
|-------------------|--------------------------|---------------------------------------------------------|
| `Find(n, path)`   | `Object(Object, String)` | Find first node matching a simple path (e.g. "a/b/c")  |
| `FindAll(n, path)`| `Seq(Object, String)`    | Find all nodes matching the path                        |

### Mutation

| Method                    | Signature                        | Description                       |
|---------------------------|----------------------------------|-----------------------------------|
| `Append(n, child)`        | `Void(Object, Object)`           | Append child to node              |
| `Insert(n, i, child)`     | `Void(Object, Integer, Object)`  | Insert child at index i           |
| `Remove(n, child)`        | `Void(Object, Object)`           | Remove specific child node        |
| `RemoveAt(n, i)`          | `Void(Object, Integer)`          | Remove child at index i           |
| `SetText(n, text)`        | `Void(Object, String)`           | Replace all children with a single text node |

### Serialization

| Method                    | Signature                         | Description                                     |
|---------------------------|-----------------------------------|-------------------------------------------------|
| `Format(n)`               | `String(Object)`                  | Compact XML string                              |
| `FormatPretty(n, indent)` | `String(Object, Integer)`         | Indented XML string (spaces per level)          |
| `Escape(s)`               | `String(String)`                  | Escape XML special characters (`<>&"'`)         |
| `Unescape(s)`             | `String(String)`                  | Unescape XML entities                           |

### Notes

- `Parse` returns the document root element on success, or NULL on error.
- Check `Error()` after any NULL return for a diagnostic message.
- Path syntax for `Find`/`FindAll`: slash-separated tag names from the given node (e.g. `"books/book/title"`).
- Attribute and child mutations are performed in-place on the node object.
- `TextContent` is useful for extracting all readable text from a subtree.

### Zia Example

```rust
module XmlDemo;

bind Viper.Terminal;
bind Viper.Data.Xml as Xml;

func start() {
    var src = "<catalog><book id=\"1\"><title>Viper Primer</title><price>29.99</price></book></catalog>";
    var root = Xml.Parse(src);

    var book = Xml.Child(root, "book");
    Say("ID: " + Xml.Attr(book, "id"));                     // 1
    Say("Title: " + Xml.TextContent(Xml.Child(book, "title")));  // Viper Primer

    // Build a new node and append it
    var book2 = Xml.Element("book");
    Xml.SetAttr(book2, "id", "2");
    var t2 = Xml.Element("title");
    Xml.SetText(t2, "Advanced Viper");
    Xml.Append(book2, t2);
    Xml.Append(root, book2);

    Say("Children: " + Xml.Format(root));
    Say("Pretty:\n" + Xml.FormatPretty(root, 2));

    // Query all books
    var books = Xml.ChildrenByTag(root, "book");
    Say("Count: " + Xml.ChildCount(root));  // 2
}
```

### BASIC Example

```basic
DIM src AS STRING = "<config><host>localhost</host><port>8080</port></config>"
DIM root AS OBJECT = Viper.Data.Xml.Parse(src)

IF root = NULL THEN
    PRINT "Parse error: "; Viper.Data.Xml.Error()
    END
END IF

DIM host AS STRING = Viper.Data.Xml.TextContent(Viper.Data.Xml.Child(root, "host"))
DIM port AS STRING = Viper.Data.Xml.TextContent(Viper.Data.Xml.Child(root, "port"))
PRINT "Host: "; host   ' localhost
PRINT "Port: "; port   ' 8080

' Add a new element
DIM debug AS OBJECT = Viper.Data.Xml.Element("debug")
Viper.Data.Xml.SetText(debug, "true")
Viper.Data.Xml.Append(root, debug)

PRINT Viper.Data.Xml.FormatPretty(root, 2)
' <config>
'   <host>localhost</host>
'   <port>8080</port>
'   <debug>true</debug>
' </config>

' Attribute access
DIM items AS STRING = "<items><item key=""a"" val=""1""/><item key=""b"" val=""2""/></items>"
DIM ir AS OBJECT = Viper.Data.Xml.Parse(items)
DIM allItems AS OBJECT = Viper.Data.Xml.ChildrenByTag(ir, "item")
FOR i = 0 TO allItems.Len - 1
    DIM it AS OBJECT = allItems.Get(i)
    PRINT Viper.Data.Xml.Attr(it, "key"); "="; Viper.Data.Xml.Attr(it, "val")
NEXT
```

---

## Viper.Data.Yaml

YAML 1.2 parser and formatter. Converts YAML text to/from Viper objects (maps, sequences, scalars). The object
model mirrors Viper.Text.Json — strings, integers, doubles, booleans, NULL, maps, and sequences.

**Type:** Static utility class

### Methods

| Method                    | Signature                        | Description                                           |
|---------------------------|----------------------------------|-------------------------------------------------------|
| `Parse(yaml)`             | `Object(String)`                 | Parse YAML string; returns value or NULL on error     |
| `Error()`                 | `String()`                       | Last error message (empty string if none)             |
| `IsValid(yaml)`           | `Boolean(String)`                | True if the string is valid YAML                      |
| `Format(obj)`             | `String(Object)`                 | Format a value as compact YAML                        |
| `FormatIndent(obj, spaces)` | `String(Object, Integer)`      | Format with given indentation level                   |
| `TypeOf(obj)`             | `String(Object)`                 | Value type string (see below)                         |

### TypeOf Return Values

| String        | Description                                   |
|---------------|-----------------------------------------------|
| `"null"`      | YAML null / missing value                     |
| `"bool"`      | Boolean (true/false)                          |
| `"int"`       | Integer scalar                                |
| `"float"`     | Floating-point scalar                         |
| `"string"`    | String scalar                                 |
| `"sequence"`  | Ordered list (YAML sequence)                  |
| `"mapping"`   | Key-value pairs (YAML mapping)                |

### Notes

- The returned object uses the same representation as `Viper.Text.Json.Parse` — use `Map`, `Seq`, and scalar
  accessors to traverse the parsed value.
- Multi-document YAML (separated by `---`) is not supported; only the first document is parsed.
- Anchors and aliases are resolved during parsing and are transparent to the caller.
- `Format` round-trips losslessly for all scalar types, sequences, and mappings.
- YAML scalars with no explicit type tag are auto-typed (number detection, boolean keywords, etc.).

### Zia Example

```rust
module YamlDemo;

bind Viper.Terminal;
bind Viper.Data.Yaml as Yaml;
bind Viper.Fmt as Fmt;

func start() {
    var src = "name: Alice\nage: 30\nscores:\n  - 95\n  - 87\n  - 92\n";
    var obj = Yaml.Parse(src);

    Say("Type: " + Yaml.TypeOf(obj));  // mapping

    // Navigate using Map accessors (same as Json object)
    // Re-format as YAML
    Say(Yaml.FormatIndent(obj, 2));

    // Check validity
    Say("Valid: " + Fmt.Bool(Yaml.IsValid(src)));  // true
    Say("Invalid: " + Fmt.Bool(Yaml.IsValid("{")));  // false
}
```

### BASIC Example

```basic
DIM src AS STRING = "server:" & Chr(10) & _
                    "  host: localhost" & Chr(10) & _
                    "  port: 9090" & Chr(10) & _
                    "  tls: false" & Chr(10)

DIM cfg AS OBJECT = Viper.Data.Yaml.Parse(src)

IF cfg = NULL THEN
    PRINT "YAML error: "; Viper.Data.Yaml.Error()
    END
END IF

PRINT "Type: "; Viper.Data.Yaml.TypeOf(cfg)   ' mapping

' Re-format as compact YAML
PRINT Viper.Data.Yaml.Format(cfg)

' Re-format with 4-space indent
PRINT Viper.Data.Yaml.FormatIndent(cfg, 4)

' Validate before processing user input
DIM userYaml AS STRING = "key: value"
IF Viper.Data.Yaml.IsValid(userYaml) THEN
    DIM parsed AS OBJECT = Viper.Data.Yaml.Parse(userYaml)
    PRINT Viper.Data.Yaml.TypeOf(parsed)  ' mapping
END IF
```

---


## See Also

- [Encoding & Identity](encoding.md)
- [Pattern Matching](patterns.md)
- [Formatting & Generation](formatting.md)
- [Text Processing Overview](README.md)
- [Viper Runtime Library](../README.md)
