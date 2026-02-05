# Collections

> Data structures for storing and organizing data.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Collections.Bag](#vipercollectionsbag)
- [Viper.Collections.Bytes](#vipercollectionsbytes)
- [Viper.Collections.Deque](#vipercollectionsdeque)
- [Viper.Collections.Heap](#vipercollectionsheap)
- [Viper.Collections.LazySeq](#vipercollectionslazyseq)
- [Viper.Collections.List](#vipercollectionslist)
- [Viper.Collections.Map](#vipercollectionsmap)
- [Viper.Collections.Queue](#vipercollectionsqueue)
- [Viper.Collections.Ring](#vipercollectionsring)
- [Viper.Collections.Seq](#vipercollectionsseq)
- [Viper.Collections.Set](#vipercollectionsset)
- [Viper.Collections.SortedSet](#vipercollectionssortedset)
- [Viper.Collections.Stack](#vipercollectionsstack)
- [Viper.Collections.TreeMap](#vipercollectionstreemap)
- [Viper.Collections.WeakMap](#vipercollectionsweakmap)

---

## Viper.Collections.Bag

A set data structure for storing unique strings. Efficiently handles membership testing, set operations (union,
intersection, difference), and enumeration.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Bag()`

### Properties

| Property  | Type    | Description                     |
|-----------|---------|---------------------------------|
| `Len`     | Integer | Number of strings in the bag    |
| `IsEmpty` | Boolean | True if bag contains no strings |

### Methods

| Method          | Signature         | Description                                                  |
|-----------------|-------------------|--------------------------------------------------------------|
| `Put(str)`      | `Boolean(String)` | Add a string; returns true if new, false if already present  |
| `Drop(str)`     | `Boolean(String)` | Remove a string; returns true if removed, false if not found |
| `Has(str)`      | `Boolean(String)` | Check if string is in the bag                                |
| `Clear()`       | `Void()`          | Remove all strings from the bag                              |
| `Items()`       | `Seq()`           | Get all strings as a Seq (order undefined)                   |
| `Merge(other)`  | `Bag(Bag)`        | Return new bag with union of both bags                       |
| `Common(other)` | `Bag(Bag)`        | Return new bag with intersection of both bags                |
| `Diff(other)`   | `Bag(Bag)`        | Return new bag with elements in this but not other           |

### Notes

- Strings are stored by value (copied into the bag)
- Order of strings returned by `Items()` is not guaranteed (hash table)
- Set operations (`Merge`, `Common`, `Diff`) return new bags; originals are unchanged
- Uses FNV-1a hash function for O(1) average-case operations
- Automatically resizes when load factor exceeds 75%

### Example

```basic
' Create and populate a bag
DIM fruits AS OBJECT = NEW Viper.Collections.Bag()
fruits.Put("apple")
fruits.Put("banana")
fruits.Put("cherry")
PRINT fruits.Len           ' Output: 3

' Duplicate add returns false
DIM wasNew AS INTEGER = fruits.Put("apple")
PRINT wasNew               ' Output: 0 (already present)

' Membership testing
PRINT fruits.Has("banana") ' Output: 1 (true)
PRINT fruits.Has("grape")  ' Output: 0 (false)

' Remove an element
DIM removed AS INTEGER = fruits.Drop("banana")
PRINT removed              ' Output: 1 (was removed)
PRINT fruits.Has("banana") ' Output: 0 (no longer present)

' Set operations
DIM bagA AS OBJECT = NEW Viper.Collections.Bag()
bagA.Put("a")
bagA.Put("b")
bagA.Put("c")

DIM bagB AS OBJECT = NEW Viper.Collections.Bag()
bagB.Put("b")
bagB.Put("c")
bagB.Put("d")

' Union: elements in either bag
DIM merged AS OBJECT = bagA.Merge(bagB)
PRINT merged.Len           ' Output: 4 (a, b, c, d)

' Intersection: elements in both bags
DIM common AS OBJECT = bagA.Common(bagB)
PRINT common.Len           ' Output: 2 (b, c)

' Difference: elements in A but not B
DIM diff AS OBJECT = bagA.Diff(bagB)
PRINT diff.Len             ' Output: 1 (a only)

' Enumerate all elements
DIM items AS OBJECT = fruits.Items()
FOR i AS INTEGER = 0 TO items.Len - 1
    PRINT items.Get(i)
NEXT
```

### Use Cases

- **Deduplication:** Track unique values encountered
- **Membership testing:** Fast O(1) lookup for string membership
- **Set mathematics:** Compute unions, intersections, and differences
- **Tag systems:** Manage collections of unique tags or labels
- **Visited tracking:** Track visited items in algorithms

---

## Viper.Collections.Bytes

An efficient byte array for binary data. More memory-efficient than Seq for byte manipulation.

**Type:** Instance (obj)
**Constructors:**

- `NEW Viper.Collections.Bytes(length)` - Create zero-filled byte array
- `Viper.Collections.Bytes.FromStr(str)` - Create from string (UTF-8 bytes)
- `Viper.Collections.Bytes.FromHex(hex)` - Create from hexadecimal string
- `Viper.Collections.Bytes.FromBase64(b64)` - Decode RFC 4648 Base64 string (traps on invalid input)

### Properties

| Property | Type    | Description     |
|----------|---------|-----------------|
| `Len`    | Integer | Number of bytes |

### Methods

| Method                                   | Signature                 | Description                                                           |
|------------------------------------------|---------------------------|-----------------------------------------------------------------------|
| `Get(index)`                             | `Integer(Integer)`        | Get byte value (0-255) at index                                       |
| `Set(index, value)`                      | `Void(Integer, Integer)`  | Set byte value at index (clamped to 0-255)                            |
| `Slice(start, end)`                      | `Bytes(Integer, Integer)` | Create new byte array from range [start, end)                         |
| `Copy(dstOffset, src, srcOffset, count)` | `Void(...)`               | Copy bytes between arrays                                             |
| `ToStr()`                                | `String()`                | Convert to string (interprets as UTF-8)                               |
| `ToHex()`                                | `String()`                | Convert to lowercase hexadecimal string                               |
| `ToBase64()`                             | `String()`                | Convert to RFC 4648 Base64 string (A-Z a-z 0-9 + /, with '=' padding) |
| `Fill(value)`                            | `Void(Integer)`           | Set all bytes to value                                                |
| `Find(value)`                            | `Integer(Integer)`        | Find first occurrence (-1 if not found)                               |
| `Clone()`                                | `Bytes()`                 | Create independent copy                                               |

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

' Base64 encode/decode (RFC 4648)
PRINT text.ToBase64()  ' Output: "SGVsbG8="
DIM decoded AS Viper.Collections.Bytes
decoded = Viper.Collections.Bytes.FromBase64("SGVsbG8=")
PRINT decoded.ToStr()  ' Output: "Hello"

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

## Viper.Collections.Deque

A double-ended queue (deque) that supports efficient insertion and removal at both ends. Combines the capabilities of
stacks and queues while also supporting indexed access.

**Type:** Instance (obj)
**Constructors:**

- `NEW Viper.Collections.Deque()` - Create with default capacity
- `Viper.Collections.Deque.WithCapacity(cap)` - Create with specified initial capacity

### Properties

| Property  | Type    | Description                               |
|-----------|---------|-------------------------------------------|
| `Len`     | Integer | Number of elements in the deque           |
| `Cap`     | Integer | Current allocated capacity                |
| `IsEmpty` | Boolean | Returns true if the deque has no elements |

### Methods

| Method               | Signature               | Description                                           |
|----------------------|-------------------------|-------------------------------------------------------|
| `PushFront(value)`   | `Void(Object)`          | Add element to front of deque                         |
| `PushBack(value)`    | `Void(Object)`          | Add element to back of deque                          |
| `PopFront()`         | `Object()`              | Remove and return front element (traps if empty)      |
| `PopBack()`          | `Object()`              | Remove and return back element (traps if empty)       |
| `PeekFront()`        | `Object()`              | Return front element without removing (traps if empty)|
| `PeekBack()`         | `Object()`              | Return back element without removing (traps if empty) |
| `Get(index)`         | `Object(Integer)`       | Get element at index (0 = front)                      |
| `Set(index, value)`  | `Void(Integer, Object)` | Set element at index                                  |
| `Has(value)`         | `Boolean(Object)`       | Check if element exists (pointer equality)            |
| `Clear()`            | `Void()`                | Remove all elements                                   |
| `Reverse()`          | `Void()`                | Reverse elements in place                             |
| `Clone()`            | `Deque()`               | Create shallow copy                                   |

### Example

```basic
DIM deque AS Viper.Collections.Deque
deque = NEW Viper.Collections.Deque()

' Add elements to both ends
deque.PushBack("middle")
deque.PushFront("front")
deque.PushBack("back")

PRINT deque.Len          ' Output: 3

' Access by index (0 = front)
PRINT deque.Get(0)       ' Output: "front"
PRINT deque.Get(1)       ' Output: "middle"
PRINT deque.Get(2)       ' Output: "back"

' Peek without removing
PRINT deque.PeekFront()  ' Output: "front"
PRINT deque.PeekBack()   ' Output: "back"

' Pop from either end
PRINT deque.PopFront()   ' Output: "front"
PRINT deque.PopBack()    ' Output: "back"
PRINT deque.Len          ' Output: 1

' Use as stack (LIFO)
deque.PushBack("a")
deque.PushBack("b")
deque.PushBack("c")
PRINT deque.PopBack()    ' Output: "c" (LIFO)

' Use as queue (FIFO)
deque.Clear()
deque.PushBack("first")
deque.PushBack("second")
PRINT deque.PopFront()   ' Output: "first" (FIFO)

' Reverse in place
deque.Clear()
deque.PushBack("a")
deque.PushBack("b")
deque.PushBack("c")
deque.Reverse()
PRINT deque.Get(0)       ' Output: "c" (was last)
```

### Deque vs Queue vs Stack

| Feature          | Deque          | Queue         | Stack         |
|------------------|----------------|---------------|---------------|
| Add front        | O(1)           | No            | No            |
| Add back         | O(1)           | O(1)          | O(1)          |
| Remove front     | O(1)           | O(1)          | No            |
| Remove back      | O(1)           | No            | O(1)          |
| Random access    | O(1)           | No            | No            |
| Reverse          | O(n)           | No            | No            |

### Use Cases

- **Sliding window:** Process data with access to both newest and oldest elements
- **Undo/Redo:** Push operations to back, pop from back for undo, front for redo
- **Work stealing:** Thread-safe deques enable work stealing for load balancing
- **Palindrome checking:** Compare elements from both ends
- **Browser history:** Navigate forward and backward through pages

---

## Viper.Collections.Heap

A priority queue implemented as a binary heap. Elements are stored with an integer priority value and retrieved in priority order. Supports both min-heap (smallest priority first) and max-heap (largest priority first) modes.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Heap()` (min-heap) or `Heap.NewMax(isMax)` for max-heap

### Properties

| Property  | Type    | Description                                    |
|-----------|---------|------------------------------------------------|
| `Len`     | Integer | Number of elements in the heap                 |
| `IsEmpty` | Boolean | Returns true if the heap has no elements       |
| `IsMax`   | Boolean | Returns true if max-heap, false if min-heap    |

### Methods

| Method               | Signature              | Description                                                |
|----------------------|------------------------|------------------------------------------------------------|
| `Push(priority,val)` | `Void(Integer,Object)` | Add element with priority (lower = higher priority in min-heap) |
| `Pop()`              | `Object()`             | Remove and return highest priority element (traps if empty) |
| `Peek()`             | `Object()`             | Return highest priority element without removing (traps if empty) |
| `TryPop()`           | `Object()`             | Remove and return highest priority element, or null if empty |
| `TryPeek()`          | `Object()`             | Return highest priority element, or null if empty          |
| `Clear()`            | `Void()`               | Remove all elements                                        |
| `ToSeq()`            | `Seq()`                | Return elements in priority order as a Seq                 |

### Example

```basic
DIM heap AS Viper.Collections.Heap
heap = NEW Viper.Collections.Heap()  ' Create min-heap

' Add tasks with priorities (lower = more urgent)
heap.Push(3, "Low priority task")
heap.Push(1, "Urgent task")
heap.Push(2, "Medium priority task")

PRINT heap.Len       ' Output: 3

' Pop returns elements in priority order (lowest priority value first)
PRINT heap.Pop()     ' Output: "Urgent task" (priority 1)
PRINT heap.Pop()     ' Output: "Medium priority task" (priority 2)
PRINT heap.Peek()    ' Output: "Low priority task" (priority 3, still in heap)

' Max-heap example
DIM maxHeap AS Viper.Collections.Heap
maxHeap = Viper.Collections.Heap.NewMax(True)

maxHeap.Push(1, "Low")
maxHeap.Push(5, "High")
maxHeap.Push(3, "Medium")

PRINT maxHeap.Pop()  ' Output: "High" (priority 5 - highest)
```

### Use Cases

- **Task scheduling:** Process tasks by priority
- **Event-driven simulation:** Handle events in time order
- **Dijkstra's algorithm:** Find shortest paths
- **Huffman coding:** Build optimal prefix codes
- **Median finding:** Use two heaps (min and max)

### Errors (Traps)

- `Heap.Pop: heap is empty` - Called Pop on empty heap
- `Heap.Peek: heap is empty` - Called Peek on empty heap
- `Heap.Push: null heap` - Called Push on null reference

---

## Viper.Collections.LazySeq

A lazy sequence that generates elements on demand rather than storing them all in memory. Useful for infinite sequences,
computed sequences, and memory-efficient processing of large datasets. Supports functional-style transformations and
collectors.

**Type:** Instance (obj)
**Constructors:**

- `Viper.Collections.LazySeq.Range(start, end, step)` - Generate integer range
- `Viper.Collections.LazySeq.Repeat(value, count)` - Repeat value count times (-1 for infinite)
- `Viper.Collections.LazySeq.Iterate(seed, fn)` - Generate by repeatedly applying function

### Properties

| Property      | Type    | Description                                        |
|---------------|---------|----------------------------------------------------|
| `Index`       | Integer | Current position (number of elements consumed)     |
| `IsExhausted` | Boolean | True if sequence has no more elements              |

### Methods

#### Element Access

| Method    | Signature       | Description                                             |
|-----------|-----------------|---------------------------------------------------------|
| `Next()`  | `Object()`      | Get next element and advance; returns null if exhausted |
| `Peek()`  | `Object()`      | Get next element without advancing                      |
| `Reset()` | `Void()`        | Reset sequence to beginning                             |

#### Transformations (return new LazySeq)

| Method             | Signature                    | Description                                      |
|--------------------|------------------------------|--------------------------------------------------|
| `Map(fn)`          | `LazySeq(Function)`          | Transform each element with function             |
| `Filter(pred)`     | `LazySeq(Function)`          | Keep only elements where predicate returns true  |
| `Take(n)`          | `LazySeq(Integer)`           | Take first n elements only                       |
| `Drop(n)`          | `LazySeq(Integer)`           | Skip first n elements                            |
| `TakeWhile(pred)`  | `LazySeq(Function)`          | Take elements while predicate is true            |
| `DropWhile(pred)`  | `LazySeq(Function)`          | Skip elements while predicate is true            |
| `Concat(other)`    | `LazySeq(LazySeq)`           | Concatenate with another lazy sequence           |
| `Zip(other, fn)`   | `LazySeq(LazySeq, Function)` | Zip with another sequence using combiner         |

#### Collectors (consume sequence)

| Method         | Signature                    | Description                                            |
|----------------|------------------------------|--------------------------------------------------------|
| `ToSeq()`      | `Seq()`                      | Collect all elements into a Seq (may not terminate!)   |
| `ToSeqN(n)`    | `Seq(Integer)`               | Collect at most n elements into a Seq                  |
| `Fold(init,fn)`| `Object(Object, Function)`   | Reduce to single value (may not terminate!)            |
| `Count()`      | `Integer()`                  | Count all elements (may not terminate!)                |
| `ForEach(fn)`  | `Void(Function)`             | Execute function for each element                      |
| `Find(pred)`   | `Object(Function)`           | Find first matching element; null if not found         |
| `Any(pred)`    | `Boolean(Function)`          | True if any element matches                            |
| `All(pred)`    | `Boolean(Function)`          | True if all elements match (may not terminate!)        |

### Example

```basic
' Create a range (like Python's range)
DIM nums AS OBJECT = Viper.Collections.LazySeq.Range(1, 11, 1)
' Generates: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10

' Manual iteration
WHILE NOT nums.IsExhausted
    DIM n AS OBJECT = nums.Next()
    PRINT n
WEND

' Create and transform lazily
DIM evens AS OBJECT = Viper.Collections.LazySeq.Range(1, 100, 1) _
    .Filter(FUNCTION(n) RETURN n MOD 2 = 0) _
    .Take(5)
' Generates: 2, 4, 6, 8, 10 (only computes what's needed)

' Collect into a Seq
DIM evenSeq AS OBJECT = evens.ToSeq()
PRINT evenSeq.Len    ' Output: 5

' Infinite sequence with Take
DIM ones AS OBJECT = Viper.Collections.LazySeq.Repeat(1, -1)  ' Infinite 1s
DIM firstTen AS OBJECT = ones.Take(10).ToSeq()
PRINT firstTen.Len   ' Output: 10

' Fibonacci using Iterate
DIM fib AS OBJECT = Viper.Collections.LazySeq.Iterate( _
    NEW Pair(0, 1), _
    FUNCTION(p) RETURN NEW Pair(p.Second, p.First + p.Second) _
).Map(FUNCTION(p) RETURN p.First).Take(10)

' Find first element matching condition
DIM firstBig AS OBJECT = Viper.Collections.LazySeq.Range(1, 1000000, 1) _
    .Find(FUNCTION(n) RETURN n > 500)
PRINT firstBig       ' Output: 501 (stops immediately, doesn't check all million)

' Fold/reduce
DIM sum AS INTEGER = Viper.Collections.LazySeq.Range(1, 11, 1) _
    .Fold(0, FUNCTION(acc, n) RETURN acc + n)
PRINT sum            ' Output: 55

' Check predicates
DIM allPositive AS INTEGER = Viper.Collections.LazySeq.Range(1, 100, 1) _
    .All(FUNCTION(n) RETURN n > 0)
PRINT allPositive    ' Output: 1 (true)

' Peek without consuming
DIM seq AS OBJECT = Viper.Collections.LazySeq.Range(1, 5, 1)
PRINT seq.Peek()     ' Output: 1
PRINT seq.Peek()     ' Output: 1 (still 1)
PRINT seq.Next()     ' Output: 1 (now consumed)
PRINT seq.Peek()     ' Output: 2

' Reset to beginning
seq.Reset()
PRINT seq.Next()     ' Output: 1 (back to start)

' Zip two sequences
DIM letters AS OBJECT = Viper.Collections.LazySeq.Repeat("a", 3)
DIM numbers AS OBJECT = Viper.Collections.LazySeq.Range(1, 4, 1)
DIM pairs AS OBJECT = letters.Zip(numbers, FUNCTION(a, b) RETURN a + STR(b))
' Generates: "a1", "a2", "a3"
```

### LazySeq vs Seq

| Feature               | LazySeq              | Seq                  |
|-----------------------|----------------------|----------------------|
| Memory                | O(1)                 | O(n)                 |
| Element generation    | On demand            | Upfront              |
| Infinite sequences    | Supported            | Not possible         |
| Random access         | No (sequential only) | O(1)                 |
| Multiple iterations   | Requires Reset()     | Automatic            |
| Chain transformations | Deferred             | Immediate            |

### Important Notes

- **Infinite sequences:** Methods like `ToSeq()`, `Count()`, `Fold()`, and `All()` may never
  terminate on infinite sequences. Always use `Take(n)` to bound infinite sequences before
  collecting.
- **Single-pass:** LazySeq is consumed as you iterate. Use `Reset()` to iterate again, or
  `ToSeq()` to materialize into a reusable collection.
- **Transformation chaining:** Transformations like `Map()`, `Filter()`, `Take()` return new
  LazySeq instances and do no work until elements are requested.

### Use Cases

- **Large datasets:** Process files or data too large to fit in memory
- **Infinite sequences:** Mathematical sequences like Fibonacci, primes
- **Early termination:** Find first match without computing everything
- **Pipeline processing:** Chain transformations efficiently
- **Generator patterns:** Produce values on demand

---

## Viper.Collections.List

Dynamic array that grows automatically. Stores object references.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.List()`

### Properties

| Property | Type    | Description                 |
|----------|---------|-----------------------------|
| `Count`  | Integer | Number of items in the list |

### Methods

| Method                   | Signature               | Description                                                                             |
|--------------------------|-------------------------|-----------------------------------------------------------------------------------------|
| `Add(item)`              | `Void(Object)`          | Appends an item to the end of the list                                                  |
| `Clear()`                | `Void()`                | Removes all items from the list                                                         |
| `Has(item)`              | `Boolean(Object)`       | Returns true if the list contains the object (reference equality)                       |
| `Find(item)`             | `Integer(Object)`       | Returns index of the first matching object, or `-1` if not found                        |
| `Insert(index, item)`    | `Void(Integer, Object)` | Inserts the item at `index` (0..Count); `index == Count` appends; traps if out of range |
| `Remove(item)`           | `Boolean(Object)`       | Removes the first matching object (reference equality); returns true if removed         |
| `RemoveAt(index)`        | `Void(Integer)`         | Removes the item at the specified index                                                 |
| `get_Item(index)`        | `Object(Integer)`       | Gets the item at the specified index                                                    |
| `set_Item(index, value)` | `Void(Integer, Object)` | Sets the item at the specified index                                                    |

### Example

```basic
DIM list AS Viper.Collections.List
list = NEW Viper.Collections.List()

' Add items
DIM a AS Object = NEW Viper.Collections.List()
DIM b AS Object = NEW Viper.Collections.List()
DIM c AS Object = NEW Viper.Collections.List()

list.Add(a)
list.Add(c)
list.Insert(1, b)          ' [a, b, c]

PRINT list.Find(b)         ' Output: 1

IF list.Has(a) THEN
  PRINT 1                  ' Output: 1 (true)
END IF

IF list.Remove(a) THEN
  PRINT list.Count         ' Output: 2
END IF
PRINT list.Find(a)         ' Output: -1

' Clear all
list.Clear()
```

---

## Viper.Collections.Map

A key-value dictionary with string keys. Provides O(1) average-case lookup, insertion, and deletion.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Map()`

### Properties

| Property  | Type    | Description                            |
|-----------|---------|----------------------------------------|
| `Len`     | Integer | Number of key-value pairs in the map   |
| `IsEmpty` | Boolean | Returns true if the map has no entries |

### Methods

| Method                     | Signature                 | Description                                                              |
|----------------------------|---------------------------|--------------------------------------------------------------------------|
| `Set(key, value)`          | `Void(String, Object)`    | Add or update key-value pair                                             |
| `Get(key)`                 | `Object(String)`          | Get value for key (returns NULL if not found)                            |
| `GetOr(key, defaultValue)` | `Object(String, Object)`  | Get value for key, or return `defaultValue` if missing (does not insert) |
| `Has(key)`                 | `Boolean(String)`         | Check if key exists                                                      |
| `SetIfMissing(key, value)` | `Boolean(String, Object)` | Insert key-value pair only when missing; returns true if inserted        |
| `Remove(key)`              | `Boolean(String)`         | Remove key-value pair; returns true if found                             |
| `Clear()`                  | `Void()`                  | Remove all entries                                                       |
| `Keys()`                   | `Seq()`                   | Get sequence of all keys                                                 |
| `Values()`                 | `Seq()`                   | Get sequence of all values                                               |

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

' Get-or-default without inserting
PRINT scores.GetOr("Dave", 0)   ' Output: 0 (and "Dave" is still missing)

' Insert only if missing
IF scores.SetIfMissing("Bob", 123) THEN
    PRINT "Inserted Bob"
ELSE
    PRINT "Bob already exists"
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

## Viper.Collections.Queue

A FIFO (first-in-first-out) collection. Elements are added at the back and removed from the front. Implemented as a
circular buffer for O(1) add and take operations.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Queue()`

### Properties

| Property  | Type    | Description                               |
|-----------|---------|-------------------------------------------|
| `Len`     | Integer | Number of elements in the queue           |
| `IsEmpty` | Boolean | Returns true if the queue has no elements |

### Methods

| Method       | Signature      | Description                                            |
|--------------|----------------|--------------------------------------------------------|
| `Add(value)` | `Void(Object)` | Add element to back of queue                           |
| `Take()`     | `Object()`     | Remove and return front element (traps if empty)       |
| `Peek()`     | `Object()`     | Return front element without removing (traps if empty) |
| `Clear()`    | `Void()`       | Remove all elements                                    |

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

## Viper.Collections.Ring

A fixed-size circular buffer (ring buffer). When full, pushing new elements automatically overwrites the oldest
elements.

**Type:** Instance class

**Constructor:** `NEW Viper.Collections.Ring(capacity)`

### Properties

| Property  | Type      | Description                          |
|-----------|-----------|--------------------------------------|
| `Len`     | `Integer` | Number of elements currently stored  |
| `Cap`     | `Integer` | Maximum capacity (fixed at creation) |
| `IsEmpty` | `Boolean` | True if ring has no elements         |
| `IsFull`  | `Boolean` | True if ring is at capacity          |

### Methods

| Method       | Returns | Description                                         |
|--------------|---------|-----------------------------------------------------|
| `Push(item)` | void    | Add item; overwrites oldest if full                 |
| `Pop()`      | Object  | Remove and return oldest item (NULL if empty)       |
| `Peek()`     | Object  | Return oldest item without removing (NULL if empty) |
| `Get(index)` | Object  | Get item by logical index (0 = oldest)              |
| `Clear()`    | void    | Remove all elements                                 |

### Example

```basic
' Create a ring buffer with capacity 3
DIM recent AS OBJECT = NEW Viper.Collections.Ring(3)

' Push some values
recent.Push("first")
recent.Push("second")
recent.Push("third")
PRINT recent.Len        ' Output: 3
PRINT recent.IsFull     ' Output: 1 (true)

' Push when full overwrites oldest
recent.Push("fourth")
PRINT recent.Len        ' Output: 3 (still 3)
PRINT recent.Peek()     ' Output: second (first was overwritten)

' Get by index (0 = oldest)
PRINT recent.Get(0)     ' Output: second
PRINT recent.Get(1)     ' Output: third
PRINT recent.Get(2)     ' Output: fourth

' Pop removes oldest (FIFO)
DIM oldest AS STRING = recent.Pop()
PRINT oldest            ' Output: second
PRINT recent.Len        ' Output: 2

' Interleaved push/pop
recent.Push("fifth")
PRINT recent.Pop()      ' Output: third
PRINT recent.Pop()      ' Output: fourth
PRINT recent.Pop()      ' Output: fifth
PRINT recent.IsEmpty    ' Output: 1 (true)
```

### Use Cases

- **Recent history:** Keep N most recent log entries, commands, or events
- **Sliding window:** Process data in fixed-size windows
- **Bounded caching:** Cache with automatic eviction of oldest entries
- **Event buffering:** Buffer events with guaranteed memory bounds
- **Audio/video buffering:** Fixed-size media sample buffers

---

## Viper.Collections.Seq

Dynamic sequence (growable array) with stack and queue operations. Viper's primary growable collection type, supporting
push/pop, insert/remove, and slicing operations.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Seq()` or `Viper.Collections.Seq.WithCapacity(cap)`

### Properties

| Property  | Type    | Description                                    |
|-----------|---------|------------------------------------------------|
| `Len`     | Integer | Number of elements currently in the sequence   |
| `Cap`     | Integer | Current allocated capacity                     |
| `IsEmpty` | Boolean | Returns true if the sequence has zero elements |

### Methods

| Method                 | Signature               | Description                                                                           |
|------------------------|-------------------------|---------------------------------------------------------------------------------------|
| `Get(index)`           | `Object(Integer)`       | Returns the element at the specified index (0-based)                                  |
| `Set(index, value)`    | `Void(Integer, Object)` | Sets the element at the specified index                                               |
| `Push(value)`          | `Void(Object)`          | Appends an element to the end                                                         |
| `PushAll(other)`       | `Void(Seq)`             | Appends all elements of `other` onto this sequence (self-appends double the sequence) |
| `Pop()`                | `Object()`              | Removes and returns the last element                                                  |
| `Peek()`               | `Object()`              | Returns the last element without removing it                                          |
| `First()`              | `Object()`              | Returns the first element                                                             |
| `Last()`               | `Object()`              | Returns the last element                                                              |
| `Insert(index, value)` | `Void(Integer, Object)` | Inserts an element at the specified position                                          |
| `Remove(index)`        | `Object(Integer)`       | Removes and returns the element at the specified position                             |
| `Clear()`              | `Void()`                | Removes all elements                                                                  |
| `Find(value)`          | `Integer(Object)`       | Returns the index of a value, or -1 if not found                                      |
| `Has(value)`           | `Boolean(Object)`       | Returns true if the sequence contains the value                                       |
| `Reverse()`            | `Void()`                | Reverses the elements in place                                                        |
| `Shuffle()`            | `Void()`                | Shuffles the elements in place (deterministic when `Viper.Random.Seed` is set)        |
| `Slice(start, end)`    | `Seq(Integer, Integer)` | Returns a new sequence with elements from start (inclusive) to end (exclusive)        |
| `Clone()`              | `Seq()`                 | Returns a shallow copy of the sequence                                                |
| `Sort()`               | `Void()`                | Sorts elements in ascending order (stable merge sort)                                 |
| `SortDesc()`           | `Void()`                | Sorts elements in descending order (stable merge sort)                                |
| `Keep(pred)`           | `Seq(Function)`         | Returns new Seq with elements where predicate returns true                            |
| `Reject(pred)`         | `Seq(Function)`         | Returns new Seq excluding elements where predicate returns true                       |
| `Apply(fn)`            | `Seq(Function)`         | Returns new Seq with each element transformed by function                             |
| `All(pred)`            | `Boolean(Function)`     | Returns true if all elements satisfy predicate (true for empty)                       |
| `Any(pred)`            | `Boolean(Function)`     | Returns true if any element satisfies predicate (false for empty)                     |
| `None(pred)`           | `Boolean(Function)`     | Returns true if no elements satisfy predicate (true for empty)                        |
| `CountWhere(pred)`     | `Integer(Function)`     | Returns count of elements satisfying predicate                                        |
| `FindWhere(pred)`      | `Object(Function)`      | Returns first element satisfying predicate, or NULL                                   |
| `Take(n)`              | `Seq(Integer)`          | Returns new Seq with first n elements                                                 |
| `Drop(n)`              | `Seq(Integer)`          | Returns new Seq skipping first n elements                                             |
| `TakeWhile(pred)`      | `Seq(Function)`         | Returns new Seq with leading elements while predicate is true                         |
| `DropWhile(pred)`      | `Seq(Function)`         | Returns new Seq skipping leading elements while predicate is true                     |
| `Fold(init, fn)`       | `Object(Object, Function)` | Reduces sequence to single value using accumulator                                 |

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

' Push all elements from another sequence
DIM other AS Viper.Collections.Seq
other = NEW Viper.Collections.Seq()
other.Push(item4)
other.Push(item5)
seq.PushAll(other)

' Deterministic shuffle (Random.Seed influences Shuffle)
Viper.Random.Seed(1)
seq.Shuffle()

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

### Sorting

```basic
DIM names AS Viper.Collections.Seq
names = NEW Viper.Collections.Seq()
names.Push("Charlie")
names.Push("Alice")
names.Push("Bob")

' Sort in ascending order
names.Sort()
' names is now: Alice, Bob, Charlie

' Sort in descending order
names.SortDesc()
' names is now: Charlie, Bob, Alice
```

### Functional Operations

Seq provides functional-style operations for filtering, transforming, and querying elements.

```basic
DIM numbers AS Viper.Collections.Seq
numbers = NEW Viper.Collections.Seq()
FOR i = 1 TO 10
    numbers.Push(i)
NEXT i

' Keep only even numbers (filter)
DIM evens AS Viper.Collections.Seq
evens = numbers.Keep(FUNCTION(n) RETURN n MOD 2 = 0)
' evens contains: 2, 4, 6, 8, 10

' Reject odd numbers (inverse filter)
DIM alsoEvens AS Viper.Collections.Seq
alsoEvens = numbers.Reject(FUNCTION(n) RETURN n MOD 2 = 1)

' Transform each element (map)
DIM doubled AS Viper.Collections.Seq
doubled = numbers.Apply(FUNCTION(n) RETURN n * 2)
' doubled contains: 2, 4, 6, 8, 10, 12, 14, 16, 18, 20

' Check predicates
DIM allPositive AS INTEGER = numbers.All(FUNCTION(n) RETURN n > 0)  ' True
DIM anyLarge AS INTEGER = numbers.Any(FUNCTION(n) RETURN n > 100)   ' False
DIM noneTiny AS INTEGER = numbers.None(FUNCTION(n) RETURN n < 0)    ' True

' Count matching elements
DIM evenCount AS INTEGER = numbers.CountWhere(FUNCTION(n) RETURN n MOD 2 = 0)
' evenCount = 5

' Find first matching element
DIM firstEven AS INTEGER = numbers.FindWhere(FUNCTION(n) RETURN n MOD 2 = 0)
' firstEven = 2

' Take and Drop
DIM firstThree AS Viper.Collections.Seq = numbers.Take(3)   ' 1, 2, 3
DIM afterThree AS Viper.Collections.Seq = numbers.Drop(3)   ' 4, 5, 6, 7, 8, 9, 10

' TakeWhile and DropWhile
DIM smallNums AS Viper.Collections.Seq = numbers.TakeWhile(FUNCTION(n) RETURN n < 5)
' smallNums contains: 1, 2, 3, 4
DIM largeNums AS Viper.Collections.Seq = numbers.DropWhile(FUNCTION(n) RETURN n < 5)
' largeNums contains: 5, 6, 7, 8, 9, 10

' Fold (reduce) to compute sum
DIM sum AS INTEGER = numbers.Fold(0, FUNCTION(acc, n) RETURN acc + n)
' sum = 55
```

### Use Cases

- **Stack:** Use `Push()` and `Pop()` for LIFO operations
- **Queue:** Use `Push()` to add and `Remove(0)` to dequeue (FIFO)
- **Dynamic Array:** Use `Get()`/`Set()` for random access
- **Slicing:** Use `Slice()` to extract sub-sequences
- **Filtering:** Use `Keep()` and `Reject()` to filter by condition
- **Transformation:** Use `Apply()` to transform all elements
- **Queries:** Use `All()`, `Any()`, `None()` for predicate checks
- **Aggregation:** Use `Fold()` to reduce to a single value

---

## Viper.Collections.Set

A generic set data structure for storing unique objects. Efficiently handles membership testing, set operations (union,
intersection, difference), and subset/superset queries. Unlike `Bag` which stores strings, `Set` stores arbitrary objects.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.Set.New()`

### Properties

| Property  | Type    | Description                       |
|-----------|---------|-----------------------------------|
| `Len`     | Integer | Number of objects in the set      |
| `IsEmpty` | Boolean | True if set contains no objects   |

### Methods

| Method              | Signature         | Description                                                    |
|---------------------|-------------------|----------------------------------------------------------------|
| `Put(obj)`          | `Boolean(Object)` | Add an object; returns true if new, false if already present   |
| `Drop(obj)`         | `Boolean(Object)` | Remove an object; returns true if removed, false if not found  |
| `Has(obj)`          | `Boolean(Object)` | Check if object is in the set                                  |
| `Clear()`           | `Void()`          | Remove all objects from the set                                |
| `Items()`           | `Seq()`           | Get all objects as a Seq (order undefined)                     |
| `Merge(other)`      | `Set(Set)`        | Return new set with union of both sets                         |
| `Common(other)`     | `Set(Set)`        | Return new set with intersection of both sets                  |
| `Diff(other)`       | `Set(Set)`        | Return new set with elements in this but not other             |
| `IsSubset(other)`   | `Boolean(Set)`    | True if this set is a subset of other                          |
| `IsSuperset(other)` | `Boolean(Set)`    | True if this set is a superset of other                        |
| `IsDisjoint(other)` | `Boolean(Set)`    | True if sets have no elements in common                        |

### Notes

- Objects are compared by reference identity, not value equality
- Order of objects returned by `Items()` is not guaranteed (hash table)
- Set operations (`Merge`, `Common`, `Diff`) return new sets; originals are unchanged
- Uses object identity hash for O(1) average-case operations
- Automatically resizes when load factor exceeds threshold

### Example

```basic
' Create and populate a set
DIM items AS OBJECT = Viper.Collections.Set.New()
DIM a AS OBJECT = Viper.Box.I64(1)
DIM b AS OBJECT = Viper.Box.I64(2)
DIM c AS OBJECT = Viper.Box.I64(3)

items.Put(a)
items.Put(b)
items.Put(c)
PRINT items.Len           ' Output: 3

' Duplicate add returns false
DIM wasNew AS INTEGER = items.Put(a)
PRINT wasNew              ' Output: 0 (already present)

' Membership testing
PRINT items.Has(b)        ' Output: 1 (true)
DIM d AS OBJECT = Viper.Box.I64(4)
PRINT items.Has(d)        ' Output: 0 (false)

' Remove an element
DIM removed AS INTEGER = items.Drop(b)
PRINT removed             ' Output: 1 (was removed)
PRINT items.Has(b)        ' Output: 0 (no longer present)

' Set operations
DIM setA AS OBJECT = Viper.Collections.Set.New()
DIM x AS OBJECT = Viper.Box.Str("x")
DIM y AS OBJECT = Viper.Box.Str("y")
DIM z AS OBJECT = Viper.Box.Str("z")
DIM w AS OBJECT = Viper.Box.Str("w")
setA.Put(x)
setA.Put(y)
setA.Put(z)

DIM setB AS OBJECT = Viper.Collections.Set.New()
setB.Put(y)
setB.Put(z)
setB.Put(w)

' Union: elements in either set
DIM merged AS OBJECT = setA.Merge(setB)
PRINT merged.Len          ' Output: 4 (x, y, z, w)

' Intersection: elements in both sets
DIM common AS OBJECT = setA.Common(setB)
PRINT common.Len          ' Output: 2 (y, z)

' Difference: elements in A but not B
DIM diff AS OBJECT = setA.Diff(setB)
PRINT diff.Len            ' Output: 1 (x only)

' Subset/superset checks
DIM subset AS OBJECT = Viper.Collections.Set.New()
subset.Put(y)
subset.Put(z)
PRINT subset.IsSubset(setA)     ' Output: 1 (true)
PRINT setA.IsSuperset(subset)   ' Output: 1 (true)

' Disjoint check
DIM disjoint AS OBJECT = Viper.Collections.Set.New()
disjoint.Put(w)
PRINT setA.IsDisjoint(disjoint) ' Output: 1 (true - no common elements)
```

### Set vs Bag

| Feature          | Set                        | Bag                     |
|------------------|----------------------------|-------------------------|
| Element type     | Any object                 | Strings only            |
| Comparison       | Reference identity         | String value            |
| Subset/Superset  | Yes                        | No                      |
| Disjoint check   | Yes                        | No                      |

### Use Cases

- **Object deduplication:** Track unique object instances
- **Graph algorithms:** Track visited nodes
- **Relationship modeling:** Many-to-many relationships
- **Set mathematics:** Compute unions, intersections, and differences
- **Access control:** Track permissions or capabilities

---

## Viper.Collections.SortedSet

A sorted set of unique strings maintained in sorted order. Unlike `Bag` which uses hash-based storage, `SortedSet`
keeps elements sorted, enabling efficient range queries, ordered iteration, and floor/ceiling operations.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.SortedSet()`

### Properties

| Property  | Type    | Description                             |
|-----------|---------|-----------------------------------------|
| `Len`     | Integer | Number of strings in the set            |
| `IsEmpty` | Boolean | True if set contains no strings         |

### Methods

| Method              | Signature                  | Description                                                        |
|---------------------|----------------------------|--------------------------------------------------------------------|
| `Put(str)`          | `Boolean(String)`          | Add a string; returns true if new, false if already present        |
| `Drop(str)`         | `Boolean(String)`          | Remove a string; returns true if removed, false if not found       |
| `Has(str)`          | `Boolean(String)`          | Check if string is in the set                                      |
| `Clear()`           | `Void()`                   | Remove all strings from the set                                    |
| `First()`           | `String()`                 | Get smallest (first) element; empty string if empty                |
| `Last()`            | `String()`                 | Get largest (last) element; empty string if empty                  |
| `Floor(str)`        | `String(String)`           | Greatest element <= given string; empty if none                    |
| `Ceil(str)`         | `String(String)`           | Least element >= given string; empty if none                       |
| `Lower(str)`        | `String(String)`           | Greatest element < given string (strictly less)                    |
| `Higher(str)`       | `String(String)`           | Least element > given string (strictly greater)                    |
| `At(index)`         | `String(Integer)`          | Get element at index in sorted order                               |
| `IndexOf(str)`      | `Integer(String)`          | Get index of element (-1 if not found)                             |
| `Items()`           | `Seq()`                    | Get all elements as a Seq in sorted order                          |
| `Range(from, to)`   | `Seq(String, String)`      | Get elements in range [from, to)                                   |
| `Take(n)`           | `Seq(Integer)`             | Get first n elements                                               |
| `Skip(n)`           | `Seq(Integer)`             | Get all elements except first n                                    |
| `Merge(other)`      | `SortedSet(SortedSet)`     | Return new set with union of both sets                             |
| `Common(other)`     | `SortedSet(SortedSet)`     | Return new set with intersection of both sets                      |
| `Diff(other)`       | `SortedSet(SortedSet)`     | Return new set with elements in this but not other                 |
| `IsSubset(other)`   | `Boolean(SortedSet)`       | True if this set is a subset of other                              |

### Example

```basic
DIM words AS OBJECT = NEW Viper.Collections.SortedSet()

' Add words (stored in sorted order)
words.Put("cherry")
words.Put("apple")
words.Put("banana")
words.Put("date")

PRINT words.Len          ' Output: 4

' Ordered access
PRINT words.First()      ' Output: "apple"
PRINT words.Last()       ' Output: "date"
PRINT words.At(1)        ' Output: "banana" (second in sorted order)

' Find index
PRINT words.IndexOf("cherry")  ' Output: 2

' Range queries
PRINT words.Floor("cat")       ' Output: "cherry" (largest <= "cat" - actually "banana")
PRINT words.Ceil("cat")        ' Output: "cherry" (smallest >= "cat")
PRINT words.Lower("cherry")    ' Output: "banana" (largest < "cherry")
PRINT words.Higher("cherry")   ' Output: "date" (smallest > "cherry")

' Get all items in sorted order
DIM all AS OBJECT = words.Items()
FOR i = 0 TO all.Len - 1
    PRINT all.Get(i)     ' Output: apple, banana, cherry, date
NEXT

' Get range [b, d) - from "b" to "d" exclusive
DIM range AS OBJECT = words.Range("b", "d")
FOR i = 0 TO range.Len - 1
    PRINT range.Get(i)   ' Output: banana, cherry
NEXT

' Set operations
DIM set1 AS OBJECT = NEW Viper.Collections.SortedSet()
set1.Put("a")
set1.Put("b")
set1.Put("c")

DIM set2 AS OBJECT = NEW Viper.Collections.SortedSet()
set2.Put("b")
set2.Put("c")
set2.Put("d")

' Union
DIM merged AS OBJECT = set1.Merge(set2)
PRINT merged.Len         ' Output: 4 (a, b, c, d)

' Intersection
DIM common AS OBJECT = set1.Common(set2)
PRINT common.Len         ' Output: 2 (b, c)

' Difference
DIM diff AS OBJECT = set1.Diff(set2)
PRINT diff.Len           ' Output: 1 (a only)

' Subset check
DIM subset AS OBJECT = NEW Viper.Collections.SortedSet()
subset.Put("b")
subset.Put("c")
PRINT subset.IsSubset(set1)  ' Output: 1 (true)
```

### SortedSet vs Bag vs Set

| Feature          | SortedSet        | Bag              | Set              |
|------------------|------------------|------------------|------------------|
| Element type     | Strings          | Strings          | Objects          |
| Order            | Sorted           | Unordered        | Unordered        |
| Lookup           | O(log n)         | O(1) average     | O(1) average     |
| Insert           | O(n)             | O(1) average     | O(1) average     |
| First/Last       | O(1)             | No               | No               |
| Floor/Ceil       | O(log n)         | No               | No               |
| Range queries    | O(log n + k)     | No               | No               |
| Index access     | O(1)             | No               | No               |

### Use Cases

- **Autocomplete:** Find words in a range starting with a prefix
- **Leaderboards:** Maintain sorted rankings with efficient updates
- **Scheduling:** Find next available time slot with Floor/Ceil
- **Range queries:** Find all items in a lexicographic range
- **Ordered iteration:** When you need strings in sorted order
- **Nearest neighbor:** Find closest match using Floor/Ceil

---

## Viper.Collections.Stack

A LIFO (last-in-first-out) collection. Elements are added and removed from the top.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Stack()`

### Properties

| Property  | Type    | Description                               |
|-----------|---------|-------------------------------------------|
| `Len`     | Integer | Number of elements on the stack           |
| `IsEmpty` | Boolean | Returns true if the stack has no elements |

### Methods

| Method        | Signature      | Description                                          |
|---------------|----------------|------------------------------------------------------|
| `Push(value)` | `Void(Object)` | Add element to top of stack                          |
| `Pop()`       | `Object()`     | Remove and return top element (traps if empty)       |
| `Peek()`      | `Object()`     | Return top element without removing (traps if empty) |
| `Clear()`     | `Void()`       | Remove all elements                                  |

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

## Viper.Collections.TreeMap

A sorted key-value map that maintains keys in sorted order. Uses a sorted array with binary search for O(log n) lookups.
Supports range queries via Floor/Ceil operations.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.TreeMap()`

### Properties

| Property  | Type    | Description                            |
|-----------|---------|----------------------------------------|
| `Len`     | Integer | Number of key-value pairs in the map   |
| `IsEmpty` | Boolean | Returns true if the map has no entries |

### Methods

| Method            | Signature              | Description                                                     |
|-------------------|------------------------|-----------------------------------------------------------------|
| `Set(key, value)` | `Void(String, Object)` | Set or update a key-value pair                                  |
| `Get(key)`        | `Object(String)`       | Get the value for a key (returns null if not found)             |
| `Has(key)`        | `Boolean(String)`      | Check if a key exists                                           |
| `Drop(key)`       | `Boolean(String)`      | Remove a key-value pair; returns true if removed                |
| `Clear()`         | `Void()`               | Remove all entries                                              |
| `Keys()`          | `Seq()`                | Get all keys as a Seq in sorted order                           |
| `Values()`        | `Seq()`                | Get all values as a Seq in key-sorted order                     |
| `First()`         | `String()`             | Get the smallest (first) key; returns empty string if empty     |
| `Last()`          | `String()`             | Get the largest (last) key; returns empty string if empty       |
| `Floor(key)`      | `String()`             | Get the largest key <= given key; returns empty string if none  |
| `Ceil(key)`       | `String()`             | Get the smallest key >= given key; returns empty string if none |

### Example

```basic
DIM tm AS Viper.Collections.TreeMap
tm = NEW Viper.Collections.TreeMap()

' Insert in any order - stored sorted
tm.Set("cherry", "red")
tm.Set("apple", "green")
tm.Set("banana", "yellow")

' Keys are always in sorted order
DIM keys AS OBJECT = tm.Keys()
PRINT keys.Get(0)  ' Output: "apple"
PRINT keys.Get(1)  ' Output: "banana"
PRINT keys.Get(2)  ' Output: "cherry"

' First/Last access
PRINT tm.First()   ' Output: "apple"
PRINT tm.Last()    ' Output: "cherry"

' Range queries
PRINT tm.Floor("blueberry")  ' Output: "banana" (largest key <= "blueberry")
PRINT tm.Ceil("blueberry")   ' Output: "cherry" (smallest key >= "blueberry")
```

### TreeMap vs Map

| Feature    | TreeMap  | Map           |
|------------|----------|---------------|
| Key order  | Sorted   | Unordered     |
| Lookup     | O(log n) | O(1) average  |
| Insert     | O(n)     | O(1) average  |
| First/Last | O(1)     | Not available |
| Floor/Ceil | O(log n) | Not available |

### Use Cases

- **Ordered iteration:** When you need keys in sorted order
- **Range queries:** Finding entries within a key range
- **Priority systems:** Using keys as priorities
- **Prefix matching:** Finding nearest matches for partial keys

---

## Viper.Collections.WeakMap

A map with weak value references. Values may become NULL when their referent is garbage collected. Uses string keys. Useful for caches and observer patterns where you don't want to prevent collection of values.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.WeakMap.New()`

### Properties

| Property  | Type    | Description                                              |
|-----------|---------|----------------------------------------------------------|
| `Len`     | Integer | Number of entries (including potentially collected ones) |
| `IsEmpty` | Boolean | True if map has no entries                               |

### Methods

| Method           | Signature              | Description                                                |
|------------------|------------------------|------------------------------------------------------------|
| `Set(key, value)`| `Void(String, Object)` | Set a value (stored as weak reference)                     |
| `Get(key)`       | `Object(String)`       | Get value for key (NULL if not found or collected)         |
| `Has(key)`       | `Boolean(String)`      | Check if key exists                                        |
| `Remove(key)`    | `Boolean(String)`      | Remove entry; returns true if found                        |
| `Keys()`         | `Seq()`                | Get all keys currently in the map                          |
| `Clear()`        | `Void()`               | Remove all entries                                         |
| `Compact()`      | `Integer()`            | Remove entries with collected values; returns count removed |

### Notes

- **Weak references:** Values are stored without preventing garbage collection. If the only reference to an object is through a WeakMap, it may be collected.
- **Stale entries:** After a value is collected, `Get()` returns NULL for that key. Use `Compact()` to clean up stale entries.
- **String keys:** Keys are regular (strong) string references.
- **Len includes stale:** `Len` counts all entries including those with collected values. Call `Compact()` first for an accurate live count.

### Example

```basic
' Create a weak map for caching
DIM cache AS OBJECT = Viper.Collections.WeakMap.New()

' Store values (weak references)
DIM obj AS OBJECT = CreateExpensiveObject()
cache.Set("key1", obj)
cache.Set("key2", CreateAnotherObject())

PRINT cache.Len      ' Output: 2
PRINT cache.Has("key1")  ' Output: 1 (true)

' Get value (may be NULL if collected)
DIM value AS OBJECT = cache.Get("key1")
IF value IS NOT NULL THEN
    PRINT "Cache hit"
ELSE
    PRINT "Cache miss - object was collected"
END IF

' Remove specific entry
cache.Remove("key2")

' Clean up stale entries
DIM removed AS INTEGER = cache.Compact()
PRINT "Compacted "; removed; " stale entries"

' Get all current keys
DIM keys AS OBJECT = cache.Keys()
FOR i = 0 TO keys.Len - 1
    PRINT keys.Get(i)
NEXT

' Clear everything
cache.Clear()
```

### WeakMap vs Map

| Feature           | WeakMap                    | Map                       |
|-------------------|----------------------------|---------------------------|
| Value references  | Weak (may be collected)    | Strong (prevents collection) |
| Memory management | Values can be GC'd         | Values kept alive          |
| Stale entries     | Possible (use Compact)     | Never                      |
| Use case          | Caches, observers          | General key-value storage  |

### Use Cases

- **Object caches:** Cache computed results without preventing GC of the source objects
- **Observer patterns:** Track observers without preventing their collection
- **Metadata storage:** Associate metadata with objects without extending their lifetime
- **Memoization:** Cache function results that can be recomputed if evicted

---

## See Also

- [Input/Output](io.md) - File operations for persisting collections
- [Text Processing](text.md) - `StringBuilder` for efficient string building, `Csv` for data import/export
- [Threads](threads.md) - Thread-safe access patterns using `Monitor` or `RwLock`

