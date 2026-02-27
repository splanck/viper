# Specialized Structures
> Bag, BloomFilter, Trie, UnionFind, BitSet, Bytes

**Part of [Viper Runtime Library](../README.md) › [Collections](README.md)**

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
| `Union(other)`     | `Bag(Bag)`        | Return new bag with union of both bags                       |
| `Intersect(other)` | `Bag(Bag)`        | Return new bag with intersection of both bags                |
| `Diff(other)`   | `Bag(Bag)`        | Return new bag with elements in this but not other           |

### Notes

- Strings are stored by value (copied into the bag)
- Order of strings returned by `Items()` is not guaranteed (hash table)
- Set operations (`Union`, `Intersect`, `Diff`) return new bags; originals are unchanged
- Uses FNV-1a hash function for O(1) average-case operations
- Automatically resizes when load factor exceeds 75%

### Zia Example

```rust
module BagDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    var fruits = new Bag();
    fruits.Put("apple");
    fruits.Put("banana");
    fruits.Put("cherry");
    Say("Count: " + Fmt.Int(fruits.Len));              // 3

    // Membership testing
    Say("Has banana: " + Fmt.Bool(fruits.Has("banana")));  // true
    Say("Has grape: " + Fmt.Bool(fruits.Has("grape")));    // false

    // Duplicate returns false
    Say("Add apple: " + Fmt.Bool(fruits.Put("apple")));    // false

    // Remove
    fruits.Drop("banana");
    Say("After drop: " + Fmt.Int(fruits.Len));             // 2
}
```

### BASIC Example

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
DIM merged AS OBJECT = bagA.Union(bagB)
PRINT merged.Len           ' Output: 4 (a, b, c, d)

' Intersection: elements in both bags
DIM common AS OBJECT = bagA.Intersect(bagB)
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

## Viper.Collections.BloomFilter

A space-efficient probabilistic data structure for membership testing. Can definitively say an element is *not* in
the set, but may produce false positives. Ideal for pre-filtering expensive lookups.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.BloomFilter.New(capacity, falsePositiveRate)` - creates a filter sized for the
expected number of elements and desired false positive rate

### Properties

| Property | Type    | Description                          |
|----------|---------|--------------------------------------|
| `Count`  | Integer | Number of elements added to the filter |

### Methods

| Method              | Signature            | Description                                                    |
|---------------------|----------------------|----------------------------------------------------------------|
| `Add(str)`          | `Void(String)`       | Add a string to the filter                                     |
| `MightContain(str)` | `Boolean(String)`    | Check if string might be in the filter (false positives possible) |
| `Clear()`           | `Void()`             | Remove all elements from the filter                            |
| `Fpr()`             | `Float()`            | Get the estimated current false positive rate                  |
| `Merge(other)`      | `Integer(BloomFilter)` | Merge another filter's bits into this one; returns 1 on success |

### Notes

- `MightContain` returning true means the element *may* be present; returning false means it is *definitely* not present
- The actual false positive rate depends on the number of elements added relative to the configured capacity
- `Merge` combines two filters that were created with the same capacity and false positive rate parameters
- After `Clear()`, `MightContain` returns false for all elements

### Zia Example

```rust
module BloomFilterDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    // Create filter for 100 items with 1% false positive rate
    var bf = BloomFilter.New(100, 0.01);

    // Add elements
    bf.Add("apple");
    bf.Add("banana");
    bf.Add("cherry");
    SayInt(bf.Count);                              // 3

    // Membership testing
    SayBool(bf.MightContain("apple"));             // 1 (true)
    SayBool(bf.MightContain("grape"));             // 0 (probably false)

    // Merge two filters
    var bf2 = BloomFilter.New(100, 0.01);
    bf2.Add("grape");
    bf2.Add("kiwi");
    SayInt(bf.Merge(bf2));                         // 1 (success)
    SayBool(bf.MightContain("grape"));             // 1 (now true)

    // Clear
    bf.Clear();
    SayInt(bf.Count);                              // 0
    SayBool(bf.MightContain("apple"));             // 0
}
```

### BASIC Example

```basic
' Create filter for 100 items with 1% false positive rate
DIM bf AS OBJECT
bf = Viper.Collections.BloomFilter.New(100, 0.01)

' Add elements
bf.Add("apple")
bf.Add("banana")
bf.Add("cherry")
PRINT bf.Count                       ' 3

' Membership testing
PRINT bf.MightContain("apple")       ' 1 (true - definitely added)
PRINT bf.MightContain("grape")       ' 0 (probably not present)

