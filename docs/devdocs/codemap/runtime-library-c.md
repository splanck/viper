# CODEMAP: Runtime Library (C)

Portable C runtime library (`src/runtime/`) providing core types, collections, I/O, text, math,
graphics, audio, input, networking, system, diagnostics, crypto, time, and threading support.

Last updated: 2026-02-04

## Overview

- **Total source files**: 183 (.c/.h/.cpp/.hpp)

## Memory Management

| File            | Purpose                                              |
|-----------------|------------------------------------------------------|
| `rt_heap.c`     | Reference-counted heap allocator with tagged headers |
| `rt_heap.h`     | Heap allocator declarations                          |
| `rt_memory.c`   | Guarded allocation helpers with overflow checks      |
| `rt_object.c`   | Object header helpers for heap-managed payloads      |
| `rt_object.h`   | Object header declarations                           |
| `rt_internal.h` | Internal runtime declarations and macros             |
| `rt_box.c`      | Boxed value types implementation                     |
| `rt_box.h`      | Boxed value type declarations                        |
| `rt_pool.c`     | Object pool for efficient allocation                 |
| `rt_pool.h`     | Object pool declarations                             |

## String Operations

| File                  | Purpose                                                 |
|-----------------------|---------------------------------------------------------|
| `rt_string.h`         | String representation and API declarations              |
| `rt_string_ops.c`     | Core string manipulation: concat, substring, trim, case |
| `rt_string_builder.c` | StringBuilder runtime class implementation              |
| `rt_string_builder.h` | StringBuilder declarations                              |
| `rt_string_encode.c`  | String encoding utilities                               |
| `rt_string_format.c`  | String formatting helpers                               |

## Arrays

| File              | Purpose                                           |
|-------------------|---------------------------------------------------|
| `rt_array.c`      | Reference-counted int32 arrays with bounds checks |
| `rt_array.h`      | Int32 array declarations                          |
| `rt_array_i64.c`  | 64-bit integer array implementation               |
| `rt_array_i64.h`  | 64-bit integer array declarations                 |
| `rt_array_f64.c`  | 64-bit float array implementation                 |
| `rt_array_f64.h`  | 64-bit float array declarations                   |
| `rt_array_str.c`  | String array implementation                       |
| `rt_array_str.h`  | String array declarations                         |
| `rt_array_obj.c`  | Object array implementation                       |
| `rt_array_obj.h`  | Object array declarations                         |
| `rt_bytes.c`      | Byte array/buffer operations                      |
| `rt_bytes.h`      | Byte array declarations                           |

## Collections

| File           | Purpose                           |
|----------------|-----------------------------------|
| `rt_list.c`    | List runtime class implementation |
| `rt_list.h`    | List declarations                 |
| `rt_map.c`     | Hash map implementation           |
| `rt_map.h`     | Hash map declarations             |
| `rt_treemap.c` | Sorted tree map implementation    |
| `rt_treemap.h` | Tree map declarations             |
| `rt_bag.c`     | Unordered bag/multiset collection |
| `rt_bag.h`     | Bag declarations                  |
| `rt_queue.c`   | FIFO queue implementation         |
| `rt_queue.h`   | Queue declarations                |
| `rt_stack.c`   | LIFO stack implementation         |
| `rt_stack.h`   | Stack declarations                |
| `rt_ring.c`    | Ring buffer implementation        |
| `rt_ring.h`    | Ring buffer declarations          |
| `rt_seq.c`            | Sequence/iterator abstraction               |
| `rt_seq.h`            | Sequence declarations                       |
| `rt_seq_functional.c` | Seq functional ops wrappers for IL          |
| `rt_seq_functional.h` | Seq functional wrapper declarations         |
| `rt_pqueue.c`         | Priority queue implementation               |
| `rt_pqueue.h`         | Priority queue declarations                 |

## Numeric Operations

| File               | Purpose                                          |
|--------------------|--------------------------------------------------|
| `rt_math.c`        | Math intrinsics (wraps libc math functions)      |
| `rt_math.h`        | Math function declarations                       |
| `rt_numeric.c`     | Numeric conversions and helpers                  |
| `rt_numeric.h`     | Numeric conversion declarations                  |
| `rt_numeric_conv.c`| Additional numeric conversion routines           |
| `rt_fp.c`          | Floating-point helpers and edge cases            |
| `rt_fp.h`          | Floating-point helper declarations               |
| `rt_random.c`      | Deterministic 64-bit LCG random number generator |
| `rt_random.h`      | Random number generator declarations             |
| `rt_bits.c`        | Bitwise operations and utilities                 |
| `rt_bits.h`        | Bitwise operation declarations                   |
| `rt_vec2.c`        | 2D vector math operations                        |
| `rt_vec2.h`        | 2D vector declarations                           |
| `rt_vec3.c`        | 3D vector math operations                        |
| `rt_vec3.h`        | 3D vector declarations                           |

