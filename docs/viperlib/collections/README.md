---
status: active
audience: public
last-verified: 2026-06-20
---

# Collections
> Data structures and container types for the Viper runtime.

**Part of [Viper Runtime Library](../README.md)**

---

## Contents

| File | Contents |
|------|----------|
| [Sequential Collections](sequential.md) | List, Queue, Stack, Deque, Ring, Heap |
| [Maps & Sets](maps-sets.md) | Map, Set, OrderedMap, SortedSet, FrozenMap, FrozenSet, TreeMap |
| [Specialized Maps](multi-maps.md) | BiMap, MultiMap, CountMap, IntMap, DefaultMap, LruCache, WeakMap, SparseArray |
| [Functional & Lazy](functional.md) | Seq, LazySeq, Iterator |
| [Specialized Structures](specialized.md) | F64Buffer, I64Buffer, Bag, BloomFilter, Trie, UnionFind, BitSet, Bytes |

## Runtime Correctness Notes

- String-keyed collection types compare the full string byte length. Embedded NUL bytes are significant and do not truncate keys or set elements.
- Owning collections retain stored objects and release them when overwritten, removed, cleared, or finalized. WeakMap is the exception: it stores zeroing weak references and does not keep values alive.
- Collection APIs that expose keys, values, indices, or sorted slices return snapshots. String snapshots own copied strings, object-value snapshots retain their elements, and integer index/key snapshots use boxed `i64` values.
- Stack and Queue default to borrowed elements at the C runtime layer, but conversion helpers such as `Seq.ToStack`, `List.ToQueue`, and `Stack.ToSeq` create retained snapshots so returned containers remain valid after the source is cleared.
- Packed numeric buffers (`F64Buffer`, `I64Buffer`) own contiguous primitive payloads internally. `Slice` returns an independent copy, while `ToList` and `ToSeq` box values into ordinary object collections.
- Capacity and count growth paths trap on overflow instead of wrapping into undersized allocations.

## See Also

- [Input/Output](../io/README.md) - File operations for persisting collections
- [Text Processing](../text/README.md) - `StringBuilder` for efficient string building, `Csv` for data import/export
- [Threads](../threads.md) - Thread-safe access patterns using `Monitor` or `RwLock`
