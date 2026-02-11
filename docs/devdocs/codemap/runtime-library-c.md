# CODEMAP: Runtime Library (C)

Portable C runtime library (`src/runtime/`) providing core types, collections, I/O, text, math,
graphics, audio, input, networking, system, diagnostics, crypto, time, and threading support.

Last updated: 2026-02-09

## Overview

- **Total source files**: 384 (.c/.h/.cpp/.hpp)

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
| `rt_gc.c`       | Cycle-detecting garbage collector (trial deletion algorithm) |
| `rt_gc.h`       | GC and zeroing weak reference declarations           |

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
| `rt_iter.c`           | Unified stateful iterator protocol for collections |
| `rt_iter.h`           | Iterator declarations                       |
| `rt_concmap.c`        | Thread-safe concurrent hash map with string keys |
| `rt_concmap.h`        | Concurrent map declarations                 |

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
| `rt_quat.c`        | Quaternion math for 3D rotations (SLERP, Euler, axis-angle) |
| `rt_quat.h`        | Quaternion declarations                          |
| `rt_spline.c`      | Spline interpolation (Catmull-Rom, cubic Bezier, linear) |
| `rt_spline.h`      | Spline declarations                              |

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
| `rt_json_stream.c` | SAX-style streaming JSON parser  |
| `rt_json_stream.h` | Streaming JSON parser declarations |
| `rt_serialize.c`    | Unified serialization interface (JSON/XML/YAML/TOML/CSV) |
| `rt_serialize.h`    | Serialization interface declarations |

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
| `rt_camera.c`       | 2D camera for viewport control   |
| `rt_camera.h`       | Camera declarations              |
| `rt_spritesheet.c`  | Sprite sheet/atlas region extraction |
| `rt_spritesheet.h`  | Sprite sheet declarations        |

## Audio

| File         | Purpose                             |
|--------------|-------------------------------------|
| `rt_audio.c` | Audio playback and sound management |
| `rt_audio.h` | Audio declarations                  |

## GUI

| File       | Purpose                                |
|------------|----------------------------------------|
| `rt_gui.h` | GUI declarations (Viper.GUI.*)         |

## Input

| File            | Purpose                  |
|-----------------|--------------------------|
| `rt_input.c`    | Keyboard and mouse input |
| `rt_input.h`    | Input declarations       |
| `rt_input_pad.c`| Gamepad/controller input |
| `rt_keychord.c` | Key chord and combo detection |
| `rt_keychord.h` | Key chord declarations   |

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
| `rt_async.c`               | Async task combinators (Run, WaitAll, WaitAny, Map) |
| `rt_async.h`               | Async task declarations                             |

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

## Game & Animation

| File                | Purpose                                              |
|---------------------|------------------------------------------------------|
| `rt_physics2d.c`    | 2D physics engine with rigid bodies and AABB collision |
| `rt_physics2d.h`    | Physics 2D declarations                              |
| `rt_collision.c`    | Collision detection utilities                        |
| `rt_collision.h`    | Collision declarations                               |
| `rt_particle.c`     | Particle system implementation                       |
| `rt_particle.h`     | Particle system declarations                         |
| `rt_scene.c`        | Scene management                                     |
| `rt_scene.h`        | Scene declarations                                   |
| `rt_easing.c`       | Easing functions for animation                       |
| `rt_easing.h`       | Easing declarations                                  |
| `rt_tween.c`        | Tweening/interpolation for animation                 |
| `rt_tween.h`        | Tween declarations                                   |
| `rt_statemachine.c` | State machine for game objects                       |
| `rt_statemachine.h` | State machine declarations                           |
| `rt_pathfollow.c`   | Path following for movement                          |
| `rt_pathfollow.h`   | Path following declarations                          |
| `rt_quadtree.c`     | Quadtree spatial partitioning                        |
| `rt_quadtree.h`     | Quadtree declarations                                |
| `rt_grid2d.c`       | 2D grid data structure                               |
| `rt_grid2d.h`       | Grid2D declarations                                  |
| `rt_perlin.c`       | Perlin noise generation                              |
| `rt_perlin.h`       | Perlin noise declarations                            |
| `rt_spriteanim.c`   | Sprite animation support                             |
| `rt_spriteanim.h`   | Sprite animation declarations                        |
| `rt_spritebatch.c`  | Sprite batch rendering                               |
| `rt_spritebatch.h`  | Sprite batch declarations                            |
| `rt_screenfx.c`     | Screen effects (transitions, shakes)                 |
| `rt_screenfx.h`     | Screen effects declarations                          |
| `rt_smoothvalue.c`  | Smooth value interpolation                           |
| `rt_smoothvalue.h`  | Smooth value declarations                            |
| `rt_playlist.c`     | Audio playlist management                            |
| `rt_playlist.h`     | Playlist declarations                                |

## GUI (Extended)

