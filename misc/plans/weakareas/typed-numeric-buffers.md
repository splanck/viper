# Typed Packed Numeric Buffers

**Status:** Completed — `F64Buffer` and `I64Buffer` expose packed numeric storage with
batch operations, interop, tests, and docs.
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
  `rt_array_i64.*`, plus legacy `rt_array.c` for i32) — refcounted contiguous storage
  with bounds-checked, fast, and unchecked accessors plus `resize`/`copy_payload` — but
  it is documented as
  "backing BASIC `DIM`/`REDIM`" and is **not surfaced as a clean language type**.

So the work is mostly *exposure + batch ops*, not a new storage engine.

## Current state (verified)

- `rt_array_f64.h/.c` and `rt_array_i64.h/.c`: `*_new/retain/release/len/cap/get/set/
  get_fast/set_fast/get_unchecked/set_unchecked/resize/copy_payload`, OOB trap.
- BASIC integer arrays lower to `rt_arr_i64_*` because Viper scalar integers are 64-bit.
  `rt_array.c` / i32 exists, but it is not the primary surface for language integers.
- `Collections.List/Seq` already provide `Slice`, `Sort`, `Reverse`, `Clone`,
  conversions — so the *generic* collection story is fine; only the *packed numeric*
  one is missing.

## Goal & scope

- **In:** `Viper.Collections.F64Buffer` and `Viper.Collections.I64Buffer` runtime classes
  wrapping the existing packed primitives, with random access, fill, copy, **slice**,
  conversions, and **batch math** (the part that justifies a packed buffer).
- **Stretch:** compact `I32Buffer` only if a binary/interop use case needs it; do not
  make it the default integer buffer.
- **Stretch:** `Vec3Buffer` for game transform/particle batches.
- **Out (v2):** zero-copy slice *views* (offset+len sharing backing with retain);
  SIMD; GPU upload. Start with copy-slices (matches `List.Slice`).

## Design

A thin runtime class whose instance wraps the existing `rt_arr_f64` / `rt_arr_i64`
payload (which already carries the refcounted heap header). Follow the object/boxing
mechanics of an existing collection class (see `docs/runtime_class_howto.md` and an
exemplar like the `List` implementation in `src/runtime/collections/`).

Avoid copy-paste proliferation: use a small macro/template-style C implementation for
the common buffer operations and instantiate it for f64 and i64. This keeps behavior and
diagnostics identical across element types without adding dependencies.

Batch ops are new, simple, allocation-light C loops operating directly on the packed
payload via `rt_arr_f64_get_unchecked`/`set_unchecked` (after a single length check),
which is exactly what the existing fast-path accessors are for.

## Implementation steps

1. `src/runtime/collections/rt_numbuf.h/.c` — the `F64Buffer`/`I64Buffer` classes:
   `new(len)`, `new_from(seq)`, `len`, `get`, `set`, `fill`, `copy_from`, `slice`,
   `to_list`, `to_seq`, and batch ops (`add_scalar`, `mul_scalar`, `add_buffer`,
   `sum`, `dot`, `min`, `max`). Reuse `rt_arr_f64_*` and `rt_arr_i64_*` for storage.
2. Register classes + methods in `runtime.def` (below); `check_runtime_completeness.sh`.
3. Add to the collections CMake source list.
4. Document under `docs/viperlib/` (collections section); note when to choose
   `F64Buffer` over `List[F64]` (tight numeric batches vs heterogeneous/boxed data).
5. Tests in `src/tests/runtime/`.

## API surface (`runtime.def`)

```
RT_FUNC(F64BufNew,     rt_f64buf_new,      "Viper.Collections.F64Buffer.New",        "obj(i64)")
RT_FUNC(F64BufFromSeq, rt_f64buf_from_seq, "Viper.Collections.F64Buffer.FromSeq",    "obj(obj)")
RT_FUNC(F64BufLen,     rt_f64buf_len,      "Viper.Collections.F64Buffer.get_Length", "i64(obj)")
RT_FUNC(F64BufGet,     rt_f64buf_get,      "Viper.Collections.F64Buffer.Get",        "f64(obj,i64)")
RT_FUNC(F64BufSet,     rt_f64buf_set,      "Viper.Collections.F64Buffer.Set",        "void(obj,i64,f64)")
RT_FUNC(F64BufFill,    rt_f64buf_fill,     "Viper.Collections.F64Buffer.Fill",       "void(obj,f64)")
RT_FUNC(F64BufSlice,   rt_f64buf_slice,    "Viper.Collections.F64Buffer.Slice",      "obj(obj,i64,i64)")
RT_FUNC(F64BufAddS,    rt_f64buf_add_scalar,"Viper.Collections.F64Buffer.AddScalar", "void(obj,f64)")
RT_FUNC(F64BufMulS,    rt_f64buf_mul_scalar,"Viper.Collections.F64Buffer.MulScalar", "void(obj,f64)")
RT_FUNC(F64BufAddB,    rt_f64buf_add_buffer,"Viper.Collections.F64Buffer.AddBuffer", "void(obj,obj)")
RT_FUNC(F64BufSum,     rt_f64buf_sum,      "Viper.Collections.F64Buffer.Sum",        "f64(obj)")
RT_FUNC(F64BufDot,     rt_f64buf_dot,      "Viper.Collections.F64Buffer.Dot",        "f64(obj,obj)")
RT_FUNC(F64BufToList,  rt_f64buf_to_list,  "Viper.Collections.F64Buffer.ToList",     "obj<Viper.Collections.List>(obj)")
// RT_CLASS_BEGIN("Viper.Collections.F64Buffer", F64Buffer, "obj", F64BufNew) ... RT_METHOD/RT_PROP ... RT_CLASS_END
// Mirror for I64Buffer.
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

- Add `F64Buffer`/`I64Buffer` (and `Vec3Buffer` if built) to the `docs/viperlib/`
  collections reference, including a **"packed buffer vs `List[T]`"** guidance box
  (tight numeric/vertex/particle batches vs heterogeneous/boxed data).
- Cross-link from the game/particle docs where batch numeric data is used, and from any
  "choosing a collection" page.
- Update `docs/codemap/` for the new `collections/rt_numbuf.*` files.
- One concise release-notes line.

## Implementation notes

- `src/runtime/collections/rt_numbuf.c/.h` wraps the existing packed array primitives for
  `Viper.Collections.F64Buffer` and `Viper.Collections.I64Buffer`.
- `runtime.def` registers construction, length/count, get/set, fill, copy, slice,
  scalar math, buffer add, sum/dot/min/max, and List/Seq conversion methods.
- `src/tests/runtime/RTNumBufTests.cpp` covers traps, copy-slice semantics, numeric
  operations, conversion, and allocation counters.
- `docs/viperlib/collections/specialized.md`, `docs/codemap/runtime-library-c.md`, and
  release notes document the new surface.

## Verification

- `ctest --test-dir build -R '^test_rt_numbuf$' --output-on-failure`
- `./scripts/check_runtime_completeness.sh`

## Risks / open questions

- **Element-type proliferation:** keep to `F64`/`I64` (+ optional compact `I32` and
  optional `Vec3`); do not
  mint a buffer per scalar type. If Zia generics should eventually express
  `Buffer[T]`, design the names so a future generic facade can map onto these concrete
  classes.
- **Slice contract:** v1 copies (safe, simple). Document clearly so a later zero-copy
  view doesn't silently change aliasing semantics.
- **BASIC reuse:** BASIC `DIM` keeps using the low-level `rt_arr_*` primitives directly;
  this class is additive and language-agnostic.
