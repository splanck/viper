---
status: active
audience: public
last-verified: 2026-07-14
---

# Sequential Collections
> List, Queue, Stack, Deque, Ring, Heap

**Part of [Viper Runtime Library](../README.md) › [Collections](README.md)**

---

## Viper.Collections.List

Dynamic array that grows automatically. Stores object references.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.List()`

### Properties

| Property | Type    | Description                 |
|----------|---------|-----------------------------|
| `Count`   | Integer | Number of items in the list            |
| `IsEmpty` | Boolean | True if the list contains no items     |

> **Note:** The public collection-size property is `Count`; `Length` is not registered for this class.

### Methods

| Method                   | Signature               | Description                                                                           |
|--------------------------|-------------------------|---------------------------------------------------------------------------------------|
| `Push(item)`             | `Void(Object)`          | Appends an item to the end of the list                                                |
| `Get(index)`             | `Object(Integer)`       | Gets the item at the specified index                                                  |
| `Set(index, value)`      | `Void(Integer, Object)` | Sets the item at the specified index                                                  |
| `Clear()`                | `Void()`                | Removes all items from the list                                                       |
| `Has(item)`              | `Boolean(Object)`       | Returns true if the list contains a matching object                                   |
| `Find(item)`             | `Integer(Object)`       | Returns index of the first matching object, or `-1` if not found                      |
| `FindOption(item)`       | `Option[Integer](Object)` | Returns `Some(index)` for the first matching object, or `None` if not found        |
| `Insert(index, item)`    | `Void(Integer, Object)` | Inserts the item at `index` (0..Count); `index == Len` appends; traps if out of range   |
| `Remove(item)`           | `Boolean(Object)`       | Removes the first matching object; returns true if removed                            |
| `RemoveAt(index)`        | `Void(Integer)`         | Removes the item at the specified index                                               |
| `Slice(start, end)`      | `List(Integer, Integer)`| Returns a new list with elements from start (inclusive) to end (exclusive)            |
| `Reverse()`              | `Void()`                | Reverses the elements of the list in place                                            |
| `First()`                | `Object()`              | Returns the first element, or null when empty                                         |
| `Last()`                 | `Object()`              | Returns the last element, or null when empty                                          |
| `Sort()`                 | `Void()`                | Stable ascending default sort; see the comparison notes below                         |
| `SortDesc()`             | `Void()`                | Sorts the list in descending order                                                    |
| `Pop()`                  | `Object()`              | Removes and returns the last element (traps if empty)                                 |
| `Shuffle()`              | `Void()`                | Shuffles the list in place (Fisher-Yates)                                             |
| `Clone()`                | `List()`                | Creates a shallow copy of the list                                                    |
| `ToSeq()`                | `Seq()`                 | Returns elements as a new Seq                                                         |
| `ToSet()`                | `Set()`                 | Returns unique elements as a new Set                                                  |
| `ToStack()`              | `Stack()`               | Returns elements as a new Stack                                                       |
| `ToQueue()`              | `Queue()`               | Returns elements as a new Queue                                                       |

### Notes

- List retains stored objects and releases them when removed, overwritten, cleared, or finalized.
- `Get()`, `First()`, `Last()`, and `Pop()` return owned object references, so callers can keep the result after the list changes or is released.
- `Slice()` and `Clone()` return independent lists that retain their elements without leaking temporary `Get()` references.
- `Has()`, `Find()`, and `Remove()` compare boxed integers, booleans, floats, and strings by value
  (including treating boxed NaNs as equal); other objects use identity. Queue, Stack, Deque, and
  Ring membership use identity for every object, so membership semantics are not uniform across
  the sequential collection classes (VDOC-086).
- The default stable sort compares raw or boxed strings lexicographically and boxed integers
  numerically. Null sorts first; other and mixed-type pairs fall back to raw pointer order. That
  fallback is not a portable semantic order, and mixed-type default sorting should not be relied
  on (VDOC-089).
- Prefer `FindOption()` for new code. `Find()` remains available for compatibility with existing `-1` checks.

### Zia Example

```rust
module ListDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Text.Fmt as Fmt;

