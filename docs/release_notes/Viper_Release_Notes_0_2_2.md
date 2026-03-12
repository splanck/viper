# Viper Compiler Platform - Release Notes

> **Development Status**: Pre-Alpha
> These are early development releases. Viper is under active development and not ready for production use.
> Future milestones will define supported releases when appropriate.

## Version 0.2.2 - Pre-Alpha (March 2026)

### Release Overview

Version 0.2.2 hardens every layer of the platform for production readiness. The Zia language
reaches full feature parity with the BASIC frontend — gaining properties, destructors, exception
handling, compound assignments, and an 18-code warning system. A new `viper package` command
generates platform-native installers (macOS .app, Linux .deb, Windows self-extracting .exe)
with zero external dependencies. The x86-64 backend adds 300+ stress tests and fixes critical
NaN/float-conversion bugs, while HiDPI display support lands for all Canvas-based demos. Across
the board, 1,108 files are touched with ~99,000 net new lines, bringing the test count from
1,196 to 1,261.

---

### New Features

#### Zia Language — Feature Parity with BASIC

Seven feature-parity batches close all remaining gaps between Zia and the BASIC frontend:

**Properties (get/set)**

Entity properties with custom getter/setter logic:

```rust
entity Counter {
    Integer _count;

    get Count() -> Integer { return _count; }
    set Count(value: Integer) { _count = value; }
}
```

**Destructors**

Deterministic cleanup via `deinit` blocks, lowered to `__dtor_TypeName` IL functions:

```rust
entity FileHandle {
    deinit {
        IO.File.Close(self.handle);
    }
}
```

**Exception Handling (try/catch/finally/throw)**

Full structured exception handling via IL EH opcodes:

```rust
try {
    var result = riskyOperation();
} catch (e: Error) {
    Terminal.Say("Error: " + e.Message());
} finally {
    cleanup();
}
```

**Compound Assignment Operators**

`+=`, `-=`, `*=`, `/=`, `%=` operators for concise mutation:

```rust
score += 100;
health -= damage;
```

**Additional Parity Features**
- `is` type-check operator with set literal lowering
- Default parameter values at call sites
- Static fields and static methods on entities
- Interface dispatch via `rt_get_interface_impl` + `call.indirect`
- Operator confusion warnings (W017/W018) for `=` vs `==` and `!=` vs `=`

#### Zia Language — Struct Literals, Fixed-Size Arrays, If-Expressions

Three new language constructs improve expressiveness:

```rust
// Struct literals (ZIA-001)
var pos = Position { x = 10, y = 20 };

// Fixed-size array fields (ZIA-002)
entity Board {
    Integer[64] squares;
}

// If-expressions (ZIA-004)
var label = if score > 100 { "High" } else { "Low" };
```

#### Zia Compiler — Warning System (W001–W018)

A complete diagnostic warning infrastructure with 18 codes:

| Code | Description |
|------|-------------|
| W001 | Unused variable |
| W002 | Unreachable code |
| W003 | Implicit narrowing conversion |
| W004 | Variable shadowing |
| W005 | Float equality comparison |
| W006 | Empty if/while body |
| W007 | Assignment in condition |
| W008 | Missing return in non-void function |
| W009 | Self-assignment |
| W010 | Division by zero |
| W011 | Redundant boolean expression |
| W012 | Duplicate bind statement |
| W013 | Unused function result |
| W014 | Uninitialized variable use |
| W015 | Optional access without null check |
| W016 | Deprecated API usage |
| W017 | Assignment `=` where comparison `==` likely intended |
| W018 | Comparison `!=` where assignment likely intended |

Controlled via CLI flags (`-Wall`, `-Werror`, `-Wno-XXX`) and suppression pragmas.

#### Zia Compiler — Type Soundness Audit

Eight type system gaps identified and fixed:

- **GAP-1/2**: Definite-assignment analysis with if/else branch tracking
- **GAP-3**: As-cast compatibility checking
- **GAP-4/5**: Optional field/method null-check warnings
- **GAP-6**: Missing field errors
- **GAP-7**: User-defined call arity and type validation
- **GAP-8**: Optional-to-non-Optional parameter validation

Includes 40 Zia + 10 BASIC type soundness regression tests.

#### Zia Compiler — Typed Sequences (seq\<str\>)

`seq<str>` is now a first-class typed sequence type enabling for-in iteration over string
sequences returned by runtime methods:

```rust
bind Viper.IO;

func start() {
    for file in Dir.FilesSeq("/tmp") {
        Terminal.Say(file);
    }
}
```

Runtime signature parser extended to parse `seq<T>` annotations. New runtime functions
`rt_seq_str_get` and `rt_seq_next_str` for typed element access.

#### `viper package` — Cross-Platform Application Packaging (VAPS)

New subcommand generates platform-native installers with zero external dependencies:

