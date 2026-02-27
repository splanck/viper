# Maps & Sets
> Map, Set, OrderedMap, SortedSet, FrozenMap, FrozenSet, TreeMap

**Part of [Viper Runtime Library](../README.md) › [Collections](README.md)**

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

### Typed Accessors

Convenience methods for storing and retrieving typed values without manual boxing/unboxing.

| Method                          | Signature                    | Description                                                       |
|---------------------------------|------------------------------|-------------------------------------------------------------------|
| `SetInt(key, value)`            | `Void(String, Integer)`      | Store an integer value                                            |
| `GetInt(key)`                   | `Integer(String)`            | Get an integer value (returns 0 if key not found)                 |
| `GetIntOr(key, default)`        | `Integer(String, Integer)`   | Get an integer value, or return `default` if missing              |
| `SetFloat(key, value)`          | `Void(String, Number)`       | Store a floating-point value                                      |
| `GetFloat(key)`                 | `Number(String)`             | Get a floating-point value (returns 0.0 if key not found)         |
| `GetFloatOr(key, default)`      | `Number(String, Number)`     | Get a floating-point value, or return `default` if missing        |
| `SetStr(key, value)`            | `Void(String, String)`       | Store a string value                                              |
| `GetStr(key)`                   | `String(String)`             | Get a string value (returns empty string if key not found)        |

### Zia Example

```rust
module MapDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    var scores = new Map[String, Integer]();

    // Add entries
    scores.set("Alice", 95);
    scores.set("Bob", 87);
    scores.set("Carol", 92);

    // Check and retrieve
    Say("Has Alice: " + Fmt.Bool(scores.has("Alice")));  // true
    Say("Alice: " + Fmt.Int(scores.get("Alice")));       // 95

    // Update
    scores.set("Bob", 91);
    Say("Bob updated: " + Fmt.Int(scores.get("Bob")));   // 91

    // Remove
    scores.remove("Carol");
    Say("Has Carol: " + Fmt.Bool(scores.has("Carol")));  // false
}
```

### BASIC Example

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
| `Add(obj)`          | `Boolean(Object)` | Add an object; returns true if new, false if already present   |
| `Remove(obj)`       | `Boolean(Object)` | Remove an object; returns true if removed, false if not found  |
| `Has(obj)`          | `Boolean(Object)` | Check if object is in the set                                  |
| `Clear()`           | `Void()`          | Remove all objects from the set                                |
| `Items()`           | `Seq()`           | Get all objects as a Seq (order undefined)                     |
| `Union(other)`      | `Set(Set)`        | Return new set with union of both sets                         |
| `Intersect(other)`  | `Set(Set)`        | Return new set with intersection of both sets                  |
| `Diff(other)`       | `Set(Set)`        | Return new set with elements in this but not other             |
| `IsSubset(other)`   | `Boolean(Set)`    | True if this set is a subset of other                          |
| `IsSuperset(other)` | `Boolean(Set)`    | True if this set is a superset of other                        |
| `IsDisjoint(other)` | `Boolean(Set)`    | True if sets have no elements in common                        |

### Notes

- Objects are compared by reference identity, not value equality
- Order of objects returned by `Items()` is not guaranteed (hash table)
- Set operations (`Union`, `Intersect`, `Diff`) return new sets; originals are unchanged
- Uses object identity hash for O(1) average-case operations
- Automatically resizes when load factor exceeds threshold

### Zia Example

> Set is not yet available as a constructible type in Zia. Use BASIC or access via the runtime API.

### BASIC Example

