# Pattern Matching
> Pattern, CompiledPattern, Scanner, Diff, String.Like/LikeCI

**Part of [Viper Runtime Library](../README.md) › [Text Processing](README.md)**

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

```rust
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

```rust
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

```rust
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

```rust
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

- [Encoding & Identity](encoding.md)
- [Data Formats](formats.md)
- [Formatting & Generation](formatting.md)
- [Text Processing Overview](README.md)
- [Viper Runtime Library](../README.md)