```bash
viper package myapp.viper.project --target macos
viper package myapp.viper.project --target linux-deb
viper package myapp.viper.project --target windows
viper package myapp.viper.project --target tarball
```

**Supported formats:**

| Target | Output | Contents |
|--------|--------|----------|
| macOS | `.app` in `.zip` | Info.plist, ICNS icon, UTI declarations |
| Linux | `.deb` package | ar/tar/gzip, .desktop file, MIME types, postinst/prerm |
| Windows | Self-extracting `.exe` | PE32+ with import tables, RT_MANIFEST, RT_ICON, .lnk shortcuts |
| Tarball | `.tar.gz` | Portable archive with Unix permissions |

All binary format generation (DEFLATE, GZIP, CRC-32, MD5, ZIP, ar, tar, PE32+, ICO, ICNS,
PNG resize) is implemented in GC-free C++ with no external library dependencies.

**Manifest directives** in `viper.project`:

```ini
package-name = MyApp
package-author = Jane Developer
package-description = A cross-platform Viper application
package-icon = icon.png
package-identifier = com.example.myapp
file-assoc = .myf:application/x-myformat
shortcut-desktop = true
```

#### Runtime Library — 226 Classes (up from 197)

29 new runtime classes registered since v0.2.1, including:

- `Box`, `Diagnostics`, `Parse`, `GC`, `Container` — core utility classes
- `BigInt`, `Mat3`, `Mat4` — math classes (previously internal, now exposed to frontends)
- `Xml`, `Yaml`, `Serialize` — data format classes
- `Channel`, `Pool`, `Parallel` — concurrency classes
- `Aes`, `LazySeq` — crypto and functional classes
- `CommandPalette`, `Breadcrumb`, `Minimap`, `MessageBox`, `FileDialog`,
  `Toast`, `Tooltip`, `Clipboard`, `Shortcuts`, `Cursor` — GUI classes
- `Playlist` — audio class

New collection methods: `List.Shuffle` (Fisher-Yates), `List.Clone` (shallow copy),
`Queue.ToList`, `Stack.ToList`, `DefaultMap.IsEmpty`. Network additions: `Tls.RecvLine`,
`Http.Patch`, `Http.Options`. Canvas extensions: `Canvas.GetTitle`, `Canvas.Resize`,
`Canvas.Close`.

API naming standardization: `List.Flip` → `List.Reverse`, `Set.Merge` → `Set.Union`,
`Set.Common` → `Set.Intersect`, `TreeMap.Drop` → `TreeMap.Remove` (old names retained
as aliases for backward compatibility).

#### IL Module Linker — Cross-Language Interop

New `ModuleLinker` enables multi-module symbol resolution with linkage types (Export, Import,
Internal). `InteropThunks` handle boolean bridging between i1/i64 representations across
language boundaries. Architecture document: `docs/interop.md`.

#### GUI Completion

**Platform Window Management (12 new APIs)**

All three platforms (macOS Cocoa, Windows Win32, Linux X11) gain:
`minimize`, `maximize`, `restore`, `focus`, `is_minimized`, `is_maximized`, `is_focused`,
`get_position`, `set_position`, `set_cursor`, `set_cursor_visible`, `set_prevent_close`.

**Widget Implementations**

- `Slider` — horizontal/vertical track, fill bar, thumb with drag handling
- `ProgressBar` — determinate bar + indeterminate sliding block animation
- `ListBox` — item rendering, keyboard/mouse selection, scroll, double-click activate
- `StatusBar` — background, border, zone hover, progress bar drawing
- `Minimap` — pixel buffer blit with viewport highlight rectangle
- `ColorSwatch` — checkerboard alpha pattern with color fill

**Code Editor Enhancements**

- Scrollbar drag (thumb vs. track click, live scroll during drag)
- Syntax highlighting with per-character color arrays (Zia, BASIC, plain-text modes,
  VS Code dark-theme inspired palette)
- Modal dialog system (`MessageBox.Info/Warning/Error/Question/Confirm`)
- 20+ advanced field functions (highlight spans, gutter icons, fold regions, extra cursors)

**macOS Live-Resize**

`vgfx_set_resize_callback()` — Cocoa `windowDidResize:` blocks the main thread during
live-resize; the new callback keeps the window painted by invoking the render function
directly from the resize handler.

#### HiDPI Canvas Scaling

Canvas-based demos (pacman, chess, paint, sidescroller) now display at full resolution on
Retina and HiDPI displays. Adds opt-in `coord_scale` to the vgfx window struct:

- Drawing primitives (`vgfx_pset`, `vgfx_line`, `vgfx_fill_rect`, `vgfx_circle`) scale
  coordinates by `coord_scale` automatically