```basic
' Create and populate a set
DIM items AS OBJECT = Viper.Collections.Set.New()
DIM a AS OBJECT = Viper.Core.Box.I64(1)
DIM b AS OBJECT = Viper.Core.Box.I64(2)
DIM c AS OBJECT = Viper.Core.Box.I64(3)

items.Add(a)
items.Add(b)
items.Add(c)
PRINT items.Len           ' Output: 3

' Duplicate add returns false
DIM wasNew AS INTEGER = items.Add(a)
PRINT wasNew              ' Output: 0 (already present)

' Membership testing
PRINT items.Has(b)        ' Output: 1 (true)
DIM d AS OBJECT = Viper.Core.Box.I64(4)
PRINT items.Has(d)        ' Output: 0 (false)

' Remove an element
DIM removed AS INTEGER = items.Remove(b)
PRINT removed             ' Output: 1 (was removed)
PRINT items.Has(b)        ' Output: 0 (no longer present)

' Set operations
DIM setA AS OBJECT = Viper.Collections.Set.New()
DIM x AS OBJECT = Viper.Core.Box.Str("x")
DIM y AS OBJECT = Viper.Core.Box.Str("y")
DIM z AS OBJECT = Viper.Core.Box.Str("z")
DIM w AS OBJECT = Viper.Core.Box.Str("w")
setA.Add(x)
setA.Add(y)
setA.Add(z)

DIM setB AS OBJECT = Viper.Collections.Set.New()
setB.Add(y)
setB.Add(z)
setB.Add(w)

' Union: elements in either set
DIM merged AS OBJECT = setA.Union(setB)
PRINT merged.Len          ' Output: 4 (x, y, z, w)

' Intersection: elements in both sets
DIM common AS OBJECT = setA.Intersect(setB)
PRINT common.Len          ' Output: 2 (y, z)

' Difference: elements in A but not B
DIM diff AS OBJECT = setA.Diff(setB)
PRINT diff.Len            ' Output: 1 (x only)

' Subset/superset checks
DIM subset AS OBJECT = Viper.Collections.Set.New()
subset.Add(y)
subset.Add(z)
PRINT subset.IsSubset(setA)     ' Output: 1 (true)
PRINT setA.IsSuperset(subset)   ' Output: 1 (true)

' Disjoint check
DIM disjoint AS OBJECT = Viper.Collections.Set.New()
disjoint.Add(w)
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

## Viper.Collections.OrderedMap

A key-value map that maintains insertion order. Keys are iterated in the order they were first inserted,
regardless of updates.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.OrderedMap.New()`

### Properties

| Property  | Type    | Description                            |
|-----------|---------|----------------------------------------|
| `IsEmpty` | Boolean | True if the map has no entries         |
| `Len`     | Integer | Number of key-value pairs in the map   |

### Methods

| Method            | Signature              | Description                                                       |
|-------------------|------------------------|-------------------------------------------------------------------|
| `Set(key, value)` | `Void(String, Object)` | Add or update a key-value pair (preserves original insertion order)|
| `Get(key)`        | `Object(String)`       | Get value for key (null if not found)                             |
| `Has(key)`        | `Boolean(String)`      | Check if key exists                                               |
| `KeyAt(index)`    | `String(Integer)`      | Get the key at the given position in insertion order              |
| `Keys()`          | `Seq()`                | Get all keys in insertion order                                   |
| `Values()`        | `Seq()`                | Get all values in insertion order                                 |
| `Remove(key)`     | `Integer(String)`      | Remove a key-value pair; returns 1 if found, 0 if not            |
| `Clear()`         | `Void()`               | Remove all entries                                                |

### Notes

- Updating an existing key's value does *not* change its position in the insertion order
- `KeyAt` provides O(1) access to keys by their insertion position
- After removing a key, subsequent keys shift down in their positional indices
- Values are boxed objects in Zia (use `Viper.Core.Box`); BASIC auto-boxes string values

### Zia Example

