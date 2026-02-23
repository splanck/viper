# Formatting & Generation
> Template, StringBuilder, TextWrapper, NumberFormat, Pluralize, Version, Html, Markdown

**Part of [Viper Runtime Library](../README.md) › [Text Processing](README.md)**

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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
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

```rust
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


## See Also

- [Encoding & Identity](encoding.md)
- [Data Formats](formats.md)
- [Pattern Matching](patterns.md)
- [Text Processing Overview](README.md)
- [Viper Runtime Library](../README.md)
