# Specialized Maps
> BiMap, MultiMap, CountMap, IntMap, DefaultMap, LruCache, WeakMap, SparseArray

**Part of [Viper Runtime Library](../README.md) › [Collections](README.md)**

---

## Viper.Collections.BiMap

A bidirectional map that maintains a one-to-one mapping between keys and values, allowing efficient lookup in both
directions. Each key maps to exactly one value, and each value maps to exactly one key.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.BiMap.New()`

### Properties

| Property  | Type    | Description                          |
|-----------|---------|--------------------------------------|
| `Len`     | Integer | Number of key-value pairs in the map |
| `IsEmpty` | Boolean | True if the map has no entries       |

### Methods

| Method                 | Signature              | Description                                                       |
|------------------------|------------------------|-------------------------------------------------------------------|
| `Put(key, value)`      | `Void(String, String)` | Add or update a bidirectional mapping; removes any old mapping    |
| `GetByKey(key)`        | `String(String)`       | Get the value associated with a key                               |
| `GetByValue(value)`    | `String(String)`       | Get the key associated with a value (reverse lookup)              |
| `HasKey(key)`          | `Boolean(String)`      | Check if a key exists                                             |
| `HasValue(value)`      | `Boolean(String)`      | Check if a value exists                                           |
| `Keys()`               | `Seq()`                | Get all keys as a Seq                                             |
| `Values()`             | `Seq()`                | Get all values as a Seq                                           |
| `RemoveByKey(key)`     | `Boolean(String)`      | Remove a mapping by key; returns true if found                    |
| `RemoveByValue(value)` | `Boolean(String)`      | Remove a mapping by value; returns true if found                  |
| `Clear()`              | `Void()`               | Remove all mappings                                               |

### Notes

- Maintains strict one-to-one relationships: updating a key to a new value removes the old value's reverse mapping
- Both key-to-value and value-to-key lookups are O(1) average case
- `Put` with an existing key replaces the old value and removes the old value's reverse entry
- Keys and values are both strings

### Zia Example

```rust
module BiMapDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    var bm = BiMap.New();

    // Add bidirectional mappings
    bm.Put("en", "English");
    bm.Put("fr", "French");
    Say("Len: " + Fmt.Int(bm.Len));                  // 2

    // Forward lookup (key -> value)
    Say("GetByKey en: " + bm.GetByKey("en"));         // English

    // Reverse lookup (value -> key)
    Say("GetByValue French: " + bm.GetByValue("French")); // fr

    // Membership checks
    Say(Fmt.Bool(bm.HasKey("en")));                   // true
    Say(Fmt.Bool(bm.HasValue("English")));            // true

    // Remove by key
    bm.RemoveByKey("en");
    SayInt(bm.Len);                                   // 1
}
```

### BASIC Example

```basic
DIM bm AS OBJECT
bm = Viper.Collections.BiMap.New()

' Add bidirectional mappings
bm.Put("en", "English")
bm.Put("fr", "French")
PRINT "Len: "; bm.Len                       ' Len: 2

' Forward lookup (key -> value)
PRINT "GetByKey en: "; bm.GetByKey("en")     ' GetByKey en: English

' Reverse lookup (value -> key)
PRINT "GetByValue French: "; bm.GetByValue("French")  ' GetByValue French: fr

' Membership checks
PRINT "HasKey fr: "; bm.HasKey("fr")         ' HasKey fr: -1
PRINT "HasValue English: "; bm.HasValue("English")  ' HasValue English: -1

' Remove by key
bm.RemoveByKey("en")
PRINT "After remove: "; bm.Len              ' After remove: 1
```

### Use Cases

- **Locale/language maps:** Map language codes to names and vice versa
- **Encoding tables:** Bidirectional lookup between encoded and decoded values
- **Identifier mapping:** Two-way mapping between internal IDs and external names
- **Bijective transformations:** Any scenario requiring invertible mappings

---

## Viper.Collections.MultiMap

A map that associates each string key with multiple values. Unlike Map which stores one value per key, MultiMap
stores a list of values for each key.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.MultiMap.New()`

