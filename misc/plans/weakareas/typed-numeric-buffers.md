# Typed Packed Numeric Buffers

**Status:** Reframed (the real gap behind the inaccurate "arrays are thin" finding)
**Area:** `src/runtime/collections/` (new) reusing `src/runtime/arrays/`
**Effort:** M
**Roadmap fit:** v0.3.x P1 (engine performance) / P3 (missing features)

## Problem

There is no first-class, packed, **typed numeric** collection exposed to the language:

- `Viper.Collections.List` is ergonomic but **boxes every element** (`Get`/`Push` traffic
  in `obj`) — cache-hostile for large numeric/vertex/particle/sample batches.
- `Viper.IO.BinaryBuffer` is **byte/serialization-oriented** (sequential typed
  read/write through a cursor), not a random-access `f64[]` with arithmetic.
- The packed primitive **already exists** (`src/runtime/arrays/rt_array_f64.*`,
  `rt_array.c` for i32) — refcounted contiguous storage with bounds-checked, fast, and
  unchecked accessors plus `resize`/`copy_payload` — but it is documented as
  "backing BASIC `DIM`/`REDIM`" and is **not surfaced as a clean language type**.

So the work is mostly *exposure + batch ops*, not a new storage engine.

## Current state (verified)

- `rt_array_f64.h/.c`, `rt_array.c` (i32): `*_new/retain/release/len/cap/get/set/
  get_fast/set_fast/get_unchecked/set_unchecked/resize/copy_payload`, OOB trap.
- `Collections.List/Seq` already provide `Slice`, `Sort`, `Reverse`, `Clone`,
  conversions — so the *generic* collection story is fine; only the *packed numeric*
  one is missing.

## Goal & scope

- **In:** `Viper.Buffers.F64Buffer` and `Viper.Buffers.I32Buffer` runtime classes
  wrapping the existing packed primitives, with random access, fill, copy, **slice**,
  conversions, and **batch math** (the part that justifies a packed buffer).
- **Stretch:** `Vec3Buffer` for game transform/particle batches.
- **Out (v2):** zero-copy slice *views* (offset+len sharing backing with retain);
  SIMD; GPU upload. Start with copy-slices (matches `List.Slice`).

## Design

A thin runtime class whose instance wraps the existing `rt_arr_f64` payload (which
already carries the refcounted heap header). Follow the object/boxing mechanics of an
existing collection class (see `docs/runtime_class_howto.md` and an exemplar like the
`List` implementation in `src/runtime/collections/`).

Batch ops are new, simple, allocation-light C loops operating directly on the packed
payload via `rt_arr_f64_get_unchecked`/`set_unchecked` (after a single length check),
which is exactly what the existing fast-path accessors are for.

## Implementation steps

1. `src/runtime/collections/rt_numbuf.h/.c` — the `F64Buffer`/`I32Buffer` class:
   `new(len)`, `new_from(seq)`, `len`, `get`, `set`, `fill`, `copy_from`, `slice`,
   `to_list`, `to_seq`, and batch ops (`add_scalar`, `mul_scalar`, `add_buffer`,
   `sum`, `dot`, `min`, `max`). Reuse `rt_arr_f64_*` for storage.
2. Register classes + methods in `runtime.def` (below); `check_runtime_completeness.sh`.
3. Add to the collections CMake source list.
4. Document under `docs/viperlib/` (collections section); note when to choose
   `F64Buffer` over `List[F64]` (tight numeric batches vs heterogeneous/boxed data).
5. Tests in `src/tests/runtime/`.

## API surface (`runtime.def`)

```
RT_FUNC(F64BufNew,     rt_f64buf_new,      "Viper.Buffers.F64Buffer.New",        "obj(i64)")
RT_FUNC(F64BufFromSeq, rt_f64buf_from_seq, "Viper.Buffers.F64Buffer.FromSeq",    "obj(obj)")
RT_FUNC(F64BufLen,     rt_f64buf_len,      "Viper.Buffers.F64Buffer.get_Length", "i64(obj)")
RT_FUNC(F64BufGet,     rt_f64buf_get,      "Viper.Buffers.F64Buffer.Get",        "f64(obj,i64)")
RT_FUNC(F64BufSet,     rt_f64buf_set,      "Viper.Buffers.F64Buffer.Set",        "void(obj,i64,f64)")
RT_FUNC(F64BufFill,    rt_f64buf_fill,     "Viper.Buffers.F64Buffer.Fill",       "void(obj,f64)")
RT_FUNC(F64BufSlice,   rt_f64buf_slice,    "Viper.Buffers.F64Buffer.Slice",      "obj(obj,i64,i64)")
RT_FUNC(F64BufAddS,    rt_f64buf_add_scalar,"Viper.Buffers.F64Buffer.AddScalar", "void(obj,f64)")
RT_FUNC(F64BufMulS,    rt_f64buf_mul_scalar,"Viper.Buffers.F64Buffer.MulScalar", "void(obj,f64)")
RT_FUNC(F64BufAddB,    rt_f64buf_add_buffer,"Viper.Buffers.F64Buffer.AddBuffer", "void(obj,obj)")
RT_FUNC(F64BufSum,     rt_f64buf_sum,      "Viper.Buffers.F64Buffer.Sum",        "f64(obj)")
RT_FUNC(F64BufDot,     rt_f64buf_dot,      "Viper.Buffers.F64Buffer.Dot",        "f64(obj,obj)")
RT_FUNC(F64BufToList,  rt_f64buf_to_list,  "Viper.Buffers.F64Buffer.ToList",     "obj<Viper.Collections.List>(obj)")
// RT_CLASS_BEGIN("Viper.Buffers.F64Buffer", F64Buffer, "obj", F64BufNew) ... RT_METHOD/RT_PROP ... RT_CLASS_END
// Mirror for I32Buffer.
```

## Tests (`src/tests/runtime/`)

- Round-trip new/set/get; `get` OOB traps; `Length` correct after `New`.
- `Slice(2,5)` returns an independent buffer with the right elements; mutating the
  slice does not alter the source (copy semantics in v1).
- `Sum`/`Dot` numerically correct (compare to a reference loop, `EXPECT_NEAR`).
- `AddBuffer` with mismatched lengths **traps** (define and test the contract).
- `ToList`/`FromSeq` interop preserves order and values.
- Refcount/leak check via the runtime allocation counters.
- VM↔native determinism for the numeric ops.

## Cross-platform

Pure C; no platform code; identical on all targets. Keeps VM/native determinism.

## Documentation

- Add `F64Buffer`/`I32Buffer` (and `Vec3Buffer` if built) to the `docs/viperlib/`
  collections reference, including a **"packed buffer vs `List[T]`"** guidance box
  (tight numeric/vertex/particle batches vs heterogeneous/boxed data).
- Cross-link from the game/particle docs where batch numeric data is used, and from any
  "choosing a collection" page.
- Update `docs/codemap/` for the new `collections/rt_numbuf.*` files.
- One concise release-notes line.

## Risks / open questions

- **Element-type proliferation:** keep to `F64`/`I32` (+ optional `Vec3`); do not
  mint a buffer per scalar type. If Zia generics should eventually express
  `Buffer[T]`, design the names so a future generic facade can map onto these concrete
  classes.
- **Slice contract:** v1 copies (safe, simple). Document clearly so a later zero-copy
  view doesn't silently change aliasing semantics.
- **BASIC reuse:** BASIC `DIM` keeps using the low-level `rt_arr_*` primitives directly;
  this class is additive and language-agnostic.