| File                       | Purpose                          |
|----------------------------|----------------------------------|
| `rt_gui.h`                 | GUI declarations (Viper.GUI.*)   |
| `rt_gui_internal.h`        | Internal GUI declarations        |
| `rt_gui_app.c`             | GUI application management       |
| `rt_gui_widgets.c`         | Core widget implementations      |
| `rt_gui_widgets_complex.c` | Complex widget implementations   |
| `rt_gui_menus.c`           | Menu system implementation       |
| `rt_gui_system.c`          | System integration               |
| `rt_gui_features.c`        | Additional GUI features          |
| `rt_gui_codeeditor.c`      | Code editor widget               |
| `rt_buttongroup.c`         | Button group widget              |
| `rt_buttongroup.h`         | Button group declarations        |

## Additional Collections

| File                 | Purpose                              |
|----------------------|--------------------------------------|
| `rt_set.c`           | Hash set implementation              |
| `rt_set.h`           | Set declarations                     |
| `rt_sortedset.c`     | Sorted set implementation            |
| `rt_sortedset.h`     | Sorted set declarations              |
| `rt_deque.c`         | Double-ended queue implementation    |
| `rt_deque.h`         | Deque declarations                   |
| `rt_bimap.c`         | Bidirectional map implementation     |
| `rt_bimap.h`         | Bimap declarations                   |
| `rt_orderedmap.c`    | Insertion-ordered map implementation |
| `rt_orderedmap.h`    | Ordered map declarations             |
| `rt_defaultmap.c`    | Map with default values              |
| `rt_defaultmap.h`    | Default map declarations             |
| `rt_frozenmap.c`     | Immutable map implementation         |
| `rt_frozenmap.h`     | Frozen map declarations              |
| `rt_frozenset.c`     | Immutable set implementation         |
| `rt_frozenset.h`     | Frozen set declarations              |
| `rt_multimap.c`      | Multi-value map implementation       |
| `rt_multimap.h`      | Multimap declarations                |
| `rt_countmap.c`      | Counting map implementation          |
| `rt_countmap.h`      | Count map declarations               |
| `rt_weakmap.c`       | Weak-reference map implementation    |
| `rt_weakmap.h`       | Weak map declarations                |
| `rt_lrucache.c`      | LRU cache implementation             |
| `rt_lrucache.h`      | LRU cache declarations               |
| `rt_sparsearray.c`   | Sparse array implementation          |
| `rt_sparsearray.h`   | Sparse array declarations            |
| `rt_trie.c`          | Trie data structure                  |
| `rt_trie.h`          | Trie declarations                    |
| `rt_unionfind.c`     | Union-find (disjoint set)            |
| `rt_unionfind.h`     | Union-find declarations              |
| `rt_bloomfilter.c`   | Bloom filter implementation          |
| `rt_bloomfilter.h`   | Bloom filter declarations            |
| `rt_bitset.c`        | Bitset implementation                |
| `rt_bitset.h`        | Bitset declarations                  |
| `rt_bigint.c`        | Big integer arithmetic               |
| `rt_bigint.h`        | Big integer declarations             |
| `rt_lazyseq.c`       | Lazy sequence evaluation             |
| `rt_lazyseq.h`       | Lazy sequence declarations           |
| `rt_convert_coll.c`  | Collection conversion utilities      |
| `rt_convert_coll.h`  | Collection conversion declarations   |

## Additional Data Formats

| File              | Purpose                          |
|-------------------|----------------------------------|
| `rt_json.c`       | JSON parsing and generation      |
| `rt_json.h`       | JSON declarations                |
| `rt_jsonpath.c`   | JSONPath query implementation    |
| `rt_jsonpath.h`   | JSONPath declarations            |
| `rt_xml.c`        | XML parsing and generation       |
| `rt_xml.h`        | XML declarations                 |
| `rt_yaml.c`       | YAML parsing and generation      |
| `rt_yaml.h`       | YAML declarations                |
| `rt_toml.c`       | TOML parsing and generation      |
| `rt_toml.h`       | TOML declarations                |
| `rt_ini.c`        | INI file parsing                 |
| `rt_ini.h`        | INI declarations                 |
| `rt_html.c`       | HTML generation utilities        |
| `rt_html.h`       | HTML declarations                |
| `rt_markdown.c`   | Markdown processing              |
| `rt_markdown.h`   | Markdown declarations            |

## Additional Math

| File            | Purpose                          |
|-----------------|----------------------------------|
| `rt_mat3.c`     | 3x3 matrix operations            |
| `rt_mat3.h`     | Mat3 declarations                |
| `rt_mat4.c`     | 4x4 matrix operations            |
| `rt_mat4.h`     | Mat4 declarations                |
| `rt_numfmt.c`   | Number formatting utilities      |
| `rt_numfmt.h`   | Number format declarations       |

## Additional Time & Scheduling