- `vgfx_pset` fills a cs×cs physical-pixel block per logical pixel — no gaps
- Mouse positions and window size queries return logical coordinates
- Blit operations use nearest-neighbor upscaling
- Gradient and flood-fill functions scale to physical framebuffer space
- GUI widget layer is unaffected (coord_scale defaults to 1.0)

#### New Demos

| Demo | Language | Description |
|------|----------|-------------|
| `demos/zia/chess/` | Zia | Full chess engine: legal move generation (perft verified depth-4 = 197,281), iterative-deepening negamax alpha-beta with transposition tables, killer moves, quiescence search, PST-based evaluation, and canvas UI with piece rendering |
| `demos/zia/novarun/` | Zia | "Nova Run" 2D sidescroller platformer (~5,500 lines): two night-themed levels, boss fight, parallax backgrounds, procedural pixel art, HUD, menus |

---

### Compiler — Backend Improvements

#### x86-64 — 300+ Stress Tests and Bug Fixes

Five new stress test suites harden the x86-64 backend:

| Suite | Tests | Coverage |
|-------|-------|----------|
| `test_fp_stress` | 36 | FP arithmetic, conversions, NaN handling |
| `test_addr_stress` | 86 | Addressing modes, displacement, scale |
| `test_asm_encoding` | 70 | Instruction encoding correctness |
| `test_cf_stress` | 56 | Comparisons, branches, switch, loops, calls |
| `test_regalloc_stress` | 55 | Register pressure, spill/reload, XMM |

**Bug fixes:**

- **NaN-safe float comparisons**: `fcmp_eq`/`fcmp_ne`/`fcmp_lt`/`fcmp_le` now emit
  multi-instruction sequences (SETE+SETNP / SETNE+SETP / operand-swap) to correctly
  handle IEEE 754 NaN semantics
- **fptoui full range**: Supports the full [0, 2^64) unsigned range via two-path conversion
  (CVTTSD2SI for values < 2^63, subtract + BTC for values ≥ 2^63) with NaN/negative traps
- **sitofp/uitofp**: New signed/unsigned integer-to-float conversion instructions
- **Regalloc hardening**: XMM spill/reload, MOVSDrm addressing, pre-colored register hints

#### AArch64 — Register Allocation Bug Fix (BUG-NAT-007)

`AddFpImm` (compute fp + immediate → dest vreg) was misclassified as USE-only in
`RegAllocLinear::operandRoles`. Under register pressure, the dirty flag was never set, so
spill-victim selection left the frame slot uninitialized. Reload read garbage, producing
a junk alloca base address (SIGSEGV at 0x19 in the chess demo). The peephole optimizer's
`operandRoles` table already had the correct classification — the two tables were out of sync.

#### AArch64 & x86-64 — Missing Native Opcodes

Both backends gain implementations for previously-missing IL opcodes:

- **AArch64**: `SAlloca`, `GSlot`, `GLoad`, `GStore`, `GAddr`, `Phi`, `Trap`
- **x86-64**: `SAlloca`, `GSlot`, `GLoad`, `GStore`, `GAddr`, `Phi`, `Trap`, `PtrAdd`, `PtrDiff`

---

### Cross-Platform Hardening

#### Windows — Full Build Compatibility

A four-tier cross-platform audit resolves 22 findings:

**Tier 1 (Critical):**
- x86-64 Win64 stack argument offset now uses `target->shadowSpace` (+32 bytes) instead of
  hardcoded SysV formula
- AArch64 Windows ARM64 target added with PE/COFF assembly emission (no ELF `.type`/`.size`)

**Tier 2 (High):**
- `strcpy` → `memcpy` in HTTP client (3 buffer overflow vectors eliminated)
- `gethostbyname` → `getaddrinfo` in TLS (thread-safe, IPv6-compatible)
- VM interrupt handling: atomic `s_interruptRequested` flag with SIGINT handler (Unix) /
  `SetConsoleCtrlHandler` (Windows), checked post-dispatch for tight loop coverage
- Windows SEH `__try`/`__except` wraps the dispatch loop body to convert hardware exceptions
  (AV, divide-by-zero) into clean VM traps

**Tier 3 (Infrastructure):**
- `.gitattributes` enforces LF line endings (fixes 193 golden test CRLF/LF mismatches on Windows)
- ARM64 test gate changed from `NOT WIN32` to `CMAKE_SYSTEM_PROCESSOR` match (enables
  Windows ARM64 devices: Surface Pro X, Snapdragon X)
- `libm` changed from PUBLIC to PRIVATE linkage
- MSVC release linker: `/OPT:REF /OPT:ICF` (dead code elimination + identical COMDAT folding)
- `build_viper.sh` rewritten as cross-platform bash script with OS/compiler auto-detection

**Tier 4 (Polish):**
- Unified `ssize_t` as `long long` on Windows (eliminates 2 GB I/O ceiling)
- `-fvisibility=hidden` on POSIX (smaller export tables, better LTO)
- `clang-cl` added to compiler detection

