---
status: active
audience: public
last-verified: 2026-02-02
---

# Runtime & VM Guide

This guide consolidates the runtime ABI, VM interpreter internals, and extern symbol bridge. Numeric conversions and
trap
behaviour shared with the BASIC front end are specified in [specs/numerics.md](specs/numerics.md).
For the authoritative error-handling model (trap kinds, handler ABI, and BASIC lowering) see
[specs/errors.md](specs/errors.md).

## Runtime symbol naming

- Canonical runtime entry points are the dotted `Viper.*` names emitted by frontends and catalogued in
  `src/il/runtime/generated/RuntimeSignatures.inc`.
- Native backends must rewrite these to the C symbols exported by the runtime library via
  `il::runtime::mapCanonicalRuntimeName` and the alias table in `src/il/runtime/RuntimeNameMap.hpp`.
- The alias map is the single source of truth for native symbol names; avoid ad-hoc string comparisons in emitters.
- Tests guard the table (`test_runtime_name_map`) and the backends’ usage (`test_emit_x86_runtime_map`); add new
  entries there when extending the runtime.

<a id="runtime-abi"></a>

## Runtime ABI

This document describes the C ABI provided by the runtime library (documented; evolving).

### Math

| Symbol               | Signature         | Semantics                                   |
|----------------------|-------------------|---------------------------------------------|
| `@rt_sqrt`           | `f64 -> f64`      | square root                                 |
| `@rt_floor`          | `f64 -> f64`      | floor                                       |
| `@rt_ceil`           | `f64 -> f64`      | ceiling                                     |
| `@rt_sin`            | `f64 -> f64`      | sine                                        |
| `@rt_cos`            | `f64 -> f64`      | cosine                                      |
| `@rt_pow_f64_chkdom` | `f64, f64 -> f64` | power                                       |
| `@rt_abs_i64`        | `i64 -> i64`      | absolute value (integer, traps on overflow) |
| `@rt_abs_f64`        | `f64 -> f64`      | absolute value (float)                      |

### String operations

| Symbol       | Signature              | Semantics                                                                                |
|--------------|------------------------|------------------------------------------------------------------------------------------|
| `@rt_len`    | `str -> i64`           | Return length of string in bytes                                                         |
| `@rt_concat` | `str, str -> str`      | Concatenate two strings; consumes both operands                                          |
| `@rt_substr` | `str, i64, i64 -> str` | Extract substring from start (0-based) with given length                                 |
| `@rt_left`   | `str, i64 -> str`      | Return leftmost n characters                                                             |
| `@rt_right`  | `str, i64 -> str`      | Return rightmost n characters                                                            |
| `@rt_mid2`   | `str, i64 -> str`      | Return substring from start (1-based) to end                                             |
| `@rt_mid3`   | `str, i64, i64 -> str` | Return substring from start (1-based) with length                                        |
| `@rt_instr2` | `str, str -> i64`      | Find needle in haystack starting at position 1; returns 1-based index or 0               |
| `@rt_instr3` | `i64, str, str -> i64` | Find needle in haystack starting at given position (1-based); returns 1-based index or 0 |
| `@rt_ltrim`  | `str -> str`           | Remove leading whitespace (spaces and tabs)                                              |
| `@rt_rtrim`  | `str -> str`           | Remove trailing whitespace (spaces and tabs)                                             |
| `@rt_trim`   | `str -> str`           | Remove leading and trailing whitespace                                                   |
| `@rt_ucase`  | `str -> str`           | Convert ASCII characters to uppercase                                                    |
| `@rt_lcase`  | `str -> str`           | Convert ASCII characters to lowercase                                                    |
| `@rt_chr`    | `i64 -> str`           | Create single-character string from ASCII code (0-255)                                   |
| `@rt_asc`    | `str -> i64`           | Return ASCII code of first character (0-255), or 0 if empty                              |
| `@rt_str_eq` | `str, str -> i1`       | Compare two strings for equality; returns 1 if equal, 0 otherwise                        |

### String conversion

| Symbol              | Signature    | Semantics                                               |
|---------------------|--------------|---------------------------------------------------------|
| `@rt_to_int`        | `str -> i64` | Parse decimal integer from string; returns 0 if invalid |
| `@rt_to_double`     | `str -> f64` | Parse floating-point value from string                  |
| `@rt_int_to_str`    | `i64 -> str` | Convert integer to decimal string                       |
| `@rt_f64_to_str`    | `f64 -> str` | Convert floating-point to decimal string                |
| `@rt_val`           | `str -> f64` | Parse leading numeric prefix as double                  |
| `@rt_str`           | `f64 -> str` | Convert numeric value to decimal string                 |
| `@rt_str_i32_alloc` | `i32 -> str` | Convert 32-bit integer to string                        |
| `@rt_str_i16_alloc` | `i16 -> str` | Convert 16-bit integer to string                        |
| `@rt_str_d_alloc`   | `f64 -> str` | Convert double to string                                |
| `@rt_str_f_alloc`   | `f32 -> str` | Convert float to string                                 |

