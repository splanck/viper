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
- [Viper.Collections.List](#vipercollectionslist)
- [Viper.Collections.Dictionary](#vipercollectionsdictionary)
- [Viper.Math](#vipermath)
- [Viper.Console](#viperconsole)
- [Viper.Convert](#viperconvert)
- [Viper.Random](#viperrandom)
- [Viper.Environment](#viperenvironment)
- [Viper.DateTime](#viperdatetime)
- [Viper.IO.File](#viperiofile)
- [Viper.Graphics.Window](#vipergraphicswindow)
- [Viper.Graphics.Color](#vipergraphicscolor)

---

## Namespace Overview

### Viper (Root Namespace)

| Class | Type | Description |
|-------|------|-------------|
| `String` | Instance | Immutable string with manipulation methods |
| `Object` | Base class | Root type for all reference types |
| `Math` | Static | Mathematical functions (trig, pow, abs, etc.) |
| `Console` | Static | Console input/output |
| `Convert` | Static | Type conversion utilities |
| `Random` | Static | Random number generation |
| `Environment` | Static | Command-line arguments and environment |
| `DateTime` | Static | Date and time operations |

### Viper.Text

| Class | Type | Description |
|-------|------|-------------|
| `StringBuilder` | Instance | Mutable string builder for efficient concatenation |

### Viper.Collections

| Class | Type | Description |
|-------|------|-------------|
| `List` | Instance | Dynamic array of objects |
| `Dictionary` | Instance | String-keyed hash map |

### Viper.IO

| Class | Type | Description |
|-------|------|-------------|
| `File` | Static | File system operations (read, write, delete) |

### Viper.Graphics

| Class | Type | Description |
|-------|------|-------------|
| `Window` | Instance | 2D graphics window with drawing primitives |
| `Color` | Static | Color creation utilities |

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

---

## Viper.Object

Base class for all Viper reference types. Provides fundamental object operations.

**Type:** Base class (not instantiated directly)

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Equals(other)` | `Boolean(Object)` | Compares this object with another for equality |
| `GetHashCode()` | `Integer()` | Returns a hash code for the object |
| `ToString()` | `String()` | Returns a string representation of the object |
| `ReferenceEquals(a, b)` | `Boolean(Object, Object)` | Static method that tests if two references point to the same object |

### Example

```basic
DIM obj1 AS Viper.Object
DIM obj2 AS Viper.Object

IF obj1.Equals(obj2) THEN
    PRINT "Objects are equal"
END IF

PRINT obj1.ToString()
PRINT obj1.GetHashCode()
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
| `RemoveAt(index)` | `Void(Integer)` | Removes the item at the specified index |
| `get_Item(index)` | `Object(Integer)` | Gets the item at the specified index |
| `set_Item(index, value)` | `Void(Integer, Object)` | Sets the item at the specified index |

### Example

```basic
DIM list AS Viper.Collections.List
list = NEW Viper.Collections.List()

' Add items
list.Add(item1)
list.Add(item2)
list.Add(item3)

PRINT list.Count  ' Output: 3

' Access by index
DIM first AS Object
first = list.get_Item(0)

' Modify by index
list.set_Item(1, newItem)

' Remove item
list.RemoveAt(0)
PRINT list.Count  ' Output: 2

' Clear all
list.Clear()
```

---

## Viper.Collections.Dictionary

Hash map with string keys and object values.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Dictionary()`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Count` | Integer | Number of key-value pairs in the dictionary |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Set(key, value)` | `Void(String, Object)` | Sets a key-value pair (adds or updates) |
| `Get(key)` | `Object(String)` | Gets the value for a key |
| `ContainsKey(key)` | `Boolean(String)` | Returns true if the key exists |
| `Remove(key)` | `Boolean(String)` | Removes a key-value pair; returns true if removed |
| `Clear()` | `Void()` | Removes all key-value pairs |

### Example

```basic
DIM dict AS Viper.Collections.Dictionary
dict = NEW Viper.Collections.Dictionary()

' Add entries
dict.Set("name", nameObj)
dict.Set("age", ageObj)
dict.Set("city", cityObj)

PRINT dict.Count  ' Output: 3

' Check existence
IF dict.ContainsKey("name") THEN
    DIM value AS Object
    value = dict.Get("name")
    PRINT "Found: "; value.ToString()
END IF

' Update
dict.Set("name", newNameObj)

' Remove
IF dict.Remove("city") THEN
    PRINT "Removed city"
END IF

' Clear all
dict.Clear()
```

---

## Viper.Math

Mathematical functions and constants.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Abs(x)` | `Double(Double)` | Absolute value of a floating-point number |
| `AbsInt(x)` | `Integer(Integer)` | Absolute value of an integer |
| `Sqrt(x)` | `Double(Double)` | Square root |
| `Sin(x)` | `Double(Double)` | Sine (radians) |
| `Cos(x)` | `Double(Double)` | Cosine (radians) |
| `Tan(x)` | `Double(Double)` | Tangent (radians) |
| `Atan(x)` | `Double(Double)` | Arctangent (returns radians) |
| `Floor(x)` | `Double(Double)` | Largest integer less than or equal to x |
| `Ceil(x)` | `Double(Double)` | Smallest integer greater than or equal to x |
| `Pow(base, exp)` | `Double(Double, Double)` | Raises base to the power of exp |
| `Log(x)` | `Double(Double)` | Natural logarithm (base e) |
| `Exp(x)` | `Double(Double)` | e raised to the power x |
| `Sgn(x)` | `Double(Double)` | Sign of x: -1, 0, or 1 |
| `SgnInt(x)` | `Integer(Integer)` | Sign of integer x: -1, 0, or 1 |
| `Min(a, b)` | `Double(Double, Double)` | Smaller of two floating-point values |
| `Max(a, b)` | `Double(Double, Double)` | Larger of two floating-point values |
| `MinInt(a, b)` | `Integer(Integer, Integer)` | Smaller of two integers |
| `MaxInt(a, b)` | `Integer(Integer, Integer)` | Larger of two integers |

### Example

```basic
DIM x AS DOUBLE
x = 2.5

PRINT Viper.Math.Sqrt(16)        ' Output: 4.0
PRINT Viper.Math.Pow(2, 10)      ' Output: 1024.0
PRINT Viper.Math.Abs(-42.5)      ' Output: 42.5
PRINT Viper.Math.Floor(2.7)      ' Output: 2.0
PRINT Viper.Math.Ceil(2.1)       ' Output: 3.0

' Trigonometry (radians)
DIM pi AS DOUBLE
pi = 3.14159265358979
PRINT Viper.Math.Sin(pi / 2)     ' Output: 1.0
PRINT Viper.Math.Cos(0)          ' Output: 1.0

' Min/Max
PRINT Viper.Math.MaxInt(10, 20)  ' Output: 20
PRINT Viper.Math.Min(3.5, 2.1)   ' Output: 2.1
```

---

## Viper.Console

Console input and output operations.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `WriteLine(text)` | `Void(String)` | Writes text followed by a newline to standard output |
| `ReadLine()` | `String()` | Reads a line of text from standard input |

### Example

```basic
Viper.Console.WriteLine("What is your name?")
DIM name AS STRING
name = Viper.Console.ReadLine()
Viper.Console.WriteLine("Hello, " + name + "!")
```

### Note

For most BASIC programs, the `PRINT` and `INPUT` statements are more convenient. Use `Viper.Console` when you need explicit control or are working at the IL level.

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
```

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

### Note

For more advanced file operations (line-by-line reading, binary files, etc.), use the lower-level file I/O statements (`OPEN`, `INPUT #`, `PRINT #`, `CLOSE`).

---

## Viper.Graphics.Window

2D graphics window for visual applications and games.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Graphics.Window(width, height, title)` — *Note: Constructor parameters may require IL-level setup*

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Width` | Integer | Window width in pixels |
| `Height` | Integer | Window height in pixels |
| `ShouldClose` | Integer | Non-zero if the user requested to close the window |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Update()` | `Void()` | Presents the back buffer and processes events |
| `Clear(color)` | `Void(Integer)` | Clears the window with a solid color |
| `DrawLine(x1, y1, x2, y2, color)` | `Void(Integer...)` | Draws a line between two points |
| `DrawRect(x, y, w, h, color)` | `Void(Integer...)` | Draws a filled rectangle |
| `DrawRectOutline(x, y, w, h, color)` | `Void(Integer...)` | Draws a rectangle outline |
| `DrawCircle(x, y, r, color)` | `Void(Integer...)` | Draws a filled circle |
| `DrawCircleOutline(x, y, r, color)` | `Void(Integer...)` | Draws a circle outline |
| `SetPixel(x, y, color)` | `Void(Integer, Integer, Integer)` | Sets a single pixel |
| `PollEvent()` | `Integer()` | Polls for input events |
| `KeyDown(keycode)` | `Integer(Integer)` | Returns non-zero if the specified key is pressed |

### Color Format

Colors are specified as 32-bit integers in `0x00RRGGBB` format:
- Red: `0x00FF0000`
- Green: `0x0000FF00`
- Blue: `0x000000FF`
- White: `0x00FFFFFF`
- Black: `0x00000000`

Use `Viper.Graphics.Color.RGB()` to create colors from components.

### Example

```basic
' Create a window
DIM win AS Viper.Graphics.Window
' (Window creation requires runtime setup)

' Main loop
DO WHILE win.ShouldClose = 0
    ' Clear to black
    win.Clear(&H00000000)

    ' Draw a red rectangle
    win.DrawRect(100, 100, 200, 150, &H00FF0000)

    ' Draw a blue circle
    win.DrawCircle(400, 300, 50, &H000000FF)

    ' Draw a green line
    win.DrawLine(0, 0, 800, 600, &H0000FF00)

    ' Present
    win.Update()

    ' Poll events
    win.PollEvent()
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

DIM gray AS INTEGER
gray = Viper.Graphics.Color.RGB(128, 128, 128)

' Use with graphics window
win.DrawRect(10, 10, 100, 100, red)
win.DrawCircle(200, 200, 50, purple)
```

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