func start() {
    var list = new List[String]();

    // Add items
    list.Push("apple");
    list.Push("banana");
    list.Push("cherry");
    Say("Count: " + Fmt.Int(list.Count));            // 3

    // Access by index
    Say("First: " + list.Get(0));                // apple
    Say("Last: " + list.Get(list.Count - 1));      // cherry

    // Iterate with for-in
    for item in list {
        Print(item + " ");
    }
    Say("");                                     // apple banana cherry

    // Modify
    list.Set(1, "blueberry");
    Say("Updated: " + list.Get(1));              // blueberry

    // Insert at position
    list.Insert(0, "avocado");
    Say("First: " + list.Get(0));                // avocado
}
```

### BASIC Example

```basic
DIM list AS Viper.Collections.List
list = NEW Viper.Collections.List()

' Add items
DIM a AS Object = NEW Viper.Collections.List()
DIM b AS Object = NEW Viper.Collections.List()
DIM c AS Object = NEW Viper.Collections.List()

list.Push(a)
list.Push(c)
list.Insert(1, b)          ' [a, b, c]

DIM found AS OBJECT = list.FindOption(b)
IF found.IsSome THEN
  PRINT found.UnwrapI64()  ' Output: 1
END IF

IF list.Has(a) THEN
  PRINT 1                  ' Output: 1 (true)
END IF

IF list.Remove(a) THEN
  PRINT list.Count           ' Output: 2
END IF
PRINT list.FindOption(a).IsNone  ' Output: 1

' Access by index
PRINT list.Get(0)          ' First element
list.Set(0, b)             ' Replace first element

' Slice, Reverse, First, Last
DIM portion AS Viper.Collections.List = list.Slice(0, 1)
list.Reverse()             ' Reverse in place
PRINT list.First()         ' First element
PRINT list.Last()          ' Last element

' Clear all
list.Clear()
```

---

## Viper.Collections.Queue

A FIFO (first-in-first-out) collection. Elements are added at the back and removed from the front.
It uses a circular buffer for amortized O(1) pushes and O(1) pops.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Queue()`

### Properties

| Property  | Type    | Description                               |
|-----------|---------|-------------------------------------------|
| `Count`   | Integer | Number of elements in the queue           |
| `IsEmpty` | Boolean | Returns true if the queue has no elements |

### Methods

| Method        | Signature      | Description                                            |
|---------------|----------------|--------------------------------------------------------|
| `Push(value)` | `Void(Object)` | Add element to back of queue                           |
| `Pop()`       | `Object()`     | Remove and return front element (traps if empty)       |
| `TryPop()`    | `Object()`     | Remove and return front element, or null if empty      |
| `TryPopOption()` | `Option[Object]()` | Remove and return front element, or `None` if empty |
| `Peek()`      | `Object()`     | Return front element without removing (traps if empty) |
| `Has(value)`  | `Boolean(Object)` | Check if an element is in the queue (by reference)  |
| `Clear()`     | `Void()`       | Remove all elements                                    |
| `Clone()`     | `Queue()`      | Create a shallow copy of the queue                     |
| `ToList()`    | `List()`       | Returns elements as a new List                         |
| `ToSeq()`     | `Seq()`        | Returns elements as a new Seq                          |

### Notes

- The registered `Queue.New` constructor creates a borrowed-element queue, and the class surface
  does not expose the C-only ownership switch. Keep every stored object alive independently, or
  create an owning queue through `List.ToQueue()` / `Seq.ToQueue()` (VDOC-087). `ToList()` and
  `ToSeq()` always return owning snapshots.
- `Pop()` and `TryPop()` return owned object references. In owning mode, the queue's retained reference is transferred to the caller.
- `Peek()` returns a borrowed reference whose lifetime is bounded by the stored element's owner
  (and, for an owning queue produced by a conversion, by the queue entry).