#### MSVC Warning Elimination

All 55 MSVC compiler warnings resolved across 19 source files while maintaining full
GCC/Clang compatibility: C4210 (block-scope extern), C4267 (size_t narrowing), C4244
(safe narrowing casts), C4146 (unsigned negation), C4133 (LPCTSTR), C4189 (unused vars),
C4702 (unreachable code), C4334 (Huffman shift).

---

### Code Quality

#### Zero-Warning Build Policy

`-Wall -Wextra -Wpedantic` enabled on all 18 C++ library targets and the runtime C object
library. 365 files touched to fix: 15 unused functions, 11 unused variables, 15 shadowing
renames, 21 cast-qual warnings, 8 unreachable code blocks, 5 struct field initializers.
Build now produces zero warnings with `-Werror`.

#### Comprehensive Error Handling Audit

Silent failures and inconsistent termination eliminated across all layers:

- **VM**: `validateArgumentCount()` changed from bool → void, `RuntimeBridge::trap()` marked
  `[[noreturn]]`, unexpected `Value::Kind` in `VMContext::eval()` now traps
- **Runtime C layer**: `abort()` → `rt_trap()` in arrays/deque/result/option, ~30 NULL checks
  after `malloc`/`calloc`/`realloc`, `fwrite` return checks in PNG writer
- **Codegen**: Register exhaustion throws `std::runtime_error` instead of asserting
- **IL transforms**: Pass failures log warnings instead of silently continuing

#### Process Exit Cleanup

Deterministic shutdown sequence via `rt_global_shutdown()` atexit handler:

1. `rt_gc_run_all_finalizers()` — unconditional finalizer sweep
2. `rt_legacy_context_shutdown()` — BASIC file channels, args, registry
3. Thread pool worker join with finalizer enhancement

#### Collection Class-ID Tagging

Replaced fragile struct-size discrimination with stable `RT_SEQ_CLASS_ID` (2) and
`RT_MAP_CLASS_ID` (3) tags. Fixes `rt_json_format` misidentifying Seq as Map.

---

### Adversarial Testing — ViperSQL

Comprehensive adversarial testing of the ViperSQL showcase uncovered and fixed 15 bugs:

| Bug | Severity | Description |
|-----|----------|-------------|
| BUG-ADV-001 | P0 | String field loads via `rt_str_retain_maybe` to prevent use-after-free |
| BUG-ADV-001a | P0 | Centralized retain in `emitFieldLoad()` covering 8 additional paths |
| BUG-ADV-002 | P1 | Trailing token detection post-SELECT/INSERT/UPDATE/DELETE |
| BUG-ADV-003 | P1 | Unclosed string literal detection (TK_ERROR) |
| BUG-ADV-004 | P1 | Nonexistent column error via hasEvalError flag |
| BUG-ADV-005 | P1 | FK reference validation at CREATE TABLE |
| BUG-ADV-006 | P1 | ORDER BY nonexistent column pre-sort validation |
| BUG-ADV-007 | P1 | INSERT value count mismatch validation |
| BUG-ADV-008 | P1 | Ambiguous column detection in JOINs |
| BUG-ADV-009 | P1 | Scalar subquery row count validation |
| BUG-ADV-010 | P1 | Clear hasEvalError at executeSqlDispatch start |
| BUG-ADV-011 | P2 | Nested IN subquery flattening (multi-row results) |
| BUG-ADV-012 | P0 | BytecodeVM allocaBuffer reallocation invalidated pointers (SIGABRT) |
| BUG-ADV-013 | P1 | SHOW TABLES dispatch ("TABLES" lexed as TK_IDENTIFIER) |
| BUG-ADV-014 | P1 | SQL keywords usable as column names (token range 170–296) |
| BUG-ADV-015 | P1 | IL VM string register leak (regIsStr bitvector + frame release) |

Adversarial suite improved from 127 pass / 26 fail to **253 pass / 0 fail**.

---

### Bug Fixes

Over 170 individual bug fixes across all layers. Organized by subsystem:

#### Codegen (10 fixes)

- **BUG-NAT-007**: AArch64 `AddFpImm` operand misclassified as USE-only in regalloc — dirty
  flag never set, spill victim left frame slot uninitialized, reload read garbage → SIGSEGV
  at 0x19 in chess demo. Fixed: add `{false, idx==0}` case in `operandRoles`
- **x86-64 NaN float comparisons**: `fcmp_eq`/`fcmp_ne`/`fcmp_lt`/`fcmp_le` now emit correct
  multi-instruction sequences (SETE+SETNP / SETNE+SETP / operand-swap) for IEEE 754 NaN
- **x86-64 fptoui range**: Now handles full [0, 2^64) unsigned range via two-path conversion
  (CVTTSD2SI for values < 2^63, subtract + BTC for ≥ 2^63) with NaN/negative traps