### Console I/O

| Symbol             | Signature              | Semantics                                         |
|--------------------|------------------------|---------------------------------------------------|
| `@rt_print_str`    | `str -> void`          | Print string to stdout                            |
| `@rt_print_i64`    | `i64 -> void`          | Print 64-bit integer to stdout                    |
| `@rt_print_f64`    | `f64 -> void`          | Print floating-point value to stdout              |
| `@rt_println_i32`  | `i32 -> void`          | Print 32-bit integer with newline                 |
| `@rt_println_str`  | `ptr -> void`          | Print C string with newline (ptr is const char*)  |
| `@rt_input_line`   | `void -> str`          | Read a line from stdin (without trailing newline) |
| `@rt_split_fields` | `str, ptr, i64 -> i64` | Split comma-separated fields; returns count       |

### File I/O

| Symbol            | Signature         | Semantics                                                     |
|-------------------|-------------------|---------------------------------------------------------------|
| `@rt_eof_ch`      | `i32 -> i32`      | Check end-of-file for channel; returns -1 at EOF, 0 otherwise |
| `@rt_lof_ch`      | `i32 -> i64`      | Get length of file in bytes for channel                       |
| `@rt_loc_ch`      | `i32 -> i64`      | Get current file position for channel                         |
| `@rt_seek_ch_err` | `i32, i64 -> i32` | Seek to absolute byte offset in channel; returns 0 on success |

### Terminal control

| Symbol                        | Signature          | Semantics                                                                                                                                                     | Feature         |
|-------------------------------|--------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------|
| `@rt_term_cls`                | `void -> void`     | Clear screen and home the cursor. No-op when stdout is not a TTY.                                                                                             | `TermCls`       |
| `@rt_term_color_i32`          | `i32, i32 -> void` | Set foreground/background colors; `-1` leaves the channel unchanged. Values 0–7 map to normal colors, 8–15 to bright colors, and ≥16 use 256-color SGR codes. | `TermColor`     |
| `@rt_term_locate_i32`         | `i32, i32 -> void` | Move the cursor to the given 1-based row and column, clamping both to ≥1.                                                                                     | `TermLocate`    |
| `@rt_term_cursor_visible_i32` | `i32 -> void`      | Show (non-zero) or hide (zero) the terminal cursor using ANSI escape sequences.                                                                               | `TermCursor`    |
| `@rt_term_alt_screen_i32`     | `i32 -> void`      | Enter (non-zero) or exit (zero) alternate screen buffer using ANSI DEC Private Mode.                                                                          | `TermAltScreen` |
| `@rt_bell`                    | `void -> void`     | Emit a beep/bell sound using BEL character or platform-specific API.                                                                                          | `Bell`          |

### Keyboard input

| Symbol                   | Signature     | Semantics                                                                    | Feature         |
|--------------------------|---------------|------------------------------------------------------------------------------|-----------------|
| `@rt_getkey_str`         | `void -> str` | Blocking single-byte key read that returns a 1-character string.             | `GetKey`        |
| `@rt_getkey_timeout_i32` | `i32 -> str`  | Block for key with timeout in milliseconds; returns empty string on timeout. | `GetKeyTimeout` |
| `@rt_inkey_str`          | `void -> str` | Non-blocking single-byte key read that returns `""` when no key is ready.    | `InKey`         |

### Memory allocation

| Symbol      | Signature    | Semantics                                           |
|-------------|--------------|-----------------------------------------------------|
| `@rt_alloc` | `i64 -> ptr` | Allocate zeroed memory block of given size in bytes |

### Time

| Symbol         | Signature     | Semantics                                                            |
|----------------|---------------|----------------------------------------------------------------------|
| `@rt_timer_ms` | `void -> i64` | Get current time in milliseconds since an arbitrary epoch; monotonic |

### Environment

| Symbol              | Signature     | Semantics                                                                 |
|---------------------|---------------|---------------------------------------------------------------------------|
| `@rt_args_count`    | `void -> i64` | Return the number of command-line arguments pushed by the host            |
| `@rt_args_get`      | `i64 -> str`  | Return the argument at the given zero-based index (retained)              |
| `@rt_cmdline`       | `void -> str` | Join the full command line into a single string                           |
| `@rt_env_is_native` | `void -> i1`  | Return `1` when running a native binary, `0` when executing inside the VM |

### Random

The runtime exposes a simple deterministic 64-bit linear congruential generator:

```
state = state * 6364136223846793005 + 1
```

| Symbol              | Signature     | Semantics                                        |
|---------------------|---------------|--------------------------------------------------|
| `@rt_randomize_u64` | `u64 -> void` | Set RNG state to given 64-bit unsigned value     |
| `@rt_randomize_i64` | `i64 -> void` | Set RNG state to given 64-bit signed value       |
| `@rt_rnd`           | `void -> f64` | Return random f64 uniformly distributed in [0,1) |