### Properties

| Property   | Type    | Description                                   |
|------------|---------|-----------------------------------------------|
| `IsEmpty`  | Boolean | True if the map has no entries                |
| `KeyCount` | Integer | Number of distinct keys                       |
| `Len`      | Integer | Total number of values across all keys        |

### Methods

| Method             | Signature              | Description                                                      |
|--------------------|------------------------|------------------------------------------------------------------|
| `Put(key, value)`  | `Void(String, Object)` | Add a value to the key's list (does not replace existing values) |
| `Get(key)`         | `Seq(String)`          | Get all values for key as a Seq (empty Seq if key not found)     |
| `GetFirst(key)`    | `Object(String)`       | Get the first value added for key                                |
| `Has(key)`         | `Boolean(String)`      | Check if key exists                                              |
| `CountFor(key)`    | `Integer(String)`      | Get the number of values for a key (0 if not found)              |
| `Keys()`           | `Seq()`                | Get all distinct keys as a Seq                                   |
| `RemoveAll(key)`   | `Boolean(String)`      | Remove a key and all its values; returns true if found           |
| `Clear()`          | `Void()`               | Remove all entries                                               |

### Notes

- `Len` is the *total* number of values across all keys, not the number of distinct keys
- `KeyCount` gives the number of distinct keys
- `Get` returns an empty Seq (not null) if the key does not exist
- Values are boxed objects in Zia (use `Viper.Core.Box`); BASIC auto-boxes string values

### Zia Example

```rust
module MultiMapDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    var mm = MultiMap.New();

    // Add multiple values per key
    mm.Put("color", Box.Str("red"));
    mm.Put("color", Box.Str("green"));
    mm.Put("color", Box.Str("blue"));
    mm.Put("size", Box.Str("small"));
    mm.Put("size", Box.Str("large"));

    SayInt(mm.Len);                              // 5 (total values)
    SayInt(mm.KeyCount);                         // 2 (distinct keys)

    // Get all values for a key
    var colors = mm.Get("color");
    SayInt(colors.Len);                          // 3
    Say(Box.ToStr(colors.Get(0)));               // red
    Say(Box.ToStr(colors.Get(1)));               // green
    Say(Box.ToStr(colors.Get(2)));               // blue

    // Get first value for a key
    Say(Box.ToStr(mm.GetFirst("color")));        // red

    // Count values for a key
    SayInt(mm.CountFor("color"));                // 3
    SayInt(mm.CountFor("shape"));                // 0

    // Remove all values for a key
    mm.RemoveAll("color");
    SayInt(mm.Len);                              // 2
    SayInt(mm.KeyCount);                         // 1
}
```

### BASIC Example

```basic
DIM mm AS OBJECT
mm = Viper.Collections.MultiMap.New()

' Add multiple values per key
mm.Put("color", "red")
mm.Put("color", "green")
mm.Put("color", "blue")
mm.Put("size", "small")
mm.Put("size", "large")

PRINT mm.Len             ' 5 (total values)
PRINT mm.KeyCount        ' 2 (distinct keys)

' Get all values for a key
DIM colors AS Viper.Collections.Seq
colors = mm.Get("color")
PRINT colors.Len         ' 3
PRINT colors.Get(0)      ' red
PRINT colors.Get(1)      ' green
PRINT colors.Get(2)      ' blue

' Get first value
PRINT mm.GetFirst("color")   ' red
PRINT mm.GetFirst("size")    ' small

' Count values for a key
PRINT mm.CountFor("color")   ' 3
PRINT mm.CountFor("shape")   ' 0

' Get for missing key returns empty Seq
DIM empty AS Viper.Collections.Seq
empty = mm.Get("shape")
PRINT empty.Len              ' 0

' Remove all values for a key
PRINT mm.RemoveAll("color")  ' 1
PRINT mm.Len                 ' 2
PRINT mm.KeyCount            ' 1

' Clear all
mm.Clear()
PRINT mm.IsEmpty             ' 1
```

### Use Cases