' Current false positive rate
PRINT bf.Fpr()                       ' Very low (near 0.0)

' Merge two filters
DIM bf2 AS OBJECT
bf2 = Viper.Collections.BloomFilter.New(100, 0.01)
bf2.Add("grape")
bf2.Add("kiwi")
PRINT bf.Merge(bf2)                  ' 1 (success)
PRINT bf.MightContain("grape")       ' 1 (now present)
PRINT bf.MightContain("kiwi")        ' 1 (now present)

' Clear the filter
bf.Clear()
PRINT bf.Count                       ' 0
PRINT bf.MightContain("apple")       ' 0 (no longer present)
```

### Use Cases

- **Database pre-filtering:** Avoid expensive disk lookups for absent keys
- **URL deduplication:** Quickly check if a URL has been visited in a web crawler
- **Spam detection:** Pre-filter email addresses against known spammers
- **Cache lookup:** Check if an item might be cached before querying the cache
- **Network routing:** Efficiently check membership in distributed systems

---

## Viper.Collections.Trie

A prefix tree (trie) that maps string keys to values. Supports efficient prefix-based operations including prefix
existence checking, longest prefix matching, and retrieving all keys with a given prefix.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.Trie.New()`

### Properties

| Property  | Type    | Description                             |
|-----------|---------|-----------------------------------------|
| `IsEmpty` | Boolean | True if the trie has no entries         |
| `Len`     | Integer | Number of key-value pairs in the trie   |

### Methods

| Method                | Signature            | Description                                                          |
|-----------------------|----------------------|----------------------------------------------------------------------|
| `Put(key, value)`     | `Void(String, Object)` | Add or update a key-value pair                                    |
| `Get(key)`            | `Object(String)`     | Get value for an exact key match (null if not found)                 |
| `Has(key)`            | `Boolean(String)`    | Check if an exact key exists                                         |
| `HasPrefix(prefix)`   | `Boolean(String)`    | Check if any key starts with the given prefix                        |
| `LongestPrefix(str)`  | `String(String)`     | Find the longest key that is a prefix of the given string            |
| `WithPrefix(prefix)`  | `Seq(String)`        | Get all keys that start with the given prefix                        |
| `Keys()`              | `Seq()`              | Get all keys as a Seq                                                |
| `Remove(key)`         | `Boolean(String)`    | Remove a key-value pair; returns true if found                       |
| `Clear()`             | `Void()`             | Remove all entries                                                   |

### Notes

- `Has` checks for an exact key, not a prefix; use `HasPrefix` for prefix existence checks
- `LongestPrefix` finds the longest stored key that is a prefix of the input string (useful for routing)
- `WithPrefix` returns all keys that start with the given prefix, including exact matches
- Removing a key does not affect other keys that share the same prefix

### Zia Example

```rust
module TrieDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    var t = Trie.New();

    // Insert keys
    t.Put("cat", Box.I64(1));
    t.Put("car", Box.I64(2));
    t.Put("card", Box.I64(3));
    t.Put("care", Box.I64(4));
    t.Put("dog", Box.I64(5));
    SayInt(t.Len);                                 // 5

    // Exact lookup
    SayInt(Box.ToI64(t.Get("cat")));               // 1
    SayBool(t.Has("cat"));                         // 1
    SayBool(t.Has("ca"));                          // 0 (prefix, not a key)

    // Prefix operations
    SayBool(t.HasPrefix("ca"));                    // 1
    SayBool(t.HasPrefix("x"));                     // 0

    // Longest prefix match
    Say(t.LongestPrefix("card_game"));             // card
    Say(t.LongestPrefix("caring"));                // care
    Say(t.LongestPrefix("catalog"));               // cat

    // All keys with prefix
    var carKeys = t.WithPrefix("car");
    SayInt(carKeys.Len);                           // 3 (car, card, care)

    // Remove
    t.Remove("card");
    SayBool(t.Has("card"));                        // 0
    SayBool(t.Has("car"));                         // 1 (not affected)
}
```

### BASIC Example

