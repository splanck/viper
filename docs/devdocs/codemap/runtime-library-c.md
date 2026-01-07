# CODEMAP: Runtime Library (C)

Portable C runtime library (`src/runtime/`) providing core types, collections, I/O, text, math,
graphics, input, networking, system, diagnostics, crypto, time, and threading support.

## Memory Management

| File            | Purpose                                              |
|-----------------|------------------------------------------------------|
| `rt_heap.c/h`   | Reference-counted heap allocator with tagged headers |
| `rt_memory.c`   | Guarded allocation helpers with overflow checks      |
| `rt_object.c/h` | Object header helpers for heap-managed payloads      |
| `rt_internal.h` | Internal runtime declarations and macros             |
| `rt_box.c/h`    | Boxed value types                                    |

## String Operations

| File                    | Purpose                                                 |
|-------------------------|---------------------------------------------------------|
| `rt_string.h`           | String representation and API declarations              |
| `rt_string_ops.c`       | Core string manipulation: concat, substring, trim, case |
| `rt_string_builder.c/h` | StringBuilder runtime class implementation              |
| `rt_string_encode.c`    | String encoding utilities                               |
| `rt_string_format.c`    | String formatting helpers                               |

## Arrays

| File               | Purpose                                           |
|--------------------|---------------------------------------------------|
| `rt_array.c/h`     | Reference-counted int32 arrays with bounds checks |
| `rt_array_i64.c/h` | 64-bit integer array implementation               |
| `rt_array_f64.c/h` | 64-bit float array implementation                 |
| `rt_array_str.c/h` | String array implementation                       |
| `rt_array_obj.c/h` | Object array implementation                       |
| `rt_bytes.c/h`     | Byte array/buffer operations                      |

## Collections

| File              | Purpose                                    |
|-------------------|--------------------------------------------|
| `rt_list.c/h`     | List runtime class implementation          |
| `rt_map.c/h`      | Hash map implementation                    |
| `rt_treemap.c/h`  | Sorted tree map implementation             |
| `rt_bag.c/h`      | Unordered bag/multiset collection          |
| `rt_queue.c/h`    | FIFO queue implementation                  |
| `rt_stack.c/h`    | LIFO stack implementation                  |
| `rt_ring.c/h`     | Ring buffer implementation                 |
| `rt_seq.c/h`      | Sequence/iterator abstraction              |
| `rt_heap.c/h`     | Priority heap (min/max heap)               |
| `rt_pqueue.c/h`   | Priority queue implementation              |

## Numeric Operations

| File                | Purpose                                          |
|---------------------|--------------------------------------------------|
| `rt_math.c/h`       | Math intrinsics (wraps libc math functions)      |
| `rt_numeric.c/h`    | Numeric conversions and helpers                  |
| `rt_numeric_conv.c` | Additional numeric conversion routines           |
| `rt_fp.c/h`         | Floating-point helpers and edge cases            |
| `rt_random.c/h`     | Deterministic 64-bit LCG random number generator |
| `rt_bits.c/h`       | Bitwise operations and utilities                 |
| `rt_vec2.c/h`       | 2D vector math operations                        |
| `rt_vec3.c/h`       | 3D vector math operations                        |

## Formatting & Output

| File                   | Purpose                                   |
|------------------------|-------------------------------------------|
| `rt_format.c/h`        | Deterministic number-to-string formatting |
| `rt_int_format.c/h`    | Integer formatting utilities              |
| `rt_printf_compat.c/h` | Minimal printf compatibility layer        |
| `rt_fmt.c/h`           | General formatting utilities              |
| `rt_output.c/h`        | Output stream handling                    |
| `rt_log.c/h`           | Logging infrastructure                    |

## I/O Operations

| File               | Purpose                                           |
|--------------------|---------------------------------------------------|
| `rt_io.c`          | Console I/O: print strings/integers/floats, input |
| `rt_file.c/h`      | File handle management: open, close, read, write  |
| `rt_file_io.c`     | File I/O operations                               |
| `rt_file_ext.c/h`  | Extended file operations                          |
| `rt_file_path.c/h` | BASIC file path utilities                         |
| `rt_binfile.c/h`   | Binary file read/write operations                 |
| `rt_linereader.c/h`| Line-by-line file reading                         |
| `rt_linewriter.c/h`| Line-by-line file writing                         |
| `rt_memstream.c/h` | In-memory stream operations                       |
| `rt_term.c`        | Terminal integration and control                  |