- **HTTP headers:** Multiple values for the same header name
- **Tag systems:** Associate multiple tags with each item
- **Graph adjacency lists:** Store multiple edges per vertex
- **Index building:** Map each term to multiple document IDs
- **Event handlers:** Register multiple handlers for each event type

---

## Viper.Collections.CountMap

A frequency counter that maps string keys to integer counts. Provides convenient increment, decrement, and
ranking operations for counting occurrences.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.CountMap.New()`

### Properties

| Property  | Type    | Description                                |
|-----------|---------|--------------------------------------------|
| `IsEmpty` | Boolean | True if the map has no entries             |
| `Len`     | Integer | Number of distinct keys in the map         |
| `Total`   | Integer | Sum of all counts across all keys          |

### Methods

| Method              | Signature               | Description                                                         |
|---------------------|-------------------------|---------------------------------------------------------------------|
| `Inc(key)`          | `Integer(String)`       | Increment count for key by 1; returns new count                     |
| `Dec(key)`          | `Integer(String)`       | Decrement count for key by 1; removes key if count reaches 0        |
| `IncBy(key, n)`     | `Integer(String, Integer)` | Increment count for key by n; returns new count                  |
| `Get(key)`          | `Integer(String)`       | Get current count for key (0 if not present)                        |
| `Set(key, count)`   | `Void(String, Integer)` | Set count for key directly                                          |
| `Has(key)`          | `Boolean(String)`       | Check if key exists in the map                                      |
| `Remove(key)`       | `Boolean(String)`       | Remove a key entirely; returns true if found                        |
| `Keys()`            | `Seq()`                 | Get all keys as a Seq                                               |
| `MostCommon(n)`     | `Seq(Integer)`          | Get top n keys sorted by count descending                           |
| `Clear()`           | `Void()`                | Remove all entries                                                  |

### Notes

- `Dec` automatically removes a key when its count reaches zero
- `Get` returns 0 for keys that have never been added (does not insert)
- `MostCommon` returns keys ordered from highest count to lowest
- `Total` is the sum of all counts, not the number of distinct keys

### Zia Example

```rust
module CountMapDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    var cm = CountMap.New();

    // Count occurrences
    cm.Inc("apple");
    cm.Inc("apple");
    cm.Inc("banana");
    cm.Inc("cherry");
    cm.Inc("cherry");
    cm.Inc("cherry");
    SayInt(cm.Len);                // 3 (distinct keys)

    // Bulk increment
    cm.IncBy("banana", 5);
    SayInt(cm.Get("banana"));      // 6

    // Total across all keys
    SayInt(cm.Total);              // 2 + 6 + 3 = 11

    // Most common
    var top = cm.MostCommon(2);
    SayInt(top.Len);               // 2

    // Decrement removes at zero
    cm.Dec("apple");               // count -> 1
    cm.Dec("apple");               // count -> 0, removed
    SayBool(cm.Has("apple"));      // 0
}
```

### BASIC Example

```basic
DIM cm AS OBJECT
cm = Viper.Collections.CountMap.New()

' Count word occurrences
cm.Inc("apple")
cm.Inc("apple")
cm.Inc("banana")
cm.Inc("cherry")
cm.Inc("cherry")
cm.Inc("cherry")
PRINT cm.Len                ' 3 (distinct keys)

' Bulk increment
cm.IncBy("banana", 5)
PRINT cm.Get("banana")      ' 6

' Total of all counts
PRINT cm.Total               ' 11

' Set a count directly
cm.Set("date", 10)
PRINT cm.Get("date")        ' 10

' Decrement (removes key when count reaches 0)
cm.Dec("apple")              ' 1
cm.Dec("apple")              ' 0 (removed)
PRINT cm.Has("apple")        ' 0

' Get top entries
DIM top AS OBJECT
top = cm.MostCommon(2)
PRINT top.Len                ' 2

' Remove a key entirely
cm.Remove("date")
PRINT cm.Has("date")         ' 0