```basic
DIM t AS OBJECT
t = Viper.Collections.Trie.New()

' Insert keys with values
t.Put("cat", Viper.Core.Box.I64(1))
t.Put("car", Viper.Core.Box.I64(2))
t.Put("card", Viper.Core.Box.I64(3))
t.Put("care", Viper.Core.Box.I64(4))
t.Put("dog", Viper.Core.Box.I64(5))
PRINT t.Len                     ' 5

' Exact lookup
PRINT Viper.Core.Box.ToI64(t.Get("cat"))   ' 1
PRINT t.Has("cat")              ' 1
PRINT t.Has("ca")               ' 0 (prefix, not a key)

' Prefix checks
PRINT t.HasPrefix("ca")         ' 1
PRINT t.HasPrefix("car")        ' 1
PRINT t.HasPrefix("x")          ' 0

' Longest prefix match
PRINT t.LongestPrefix("card_game")  ' card
PRINT t.LongestPrefix("caring")     ' care
PRINT t.LongestPrefix("catalog")    ' cat
PRINT t.LongestPrefix("dogs")       ' dog

' All keys with prefix
DIM carKeys AS OBJECT
carKeys = t.WithPrefix("car")
PRINT carKeys.Len               ' 3 (car, card, care)

DIM caKeys AS OBJECT
caKeys = t.WithPrefix("ca")
PRINT caKeys.Len                ' 4 (cat, car, card, care)

' Update existing key
t.Put("cat", Viper.Core.Box.I64(100))
PRINT Viper.Core.Box.ToI64(t.Get("cat"))  ' 100
PRINT t.Len                     ' 5 (no new entry)

' Remove a key (does not affect siblings)
PRINT t.Remove("card")          ' 1
PRINT t.Has("card")             ' 0
PRINT t.Has("car")              ' 1
PRINT t.Has("care")             ' 1

' Clear
t.Clear()
PRINT t.IsEmpty                 ' 1
```

### Use Cases

- **Autocomplete:** Find all words matching a typed prefix with `WithPrefix`
- **URL routing:** Match URL paths to handlers using `LongestPrefix`
- **IP routing tables:** Longest prefix matching for network routing
- **Spell checking:** Check if partial words exist as prefixes of known words
- **Dictionary lookup:** Efficient storage and retrieval of string-keyed data

---

## Viper.Collections.UnionFind

A disjoint set (union-find) data structure with path compression and union by rank. Efficiently tracks which
elements belong to the same group and supports merging groups.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.UnionFind.New(size)` - creates a structure with `size` elements, each in its own set

### Properties

| Property | Type    | Description                                    |
|----------|---------|------------------------------------------------|
| `Count`  | Integer | Number of disjoint sets (decreases after Union) |

### Methods

| Method                | Signature                  | Description                                                     |
|-----------------------|----------------------------|-----------------------------------------------------------------|
| `Find(x)`            | `Integer(Integer)`         | Find the representative (root) of element x's set               |
| `Union(x, y)`        | `Integer(Integer, Integer)`| Merge the sets containing x and y; returns 1 if merged, 0 if already same set |
| `Connected(x, y)`    | `Boolean(Integer, Integer)`| Check if x and y are in the same set                            |
| `SetSize(x)`         | `Integer(Integer)`         | Get the size of the set containing element x                    |
| `Reset()`            | `Void()`                   | Reset all elements to individual sets                           |

### Notes

- Elements are identified by integers from 0 to size-1
- Uses path compression and union by rank for near-O(1) amortized operations
- `Union` returns 0 if the elements are already in the same set (no operation performed)
- `Reset` restores the structure to its initial state with `Count` equal to size

### Zia Example

```rust
module UnionFindDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    var uf = UnionFind.New(6);
    SayInt(uf.Count);                            // 6 (each element is its own set)

    // Merge sets
    uf.Union(0, 1);
    uf.Union(2, 3);
    SayInt(uf.Count);                            // 4

    // Check connectivity
    SayBool(uf.Connected(0, 1));                 // 1
    SayBool(uf.Connected(0, 2));                 // 0

    // Merge across groups
    uf.Union(1, 3);
    SayBool(uf.Connected(0, 2));                 // 1 (now connected via 1-3)
    SayInt(uf.Count);                            // 3

    // Set sizes
    SayInt(uf.SetSize(0));                       // 4 ({0,1,2,3})
    SayInt(uf.SetSize(4));                       // 1 ({4} alone)

    // Reset to individual sets
    uf.Reset();
    SayInt(uf.Count);                            // 6
    SayBool(uf.Connected(0, 1));                 // 0
}
```

### BASIC Example

```basic
DIM uf AS OBJECT
uf = Viper.Collections.UnionFind.New(6)
PRINT uf.Count               ' 6 (each element is its own set)

' Find representative
PRINT uf.Find(0)             ' 0 (own representative)
PRINT uf.Find(3)             ' 3

