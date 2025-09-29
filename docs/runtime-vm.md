---
status: active
audience: public
last-verified: 2025-09-23
---

# Runtime & VM Guide

This guide consolidates the runtime ABI, VM interpreter internals, and extern symbol bridge. Numeric conversions and trap
behaviour shared with the BASIC front end are specified in [specs/numerics.md](specs/numerics.md).
For the authoritative error-handling model (trap kinds, handler ABI, and BASIC lowering) see
[specs/errors.md](specs/errors.md).

<a id="runtime-abi"></a>
## Runtime ABI

This document describes the stable C ABI provided by the runtime library.

### Math

| Symbol | Signature | Semantics |
|--------|-----------|-----------|
| `@rt_sqrt` | `f64 -> f64` | square root |
| `@rt_floor` | `f64 -> f64` | floor |
| `@rt_ceil` | `f64 -> f64` | ceiling |
| `@rt_sin` | `f64 -> f64` | sine |
| `@rt_cos` | `f64 -> f64` | cosine |
| `@rt_pow_f64_chkdom` | `f64, f64 -> f64` | power |
| `@rt_abs_i64` | `i64 -> i64` | absolute value (integer, traps on overflow) |
| `@rt_abs_f64` | `f64 -> f64` | absolute value (float) |

### Random

The runtime exposes a simple deterministic 64-bit linear congruential generator:

```
state = state * 6364136223846793005 + 1
```

`@rt_randomize_u64` and `@rt_randomize_i64` set the internal state exactly. The
default state on startup is `0xDEADBEEFCAFEBABE`.

`@rt_rnd` returns a `f64` uniformly distributed in `[0,1)` by taking the top
53 bits of the state and scaling by `2^-53`. For a given seed the sequence of
values is reproducible across platforms.

### Memory model

Strings and arrays ride on the shared heap header defined in [`rt_heap.h`](../src/runtime/rt_heap.h). The runtime keeps a magic tag, refcount, length, and capacity alongside every payload and validates these invariants when `VIPER_RC_DEBUG=1` (enabled for Debug builds). Array handles behave like strings: assignment and parameter passing retain, scope exits release, and `rt_arr_i32_resize` only clones when the refcount shows shared ownership. The architecture overview dives deeper into retain/release rules and copy-on-resize semantics—see [Architecture §Runtime memory model](architecture.md#runtime-memory-model).

<a id="vm-internals"></a>
## VM Internals

### Block Parameter Slots

On control transfer the interpreter evaluates branch arguments and stores them
into parameter slots within the active frame. Each slot is keyed by the
parameter's value identifier. When a block is entered these slots are copied
into the register file, making block parameters available as normal SSA values.
Blocks with no parameters skip this step to remain fast.

### Tracing hooks

The interpreter can emit a deterministic trace of executed IL instructions. On
each dispatch, `TraceSink::onStep` records the current instruction before it is
executed. Enable this via `--trace=il` in `ilc -run`:

```
[IL] fn=@foo blk=L3 ip=#12 op=add %t1, %t2 -> %t3
```

### Trace format stability

Trace output is identical across platforms. All numbers use the C locale with
booleans printed as `0` or `1`, integers in base‑10, and floating-point values
formatted using `%.17g`. Line endings are normalized to `\n` even on Windows.

### Recursion

The interpreter allocates a fresh frame for each call. Recursive functions thus
nest frames naturally; see `tests/il/e2e/rec_fact.il` for an end-to-end factorial
example.

<a id="vm-externs"></a>
## VM Externs

The VM runtime bridge recognizes these symbols:

- rt_print_str
- rt_print_i64
- rt_print_f64
- rt_len
- rt_concat
- rt_substr
- rt_str_eq
- rt_input_line
- rt_to_int
- rt_int_to_str
- rt_f64_to_str
- rt_alloc
- rt_sqrt
- rt_floor
- rt_ceil
- rt_sin
- rt_cos
- rt_pow_f64_chkdom
- rt_abs_i64
- rt_abs_f64

Sources:
- docs/runtime-vm.md#runtime-abi
- docs/runtime-vm.md#vm-internals
- docs/runtime-vm.md#vm-externs