' Clear all
cm.Clear()
PRINT cm.IsEmpty             ' 1
```

### Use Cases

- **Word frequency analysis:** Count occurrences of words in text
- **Vote counting:** Tally votes for candidates
- **Event tracking:** Count how often each event type occurs
- **Histogram building:** Build frequency distributions
- **Top-N queries:** Find the most common items with `MostCommon`

---

## Viper.Collections.IntMap

An integer-keyed dictionary for efficient mapping of integer keys to object values. Uses a hash table with O(1) average-case operations.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.IntMap.New()`

### Properties

| Property  | Type    | Description                            |
|-----------|---------|----------------------------------------|
| `Len`     | Integer | Number of key-value pairs in the map   |
| `IsEmpty` | Boolean | True if the map has no entries         |

### Methods

| Method                        | Signature                  | Description                                                              |
|-------------------------------|----------------------------|--------------------------------------------------------------------------|
| `Set(key, value)`             | `Void(Integer, Object)`    | Add or update a key-value pair                                           |
| `Get(key)`                    | `Object(Integer)`          | Get value for key (returns NULL if not found)                            |
| `GetOr(key, default)`         | `Object(Integer, Object)`  | Get value for key, or return `default` if missing (does not insert)      |
| `Has(key)`                    | `Boolean(Integer)`         | Check if key exists                                                      |
| `Remove(key)`                 | `Boolean(Integer)`         | Remove key-value pair; returns true if found                             |
| `Clear()`                     | `Void()`                   | Remove all entries                                                       |
| `Keys()`                      | `Seq()`                    | Get sequence of all keys                                                 |
| `Values()`                    | `Seq()`                    | Get sequence of all values                                               |

### Zia Example

```rust
module IntMapDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    var m = IntMap.New();

    // Add entries with integer keys
    m.Set(1, Box.Str("one"));
    m.Set(2, Box.Str("two"));
    m.Set(3, Box.Str("three"));

    Say("Len: " + Fmt.Int(m.Len));                  // 3
    Say("Has 2: " + Fmt.Bool(m.Has(2)));             // true
    Say("Get 1: " + Box.ToStr(m.Get(1)));            // one

    // Remove
    m.Remove(2);
    Say("Has 2: " + Fmt.Bool(m.Has(2)));             // false
}
```

### BASIC Example

```basic
DIM m AS OBJECT = Viper.Collections.IntMap.New()

' Add entries
m.Set(100, "apple")
m.Set(200, "banana")
m.Set(300, "cherry")

PRINT m.Len      ' Output: 3
PRINT m.IsEmpty  ' Output: 0

' Check existence and get value
IF m.Has(100) THEN
    PRINT m.Get(100)  ' Output: apple
END IF

' Get with default
PRINT m.GetOr(999, "unknown")  ' Output: unknown

' Remove
IF m.Remove(200) THEN
    PRINT "Removed 200"
END IF

' Iterate keys
DIM keys AS OBJECT = m.Keys()
FOR i = 0 TO keys.Len - 1
    PRINT keys.Get(i)
NEXT i

' Clear all
m.Clear()
PRINT m.IsEmpty  ' Output: 1
```

### Use Cases

- **Sparse arrays:** Map non-contiguous integer indices to values
- **ID-based lookup:** Look up objects by numeric identifier
- **Counting by ID:** Count occurrences keyed by integer values
- **Graph adjacency:** Map node IDs to neighbor lists

---

## Viper.Collections.DefaultMap