| File               | Purpose                          |
|--------------------|----------------------------------|
| `rt_timer.c`       | Timer event implementation       |
| `rt_timer.h`       | Timer declarations               |
| `rt_dateonly.c`    | Date-only (no time) operations   |
| `rt_dateonly.h`    | DateOnly declarations            |
| `rt_daterange.c`   | Date range operations            |
| `rt_daterange.h`   | Date range declarations          |
| `rt_duration.c`    | Duration/time span operations    |
| `rt_duration.h`    | Duration declarations            |
| `rt_reltime.c`     | Relative time formatting         |
| `rt_reltime.h`     | Relative time declarations       |

## Additional Concurrency

| File                | Purpose                              |
|---------------------|--------------------------------------|
| `rt_channel.c`      | Channel-based communication          |
| `rt_channel.h`      | Channel declarations                 |
| `rt_concqueue.c`    | Concurrent queue implementation      |
| `rt_concqueue.h`    | Concurrent queue declarations        |
| `rt_threadpool.c`   | Thread pool implementation           |
| `rt_threadpool.h`   | Thread pool declarations             |
| `rt_future.c`       | Future/promise implementation        |
| `rt_future.h`       | Future declarations                  |
| `rt_cancellation.c` | Cancellation token support           |
| `rt_cancellation.h` | Cancellation declarations            |
| `rt_parallel.c`     | Parallel execution utilities         |
| `rt_parallel.h`     | Parallel declarations                |
| `rt_scheduler.c`    | Task scheduler implementation        |
| `rt_scheduler.h`    | Scheduler declarations               |
| `rt_ratelimit.c`    | Rate limiting implementation         |
| `rt_ratelimit.h`    | Rate limit declarations              |
| `rt_debounce.c`     | Debounce logic implementation        |
| `rt_debounce.h`     | Debounce declarations                |

## Additional Networking

| File                | Purpose                          |
|---------------------|----------------------------------|
| `rt_websocket.c`    | WebSocket client implementation  |
| `rt_websocket.h`    | WebSocket declarations           |
| `rt_restclient.c`   | REST client implementation       |
| `rt_restclient.h`   | REST client declarations         |
| `rt_tls.c`          | TLS/SSL support                  |
| `rt_tls.h`          | TLS declarations                 |

## Additional Cryptography

| File             | Purpose                          |
|------------------|----------------------------------|
| `rt_aes.c`       | AES encryption implementation    |
| `rt_aes.h`       | AES declarations                 |
| `rt_crypto.c`    | General cryptography utilities   |
| `rt_crypto.h`    | Crypto declarations              |
| `rt_crc32.c`     | CRC32 implementation             |
| `rt_crc32.h`     | CRC32 declarations               |
| `rt_password.c`  | Password hashing                 |
| `rt_password.h`  | Password declarations            |

## Additional Text & String

| File                    | Purpose                              |
|-------------------------|--------------------------------------|
| `rt_scanner.c`          | Text scanner/tokenizer               |
| `rt_scanner.h`          | Scanner declarations                 |
| `rt_compiled_pattern.c` | Compiled regex patterns              |
| `rt_compiled_pattern.h` | Compiled pattern declarations        |
| `rt_textwrap.c`         | Text wrapping utilities              |
| `rt_textwrap.h`         | Text wrap declarations               |
| `rt_pluralize.c`        | Pluralization rules                  |
| `rt_pluralize.h`        | Pluralize declarations               |
| `rt_diff.c`             | Text diff implementation             |
| `rt_diff.h`             | Diff declarations                    |
| `rt_glob.c`             | Glob pattern matching                |
| `rt_glob.h`             | Glob declarations                    |

## Additional Utilities

| File               | Purpose                              |
|--------------------|--------------------------------------|
| `rt_option.c`      | Optional value type                  |
| `rt_option.h`      | Option declarations                  |
| `rt_result.c`      | Result type (Ok/Err)                 |
| `rt_result.h`      | Result declarations                  |
| `rt_lazy.c`        | Lazy evaluation support              |
| `rt_lazy.h`        | Lazy declarations                    |
| `rt_action.c`      | Action/callback management           |
| `rt_action.h`      | Action declarations                  |
| `rt_msgbus.c`      | Message bus / pub-sub                |
| `rt_msgbus.h`      | Message bus declarations             |
| `rt_version.c`     | Runtime version information          |
| `rt_version.h`     | Version declarations                 |
| `rt_objpool.c`     | Object pool allocator                |
| `rt_objpool.h`     | Object pool declarations             |
| `rt_stack_safety.c`| Stack overflow detection             |
| `rt_stack_safety.h`| Stack safety declarations            |
| `rt_tempfile.c`    | Temporary file management            |
| `rt_tempfile.h`    | Temp file declarations               |
| `rt_hash_util.h`   | Hash utility declarations            |
| `rt_regex_internal.h` | Internal regex declarations       |
| `rt_inputmgr.c`    | Input manager implementation         |
| `rt_inputmgr.h`    | Input manager declarations           |

## C++ Interface

| File     | Purpose                                 |
|----------|-----------------------------------------|
| `rt.hpp` | C++ shim with `extern "C"` declarations |
