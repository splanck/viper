# Collections

> Data structures for storing and organizing data.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Viper.Collections.Bag](#vipercollectionsbag)
- [Viper.Collections.BiMap](#vipercollectionsbimap)
- [Viper.Collections.BitSet](#vipercollectionsbitset)
- [Viper.Collections.BloomFilter](#vipercollectionsbloomfilter)
- [Viper.Collections.Bytes](#vipercollectionsbytes)
- [Viper.Collections.CountMap](#vipercollectionscountmap)
- [Viper.Collections.DefaultMap](#vipercollectionsdefaultmap)
- [Viper.Collections.Deque](#vipercollectionsdeque)
- [Viper.Collections.FrozenMap](#vipercollectionsfrozenmap)
- [Viper.Collections.FrozenSet](#vipercollectionsfrozenset)
- [Viper.Collections.Heap](#vipercollectionsheap)
- [Viper.Collections.Iterator](#vipercollectionsiterator)
- [Viper.LazySeq](#viperlazyseq)
- [Viper.Collections.List](#vipercollectionslist)
- [Viper.Collections.LruCache](#vipercollectionslrucache)
- [Viper.Collections.Map](#vipercollectionsmap)
- [Viper.Collections.MultiMap](#vipercollectionsmultimap)
- [Viper.Collections.OrderedMap](#vipercollectionsorderedmap)
- [Viper.Collections.Queue](#vipercollectionsqueue)
- [Viper.Collections.Ring](#vipercollectionsring)
- [Viper.Collections.Seq](#vipercollectionsseq)
- [Viper.Collections.Set](#vipercollectionsset)
- [Viper.Collections.SortedSet](#vipercollectionssortedset)
- [Viper.Collections.SparseArray](#vipercollectionssparsearray)
- [Viper.Collections.Stack](#vipercollectionsstack)
- [Viper.Collections.TreeMap](#vipercollectionstreemap)
- [Viper.Collections.Trie](#vipercollectionstrie)
- [Viper.Collections.UnionFind](#vipercollectionsunionfind)
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

### Zia Example

```zia
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

```zia
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

```zia
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

```zia
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

### Zia Example

```zia
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

```zia
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

```zia
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

## Viper.Collections.Deque

A double-ended queue (deque) that supports efficient insertion and removal at both ends. Combines the capabilities of
stacks and queues while also supporting indexed access.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.Deque()`

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

### Zia Example

> Deque is not yet available as a constructible type in Zia. Use BASIC or access via the runtime API.

### BASIC Example

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

```zia
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

```zia
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

### Zia Example

```zia
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

```zia
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

## Viper.Collections.List

Dynamic array that grows automatically. Stores object references.

**Type:** Instance (obj)
**Constructor:** `NEW Viper.Collections.List()`

### Properties

| Property | Type    | Description                 |
|----------|---------|-----------------------------|
| `Len`    | Integer | Number of items in the list |

> **Note:** `Count` is available as an alias for `Len` for backward compatibility.

### Methods

| Method                   | Signature               | Description                                                                           |
|--------------------------|-------------------------|---------------------------------------------------------------------------------------|
| `Push(item)`             | `Void(Object)`          | Appends an item to the end of the list                                                |
| `Get(index)`             | `Object(Integer)`       | Gets the item at the specified index                                                  |
| `Set(index, value)`      | `Void(Integer, Object)` | Sets the item at the specified index                                                  |
| `Clear()`                | `Void()`                | Removes all items from the list                                                       |
| `Has(item)`              | `Boolean(Object)`       | Returns true if the list contains the object (reference equality)                     |
| `Find(item)`             | `Integer(Object)`       | Returns index of the first matching object, or `-1` if not found                      |
| `Insert(index, item)`    | `Void(Integer, Object)` | Inserts the item at `index` (0..Len); `index == Len` appends; traps if out of range   |
| `Remove(item)`           | `Boolean(Object)`       | Removes the first matching object (reference equality); returns true if removed       |
| `RemoveAt(index)`        | `Void(Integer)`         | Removes the item at the specified index                                               |
| `Slice(start, end)`      | `List(Integer, Integer)`| Returns a new list with elements from start (inclusive) to end (exclusive)            |
| `Flip()`                 | `Void()`                | Reverses the elements of the list in place                                            |
| `First()`                | `Object()`              | Returns the first element in the list                                                 |
| `Last()`                 | `Object()`              | Returns the last element in the list                                                  |

### Zia Example

```zia
module ListDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    var list = new List[String]();

    // Add items
    list.Push("apple");
    list.Push("banana");
    list.Push("cherry");
    Say("Len: " + Fmt.Int(list.Len));            // 3

    // Access by index
    Say("First: " + list.Get(0));                // apple
    Say("Last: " + list.Get(list.Len - 1));      // cherry

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

PRINT list.Find(b)         ' Output: 1

IF list.Has(a) THEN
  PRINT 1                  ' Output: 1 (true)
END IF

IF list.Remove(a) THEN
  PRINT list.Len           ' Output: 2
END IF
PRINT list.Find(a)         ' Output: -1

' Access by index
PRINT list.Get(0)          ' First element
list.Set(0, b)             ' Replace first element

' Slice, Flip, First, Last
DIM sub AS OBJECT = list.Slice(0, 1)
list.Flip()                ' Reverse in place
PRINT list.First()         ' First element
PRINT list.Last()          ' Last element

' Clear all
list.Clear()
```

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

```zia
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

### Zia Example

```zia
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

```zia
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

```zia
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

| Method        | Signature      | Description                                            |
|---------------|----------------|--------------------------------------------------------|
| `Push(value)` | `Void(Object)` | Add element to back of queue                           |
| `Pop()`       | `Object()`     | Remove and return front element (traps if empty)       |
| `Peek()`      | `Object()`     | Return front element without removing (traps if empty) |
| `Clear()`     | `Void()`       | Remove all elements                                    |

### Zia Example

```zia
module QueueDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    var queue = new Queue();
    queue.Push("first");
    queue.Push("second");
    queue.Push("third");
    Say("Length: " + Fmt.Int(queue.Len));                 // 3

    // FIFO order
    Say("Pop: " + Viper.Core.Box.ToStr(queue.Pop()));          // first
    Say("Peek: " + Viper.Core.Box.ToStr(queue.Peek()));        // second
    Say("After pop: " + Fmt.Int(queue.Len));               // 2

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

PRINT queue.Len      ' Output: 3
PRINT queue.IsEmpty  ' Output: False

' Pop returns elements in FIFO order
PRINT queue.Pop()    ' Output: "first"
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

### Zia Example

```zia
module RingDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    var ring = new Ring(3);

    ring.Push(Viper.Core.Box.Str("first"));
    ring.Push(Viper.Core.Box.Str("second"));
    ring.Push(Viper.Core.Box.Str("third"));
    Say("Length: " + Fmt.Int(ring.Len));         // 3
    Say("Full: " + Fmt.Bool(ring.IsFull));       // true

    // Overflow overwrites oldest
    ring.Push(Viper.Core.Box.Str("fourth"));
    Say("Oldest: " + Viper.Core.Box.ToStr(ring.Peek()));  // second

    // Pop removes oldest (FIFO)
    Say("Pop: " + Viper.Core.Box.ToStr(ring.Pop()));       // second
    Say("After pop: " + Fmt.Int(ring.Len));            // 2
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

### Zia Example

```zia
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

```zia
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

### Zia Example

```zia
module StackDemo;

bind Viper.Terminal;
bind Viper.Collections;
bind Viper.Fmt as Fmt;

func start() {
    var stack = new Stack();
    stack.Push("first");
    stack.Push("second");
    stack.Push("third");
    Say("Length: " + Fmt.Int(stack.Len));                  // 3

    // LIFO order
    Say("Pop: " + Viper.Core.Box.ToStr(stack.Pop()));           // third
    Say("Peek: " + Viper.Core.Box.ToStr(stack.Peek()));         // second
    Say("After pop: " + Fmt.Int(stack.Len));               // 2

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

### Zia Example

```zia
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

```zia
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

```zia
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

## See Also

- [Input/Output](io.md) - File operations for persisting collections
- [Text Processing](text.md) - `StringBuilder` for efficient string building, `Csv` for data import/export
- [Threads](threads.md) - Thread-safe access patterns using `Monitor` or `RwLock`