- **AArch64 anyUseAfterCall**: Checked next-use instead of last-use for callee-saved decisions
- **AArch64 dead vreg spill**: Skip spilling dead vregs before calls in `handleCall`
- **AArch64 callee-saved recalculation**: Post-peephole pass removes unused saves
- **MIR result-to-return fold**: ALU result → x0 → ret peephole pattern
- **MIR dead code elimination**: New peephole DCE pass
- **x86-64 XMM regalloc**: XMM spill/reload support, MOVSDrm addressing, pre-colored hints
- **Register exhaustion**: Replaced assert with `std::runtime_error` in AArch64 RegAllocLinear

#### Zia Compiler (16 fixes)

- **BUG-ADV-001**: String field loads now emit `rt_str_retain_maybe` in `lowerField()` to
  convert borrowed refs to owned (prevents use-after-free)
- **BUG-ADV-001a**: Centralized retain in `emitFieldLoad()` covering 8 additional code paths
  (self-field, boxing, copying, pattern matching)
- **String concat double-free**: `rt_str_concat` consumes operands, but Zia deferred release
  also released them — fixed with `consumeDeferred()` post-concat
- **Entity field store**: Missing retain/release for string fields — added retain-new and
  release-old logic in `emitFieldStore()`
- **List.Length/Length property**: `lowerField()` only handled Count/count/size/length — added
  Len and Length variants
- **List.Pop() missing case**: Dispatch table had `pop→Pop` but `lowerListMethodCall` had
  no implementation — added case calling `kListPop`
- **Entity method currentReturnType_**: `lowerMethodDecl` computed returnType but never stored
  it in `currentReturnType_`, silently bypassing all return-type coercions for entity methods
- **List[Boolean] boxing**: `emitBox` I1 case now inserts Zext1 (was emitting wrong IL type)
- **Canvas.KeyHeld signature**: RT_FUNC fixed from `i1(obj,i64)` to `i64(obj,i64)`
- **Canvas boolean returns**: `IsMaximized`/`IsMinimized`/`IsFocused` return type corrected
- **GAP-1/2**: Definite-assignment analysis — flow-sensitive if/else branch tracking
- **GAP-3**: As-cast compatibility checking via `isConvertibleTo()` + entity/Ptr rules
- **GAP-4/5**: Optional field/method access without null check now emits warning
- **GAP-6**: Missing field error for entity/value/primitive types
- **GAP-7**: `validateCallArgs()` for arity and type checking on user-defined calls
- **GAP-8**: Optional-to-non-Optional parameter mismatch caught by argument validation

#### VM & BytecodeVM (8 fixes)

- **BUG-ADV-012**: BytecodeVM `allocaBuffer_` reallocation invalidated alloca pointers held
  in registers/stack — pre-reserve 16MB capacity to prevent resize (P0 SIGABRT crash)
- **BUG-ADV-015**: IL VM string register leak — `storeResult` retained strings into registers
  but `releaseFrameBuffers` never released them. Added `regIsStr` bitvector with release on
  frame exit
- **validateArgumentCount()**: Changed from bool → void — no caller was checking the return
- **RuntimeBridge::trap()**: Marked `[[noreturn]]`, removed dead code after trap calls
- **VMContext::eval()**: Unexpected `Value::Kind` now traps instead of returning silent zero
- **VIPER_TRAP_ASSERT**: Now checks conditions in release builds (was `((void)0)`)
- **VM interrupt handling**: Added atomic `s_interruptRequested` flag with SIGINT handler
  (Unix) / `SetConsoleCtrlHandler` (Windows), checked post-dispatch for tight loop coverage
- **Windows SEH**: `__try`/`__except` wraps dispatch loop body to convert hardware exceptions
  (access violations, divide-by-zero) into clean VM traps

#### Network & TLS (28 fixes)

- **CS-4** (Critical): WebSocket masking key now uses `rt_crypto_random_bytes` instead of
  weak `rand()`
