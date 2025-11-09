# CODEMAP: Runtime Library (C)

- **src/runtime/rt_array.c**

  Implements the BASIC runtime’s reference-counted int32 array helpers, including allocation, resizing, accessors, and bounds checks. Growth paths zero-fill new elements, handle copy-on-write when multiple references share the buffer, and trap on arithmetic overflow during reallocation to keep state consistent. Helper routines validate heap headers and cooperate with the shared runtime heap so arrays integrate cleanly with the VM’s garbage-aware helpers. Dependencies include `rt_array.h`, the heap primitives in `rt_heap.h`, and standard C headers for memory, assertions, and I/O.

- Headers for quick lookup: **src/runtime/rt_array.h**, **src/runtime/rt_heap.h**, **src/runtime/rt_internal.h**, **src/runtime/rt_math.h**, **src/runtime/rt_random.h**

- **src/runtime/rt_heap.c**

  Provides the core reference-counted heap allocator backing runtime strings and arrays. Allocation routines prepend a tagged header, zero-initialise payloads, and enforce overflow checks so helpers always observe valid metadata. Retain and release operations adjust reference counts with optional debug logging, freeing the block once the last reference is dropped while exposing utilities to query length, capacity, and raw payload pointers. Dependencies include `rt_heap.h` plus the standard C library for memory management, assertions, and debugging output.

- **src/runtime/rt_io.c**

  Houses the runtime’s I/O surface and trap plumbing, including weakly linked `vm_trap` forwarding and printing helpers for strings, integers, and floats. String printers cooperate with heap-managed payloads to honour literal versus heap-backed data, while `rt_input_line` reads stdin into freshly allocated runtime strings with exponential buffer growth. On fatal conditions the module reports a clear diagnostic before exiting so the VM observes consistent trap semantics. Dependencies include `rt_internal.h`, the heap utilities, and standard C headers for I/O, allocation, and assertions.

- **src/runtime/rt_math.c**

  Wraps libc math functions to expose the runtime ABI expected by lowered BASIC programs and the VM bridge. Each helper forwards directly to `<math.h>` while documenting IEEE-754 edge cases, and integer absolute value triggers a trap on overflow to mirror IL semantics. By funnelling all math intrinsics through this shim the compiler can rely on deterministic spellings regardless of host platform quirks. Dependencies include `rt_math.h`, the trap declarations in `rt.hpp`, and standard math and limits headers.

- **src/runtime/rt_memory.c**

  Offers guarded allocation helpers used by runtime string and IO routines, translating IL’s signed size requests into safe `calloc` calls. Negative or excessively large sizes trigger runtime traps, while zero-byte requests allocate a single byte so callers never receive `nullptr`. Centralising these checks keeps front-end generated helpers simple while guaranteeing consistent trap messages. Dependencies include `rt_internal.h` for trap wiring and the C standard library’s allocation APIs.

- **src/runtime/rt_random.c**

  Implements a deterministic 64-bit linear congruential generator that backs BASIC’s `RND` intrinsic. Seed helpers accept signed or unsigned integers to initialise the shared global state, and `rt_rnd` scales the next state into a double within [0, 1) so IL code can rely on floating-point results. Because the generator uses a fixed multiplier and increment the sequence is reproducible across runs unless reseeded. Dependencies are limited to `rt_random.h` and the C runtime headers it includes.

- **src/runtime/rt_string.c**

  Provides the runtime’s string representation and manipulation primitives, including reference counting, concatenation, substring extraction, trimming, case conversion, and numeric parsing. Functions distinguish between immortal literals, heap-backed data, and temporary buffers so reference management remains correct even when sharing payloads. Many helpers allocate new strings via the shared heap and route fatal conditions through `rt_trap`, ensuring VM-visible behaviour stays consistent with the IL specification. Dependencies include `rt_string.h`, `rt_internal.h`, the heap allocator, and standard C headers for character classification, memory, and numeric conversion.

- **src/runtime/rt_file.c**, **src/runtime/rt_file.h**, **src/runtime/rt_file_io.c**, **src/runtime/rt_file_path.c**, **src/runtime/rt_file_path.h**, **src/runtime/rt_term.c**

  File and terminal I/O helpers: open/close/read/write abstractions, BASIC file path utilities, and terminal integration. Respect VM trap conventions on failure.

- **src/runtime/rt_format.c**, **src/runtime/rt_format.h**, **src/runtime/rt_int_format.c**, **src/runtime/rt_int_format.h**, **src/runtime/rt_printf_compat.c**, **src/runtime/rt_printf_compat.h**

  Formatting shims that produce deterministic strings across platforms (integers, floats), plus minimal printf compatibility where needed.

- **src/runtime/rt_fp.c**, **src/runtime/rt_fp.h**

  Floating‑point helpers complementing `rt_math`, covering conversions or edge‑case handling used by string formatting and runtime ops.

- **src/runtime/rt_numeric.c**, **src/runtime/rt_numeric.h**, **src/runtime/rt_numeric_conv.c**

  Numeric conversions and helpers shared by string parsing/formatting and runtime math wrappers.

- **src/runtime/rt_object.c**, **src/runtime/rt_object.h**

  Object header helpers and reference management for heap‑managed payloads, complementing `rt_heap`.

- **src/runtime/rt_string_builder.c**, **src/runtime/rt_string_builder.h**, **src/runtime/rt_string_encode.c**, **src/runtime/rt_string_format.c**, **src/runtime/rt_string_ops.c**, **src/runtime/rt_string.h**

  String builder, encoding/formatting, and operation helpers layered over the core runtime string representation.

- **src/runtime/rt_trap.c**, **src/runtime/rt_trap.h**, **src/runtime/rt_debug.c**, **src/runtime/rt_debug.h**, **src/runtime/rt_error.c**, **src/runtime/rt_error.h**

  Trap and diagnostics plumbing: unified trap reporting (`vm_trap` bridge), debug printing toggles, and error helpers used across the runtime.

- **src/runtime/rt.hpp**

  C++ shim header exposing the C runtime ABI to C++ components with appropriate extern "C" declarations and convenience wrappers.
