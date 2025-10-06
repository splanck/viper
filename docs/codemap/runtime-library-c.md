# CODEMAP: Runtime Library (C)

- **src/runtime/rt_array.c**

  Implements the BASIC runtime’s reference-counted int32 array helpers, including allocation, resizing, accessors, and bounds checks. Growth paths zero-fill new elements, handle copy-on-write when multiple references share the buffer, and trap on arithmetic overflow during reallocation to keep state consistent. Helper routines validate heap headers and cooperate with the shared runtime heap so arrays integrate cleanly with the VM’s garbage-aware helpers. Dependencies include `rt_array.h`, the heap primitives in `rt_heap.h`, and standard C headers for memory, assertions, and I/O.

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