A map that returns a configurable default value for missing keys instead of null. The default value is a boxed
value specified at construction time via `Viper.Core.Box`.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.DefaultMap.New(defaultValue)` - `defaultValue` is a boxed value (e.g., `Box.Str("N/A")`, `Box.I64(0)`)

### Properties

| Property | Type    | Description                        |
|----------|---------|------------------------------------|
| `Len`    | Integer | Number of key-value pairs in the map |

### Methods

| Method            | Signature              | Description                                                   |
|-------------------|------------------------|---------------------------------------------------------------|
| `Get(key)`        | `Object(String)`       | Get value for key, or the default value if key is missing     |
| `Set(key, value)` | `Void(String, Object)` | Set a key-value pair                                          |
| `Has(key)`        | `Boolean(String)`      | Check if a key exists (Get does not insert missing keys)      |
| `Remove(key)`     | `Integer(String)`      | Remove a key-value pair; returns 1 if found, 0 if not        |
| `Keys()`          | `Seq()`                | Get all keys as a Seq                                         |
| `GetDefault()`    | `Object()`             | Get the default value configured at construction              |
| `Clear()`         | `Void()`               | Remove all entries                                            |

### Notes

- `Get` for a missing key returns the default value but does *not* insert the key into the map
- The default value is set at construction time and cannot be changed
- Values are boxed objects; use `Viper.Core.Box` to create and unwrap values
- In BASIC, string values are automatically boxed when passed to `Set`

### Zia Example

```rust
module DefaultMapDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    // Create with string default
    var dm = DefaultMap.New(Box.Str("N/A"));

    dm.Set("name", Box.Str("Alice"));
    dm.Set("city", Box.Str("Boston"));
    SayInt(dm.Len);                            // 2

    // Existing key returns stored value
    Say(Box.ToStr(dm.Get("name")));            // Alice

    // Missing key returns default
    Say(Box.ToStr(dm.Get("email")));           // N/A
    SayBool(dm.Has("email"));                  // 0 (not inserted)

    // Get the default value
    Say(Box.ToStr(dm.GetDefault()));           // N/A

    // Integer default
    var dm2 = DefaultMap.New(Box.I64(0));
    dm2.Set("count", Box.I64(42));
    SayInt(Box.ToI64(dm2.Get("count")));       // 42
    SayInt(Box.ToI64(dm2.Get("missing")));     // 0
}
```

### BASIC Example

```basic
' Create with string default
DIM dm AS OBJECT
dm = Viper.Collections.DefaultMap.New(Viper.Core.Box.Str("N/A"))

dm.Set("name", "Alice")
dm.Set("city", "Boston")
PRINT dm.Len                  ' 2

' Existing key returns stored value
PRINT dm.Get("name")          ' Alice

' Missing key returns default (does not insert)
PRINT dm.Get("email")         ' N/A
PRINT dm.Has("email")         ' 0

' Get the default value
PRINT dm.GetDefault()         ' N/A

' Remove a key
PRINT dm.Remove("city")       ' 1
PRINT dm.Get("city")          ' N/A (returns default now)

' Integer default
DIM dm2 AS OBJECT
dm2 = Viper.Collections.DefaultMap.New(Viper.Core.Box.I64(0))
dm2.Set("count", Viper.Core.Box.I64(42))
PRINT Viper.Core.Box.ToI64(dm2.Get("count"))     ' 42
PRINT Viper.Core.Box.ToI64(dm2.Get("missing"))   ' 0

' Clear all
dm.Clear()
PRINT dm.Len                  ' 0
PRINT dm.Get("name")          ' N/A
```

### Use Cases

- **Configuration with fallbacks:** Access settings with sensible defaults for missing keys
- **Sparse data:** Represent data where most entries have the same default value
- **Counter initialization:** Use integer default of 0 to avoid checking for key existence
- **Template rendering:** Default to placeholder text for missing template variables

---

## Viper.Collections.LruCache

A fixed-capacity cache that evicts the least recently used (LRU) entry when full. Supports O(1) get, put, and
eviction operations. Values are accessed by string keys.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.LruCache.New(capacity)` - creates a cache with the given maximum capacity

### Properties

| Property  | Type    | Description                               |
|-----------|---------|-------------------------------------------|
| `Cap`     | Integer | Maximum capacity (fixed at creation)      |
| `IsEmpty` | Boolean | True if the cache has no entries          |
| `Len`     | Integer | Number of entries currently in the cache  |

### Methods