- **CS-5** (Critical): Validate `Sec-WebSocket-Accept` header with inline SHA-1 + base64
- **H-1/H-2**: Free recv buffer on timeout/close paths (TCP memory leak)
- **H-3**: Apply `HTTP_MAX_BODY_SIZE` guard to `read_body_until_close_conn`
- **H-4**: Cap `read_line_conn` buffer at 64 KB to prevent unbounded realloc
- **H-5**: Limit header count to 256 with drain loop for excess headers
- **H-8**: TLS sequence number overflow check; close connection on wrap (RFC 8446 §5.5)
- **H-9**: Constant-time Finished MAC comparison (`ct_memcmp`)
- **H-10**: Transcript buffer overflow aborts handshake instead of silently truncating
- **H-11**: WebSocket FIN-bit fragmentation reassembly in `recv` and `recv_bytes`
- **H-13**: `set_nonblocking` now returns bool; errors checked at both call sites
- **M-5**: Guard negative `atoll` Content-Length before casting to `size_t`
- **M-6**: Overflow-safe hex chunk size parser (pre-multiply range check)
- **M-9**: Reject server-masked WebSocket frames with protocol error (RFC 6455 §5.1)
- **M-13**: Replace recursive `rt_tls_recv` retry with goto to prevent stack overflow
- **M-13a**: All HTTP convenience methods use `strdup` for method string
- **RC-1**: REST client finalizer registered via `rt_obj_set_finalizer` (memory leak)
- **RC-2**: Release previous `last_response` before overwriting in `execute_request`
- **RC-5/RC-6**: Check `delay >= max` and `INT64_MAX/2` before multiplying (overflow guard)
- **RC-7**: Add ±25% jitter to exponential backoff to prevent thundering herd
- **RC-9**: Clamp rate limiter tokens to 0 after subtraction (float precision underflow)
- **RC-10**: Fall back to `GetTickCount64` if `QueryPerformanceFrequency` fails
- **RC-11**: `rt_http_post` copies body with `malloc`+`memcpy` (not raw pointer alias)
- **RC-12**: Reject unrecognized URL schemes (ftp://, etc.) in `parse_url`
- **RC-13**: Drain all chunked trailer headers until empty line (RFC 7230)
- **RC-14**: Remove partial file on fwrite failure in `rt_http_download`
- **strcpy → memcpy**: 3 buffer overflow vectors eliminated in `rt_network_http.c`
- **gethostbyname → getaddrinfo**: Thread-safe, IPv6-compatible hostname resolution in TLS

#### GUI & Graphics (30+ fixes)

- **Mouse wheel routing**: `VG_EVENT_MOUSE_WHEEL` missing from hit-test routing — fell
  through to root widget instead of reaching CodeEditor under cursor
- **Dialog rendering**: `dialog_paint` was a complete stub (all draws were `(void)dlg->bg_color`
  no-ops) — implemented background, title bar, separator, border, buttons
- **Dialog font**: `rt_messagebox_info/warning/error` created dialogs with no font set —
  added `rt_gui_ensure_default_font()` + `vg_dialog_set_font()` calls
- **Dialog overlay**: `0x80000000` overlay fill was solid black in `0x00RRGGBB` format, not
  semi-transparent — removed overlay (vgfx has no alpha blending)
- **macOS live-resize**: Cocoa `windowDidResize:` blocks main thread during resize, causing
  black window — added `vgfx_set_resize_callback()` for render-during-resize
- **ListBox virtual mode crash**: `visible_cache` never allocated in virtual mode
- **Cache bitmap UAF**: `vg_cache` NULL bitmap on malloc failure → use-after-free
- **Linux XImage depth**: 24→32 fix for alpha channel corruption
- **ProgressBar text color**: Fully-transparent `0x00FFFFFF` → opaque `0xFFFFFFFF`
- **ScrollView divide-by-zero**: Division by zero in scrollbar paint
- **Label word-wrap overflow**: Fixed-size stack buffers → heap allocation
- **Widget self-loop**: `vg_widget_add_child` self-loop guard added
- **World transform recursion**: Recursive world-transform update → iterative
- **Syntax highlight bounds**: Color array bounds-check missing in CodeEditor
- **Char input range**: ASCII-only → full Unicode
- **UTF-8 decoder**: Infinite loop on invalid continuation bytes
- **Font kern pairs**: Unsorted pairs → binary search incorrect
- **TTF cmap parse**: Partial malloc chain on cmap4/cmap12 parse failure
- **Event bubbling recursion**: Converted to iterative loop
- **ScrollView bounds**: `get_screen_bounds` didn't subtract scroll offset
- **Tab-order sort**: O(n²) insertion sort → merge sort
- **Word-wrap cache**: Redundant greedy-algorithm runs on every paint
- **TabBar buffer overflow**: `char[256]` modified-title buffer → malloc
- **TTF table directory**: Bounds validation missing before reading
- **HiDPI layout**: `vg_widget_set_fixed_size` pinned root at creation-time size, prevented
  fill/resize in ViperIDE
- **HiDPI scale propagation**: Added `float ui_scale` to `vg_theme_t`; propagates to
  typography, spacing, and all bar heights for Retina consistency
- **Toolbar text centering**: Font size not multiplied by `ui_scale` for vertical alignment
- **Font zoom line overlap**: `rt_codeeditor_set_font_size` didn't update `line_height`
- **HiDPI canvas scaling**: All Canvas demos displayed at 1/4 size on Retina — added opt-in
  `coord_scale` to vgfx window struct with auto-scaling in drawing primitives

#### Runtime (20+ fixes)

- **Pixels.FlipH/FlipV**: Silently returned new object instead of mutating in place — caused
  discarded return values where sprites were never actually flipped
- **rt_physics2d broad-phase**: Static grid variables → stack-allocated (thread-safety)
- **rt_quadtree edge case**: `query_was_truncated()` didn't set flag for 256th item
- **rt_quadtree duplicate ID**: Missing guard allowed duplicate entries
- **RT_STATE_MAX overflow**: Increased from 32→256 with `rt_trap` on overflow
- **RT_BUTTONGROUP_MAX overflow**: Increased from 64→256 with `rt_trap` on overflow
- **Collision mask**: Default `0x7FFFFFFF` → `0xFFFFFFFF` (sign bit matters)
- **rt_screenfx**: Quadratic decay model when decay ≥ 1500; per-instance `rand_state` for
  thread-safety
- **rt_objpool**: O(n) `FirstActive`/`NextActive` → O(1) via intrusive active list
- **malloc NULL checks**: Added ~30 NULL checks after `malloc`/`calloc`/`realloc` in bigint,
  gc, string ops, msgbus, sortedset, defaultmap, trie, frozenset, frozenmap, compress, path,
  tempfile, yaml, diff, jsonpath, playlist
- **PNG writer**: Check all `fwrite` returns — return 0 on I/O failure instead of producing
  corrupt output
- **abort() → rt_trap()**: Replaced in `rt_array.c`, `rt_deque.c`, `rt_result.c`,
  `rt_option.c` for consistent error handling
- **Collection class-ID tagging**: Replaced fragile struct-size discrimination with stable
  `RT_SEQ_CLASS_ID` (2) and `RT_MAP_CLASS_ID` (3) tags — fixes `rt_json_format`
  misidentifying Seq as Map
- **Process exit cleanup**: `rt_global_shutdown()` atexit handler runs all GC finalizers,
  closes BASIC file channels, joins thread pool workers
- **Windows DNS**: `gethostname`/`getaddrinfo` → `GetAdaptersAddresses` for `rt_dns_local_addrs()`
- **Windows canvas resize**: WM_SIZE handler was updating framebuffer dims, causing heap
  overflow — fixed to skip dimension update
- **Windows lseek**: 32-bit `_lseek` → `_lseeki64` for >2GB file support
- **Windows O_BINARY**: Added to `rt_file_read_bytes`/`write_bytes` for binary mode
- **Tarjan SCC**: Use-after-free from dangling reference after `vector::pop_back`
- **Winsock cleanup**: `WSACleanup` registered via `atexit()` after `WSAStartup`

#### Windows/MSVC Compatibility (17 fixes)

- **wincrypt.h include order**: `windows.h` must precede wincrypt.h in rt_tempfile, rt_crypto,
  rt_guid
- **MSVC atomic intrinsics**: New `rt_atomic_compat.h` maps GCC `__atomic` builtins to MSVC
  `_Interlocked*` intrinsics
- **pthread → CRITICAL_SECTION**: `rt_string_intern.c` threading on Windows
- **Missing headers**: Added `<functional>` (MemorySSA.cpp), `<algorithm>` (test_callgraph_scc)
- **Deprecated u8path()**: `std::filesystem::u8path()` → current API in RunProcess.cpp
- **Compiler builtins**: `__builtin_popcountll` → `__popcnt64`,
  `__builtin_unreachable` → `__assume(0)`
- **POSIX string**: `strncasecmp` → `_strnicmp` on Windows
- **Forward declarations**: Struct/class forward declaration mismatches (C4099)
- **extern "C" throw**: Removed throw from extern "C" function in ThreadsRuntime.cpp
- **NOMINMAX**: Added before `windows.h` in VM.cpp
- **SEH separation**: Separated `__try`/`__except` from C++ unwinding in dispatch
- **55 MSVC warnings**: All resolved (C4210, C4267, C4244, C4146, C4133, C4189, C4702, C4334)
- **Test dialog suppression**: `WinDialogSuppress.c` CRT initializer + `_CrtSetReportMode`
  enhancements + `VIPER_DISABLE_ABORT_DIALOG()` macro + CMake injection into all test targets
- **Test ABI check**: `stem()` not `filename()` for "lib" prefix in test_cross_platform_abi
- **Test stack size**: 8MB stack for depth limit tests on Windows
- **Test pointer deref**: `blocks` → `*blocks` in test_vm_switch_block_label
- **Unified ssize_t**: `long long` on both 32-bit and 64-bit Windows (eliminates 2GB I/O
  ceiling)

---

### Documentation

- `docs/release_notes/Viper_Release_Notes_0_2_1.md` — v0.2.1 release notes (870 lines)
- `docs/memory-management.md` — Shutdown cleanup and GC lifecycle (627 lines)
- `docs/interop.md` — IL module linker and cross-language interop (285 lines)
- `docs/bugs/cross_platform_issues.md` — 22 cross-platform audit findings
- `docs/bugs/chess_bugs.md` — Chess demo bug log
- `docs/feature-parity.md` — Zia/BASIC audit matrix with source references
- `docs/zia-reference.md` — Updated syntax, grammar, reserved words
- Updated viperlib docs: BigInt, Mat3, Mat4, Xml, Yaml, Diagnostics, Parse, GC, Channel, Pool, Aes

---

### Project Statistics

| Metric              | v0.2.1    | v0.2.2    | Change     |
|---------------------|-----------|-----------|------------|
| Total Lines (LOC)   | ~900,000  | ~1,000,000 | +~100,000  |
| C/C++ Source Files  | 1,542     | 2,288     | +746       |
| Runtime Classes     | 197       | 226       | +29        |
| Demo Source Files   | 664       | 698       | +34        |
| Test Count          | 1,196     | 1,261     | +65        |
| Commits             | —         | 45        | —          |
| Files Changed       | —         | 1,108     | —          |

---

### Breaking Changes

1. **Collection method renames** (old names retained as aliases):

   | Old | New |
   |-----|-----|
   | `List.Flip()` | `List.Reverse()` |
   | `Set.Merge()` | `Set.Union()` |
   | `Set.Common()` | `Set.Intersect()` |
   | `TreeMap.Drop()` | `TreeMap.Remove()` |

   Old names continue to work via RT_ALIAS backward compatibility.

---

### Architecture

```text
┌──────────────┐  ┌──────────────┐
│ BASIC Source │  │  Zia Source  │
│    (.bas)    │  │    (.zia)    │
└──────┬───────┘  └──────┬───────┘
       │                 │
       ▼                 ▼
┌─────────────────────────────────────────────────────┐
│                     Viper IL                        │
│      SimplifyCFG → SCCP → EarlyCSE → Mem2Reg       │
│         → DSE (MemorySSA) → CheckOpt → Inliner      │
│                                                     │
│             ModuleLinker (NEW)                       │
│         Multi-module symbol resolution              │
└─────────────────────┬───────────────────────────────┘
                      │
      ┌───────────────┼───────────────┐
      ▼               ▼               ▼
┌──────────┐    ┌──────────┐    ┌──────────┐
│  IL VM   │    │  x86-64  │    │ AArch64  │
│ Bytecode │    │  Native  │    │  Native  │
│    VM    │    └──────────┘    └──────────┘
└──────────┘
                      │
                      ▼
              ┌──────────────┐
              │ viper package│  (NEW)
              │ .app .deb    │
              │ .exe .tar.gz │
              └──────────────┘
```

---

### Feature Comparison

| Feature                   | v0.2.1               | v0.2.2                           |
|---------------------------|----------------------|----------------------------------|
| Runtime Classes           | 197                  | 226 (+29)                        |
| Zia Properties            | No                   | get/set properties               |
| Zia Destructors           | No                   | deinit blocks                    |
| Zia try/catch/finally     | No                   | Full exception handling          |
| Zia Compound Assignment   | No                   | +=, -=, *=, /=, %=              |
| Zia Struct Literals       | No                   | TypeName { field = expr }        |
| Zia Fixed-Size Arrays     | No                   | T[N] fields                      |
| Zia If-Expressions        | No                   | var x = if cond { a } else { b } |
| Zia Warnings              | No                   | 18 codes, -Wall/-Werror          |
| Zia Type Soundness        | Partial              | 8 gaps fixed, full audit         |
| Zia Feature Parity        | ~80%                 | 100% vs BASIC                   |
| Application Packaging     | No                   | viper package (.app/.deb/.exe)   |
| IL Module Linker          | No                   | Multi-module, interop thunks     |
| HiDPI Canvas              | 1/4 size on Retina   | Full resolution, all platforms   |
| GUI Window Management     | Basic                | 12 new APIs, 3 platforms         |
| GUI Widgets               | Stubs                | Slider, ProgressBar, ListBox     |
| x86-64 Test Coverage      | Baseline             | +300 stress tests                |
| NaN Float Comparisons     | Incorrect            | IEEE 754 compliant               |
| Zero-Warning Build        | No                   | -Wall -Wextra -Wpedantic         |
| MSVC Warnings             | 55                   | 0                                |
| Adversarial Test Suite    | —                    | 253 pass / 0 fail                |

---

### v0.2.x Roadmap

Remaining v0.2.x focus areas:

- Zia debugger integration with breakpoints and watch expressions
- Native code generation coverage expansion
- GUI library maturation (accessibility, additional widget types)
- Runtime API stability and performance improvements
- BASIC frontend OOP interop with Zia modules

---

*Viper Compiler Platform v0.2.2 (Pre-Alpha)*
*Released: March 2026*
*Note: This is an early development release. Future milestones will define supported releases when appropriate.*