- Prefer `TryPopOption()` for new code. It distinguishes an empty queue from a stored null object; `TryPop()` remains as a compatibility helper.

### Zia Example

```rust
module QueueDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Text.Fmt as Fmt;

func start() {
    var queue = new Queue();
    queue.Push("first");
    queue.Push("second");
    queue.Push("third");
    Say("Count: " + Fmt.Int(queue.Count));                 // 3

    // FIFO order
    Say("Pop: " + Viper.Core.Box.ToStr(queue.Pop()));          // first
    Say("Peek: " + Viper.Core.Box.ToStr(queue.Peek()));        // second
    Say("After pop: " + Fmt.Int(queue.Count));               // 2

    queue.Clear();
    Say("Empty: " + Fmt.Bool(queue.IsEmpty));              // true
}
```

### BASIC Example

```basic
DIM queue AS Viper.Collections.Queue
queue = NEW Viper.Collections.Queue()

' Add elements to the queue
queue.Push("first")
queue.Push("second")
queue.Push("third")

PRINT queue.Count      ' Output: 3
PRINT queue.IsEmpty  ' Output: False

' Pop returns elements in FIFO order
PRINT queue.Pop()    ' Output: "first"
PRINT queue.Peek()   ' Output: "second" (still in queue)
PRINT queue.Count      ' Output: 2

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

## Viper.Collections.Stack

A LIFO (last-in-first-out) collection. Elements are added and removed from the top.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Stack()`

### Properties

| Property  | Type    | Description                               |
|-----------|---------|-------------------------------------------|
| `Count`   | Integer | Number of elements on the stack           |
| `IsEmpty` | Boolean | Returns true if the stack has no elements |

### Methods

| Method        | Signature      | Description                                          |
|---------------|----------------|------------------------------------------------------|
| `Push(value)` | `Void(Object)` | Add element to top of stack                          |
| `Pop()`       | `Object()`     | Remove and return top element (traps if empty)       |
| `TryPop()`    | `Object()`     | Remove and return top element, or null if empty      |
| `TryPopOption()` | `Option[Object]()` | Remove and return top element, or `None` if empty |
| `Peek()`      | `Object()`     | Return top element without removing (traps if empty) |
| `Has(value)`  | `Boolean(Object)` | Check if an element is on the stack (by reference) |
| `Clear()`     | `Void()`       | Remove all elements                                  |
| `Clone()`     | `Stack()`      | Create a shallow copy of the stack                   |
| `ToList()`    | `List()`       | Returns elements as a new List                       |
| `ToSeq()`     | `Seq()`        | Returns elements as a new Seq                        |

### Notes

- Stack-to-list, stack-to-seq, and iterator snapshots preserve bottom-to-top order without mutating the source stack.
- The registered `Stack.New` constructor creates a borrowed-element stack, and the class surface
  does not expose the C-only ownership switch. Keep every stored object alive independently, or
  create an owning stack through `List.ToStack()` / `Seq.ToStack()` (VDOC-087). `ToList()` and
  `ToSeq()` always return owning snapshots.
- `Pop()` and `TryPop()` return owned object references. In owning mode, the stack's retained reference is transferred to the caller.
- `Peek()` returns a borrowed reference whose lifetime is bounded by the stored element's owner
  (and, for an owning stack produced by a conversion, by the stack entry).
- Prefer `TryPopOption()` for new code. It distinguishes an empty stack from a stored null object; `TryPop()` remains as a compatibility helper.
- Constructor allocation failures trap cleanly instead of returning a partial stack.

### Zia Example

```rust
module StackDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Text.Fmt as Fmt;

func start() {
    var stack = new Stack();
    stack.Push("first");
    stack.Push("second");
    stack.Push("third");
    Say("Count: " + Fmt.Int(stack.Count));                  // 3

    // LIFO order
    Say("Pop: " + Viper.Core.Box.ToStr(stack.Pop()));           // third
    Say("Peek: " + Viper.Core.Box.ToStr(stack.Peek()));         // second
    Say("After pop: " + Fmt.Int(stack.Count));               // 2

    stack.Clear();
    Say("Empty: " + Fmt.Bool(stack.IsEmpty));              // true
}
```