| Method              | Signature              | Description                                                            |
|---------------------|------------------------|------------------------------------------------------------------------|
| `Put(key, value)`   | `Void(String, Object)` | Add or update an entry; evicts LRU entry if at capacity                |
| `Get(key)`          | `Object(String)`       | Get value for key and promote to most recently used; null if not found |
| `Has(key)`          | `Boolean(String)`      | Check if key exists in the cache                                       |
| `Peek(key)`         | `Object(String)`       | Get value for key without promoting (does not affect LRU order)        |
| `Remove(key)`       | `Boolean(String)`      | Remove an entry; returns true if found                                 |
| `RemoveOldest()`    | `Boolean()`            | Remove the least recently used entry; returns true if cache was non-empty |
| `Keys()`            | `Seq()`                | Get all keys as a Seq (MRU to LRU order)                              |
| `Values()`          | `Seq()`                | Get all values as a Seq (MRU to LRU order)                            |
| `Clear()`           | `Void()`               | Remove all entries (capacity is preserved)                             |

### Notes

- `Get` promotes the accessed entry to most recently used; `Peek` does not
- When `Put` is called at capacity, the least recently used entry is automatically evicted
- Updating an existing key with `Put` promotes it to most recently used without eviction
- Values are boxed objects in Zia (use `Viper.Core.Box`); BASIC auto-boxes string values

### Zia Example

```rust
module LruCacheDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    var cache = LruCache.New(3);
    SayInt(cache.Cap);                             // 3

    // Add entries
    cache.Put("a", Box.Str("alpha"));
    cache.Put("b", Box.Str("beta"));
    cache.Put("c", Box.Str("gamma"));
    SayInt(cache.Len);                             // 3

    // Get promotes to MRU
    Say(Box.ToStr(cache.Get("a")));                // alpha

    // Peek does not promote
    Say(Box.ToStr(cache.Peek("c")));               // gamma

    // Adding a 4th entry evicts LRU (c, since Peek didn't promote it)
    cache.Put("d", Box.Str("delta"));
    SayBool(cache.Has("c"));                       // 0 (evicted)
    SayBool(cache.Has("d"));                       // 1

    // Remove specific entry
    cache.Remove("b");
    SayInt(cache.Len);                             // 2

    // Clear all
    cache.Clear();
    SayBool(cache.IsEmpty);                        // 1
    SayInt(cache.Cap);                             // 3 (capacity preserved)
}
```

### BASIC Example

```basic
DIM cache AS OBJECT
cache = Viper.Collections.LruCache.New(3)
PRINT cache.Cap          ' 3

' Add entries
cache.Put("a", "alpha")
cache.Put("b", "beta")
cache.Put("c", "gamma")
PRINT cache.Len          ' 3

' Get promotes to most recently used
PRINT cache.Get("a")     ' alpha
PRINT cache.Get("b")     ' beta

' Peek does NOT promote
PRINT cache.Peek("c")    ' gamma

' Adding when full evicts LRU (c was not promoted by Peek)
cache.Put("d", "delta")
PRINT cache.Len          ' 3 (still at capacity)
PRINT cache.Has("c")     ' 0 (evicted)
PRINT cache.Has("d")     ' 1

' Update existing entry (no eviction)
cache.Put("a", "ALPHA")
PRINT cache.Get("a")     ' ALPHA
PRINT cache.Len          ' 3

' Remove specific entry
PRINT cache.Remove("b")  ' 1
PRINT cache.Len          ' 2

' Remove oldest (LRU) entry
cache.Put("e", "epsilon")
PRINT cache.RemoveOldest() ' 1
PRINT cache.Len          ' 2

' Clear all
cache.Clear()
PRINT cache.IsEmpty      ' 1
PRINT cache.Cap          ' 3
```

### Use Cases

- **Database query caching:** Cache recent query results with automatic eviction
- **Web page caching:** Keep recently accessed pages in memory
- **DNS caching:** Cache recent DNS lookups with bounded memory
- **Session management:** Track active sessions with automatic expiration of idle ones
- **Memoization:** Cache function results with bounded memory usage

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

### Zia Example

> WeakMap is not yet available as a constructible type in Zia. Use BASIC or access via the runtime API.

### BASIC Example

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

## Viper.Collections.SparseArray

