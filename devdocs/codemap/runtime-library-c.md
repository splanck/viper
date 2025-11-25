# CODEMAP: Runtime Library (C)

Portable C runtime library (`src/runtime/`) providing strings, arrays, math, I/O, and OOP support.

## Memory Management

| File | Purpose |
|------|---------|
| `rt_heap.c/h` | Reference-counted heap allocator with tagged headers |
| `rt_memory.c` | Guarded allocation helpers with overflow checks |
| `rt_object.c/h` | Object header helpers for heap-managed payloads |
| `rt_internal.h` | Internal runtime declarations and macros |

## String Operations

| File | Purpose |
|------|---------|
| `rt_string.h` | String representation and API declarations |
| `rt_string_ops.c` | Core string manipulation: concat, substring, trim, case |
| `rt_string_builder.c/h` | StringBuilder runtime class implementation |
| `rt_string_encode.c` | String encoding utilities |
| `rt_string_format.c` | String formatting helpers |

## Arrays

| File | Purpose |
|------|---------|
| `rt_array.c/h` | Reference-counted int32 arrays with bounds checks |
| `rt_array_str.c/h` | String array implementation |
| `rt_array_obj.c/h` | Object array implementation |

## Numeric Operations

| File | Purpose |
|------|---------|
| `rt_math.c/h` | Math intrinsics (wraps libc math functions) |
| `rt_numeric.c/h` | Numeric conversions and helpers |
| `rt_numeric_conv.c` | Additional numeric conversion routines |
| `rt_fp.c/h` | Floating-point helpers and edge cases |
| `rt_random.c/h` | Deterministic 64-bit LCG random number generator |

## Formatting

| File | Purpose |
|------|---------|
| `rt_format.c/h` | Deterministic number-to-string formatting |
| `rt_int_format.c/h` | Integer formatting utilities |
| `rt_printf_compat.c/h` | Minimal printf compatibility layer |

## I/O Operations

| File | Purpose |
|------|---------|
| `rt_io.c` | Console I/O: print strings/integers/floats, input |
| `rt_file.c/h` | File handle management: open, close, read, write |
| `rt_file_io.c` | File I/O operations |
| `rt_file_ext.c` | Extended file operations |
| `rt_file_path.c/h` | BASIC file path utilities |
| `rt_term.c` | Terminal integration and control |

## OOP Support

| File | Purpose |
|------|---------|
| `rt_oop.h` | OOP runtime declarations |
| `rt_oop_dispatch.c` | Virtual method dispatch |
| `rt_type_registry.c` | Runtime type registry |
| `rt_list.c/h` | List runtime class implementation |

## Runtime Context

| File | Purpose |
|------|---------|
| `rt_context.c/h` | Per-VM execution context and state |
| `rt_modvar.c/h` | Module-level variable storage |
| `rt_ns_bridge.c/h` | Namespace bridging for `Viper.*` runtime |
| `rt_args.c/h` | Command-line argument handling |
| `rt_time.c` | Timer and sleep functionality |

## Diagnostics

| File | Purpose |
|------|---------|
| `rt_trap.c/h` | Trap reporting and `vm_trap` bridge |
| `rt_error.c/h` | Error helpers and formatting |
| `rt_debug.c/h` | Debug printing toggles |

## C++ Interface

| File | Purpose |
|------|---------|
| `rt.hpp` | C++ shim with `extern "C"` declarations |
