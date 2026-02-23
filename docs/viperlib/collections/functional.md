# Functional & Lazy
> Seq, LazySeq, Iterator

**Part of [Viper Runtime Library](../README.md) › [Collections](README.md)**

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

### Zia Example

```rust
module SeqDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    var seq = new Seq();

    // Stack-like operations (stores boxed values)
    seq.Push("first");
    seq.Push("second");
    seq.Push("third");
    Say("Length: " + Fmt.Int(seq.Len));                     // 3

    // Peek and Pop (LIFO)
    Say("Peek: " + Viper.Core.Box.ToStr(seq.Peek()));            // third
    Say("Pop: " + Viper.Core.Box.ToStr(seq.Pop()));              // third
    Say("After pop: " + Fmt.Int(seq.Len));                   // 2

    // Reverse in place
    seq.Reverse();
    Say("Reversed first: " + Viper.Core.Box.ToStr(seq.Get(0)));  // second
}
```

### BASIC Example

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

## Viper.LazySeq

A lazy sequence that generates elements on demand rather than storing them all in memory. Useful for infinite sequences,
computed sequences, and memory-efficient processing of large datasets. Supports functional-style transformations and
collectors.

> **Note:** The canonical namespace is `Viper.LazySeq`, not `Viper.Collections.LazySeq`.

**Type:** Instance (obj)
**Constructors:**

- `Viper.LazySeq.Range(start, end, step)` - Generate integer range
- `Viper.LazySeq.Repeat(value, count)` - Repeat value count times (-1 for infinite)

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

#### Collectors (consume sequence)

| Method         | Signature                    | Description                                            |
|----------------|------------------------------|--------------------------------------------------------|
| `ToSeq()`      | `Seq()`                      | Collect all elements into a Seq (may not terminate!)   |
| `ToSeqN(n)`    | `Seq(Integer)`               | Collect at most n elements into a Seq                  |
| `Count()`      | `Integer()`                  | Count all elements (may not terminate!)                |
| `Find(pred)`   | `Object(Function)`           | Find first matching element; null if not found         |
| `Any(pred)`    | `Boolean(Function)`          | True if any element matches                            |
| `All(pred)`    | `Boolean(Function)`          | True if all elements match (may not terminate!)        |

### Zia Example

> LazySeq is not yet available as a constructible type in Zia. Use BASIC or access via the runtime API.

### BASIC Example

```basic
' Create a range (like Python's range)
DIM nums AS OBJECT = Viper.LazySeq.Range(1, 11, 1)
' Generates: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10

' Manual iteration
WHILE NOT nums.IsExhausted
    DIM n AS OBJECT = nums.Next()
    PRINT n
WEND

' Create and transform lazily
DIM evens AS OBJECT = Viper.LazySeq.Range(1, 100, 1) _
    .Filter(FUNCTION(n) RETURN n MOD 2 = 0) _
    .Take(5)
' Generates: 2, 4, 6, 8, 10 (only computes what's needed)

' Collect into a Seq
DIM evenSeq AS OBJECT = evens.ToSeq()
PRINT evenSeq.Len    ' Output: 5

' Infinite sequence with Take
DIM ones AS OBJECT = Viper.LazySeq.Repeat(1, -1)  ' Infinite 1s
DIM firstTen AS OBJECT = ones.Take(10).ToSeq()
PRINT firstTen.Len   ' Output: 10

' Find first element matching condition
DIM firstBig AS OBJECT = Viper.LazySeq.Range(1, 1000000, 1) _
    .Find(FUNCTION(n) RETURN n > 500)
PRINT firstBig       ' Output: 501 (stops immediately, doesn't check all million)

' Check predicates
DIM allPositive AS INTEGER = Viper.LazySeq.Range(1, 100, 1) _
    .All(FUNCTION(n) RETURN n > 0)
PRINT allPositive    ' Output: 1 (true)

' Peek without consuming
DIM seq AS OBJECT = Viper.LazySeq.Range(1, 5, 1)
PRINT seq.Peek()     ' Output: 1
PRINT seq.Peek()     ' Output: 1 (still 1)
PRINT seq.Next()     ' Output: 1 (now consumed)
PRINT seq.Peek()     ' Output: 2

' Reset to beginning
seq.Reset()
PRINT seq.Next()     ' Output: 1 (back to start)

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

- **Infinite sequences:** Methods like `ToSeq()`, `Count()`, and `All()` may never
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

## Viper.Collections.Iterator

A sequential cursor over a Seq that provides controlled, single-pass traversal with peek-ahead, skip, and reset
capabilities.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.Iterator.FromSeq(seq)` - creates an iterator over an existing Seq