An array-like data structure that efficiently stores values at arbitrary integer indices without allocating memory
for gaps. Only occupied indices consume storage.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.SparseArray.New()`

### Properties

| Property | Type    | Description                                  |
|----------|---------|----------------------------------------------|
| `Len`    | Integer | Number of elements stored (occupied indices) |

### Methods

| Method             | Signature               | Description                                            |
|--------------------|-------------------------|--------------------------------------------------------|
| `Set(index, value)`| `Void(Integer, Object)` | Set value at the given index                           |
| `Get(index)`       | `Object(Integer)`       | Get value at the given index (null if not set)         |
| `Has(index)`       | `Boolean(Integer)`      | Check if an index has a value                          |
| `Remove(index)`    | `Boolean(Integer)`      | Remove value at index; returns true if found           |
| `Indices()`        | `Seq()`                 | Get all occupied indices as a Seq                      |
| `Values()`         | `Seq()`                 | Get all values as a Seq                                |
| `Clear()`          | `Void()`                | Remove all entries                                     |

### Notes

- Indices can be any integer, including negative numbers (e.g., -5, 0, 1000)
- Only occupied indices consume memory; gaps between indices cost nothing
- `Indices()` returns the set of indices that have been assigned values
- Values are boxed objects in Zia (use `Viper.Core.Box`); BASIC auto-boxes string values

### Zia Example

```rust
module SparseArrayDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    var sa = SparseArray.New();

    // Set values at sparse indices
    sa.Set(0, Box.Str("zero"));
    sa.Set(100, Box.Str("hundred"));
    sa.Set(1000, Box.Str("thousand"));
    sa.Set(-5, Box.Str("negative"));
    SayInt(sa.Len);                               // 4

    // Retrieve values
    Say(Box.ToStr(sa.Get(0)));                    // zero
    Say(Box.ToStr(sa.Get(100)));                  // hundred
    Say(Box.ToStr(sa.Get(-5)));                   // negative

    // Check existence
    SayBool(sa.Has(100));                         // 1
    SayBool(sa.Has(50));                          // 0

    // Update existing index
    sa.Set(100, Box.Str("HUNDRED"));
    Say(Box.ToStr(sa.Get(100)));                  // HUNDRED
    SayInt(sa.Len);                               // 4 (no new entry)

    // Remove
    sa.Remove(1000);
    SayInt(sa.Len);                               // 3

    // Get all indices and values
    var indices = sa.Indices();
    SayInt(indices.Len);                          // 3
}
```

### BASIC Example

```basic
DIM sa AS OBJECT
sa = Viper.Collections.SparseArray.New()

' Set values at sparse indices
sa.Set(0, "zero")
sa.Set(100, "hundred")
sa.Set(1000, "thousand")
sa.Set(-5, "negative")
PRINT sa.Len             ' 4

' Retrieve values
PRINT sa.Get(0)          ' zero
PRINT sa.Get(100)        ' hundred
PRINT sa.Get(1000)       ' thousand
PRINT sa.Get(-5)         ' negative

' Check existence
PRINT sa.Has(100)        ' 1
PRINT sa.Has(50)         ' 0

' Update existing
sa.Set(100, "HUNDRED")
PRINT sa.Get(100)        ' HUNDRED
PRINT sa.Len             ' 4

' Remove
PRINT sa.Remove(1000)    ' 1
PRINT sa.Has(1000)       ' 0
PRINT sa.Len             ' 3

' Get all indices and values
DIM indices AS OBJECT
indices = sa.Indices()
PRINT indices.Len        ' 3

DIM vals AS OBJECT
vals = sa.Values()
PRINT vals.Len           ' 3

' Clear all
sa.Clear()
PRINT sa.Len             ' 0
```

### Use Cases

- **Game maps:** Store tile data at arbitrary 2D coordinates (using computed indices)
- **Sparse matrices:** Represent matrices where most entries are zero
- **Event scheduling:** Map time slots to events without allocating empty slots
- **Lookup tables:** Map non-contiguous IDs to objects efficiently
- **Dynamic arrays:** Store data at arbitrary positions without pre-allocation

---


## See Also

- [Sequential Collections](sequential.md)
- [Maps & Sets](maps-sets.md)
- [Functional & Lazy](functional.md)
- [Specialized Structures](specialized.md)
- [Collections Overview](README.md)
- [Viper Runtime Library](../README.md)