## Formatting & Output

| File                 | Purpose                                   |
|----------------------|-------------------------------------------|
| `rt_format.c`        | Deterministic number-to-string formatting |
| `rt_format.h`        | Format function declarations              |
| `rt_int_format.c`    | Integer formatting utilities              |
| `rt_int_format.h`    | Integer format declarations               |
| `rt_printf_compat.c` | Minimal printf compatibility layer        |
| `rt_printf_compat.h` | Printf compatibility declarations         |
| `rt_fmt.c`           | General formatting utilities              |
| `rt_fmt.h`           | General format declarations               |
| `rt_output.c`        | Output stream handling                    |
| `rt_output.h`        | Output stream declarations                |
| `rt_log.c`           | Logging infrastructure                    |
| `rt_log.h`           | Logging declarations                      |

## I/O Operations

| File              | Purpose                                           |
|-------------------|---------------------------------------------------|
| `rt_io.c`         | Console I/O: print strings/integers/floats, input |
| `rt_file.c`       | File handle management: open, close, read, write  |
| `rt_file.h`       | File handle declarations                          |
| `rt_file_io.c`    | File I/O operations                               |
| `rt_file_ext.c`   | Extended file operations                          |
| `rt_file_ext.h`   | Extended file operation declarations              |
| `rt_file_path.c`  | BASIC file path utilities                         |
| `rt_file_path.h`  | File path utility declarations                    |
| `rt_binfile.c`    | Binary file read/write operations                 |
| `rt_binfile.h`    | Binary file declarations                          |
| `rt_linereader.c` | Line-by-line file reading                         |
| `rt_linereader.h` | Line reader declarations                          |
| `rt_linewriter.c` | Line-by-line file writing                         |
| `rt_linewriter.h` | Line writer declarations                          |
| `rt_memstream.c`  | In-memory stream operations                       |
| `rt_memstream.h`  | Memory stream declarations                        |
| `rt_stream.c`     | Unified stream abstraction over BinFile/MemStream |
| `rt_stream.h`     | Stream interface declarations                     |
| `rt_term.c`       | Terminal integration and control                  |

## Text Processing

| File           | Purpose                              |
|----------------|--------------------------------------|
| `rt_codec.c`   | Text encoding/decoding (UTF-8, etc.) |
| `rt_codec.h`   | Codec declarations                   |
| `rt_csv.c`     | CSV parsing and generation           |
| `rt_csv.h`     | CSV declarations                     |
| `rt_parse.c`   | Text parsing utilities               |
| `rt_parse.h`   | Parse utility declarations           |
| `rt_regex.c`   | Regular expression matching          |
| `rt_regex.h`   | Regex declarations                   |
| `rt_template.c`| Template string processing           |
| `rt_template.h`| Template declarations                |
| `rt_guid.c`    | GUID/UUID generation                 |
| `rt_guid.h`    | GUID declarations                    |

## Archive & Compression

| File           | Purpose                   |
|----------------|---------------------------|
| `rt_archive.c` | Archive file handling     |
| `rt_archive.h` | Archive declarations      |
| `rt_compress.c`| Compression/decompression |
| `rt_compress.h`| Compression declarations  |

## Time & Timers

| File             | Purpose                            |
|------------------|------------------------------------|
| `rt_time.c`      | Timer and sleep functionality      |
| `rt_datetime.c`  | Date and time operations           |
| `rt_datetime.h`  | DateTime declarations              |
| `rt_countdown.c` | Countdown timer implementation     |
| `rt_countdown.h` | Countdown declarations             |
| `rt_stopwatch.c` | Stopwatch/elapsed time measurement |
| `rt_stopwatch.h` | Stopwatch declarations             |

## System & Environment

| File           | Purpose                      |
|----------------|------------------------------|
| `rt_dir.c`     | Directory operations         |
| `rt_dir.h`     | Directory declarations       |
| `rt_path.c`    | Path manipulation utilities  |
| `rt_path.h`    | Path utility declarations    |
| `rt_exec.c`    | Process execution            |
| `rt_exec.h`    | Process execution declarations|
| `rt_machine.c` | Machine/system information   |
| `rt_machine.h` | Machine info declarations    |
| `rt_watcher.c` | File system watcher          |
| `rt_watcher.h` | Watcher declarations         |
| `rt_platform.h`| Platform detection macros    |

