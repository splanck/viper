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
- [Viper.Collections.Seq](#vipercollectionsseq)
- [Viper.Math](#vipermath)
- [Viper.Terminal](#viperterminal)
- [Viper.Convert](#viperconvert)
- [Viper.Random](#viperrandom)
- [Viper.Environment](#viperenvironment)
- [Viper.DateTime](#viperdatetime)
- [Viper.IO.File](#viperiofile)
- [Viper.Graphics.Canvas](#vipergraphicscanvas)
- [Viper.Graphics.Color](#vipergraphicscolor)

---

## Namespace Overview

### Viper (Root Namespace)

| Class | Type | Description |
|-------|------|-------------|
| `String` | Instance | Immutable string with manipulation methods |
| `Object` | Base class | Root type for all reference types |
| `Math` | Static | Mathematical functions (trig, pow, abs, etc.) |
| `Terminal` | Static | Terminal input/output |
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
| `Seq` | Instance | Dynamic sequence (growable array) with stack/queue operations |

### Viper.IO

| Class | Type | Description |
|-------|------|-------------|
| `File` | Static | File system operations (read, write, delete) |

### Viper.Graphics

| Class | Type | Description |
|-------|------|-------------|
| `Canvas` | Instance | 2D graphics canvas with drawing primitives |
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
| `IsNative()` | `Boolean()` | Returns `TRUE` when running native code, `FALSE` when running in the VM |

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