### BASIC Example

```basic
DIM stack AS Viper.Collections.Stack
stack = NEW Viper.Collections.Stack()

' Push elements onto the stack
stack.Push("first")
stack.Push("second")
stack.Push("third")

PRINT stack.Count      ' Output: 3
PRINT stack.IsEmpty  ' Output: False

' Pop returns elements in LIFO order
PRINT stack.Pop()    ' Output: "third"
PRINT stack.Peek()   ' Output: "second" (still on stack)
PRINT stack.Count      ' Output: 2

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

## Viper.Collections.Deque

A double-ended queue (deque) that supports efficient insertion and removal at both ends. Combines the capabilities of
stacks and queues while also supporting indexed access.

**Type:** Instance (obj)
**Factories:** `NEW Viper.Collections.Deque()` or
`Viper.Collections.Deque.WithCapacity(capacity)`. The capacity factory clamps values below 1 to
one slot.

### Properties

| Property  | Type    | Description                               |
|-----------|---------|-------------------------------------------|
| `Count`   | Integer | Number of elements in the deque           |
| `Capacity` | Integer | Current allocated capacity              |
| `IsEmpty` | Boolean | Returns true if the deque has no elements |

### Methods

| Method               | Signature               | Description                                           |
|----------------------|-------------------------|-------------------------------------------------------|
| `PushFront(value)`   | `Void(Object)`          | Add element to front of deque                         |
| `PushBack(value)`    | `Void(Object)`          | Add element to back of deque                          |
| `PopFront()`         | `Object()`              | Remove and return front element (traps if empty)      |
| `PopBack()`          | `Object()`              | Remove and return back element (traps if empty)       |
| `TryPopFront()`      | `Object()`              | Remove and return front element, or null if empty     |
| `TryPopBack()`       | `Object()`              | Remove and return back element, or null if empty      |
| `TryPopFrontOption()`| `Option[Object]()`      | Remove and return front element, or `None` if empty   |
| `TryPopBackOption()` | `Option[Object]()`      | Remove and return back element, or `None` if empty    |
| `PeekFront()`        | `Object()`              | Return front element without removing (traps if empty)|
| `PeekBack()`         | `Object()`              | Return back element without removing (traps if empty) |
| `Get(index)`         | `Object(Integer)`       | Get element at index (0 = front)                      |
| `Set(index, value)`  | `Void(Integer, Object)` | Set element at index                                  |
| `Has(value)`         | `Boolean(Object)`       | Check if element exists (object identity)             |
| `Clear()`            | `Void()`                | Remove all elements                                   |
| `Reverse()`          | `Void()`                | Reverse elements in place                             |
| `Clone()`            | `Deque()`               | Create shallow copy                                   |
| `ToSeq()`            | `Seq()`                 | Returns elements as a new Seq                         |
| `ToList()`           | `List()`                | Returns elements as a new List                        |

### Notes

- Deque retains stored objects and releases them when removed, overwritten, cleared, or finalized.
- `Get()`, `PeekFront()`, `PeekBack()`, `PopFront()`, `PopBack()`, `TryPopFront()`, and `TryPopBack()` return owned object references.
- Prefer `TryPopFrontOption()` and `TryPopBackOption()` for new code. They distinguish an empty deque from a stored null object.
- `Clone()`, `ToSeq()`, and `ToList()` return independent collections that retain their elements.

### Zia Example

```rust
module DequeDemo;

bind Viper.Collections.Deque as Deque;
bind Viper.Core.Box as Box;
bind Viper.Terminal;

func start() {
    var deque: Deque = Deque.New();
    deque.PushBack(Box.Str("middle"));
    deque.PushFront(Box.Str("front"));
    deque.PushBack(Box.Str("back"));

    Say(Box.ToStr(deque.PopFront()));  // front
    Say(Box.ToStr(deque.PopBack()));   // back
    SayInt(deque.Count);               // 1
}
```

### BASIC Example

```basic
DIM deque AS Viper.Collections.Deque
deque = NEW Viper.Collections.Deque()

