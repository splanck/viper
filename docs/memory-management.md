# Viper Memory Management

> **Status**: Active reference. Some aspects (particularly Zia frontend lifetime
> management) are incomplete — see [Known Unsoundness](#known-unsoundness) and
> [v1.0 Roadmap](#v10-roadmap).
>
> **Audience**: Runtime developers, language users, future contributors.

Viper uses a **hybrid memory model**: atomic reference counting as the primary
lifetime mechanism, a slab pool allocator for small strings, and an opt-in
cycle-detecting garbage collector for breaking reference cycles. There is no
generational collector, no arena allocator, and no tracing GC. The design
prioritises determinism and low latency over throughput.

---

## Architecture Overview

```
┌───────────────────────────────────────────────────────────────┐
│                     Viper Program                             │
│  (Zia / BASIC source → IL → VM or Native)                    │
└──────────────────────────┬────────────────────────────────────┘
                           │ calls
┌──────────────────────────▼────────────────────────────────────┐
│                   Unified Heap API                            │
│   rt_heap_alloc · rt_heap_retain · rt_heap_release            │
│   rt_obj_new_i64 · rt_string_from_bytes                       │
│                                                               │
│   ┌─────────────────────┐  ┌───────────────────────────────┐  │
│   │  Pool Allocator     │  │  System Allocator             │  │
│   │  (strings ≤ 512B)   │  │  (arrays, objects, large str) │  │
│   │  Lock-free freelists │  │  malloc / calloc / free       │  │
│   │  4 size classes      │  │                               │  │
│   └─────────────────────┘  └───────────────────────────────┘  │
│                                                               │
│   ┌─────────────────────────────────────────────────────────┐ │
│   │  Cycle-Detecting GC (opt-in)                            │ │
│   │  rt_gc_track · rt_gc_collect · rt_weakref_*             │ │
│   └─────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────┘
```

Every heap object — string, array, or runtime object — is allocated through
`rt_heap_alloc()` and prefixed by a common header:

```
Memory layout:
┌──────────────────────────┬─────────────────────────┐
│     rt_heap_hdr_t        │       Payload            │
│  (metadata + refcount)   │  (string/array/object)   │
└──────────────────────────┴─────────────────────────┘
^                          ^
header address             payload pointer (returned to caller)
```

**Key source files:**

| File | Role |
|------|------|
| `src/runtime/core/rt_heap.h` | Header struct, allocation API |
| `src/runtime/core/rt_heap.c` | Allocation, retain/release, realloc |
| `src/runtime/core/rt_pool.h` / `.c` | Slab pool allocator |
| `src/runtime/core/rt_gc.h` / `.c` | Cycle GC and weak references |
| `src/runtime/oop/rt_object.h` / `.c` | Object allocation, finalizers, resurrection |
| `src/runtime/core/rt_string.h` / `rt_string_ops.c` | String refcounting and operations |
| `src/runtime/core/rt_memory.c` | General-purpose allocation shim (`rt_alloc`) |

---

## Heap Object Header

Every heap-allocated runtime value is preceded by `rt_heap_hdr_t`:

```c
typedef struct rt_heap_hdr {
    uint32_t magic;                // 0x52504956 ('VIPR') — corruption sentinel
    uint16_t kind;                 // RT_HEAP_STRING(1) | RT_HEAP_ARRAY(2) | RT_HEAP_OBJECT(3)
    uint16_t elem_kind;            // RT_ELEM_NONE(0) | I32(1) | I64(2) | F64(3) | U8(4) | STR(5) | BOX(6)
    uint32_t flags;                // bit0 = disposed, bit1 = pool-allocated
    size_t   refcnt;               // Atomic reference count; 1 at creation; SIZE_MAX = immortal
    size_t   len;                  // Current logical length (elements)
    size_t   cap;                  // Allocated capacity (elements)
    size_t   alloc_size;           // Total allocation bytes (header + payload)
    int64_t  class_id;             // Runtime class ID (objects only)
    rt_heap_finalizer_t finalizer; // Optional cleanup callback (objects only)
} rt_heap_hdr_t;
```

**Invariants:**
- `magic == 0x52504956` — validated in debug builds via `RT_HEAP_VALIDATE`
- `refcnt == 1` on fresh allocation; caller owns the initial reference
- `refcnt == SIZE_MAX` = immortal (never freed; used for string literals)
- `len <= cap` maintained by all mutating operations
- Payload address = `(uint8_t*)header + sizeof(rt_heap_hdr_t)`
- Header address = `(uint8_t*)payload - sizeof(rt_heap_hdr_t)`

---

## Reference Counting

### Core API

| Function | Behaviour |
|----------|-----------|
| `rt_heap_retain(payload)` | Atomic increment. No-op for NULL. Traps on overflow (`SIZE_MAX - 1`). |
| `rt_heap_release(payload)` | Atomic decrement. Frees (pool or system) when count reaches zero. |
| `rt_heap_release_deferred(payload)` | Decrement without freeing at zero. Caller must later call `rt_heap_free_zero_ref`. |
| `rt_heap_free_zero_ref(payload)` | Free only if refcount is already zero. No-op otherwise. |

### Memory Ordering

The refcount protocol follows the standard release-acquire pattern used by
`std::shared_ptr`:

- **Retain**: `__ATOMIC_RELAXED` — visibility is sufficient; no ordering constraint.
- **Release (decrement)**: `__ATOMIC_RELEASE` — ensures all writes to the object
  happen-before the decrement is visible to other threads.
- **Release (at zero)**: `__atomic_thread_fence(__ATOMIC_ACQUIRE)` — ensures the
  releasing thread sees all prior writes before freeing.

### String Refcounting

Strings have wrapper functions that dispatch to the heap API:

- `rt_string_ref(s)` → `rt_heap_retain` (returns same pointer for chaining)
- `rt_string_unref(s)` → `rt_heap_release`
- `rt_str_retain_maybe(s)` / `rt_str_release_maybe(s)` — NULL-safe variants

**Immortal strings**: Literal strings created via `rt_str_from_lit()` or
`rt_const_cstr()` have `refcnt == SIZE_MAX` and are never freed. The
retain/release fast path checks for this and short-circuits.

**String interning**: `rt_string_intern(s)` inserts into a global hash table
(FNV-1a, open-addressing, 5/8 load factor). Interned strings are effectively
immortal — retained by the intern table until `rt_string_intern_drain()` is
called (currently only used in tests).

### Object Refcounting

Objects use a two-step release pattern that allows finalizer interleaving:

```
1. rt_obj_release_check0(obj)  → decrements refcount (deferred)
                                  returns 1 if count reached zero
2. [caller runs custom cleanup]
3. rt_obj_free(obj)            → invokes finalizer, then rt_heap_free_zero_ref
```

This split exists because finalizers may need the object's allocation to remain
valid (e.g., to read fields during cleanup).

### Consume Semantics

Some operations consume their operands — they release the input references
and return a new reference:

- `rt_str_concat(a, b)` releases both `a` and `b`, returns a new string
  (or reuses `a` in-place if uniquely owned with sufficient capacity)

### Debug Tracing

Define `VIPER_RC_DEBUG` to enable stderr logging of every retain/release with
the payload address and resulting refcount.

---

## Pool Allocator

The pool reduces `malloc`/`free` overhead for small, frequently-allocated objects
(primarily short strings).

### Size Classes

| Class | Block Size | Blocks/Slab |
|-------|-----------|-------------|
| `RT_POOL_64` | 64 bytes | 64 |
| `RT_POOL_128` | 128 bytes | 64 |
| `RT_POOL_256` | 256 bytes | 64 |
| `RT_POOL_512` | 512 bytes | 64 |

Allocations larger than 512 bytes fall through to `malloc`/`free`.

### Design

- **Lock-free freelists** using tagged pointers: 48-bit address + 16-bit version
  counter for ABA prevention via CAS.
- **Slab allocation**: when a freelist is empty, a new slab of 64 blocks is
  allocated from the system, all blocks are pushed onto the freelist, and one is
  returned.
- **Freed blocks are zeroed** (`memset(0)`) before returning to the freelist.
- **Only strings use the pool**: arrays and objects may need `realloc()`, which
  is incompatible with pool-allocated memory.

### Lifetime

- Pool memory is **never returned to the OS**. Freed blocks stay on the freelist
  for reuse.
- `rt_pool_shutdown()` exists to release all slabs but is **never called** in the
  current codebase.
- `rt_pool_stats(class_idx, &allocated, &free)` provides per-class diagnostics.

---

## Cycle-Detecting Garbage Collector

### Purpose

Reference counting cannot reclaim cycles (A → B → A). The cycle GC supplements
refcounting by detecting and breaking unreachable reference cycles among
explicitly registered objects.

### Algorithm: Trial-Deletion Mark-Sweep

The collector runs synchronously in four phases:

```
Phase 1 — Initialize
  For each tracked object: trial_rc = 1, color = white

Phase 2 — Trial Decrement
  For each tracked object, call its traverse function.
  For each child that is also tracked: decrement child's trial_rc.
  After this phase, objects with trial_rc <= 0 are only referenced
  by other tracked objects (candidate cycle members).

Phase 3 — Scan (Mark Reachable)
  Objects with trial_rc > 0 have external references → mark black.
  Recursively mark all children reachable from black objects.

Phase 4 — Collect
  White (unmarked) objects are unreachable cycle members.
  Untrack them, clear their weak references, invoke finalizers, free.
```

The `trial_rc = 1` assumption means the algorithm assumes exactly one external
reference per tracked object. After trial decrements from tracked children,
objects that still have `trial_rc > 0` are provably reachable. This is
conservative — it may not detect all cycles in one pass if objects have multiple
external references — but it is always safe.

### Registration

Objects must be **explicitly registered** for cycle collection:

```c
rt_gc_track(obj, traverse_fn);   // Register as potentially cyclic
rt_gc_untrack(obj);              // Remove before manual free
rt_gc_is_tracked(obj);           // Query tracking status
```

The `traverse_fn` callback must enumerate all strong references held by the
object by calling `visitor(child, ctx)` for each one.

### Triggering

The collector **never runs automatically**. It must be explicitly invoked:

```c
int64_t freed = rt_gc_collect();  // Run one collection pass
```

Exposed to Viper programs as `Viper.Memory.GC.Collect()`.

### Thread Safety

The GC state (tracked-object table, weak reference registry) is protected by a
global mutex (`pthread_mutex_t` on Unix, `CRITICAL_SECTION` on Windows).
Finalizers run outside the lock to avoid deadlocks.

### Statistics

| Function | Returns |
|----------|---------|
| `rt_gc_tracked_count()` | Number of currently tracked objects |
| `rt_gc_total_collected()` | Cumulative objects freed by cycle collection |
| `rt_gc_pass_count()` | Number of `rt_gc_collect()` invocations |

### Limitations

- **Linear scan**: `find_entry()` is O(N) over the tracked-object array. Will
  degrade with thousands of tracked objects.
- **No automatic triggering**: cycles accumulate silently unless the program
  explicitly calls `GC.Collect()`.
- **Must not be called from finalizers**: the GC holds the mutex during
  collection; a finalizer calling `rt_gc_collect()` would deadlock.
- **Synchronous**: stop-the-world; no concurrent or incremental mode.

---

## Weak References and Finalizers

### Zeroing Weak References

```c
rt_weakref *rt_weakref_new(target);     // Create (does NOT retain target)
void       *rt_weakref_get(ref);        // Dereference (NULL if target freed)
int8_t      rt_weakref_alive(ref);      // Check if target alive
void        rt_weakref_free(ref);       // Destroy handle (does not affect target)
```

When a target object is freed, `rt_gc_clear_weak_refs(target)` automatically
nullifies all weak references pointing to it. This is called from `rt_obj_free()`
and from the GC collector before freeing cycle garbage.

Implementation: hash table of per-target weak reference chains (64 buckets),
protected by the GC mutex.

### Finalizers

```c
rt_obj_set_finalizer(obj, fn);  // Install callback (one per object, replaces previous)
```

- Invoked from `rt_obj_free()` when refcount has already reached zero.
- Only works for `RT_HEAP_OBJECT` kind (not strings or arrays).
- Runs **before** the heap storage is freed.
- Must not call `rt_gc_collect()`.

### Object Resurrection

```c
rt_obj_resurrect(obj);  // Set refcount from 0 → 1 (inside finalizer only)
```

Allows finalizers to prevent deallocation by resetting the refcount. After
`rt_obj_resurrect()`, `rt_heap_free_zero_ref()` observes a non-zero refcount and
skips deallocation. The caller must re-install the finalizer before returning the
object to users.

**Use case**: Vec2/Vec3 thread-local pool recycling. When the pool has space, the
finalizer resurrects the object and pushes it back to the LIFO pool for reuse
without `malloc`/`free` overhead.

---

## Per-Type Ownership Reference

| Type | Allocation | Ownership | Element Management | Notes |
|------|-----------|-----------|-------------------|-------|
| **Strings** | Pool (≤512B) or malloc | Refcounted | N/A | Immortal literals, interning, `rt_str_concat` consumes both operands |
| **Lists** | malloc | Refcounted | Auto retain/release | `rt_list_i64` for unboxed ints (no per-element refcounting) |
| **Sequences** | malloc | Refcounted | **NOT managed** | Caller must manage element lifetimes |
| **Maps** | malloc | Refcounted | Keys copied, values retained | String-keyed |
| **LazySeq** | malloc | **Manual destroy** | On-demand generation | Not refcounted; requires `rt_lazyseq_destroy()` |
| **Boxed values** | malloc | Refcounted | Type-tagged (I64/F64/I1/STR) | Unbox does not consume the box |
| **Objects/Entities** | malloc | Refcounted | Optional finalizer | Optional GC tracking for cycles |
| **Vec2/Vec3** | Thread-local pool (cap 32) | Refcounted + resurrection | Immutable values | Pool recycling via finalizer |
| **Files** | Stack (`RtFile`) | Caller-owned | POSIX fd | Manual close or finalizer-based cleanup |
| **Network** | malloc | Refcounted | N/A | Manual `close()` available |
| **GUI Widgets** | malloc | Refcounted (vgfx) | Widget tree | Managed by GUI framework |
| **LRU Cache** | malloc | Refcounted | Values are **weak** (non-retained) | Finalizer frees internal nodes and bucket array |
| **WeakMap** | malloc | Refcounted | Keys retained, values **weak** | Values may be collected independently |

### Strings

- Created via `rt_string_from_bytes(bytes, len)` — heap-backed, refcount=1
- Created via `rt_const_cstr(literal)` — immortal wrapper, never freed
- Pool-allocated when total size (header + payload) ≤ 512 bytes
- `rt_string_intern(s)` returns the canonical pointer; enables O(1) pointer
  equality. The intern table retains its own reference.
- `rt_str_concat(a, b)` **consumes both operands** (releases a and b). May
  append in-place if `a` is uniquely owned with sufficient capacity.
- UTF-8 encoded, null-terminated. Byte-based indexing (not codepoint).

### Lists

- Elements are retained on store (`push`, `set`), released on removal
  (`remove_at`, `clear`, destructor).
- The list itself is refcounted.
- `rt_list_i64` is a typed variant for unboxed int64 elements with no
  per-element refcounting — separate `len`/`cap` via `rt_heap_set_len`.
- Backing array grows geometrically (doubling).

### Sequences (Seq)

- The container itself is refcounted.
- **Elements are NOT individually reference-counted.** The base `rt_seq` stores
  opaque `void*` pointers without retaining or releasing them.
- This means: placing refcounted objects into a Seq without retaining them is
  unsafe; the Seq may outlive the elements. Similarly, destroying a Seq does not
  release its elements.

### LazySeq

- **Not refcounted** — caller owns the handle.
- Must be destroyed with `rt_lazyseq_destroy()`.
- Viper has no `using`/`Dispose` pattern, so this requires manual lifecycle
  management in user code.
- Lazy sequences can be infinite; collector operations (`ToSeq`, `Count`) may
  not terminate.

### Mixed Ownership in Caches

- **LRU Cache**: GC object containing a `malloc`'d bucket array and node chain.
  Values are weak (non-retained). Finalizer frees all internal allocations.
- **WeakMap**: GC object with `malloc`'d hash table. Keys are retained; values
  are weak. Stale value pointers may be returned if the value was collected.

---

## Compiler Lifetime Emission

This section documents what each compiler frontend does (and doesn't do) about
object lifetimes. This is the most important section for understanding the
soundness of Viper programs.

### BASIC Frontend

The BASIC lowerer (`src/frontends/basic/lower/Emitter.cpp`) emits explicit
retain/release calls:

- **Strings**: `deferReleaseStr(v)` queues a temporary for cleanup.
  `releaseDeferredTemps()` emits `rt_str_release_maybe` at scope boundaries.
- **Objects**: `deferReleaseObj(v, className)` queues objects.
  `rt_obj_release_check0` + `rt_obj_free` emitted at scope boundaries.
- **Arrays**: `emitArrayRelease()` emits type-specific release calls
  (`rt_arr_str_release`, `rt_arr_obj_release`, `rt_arr_f64_release`,
  `rt_arr_i64_release`) at function exit.
- **Array stores**: retain new value, release old value (`emitArrayStore`).
- **Object assignment**: `rt_obj_retain_maybe` on new value, `rt_obj_release_check0`
  + `rt_obj_free` on old value.

**Result**: BASIC programs have well-managed lifetimes. Temporaries are tracked
and released at scope boundaries. Arrays and objects are cleaned up at function
exit.

### Zia Frontend

The Zia lowerer emits **zero** retain/release calls. Confirmed by searching all
Zia lowerer source files — no calls to `rt_heap_retain`, `rt_heap_release`,
`rt_string_ref`, `rt_string_unref`, `rt_obj_retain_maybe`, or
`rt_obj_release_check0`.

Zia relies entirely on runtime ownership conventions:
- Runtime functions that create values return them with refcount=1.
- The caller "owns" this reference by convention.
- **There is no mechanism to release these references when they go out of scope.**

**Result**: Every intermediate string, object, or collection created during Zia
expression evaluation accumulates in memory until process exit. In a tight loop
doing string concatenation or object creation, this means unbounded memory
growth.

### VM (Runner.cpp)

The VM has minimal explicit lifecycle management — only one `rt_string_unref`
call (for command-line argument passing). The VM relies on runtime functions to
manage their own internal retain/release.

---

## Known Unsoundness

Severity-ordered list of memory management gaps:

### 1. CRITICAL: Zia Programs Leak All Temporaries

No compiler-inserted release means every intermediate string, object, or
collection created during expression evaluation accumulates until process exit.
This affects any non-trivial Zia program.

**Example**: `Str.ToUpper(Str.Concat(a, b))` creates an intermediate concat
result that is never released.

### 2. HIGH: No Automatic GC Triggering

The cycle collector only runs when explicitly called via
`Viper.Memory.GC.Collect()`. Programs that create cyclic object graphs (e.g.,
doubly-linked lists, parent-child entity references) without calling
`GC.Collect()` will leak those cycles indefinitely.

### 3. HIGH: Seq Elements Not Lifetime-Managed

`rt_seq` does not retain or release elements. This creates two hazards:
- **Dangling references**: an element may be freed while still referenced by the
  Seq.
- **Leaks**: destroying a Seq does not release its elements.

### 4. MEDIUM: LazySeq Requires Manual Destroy

LazySeq handles are not refcounted and require explicit `destroy()` calls.
Viper has no `using`/`Dispose` language-level pattern, making it easy to
forget cleanup.

### 5. MEDIUM: Pool Memory Never Returned to OS

The slab allocator retains all allocated slabs for the process lifetime.
`rt_pool_shutdown()` exists but is never called. For long-running processes
that create many short strings early, this memory remains allocated even if
never used again.

### 6. MEDIUM: No Shutdown Cleanup Path

No `atexit` handler or main-cleanup path calls `rt_pool_shutdown()` or
`rt_string_intern_drain()`. While the OS reclaims all memory at process exit,
this makes leak-detection tools (Valgrind, ASan) noisy and complicates
embedding Viper in larger applications.

### 7. LOW: Interned Strings Are Immortal

The intern table retains strings forever. `rt_string_intern_drain()` exists
for test cleanup but is not called during normal execution. Programs that
intern many unique strings will see monotonically growing memory.

### 8. LOW: GC Tracking Uses Linear Scan

`find_entry()` in `rt_gc.c` is O(N) over the tracked-object array. This will
degrade with thousands of tracked objects.

---

## v1.0 Roadmap

Proposed improvements in priority order. The goal is a coherent, user-transparent
memory management story where Viper programs don't leak by default.

### P0: Zia Compiler-Inserted Retain/Release

Mirror the BASIC lowerer's `deferReleaseStr`/`deferReleaseObj` pattern in the
Zia frontend:

- Track all temporaries created during expression evaluation.
- At statement boundaries, emit `rt_str_release_maybe` / `rt_obj_release_check0`
  + `rt_obj_free` for each temporary.
- At function exit, release all local variables holding refcounted types.

This is the single highest-impact change for memory correctness.

### P1: Automatic GC Triggering

Add a configurable allocation-count threshold (e.g., every N allocations to
tracked objects, run a collection pass). Must be opt-in or have a sensible
default to avoid latency spikes in real-time programs (games, audio).

Options:
- Allocation-count threshold (simplest)
- Byte-pressure threshold (more precise)
- Periodic timer (least intrusive but less responsive)

### P2: `using` / Dispose Pattern

Add a `using` statement to Zia for deterministic cleanup:

```
using reader = IO.File.OpenReader("data.txt")
    ' reader is automatically closed at block exit
end using
```

Requires:
- A `Dispose()` interface convention.
- Compiler support for `using` blocks that emit cleanup code in all exit paths
  (normal, exception, early return).
- Apply to: LazySeq, file handles, network connections, database connections.

### P3: Pool Memory Budget

Add a configurable memory budget for the pool allocator. When pool usage exceeds
the budget, return slabs to the OS. Requires tracking total allocated bytes per
size class and implementing a slab-return path.

### P4: Shutdown Cleanup

Register an `atexit` handler (or call from the main cleanup path) that invokes:
- `rt_string_intern_drain()`
- `rt_pool_shutdown()`
- Any other global state cleanup

Important for embedding scenarios and leak-detection tool hygiene.

### P5: GC Performance

- Replace the linear `find_entry` with a hash table lookup.
- Consider lock-free data structures for the tracked-object table.
- Evaluate concurrent or incremental collection for large tracked-object sets.

---

## Quick Reference

### Allocation & Lifetime API

| Operation | API Call | Notes |
|-----------|---------|-------|
| Allocate string | `rt_string_from_bytes(bytes, len)` | refcount=1; pool if ≤512B |
| Allocate object | `rt_obj_new_i64(class_id, size)` | refcount=1 |
| Retain | `rt_heap_retain(p)` or `rt_string_ref(s)` | Atomic increment |
| Release | `rt_heap_release(p)` or `rt_string_unref(s)` | Frees at zero |
| Deferred release | `rt_heap_release_deferred(p)` then `rt_heap_free_zero_ref(p)` | Two-step pattern |
| Set finalizer | `rt_obj_set_finalizer(obj, fn)` | Objects only; one per object |
| Resurrect | `rt_obj_resurrect(obj)` | Inside finalizer only; 0→1 |
| Create weak ref | `rt_weakref_new(target)` | Does NOT retain target |
| Read weak ref | `rt_weakref_get(ref)` | Returns NULL if target freed |
| Track for GC | `rt_gc_track(obj, traverse_fn)` | Required for cycle detection |
| Collect cycles | `rt_gc_collect()` | Synchronous; returns count freed |
| Intern string | `rt_string_intern(s)` | Returns canonical pointer |
| Mark disposed | `rt_heap_mark_disposed(p)` | Debug aid; atomic flag |
| Pool stats | `rt_pool_stats(class, &alloc, &free)` | Per-class diagnostics |

### Ownership Rules

1. Every `rt_heap_alloc` / `rt_obj_new_i64` / `rt_string_from_bytes` returns
   ownership (refcount=1). The caller is responsible for releasing.
2. `rt_heap_retain` / `rt_string_ref` to share ownership.
3. `rt_heap_release` / `rt_string_unref` to relinquish ownership.
4. The last release frees the object.
5. Immortal strings (literals, interned) skip the retain/release cycle entirely.
6. **List elements** are auto-managed by the list (retained on store, released
   on removal).
7. **Seq elements** are **NOT** auto-managed — caller's responsibility.
8. The cycle GC only helps objects explicitly registered via `rt_gc_track`.
9. `rt_str_concat` consumes both operands — do not use `a` or `b` after calling.

---

## Glossary

| Term | Definition |
|------|-----------|
| **Immortal string** | A string with `refcnt == SIZE_MAX`; never freed. Created by `rt_str_from_lit()` or `rt_const_cstr()`. |
| **Pool-allocated** | Memory sourced from the slab allocator. Identified by `RT_HEAP_FLAG_POOLED` (bit 1) in the header flags. |
| **Trial deletion** | The algorithm used by the cycle GC: temporarily decrement refcounts to identify objects only reachable through cycles. |
| **Object resurrection** | Re-arming an object's refcount from 0→1 inside a finalizer, preventing deallocation. Used for pool recycling. |
| **Deferred release** | Decrementing the refcount without immediately freeing, allowing cleanup code to run while the allocation remains valid. |
| **Tagged pointer** | A pointer with metadata (version counter) packed in unused upper bits. Used by the pool freelist for ABA prevention. |
| **Consume semantics** | A function that releases its operand references and returns a new reference. Callers must not use the operands after the call. |
