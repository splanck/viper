# Viper Platform OS Requirements for ViperDOS

**Document Purpose:** Comprehensive analysis of OS capabilities required to host the Viper platform on ViperDOS.

**Analysis Date:** 2026-01-26
**Total Source Files Analyzed:** 1,428 C/C++ files
**Analysis Status:** IN PROGRESS - Runtime files partially complete

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Component Overview](#component-overview)
3. [Runtime Requirements (rt_*.c)](#runtime-requirements)
4. [VM Requirements](#vm-requirements)
5. [Codegen/Native Binary Requirements](#codegen-requirements)
6. [Frontend/Compiler Requirements](#frontend-requirements)
7. [Tool Requirements](#tool-requirements)
8. [TUI Library Requirements](#tui-requirements)
9. [Graphics Library Requirements](#graphics-requirements)
10. [Audio Library Requirements](#audio-requirements)
11. [GUI Library Requirements](#gui-requirements)
12. [Summary: OS Capabilities Needed](#os-capabilities-summary)
13. [Runtime Porting Checklist](#runtime-porting-checklist)
14. [Implementation Priority](#implementation-priority)

---

## Executive Summary

**Preliminary findings (analysis ongoing):**

The Viper runtime can be categorized into three tiers based on OS dependency:

1. **Tier 1 - Pure C (No OS dependencies):** Many runtime files use only standard C library functions (malloc, free,
   memcpy, strlen, etc.) and can be ported with minimal effort.

2. **Tier 2 - Platform Abstraction Required:** Files requiring thread-local storage, atomics, file I/O, timing, and
   networking need ViperDOS implementations.

3. **Tier 3 - Complex OS Integration:** Files requiring process execution, file watching, TLS/SSL, and compression
   libraries need significant work or may be deferred.

---

## Component Overview

### Directory Structure

```
src/
├── runtime/          # C runtime library (rt_*.c) - CRITICAL for porting
├── vm/               # Virtual machine implementation
├── codegen/          # Native code generation (x86-64)
├── il/               # Intermediate language core
├── frontends/        # Language frontends (BASIC, Pascal, Zia)
├── tools/            # CLI tools (vbasic, vpascal, ilrun, etc.)
├── tui/              # Terminal UI library
├── lib/
│   ├── graphics/     # Graphics library (vgfx)
│   ├── audio/        # Audio library (vaud)
│   └── gui/          # GUI widget library
├── tests/            # Test suites
└── pass/             # Optimization passes
```

---

## Runtime Requirements

### File-by-File Analysis

#### Legend

- **OS Deps:** None | Minimal | Moderate | Heavy
- **Priority:** P0 (Critical) | P1 (High) | P2 (Medium) | P3 (Low)

---

### Category: Platform Abstraction Layer

#### rt_platform.h

- **Purpose:** Platform detection macros and compiler abstractions
- **OS Dependencies:** None (compile-time detection only)
- **Key Definitions:**
    - `RT_PLATFORM_WINDOWS`, `RT_PLATFORM_LINUX`, `RT_PLATFORM_MACOS`, `RT_PLATFORM_VIPERDOS`
    - `RT_THREAD_LOCAL` - Thread-local storage qualifier
    - `RT_EXPORT`, `RT_IMPORT` - Symbol visibility
- **ViperDOS Action:** Add `RT_PLATFORM_VIPERDOS` detection, implement `RT_THREAD_LOCAL`
- **Priority:** P0

#### rt_internal.h

- **Purpose:** Internal runtime structures and utilities
- **OS Dependencies:** None (header only)
- **Key Structures:**
    - `rt_string_impl` - String implementation with SSO
    - `rt_input_grow_result` - Buffer growth result codes
    - Array implementation macros (`RT_ARR_DEFINE_*`)
- **ViperDOS Action:** None required
- **Priority:** P0

---

### Category: Memory Management

#### rt_heap.c

- **Purpose:** Reference-counted heap allocation with pool allocator
- **OS Dependencies:** Minimal - uses `malloc`, `realloc`, `free`
- **Key Features:**
    - Pool allocator for small objects
    - Reference counting with atomic operations
    - Header-based metadata (`rt_heap_hdr_t`)
- **libc Functions:** `malloc`, `realloc`, `free`, `memset`
- **Atomics:** `__atomic_fetch_add`, `__atomic_fetch_sub`, `__atomic_load_n`
- **ViperDOS Action:** Implement atomic operations, standard allocator available
- **Priority:** P0

#### rt_memory.c

- **Purpose:** Memory allocation wrappers
- **OS Dependencies:** Minimal
- **libc Functions:** `malloc`, `realloc`, `free`, `calloc`
- **ViperDOS Action:** Standard C library sufficient
- **Priority:** P0

#### rt_object.c

- **Purpose:** Object allocation and lifetime management
- **OS Dependencies:** Minimal - atomic reference counting
- **Key Features:**
    - Finalizer support for cleanup callbacks
    - Atomic reference counting
- **Atomics:** `__atomic_fetch_add`, `__atomic_fetch_sub`
- **ViperDOS Action:** Ensure atomic operations available
- **Priority:** P0

---

### Category: String and Text Processing

#### rt_string.c / rt_string_ops.c

- **Purpose:** Reference-counted string implementation
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - Small String Optimization (SSO) for strings <= 32 bytes
    - Copy-on-write semantics
    - Atomic reference counting
- **libc Functions:** `malloc`, `free`, `memcpy`, `memcmp`, `strlen`, `snprintf`
- **ViperDOS Action:** None required
- **Priority:** P0

#### rt_format.c

- **Purpose:** Numeric and CSV formatting
- **OS Dependencies:** Minimal
- **libc Functions:** `snprintf`, `malloc`, `free`, `strlen`, `memcpy`
- **Special:** `localeconv()` for locale-aware formatting
- **ViperDOS Action:** Implement `localeconv()` or provide stub
- **Priority:** P1

#### rt_template.c

- **Purpose:** String templating engine
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `free`, `strlen`, `memcmp`
- **ctype Functions:** `isspace`, `isdigit`
- **ViperDOS Action:** None required
- **Priority:** P2

#### rt_regex.c

- **Purpose:** Regular expression pattern matching
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - Backtracking pattern matcher
    - Supports: literals, dot, anchors, character classes, quantifiers, groups, alternation
    - Pattern cache (LRU with 16 entries)
- **libc Functions:** `malloc`, `realloc`, `free`, `memcpy`, `memset`
- **ViperDOS Action:** None required
- **Priority:** P2

#### rt_json.c

- **Purpose:** JSON parsing and formatting (ECMA-404/RFC 8259)
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `free`, `strlen`, `memcpy`, `snprintf`
- **math.h:** `isnan`, `isinf`
- **ViperDOS Action:** Ensure math.h functions available
- **Priority:** P1

#### rt_csv.c

- **Purpose:** CSV parsing and formatting (RFC 4180)
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `realloc`, `free`, `strlen`, `memcpy`
- **ViperDOS Action:** None required
- **Priority:** P2

---

### Category: Collections

#### rt_array.c

- **Purpose:** Dynamic array helpers for int32_t arrays
- **OS Dependencies:** Minimal - atomic reference counting
- **Key Features:**
    - Bounds checking with panic on violation
    - Copy-on-write semantics
    - Integration with heap allocator
- **libc Functions:** `malloc`, `realloc`, `free`, `memcpy`, `memset`
- **Atomics:** `__atomic_load_n`
- **ViperDOS Action:** Ensure atomic operations available
- **Priority:** P0

#### rt_seq.c

- **Purpose:** Dynamic sequence (growable array)
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - Growth factor 2x, default capacity 16
    - Merge sort for sorting
- **libc Functions:** `malloc`, `realloc`, `free`, `memcpy`, `memmove`
- **ViperDOS Action:** None required
- **Priority:** P0

#### rt_map.c

- **Purpose:** String-keyed hash map
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - FNV-1a hash with separate chaining
    - Load factor 75% triggers resize
- **libc Functions:** `malloc`, `calloc`, `free`, `strlen`, `memcpy`, `memcmp`
- **ViperDOS Action:** None required
- **Priority:** P0

#### rt_list.c

- **Purpose:** Object-backed list
- **OS Dependencies:** None (pure C)
- **Key Features:** Delegates to rt_arr_obj for storage
- **ViperDOS Action:** None required
- **Priority:** P1

#### rt_set.c

- **Purpose:** Generic hash set
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - Identity hashing (pointer-based)
    - Knuth's multiplicative hash
- **libc Functions:** `malloc`, `calloc`, `free`
- **ViperDOS Action:** None required
- **Priority:** P1

#### rt_bag.c

- **Purpose:** String set using FNV-1a hash
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - FNV-1a hash with separate chaining
    - Set operations: union, intersection, difference
- **libc Functions:** `malloc`, `calloc`, `free`, `strlen`, `memcpy`, `memcmp`
- **ViperDOS Action:** None required
- **Priority:** P1

---

### Category: Binary Data

#### rt_bytes.c

- **Purpose:** Byte array storage for binary data
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - Inline data storage for cache locality
    - Base64/Hex encoding/decoding
- **libc Functions:** `malloc`, `free`, `memcpy`, `memmove`, `memset`, `strlen`
- **ViperDOS Action:** None required
- **Priority:** P0

#### rt_box.c

- **Purpose:** Boxing/unboxing primitives for generic collections
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - Type-tagged union for i64, f64, bool, string
    - Participates in reference counting
- **ViperDOS Action:** None required
- **Priority:** P1

---

### Category: Encoding and Hashing

#### rt_codec.c

- **Purpose:** Base64, Hex, URL encoding/decoding
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `free`, `strlen`
- **ctype Functions:** `isalnum`
- **ViperDOS Action:** None required
- **Priority:** P1

#### rt_hash.c

- **Purpose:** Cryptographic hash functions
- **OS Dependencies:** None (pure C - self-contained implementations)
- **Key Algorithms:**
    - MD5 (RFC 1321) - 128-bit, broken but useful for checksums
    - SHA-1 (RFC 3174) - 160-bit, broken
    - SHA-256 (RFC 6234) - 256-bit, secure
    - CRC32 (IEEE 802.3) - 32-bit checksum
    - HMAC variants for all hash functions
- **libc Functions:** `malloc`, `free`, `strlen`, `memcpy`, `memset`
- **ViperDOS Action:** None required
- **Priority:** P1

---

### Category: Math and Numeric

#### rt_math.c

- **Purpose:** Math function wrappers
- **OS Dependencies:** Requires math library (libm)
- **math.h Functions:**
    - `sqrt`, `floor`, `ceil`, `round`, `trunc`
    - `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`
    - `exp`, `log`, `log10`, `pow`
    - `fabs`, `fmod`
    - `isnan`, `isinf`
- **ViperDOS Action:** Implement or link math library
- **Priority:** P0

#### rt_bigint.c

- **Purpose:** Arbitrary precision integers
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `realloc`, `calloc`, `free`, `memcpy`, `memset`
- **ViperDOS Action:** None required
- **Priority:** P2

#### rt_bits.c

- **Purpose:** Bit manipulation utilities
- **OS Dependencies:** None (uses compiler intrinsics)
- **Compiler Intrinsics (GCC/Clang):**
    - `__builtin_popcountll` - population count
    - `__builtin_clzll` - count leading zeros
    - `__builtin_ctzll` - count trailing zeros
    - `__builtin_bswap64` - byte swap
- **MSVC Intrinsics:** `__popcnt64`, `_BitScanReverse64`, `_byteswap_uint64`
- **ViperDOS Action:** Ensure compiler intrinsics available or provide fallbacks
- **Priority:** P2

---

### Category: Error Handling

#### rt_trap.c

- **Purpose:** Fatal trap helpers
- **OS Dependencies:** Minimal
- **libc Functions:** `fprintf`, `fflush`, `exit`, `strcmp`, `strlen`
- **math.h:** `fabs`, `fmax`, `isnan`
- **ViperDOS Action:** Implement stderr output and exit()
- **Priority:** P0

#### rt_error.c

- **Purpose:** Error sentinel definition
- **OS Dependencies:** None
- **ViperDOS Action:** None required
- **Priority:** P0

---

### Category: Input Handling

#### rt_input.c

- **Purpose:** Keyboard/mouse input handling
- **OS Dependencies:** None (pure C with statics for state)
- **Key Features:**
    - Static state arrays for key/button tracking
    - GLFW-style key codes to vgfx mapping
- **Note:** Requires platform input event source
- **ViperDOS Action:** Implement input event delivery mechanism
- **Priority:** P1

---

### Category: Threading and Concurrency (MODERATE OS DEPS)

#### rt_context.c

- **Purpose:** Per-VM runtime context management
- **OS Dependencies:** MODERATE
- **Key Features:**
    - Thread-local storage (`RT_THREAD_LOCAL`) for g_rt_context
    - Spinlocks with atomics (`__atomic_test_and_set`, `__atomic_clear`)
    - Lazy initialization with CAS (`__atomic_compare_exchange_n`)
    - Per-VM isolation (RNG, files, args, types, module vars)
    - State handoff between VM and legacy context
- **Required:**
    - `RT_THREAD_LOCAL` - thread-local storage
    - `__atomic_test_and_set`, `__atomic_clear` - spinlock primitives
    - `__atomic_compare_exchange_n`, `__atomic_load_n` - CAS and atomic load
- **ViperDOS Action:** Implement TLS and atomic operations
- **Priority:** P0

#### rt_stack_safety.c

- **Purpose:** Stack overflow detection and graceful error handling
- **OS Dependencies:** HEAVY
- **Platform APIs:**
    - Windows: `AddVectoredExceptionHandler`, `EXCEPTION_STACK_OVERFLOW`
    - Unix: `sigaltstack`, `sigaction`, `SIGSEGV`, `SIGBUS`
- **Key Features:**
    - Alternate stack for signal handling
    - Graceful error messages before termination
- **Fallback:** Has `#else` fallback (no-op init, basic trap)
- **ViperDOS Action:** Use fallback or implement signal handling
- **Priority:** P2 (has fallback)

#### rt_threads.c

- **Purpose:** Thread creation and synchronization
- **OS Dependencies:** HEAVY
- **POSIX APIs:**
    - `pthread_create`, `pthread_join`, `pthread_detach`
    - `pthread_mutex_init/lock/unlock/destroy`
    - `pthread_cond_init/wait/signal/broadcast/destroy`
    - `pthread_rwlock_*` - read-write locks
- **Windows APIs:** `CreateThread`, `WaitForSingleObject`, `CRITICAL_SECTION`, etc.
- **ViperDOS Action:** Implement threading primitives
- **Priority:** P1

#### rt_threadpool.c

- **Purpose:** Thread pool for parallel task execution
- **OS Dependencies:** HEAVY (depends on rt_threads.c)
- **ViperDOS Action:** Implement after rt_threads.c
- **Priority:** P2

#### rt_channel.c

- **Purpose:** Go-style channels for inter-thread communication
- **OS Dependencies:** HEAVY (depends on rt_threads.c)
- **ViperDOS Action:** Implement after rt_threads.c
- **Priority:** P2

---

### Category: File I/O (HEAVY OS DEPS)

#### rt_file.c / rt_file_io.c

- **Purpose:** File operations
- **OS Dependencies:** HEAVY
- **POSIX APIs:**
    - `open`, `close`, `read`, `write`, `lseek`
    - `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`
    - `stat`, `fstat`, `access`
    - `rename`, `remove`, `mkdir`
- **Windows APIs:** `CreateFile`, `ReadFile`, `WriteFile`, etc.
- **ViperDOS Action:** Implement file system interface
- **Priority:** P0

#### rt_dir.c

- **Purpose:** Directory operations
- **OS Dependencies:** HEAVY
- **POSIX APIs:**
    - `opendir`, `readdir`, `closedir`
    - `mkdir`, `rmdir`
    - `getcwd`, `chdir`
- **Windows APIs:** `FindFirstFile`, `FindNextFile`, etc.
- **ViperDOS Action:** Implement directory enumeration
- **Priority:** P1

#### rt_path.c

- **Purpose:** Path manipulation
- **OS Dependencies:** Minimal (mostly string manipulation)
- **Platform-Specific:** Path separator detection (`/` vs `\`)
- **ViperDOS Action:** Define path separator, implement path utilities
- **Priority:** P1

#### rt_watcher.c *** HAS VIPERDOS PLACEHOLDER ***

- **Purpose:** File system change notification (Viper.IO.Watcher)
- **OS Dependencies:** HEAVY
- **Platform APIs:**
    - Linux: `inotify_init`, `inotify_add_watch`, `inotify_rm_watch`, `poll`
    - macOS: `kqueue`, `kevent`
    - Windows: `ReadDirectoryChangesW`, overlapped I/O
- **ViperDOS Status:** Has placeholder at lines 50-52:
  ```c
  #elif defined(__viperdos__)
  // TODO: ViperDOS - file system watching not yet implemented
  // Would need kernel support for file change notifications
  ```
- **ViperDOS Action:** Defer - needs kernel support
- **Priority:** P3 (deferrable)

---

### Category: Time (MODERATE OS DEPS)

#### rt_time.c *** ALREADY HAS VIPERDOS STUBS ***

- **Purpose:** High-resolution timing and sleep
- **OS Dependencies:** MODERATE
- **Platform APIs:**
    - Windows: `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `Sleep`
    - POSIX: `clock_gettime(CLOCK_MONOTONIC)`, `nanosleep`
- **Key Functions:**
    - `rt_sleep_ms()` - sleep in milliseconds
    - `rt_timer_ms()` - monotonic timer in ms
    - `rt_clock_ticks_us()` - microsecond precision
- **ViperDOS Status:** Full stubs exist at lines 169-190
  ```c
  void rt_sleep_ms(int32_t ms) { (void)ms; }
  int64_t rt_timer_ms(void) { return 0; }
  int64_t rt_clock_ticks_us(void) { return 0; }
  ```
- **ViperDOS Action:** Implement clock/sleep syscalls
- **Priority:** P0

#### rt_datetime.c

- **Purpose:** Date and time operations (Viper.DateTime class)
- **OS Dependencies:** MODERATE
- **libc Functions:** `time`, `localtime_r`, `gmtime_r`, `mktime`, `strftime`
- **Platform-Specific:**
    - macOS: `gettimeofday` for millisecond precision
    - Others: `clock_gettime(CLOCK_REALTIME)`
- **Key Features:**
    - Unix timestamp handling
    - Date/time component extraction (year, month, day, etc.)
    - ISO 8601 formatting
    - Time zone handling (local + UTC)
- **ViperDOS Action:** Implement time() and localtime_r()
- **Priority:** P1

#### rt_stopwatch.c

- **Purpose:** Stopwatch for timing code sections
- **OS Dependencies:** MODERATE (depends on rt_time.c)
- **ViperDOS Action:** Implement after rt_time.c
- **Priority:** P2

---

### Category: Networking (HEAVY OS DEPS)

#### rt_network.c

- **Purpose:** BSD sockets networking
- **OS Dependencies:** HEAVY
- **POSIX APIs:**
    - `socket`, `bind`, `listen`, `accept`, `connect`
    - `send`, `recv`, `sendto`, `recvfrom`
    - `close`, `shutdown`
    - `getaddrinfo`, `freeaddrinfo`, `getnameinfo`
    - `setsockopt`, `getsockopt`
    - `select`, `poll`
- **Windows APIs:** Winsock2 equivalents
- **ViperDOS Action:** Implement socket layer
- **Priority:** P2

#### rt_tls.c *** SELF-CONTAINED TLS 1.3 ***

- **Purpose:** TLS 1.3 encrypted connections
- **OS Dependencies:** MODERATE (depends on networking)
- **External Libraries:** NONE - self-contained implementation!
- **Key Features:**
    - TLS 1.3 client implementation
    - ChaCha20-Poly1305 AEAD cipher
    - X25519 key exchange
    - Uses rt_crypto.c for crypto primitives
- **Dependencies:** rt_crypto.h, socket APIs
- **ViperDOS Action:** Implement after networking
- **Priority:** P3

#### rt_network_http.c

- **Purpose:** HTTP client implementation (GET, POST, redirects)
- **OS Dependencies:** HEAVY (depends on rt_network.c, rt_tls.c)
- **Key Features:**
    - HTTP/1.1 client with redirect following
    - HTTPS support via rt_tls.c
    - Header parsing and response handling
- **Dependencies:** rt_network.c, rt_tls.c, rt_bytes.c
- **ViperDOS Action:** Implement after networking and TLS
- **Priority:** P3

#### rt_websocket.c

- **Purpose:** WebSocket client (RFC 6455)
- **OS Dependencies:** HEAVY (depends on rt_network.c, rt_tls.c)
- **Key Features:**
    - Opening/closing handshake
    - Text and binary frame support
    - Ping/pong handling
    - Uses rt_random.c for masking key generation
- **Dependencies:** rt_network.c, rt_tls.c, rt_random.c
- **ViperDOS Action:** Implement after networking and TLS
- **Priority:** P3

---

### Category: Process Execution (HEAVY OS DEPS)

#### rt_exec.c

- **Purpose:** Process creation and execution
- **OS Dependencies:** HEAVY
- **POSIX APIs:**
    - `fork`, `exec*`, `waitpid`
    - `pipe`, `dup2`
    - `kill`, `signal`
- **Windows APIs:** `CreateProcess`, `WaitForSingleObject`, etc.
- **ViperDOS Action:** Implement process model or defer
- **Priority:** P3

---

### Category: Random Number Generation (MODERATE OS DEPS)

#### rt_random.c

- **Purpose:** Random number generation
- **OS Dependencies:** MODERATE
- **Cryptographic Sources:**
    - Linux: `/dev/urandom`
    - Windows: `BCryptGenRandom`
    - macOS: `arc4random_buf`
- **ViperDOS Action:** Implement entropy source
- **Priority:** P1

#### rt_guid.c

- **Purpose:** GUID/UUID generation
- **OS Dependencies:** MODERATE (depends on rt_random.c)
- **ViperDOS Action:** Implement after rt_random.c
- **Priority:** P2

---

### Category: Compression (PURE C - NO EXTERNAL DEPS!)

#### rt_compress.c *** MAJOR FINDING: SELF-CONTAINED ***

- **Purpose:** DEFLATE/GZIP compression and decompression
- **OS Dependencies:** None (pure C)
- **External Libraries:** NONE - fully self-contained implementation!
- **Key Features:**
    - RFC 1951 (DEFLATE) and RFC 1952 (GZIP) compliant
    - LZ77 compression with sliding window
    - Huffman coding (fixed and dynamic)
    - 9 compression levels (1-9)
    - Uses rt_crc32.c for checksums
- **libc Functions:** `malloc`, `realloc`, `free`, `memcpy`, `memset`
- **Internal Dependencies:** rt_bytes.h, rt_crc32.h, rt_object.h, rt_string.h
- **ViperDOS Action:** None required - works as-is!
- **Priority:** P1 (now easier than expected)

#### rt_archive.c

- **Purpose:** ZIP archive reading/writing (PKWARE APPNOTE spec)
- **OS Dependencies:** MODERATE (file I/O for writing)
- **Key Features:**
    - ZIP file format support (stored + deflate methods)
    - Uses self-contained rt_compress.c (no zlib!)
    - CRC32 validation
    - Little-endian byte manipulation
- **Dependencies:** rt_compress.h, rt_crc32.h, rt_dir.h, file I/O
- **libc Functions:** `malloc`, `free`, `memcpy`, `open`, `close`, `read`, `write`
- **ViperDOS Action:** Implement after file I/O
- **Priority:** P2

---

### Category: Cryptography (EXTERNAL DEPS)

#### rt_crypto.c

- **Purpose:** Cryptographic operations
- **OS Dependencies:** May use external libraries
- **Key Features:** Likely AES, RSA, etc.
- **ViperDOS Action:** Implement or use embedded crypto library
- **Priority:** P2

---

### Category: Terminal (MODERATE OS DEPS)

#### rt_term.c

- **Purpose:** Terminal/console operations
- **OS Dependencies:** MODERATE
- **POSIX APIs:**
    - `tcgetattr`, `tcsetattr` - terminal attributes
    - `ioctl` - terminal size
    - ANSI escape sequences
- **Windows APIs:** Console API
- **ViperDOS Action:** Implement terminal interface
- **Priority:** P1

---

### Category: Additional Collections (Pure C)

#### rt_pqueue.c (Priority Queue/Heap)

- **Purpose:** Binary heap priority queue implementation
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `free`, `memcpy`
- **Key Features:**
    - Min-heap and max-heap modes
    - O(log n) push/pop operations
    - Uses rt_obj_new_i64 for GC-managed allocation
    - Priority + value pairs
- **ViperDOS Action:** None required
- **Priority:** P1

#### rt_ring.c (Ring Buffer)

- **Purpose:** Fixed-size circular buffer
- **OS Dependencies:** None (pure C)
- **libc Functions:** `calloc`, `free`
- **Key Features:**
    - Fixed capacity at creation time
    - O(1) push/pop with wrap-around
    - Overwrites oldest element when full
    - GC-managed with finalizer
- **ViperDOS Action:** None required
- **Priority:** P1

#### rt_queue.c (FIFO Queue)

- **Purpose:** Dynamic FIFO queue with circular buffer
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `free`, `memcpy`
- **Key Features:**
    - Circular buffer implementation
    - Auto-growth with linearization
    - O(1) add/take operations
    - Default capacity 16, 2x growth
- **ViperDOS Action:** None required
- **Priority:** P1

#### rt_stack.c (LIFO Stack)

- **Purpose:** Dynamic LIFO stack
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `realloc`, `free`
- **Key Features:**
    - Dynamic array backing store
    - O(1) push/pop/peek
    - Default capacity 16, 2x growth
- **ViperDOS Action:** None required
- **Priority:** P1

---

### Category: Timing and Timers (MODERATE OS DEPS)

#### rt_countdown.c

- **Purpose:** Countdown timer for interval timing with expiration detection
- **OS Dependencies:** MODERATE
- **Platform APIs:**
    - Windows: `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `Sleep`
    - POSIX: `clock_gettime(CLOCK_MONOTONIC)`, `nanosleep`
- **Key Features:**
    - Start/stop/reset operations
    - Elapsed/remaining time queries
    - Expiration detection
    - Blocking wait support
- **ViperDOS Action:** Implement high-res timer + sleep
- **Priority:** P1

#### rt_stopwatch.c

- **Purpose:** High-precision stopwatch for benchmarking
- **OS Dependencies:** MODERATE
- **Platform APIs:**
    - Windows: `QueryPerformanceCounter`, `QueryPerformanceFrequency`
    - POSIX: `clock_gettime(CLOCK_MONOTONIC)`
- **Key Features:**
    - Nanosecond resolution
    - Monotonic clock (immune to system time changes)
    - Start/stop/reset/restart operations
    - Elapsed time in ns/us/ms
- **ViperDOS Action:** Implement high-res timer
- **Priority:** P1

---

### Category: Exception Handling (Pure C)

#### rt_exc.c

- **Purpose:** Runtime exception support
- **OS Dependencies:** None (pure C)
- **libc Functions:** None (uses rt_object, rt_string)
- **Key Features:**
    - Exception object creation with message
    - Message retrieval
    - Finalizer releases message string
- **ViperDOS Action:** None required
- **Priority:** P0

---

### Category: Debug and Logging (Minimal OS Deps)

#### rt_debug.c

- **Purpose:** Debug trace printing for test harnesses
- **OS Dependencies:** Minimal
- **libc Functions:** `printf`, `fflush`
- **Key Features:**
    - Print i32 and string with newline
    - Immediate flush for deterministic output
- **ViperDOS Action:** Implement stdout + fflush
- **Priority:** P0

#### rt_log.c

- **Purpose:** Structured logging with severity levels
- **OS Dependencies:** Minimal
- **libc Functions:** `fprintf`, `fflush`, `time`, `strftime`
- **Platform Functions:** `rt_localtime_r` (thread-safe localtime)
- **Key Features:**
    - DEBUG/INFO/WARN/ERROR levels
    - Timestamped output to stderr
    - Configurable log level filtering
- **ViperDOS Action:** Implement time functions + stderr
- **Priority:** P1

#### rt_output.c

- **Purpose:** Centralized output buffering for terminal rendering
- **OS Dependencies:** Minimal
- **libc Functions:** `setvbuf`, `fputs`, `fwrite`, `fflush`
- **Key Features:**
    - 16KB stdout buffer for reduced syscalls
    - Batch mode for grouped operations
    - Critical for TUI performance
- **ViperDOS Action:** Implement buffered I/O
- **Priority:** P1

---

### Category: System Information (HEAVY OS DEPS)

#### rt_machine.c *** ALREADY HAS VIPERDOS STUBS ***

- **Purpose:** System information queries (Viper.Machine)
- **OS Dependencies:** HEAVY
- **Platform APIs:**
    - Windows: `GetVersionExA`, `GetComputerNameA`, `GetUserNameA`, `GetTempPathA`, `GetSystemInfo`,
      `GlobalMemoryStatusEx`
    - POSIX: `uname`, `gethostname`, `getpwuid`, `getuid`, `sysconf`
    - macOS: `sysctlbyname`, `mach_host_self`, `vm_statistics64`
    - Linux: `sysinfo`, `/etc/os-release`
- **Key Features:**
    - OS name and version
    - Hostname and username
    - Home and temp directories
    - CPU core count
    - Total and free memory
    - Endianness detection
- **ViperDOS Status:** Already has `#if defined(__viperdos__)` stubs:
    - `rt_machine_os()` → returns "viperdos"
    - `rt_machine_os_ver()` → returns "0.2.7"
    - Other functions need TODO implementations
- **Priority:** P1

---

### Category: Floating-Point Helpers (Minimal)

#### rt_fp.c

- **Purpose:** Floating-point domain helpers for BASIC runtime
- **OS Dependencies:** Minimal (math.h only)
- **math.h Functions:** `pow`, `isfinite`, `trunc`, `NAN`
- **Key Features:**
    - Domain checking for exponentiation
    - Rejects negative bases with fractional exponents
    - Error reporting via output parameter
- **ViperDOS Action:** Requires math library
- **Priority:** P0

---

### Category: Module Variables (Pure C)

#### rt_modvar.c

- **Purpose:** Runtime-managed addresses for module-level BASIC variables
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `realloc`, `free`, `memset`, `memcpy`, `strcmp`, `strlen`
- **Key Features:**
    - Per-VM context isolation via RtContext
    - Linear table keyed by name+kind
    - Supports i64, f64, i1, ptr, str, and block types
    - Zero-initialized storage
- **ViperDOS Action:** None required
- **Priority:** P0

---

### Category: OOP Support (Pure C)

#### rt_oop_dispatch.c

- **Purpose:** Virtual method dispatch for polymorphism
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - VTable-based virtual function lookup
    - Slot bounds checking
    - NULL safety (returns NULL on errors)
    - O(1) dispatch time
- **ViperDOS Action:** None required
- **Priority:** P0

#### rt_type_registry.c

- **Purpose:** Runtime type system for OOP
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `realloc`, `free`, `strcmp`, `strlen`
- **Key Features:**
    - Class and interface metadata registration
    - VTable and ITable management
    - Type hierarchy queries
- **ViperDOS Action:** None required
- **Priority:** P0

---

### Category: Thread-Safe Primitives (MODERATE OS DEPS)

#### rt_safe_i64.c

- **Purpose:** Thread-safe 64-bit integer cell
- **OS Dependencies:** MODERATE (depends on rt_monitor)
- **Key Features:**
    - Thread-safe get/set
    - Atomic add
    - Compare-and-exchange (CAS)
    - Uses monitor for synchronization
- **Platform Support:**
    - Windows: Traps (stubs only)
    - POSIX: Full support via rt_monitor
- **ViperDOS Action:** Implement after rt_monitor
- **Priority:** P2

#### rt_monitor.c *** DEPENDS ON THREADING ***

- **Purpose:** FIFO-fair reentrant monitor implementation
- **OS Dependencies:** HEAVY (depends on rt_threads)
- **Platform APIs:**
    - Windows: `CRITICAL_SECTION`, `CONDITION_VARIABLE`
    - POSIX: `pthread_mutex_*`, `pthread_cond_*`
- **Key Features:**
    - Java-style monitors with Enter/Exit/Wait/Signal
    - FIFO fairness via waiter queue
    - Hash table mapping object addresses to monitors
    - Timeout support (TryEnterFor, WaitFor)
- **ViperDOS Action:** Implement after threading
- **Priority:** P2

---

### Category: Bridge and Namespace Helpers (Pure C)

#### rt_ns_bridge.c

- **Purpose:** Bridges Viper.* namespaced types to runtime objects
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - StringBuilder allocation and initialization
    - GC-managed objects with finalizers
- **ViperDOS Action:** None required
- **Priority:** P1

---

### Category: Checksums (Pure C)

#### rt_crc32.c

- **Purpose:** CRC32 checksum (IEEE 802.3 polynomial)
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - Standard IEEE polynomial (0xEDB88320)
    - Lazy lookup table initialization
    - Used by rt_hash, rt_compress, rt_archive
- **ViperDOS Action:** None required
- **Priority:** P1

---

### Category: Secure Random (MODERATE OS DEPS)

#### rt_rand.c *** ALREADY HAS VIPERDOS TODO ***

- **Purpose:** Cryptographically secure random number generation
- **OS Dependencies:** MODERATE
- **Platform APIs:**
    - Windows: `BCryptGenRandom`
    - Linux/macOS: `/dev/urandom` via `open`/`read`/`close`
- **Key Features:**
    - Secure random bytes
    - Uniform random integers with rejection sampling
- **ViperDOS Status:** Already has TODO stub:
  ```c
  #elif defined(__viperdos__)
  // TODO: ViperDOS - implement using VirtIO-RNG or getrandom syscall
  return -1;
  ```
- **Priority:** P1

---

### Category: Cryptography (Pure C)

#### rt_aes.c

- **Purpose:** AES-128/256 encryption (FIPS-197)
- **OS Dependencies:** None (self-contained pure C)
- **Key Features:**
    - CBC mode with PKCS7 padding
    - Integrated SHA-256 for key derivation
    - S-box and round constant tables
- **ViperDOS Action:** None required
- **Priority:** P2

#### rt_keyderive.c

- **Purpose:** PBKDF2-SHA256 key derivation (RFC 2898/8018)
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - Configurable iteration count
    - Variable output length
    - HMAC-SHA256 based
- **ViperDOS Action:** None required
- **Priority:** P2

#### rt_crypto.c *** MAJOR FINDING: SELF-CONTAINED ***

- **Purpose:** Cryptographic primitives for TLS and general crypto
- **OS Dependencies:** None (pure C - completely self-contained!)
- **External Libraries:** NONE
- **Key Algorithms:**
    - SHA-256 (FIPS 180-4)
    - HMAC-SHA256 (RFC 2104)
    - HKDF (RFC 5869)
    - ChaCha20-Poly1305 AEAD (RFC 8439)
    - X25519 key exchange (RFC 7748)
- **libc Functions:** `memcpy`, `memset` only
- **ViperDOS Action:** None required - works as-is!
- **Priority:** P1 (enables TLS)

#### rt_random.c (Deterministic PRNG)

- **Purpose:** Deterministic pseudo-random number generator
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - 64-bit Linear Congruential Generator (LCG)
    - BASIC-compatible `RND` semantics
    - Per-VM context isolation
    - Range and distribution functions
- **libc Functions:** None (uses math.h for double conversion)
- **Note:** Different from rt_rand.c (secure random)
- **ViperDOS Action:** None required
- **Priority:** P0

#### rt_parse.c

- **Purpose:** Safe string-to-value parsing (Viper.Parse)
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - Integer, float, bool, hex parsing
    - Graceful error handling (no traps)
    - Whitespace tolerance
- **libc Functions:** `strtoll`, `strtod`, `tolower`, `isspace`
- **Platform-Specific:** Uses `_l` locale variants on macOS/BSD
- **ViperDOS Action:** Implement locale functions or use C locale
- **Priority:** P1

#### rt_args.c

- **Purpose:** Command-line arguments and environment variables
- **OS Dependencies:** Minimal
- **Key Features:**
    - Argument access by index
    - Environment variable get/set
    - Command line reconstruction
- **libc Functions:** `getenv`, `setenv`, `malloc`, `free`
- **Windows-Specific:** Uses Windows API for unicode args
- **ViperDOS Action:** Implement getenv/setenv or pass from kernel
- **Priority:** P1

---

### Category: Sorted Collections (Pure C)

#### rt_treemap.c

- **Purpose:** Sorted key-value map using binary search
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `realloc`, `free`, `memmove`, `strcmp`
- **Key Features:**
    - Sorted string keys
    - Binary search for O(log n) lookup
    - Array-based storage
- **ViperDOS Action:** None required
- **Priority:** P1

---

### Category: Array Types (Pure C)

#### rt_array_i64.c, rt_array_f64.c, rt_array_obj.c, rt_array_str.c

- **Purpose:** Type-specific dynamic array implementations
- **OS Dependencies:** Minimal (atomic reference counting)
- **Key Features:**
    - Uses macros from rt_internal.h for code generation
    - Bounds checking with panic
    - Copy-on-write semantics
    - Resize with grow-in-place optimization
- **Atomics:** `__atomic_load_n`
- **ViperDOS Action:** Ensure atomics available
- **Priority:** P0

---

### Category: File Streams (Minimal OS Deps)

#### rt_linereader.c

- **Purpose:** Line-by-line file reading
- **OS Dependencies:** Minimal (FILE* APIs)
- **libc Functions:** `fopen`, `fclose`, `fgetc`, `fread`, `fseek`, `ftell`, `malloc`, `realloc`, `free`
- **Key Features:**
    - Handles CR, LF, and CRLF line endings
    - Buffered reading
    - Line number tracking
- **ViperDOS Action:** Implement FILE* APIs
- **Priority:** P1

#### rt_linewriter.c

- **Purpose:** Buffered text file writing
- **OS Dependencies:** Minimal (FILE* APIs)
- **libc Functions:** `fopen`, `fclose`, `fwrite`, `fputc`, `fflush`
- **Key Features:**
    - Platform-specific default newline
    - Buffered output
    - Explicit flush support
- **ViperDOS Action:** Implement FILE* APIs
- **Priority:** P1

#### rt_binfile.c

- **Purpose:** Binary file operations
- **OS Dependencies:** Minimal (FILE* APIs)
- **libc Functions:** `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`
- **Key Features:**
    - Little-endian integer encoding
    - Raw byte I/O
- **ViperDOS Action:** Implement FILE* APIs
- **Priority:** P1

#### rt_memstream.c

- **Purpose:** In-memory binary stream
- **OS Dependencies:** None (pure C)
- **libc Functions:** `malloc`, `realloc`, `free`, `memcpy`
- **Key Features:**
    - Read/write positioning
    - Auto-growing buffer
    - Seek support
- **ViperDOS Action:** None required
- **Priority:** P1

---

### Category: Math and Vectors (Pure C + math.h)

#### rt_vec2.c

- **Purpose:** 2D vector mathematics
- **OS Dependencies:** None (pure C + math.h)
- **math.h Functions:** `sqrt`, `atan2`, `cos`, `sin`
- **Key Features:**
    - Immutable vector type with x,y components (double)
    - Operations: add, sub, mul, div, neg, dot, cross
    - Length, distance, normalization, interpolation
    - Angle and rotation operations
- **ViperDOS Action:** Requires math library
- **Priority:** P1

#### rt_vec3.c

- **Purpose:** 3D vector mathematics
- **OS Dependencies:** None (pure C + math.h)
- **math.h Functions:** `sqrt`
- **Key Features:**
    - Immutable vector type with x,y,z components (double)
    - 3D cross product returns vector (not scalar)
    - Same operations as Vec2 extended to 3D
- **ViperDOS Action:** Requires math library
- **Priority:** P1

#### rt_mat3.c

- **Purpose:** 3x3 matrix math for 2D affine transforms
- **OS Dependencies:** None (pure C + math.h)
- **math.h Functions:** `cos`, `sin`, `fabs`
- **Key Features:**
    - Row-major 3x3 matrix storage
    - Transformation factories: translate, scale, rotate, shear
    - Matrix operations: add, sub, mul, transpose, inverse, determinant
    - Transform Vec2 points and vectors
- **ViperDOS Action:** Requires math library
- **Priority:** P1

---

### Category: Game Development (Pure C)

#### rt_sprite.c

- **Purpose:** 2D sprite class for game development
- **OS Dependencies:** None (pure C, depends on rt_pixels)
- **Key Features:**
    - Position, scale, rotation, visibility
    - Animation support (up to 64 frames)
    - Collision detection (AABB overlap, point containment)
    - Uses rt_timer_ms() for frame timing
- **Dependencies:** rt_pixels.c, rt_graphics.c, rt_heap.c
- **ViperDOS Action:** Should work after dependencies
- **Priority:** P2

#### rt_tilemap.c

- **Purpose:** Tilemap for tile-based 2D rendering
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - Grid-based tile storage with tileset support
    - Region-based rendering for scrolling
    - Pixel-to-tile and tile-to-pixel conversions
- **Dependencies:** rt_pixels.c, rt_graphics.c, rt_heap.c
- **ViperDOS Action:** Should work after dependencies
- **Priority:** P2

#### rt_camera.c

- **Purpose:** 2D camera for world-to-screen transforms
- **OS Dependencies:** None (pure C)
- **Key Features:**
    - Position, zoom, rotation
    - Bounds clamping
    - World-to-screen and screen-to-world conversion
    - Follow target tracking
- **ViperDOS Action:** Should work as-is
- **Priority:** P2

---

### Category: Graphics and Pixels (Moderate OS Deps)

#### rt_pixels.c

- **Purpose:** Software image buffer for pixel manipulation
- **OS Dependencies:** Minimal (pure C + file I/O for BMP)
- **libc Functions:** `malloc`, `free`, `memcpy`, `memset`, `calloc`
- **math.h Functions:** `floor`, `ceil`, `cos`, `sin`, `fabs`
- **stdio.h Functions:** `fopen`, `fclose`, `fread`, `fwrite`, `fseek` (BMP I/O)
- **Key Features:**
    - RGBA pixel buffer with inline data storage
    - BMP loading/saving
    - Pixel manipulation: fill, blit, scale, rotate, flip
    - Clipping and alpha blending
- **ViperDOS Action:** Implement file I/O + math library
- **Priority:** P1

#### rt_font.c

- **Purpose:** Embedded 8x8 bitmap font
- **OS Dependencies:** None (static data)
- **Key Features:**
    - ASCII 32-126 character coverage
    - 8x8 pixels per glyph, MSB on left
    - Simple glyph lookup function
- **ViperDOS Action:** None required
- **Priority:** P1

#### rt_graphics.c

- **Purpose:** Graphics runtime bridge to vgfx library
- **OS Dependencies:** HEAVY (depends on external vgfx)
- **Conditional:** `#ifdef VIPER_ENABLE_GRAPHICS`
- **libc Functions:** `stdlib.h`, `string.h`
- **Key Features:**
    - Canvas creation/destruction (wraps vgfx window)
    - Drawing primitives: line, box, frame, disc, ring, plot
    - Event polling and input handling
    - Integration with rt_input.c for keyboard/mouse
- **External Dependencies:** vgfx library (platform graphics abstraction)
- **ViperDOS Action:** Port or implement vgfx backend for ViperDOS
- **Priority:** P1 (for graphical programs)

---

### Category: Audio (HEAVY OS DEPS)

#### rt_audio.c

- **Purpose:** Audio runtime bridge to vaud library
- **OS Dependencies:** HEAVY (depends on external vaud)
- **Conditional:** `#ifdef VIPER_ENABLE_AUDIO`
- **Atomics:** `__atomic_load_n`, `__atomic_store_n`, `__atomic_test_and_set`, `__atomic_clear`
- **Key Features:**
    - Audio playback, volume, channels
    - Sound and music loading
    - Provides stub implementations when audio disabled
- **External Dependencies:** vaud library (platform audio abstraction)
- **ViperDOS Action:** Port or implement vaud backend for ViperDOS
- **Priority:** P2 (for audio programs)

---

### Category: GUI Widgets (HEAVY EXTERNAL DEPS)

#### rt_gui.c

- **Purpose:** GUI runtime bridge to vgui widget library
- **OS Dependencies:** HEAVY (depends on external vgfx/vgui)
- **libc Functions:** `malloc`, `free`, `memcpy`, `calloc`
- **ctype Functions:** `toupper`
- **Platform-Specific:** `strcasecmp` (POSIX) vs `_stricmp` (Windows)
- **Key Features:**
    - Application window with root widget container
    - Event dispatch to widget tree
    - Theme support (dark theme default)
    - Widget creation and property access
- **External Dependencies:** vgfx library, vgui library
- **ViperDOS Action:** Port vgfx/vgui libraries
- **Priority:** P2 (for GUI programs)

---

## Files Already with ViperDOS Support

### CRITICAL FINDING: Existing Platform Stubs

The following files already contain `#if defined(__viperdos__)` or similar platform detection for ViperDOS. This
significantly reduces porting effort as the infrastructure is already in place.

#### rt_file_io.c (Lines 83-99)

```c
#elif defined(__viperdos__)
// TODO: ViperDOS - include file I/O headers when available
#include <sys/stat.h>
#include <sys/types.h>
typedef long ssize_t;
```

**Status:** Headers included, needs syscall implementations

#### rt_threads.c (Lines 124-180)

```c
#if defined(__viperdos__)
// TODO: ViperDOS - implement threading using ViperDOS task/thread syscalls
void *rt_thread_start(void *entry, void *arg) {
    rt_trap("Viper.Threads.Thread: unsupported on this platform");
    return NULL;
}
// ... all threading functions stub to trap
```

**Status:** Stubs in place, need actual implementations

#### rt_machine.c (Lines 73-77, 155-157)

```c
#elif defined(__viperdos__)
    return make_str("viperdos");
// ...
    return make_str("0.2.7");
```

**Status:** OS name and version implemented, other queries need work

#### rt_rand.c (Lines 51-58, 73-77)

```c
#elif defined(__viperdos__)
// TODO: ViperDOS - include random number headers when available
// ViperDOS has VirtIO-RNG and syscall for random bytes
// ...
    // TODO: ViperDOS - implement using VirtIO-RNG or getrandom syscall
    return -1;
```

**Status:** Comment mentions VirtIO-RNG support needed

#### rt_network.c (Lines 66-79)

```c
#elif defined(__viperdos__)
// TODO: ViperDOS - include network headers when available
// ViperDOS has BSD-style socket API in kernel/net/

typedef int socket_t;
#define INVALID_SOCK (-1)
#define SOCK_ERROR (-1)
#define CLOSE_SOCKET(s) (-1)  // TODO: ViperDOS - implement close
#define GET_LAST_ERROR() (-1) // TODO: ViperDOS - implement errno
// ... more stubs
```

**Status:** Socket type defined, BSD-style API noted, stubs in place

#### rt_exec.c (Lines 102-177)

```c
#elif defined(__viperdos__)
// TODO: ViperDOS - include process control headers when available
// ...
static int64_t exec_spawn(const char *program, void *args)
{
    // TODO: ViperDOS - implement using task_spawn or similar syscall
    (void)program;
    (void)args;
    return -1;
}
```

**Status:** Stub functions return -1 or empty string, mentions task_spawn

#### rt_term.c (Lines 38-40)

```c
#elif defined(__viperdos__)
// TODO: ViperDOS - include terminal control headers when available
// ViperDOS will need termios-like APIs for console I/O
```

**Status:** Placeholder only, notes termios-like APIs needed

#### rt_time.c (Lines 169-190)

```c
#elif defined(__viperdos__)
// ViperDOS time implementation
// TODO: ViperDOS - implement using ViperDOS syscalls (sys_clock_gettime or similar)

void rt_sleep_ms(int32_t ms)
{
    // TODO: ViperDOS - implement sleep using nanosleep syscall
    (void)ms;
}

int64_t rt_timer_ms(void)
{
    // TODO: ViperDOS - implement using monotonic clock syscall
    return 0;
}
```

**Status:** Full stubs for sleep and timer functions, return 0 or no-op

#### rt_dir.c (Lines 100-106)

```c
#elif defined(__viperdos__)
// TODO: ViperDOS - include appropriate headers when available
// ViperDOS uses POSIX-like APIs
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define PATH_SEP '/'
```

**Status:** PATH_MAX defined, PATH_SEP set to '/', notes POSIX-like APIs

#### rt_watcher.c (Lines 50-52)

```c
#elif defined(__viperdos__)
// TODO: ViperDOS - file system watching not yet implemented
// Would need kernel support for file change notifications
```

**Status:** Placeholder only, notes kernel support needed (can defer)

#### rt_platform.h (Expected)

**Status:** Needs `RT_PLATFORM_VIPERDOS` macro definition

### Summary of Pre-Existing ViperDOS Work

| File         | Status         | Work Remaining                 |
|--------------|----------------|--------------------------------|
| rt_file_io.c | Headers set up | Implement POSIX-style syscalls |
| rt_threads.c | Full stubs     | Implement task/thread syscalls |
| rt_machine.c | Partial impl   | Implement system info queries  |
| rt_rand.c    | TODO noted     | Implement VirtIO-RNG interface |
| rt_network.c | Types + stubs  | Implement BSD socket API       |
| rt_exec.c    | Full stubs     | Implement task_spawn syscall   |
| rt_term.c    | Placeholder    | Implement termios-like APIs    |
| rt_time.c    | Full stubs     | Implement clock syscalls       |
| rt_dir.c     | Constants set  | Implement directory syscalls   |
| rt_watcher.c | Placeholder    | Defer (needs kernel support)   |

This indicates a clear design pattern was established for ViperDOS porting:

1. Use `#if defined(__viperdos__)` for platform detection
2. Include appropriate headers (when available)
3. Stub with `rt_trap()` for unimplemented features
4. TODO comments reference specific ViperDOS capabilities

---

## VM Requirements

### Overview

The VM (Virtual Machine) is implemented in C++ and is largely platform-agnostic. It interprets IL (Intermediate
Language) code and uses the C runtime library for all platform-specific operations.

### Key Files Analyzed

#### VM.cpp

- **Purpose:** Core VM execution engine
- **OS Dependencies:** None (pure C++)
- **Key Features:**
    - Pluggable dispatch strategies (function table, switch, computed goto)
    - Trap handling
    - Opcode execution
- **Dependencies:** IL core classes, C++ stdlib, rt_context.h
- **ViperDOS Action:** Should compile as-is

#### VMInit.cpp

- **Purpose:** VM initialization and setup
- **OS Dependencies:** Minimal
- **Key Features:**
    - Locale initialization (`setlocale`)
    - Runtime descriptor validation
    - Thread runtime registration
- **Dependencies:** IL core classes, rt_context.h
- **ViperDOS Action:** Should compile as-is

#### ThreadsRuntime.cpp

- **Purpose:** Threading support for VM
- **OS Dependencies:** MODERATE (depends on rt_threads)
- **ViperDOS Action:** Depends on threading implementation

### VM Porting Summary

The VM itself is largely portable C++. The platform-specific work is delegated to:

- **rt_context.c** - Thread-local storage for VM context
- **rt_threads.c** - Threading primitives
- **Other runtime files** - File I/O, networking, etc.

**Estimated Effort:** LOW - VM compiles with standard C++ compiler

---

## Codegen Requirements

### Overview

The codegen (code generation) subsystem compiles IL to native machine code. Currently targets x86-64 with System V ABI.

### Platform Considerations

- **Target Architecture:** x86-64 (aarch64 support planned)
- **ABI:** SysV x86-64 calling convention
- **Object Format:** ELF on Linux, Mach-O on macOS
- **Linking:** Uses system linker (ld/lld)

### ViperDOS Implications

For ViperDOS on aarch64:

1. **Primary path:** Use VM interpreter (no native codegen needed)
2. **Future path:** Implement aarch64 codegen with ViperDOS-specific object format/linker

**Estimated Effort:** NONE for VM-only, SIGNIFICANT for native codegen

---

## Frontend Requirements

### Overview

Frontends (BASIC, Pascal, Zia) parse source code and generate IL. They are largely platform-agnostic C++.

### Platform Considerations

- **File I/O:** Read source files, write IL output
- **Path handling:** Platform-specific path separators
- **Console output:** Error messages and diagnostics

### Key Dependencies

- Standard C++ library (string, vector, map, iostream)
- C file I/O (fopen, fread, etc.)
- rt_path utilities

### ViperDOS Implications

Frontends should work once file I/O is implemented.

**Estimated Effort:** LOW - standard C++ code

---

## Tool Requirements

### Overview

The tools directory contains command-line utilities for working with Viper code. Most are simple C++ programs that use
the compiler libraries.

### Key Tools

- **viprc** - Viper compiler driver
- **vipvm** - VM runner
- **vipasm** - IL assembler
- **vipdis** - IL disassembler
- **vipfmt** - Code formatter

### ViperDOS Implications

Tools should work once:

1. File I/O is implemented
2. Command-line arguments work (rt_args.c)
3. Standard C++ I/O works

**Estimated Effort:** LOW - standard C++ command-line tools

---

## TUI Requirements (ViperTUI)

### Overview

ViperTUI provides terminal-based UI using ANSI escape sequences. Platform-agnostic C++ with minimal OS dependencies.

### Architecture

```
┌─────────────────────────────────────────┐
│             ViperTUI                    │
│  ┌─────────────────────────────────┐    │
│  │ Widgets (button, label, list,   │    │
│  │  tree, text_view, splitter)     │    │
│  └─────────────────────────────────┘    │
│  ┌─────────────────────────────────┐    │
│  │ Core (screen, renderer, input)  │    │
│  └─────────────────────────────────┘    │
│  ┌─────────────────────────────────┐    │
│  │ Term I/O (real, string mock)    │    │
│  └─────────────────────────────────┘    │
└─────────────────────────────────────────┘
                    │
                    ▼
         stdout (ANSI terminal)
```

### Key Files

#### term_io.cpp

- **Purpose:** Terminal I/O abstraction
- **OS Dependencies:** Minimal (stdio)
- **Features:** `RealTermIO` (stdout), `StringTermIO` (mock)
- **ViperDOS Action:** Works as-is (stdio based)

#### input.cpp

- **Purpose:** Terminal input handling
- **OS Dependencies:** MODERATE (termios/conio)
- **ViperDOS Action:** Implement terminal input API

#### session.cpp

- **Purpose:** Terminal session management
- **OS Dependencies:** MODERATE (termios, signals)

### ViperDOS Requirements

1. Terminal output via stdout - **WORKS**
2. Terminal input (raw mode) - **NEEDS rt_term.c**
3. Terminal size detection - **NEEDS ioctl equivalent**
4. Optional: clipboard support

**Estimated Effort:** LOW - uses stdio for output

---

## Graphics Requirements (ViperGFX)

### Overview

ViperGFX provides cross-platform 2D graphics with software framebuffer rendering.

### Architecture

```
┌─────────────────────────────────────────┐
│           ViperGFX Core (vgfx.c)        │
│  - Window lifecycle, event queue        │
│  - Framebuffer operations               │
│  - FPS limiting                         │
└─────────────────────────────────────────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ X11 Backend  │ │ Win32 Backend│ │ Mock Backend │
└──────────────┘ └──────────────┘ └──────────────┘
```

### Key Files

#### vgfx.c (Core - Platform Agnostic)

- Thread-local error storage
- Lock-free SPSC event queue
- Aligned framebuffer allocation
- Integer-only math

#### vgfx_platform_linux.c (X11)

- Xlib window management
- XImage framebuffer blitting
- KeySym translation

#### vgfx_platform_mock.c

- Headless testing backend
- Starting point for ViperDOS backend

### ViperDOS Backend Requirements

Need `vgfx_platform_viperdos.c`:

- Window creation via displayd IPC
- Framebuffer sharing (shared memory)
- Event delivery from wm/input
- Timer for FPS limiting

**Estimated Effort:** MODERATE - displayd provides similar capabilities

---

## Audio Requirements (ViperAUD)

### Overview

ViperAUD provides cross-platform audio with software mixing.

### Architecture

```
┌─────────────────────────────────────────┐
│      ViperAUD Core (vaud.c + mixer)     │
│  - Context management, software mixing  │
└─────────────────────────────────────────┘
                    │
        ┌───────────┴───────────┐
        ▼                       ▼
┌──────────────────┐   ┌──────────────────┐
│  ALSA Backend    │   │  Win32 Backend   │
│  (Linux)         │   │  (Windows)       │
└──────────────────┘   └──────────────────┘
```

### Key Files

#### vaud.c (Core)

- Thread-local errors
- Mutex abstraction

#### vaud_mixer.c (Pure C)

- 16-bit stereo mixing
- Volume/pan control

#### vaud_wav.c

- WAV file loading

### ViperDOS Backend Requirements

Need `vaud_platform_viperdos.c`:

- Audio device opening (audioctl IPC)
- Audio buffer submission
- Audio thread/callback

**Estimated Effort:** MODERATE - ViperDOS has audio driver infrastructure

---

## GUI Requirements (ViperGUI)

### Overview

ViperGUI provides a widget toolkit built on ViperGFX. All widgets are pure C.

### Architecture

```
┌─────────────────────────────────────────┐
│             ViperGUI Widgets            │
│  (button, label, textinput, listbox,    │
│   treeview, menubar, scrollview, etc.)  │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│  Core (vg_widget, vg_event, vg_layout)  │
└─────────────────────────────────────────┘
                    │
                    ▼
         ┌─────────────────┐
         │   ViperGFX      │
         └─────────────────┘
```

### Key Files (All Pure C)

- vg_widget.c - Widget base class
- vg_event.c - Event dispatch
- vg_layout.c - Layout algorithms
- vg_theme.c - Styling system
- vg_ttf.c - TrueType font parsing
- 30+ widget files

### ViperDOS Status

**Works automatically once ViperGFX backend is implemented.**

**Estimated Effort:** NONE (inherits from ViperGFX)

---

## OS Capabilities Summary

### Required C Standard Library Functions

#### Memory

- `malloc`, `free`, `realloc`, `calloc`
- `memcpy`, `memmove`, `memset`, `memcmp`, `memchr`

#### String

- `strlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`
- `strcat`, `strncat`, `strchr`, `strrchr`, `strstr`
- `snprintf`, `sprintf`, `sscanf`

#### Character Classification (ctype.h)

- `isspace`, `isdigit`, `isalpha`, `isalnum`
- `tolower`, `toupper`

#### Math (math.h / libm)

- `sqrt`, `pow`, `exp`, `log`, `log10`
- `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`
- `floor`, `ceil`, `round`, `trunc`, `fmod`, `fabs`
- `isnan`, `isinf`, `signbit`

#### I/O

- `fprintf`, `printf`, `snprintf`
- `fopen`, `fclose`, `fread`, `fwrite`, `fseek`, `ftell`
- `stdin`, `stdout`, `stderr`

#### Other

- `exit`, `abort`
- `qsort`, `bsearch`

### Required Atomic Operations

- `__atomic_load_n` / `__atomic_store_n`
- `__atomic_fetch_add` / `__atomic_fetch_sub`
- `__atomic_compare_exchange_n`
- `__atomic_test_and_set` / `__atomic_clear`

### Required Compiler Intrinsics

- `__builtin_popcountll` - population count
- `__builtin_clzll` - count leading zeros
- `__builtin_ctzll` - count trailing zeros
- `__builtin_bswap64` - byte swap

### Required Platform APIs (by priority)

#### P0 - Critical

- Thread-local storage (`RT_THREAD_LOCAL`)
- High-resolution timer
- File open/read/write/close/seek
- Basic terminal output

#### P1 - High

- Directory operations
- Thread creation/synchronization
- Random number generation
- Date/time functions

#### P2 - Medium

- Networking (sockets)
- Compression - **NONE NEEDED** (rt_compress.c is self-contained!)
- File system watching

#### P3 - Low/Deferrable

- Process execution
- TLS/SSL
- WebSocket

---

## Runtime Porting Checklist

### Phase 1: Core Runtime (P0)

- [ ] rt_platform.h - Add ViperDOS detection
- [ ] rt_heap.c - Verify atomics work
- [ ] rt_memory.c - Verify allocator works
- [ ] rt_string.c/rt_string_ops.c - Should work as-is
- [ ] rt_array.c - Verify atomics work
- [ ] rt_array_i64.c - Verify atomics work
- [ ] rt_array_f64.c - Verify atomics work
- [ ] rt_array_obj.c - Verify atomics work
- [ ] rt_array_str.c - Verify atomics work
- [ ] rt_seq.c - Should work as-is
- [ ] rt_map.c - Should work as-is
- [ ] rt_bytes.c - Should work as-is
- [ ] rt_trap.c - Implement stderr + exit
- [ ] rt_error.c - Should work as-is
- [ ] rt_exc.c - Should work as-is
- [ ] rt_debug.c - Implement stdout + fflush
- [ ] rt_math.c - Implement/link libm
- [ ] rt_fp.c - Requires math library
- [ ] rt_time.c - Implement timer (**STUBS EXIST**)
- [ ] rt_context.c - Implement TLS
- [ ] rt_file.c/rt_file_io.c - Implement file I/O (**STUBS EXIST**)
- [ ] rt_modvar.c - Should work as-is
- [ ] rt_oop_dispatch.c - Should work as-is
- [ ] rt_type_registry.c - Should work as-is
- [ ] rt_object.c - Verify atomics work

### Phase 2: Extended Runtime (P1)

- [ ] rt_format.c - Implement localeconv or stub
- [ ] rt_json.c - Should work as-is
- [ ] rt_codec.c - Should work as-is
- [ ] rt_hash.c - Should work as-is
- [ ] rt_crc32.c - Should work as-is
- [ ] rt_list.c - Should work as-is
- [ ] rt_set.c - Should work as-is
- [ ] rt_bag.c - Should work as-is
- [ ] rt_box.c - Should work as-is
- [ ] rt_pqueue.c - Should work as-is
- [ ] rt_queue.c - Should work as-is
- [ ] rt_stack.c - Should work as-is
- [ ] rt_ring.c - Should work as-is
- [ ] rt_treemap.c - Should work as-is
- [ ] rt_dir.c - Implement directory ops (**STUBS EXIST**)
- [ ] rt_path.c - Should mostly work
- [ ] rt_datetime.c - Implement time functions
- [ ] rt_log.c - Implement time + stderr
- [ ] rt_output.c - Implement buffered I/O
- [ ] rt_rand.c - Implement entropy source (**STUBS EXIST**)
- [ ] rt_threads.c - Implement threading (**STUBS EXIST**)
- [ ] rt_term.c - Implement terminal (**STUBS EXIST**)
- [ ] rt_input.c - Implement input events
- [ ] rt_machine.c - Implement system info (**PARTIAL IMPL**)
- [ ] rt_stopwatch.c - Implement high-res timer
- [ ] rt_countdown.c - Implement timer + sleep
- [ ] rt_vec2.c - Requires math library
- [ ] rt_vec3.c - Requires math library
- [ ] rt_mat3.c - Requires math library
- [ ] rt_pixels.c - Requires file I/O + math
- [ ] rt_font.c - Should work as-is
- [ ] rt_graphics.c - Port vgfx backend
- [ ] rt_linereader.c - Requires FILE* APIs
- [ ] rt_linewriter.c - Requires FILE* APIs
- [ ] rt_binfile.c - Requires FILE* APIs
- [ ] rt_memstream.c - Should work as-is
- [ ] rt_ns_bridge.c - Should work as-is

### Phase 3: Optional Features (P2-P3)

- [ ] rt_regex.c - Should work as-is
- [ ] rt_template.c - Should work as-is
- [ ] rt_csv.c - Should work as-is
- [ ] rt_bigint.c - Should work as-is
- [ ] rt_bits.c - Verify intrinsics
- [ ] rt_aes.c - Should work as-is (pure C crypto)
- [ ] rt_keyderive.c - Should work as-is (PBKDF2)
- [ ] rt_threadpool.c - After threading
- [ ] rt_channel.c - After threading
- [ ] rt_monitor.c - After threading
- [ ] rt_safe_i64.c - After threading/monitor
- [ ] rt_guid.c - After random
- [ ] rt_network.c - Implement sockets (**STUBS EXIST**)
- [ ] rt_network_http.c - After networking
- [ ] rt_compress.c - Should work as-is (self-contained!)
- [ ] rt_archive.c - After compression
- [ ] rt_crypto.c - Should work as-is (self-contained!)
- [ ] rt_tls.c - After networking + crypto
- [ ] rt_websocket.c - Defer
- [ ] rt_exec.c - Defer (**STUBS EXIST**)
- [ ] rt_watcher.c - Defer (**STUBS EXIST**)
- [ ] rt_sprite.c - After graphics/pixels
- [ ] rt_tilemap.c - After graphics/pixels
- [ ] rt_camera.c - Should work as-is
- [ ] rt_audio.c - Port vaud backend
- [ ] rt_gui.c - Port vgfx/vgui backends

---

## Implementation Priority

### Immediate (Required for basic programs)

1. Platform detection and TLS
2. Memory allocation (malloc/free)
3. Atomic operations
4. String operations
5. File I/O basics
6. Terminal output
7. Timer/timing
8. Math library

### Short-term (Required for most programs)

1. Collections (array, seq, map)
2. JSON parsing
3. Directory operations
4. Threading basics
5. Random numbers
6. Date/time

### Medium-term (Required for advanced features)

1. Networking
2. Compression
3. Cryptography
4. Full threading (pools, channels)

### Long-term (Nice to have)

1. TLS/SSL
2. WebSocket
3. Process execution
4. File watching

---

*Document last updated: 2026-01-26*
*Analysis Status: **COMPLETE***

- *Runtime: ~90% complete - 70+ files analyzed*
- *VM: Complete*
- *Codegen: Complete*
- *Frontends: Complete*
- *TUI: Complete (ViperTUI)*
- *Graphics: Complete (ViperGFX)*
- *Audio: Complete (ViperAUD)*
- *GUI: Complete (ViperGUI)*

**Key Findings Summary:**

1. **Self-contained implementations:** rt_compress.c (DEFLATE/GZIP), rt_crypto.c (SHA-256, ChaCha20-Poly1305, X25519),
   rt_tls.c (TLS 1.3) - NO external library dependencies!
2. **Extensive ViperDOS stubs:** 10+ files already have `#if defined(__viperdos__)` platform detection
3. **Pure C majority:** ~70% of runtime files have no or minimal OS dependencies
4. **VM is portable:** Standard C++ with platform specifics delegated to runtime
5. **Frontends are portable:** Standard C++ file processing
6. **GUI/TUI portable:** Build on portable base layers (ViperGFX/stdio)

---

## Executive Summary

### Porting Effort Estimate

| Component           | Files | Effort   | Notes                      |
|---------------------|-------|----------|----------------------------|
| C Standard Library  | -     | LOW      | newlib/musl provides most  |
| Runtime (Pure C)    | ~50   | NONE     | Work as-is                 |
| Runtime (Moderate)  | ~15   | LOW      | Minor platform adaptations |
| Runtime (Heavy)     | ~15   | MODERATE | Need platform backends     |
| VM                  | ~20   | LOW      | Portable C++               |
| Codegen             | ~30   | NONE*    | VM-only approach           |
| Frontends           | ~50   | LOW      | Portable C++               |
| TUI (ViperTUI)      | ~40   | LOW      | ANSI terminal based        |
| Graphics (ViperGFX) | ~10   | MODERATE | Need displayd backend      |
| Audio (ViperAUD)    | ~5    | MODERATE | Need audio backend         |
| GUI (ViperGUI)      | ~40   | NONE     | Builds on ViperGFX         |

*Native codegen would require significant work for aarch64

### Critical Path for Basic Viper Programs

1. **rt_platform.h** - Add `RT_PLATFORM_VIPERDOS` definition
2. **rt_context.c** - Implement `RT_THREAD_LOCAL` (or single-threaded stub)
3. **rt_file_io.c** - Complete POSIX-style file I/O stubs
4. **rt_time.c** - Implement clock/timer syscalls
5. **rt_heap.c** - Verify malloc/free work correctly
6. **rt_string.c** - Should work as-is (uses heap)
7. **libc** - Ensure math.h, stdio.h, stdlib.h, string.h available

### Positive Surprises

1. **No zlib needed** - rt_compress.c implements DEFLATE/GZIP natively
2. **No OpenSSL needed** - rt_crypto.c implements SHA-256, ChaCha20, X25519 natively
3. **TLS 1.3 self-contained** - rt_tls.c uses only rt_crypto.c
4. **Pre-existing ViperDOS work** - 10+ files already have platform stubs
5. **Extensive pure-C code** - Most collections, algorithms, formats are portable

### Recommended Implementation Order

**Week 1-2: Core Foundation**

- Platform headers and detection
- File I/O (open, read, write, close)
- Timer and sleep functions
- Basic terminal output

**Week 3-4: Data Structures**

- Memory allocation verification
- String operations
- Collections (array, seq, map)
- JSON parsing

**Week 5-6: Advanced Runtime**

- Threading primitives
- Synchronization (mutex, condition)
- Random number generation
- Date/time functions

**Week 7-8: Graphics/Audio**

- ViperGFX displayd backend
- ViperAUD audio backend
- Input event handling

**Beyond: Optional Features**

- Networking (sockets)
- Process execution
- File watching

### Conclusion

ViperDOS can host the Viper platform with **moderate effort**. The codebase is well-structured for portability, with
clear separation between platform-agnostic logic and OS-specific backends. The discovery of self-contained
implementations for compression, cryptography, and TLS significantly reduces external dependencies.

The recommended approach:

1. Start with VM-only execution (no native codegen)
2. Implement file I/O and terminal support first
3. Add threading support for multi-threaded BASIC programs
4. Implement graphics/audio backends for games and demos
5. Add networking last (most complex OS integration)

**Total estimated effort:** 4-8 developer-weeks for basic functionality, 12-16 weeks for full feature parity.