' Merge sets
PRINT uf.Union(0, 1)         ' 1 (merged)
PRINT uf.Count               ' 5
PRINT uf.Union(2, 3)         ' 1 (merged)
PRINT uf.Count               ' 4
PRINT uf.Union(0, 1)         ' 0 (already in same set)

' Check connectivity
PRINT uf.Connected(0, 1)     ' 1 (same set)
PRINT uf.Connected(0, 2)     ' 0 (different sets)

' Merge across groups
uf.Union(1, 3)
PRINT uf.Connected(0, 2)     ' 1 (now connected)
PRINT uf.Count               ' 3

' Set sizes
PRINT uf.SetSize(0)          ' 4 ({0,1,2,3})
PRINT uf.SetSize(4)          ' 1 ({4} alone)

' Merge all remaining
uf.Union(4, 5)
uf.Union(0, 4)
PRINT uf.Count               ' 1
PRINT uf.SetSize(0)          ' 6 (all connected)

' Reset to individual sets
uf.Reset()
PRINT uf.Count               ' 6
PRINT uf.Connected(0, 1)     ' 0
PRINT uf.SetSize(0)          ' 1
```

### Use Cases

- **Network connectivity:** Determine if two nodes are connected in a network
- **Kruskal's algorithm:** Build minimum spanning trees
- **Image segmentation:** Group connected pixels into regions
- **Equivalence classes:** Track which items are equivalent
- **Percolation:** Model physical systems to detect connected paths

---

## Viper.Collections.BitSet

A fixed-size set of bits supporting efficient bitwise operations. Useful for compact storage of boolean flags and
set operations on integer-indexed elements.

**Type:** Instance (obj)
**Constructor:** `Viper.Collections.BitSet.New(size)` - creates a BitSet with the given number of bits

### Properties

| Property  | Type    | Description                              |
|-----------|---------|------------------------------------------|
| `Count`   | Integer | Number of bits currently set to 1        |
| `IsEmpty` | Boolean | True if no bits are set                  |
| `Len`     | Integer | Total number of bits (size of the set)   |

### Methods

| Method        | Signature              | Description                                            |
|---------------|------------------------|--------------------------------------------------------|
| `Set(index)`  | `Void(Integer)`        | Set bit at index to 1                                  |
| `Clear(index)`| `Void(Integer)`        | Clear bit at index (set to 0)                          |
| `ClearAll()`  | `Void()`               | Clear all bits to 0                                    |
| `Get(index)`  | `Boolean(Integer)`     | Get value of bit at index                              |
| `Toggle(index)` | `Void(Integer)`      | Flip bit at index (0 becomes 1, 1 becomes 0)          |
| `SetAll()`    | `Void()`               | Set all bits to 1                                      |
| `And(other)`  | `BitSet(BitSet)`       | Return new BitSet with bitwise AND of both sets        |
| `Or(other)`   | `BitSet(BitSet)`       | Return new BitSet with bitwise OR of both sets         |
| `Xor(other)`  | `BitSet(BitSet)`       | Return new BitSet with bitwise XOR of both sets        |
| `Not()`       | `BitSet()`             | Return new BitSet with all bits inverted               |
| `ToString()`  | `String()`             | Return binary string representation of the bit pattern |

### Notes

- Size is fixed at creation time and cannot grow
- Bitwise operations (`And`, `Or`, `Xor`, `Not`) return new BitSet instances; originals are unchanged
- `ToString()` returns a binary string with the most significant bit on the left
- Indices are zero-based; index 0 is the least significant bit

### Zia Example

```rust
module BitSetDemo;

bind Viper.Collections;
bind Viper.Core;
bind Viper.Terminal;
bind Viper.Fmt;

func start() {
    var bs = BitSet.New(8);

    // Set some bits
    bs.Set(0);
    bs.Set(2);
    bs.Set(7);
    Say("Count: " + Fmt.Int(bs.Count));         // 3
    Say("Get(2): " + Fmt.Bool(bs.Get(2)));       // true
    Say("Get(1): " + Fmt.Bool(bs.Get(1)));       // false

    // Toggle a bit
    bs.Toggle(2);
    Say("After toggle Get(2): " + Fmt.Bool(bs.Get(2)));  // false

    // Binary representation
    Say("Binary: " + bs.ToString());
    Say("Len: " + Fmt.Int(bs.Len));              // 8

    // Bitwise operations
    var a = BitSet.New(8);
    a.Set(0);
    a.Set(1);
    var b = BitSet.New(8);
    b.Set(1);
    b.Set(2);
    var result = a.And(b);
    SayInt(result.Count);                        // 1 (only bit 1)
}
```

### BASIC Example

```basic
DIM bs AS OBJECT
bs = Viper.Collections.BitSet.New(8)