```rust
module OrderedMapDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    var om = OrderedMap.New();

    // Insert in specific order
    om.Set("first", Box.Str("alpha"));
    om.Set("second", Box.Str("beta"));
    om.Set("third", Box.Str("gamma"));
    SayInt(om.Len);                             // 3

    // Access by key
    Say(Box.ToStr(om.Get("first")));            // alpha

    // Access by insertion position
    Say(om.KeyAt(0));                           // first
    Say(om.KeyAt(1));                           // second
    Say(om.KeyAt(2));                           // third

    // Update preserves insertion order
    om.Set("second", Box.Str("BETA"));
    Say(om.KeyAt(1));                           // second (still position 1)
    Say(Box.ToStr(om.Get("second")));           // BETA

    // Keys and values in insertion order
    var keys = om.Keys();
    Say(Box.ToStr(keys.Get(0)));                // first
    Say(Box.ToStr(keys.Get(1)));                // second
    Say(Box.ToStr(keys.Get(2)));                // third
}
```

### BASIC Example

```basic
DIM om AS OBJECT
om = Viper.Collections.OrderedMap.New()

' Insert in specific order
om.Set("first", "alpha")
om.Set("second", "beta")
om.Set("third", "gamma")
PRINT om.Len              ' 3

' Access by key
PRINT om.Get("first")     ' alpha
PRINT om.Get("second")    ' beta

' Access by insertion position
PRINT om.KeyAt(0)         ' first
PRINT om.KeyAt(1)         ' second
PRINT om.KeyAt(2)         ' third

' Update preserves order
om.Set("second", "BETA")
PRINT om.Get("second")    ' BETA
PRINT om.KeyAt(1)         ' second (still position 1)

' Keys in insertion order
DIM keys AS OBJECT
keys = om.Keys()
PRINT keys.Len            ' 3
PRINT keys.Get(0)         ' first
PRINT keys.Get(1)         ' second
PRINT keys.Get(2)         ' third

' Values in insertion order
DIM vals AS OBJECT
vals = om.Values()
PRINT vals.Get(0)         ' alpha
PRINT vals.Get(1)         ' BETA
PRINT vals.Get(2)         ' gamma

' Remove
PRINT om.Remove("second") ' 1
PRINT om.Len              ' 2
DIM keys2 AS OBJECT
keys2 = om.Keys()
PRINT keys2.Get(0)        ' first
PRINT keys2.Get(1)        ' third

' Clear
om.Clear()
PRINT om.IsEmpty          ' 1
```

### OrderedMap vs Map vs TreeMap

| Feature           | OrderedMap       | Map              | TreeMap          |
|-------------------|------------------|------------------|------------------|
| Key order         | Insertion order  | Unordered        | Sorted order     |
| Lookup            | O(1) average     | O(1) average     | O(log n)         |
| Insert            | O(1) average     | O(1) average     | O(n)             |
| Positional access | O(1) via KeyAt   | Not available    | Not available    |

### Use Cases

- **JSON-like serialization:** Preserve key order for deterministic output
- **Configuration files:** Maintain the order of settings as authored
- **Form processing:** Process form fields in submission order
- **Audit trails:** Track entries in the order they were created

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
| `Union(other)`      | `SortedSet(SortedSet)`     | Return new set with union of both sets                             |
| `Intersect(other)`  | `SortedSet(SortedSet)`     | Return new set with intersection of both sets                      |
| `Diff(other)`       | `SortedSet(SortedSet)`     | Return new set with elements in this but not other                 |
| `IsSubset(other)`   | `Boolean(SortedSet)`       | True if this set is a subset of other                              |

### Zia Example

> SortedSet is not yet available as a constructible type in Zia. Use BASIC or access via the runtime API.

### BASIC Example

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
PRINT words.Floor("cat")       ' Output: "banana" (largest <= "cat")
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
DIM merged AS OBJECT = set1.Union(set2)
PRINT merged.Len         ' Output: 4 (a, b, c, d)

' Intersection
DIM common AS OBJECT = set1.Intersect(set2)
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

## Viper.Collections.FrozenMap

An immutable key-value map. Once created, entries cannot be added, removed, or modified. Supports efficient
lookup, merging (which returns a new FrozenMap), and equality comparison.

**Type:** Instance (obj)
**Constructors:**