## Text Processing

| File              | Purpose                              |
|-------------------|--------------------------------------|
| `rt_codec.c/h`    | Text encoding/decoding (UTF-8, etc.) |
| `rt_csv.c/h`      | CSV parsing and generation           |
| `rt_parse.c/h`    | Text parsing utilities               |
| `rt_regex.c/h`    | Regular expression matching          |
| `rt_template.c/h` | Template string processing           |
| `rt_guid.c/h`     | GUID/UUID generation                 |

## Archive & Compression

| File              | Purpose                       |
|-------------------|-------------------------------|
| `rt_archive.c/h`  | Archive file handling         |
| `rt_compress.c/h` | Compression/decompression     |

## Time & Timers

| File                | Purpose                              |
|---------------------|--------------------------------------|
| `rt_time.c`         | Timer and sleep functionality        |
| `rt_datetime.c/h`   | Date and time operations             |
| `rt_countdown.c/h`  | Countdown timer implementation       |
| `rt_stopwatch.c/h`  | Stopwatch/elapsed time measurement   |

## System & Environment

| File              | Purpose                              |
|-------------------|--------------------------------------|
| `rt_dir.c/h`      | Directory operations                 |
| `rt_path.c/h`     | Path manipulation utilities          |
| `rt_exec.c/h`     | Process execution                    |
| `rt_machine.c/h`  | Machine/system information           |
| `rt_watcher.c/h`  | File system watcher                  |
| `rt_platform.h`   | Platform detection macros            |

## Graphics

| File              | Purpose                              |
|-------------------|--------------------------------------|
| `rt_graphics.c/h` | 2D graphics rendering                |
| `rt_pixels.c/h`   | Pixel buffer operations              |
| `rt_font.c/h`     | Font loading and text rendering      |

## Input

| File              | Purpose                              |
|-------------------|--------------------------------------|
| `rt_input.c/h`    | Keyboard and mouse input             |
| `rt_input_pad.c`  | Gamepad/controller input             |

## Networking

| File                | Purpose                            |
|---------------------|------------------------------------|
| `rt_network.c/h`    | TCP/UDP socket operations          |
| `rt_network_http.c` | HTTP client implementation         |

## Cryptography

| File               | Purpose                                     |
|--------------------|---------------------------------------------|
| `rt_hash.c/h`      | Hash functions (CRC32, MD5, SHA1, SHA256)   |
| `rt_keyderive.c/h` | Key derivation (PBKDF2)                     |
| `rt_rand.c/h`      | Cryptographically secure random numbers     |

## OOP Support

| File                 | Purpose                           |
|----------------------|-----------------------------------|
| `rt_oop.h`           | OOP runtime declarations          |
| `rt_oop_dispatch.c`  | Virtual method dispatch           |
| `rt_type_registry.c` | Runtime type registry             |

## Runtime Context

| File               | Purpose                                  |
|--------------------|------------------------------------------|
| `rt_context.c/h`   | Per-VM execution context and state       |
| `rt_modvar.c/h`    | Module-level variable storage            |
| `rt_ns_bridge.c/h` | Namespace bridging for `Viper.*` runtime |
| `rt_args.c/h`      | Command-line argument handling           |

## Threading

| File              | Purpose                                              |
|-------------------|------------------------------------------------------|
| `rt_threads.c/h`  | OS thread helpers backing `Viper.Threads.Thread`     |
| `rt_monitor.c`    | FIFO-fair, re-entrant monitor backing `Monitor`      |
| `rt_safe_i64.c`   | FIFO-serialized safe integer backing `SafeI64`       |

## Diagnostics & Errors

| File           | Purpose                             |
|----------------|-------------------------------------|
| `rt_trap.c/h`  | Trap reporting and `vm_trap` bridge |
| `rt_error.c/h` | Error helpers and formatting        |
| `rt_debug.c/h` | Debug printing toggles              |
| `rt_exc.c/h`   | Exception handling support          |

## C++ Interface

| File     | Purpose                                 |
|----------|-----------------------------------------|
| `rt.hpp` | C++ shim with `extern "C"` declarations |