' Set some bits
bs.Set(0)
bs.Set(2)
bs.Set(7)
PRINT "Count: "; bs.Count       ' Count: 3
PRINT "Get(7): "; bs.Get(7)     ' Get(7): -1
PRINT "Get(1): "; bs.Get(1)     ' Get(1): 0

' Toggle a bit
bs.Toggle(7)
PRINT "After toggle Get(7): "; bs.Get(7)  ' After toggle Get(7): 0

PRINT "Len: "; bs.Len           ' Len: 8

' Binary representation
PRINT bs.ToString()

' Bitwise AND
DIM a AS OBJECT = Viper.Collections.BitSet.New(8)
a.Set(0)
a.Set(1)
a.Set(2)
DIM b AS OBJECT = Viper.Collections.BitSet.New(8)
b.Set(1)
b.Set(2)
b.Set(3)
DIM andResult AS OBJECT = a.And(b)
PRINT andResult.Count            ' 2 (bits 1 and 2)

' Bitwise NOT
DIM c AS OBJECT = Viper.Collections.BitSet.New(4)
c.Set(0)
c.Set(2)
DIM notResult AS OBJECT = c.Not()
PRINT notResult.Get(1)           ' 1 (inverted)
```

### Use Cases

- **Permission flags:** Store and check multiple boolean permissions compactly
- **Feature toggles:** Track which features are enabled
- **Set intersection/union:** Efficient set operations on integer-indexed elements
- **Bloom filter backing:** Underlying storage for probabilistic data structures
- **Graph algorithms:** Track visited nodes in dense graphs

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
| `ReadI16LE(offset)`                      | `Integer(Integer)`        | Read 16-bit signed integer at offset (little-endian)                  |
| `ReadI16BE(offset)`                      | `Integer(Integer)`        | Read 16-bit signed integer at offset (big-endian)                     |
| `ReadI32LE(offset)`                      | `Integer(Integer)`        | Read 32-bit signed integer at offset (little-endian)                  |
| `ReadI32BE(offset)`                      | `Integer(Integer)`        | Read 32-bit signed integer at offset (big-endian)                     |
| `ReadI64LE(offset)`                      | `Integer(Integer)`        | Read 64-bit signed integer at offset (little-endian)                  |
| `ReadI64BE(offset)`                      | `Integer(Integer)`        | Read 64-bit signed integer at offset (big-endian)                     |
| `WriteI16LE(offset, value)`              | `Void(Integer, Integer)`  | Write 16-bit integer at offset (little-endian)                        |
| `WriteI16BE(offset, value)`              | `Void(Integer, Integer)`  | Write 16-bit integer at offset (big-endian)                           |
| `WriteI32LE(offset, value)`              | `Void(Integer, Integer)`  | Write 32-bit integer at offset (little-endian)                        |
| `WriteI32BE(offset, value)`              | `Void(Integer, Integer)`  | Write 32-bit integer at offset (big-endian)                           |
| `WriteI64LE(offset, value)`              | `Void(Integer, Integer)`  | Write 64-bit integer at offset (little-endian)                        |
| `WriteI64BE(offset, value)`              | `Void(Integer, Integer)`  | Write 64-bit integer at offset (big-endian)                           |

### Zia Example

```rust
module BytesDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    // Create from string
    var data = Bytes.FromStr("Hello");
    Say("Length: " + Fmt.Int(data.Len));        // 5
    Say("First byte: " + Fmt.Int(data.Get(0))); // 72 (ASCII 'H')
    Say("As string: " + data.ToStr());          // Hello

    // Hex and Base64 encoding
    Say("Hex: " + data.ToHex());                // 48656c6c6f
    Say("Base64: " + data.ToBase64());          // SGVsbG8=

    // Create from hex
    var hex = Bytes.FromHex("deadbeef");
    Say("From hex len: " + Fmt.Int(hex.Len));   // 4
}
```

### BASIC Example

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


## See Also

- [Sequential Collections](sequential.md)
- [Maps & Sets](maps-sets.md)
- [Specialized Maps](multi-maps.md)
- [Functional & Lazy](functional.md)
- [Collections Overview](README.md)
- [Viper Runtime Library](../README.md)