### Properties

| Property | Type    | Description                                      |
|----------|---------|--------------------------------------------------|
| `Index`  | Integer | Current position (number of elements consumed)   |
| `Count`  | Integer | Total number of elements in the underlying Seq   |

### Methods

| Method      | Signature        | Description                                                 |
|-------------|------------------|-------------------------------------------------------------|
| `HasNext()` | `Boolean()`      | Returns true if there are more elements to iterate          |
| `Next()`    | `Object()`       | Return the current element and advance the cursor           |
| `Peek()`    | `Object()`       | Return the current element without advancing                |
| `Reset()`   | `Void()`         | Reset the cursor to the beginning                           |
| `ToSeq()`   | `Seq()`          | Collect all remaining elements into a new Seq               |
| `Skip(n)`   | `Integer(Integer)` | Skip forward by n elements; returns actual number skipped |

### Notes

- `Next()` and `Peek()` return boxed values in Zia; use `Box.ToStr()`, `Box.ToI64()`, etc. to unwrap
- In BASIC, `Next()` and `Peek()` return values directly
- `Skip(n)` returns the actual number of elements skipped, which may be less than n if the iterator is near the end
- `ToSeq()` collects only the *remaining* elements from the current position onward

### Zia Example

```rust
module IteratorDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    var seq = Seq.New();
    seq.Push(Box.Str("a"));
    seq.Push(Box.Str("b"));
    seq.Push(Box.Str("c"));
    seq.Push(Box.Str("d"));
    seq.Push(Box.Str("e"));

    var it = Iterator.FromSeq(seq);
    SayInt(it.Count);                         // 5

    // Peek without advancing
    Say(Box.ToStr(it.Peek()));                // a
    SayInt(it.Index);                         // 0

    // Next advances the cursor
    Say(Box.ToStr(it.Next()));                // a
    Say(Box.ToStr(it.Next()));                // b
    SayInt(it.Index);                         // 2

    // Skip forward
    SayInt(it.Skip(2));                       // 2 (skipped c, d)
    Say(Box.ToStr(it.Peek()));                // e

    // Collect remaining
    it.Reset();
    it.Next();   // consume a
    it.Next();   // consume b
    var rest = it.ToSeq();
    SayInt(rest.Len);                         // 3 (c, d, e)
}
```

### BASIC Example

```basic
DIM seq AS Viper.Collections.Seq
seq = Viper.Collections.Seq.New()
seq.Push("a")
seq.Push("b")
seq.Push("c")
seq.Push("d")
seq.Push("e")

DIM it AS OBJECT
it = Viper.Collections.Iterator.FromSeq(seq)
PRINT it.Count          ' 5

' Peek without advancing
PRINT it.Peek()          ' a
PRINT it.Index           ' 0

' Next advances
PRINT it.Next()          ' a
PRINT it.Index           ' 1
PRINT it.Next()          ' b
PRINT it.Index           ' 2

' Peek after Next
PRINT it.Peek()          ' c

' Skip forward
PRINT it.Skip(2)         ' 2 (skipped c, d)
PRINT it.Index           ' 4
PRINT it.Peek()          ' e

' Iterate to exhaustion
PRINT it.Next()          ' e
PRINT it.HasNext         ' 0

' Reset to beginning
it.Reset()
PRINT it.Index           ' 0
PRINT it.Peek()          ' a

' Collect remaining after consuming some
it.Next()   ' consume a
it.Next()   ' consume b
DIM rest AS OBJECT
rest = it.ToSeq()
PRINT rest.Len           ' 3 (c, d, e)
PRINT rest.Get(0)        ' c
PRINT rest.Get(1)        ' d
PRINT rest.Get(2)        ' e
```

### Use Cases

- **Parser input:** Iterate over tokens with peek-ahead for lookahead parsing
- **Streaming processing:** Process elements one at a time with position tracking
- **Batch processing:** Skip forward to a specific position, then collect a batch with `ToSeq`
- **Resumable iteration:** Use `Index` and `Reset` to save and restore position

---


## See Also

- [Sequential Collections](sequential.md)
- [Maps & Sets](maps-sets.md)
- [Specialized Maps](multi-maps.md)
- [Specialized Structures](specialized.md)
- [Collections Overview](README.md)
- [Viper Runtime Library](../README.md)