' Add elements to both ends
deque.PushBack("middle")
deque.PushFront("front")
deque.PushBack("back")

PRINT deque.Count          ' Output: 3

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
PRINT deque.Count          ' Output: 1

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
| Add front        | O(1) amortized | No            | No            |
| Add back         | O(1) amortized | O(1) amortized | O(1) amortized |
| Remove front     | O(1)           | O(1)          | No            |
| Remove back      | O(1)           | No            | O(1)          |
| Random access    | O(1)           | No            | No            |
| Reverse          | O(n)           | No            | No            |

### Use Cases

- **Sliding window:** Process data with access to both newest and oldest elements
- **Undo/Redo:** Push operations to back, pop from back for undo, front for redo
- **Work queues:** Manage both ends in single-threaded schedulers; this deque requires external synchronization for concurrent access
- **Palindrome checking:** Compare elements from both ends
- **Browser history:** Navigate forward and backward through pages

---

## Viper.Collections.Ring

A fixed-size circular buffer (ring buffer). When full, pushing new elements automatically overwrites the oldest
elements.

**Type:** Instance class

**Constructors:**
- `NEW Viper.Collections.Ring(capacity)` — fixed capacity ring buffer
- `Viper.Collections.Ring.New()` — ring buffer with default capacity (implementation-defined)

`capacity = 0` creates a one-slot ring. Negative capacities trap.

### Properties

| Property  | Type      | Description                          |
|-----------|-----------|--------------------------------------|
| `Count`   | `Integer` | Number of elements currently stored  |
| `Capacity` | `Integer` | Maximum capacity (fixed at creation) |
| `IsEmpty` | `Boolean` | True if ring has no elements         |
| `IsFull`  | `Boolean` | True if ring is at capacity          |
| `OwnsElements` | `Boolean` | True when the ring retains/releases stored runtime objects |
| `First`   | `Object`  | Oldest element, or null when empty        |
| `Last`    | `Object`  | Newest element, or null when empty        
### Methods

| Method       | Returns | Description                                         |
|--------------|---------|-----------------------------------------------------|
| `Push(item)` | void    | Add item; overwrites oldest if full                 |
| `Pop()`      | Object  | Remove and return oldest item (NULL if empty)       |
| `Peek()`     | Object  | Return oldest item without removing (NULL if empty) |
| `Get(index)` | Object  | Get item by logical index (0 = oldest)              |
| `Has(value)` | Boolean | Check if an element is in the ring (by reference)   |
| `Reverse()`  | void    | Reverse all elements in place                       |
| `SetOwnsElements(owns)` | void | Select owned or borrowed element mode while empty |
| `Clone()`    | Ring    | Create a shallow copy of the ring                   |
| `Clear()`    | void    | Remove all elements                                 |
| `ToSeq()`    | Seq     | Return all elements as a new Seq (oldest to newest) |

Rings retain stored runtime objects by default and release overwritten, popped, cleared, or finalized values. Runtime
callers can switch an empty ring to borrowed-element mode before pushing values with `SetOwnsElements(false)`.
`Get()`, `Peek()`, `First`, and `Last` return borrowed references; `Pop()` returns a retained
transfer in owning mode. `Clone()` preserves the source ring's ownership mode.

### Zia Example

```rust
module RingDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Text.Fmt as Fmt;

func start() {
    var ring = new Ring(3);

    ring.Push(Viper.Core.Box.Str("first"));
    ring.Push(Viper.Core.Box.Str("second"));
    ring.Push(Viper.Core.Box.Str("third"));
    Say("Count: " + Fmt.Int(ring.Count));         // 3
    Say("Full: " + Fmt.Bool(ring.IsFull));       // true

    // Overflow overwrites oldest
    ring.Push(Viper.Core.Box.Str("fourth"));
    Say("Oldest: " + Viper.Core.Box.ToStr(ring.Peek()));  // second

    // Pop removes oldest (FIFO)
    Say("Pop: " + Viper.Core.Box.ToStr(ring.Pop()));       // second
    Say("After pop: " + Fmt.Int(ring.Count));            // 2
}
```