- `Viper.Collections.FrozenMap.FromSeqs(keys, values)` - Create from two parallel Seq objects
- `Viper.Collections.FrozenMap.Empty()` - Create an empty immutable map

### Properties

| Property  | Type    | Description                            |
|-----------|---------|----------------------------------------|
| `IsEmpty` | Boolean | True if the map has no entries         |
| `Len`     | Integer | Number of key-value pairs in the map   |

### Methods

| Method                     | Signature                 | Description                                                       |
|----------------------------|---------------------------|-------------------------------------------------------------------|
| `Get(key)`                 | `Object(String)`          | Get value for key (returns null if not found)                     |
| `GetOr(key, defaultValue)` | `Object(String, Object)` | Get value for key, or return defaultValue if missing              |
| `Has(key)`                 | `Boolean(String)`         | Check if key exists                                               |
| `Keys()`                   | `Seq()`                   | Get all keys as a Seq                                             |
| `Values()`                 | `Seq()`                   | Get all values as a Seq                                           |
| `Merge(other)`             | `FrozenMap(FrozenMap)`    | Return new FrozenMap with entries from both; other's values win on conflict |
| `Equals(other)`            | `Boolean(FrozenMap)`      | Check if two maps have the same key-value pairs                   |

### Notes

- Keys in the `FromSeqs` constructor should be boxed strings (e.g., `Box.Str("key")`) in Zia; BASIC auto-boxes strings
- FrozenMap is truly immutable: there are no Set, Remove, or Clear methods
- `Merge` returns a new FrozenMap; when both maps have the same key, the other map's value wins
- `Equals` compares by value, not reference identity

### Zia Example

```rust
module FrozenMapDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    // Build from parallel sequences
    var keys = Seq.New();
    keys.Push(Box.Str("name"));
    keys.Push(Box.Str("city"));

    var vals = Seq.New();
    vals.Push(Box.Str("Alice"));
    vals.Push(Box.Str("Boston"));

    var fm = FrozenMap.FromSeqs(keys, vals);
    SayInt(fm.Len);                                // 2
    Say(Box.ToStr(fm.Get("name")));                // Alice
    SayBool(fm.Has("name"));                       // 1
    Say(Box.ToStr(fm.GetOr("email", Box.Str("N/A")))); // N/A

    // Merge two frozen maps
    var keys2 = Seq.New();
    keys2.Push(Box.Str("city"));
    keys2.Push(Box.Str("email"));
    var vals2 = Seq.New();
    vals2.Push(Box.Str("NYC"));
    vals2.Push(Box.Str("a@b.com"));
    var fm2 = FrozenMap.FromSeqs(keys2, vals2);

    var merged = fm.Merge(fm2);
    SayInt(merged.Len);                            // 3
    Say(Box.ToStr(merged.Get("city")));            // NYC (fm2 wins)
}
```

### BASIC Example

```basic
' Create from parallel sequences
DIM keys AS Viper.Collections.Seq
keys = Viper.Collections.Seq.New()
keys.Push("name")
keys.Push("city")

DIM vals AS Viper.Collections.Seq
vals = Viper.Collections.Seq.New()
vals.Push("Alice")
vals.Push("Boston")

DIM fm AS OBJECT
fm = Viper.Collections.FrozenMap.FromSeqs(keys, vals)
PRINT fm.Len                ' 2
PRINT fm.Get("name")        ' Alice
PRINT fm.Has("name")        ' 1

' GetOr for missing key
PRINT fm.GetOr("email", "N/A")  ' N/A

' Merge two frozen maps
DIM keys2 AS Viper.Collections.Seq
keys2 = Viper.Collections.Seq.New()
keys2.Push("city")
keys2.Push("email")
DIM vals2 AS Viper.Collections.Seq
vals2 = Viper.Collections.Seq.New()
vals2.Push("NYC")
vals2.Push("a@b.com")

DIM fm2 AS OBJECT
fm2 = Viper.Collections.FrozenMap.FromSeqs(keys2, vals2)
DIM merged AS OBJECT
merged = fm.Merge(fm2)
PRINT merged.Len             ' 3
PRINT merged.Get("city")     ' NYC (fm2 wins)
PRINT merged.Get("name")     ' Alice (from fm)

' Equality
DIM keys3 AS Viper.Collections.Seq
keys3 = Viper.Collections.Seq.New()
keys3.Push("city")
keys3.Push("name")
DIM vals3 AS Viper.Collections.Seq
vals3 = Viper.Collections.Seq.New()
vals3.Push("Boston")
vals3.Push("Alice")
DIM fm3 AS OBJECT
fm3 = Viper.Collections.FrozenMap.FromSeqs(keys3, vals3)
PRINT fm.Equals(fm3)         ' 1 (same key-value pairs)
PRINT fm.Equals(fm2)         ' 0
```

