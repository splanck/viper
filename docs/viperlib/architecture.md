---
status: active
audience: public
last-verified: 2026-07-14
---

# Runtime Architecture

> Internal architecture and type system reference.

**Part of the [Viper Runtime Library](README.md)**

## Contents

- [Runtime Architecture](#runtime-architecture)
- [Type Reference](#type-reference)

---

## Runtime Architecture

### Overview

The public runtime registry is rooted at `src/il/runtime/runtime.def`. That file
is an ordered manifest for the modular definition fragments under
`src/il/runtime/defs/`; `rtgen` parses the manifest and its declarative macro
grammar. The registry generates:

- `RuntimeNameMap.inc` — Maps canonical names to C symbols
- `RuntimeClasses.inc` — OOP class catalog for the type system
- `RuntimeSignatures.inc` — Runtime-call descriptor rows
- `RuntimeNames.hpp` — Canonical-name constants for C++ consumers
- `ZiaRuntimeExterns.inc` — Zia frontend extern declarations

The same registry also drives the generated Markdown reference under
[`docs/generated/runtime`](../generated/runtime/README.md). Runtime C headers
remain authoritative for the actual C prototypes; `rtgen` reads them while
building and auditing the descriptor table.

### RT_FUNC Syntax

```cpp
RT_FUNC(id, c_symbol, "canonical_name", "signature"[, lowering])
```

- **id**: Unique C++ identifier used in generated code
- **c_symbol**: The C function name (rt_* prefix by convention)
- **canonical_name**: The Viper namespace path (e.g., "Viper.Math.Sin")
- **signature**: IL type signature using type abbreviations
- **lowering**: Optional `always` marker for operations that frontends must
  lower through the registered runtime call; omission uses manual lowering

`RT_INTERNAL_FUNC` has the same shape but hides the standalone function from
public frontend catalogs while leaving it available as a class-member or
lowering target. `RT_BRIDGE(target_id, "role,...")` records safe callback and
payload roles for frontend callback bridges.

### RT_CLASS Syntax

```cpp
RT_CLASS_BEGIN("canonical_name", type_id, "layout", ctor_id)
    RT_PROP("name", "type", getter_id, setter_id_or_none)
    RT_METHOD("name", "signature", target_id)
RT_CLASS_END()
```

Classes define the OOP interface exposed to Viper languages. The constructor,
property getters/setters, and methods reference function IDs declared elsewhere
in the registry. Method signatures omit the receiver (`arg0`). Every public
class definition is preceded by authored `@summary` and `@details`
documentation, which `rtgen` validates and publishes in the generated reference.

### Type Abbreviations

| Abbrev | Registry meaning | IL representation |
|--------|------------------|-------------------|
| `void` | No value | `void` |
| `i1` | Boolean | 1-bit integer |
| `i16` | Short integer | 16-bit integer |
| `i32` | Integer | 32-bit integer |
| `i64` | Long integer | 64-bit integer |
| `f64` | Double | 64-bit float |
| `str` | Runtime string handle | `str` |
| `obj` | Untyped runtime object handle | `ptr` |
| `obj<Viper.Namespace.Type>` | Runtime object handle with frontend type metadata | `ptr` |
| `seq<T>` / `list<T>` | Typed collection metadata for frontends | `ptr` |
| `ptr...` | Raw pointer form for internal descriptors | `ptr` |
| `resume` / `resume_tok` | Exception-resume token | `resume` |

The parser also accepts the aliases `bool` and `string`, plus a trailing `?`
for nullable pointer-like types. Although IL itself has `i8` and `f32`, the
current runtime-signature parser does not accept those two tokens; public C ABI
surfaces widen such values to one of the active types above.

### Quick Reference

| Class                         | Description                                                 |
|-------------------------------|-------------------------------------------------------------|
| `Viper.Collections.StringSet`       | String set with union, intersection, difference             |
| `Viper.Collections.Bytes`     | Efficient byte array                                        |
| `Viper.Collections.List`      | Dynamic list of objects                                     |
| `Viper.Collections.Map`       | String-keyed dictionary                                     |
| `Viper.Collections.Queue`     | FIFO with Push, Pop, Peek                                   |
| `Viper.Collections.Ring`      | Fixed-size circular buffer (overwrites oldest)              |
| `Viper.Collections.Seq`       | Dynamic array with Push, Pop, Get, Set                      |
| `Viper.Collections.Stack`     | LIFO with Push, Pop, Peek                                   |
| `Viper.Core.Convert`          | Type conversion (ToInt64, ToDouble)                         |
| `Viper.Core.Box`              | Boxing primitives for generic collections                   |
| `Viper.Core.Diagnostics`      | Runtime assertions and traps                                |
| `Viper.Crypto.Hash`           | SHA-256, HMAC-SHA256, fast hashing, and constant-time compare |
| `Viper.Crypto.Legacy.Hash`    | CRC32, MD5, SHA1, and legacy HMAC compatibility helpers      |
| `Viper.System.Environment`           | Command-line args, environment variables, process exit      |
| `Viper.Graphics.Canvas`       | Window and 2D drawing                                       |
| `Viper.Graphics.Color`        | RGB/RGBA color creation                                     |
| `Viper.Graphics.Pixels`       | Software image buffer for pixel manipulation                |
| `Viper.IO.BinFile`            | Binary file stream with random access                       |
| `Viper.IO.Dir`                | Directory create/list/delete                                |
| `Viper.IO.File`               | File read/write/copy/delete                                 |
| `Viper.IO.LineReader`         | Line-by-line text file reading                              |
| `Viper.IO.LineWriter`         | Buffered text file writing                                  |
| `Viper.IO.Path`               | Path join/split/normalize                                   |
| `Viper.Math`                  | Math functions (Sin, Cos, Sqrt, etc.) and constants (Pi, E) |
| `Viper.Math.Random`           | Random number generation                                    |
| `Viper.Object`                | Base class with Equals, HashCode, ToString                  |
| `Viper.String`                | String manipulation (Substring, Trim, Replace, etc.)        |
| `Viper.String` (static)       | Static string utilities (Join, FromStr, Equals, etc.)       |
| `Viper.Terminal`              | Terminal I/O (Say, Print, Ask, ReadLine)                    |
| `Viper.Text.Codec`            | Base64, Hex, URL encoding/decoding                          |
| `Viper.Text.StringBuilder`    | Efficient string concatenation                              |
| `Viper.Text.Uuid`             | UUID v4 generation                                          |
| `Viper.Time.Clock`            | Sleep and tick counting                                     |
| `Viper.Time.DateTime`         | Date/time creation and formatting                           |
| `Viper.Time.Stopwatch`        | Benchmarking timer                                          |

---

## Memory Management

### Reference Counting

Managed runtime strings, arrays, and objects share the unified heap header in
`src/runtime/core/rt_heap.h`. The header contains an atomic reference count,
kind and element metadata, an optional class ID/finalizer, and the validation
word `RT_MAGIC = 0x52504956`. Fresh mortal allocations start with one owned
reference; the final release runs object cleanup and frees the allocation.
Immortal values use a reserved high reference-count sentinel and are not
released. Internal raw allocations and explicitly destroyed native handles are
outside this managed-header contract.

### Cycle-Detecting Garbage Collector

Reference counting cannot reclaim cycles (A → B → A). The synchronous,
non-moving cycle collector (`rt_gc`) supplements reference counting for objects
that explicitly register a traversal callback:

1. **Track**: Objects that may form cycles register with `rt_gc_track(obj, traverse_fn)`, providing a callback that
   visits the object's children.
2. **Snapshot**: Initialize each tracked object's trial count from its real heap-header reference count.
3. **Trial decrement**: Traverse the tracked graph and subtract each strong edge between tracked objects from the
   destination's trial count.
4. **Scan**: Objects with `trial_rc > 0` are reachable from outside the candidate graph — mark them black and propagate
   reachability to their children.
5. **Collect**: Finalize the remaining white objects, honor any resurrection, release outgoing references, clear weak
   references, and reclaim confirmed cycle members.

`rt_gc_collect()` runs one pass and returns the number of objects reclaimed.
Automatic triggering is disabled by default but can be enabled with an
allocation threshold. Long-lived tracked objects may be skipped on incremental
passes and are reconsidered by periodic full scans. Statistics are available
through `rt_gc_tracked_count()`, `rt_gc_total_collected()`, and
`rt_gc_pass_count()`.

### Zeroing Weak References

The GC provides zeroing weak references via `rt_weakref`. Targets must be live runtime handles (`NULL`, heap
objects/arrays, or runtime strings); raw foreign pointers are rejected. When a target is collected or released, all
weak references pointing to it are automatically set to NULL. This prevents dangling pointer access.

```c
rt_weakref *ref = rt_weakref_new(target);
void *obj = rt_weakref_get(ref);    // retained target, or NULL if collected
if (obj)
    rt_memory_release(obj);         // release the reference returned by Get
int alive = rt_weakref_alive(ref);  // snapshot only; does not retain the target
rt_weakref_free(ref);               // release the weak-ref handle
```

Weak references use per-target chains in a hash table protected by the GC lock.
`rt_weakref_alive()` is only an observation; use the retained result from
`rt_weakref_get()` when the target must remain alive after the call.

### Collection Ownership Rules

| Collection | Retains elements? | Notes |
|------------|-------------------|-------|
| Seq | Yes through public `New`, `NewSized`, and `WithCapacity` | Internal constructors can create borrowing sequences for runtime snapshots |
| List | Yes | Stored objects are retained; getters return retained references |
| Map | Yes (values) | String keys are copied/retained and values are retained |
| Set | Yes | Elements are retained while present |
| WeakMap | No (values) | Keys and weak-reference handles are owned; values are zeroing weak references |

---

## Type Reference

| Viper Type | IL Type | Description             |
|------------|---------|-------------------------|
| `Integer`  | `i64`   | 64-bit signed integer   |
| `Double`   | `f64`   | 64-bit floating point   |
| `Boolean`  | `i1`    | Boolean (0 or 1)        |
| `String`   | `str`   | Immutable string        |
| `Object`   | `obj`   | Reference to any object |
| `Void`     | `void`  | No return value         |