### BASIC Example

```basic
' Create a ring buffer with capacity 3
DIM recent AS OBJECT = NEW Viper.Collections.Ring(3)

' Push some values
recent.Push("first")
recent.Push("second")
recent.Push("third")
PRINT recent.Count        ' Output: 3
PRINT recent.IsFull     ' Output: 1 (true)

' Push when full overwrites oldest
recent.Push("fourth")
PRINT recent.Count        ' Output: 3 (still 3)
PRINT recent.Peek()     ' Output: second (first was overwritten)

' Get by index (0 = oldest)
PRINT recent.Get(0)     ' Output: second
PRINT recent.Get(1)     ' Output: third
PRINT recent.Get(2)     ' Output: fourth

' Pop removes oldest (FIFO)
DIM oldest AS OBJECT = recent.Pop()
PRINT oldest            ' Output: second
PRINT recent.Count        ' Output: 2

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

## Viper.Collections.Heap

A priority queue implemented as a binary heap. Elements are stored with an integer priority value and retrieved in priority order. Supports both min-heap (smallest priority first) and max-heap (largest priority first) modes.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Heap()` (min-heap) or `Heap.NewMax(isMax)` for max-heap

### Properties

| Property  | Type    | Description                                    |
|-----------|---------|------------------------------------------------|
| `Count`   | Integer | Number of elements in the heap                 |
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
| `TryPopOption()`     | `Option[Object]()`     | Remove and return highest priority element, or `None` if empty |
| `TryPeekOption()`    | `Option[Object]()`     | Return highest priority element, or `None` if empty        |
| `Clear()`            | `Void()`               | Remove all elements                                        |
| `ToSeq()`            | `Seq()`                | Return elements in priority order as a Seq                 |

### Notes

- Heap retains pushed object values and releases them when removed, cleared, or finalized.
- `Pop()` and `TryPop()` transfer the heap's retained object reference to the caller. `Peek()` and `TryPeek()` return an additional owned reference without removing the item.
- Prefer `TryPopOption()` and `TryPeekOption()` for new code. They distinguish an empty heap from a stored null object; the nullable forms remain for compatibility.
- `ToSeq()` returns an independent owning snapshot in priority order.
- Equal-priority entries have no stable FIFO/LIFO guarantee.
- `ToSeq()` is registered as a typed sequence return, so chains such as `heap.ToSeq().Count`
  resolve against the returned Seq.

### Zia Example

```rust
module HeapDemo;

bind Viper.Terminal;
bind Viper.Collections;

func start() {
    var heap = new Heap();

    // Add tasks with priorities (lower = more urgent)
    heap.Push(3, Viper.Core.Box.Str("Low priority"));
    heap.Push(1, Viper.Core.Box.Str("Urgent"));
    heap.Push(2, Viper.Core.Box.Str("Medium"));

    // Pop returns in priority order (min-heap)
    Say(Viper.Core.Box.ToStr(heap.Pop()));   // Urgent
    Say(Viper.Core.Box.ToStr(heap.Pop()));   // Medium
    Say(Viper.Core.Box.ToStr(heap.Pop()));   // Low priority
}
```

### BASIC Example

```basic
DIM heap AS Viper.Collections.Heap
heap = NEW Viper.Collections.Heap()  ' Create min-heap

' Add tasks with priorities (lower = more urgent)
heap.Push(3, "Low priority task")
heap.Push(1, "Urgent task")
heap.Push(2, "Medium priority task")

PRINT heap.Count       ' Output: 3

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


## See Also

- [Maps & Sets](maps-sets.md)
- [Specialized Maps](multi-maps.md)
- [Functional & Lazy](functional.md)
- [Specialized Structures](specialized.md)
- [Collections Overview](README.md)
- [Viper Runtime Library](../README.md)