### Use Cases

- **Configuration snapshots:** Immutable configuration that cannot be accidentally modified
- **Thread-safe sharing:** Share data between threads without locking
- **Function return values:** Return read-only dictionaries from functions
- **Caching:** Store computed results as immutable maps

---

## Viper.Collections.FrozenSet

An immutable set of unique strings. Once created, elements cannot be added or removed. Supports set operations
that return new FrozenSet instances.

**Type:** Instance (obj)
**Constructors:**

- `Viper.Collections.FrozenSet.FromSeq(seq)` - Create from a Seq of boxed strings (duplicates are removed)
- `Viper.Collections.FrozenSet.Empty()` - Create an empty immutable set

### Properties

| Property  | Type    | Description                             |
|-----------|---------|-----------------------------------------|
| `IsEmpty` | Boolean | True if the set has no elements         |
| `Len`     | Integer | Number of unique elements in the set    |

### Methods

| Method             | Signature              | Description                                                   |
|--------------------|------------------------|---------------------------------------------------------------|
| `Has(str)`         | `Boolean(String)`      | Check if string is in the set                                 |
| `Items()`          | `Seq()`                | Get all elements as a Seq (order undefined)                   |
| `Union(other)`     | `FrozenSet(FrozenSet)` | Return new set with elements from either set                  |
| `Intersect(other)` | `FrozenSet(FrozenSet)` | Return new set with elements in both sets                     |
| `Diff(other)`      | `FrozenSet(FrozenSet)` | Return new set with elements in this but not other            |
| `IsSubset(other)`  | `Boolean(FrozenSet)`   | True if all elements of this set are in the other set         |
| `Equals(other)`    | `Boolean(FrozenSet)`   | True if both sets contain exactly the same elements           |

### Notes

- Elements in the `FromSeq` constructor should be boxed strings (e.g., `Box.Str("value")`) in Zia; BASIC auto-boxes
- Duplicate elements in the source Seq are automatically removed
- All set operations return new FrozenSet instances; originals are unchanged
- `Equals` compares by value regardless of insertion order

### Zia Example

```rust
module FrozenSetDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    // Build from a Seq (duplicates removed)
    var items = Seq.New();
    items.Push(Box.Str("apple"));
    items.Push(Box.Str("banana"));
    items.Push(Box.Str("cherry"));
    items.Push(Box.Str("apple"));  // duplicate

    var fs = FrozenSet.FromSeq(items);
    SayInt(fs.Len);                               // 3
    SayBool(fs.Has("apple"));                     // 1
    SayBool(fs.Has("grape"));                     // 0

    // Set operations
    var items2 = Seq.New();
    items2.Push(Box.Str("cherry"));
    items2.Push(Box.Str("date"));
    var fs2 = FrozenSet.FromSeq(items2);

    var united = fs.Union(fs2);
    SayInt(united.Len);                           // 4

    var inter = fs.Intersect(fs2);
    SayInt(inter.Len);                            // 1 (cherry)

    var diff = fs.Diff(fs2);
    SayInt(diff.Len);                             // 2 (apple, banana)
}
```

### BASIC Example