## Graphics

| File           | Purpose                          |
|----------------|----------------------------------|
| `rt_graphics.c`| 2D graphics rendering            |
| `rt_graphics.h`| Graphics declarations            |
| `rt_pixels.c`  | Pixel buffer operations          |
| `rt_pixels.h`  | Pixel buffer declarations        |
| `rt_font.c`    | Font loading and text rendering  |
| `rt_font.h`    | Font declarations                |
| `rt_sprite.c`  | Sprite rendering and animation   |
| `rt_sprite.h`  | Sprite declarations              |
| `rt_tilemap.c` | Tilemap rendering for 2D games   |
| `rt_tilemap.h` | Tilemap declarations             |
| `rt_camera.c`  | 2D camera for viewport control   |
| `rt_camera.h`  | Camera declarations              |

## Audio

| File         | Purpose                             |
|--------------|-------------------------------------|
| `rt_audio.c` | Audio playback and sound management |
| `rt_audio.h` | Audio declarations                  |

## GUI

| File       | Purpose                                |
|------------|----------------------------------------|
| `rt_gui.c` | Widget-based UI bindings (Viper.GUI.*) |
| `rt_gui.h` | GUI declarations                       |

## Input

| File            | Purpose                  |
|-----------------|--------------------------|
| `rt_input.c`    | Keyboard and mouse input |
| `rt_input.h`    | Input declarations       |
| `rt_input_pad.c`| Gamepad/controller input |

## Networking

| File               | Purpose                    |
|--------------------|----------------------------|
| `rt_network.c`     | TCP/UDP socket operations  |
| `rt_network.h`     | Network declarations       |
| `rt_network_http.c`| HTTP client implementation |

## Cryptography

| File            | Purpose                                       |
|-----------------|-----------------------------------------------|
| `rt_cipher.c`   | High-level encryption (ChaCha20-Poly1305)     |
| `rt_cipher.h`   | Cipher API declarations                       |
| `rt_hash.c`     | Hash functions (CRC32, MD5, SHA1, SHA256)     |
| `rt_hash.h`     | Hash declarations                             |
| `rt_keyderive.c`| Key derivation (PBKDF2, HKDF)                 |
| `rt_keyderive.h`| Key derivation declarations                   |
| `rt_rand.c`     | Cryptographically secure random numbers       |
| `rt_rand.h`     | Secure random declarations                    |

## OOP Support

| File                | Purpose                 |
|---------------------|-------------------------|
| `rt_oop.h`          | OOP runtime declarations|
| `rt_oop_dispatch.c` | Virtual method dispatch |
| `rt_type_registry.c`| Runtime type registry   |

## Runtime Context

| File            | Purpose                                  |
|-----------------|------------------------------------------|
| `rt_context.c`  | Per-VM execution context and state       |
| `rt_context.h`  | Context declarations                     |
| `rt_modvar.c`   | Module-level variable storage            |
| `rt_modvar.h`   | Module variable declarations             |
| `rt_ns_bridge.c`| Namespace bridging for `Viper.*` runtime |
| `rt_ns_bridge.h`| Namespace bridge declarations            |
| `rt_args.c`     | Command-line argument handling           |
| `rt_args.h`     | Argument handling declarations           |

## Threading

| File                       | Purpose                                             |
|----------------------------|-----------------------------------------------------|
| `rt_threads.c`             | OS thread helpers backing `Viper.Threads.Thread`    |
| `rt_threads.h`             | Thread declarations                                 |
| `rt_threads_primitives.cpp`| Low-level threading primitives (C++ implementation) |
| `rt_monitor.c`             | FIFO-fair, re-entrant monitor backing `Monitor`     |
| `rt_safe_i64.c`            | FIFO-serialized safe integer backing `SafeI64`      |

## Diagnostics & Errors

| File         | Purpose                             |
|--------------|-------------------------------------|
| `rt_trap.c`  | Trap reporting and `vm_trap` bridge |
| `rt_trap.h`  | Trap declarations                   |
| `rt_error.c` | Error helpers and formatting        |
| `rt_error.h` | Error declarations                  |
| `rt_debug.c` | Debug printing toggles              |
| `rt_debug.h` | Debug declarations                  |
| `rt_exc.c`   | Exception handling support          |
| `rt_exc.h`   | Exception declarations              |

## C++ Interface

| File     | Purpose                                 |
|----------|-----------------------------------------|
| `rt.hpp` | C++ shim with `extern "C"` declarations |