The default state on startup is `0xDEADBEEFCAFEBABE`. `@rt_rnd` returns a `f64` uniformly distributed in `[0,1)` by
taking the top
53 bits of the state and scaling by `2^-53`. For a given seed the sequence of
values is reproducible across platforms.

### Memory model

Strings and arrays ride on the shared heap header defined in [`rt_heap.h`](../../src/runtime/rt_heap.h). The runtime
keeps a magic tag, refcount, length, and capacity alongside every payload and validates these invariants when
`VIPER_RC_DEBUG=1` (enabled for Debug builds). Array handles behave like strings: assignment and parameter passing
retain, scope exits release, and `rt_arr_i32_resize` only clones when the refcount shows shared ownership. The
architecture overview dives deeper into retain/release rules and copy-on-resize
semantics—see [Architecture §Runtime memory model](architecture.md#runtime-memory-model).

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
executed. Enable this via `--trace=il` in `viper -run`:

```
[IL] fn=@foo blk=L3 ip=#12 op=add %t1, %t2 -> %t3
```

### Trace format stability

Trace output is identical across platforms. All numbers use the C locale with
booleans printed as `0` or `1`, integers in base‑10, and floating-point values
formatted using `%.17g`. Line endings are normalized to `\n` even on Windows.

At process start the runtime sets `LC_NUMERIC` to `"C"` to ensure numeric
parsing and printing stay deterministic regardless of the host locale.

### Deterministic numeric I/O

Locale-sensitive conversions are pinned to the C locale for every VM process.
The runtime installs `LC_NUMERIC="C"` during initialization so `strtod` and
`printf`-family entry points always accept and emit a dot (`.`) decimal
separator. All floating-point values that reach user-visible text funnel
through [`rt_format_f64`](../../src/runtime/rt_format.c), which canonicalises the
representation:

- Normal numbers are rendered with `%.15g` and a dot decimal separator, even on
  platforms where the active locale would use a comma.
- `NaN` prints as `NaN`; positive infinity as `Inf`; negative infinity as `-Inf`.
- Runtime helpers such as `rt_print_f64`, `rt_f64_to_str`, and BASIC constant
  folding reuse `rt_format_f64` to keep the output consistent across the
  interpreter, ahead-of-time front ends, and the C runtime bridge.

The determinism guarantees in this section are covered by
[`src/tests/runtime/FloatFormattingTests.cpp`](../../src/tests/runtime/FloatFormattingTests.cpp).

### Recursion

The interpreter allocates a fresh frame for each call. Recursive functions thus
nest frames naturally; see `tests/il/e2e/rec_fact.il` for an end-to-end factorial
example.

<a id="vm-externs"></a>

## VM Externs

The VM runtime bridge recognizes these extern symbols and dispatches them to the C runtime. All symbols documented in
the Runtime ABI section above are available for use in IL programs via `extern` declarations.

Compatibility:

- When built with `-DVIPER_RUNTIME_NS_DUAL=ON`, legacy `@rt_*` externs are accepted as aliases of `@Viper.*`.
- New code should emit `@Viper.*`.

**Common extern patterns:**

```il
# Console I/O (canonical)
extern @Viper.Console.PrintStr(str) -> void
extern @Viper.Console.PrintI64(i64) -> void
extern @Viper.Console.PrintF64(f64) -> void
extern @Viper.Console.ReadLine() -> str

# String operations
extern @Viper.Strings.Len(str) -> i64
extern @Viper.Strings.Concat(str, str) -> str
extern @Viper.Strings.Mid(str, i64, i64) -> str
extern @rt_str_eq(str, str) -> i1   # no Viper alias yet

# String conversion
extern @Viper.Convert.ToInt(str) -> i64
extern @Viper.Strings.FromInt(i64) -> str
extern @Viper.Strings.FromDouble(f64) -> str

# Math
extern @rt_sqrt(f64) -> f64
extern @rt_floor(f64) -> f64
extern @rt_ceil(f64) -> f64
extern @rt_sin(f64) -> f64
extern @rt_cos(f64) -> f64
extern @rt_pow_f64_chkdom(f64, f64) -> f64
extern @rt_abs_i64(i64) -> i64
extern @rt_abs_f64(f64) -> f64

# Memory
extern @rt_alloc(i64) -> ptr

# Terminal control
extern @rt_term_cls() -> void
extern @rt_term_color_i32(i32, i32) -> void
extern @rt_term_locate_i32(i32, i32) -> void
extern @rt_bell() -> void

# Keyboard
extern @rt_getkey_str() -> str
extern @rt_inkey_str() -> str

# Time and random
extern @rt_timer_ms() -> i64
extern @rt_rnd() -> f64
extern @rt_randomize_i64(i64) -> void
```

For the complete list of available runtime functions, see the Runtime ABI section above.

Sources:

- docs/runtime-vm.md#runtime-abi
- docs/runtime-vm.md#vm-internals
- docs/runtime-vm.md#vm-externs
