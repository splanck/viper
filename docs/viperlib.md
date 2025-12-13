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
- [Viper.Text.Guid](#vipertextguid)
- [Viper.Collections.List](#vipercollectionslist)
- [Viper.Collections.Map](#vipercollectionsmap)
- [Viper.Collections.Seq](#vipercollectionsseq)
- [Viper.Collections.Stack](#vipercollectionsstack)
- [Viper.Collections.Queue](#vipercollectionsqueue)
- [Viper.Collections.Bytes](#vipercollectionsbytes)
- [Viper.Math](#vipermath)
- [Viper.Terminal](#viperterminal)
- [Viper.Convert](#viperconvert)
- [Viper.Random](#viperrandom)
- [Viper.Environment](#viperenvironment)
- [Viper.DateTime](#viperdatetime)
- [Viper.IO.File](#viperiofile)
- [Viper.IO.Path](#viperiopath)
- [Viper.IO.Dir](#viperiodir)
- [Viper.Graphics.Canvas](#vipergraphicscanvas)
- [Viper.Graphics.Color](#vipergraphicscolor)
- [Viper.Time.Clock](#vipertimeclock)
- [Viper.Diagnostics.Assert](#viperdiagnosticsassert)
- [Viper.Diagnostics.Stopwatch](#viperdiagnosticsstopwatch)
- [Runtime Architecture](#runtime-architecture)

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
| `Guid` | Static | UUID version 4 generation and manipulation |

### Viper.Collections

| Class | Type | Description |
|-------|------|-------------|
| `List` | Instance | Dynamic array of objects |
| `Map` | Instance | String-keyed hash map with Keys/Values iteration |
| `Seq` | Instance | Dynamic sequence (growable array) with stack/queue operations |
| `Stack` | Instance | LIFO (last-in-first-out) collection |
| `Queue` | Instance | FIFO (first-in-first-out) collection |
| `Bytes` | Instance | Efficient byte array for binary data |

### Viper.IO

| Class | Type | Description |
|-------|------|-------------|
| `File` | Static | File system operations (read, write, delete) |
| `Path` | Static | Cross-platform path manipulation utilities |
| `Dir` | Static | Directory operations (create, remove, list) |

### Viper.Graphics

| Class | Type | Description |
|-------|------|-------------|
| `Canvas` | Instance | 2D graphics canvas with drawing primitives |
| `Color` | Static | Color creation utilities |

### Viper.Time

| Class | Type | Description |
|-------|------|-------------|
| `Clock` | Static | Basic timing utilities (sleep, ticks) |

### Viper.Diagnostics

| Class | Type | Description |
|-------|------|-------------|
| `Assert` | Static | Trap when a condition fails |
| `Stopwatch` | Instance | High-precision timing for benchmarking |

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

## Viper.Text.Guid

UUID version 4 (random) generation and manipulation per RFC 4122.

**Type:** Static utility class

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Empty` | String | Returns the nil UUID "00000000-0000-0000-0000-000000000000" |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `String()` | Generate a new random UUID v4 |
| `IsValid(guid)` | `Boolean(String)` | Check if string is a valid GUID format |
| `ToBytes(guid)` | `Bytes(String)` | Convert GUID string to 16-byte array |
| `FromBytes(bytes)` | `String(Bytes)` | Convert 16-byte array to GUID string |

### Notes

- Generated GUIDs follow UUID version 4 format (random)
- Format: `xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx` where:
  - `4` indicates version 4 (random UUID)
  - `y` is one of `8`, `9`, `a`, or `b` (variant indicator)
- All hex characters are lowercase
- Uses cryptographically secure random source where available (/dev/urandom on Unix, CryptGenRandom on Windows)
- `ToBytes()` traps if the GUID format is invalid
- `FromBytes()` traps if the Bytes object is not exactly 16 bytes

### Example

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

## Viper.Collections.Map

A key-value dictionary with string keys. Provides O(1) average-case lookup, insertion, and deletion.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Map()`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Len` | Integer | Number of key-value pairs in the map |
| `IsEmpty` | Boolean | Returns true if the map has no entries |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Set(key, value)` | `Void(String, Object)` | Add or update key-value pair |
| `Get(key)` | `Object(String)` | Get value for key (returns NULL if not found) |
| `Has(key)` | `Boolean(String)` | Check if key exists |
| `Remove(key)` | `Boolean(String)` | Remove key-value pair; returns true if found |
| `Clear()` | `Void()` | Remove all entries |
| `Keys()` | `Seq()` | Get sequence of all keys |
| `Values()` | `Seq()` | Get sequence of all values |

### Example

```basic
DIM scores AS Viper.Collections.Map
scores = NEW Viper.Collections.Map()

' Add entries
scores.Set("Alice", 95)
scores.Set("Bob", 87)
scores.Set("Carol", 92)

PRINT scores.Len      ' Output: 3
PRINT scores.IsEmpty  ' Output: False

' Check existence and get value
IF scores.Has("Alice") THEN
    PRINT "Alice's score: "; scores.Get("Alice")
END IF

' Update existing entry
scores.Set("Bob", 91)

' Remove an entry
IF scores.Remove("Carol") THEN
    PRINT "Removed Carol"
END IF

' Iterate over keys
DIM names AS Viper.Collections.Seq
names = scores.Keys()
FOR i = 0 TO names.Len - 1
    PRINT names.Get(i)
NEXT i

' Clear all
scores.Clear()
PRINT scores.IsEmpty  ' Output: True
```

### Use Cases

- **Configuration storage:** Store key-value settings
- **Caching:** Cache computed values by key
- **Lookup tables:** Map identifiers to objects
- **Counting:** Count occurrences by key

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

## Viper.Collections.Stack

A LIFO (last-in-first-out) collection. Elements are added and removed from the top.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Stack()`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Len` | Integer | Number of elements on the stack |
| `IsEmpty` | Boolean | Returns true if the stack has no elements |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Push(value)` | `Void(Object)` | Add element to top of stack |
| `Pop()` | `Object()` | Remove and return top element (traps if empty) |
| `Peek()` | `Object()` | Return top element without removing (traps if empty) |
| `Clear()` | `Void()` | Remove all elements |

### Example

```basic
DIM stack AS Viper.Collections.Stack
stack = NEW Viper.Collections.Stack()

' Push elements onto the stack
stack.Push("first")
stack.Push("second")
stack.Push("third")

PRINT stack.Len      ' Output: 3
PRINT stack.IsEmpty  ' Output: False

' Pop returns elements in LIFO order
PRINT stack.Pop()    ' Output: "third"
PRINT stack.Peek()   ' Output: "second" (still on stack)
PRINT stack.Len      ' Output: 2

' Clear the stack
stack.Clear()
PRINT stack.IsEmpty  ' Output: True
```

### Use Cases

- **Undo/Redo:** Push actions to track history, pop to undo
- **Expression parsing:** Track operators and operands
- **Backtracking algorithms:** Store states to return to
- **Function call simulation:** Track return addresses

---

## Viper.Collections.Queue

A FIFO (first-in-first-out) collection. Elements are added at the back and removed from the front. Implemented as a circular buffer for O(1) add and take operations.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Queue()`

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Len` | Integer | Number of elements in the queue |
| `IsEmpty` | Boolean | Returns true if the queue has no elements |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Add(value)` | `Void(Object)` | Add element to back of queue |
| `Take()` | `Object()` | Remove and return front element (traps if empty) |
| `Peek()` | `Object()` | Return front element without removing (traps if empty) |
| `Clear()` | `Void()` | Remove all elements |

### Example

```basic
DIM queue AS Viper.Collections.Queue
queue = NEW Viper.Collections.Queue()

' Add elements to the queue
queue.Add("first")
queue.Add("second")
queue.Add("third")

PRINT queue.Len      ' Output: 3
PRINT queue.IsEmpty  ' Output: False

' Take returns elements in FIFO order
PRINT queue.Take()   ' Output: "first"
PRINT queue.Peek()   ' Output: "second" (still in queue)
PRINT queue.Len      ' Output: 2

' Clear the queue
queue.Clear()
PRINT queue.IsEmpty  ' Output: True
```

### Use Cases

- **Task scheduling:** Process tasks in the order they arrive
- **Breadth-first search:** Track nodes to visit
- **Message passing:** Handle messages in arrival order
- **Print queues:** Process print jobs sequentially

---

## Viper.Collections.Bytes

An efficient byte array for binary data. More memory-efficient than Seq for byte manipulation.

**Type:** Instance (obj)
**Constructors:**
- `NEW Viper.Collections.Bytes(length)` - Create zero-filled byte array
- `Viper.Collections.Bytes.FromStr(str)` - Create from string (UTF-8 bytes)
- `Viper.Collections.Bytes.FromHex(hex)` - Create from hexadecimal string

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Len` | Integer | Number of bytes |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Get(index)` | `Integer(Integer)` | Get byte value (0-255) at index |
| `Set(index, value)` | `Void(Integer, Integer)` | Set byte value at index (clamped to 0-255) |
| `Slice(start, end)` | `Bytes(Integer, Integer)` | Create new byte array from range [start, end) |
| `Copy(dstOffset, src, srcOffset, count)` | `Void(...)` | Copy bytes between arrays |
| `ToStr()` | `String()` | Convert to string (interprets as UTF-8) |
| `ToHex()` | `String()` | Convert to lowercase hexadecimal string |
| `Fill(value)` | `Void(Integer)` | Set all bytes to value |
| `Find(value)` | `Integer(Integer)` | Find first occurrence (-1 if not found) |
| `Clone()` | `Bytes()` | Create independent copy |

### Example

```basic
' Create a 4-byte array and set values
DIM data AS Viper.Collections.Bytes
data = NEW Viper.Collections.Bytes(4)
data.Set(0, &HDE)
data.Set(1, &HAD)
data.Set(2, &HBE)
data.Set(3, &HEF)

PRINT data.ToHex()  ' Output: "deadbeef"
PRINT data.Len      ' Output: 4

' Create from hex string
DIM copy AS Viper.Collections.Bytes
copy = Viper.Collections.Bytes.FromHex("cafebabe")
PRINT copy.Get(0)   ' Output: 202 (0xCA)

' Create from string
DIM text AS Viper.Collections.Bytes
text = Viper.Collections.Bytes.FromStr("Hello")
PRINT text.Len      ' Output: 5
PRINT text.Get(0)   ' Output: 72 (ASCII 'H')

' Slice and copy
DIM slice AS Viper.Collections.Bytes
slice = data.Slice(1, 3)  ' Bytes at indices 1 and 2
PRINT slice.Len           ' Output: 2

' Find a byte
PRINT data.Find(&HBE)     ' Output: 2

' Fill with a value
data.Fill(0)
PRINT data.ToHex()        ' Output: "00000000"
```

### Use Cases

- **Binary file parsing:** Read and manipulate binary file formats
- **Network protocols:** Pack and unpack protocol messages
- **Cryptography:** Handle raw byte sequences
- **Image data:** Manipulate raw pixel data
- **Base encoding:** Convert between binary and text representations

---

## Viper.Math

Mathematical functions and constants.

**Type:** Static utility class

### Constants

| Property | Type | Description |
|----------|------|-------------|
| `Pi` | `Double` | π (3.14159265358979...) |
| `E` | `Double` | Euler's number (2.71828182845904...) |
| `Tau` | `Double` | τ = 2π (6.28318530717958...) |

### Basic Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Abs(x)` | `Double(Double)` | Absolute value of a floating-point number |
| `AbsInt(x)` | `Integer(Integer)` | Absolute value of an integer |
| `Sqrt(x)` | `Double(Double)` | Square root |
| `Pow(base, exp)` | `Double(Double, Double)` | Raises base to the power of exp |
| `Exp(x)` | `Double(Double)` | e raised to the power x |
| `Sgn(x)` | `Double(Double)` | Sign of x: -1, 0, or 1 |
| `SgnInt(x)` | `Integer(Integer)` | Sign of integer x: -1, 0, or 1 |

### Trigonometric Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sin(x)` | `Double(Double)` | Sine (radians) |
| `Cos(x)` | `Double(Double)` | Cosine (radians) |
| `Tan(x)` | `Double(Double)` | Tangent (radians) |
| `Atan(x)` | `Double(Double)` | Arctangent (returns radians) |
| `Atan2(y, x)` | `Double(Double, Double)` | Arctangent of y/x (returns radians, respects quadrant) |
| `Asin(x)` | `Double(Double)` | Arc sine (returns radians) |
| `Acos(x)` | `Double(Double)` | Arc cosine (returns radians) |

### Hyperbolic Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sinh(x)` | `Double(Double)` | Hyperbolic sine |
| `Cosh(x)` | `Double(Double)` | Hyperbolic cosine |
| `Tanh(x)` | `Double(Double)` | Hyperbolic tangent |

### Logarithmic Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Log(x)` | `Double(Double)` | Natural logarithm (base e) |
| `Log10(x)` | `Double(Double)` | Base-10 logarithm |
| `Log2(x)` | `Double(Double)` | Base-2 logarithm |

### Rounding Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Floor(x)` | `Double(Double)` | Largest integer less than or equal to x |
| `Ceil(x)` | `Double(Double)` | Smallest integer greater than or equal to x |
| `Round(x)` | `Double(Double)` | Round to nearest integer |
| `Trunc(x)` | `Double(Double)` | Truncate toward zero |

### Min/Max Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `Min(a, b)` | `Double(Double, Double)` | Smaller of two floating-point values |
| `Max(a, b)` | `Double(Double, Double)` | Larger of two floating-point values |
| `MinInt(a, b)` | `Integer(Integer, Integer)` | Smaller of two integers |
| `MaxInt(a, b)` | `Integer(Integer, Integer)` | Larger of two integers |
| `Clamp(val, lo, hi)` | `Double(Double, Double, Double)` | Constrain value to range [lo, hi] |
| `ClampInt(val, lo, hi)` | `Integer(Integer, Integer, Integer)` | Constrain integer to range [lo, hi] |

### Utility Functions

| Method | Signature | Description |
|--------|-----------|-------------|
| `FMod(x, y)` | `Double(Double, Double)` | Floating-point remainder of x/y |
| `Lerp(a, b, t)` | `Double(Double, Double, Double)` | Linear interpolation: a + t*(b-a) |
| `Wrap(val, lo, hi)` | `Double(Double, Double, Double)` | Wrap value to range [lo, hi) |
| `WrapInt(val, lo, hi)` | `Integer(Integer, Integer, Integer)` | Wrap integer to range [lo, hi) |
| `Hypot(x, y)` | `Double(Double, Double)` | Hypotenuse: sqrt(x² + y²) |

### Angle Conversion

| Method | Signature | Description |
|--------|-----------|-------------|
| `Deg(radians)` | `Double(Double)` | Convert radians to degrees |
| `Rad(degrees)` | `Double(Double)` | Convert degrees to radians |

### Example

```basic
' Using constants
PRINT Viper.Math.Pi              ' Output: 3.14159265358979
PRINT Viper.Math.E               ' Output: 2.71828182845905

' Basic math
PRINT Viper.Math.Sqrt(16)        ' Output: 4.0
PRINT Viper.Math.Pow(2, 10)      ' Output: 1024.0
PRINT Viper.Math.Abs(-42.5)      ' Output: 42.5

' Rounding
PRINT Viper.Math.Floor(2.7)      ' Output: 2.0
PRINT Viper.Math.Ceil(2.1)       ' Output: 3.0
PRINT Viper.Math.Round(2.5)      ' Output: 3.0
PRINT Viper.Math.Trunc(-2.7)     ' Output: -2.0

' Trigonometry (radians)
PRINT Viper.Math.Sin(Viper.Math.Pi / 2)  ' Output: 1.0
PRINT Viper.Math.Cos(0)                   ' Output: 1.0

' Angle conversion
PRINT Viper.Math.Deg(Viper.Math.Pi)      ' Output: 180.0
PRINT Viper.Math.Rad(90)                  ' Output: 1.5707963...

' Min/Max/Clamp
PRINT Viper.Math.MaxInt(10, 20)          ' Output: 20
PRINT Viper.Math.Clamp(15, 0, 10)        ' Output: 10.0

' Lerp and Wrap
PRINT Viper.Math.Lerp(0, 100, 0.5)       ' Output: 50.0
PRINT Viper.Math.Wrap(370, 0, 360)       ' Output: 10.0

' Geometry
PRINT Viper.Math.Hypot(3, 4)             ' Output: 5.0
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
| `GetVariable(name)` | `String(String)` | Returns the value of an environment variable, or `""` when missing |
| `HasVariable(name)` | `Boolean(String)` | Returns `TRUE` when the environment variable exists |
| `IsNative()` | `Boolean()` | Returns `TRUE` when running native code, `FALSE` when running in the VM |
| `SetVariable(name, value)` | `Void(String, String)` | Sets or overwrites an environment variable (empty value allowed) |
| `EndProgram(code)` | `Void(Integer)` | Terminates the program with the provided exit code |

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

' Environment variables
DIM name AS STRING
DIM value AS STRING
name = "VIPER_SAMPLE_ENV"
value = Viper.Environment.GetVariable(name)
IF Viper.Environment.HasVariable(name) THEN
    PRINT name; " is set to "; value
ELSE
    PRINT name; " is not set"
END IF

Viper.Environment.SetVariable(name, "abc")
PRINT "Updated value: "; Viper.Environment.GetVariable(name)

' Process exit
' Viper.Environment.EndProgram(7)
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

## Viper.Time.Clock

Basic timing utilities for sleeping and measuring elapsed time.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Sleep(ms)` | `Void(Integer)` | Pause execution for the specified number of milliseconds |
| `Ticks()` | `Integer()` | Returns monotonic time in milliseconds since an unspecified epoch |
| `TicksUs()` | `Integer()` | Returns monotonic time in microseconds since an unspecified epoch |

### Notes

- `Ticks()` and `TicksUs()` return monotonic, non-decreasing values suitable for measuring elapsed time
- The epoch (starting point) is unspecified - only use these functions for measuring durations, not absolute time
- `TicksUs()` provides microsecond resolution for high-precision timing
- `Sleep(0)` returns immediately without sleeping
- Negative values passed to `Sleep()` are treated as 0

### Example

```basic
' Measure execution time
DIM startMs AS INTEGER = Viper.Time.Clock.Ticks()

' Do some work...
FOR i = 1 TO 1000000
    ' busy loop
NEXT

DIM endMs AS INTEGER = Viper.Time.Clock.Ticks()
PRINT "Elapsed time: "; endMs - startMs; " ms"

' High-precision timing with microseconds
DIM startUs AS INTEGER = Viper.Time.Clock.TicksUs()
' ... fast operation ...
DIM endUs AS INTEGER = Viper.Time.Clock.TicksUs()
PRINT "Elapsed: "; endUs - startUs; " microseconds"

' Sleep for a short delay
Viper.Time.Clock.Sleep(100)  ' Sleep for 100ms
```

---

## Viper.Diagnostics.Assert

Runtime assertion helper that terminates execution when a required condition is
not satisfied.

**Type:** Static function

### Signature

| Function | Signature | Description |
|----------|-----------|-------------|
| `Viper.Diagnostics.Assert` | `Void(Boolean, String)` | Traps when @p condition is false; uses default message when empty |

### Notes

- When `condition` is zero/false, the runtime traps (prints to stderr and exits with status 1).
- An empty string or `""` message is replaced with `"Assertion failed"` to keep diagnostics informative.

### Example

```basic
DIM count AS INTEGER
count = 3

' Passes: execution continues
Viper.Diagnostics.Assert(count > 0, "count must be positive")

' Fails: terminates with the provided message
Viper.Diagnostics.Assert(count < 0, "boom")
```

---

## Viper.Diagnostics.Stopwatch

High-precision stopwatch for benchmarking and performance measurement. Supports pause/resume timing with nanosecond resolution.

**Type:** Instance class (requires `New()` or `StartNew()`)

### Constructors

| Method | Signature | Description |
|--------|-----------|-------------|
| `New()` | `Stopwatch()` | Create a new stopped stopwatch |
| `StartNew()` | `Stopwatch()` | Create and immediately start a new stopwatch |

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `ElapsedMs` | `Integer` (read-only) | Total elapsed time in milliseconds |
| `ElapsedUs` | `Integer` (read-only) | Total elapsed time in microseconds |
| `ElapsedNs` | `Integer` (read-only) | Total elapsed time in nanoseconds |
| `IsRunning` | `Boolean` (read-only) | True if stopwatch is currently running |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Start()` | `Void()` | Start or resume timing (no effect if already running) |
| `Stop()` | `Void()` | Pause timing, preserving accumulated time |
| `Reset()` | `Void()` | Stop and clear all accumulated time |
| `Restart()` | `Void()` | Reset and start in one atomic operation |

### Notes

- Stopwatch provides nanosecond resolution on supported platforms
- Time accumulates across multiple Start/Stop cycles until Reset
- Reading `ElapsedMs`/`ElapsedUs`/`ElapsedNs` while running returns current elapsed time
- `Start()` has no effect if already running (doesn't reset)
- `Stop()` has no effect if already stopped

### Example

```basic
' Create and start a stopwatch
DIM sw AS OBJECT = Viper.Diagnostics.Stopwatch.StartNew()

' Code to benchmark
FOR i = 1 TO 1000000
    DIM x AS INTEGER = i * i
NEXT

sw.Stop()
PRINT "Elapsed: "; sw.ElapsedMs; " ms"
PRINT "Elapsed: "; sw.ElapsedUs; " us"
PRINT "Elapsed: "; sw.ElapsedNs; " ns"

' Resume timing for additional work
sw.Start()
FOR i = 1 TO 500000
    DIM x AS INTEGER = i * i
NEXT
sw.Stop()
PRINT "Total: "; sw.ElapsedMs; " ms"

' Reset and restart
sw.Restart()
' More benchmarking...
sw.Stop()
PRINT "New timing: "; sw.ElapsedMs; " ms"
```

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
| `Copy(src, dst)` | `Void(String, String)` | Copies a file from src to dst |
| `Move(src, dst)` | `Void(String, String)` | Moves/renames a file from src to dst |
| `Size(path)` | `Integer(String)` | Returns file size in bytes, or -1 if not found |
| `ReadBytes(path)` | `Bytes(String)` | Reads the entire file as binary data |
| `WriteBytes(path, bytes)` | `Void(String, Bytes)` | Writes binary data to a file |
| `ReadAllBytes(path)` | `Bytes(String)` | Reads the entire file as binary data (traps on I/O errors) |
| `WriteAllBytes(path, bytes)` | `Void(String, Bytes)` | Writes binary data to a file (overwrites; traps on I/O errors) |
| `ReadLines(path)` | `Seq(String)` | Reads the file as a sequence of lines |
| `WriteLines(path, lines)` | `Void(String, Seq)` | Writes a sequence of strings as lines |
| `Append(path, text)` | `Void(String, String)` | Appends text to a file |
| `AppendLine(path, text)` | `Void(String, String)` | Appends text followed by `\n` to a file (creates if missing) |
| `ReadAllLines(path)` | `Seq(String)` | Reads file as a sequence of lines; strips `\n` / `\r\n` terminators (traps on I/O errors) |
| `Modified(path)` | `Integer(String)` | Returns file modification time as Unix timestamp |
| `Touch(path)` | `Void(String)` | Creates file or updates its modification time |

### Notes

- `AppendLine` always appends a single `\n` byte (no platform newline normalization).
- `ReadAllLines` splits on `\n` and `\r\n` and does not include line endings in returned strings; a trailing line ending does not add an extra empty final line.
- `ReadAllBytes`, `WriteAllBytes`, and `ReadAllLines` trap (write a diagnostic to stderr and terminate) on I/O errors.

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

### Binary File Example

```basic
' Create binary data
DIM data AS Bytes
data = Viper.Collections.Bytes.New(4)
data.Set(0, &H48)  ' H
data.Set(1, &H69)  ' i
data.Set(2, &H21)  ' !
data.Set(3, &H00)  ' null byte

' Write binary file
Viper.IO.File.WriteAllBytes("test.bin", data)

' Read binary file
DIM loaded AS Bytes
loaded = Viper.IO.File.ReadAllBytes("test.bin")
PRINT "Size:"; loaded.Len()  ' Output: Size: 4
```

### Line-by-Line Example

```basic
' Write lines to file
DIM lines AS Seq
lines = Viper.Collections.Seq.New()
lines.Push("First line")
lines.Push("Second line")
lines.Push("Third line")
Viper.IO.File.WriteLines("output.txt", lines)

' Read lines from file
DIM readLines AS Seq
readLines = Viper.IO.File.ReadAllLines("output.txt")
FOR i = 0 TO readLines.Len() - 1
    PRINT readLines.Get(i)
NEXT i

' Append to file
Viper.IO.File.Append("output.txt", "Appended text")
```

### File Management Example

```basic
' Copy file
Viper.IO.File.Copy("source.txt", "backup.txt")

' Move/rename file
Viper.IO.File.Move("old_name.txt", "new_name.txt")

' Get file info
DIM size AS INTEGER
size = Viper.IO.File.Size("data.txt")
PRINT "File size:"; size; "bytes"

DIM mtime AS INTEGER
mtime = Viper.IO.File.Modified("data.txt")
PRINT "Modified:"; mtime

' Create empty file or update timestamp
Viper.IO.File.Touch("marker.txt")
```

---

## Viper.IO.Path

Cross-platform path manipulation utilities. All functions work with both Unix (`/`) and Windows (`\`) path separators.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Join(a, b)` | `String(String, String)` | Joins two path components with the platform separator |
| `Dir(path)` | `String(String)` | Returns the directory portion of a path |
| `Name(path)` | `String(String)` | Returns the filename portion of a path |
| `Stem(path)` | `String(String)` | Returns the filename without extension |
| `Ext(path)` | `String(String)` | Returns the file extension (including the dot) |
| `WithExt(path, ext)` | `String(String, String)` | Replaces the extension of a path |
| `IsAbs(path)` | `Boolean(String)` | Returns true if the path is absolute |
| `Abs(path)` | `String(String)` | Converts a relative path to absolute |
| `Norm(path)` | `String(String)` | Normalizes a path (removes `.`, `..`, duplicate separators) |
| `Sep()` | `String()` | Returns the platform-specific path separator |

### Example

```basic
DIM path AS STRING
path = "/home/user/documents/report.txt"

' Extract path components
PRINT Viper.IO.Path.Dir(path)   ' Output: "/home/user/documents"
PRINT Viper.IO.Path.Name(path)  ' Output: "report.txt"
PRINT Viper.IO.Path.Stem(path)  ' Output: "report"
PRINT Viper.IO.Path.Ext(path)   ' Output: ".txt"

' Join paths
DIM newPath AS STRING
newPath = Viper.IO.Path.Join("/home/user", "downloads")
PRINT newPath  ' Output: "/home/user/downloads"

' Replace extension
DIM mdPath AS STRING
mdPath = Viper.IO.Path.WithExt(path, ".md")
PRINT mdPath  ' Output: "/home/user/documents/report.md"

' Check if absolute
PRINT Viper.IO.Path.IsAbs(path)      ' Output: true
PRINT Viper.IO.Path.IsAbs("foo/bar") ' Output: false

' Normalize paths
PRINT Viper.IO.Path.Norm("/foo//bar/../baz")  ' Output: "/foo/baz"
PRINT Viper.IO.Path.Norm("./a/b/../c")        ' Output: "a/c"

' Get platform separator
PRINT Viper.IO.Path.Sep()  ' Output: "/" on Unix, "\" on Windows
```

### Path Normalization

The `Norm()` function performs the following transformations:
- Removes redundant separators (`//` becomes `/`)
- Resolves `.` components (current directory)
- Resolves `..` components (parent directory) where possible
- Returns `.` for an empty result
- Preserves leading `..` in relative paths

### Platform Differences

| Behavior | Unix | Windows |
|----------|------|---------|
| Path separator | `/` | `\` |
| Absolute path detection | Starts with `/` | Starts with `C:\` or `\\` |
| Example absolute path | `/home/user` | `C:\Users\user` |

### Use Cases

- **Building file paths:** Use `Join()` to create paths safely
- **Extracting components:** Use `Dir()`, `Name()`, `Stem()`, `Ext()` to parse paths
- **Changing extensions:** Use `WithExt()` to replace file extensions
- **Cleaning paths:** Use `Norm()` to clean up user-provided paths
- **Portable code:** Use `Sep()` for platform-specific separators

---

## Viper.IO.Dir

Cross-platform directory operations for creating, removing, listing, and navigating directories.

**Type:** Static utility class

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `Exists(path)` | `Boolean(String)` | Returns true if the directory exists |
| `Make(path)` | `Void(String)` | Creates a single directory (parent must exist) |
| `MakeAll(path)` | `Void(String)` | Creates a directory and all parent directories |
| `Remove(path)` | `Void(String)` | Removes an empty directory |
| `RemoveAll(path)` | `Void(String)` | Recursively removes a directory and all its contents |
| `List(path)` | `Seq(String)` | Returns all entries in a directory (excluding `.` and `..`) |
| `Files(path)` | `Seq(String)` | Returns only files in a directory (no subdirectories) |
| `Dirs(path)` | `Seq(String)` | Returns only subdirectories in a directory |
| `Current()` | `String()` | Returns the current working directory |
| `SetCurrent(path)` | `Void(String)` | Changes the current working directory |
| `Move(src, dst)` | `Void(String, String)` | Moves/renames a directory |

### Example

```basic
' Check if a directory exists
IF Viper.IO.Dir.Exists("/home/user/documents") THEN
    PRINT "Documents folder exists"
END IF

' Create a new directory
Viper.IO.Dir.Make("/home/user/newdir")

' Create nested directories (like mkdir -p)
Viper.IO.Dir.MakeAll("/home/user/a/b/c/d")

' List all entries in a directory
DIM entries AS Viper.Collections.Seq
entries = Viper.IO.Dir.List("/home/user")
FOR i = 0 TO entries.Len - 1
    PRINT entries.Get(i)
NEXT i

' List only files (no subdirectories)
DIM files AS Viper.Collections.Seq
files = Viper.IO.Dir.Files("/home/user")

' List only subdirectories
DIM subdirs AS Viper.Collections.Seq
subdirs = Viper.IO.Dir.Dirs("/home/user")

' Get and change current working directory
DIM cwd AS STRING
cwd = Viper.IO.Dir.Current()
PRINT "Current directory: "; cwd

Viper.IO.Dir.SetCurrent("/home/user/projects")
PRINT "New directory: "; Viper.IO.Dir.Current()

' Restore original directory
Viper.IO.Dir.SetCurrent(cwd)

' Move/rename a directory
Viper.IO.Dir.Move("/home/user/oldname", "/home/user/newname")

' Remove an empty directory
Viper.IO.Dir.Remove("/home/user/emptydir")

' Recursively remove a directory and all its contents
' WARNING: This permanently deletes files!
Viper.IO.Dir.RemoveAll("/home/user/tempdir")
```

### Error Handling

Directory operations trap on errors:
- `Make()` traps if the parent directory doesn't exist or creation fails
- `Remove()` traps if the directory is not empty or doesn't exist
- `RemoveAll()` silently ignores non-existent directories
- `SetCurrent()` traps if the directory doesn't exist

Use `Exists()` to check before performing operations that may fail.

### Listing Functions

The three listing functions return `Seq` objects containing entry names (not full paths):

| Function | Returns | Includes |
|----------|---------|----------|
| `List(path)` | All entries | Files and subdirectories |
| `Files(path)` | Files only | Regular files, no directories |
| `Dirs(path)` | Directories only | Subdirectories, no files |

All listing functions exclude `.` and `..` entries. If the directory doesn't exist or can't be read, an empty sequence is returned.

### Use Cases

- **File management:** List, copy, move, and delete directories
- **Build systems:** Create output directories with `MakeAll()`
- **Cleanup:** Remove temporary directories with `RemoveAll()`
- **Navigation:** Get and set the working directory
- **Filtering:** Separate files from subdirectories with `Files()` and `Dirs()`

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

## Runtime Architecture

### Overview

The Viper runtime is defined in `src/il/runtime/runtime.def` using X-macros. This single source of truth generates:

- `RuntimeNameMap.inc` — Maps canonical names to C symbols
- `RuntimeClasses.inc` — OOP class catalog for the type system

### RT_FUNC Syntax

```
RT_FUNC(id, c_symbol, "canonical_name", "signature")
```

- **id**: Unique C++ identifier used in generated code
- **c_symbol**: The C function name (rt_* prefix by convention)
- **canonical_name**: The Viper namespace path (e.g., "Viper.Math.Sin")
- **signature**: IL type signature using type abbreviations

### RT_CLASS Syntax

```
RT_CLASS_BEGIN("canonical_name", type_id, "layout", ctor_id)
    RT_PROP("name", "type", getter_id, setter_id_or_none)
    RT_METHOD("name", "signature", target_id)
RT_CLASS_END()
```

Classes define the OOP interface exposed to Viper languages. Method signatures omit the receiver (arg0).

### Type Abbreviations

| Abbrev | Type | Size |
|--------|------|------|
| `void` | No value | 0 |
| `i1` | Boolean | 1 bit |
| `i8` | Signed byte | 8 bits |
| `i16` | Short integer | 16 bits |
| `i32` | Integer | 32 bits |
| `i64` | Long integer | 64 bits |
| `f32` | Single float | 32 bits |
| `f64` | Double float | 64 bits |
| `str` | String | pointer |
| `obj` | Object | pointer |
| `ptr` | Raw pointer | pointer |

### Quick Reference

| Class | Description |
|-------|-------------|
| `Viper.Object` | Base class with Equals, GetHashCode, ToString |
| `Viper.String` | String manipulation (Substring, Trim, Replace, etc.) |
| `Viper.Strings` | Static string utilities (Join, FromInt, etc.) |
| `Viper.Math` | Math functions (Sin, Cos, Sqrt, etc.) and constants (Pi, E) |
| `Viper.Terminal` | Terminal I/O (Say, Print, Ask, ReadLine) |
| `Viper.Convert` | Type conversion (ToInt, ToDouble) |
| `Viper.Environment` | Command-line args, environment variables, process exit |
| `Viper.Random` | Random number generation |
| `Viper.Collections.Seq` | Dynamic array with Push, Pop, Get, Set |
| `Viper.Collections.Stack` | LIFO with Push, Pop, Peek |
| `Viper.Collections.Queue` | FIFO with Add, Take, Peek |
| `Viper.Collections.Map` | String-keyed dictionary |
| `Viper.Collections.Bytes` | Efficient byte array |
| `Viper.Collections.List` | Dynamic list of objects |
| `Viper.IO.File` | File read/write/copy/delete |
| `Viper.IO.Dir` | Directory create/list/delete |
| `Viper.IO.Path` | Path join/split/normalize |
| `Viper.Text.StringBuilder` | Efficient string concatenation |
| `Viper.Text.Guid` | UUID v4 generation |
| `Viper.Graphics.Canvas` | Window and 2D drawing |
| `Viper.Graphics.Color` | RGB/RGBA color creation |
| `Viper.Time.Clock` | Sleep and tick counting |
| `Viper.DateTime` | Date/time creation and formatting |
| `Viper.Diagnostics.Stopwatch` | Benchmarking timer |

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