```basic
' Build from a Seq (duplicates removed)
DIM items AS Viper.Collections.Seq
items = Viper.Collections.Seq.New()
items.Push("apple")
items.Push("banana")
items.Push("cherry")
items.Push("apple")  ' duplicate

DIM fs AS OBJECT
fs = Viper.Collections.FrozenSet.FromSeq(items)
PRINT fs.Len              ' 3
PRINT fs.Has("apple")     ' 1
PRINT fs.Has("grape")     ' 0

' Get all items
DIM all AS OBJECT
all = fs.Items()
PRINT all.Len             ' 3

' Set operations
DIM items2 AS Viper.Collections.Seq
items2 = Viper.Collections.Seq.New()
items2.Push("cherry")
items2.Push("date")
items2.Push("elderberry")
DIM fs2 AS OBJECT
fs2 = Viper.Collections.FrozenSet.FromSeq(items2)

DIM united AS OBJECT
united = fs.Union(fs2)
PRINT united.Len          ' 5

DIM inter AS OBJECT
inter = fs.Intersect(fs2)
PRINT inter.Len           ' 1 (cherry)

DIM diff AS OBJECT
diff = fs.Diff(fs2)
PRINT diff.Len            ' 2 (apple, banana)

' Subset check
DIM subItems AS Viper.Collections.Seq
subItems = Viper.Collections.Seq.New()
subItems.Push("apple")
subItems.Push("banana")
DIM subset AS OBJECT
subset = Viper.Collections.FrozenSet.FromSeq(subItems)
PRINT subset.IsSubset(fs)   ' 1 (true)

' Equality (order independent)
DIM items3 AS Viper.Collections.Seq
items3 = Viper.Collections.Seq.New()
items3.Push("banana")
items3.Push("cherry")
items3.Push("apple")
DIM fs3 AS OBJECT
fs3 = Viper.Collections.FrozenSet.FromSeq(items3)
PRINT fs.Equals(fs3)        ' 1 (same elements)
```

### Use Cases

- **Constant sets:** Define a fixed set of valid values (e.g., allowed file extensions)
- **Thread-safe sharing:** Share immutable sets between threads without locking
- **Snapshot comparisons:** Compare two snapshots of data for changes
- **Access control lists:** Define immutable permission sets

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
| `Remove(key)`     | `Boolean(String)`      | Remove a key-value pair; returns true if removed                |
| `Clear()`         | `Void()`               | Remove all entries                                              |
| `Keys()`          | `Seq()`                | Get all keys as a Seq in sorted order                           |
| `Values()`        | `Seq()`                | Get all values as a Seq in key-sorted order                     |
| `First()`         | `String()`             | Get the smallest (first) key; returns empty string if empty     |
| `Last()`          | `String()`             | Get the largest (last) key; returns empty string if empty       |
| `Floor(key)`      | `String(String)`       | Get the largest key <= given key; returns empty string if none  |
| `Ceil(key)`       | `String(String)`       | Get the smallest key >= given key; returns empty string if none |

### Zia Example

```rust
module TreeMapDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    var tm = new TreeMap();

    // Insert in any order — stored sorted
    tm.Set("cherry", Viper.Core.Box.Str("red"));
    tm.Set("apple", Viper.Core.Box.Str("green"));
    tm.Set("banana", Viper.Core.Box.Str("yellow"));

    Say("Length: " + Fmt.Int(tm.Len));           // 3
    Say("First: " + tm.First());                  // apple
    Say("Last: " + tm.Last());                    // cherry

    // Range queries
    Say("Floor(cat): " + tm.Floor("cat"));        // banana
    Say("Ceil(cat): " + tm.Ceil("cat"));          // cherry
}
```

### BASIC Example

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


## See Also

- [Sequential Collections](sequential.md)
- [Specialized Maps](multi-maps.md)
- [Functional & Lazy](functional.md)
- [Specialized Structures](specialized.md)
- [Collections Overview](README.md)
- [Viper Runtime Library](../README.md)
