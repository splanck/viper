# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.5 was cut on 2026-05-07. -->

### What this release is about

A focused hardening-and-correctness cycle. Every change closes an attack surface, eliminates a latent correctness bug, or tightens a lifetime, locking, or type invariant.

- **Memory / GC overhaul** — validated `Viper.Memory.Retain` / `Release`, lock-free GC traversal, trap-safe finalizer phase, resurrection-aware refcounting.
- **MessageBus end-to-end** — class-ID validation, managed callback objects, NUL-safe topic hashing, internal locking, GC traversal registration.
- **IO security hardening (two rounds)** — random-nonce temp files, `openat`-based recursive removal, `O_CLOEXEC` / `O_NOFOLLOW` everywhere, SaveData absolute-path resolution.
- **Crypto subsystem upgrade** — `Viper.Text.*` → `Viper.Crypto.*` migration, scrypt-SHA256 KDF (RFC 7914), AES-GCM with AAD, constant-time hash compare.
- **TLS X.509 verifier hardening** — Key Usage / Basic Constraints / Extended Key Usage extensions enforced, DNS-name validation tightened.
- **Archive / Compress / VPA correctness** — ZIP64 rejection, local-vs-central header cross-check, exact GZIP boundary detection, 64-bit VPA seeks.
- **2D graphics correctness pass** — saturating int64 clip math, validated polyline/polygon arrays, sprite tint alpha preservation, tilemap I/O ownership discipline.
- **`Viper.Threads.*` correctness pass** — repeatable joins, class-ID validation, safer pool shutdown/finalization, preserved task trap messages, listener trap isolation, and unbuffered channel rendezvous fixes.
- **`Viper.Audio.*` compat surface rebuilt** from `RT_ALIAS` aliases to full typed registrations.
- **ViperIDE polish** — IntelliSense linkage fix, real keyword highlighting, scrollBeyondLastLine, BMP toolbar glyphs.
- **AArch64 register-allocator correctness** — protection set covers def operands, fixing register-reuse clobbers.
- **Windows x64 stabilization** — out-of-order block-param lowering, RSP/RBP scheduling boundaries, lone-JCC trace fix, parallel-copy spill scratch; D3D11 bone-palette sized to the shared 256-entry shader budget.
- **MSVC build hardening** — embedded debug info via CMP0141, expanded import policy for UCRT/Win32 symbols, WSL-aware audit and smoke scripts.

### By the Numbers

| Metric | v0.2.5 | v0.2.6 | Delta |
|---|---|---|---|
| Commits | — | 17 | +17 |
| Source files | 2,996 | 3,005 | +9 |
| Production SLOC | 552K | 560K | +8K |
| Test SLOC | 228K | 232K | +4K |
| Demo SLOC | 188K | 188K | 0 |

Counts via `scripts/count_sloc.sh`.

---

### Core runtime

**Viper.Memory**

- `Retain` / `Release` re-wired to validated wrappers (`rt_memory_retain` / `rt_memory_release`) that authenticate live runtime handles.
- New string-typed variants `RetainStr` / `ReleaseStr` callable from Zia without a `void *` cast.
- `Release` now releases `RT_HEAP_ARRAY` element refs (`RT_ELEM_STR`, `RT_ELEM_NONE`, `RT_ELEM_BOX`) before freeing storage — previously leaked.
- `rt_obj_new_i64` traps on negative `byte_size` and `byte_size > SIZE_MAX`.
- Public heap helpers validate payloads against the live registry; `rt_obj_free` rejects non-zero refcounts.
- Resurrection-aware: `rt_obj_resurrect` uses CAS, traps if refcount ≠ 0; `Release` reports the post-finalizer refcount.

**GC cycle collector**

- Phases 2 and 3 drop the GC lock during traversal callbacks; edges collected into a heap-allocated `gc_edge_list`, lock acquired only to apply colour updates.
- Phase 3 restore uses iterative worklist BFS instead of recursive in-lock callbacks.
- Phase 4 wrapped in `setjmp` / `rt_trap_set_recovery` so a trapping finalizer cleans up garbage and snapshot arrays before re-raising.
- Snapshot objects retained via `rt_obj_retain_maybe` before lock release — prevents UAF if another thread drops the last external ref.
- Promoted-root restoration during non-full passes preserves young objects reachable from long-lived roots.
- Weak refs cleared only after finalizers decline resurrection; OOM paths clean up all heap allocations and snapshot refs.
- `TotalCollected` saturates at `INT64_MAX`.

**MessageBus**

- `RT_MSGBUS_CLASS_ID` and `RT_MSGBUS_CALLBACK_CLASS_ID` constants; `mb_require()` validates every public API entry.
- Callbacks must be heap-managed objects from `rt_msgbus_callback_new()`; raw function pointers and unrelated heap objects now trap.
- Topic hashing uses full byte length via `mb_hash_bytes` / `memcmp` — embedded NULs preserved across subscribe / publish / clear.
- `mb_traverse()` registers retained callback objects with the GC tracker.
- Internal lock serializes public operations; `Topics()` returns an owning `Seq`; `Publish` releases its retained snapshot before re-raising a trapping handler.
- Subscription-ID overflow guard traps before the counter wraps.

**Box / Heap / Type registry**

- Boxed values carry `RT_BOX_CLASS_ID`; `Object.Equals` / `GetHashCode` dispatch correctly through `Set[Box]` and `Map[Box, ...]`.
- `rt_box_hash` normalizes `-0.0` to `+0.0` so equal values hash equally.
- New `rt_box_i1_bool(int8_t)` ABI-matching variant; Zia lowerer no longer zero-extends `i1` to `i64`.
- `Box.ValueType` is now a class-tagged managed type — boxed structs retain object/string fields, participate in GC traversal, and release fields on finalization. `Box.ValueType(0)` is legal.
- `rt_heap_realloc` holds the registry lock as one critical section across check / `realloc` / move.
- `rt_heap_mark_disposed` return value corrected (was inverted).
- `rt_heap_try_get_header` closes a TOCTOU window on the magic check.
- `rt_type_registry` refactored to a `const char*`-error-return pattern; capacity growth no longer traps under the registry write lock.

**Parse / Diagnostics / Object**

- `Parse.DoubleOption` / `Int64Option` (with `Viper.Parse.*` aliases) — Option-returning helpers for graceful parse failure.
- `rt_diag_assert_eq_str` uses `rt_str_eq` (NUL-safe) with byte-aware C-escape message formatting.
- `Object.RefEquals` exposed through the class method index.
- `rt_to_double` switched from `rt_val_to_double` (partial-parse tolerant) to strict `rt_parse_double` with overflow checks.

### Crypto runtime

**Namespace migration**

- The crypto classes formerly under `Viper.Text.*` (Password, KeyDerive, Aes, Cipher, Hash, Rand) are now canonical members of `Viper.Crypto.*`.
- The old `Viper.Text.*` names continue to resolve through the runtime alias mechanism — no source changes required.
- `runtime.def` registrations, viperlib docs, demos, and file headers all updated to the new names.

**Memory-hard password hashing (scrypt)**

- New `KeyDerive.ScryptSHA256(password, salt, N, r, p, dklen)` and `ScryptSHA256Str` — RFC 7914 with bounded `N * r * 128` memory cap; out-of-range parameters rejected, not silently clamped.
- `Password.Hash()` defaults to scrypt and emits `SCRYPT$<log2N>$<r>$<p>$<salt_b64>$<hash_b64>`.
- `Password.Verify()` still accepts the legacy `PBKDF2$<iters>$<salt_b64>$<hash_b64>` format — old hashes verify without migration.
- PBKDF2 wrappers reject below-minimum iteration counts (was silently raised).

**Authenticated encryption (AES-GCM)**

- New `Aes.EncryptAuth(key, plaintext, aad)` and `DecryptAuth(key, ciphertext, aad)`.
- `Aes.EncryptAuth` / `DecryptAuth` now accept AES-256-GCM keys in addition to AES-128-GCM keys.
- 16-byte magic header (`'V', 'A', 'K', '1'` + version) prevents plain-AES and authenticated-AES ciphertexts from being confused.
- The header is composed with the application-supplied AAD so the GCM tag covers both.
- `Cipher` exposes parallel AAD overloads: `EncryptAAD`, `DecryptAAD`, `EncryptWithKeyAAD`, `DecryptWithKeyAAD`.

**Validation-ready backend groundwork**

- Added `Viper.Crypto.Module` with approved-mode controls, module self-tests, and fail-closed policy gates for non-approved public services.
- Added AES-256-GCM, HMAC-SHA384, HMAC-SHA512, HKDF-SHA384, and native P-256 ECDH primitives.
- Approved mode routes `Cipher` through AES-256-GCM formats (`VCA1` / `VKA1`), routes `Password.Hash` through PBKDF2, and uses an in-tree HMAC-DRBG for runtime crypto randomness.
- Current TLS fails closed in approved mode until the P-256/P-384 TLS key-share profile is wired and validated.

**Constant-time comparison**

- `Hash.ConstantTimeEquals(str, str)` and `ConstantTimeEqualsBytes(bytes, bytes)` — branch-free equality for MAC tags, session IDs, and other secret-bearing strings.
- `Password.Verify()` uses these internally.

### TLS

- Key Usage (OID 2.5.29.15), Basic Constraints (2.5.29.19), and Extended Key Usage (2.5.29.37) extensions parsed and enforced during chain validation.
- Intermediate certificates without the cA flag in Basic Constraints are rejected.
- Leaf certificates without TLS Web Server Authentication EKU are rejected.
- Key Usage bits checked against the role each certificate plays in the chain.
- `tls_dns_name_bytes_valid` rejects empty names, embedded-NUL or non-ASCII bytes, names > 255 bytes, labels > 63 bytes, and improper wildcard placement.
- `rt_crypto.c` and `rt_tls.c` thread the new constant-time helpers and per-namespace class IDs through the handshake state.

### IO runtime

**Temp files and asset loading**

- All temp-file creation paths now use a 64-bit random nonce (`/dev/urandom` on POSIX, `rand_s` + `QueryPerformanceCounter` on Windows) — eliminates the ASLR side-channel from the previous pointer-encoded scheme.
- Asset decoding creates a private directory (`mkdtemp` on POSIX, random subdirectory on Windows), cleaned up on success and failure.
- `asset_name_is_safe()` rejects absolute paths, drive letters, colons, empty segments, and dot/dotdot components.
- Asset opens use `open(O_NOFOLLOW | O_CLOEXEC) + fdopen`.

**Recursive directory removal**

- Rebuilt around `openat` / `fstatat` / `unlinkat` / `fdopendir` with `AT_SYMLINK_NOFOLLOW` — a symlink substituted mid-traversal cannot redirect deletion.

**File descriptor hygiene**

- `O_CLOEXEC` at every open, `FD_CLOEXEC` fallback via `fcntl`; Windows paths add `_O_NOINHERIT`.
- Channel validity tightened: `channel <= 0` rejected (channel 0 was never valid).
- `rt_file_close` saves the fd locally before clearing — closes a UAF race.
- `Glob` rejects embedded NULs; recursion capped at 4096.
- `MemStream.WriteI8` validates `[-128, 127]`.
- Watcher emits `RT_WATCH_EVENT_OVERFLOW` on buffer overflow rather than silently dropping; `IN_CLOEXEC` / `FD_CLOEXEC` on inotify and kqueue fds.
- New `rt_file_stdio.h` consolidates the UTF-8-aware `fopen` wrapper duplicated across four files.

**SaveData**

- Paths resolved to absolute before first use (`GetFullPathNameW` / `getcwd`-with-doubling) — eliminates chdir-relative confusion.
- Windows opens use `_O_NOINHERIT | _O_BINARY`.
- Malformed entries are silently skipped — games are now resilient to partial writes and filesystem corruption.

**Archive / Compress / VPA**

- ZIP extra-field parser rejects ZIP64 extension headers; central-directory rejects `version_needed >= 45`, encryption flags, ZIP64 sentinels.
- Local-file headers cross-checked against central-directory values for method, flags, CRC, and both size fields.
- `inflate_data_limited_ex()` exposes `consumed_bytes` for exact compressed-input accounting; multi-member GZIP uses this for precise boundary detection.
- VPA seeks use 64-bit `_fseeki64` / `fseeko`; opens perform `lstat` pre-check then `O_NOFOLLOW`.

### Collections runtime

- Eight new unsigned `BinaryBuffer` methods: `WriteU16LE/BE`, `WriteU32LE/BE`, `ReadU16LE/BE`, `ReadU32LE/BE` (with `[0, UINT16_MAX]` / `[0, UINT32_MAX]` range validation).
- Retroactive range-check guards on `WriteI16LE/BE` and `WriteI32LE/BE` — previously truncated silently.

### Threading runtime

- Thread joins are now repeatable after completion: the first successful join reclaims the OS handle, and later `Join`, `TryJoin`, or `JoinFor` calls on the same runtime handle return success instead of trapping as already joined.
- Thread pool shutdown steals worker handles under the pool monitor and joins outside the lock, making repeated shutdown calls idempotent and preventing double-join/double-release races. Pool task trap messages are sticky so later waits still surface the failure.
- `Thread.Start` / `StartSafe` VM and BytecodeVM dispatchers now borrow their argument consistently with native threads; owned lifetime remains the job of `StartOwned` / `StartSafeOwned`. Safe VM/BytecodeVM workers propagate trap text into the safe-thread boundary rather than aborting.
- Concurrent queue, map, scheduler, debouncer, throttler, and pool objects now carry distinct runtime class IDs and validate public API handles before downcasting.
- `ConcurrentMap.Keys()` / `Values()` snapshot under the map lock and retain copied values; `ConcurrentQueue.Clear()` and finalization detach nodes before releasing retained values.
- `Channel.TrySend()` on synchronous channels now completes the rendezvous acknowledgement before reporting success; `TryRecv()` remains strictly non-blocking.
- `Parallel.*Pool` detects calls from a worker already running in the target pool and runs nested work inline to avoid self-deadlock. Worker callback traps preserve the original trap text.
- Future completion listeners and listener-cancel hooks isolate trapping callbacks so one bad listener cannot rethrow through promise completion or cancel cleanup.

### Audio

- The `Viper.Audio.*` compatibility namespace was rebuilt from `RT_ALIAS` forwarding entries into full `RT_CLASS_BEGIN` / `RT_FUNC` / `RT_METHOD` / `RT_PROP` registrations.
- `RT_ALIAS` cannot carry typed object return values — `obj<Viper.Audio.Sound>` returns silently failed type resolution at every call site.
- All eight classes promoted: `Audio`, `Sound`, `Voice`, `Music`, `Playlist`, `SoundBank`, `Synth`, `MusicGen`.
- `rt_audio3d_register_voice` added to `RuntimeSurfacePolicy.inc`.

### 2D graphics

**Drawing primitives**

- New saturating int64 clip-rect arithmetic via `rtg_add_sat64` / `rtg_i64_fits_i32` helpers — extreme coordinates clip safely instead of overflowing.
- Polyline / Polygon / Path point arrays validated through `rt_heap_try_get_header` — NULL pointers, non-Integer arrays, and arrays shorter than `count * 2` elements are now silently ignored rather than read past their bounds.
- `INT64_MAX / 2` cap on `count` prevents the `count * 2` multiply from overflowing.

**Sprite, tilemap, color**

- Sprite tint normalization preserves the explicit-alpha tag from `Color.RGBA` — was masked to 24 bits, forcing every tinted sprite back to fully-opaque.
- Tilemap I/O introduces `tilemap_io_release_ref` plus `map_set_owned` / `seq_push_owned` so reload cycles can't double-free shared tile references.
- `Color.FromHex` rejects strings with embedded NUL bytes — was returning a partial color.
- Redundant per-subsystem class-ID constants in `rt_graphics2d.c` removed; canonical IDs come from the runtime registry.

### ViperIDE

**IntelliSense linkage fix**

- The `zia` binary's CMake target now force-loads `fe_zia` (`LINKER:-force_load` on macOS, `--whole-archive` on Linux).
- Without force-loading, the static linker satisfied every `rt_zia_*` reference from the weak stubs in `viper_runtime/core/rt_zia_completion_stub.c`, so completion / hover / diagnostics / symbols silently returned `"Zia completion unavailable"`.

**Syntax highlighting**

- New C-callable bridge `rt_zia_highlight.cpp` in `fe_zia` exposes `rt_zia_is_keyword(name, len)`.
- The GUI tokenizer in `rt_gui_codeeditor.c` now consults `Lexer::lookupKeyword` (52 keywords) instead of the local 28-entry `zia_keywords[]` table that had been silently drifting.
- Inline `/* ... */` block-comment tracking via a per-editor `zia_block_comment_depth` field.
- Uppercase-first heuristic colors `Foo` / `MyClass` / `Math` / `Viper` as types (teal).
- Identifiers immediately followed by `(` are colored as function calls (yellow `#DCDCAA`).

**Editor UX**

- `codeeditor_max_scroll_y` uses half-viewport scrollBeyondLastLine padding (matches VS Code / Sublime) — last line floats to mid-viewport rather than being pinned to the bottom edge.
- Mouse-wheel scroll dropped from 1.0 to 0.3 lines per delta — more comfortable on macOS trackpads.

**Toolbar and welcome buffer**

- Toolbar uses BMP-only Geometric Shapes / Arrows glyphs as button text labels — the previous emoji attempt fell back to the missing-glyph "hamburger" on UI fonts that lack color emoji support.
- New `tbBuild` / `tbRun` / `tbFind` items wired into the command dispatch.
- Untitled (Ctrl+N) buffers and the welcome buffer now begin with `module UntitledN;` / `module Welcome;` so the completion engine can parse them on the first keystroke; without a module declaration `CompleteForFile` returned an empty string and the popup appeared blank.

### Compiler, IL & codegen

**AArch64 register allocator**

- Protection set in `Allocator.cpp` now covers both *use* and *def* operands — prevents eviction of live def vregs mid-instruction.
- Fields renamed `protectedUse{GPR,FPR}_` → `protectedOperand{GPR,FPR}_` to reflect the broader scope.
- Fixes a class of register-reuse clobber bugs.

**x86-64 codegen — out-of-order block lowering**

- The lowerer now pre-registers block parameters and instruction results before lowering textually out-of-order blocks, so optimized IL can reference dominated values without tripping unknown-SSA diagnostics.
- Legalize creates typed virtual registers for edge-copy arguments whose defining block has not been lowered yet — fixes out-of-order block-parameter copies in optimized BASIC demos.

**x86-64 codegen — Win64 prologue and branch layout**

- The post-RA scheduler treats physical RSP / RBP definitions as scheduling boundaries, preserving Win64 frame setup and unwind-sensitive prologue ordering before callee-save spills.
- Branch layout no longer follows a lone JCC as a trace edge after fallthrough-jump removal — a JCC-only terminator still has an implicit physical fallthrough block, and moving its taken target into that slot was changing optimized native control flow and causing immediate startup traps.

**Register coalescer — spill scratch**

- Spilled parallel-copy sources are now lowered from memory one at a time instead of requiring one scratch register per source. Avoids scratch exhaustion in large demos (3dbowling, xenoscape).

**DynamicSymbolPolicy**

- Extended with all syscalls introduced by IO hardening: `openat`, `fstatat`, `mkdirat`, `unlinkat`, `renameat`, `fdopendir`, `dirfd`, `chmod`, `fchmod`, `mkdtemp`.
- Native-linked Linux binaries previously failed dynamic-symbol validation without these entries.

### Windows / MSVC toolchain

**Build system**

- Top-level CMake opts into CMP0141 (when available) and requests embedded MSVC debug information for `Debug` / `RelWithDebInfo` builds — keeps generated projects aligned with modern CMake/MSVC behavior and removes PDB-discovery friction for local Windows debug builds.
- New size_t compare-exchange wrapper in the MSVC atomic compatibility layer wires through `rt_atomic_compare_exchange`. Code that uses size_t refcounts now shares the same portable atomic API as the existing int / pointer paths on Windows.

**Import policy**

- Windows import policy expanded for newly observed UCRT and Win32 symbols: `GetFullPathNameA`, `DragQueryFileW`, `CoTaskMemFree`, `__CxxFrameHandler4`, `_open_osfhandle`, `rand_s`, `fmaxl` / `fminl`, `_wcsnicmp`, and `_wchmod`. The planner routes these to the correct DLL buckets so dynamic-link audits no longer reject valid Windows surface area.

**WSL-aware audit and smoke scripts**

- `audit_runtime_surface.sh` and `run_cross_platform_smoke.sh` now honor `VIPER_BUILD_TYPE`, pass `--config` for multi-config generators, and use `cmake.exe` / `ctest.exe` plus `wslpath` when run from WSL against a Windows build tree. Capability-macro reads strip CRLF so values parsed from Windows-built headers compare correctly in POSIX shell arithmetic. `rtgen` lookup also handles the `build/src/<Config>/rtgen.exe` layout produced by Visual Studio generators.

**Runtime / graphics fixes**

- `rt_crypto` clamps BCrypt random-fill chunks with `UINT32_MAX` instead of the unavailable `DWORD_MAX` macro — MSVC builds the network runtime without ad-hoc workarounds.
- The D3D11 backend derives the bone-palette buffer allocation from the shared 256-bone palette constants used by the shader and upload-packing path. The old 128-bone hard-coded cbuffer size let the first 16 KB palette upload overrun an 8 KB mapped buffer, crashing 3dbowling under the Windows debug UCRT.

### Testing & build

- New / expanded test suites for every hardening path:
  - **Memory / GC** — traversal callbacks, promoted roots, weak-ref resurrection, finalizer trap recovery.
  - **MessageBus** — NUL-embedded topics, callback class-ID validation, raw callback rejection, owning topic snapshots, publish trap cleanup, subscription-ID overflow.
  - **Object** — `rt_obj_equals` / `GetHashCode` box dispatch, `±0.0` hash equality, type identity, `RefEquals`.
  - **`RTMemorySurfaceTests`** (new) — invalid-handle traps, resurrected release counts, array-element release, `RetainStr` / `ReleaseStr` round-trip.
  - **`RTParseTests`** — `DoubleOption` / `Int64Option` success and `None`, class IDs, typed return metadata.
  - **`RTTrapContractTests`** — NUL-embedded diagnostic message escaping.
  - **IO** — ZIP64 / extra-field rejection, asset temp-dir isolation, savedata absolute paths, VPA large-file seek, watcher overflow, inflate consumed-bytes, unsigned `BinaryBuffer`, OGG CRC.
  - **Threads** — repeatable joins, future listener trap isolation, sticky pool errors, idempotent pool shutdown, nested parallel same-pool execution, borrowed-map result retention, synchronous-channel `TrySend`, VM/BytecodeVM thread and async dispatch.
  - **Crypto** — scrypt round-trips, AEAD encrypt / decrypt with AAD, `ConstantTimeEquals`, TLS extension parsers.
  - **2D graphics** — `RTBitmapFontContractTests`, expanded `RTCanvasContractTests`, `RTColorUtilsTests` (alpha-tag preservation through `Brighten` / `Darken` / `Invert` / `Grayscale` / `Saturate` / `Desaturate` / `Complement` / `Lerp`), `RTSpriteContractTests` (tint alpha survives normalization), `RTGraphics2DTests`, `RTPixelsTests`, `RTCameraTests`, `TestTilemapAnim`, etc.
  - **x86-64 codegen** — out-of-order block-param lowering, instruction-result pre-registration, scheduler prologue-boundary preservation, large-spill `PX_COPY` regression, and a peephole regression for lone-JCC fallthrough across fixed-point branch optimization.
  - **D3D11 backend** — shared-backend coverage tying the bone-palette byte count to the supported 256 shader entries.
- The seven `Viper.GUI` widget tests in `src/lib/gui/tests/` were relabeled `tui` → `gui` (a new dedicated ctest label); the wheel-scroll regression contract was rewritten with floating-point tolerance to match the new mouse-wheel constant.

### Demos & docs

Per-file file-header and `@brief` pass across all 17 `Viper.Graphics.*` runtime sources (Theora decoder header upgraded to the full Viper template); new viperlib documentation for the `Viper.Crypto.*` namespace migration (scrypt parameters, AEAD usage, constant-time comparison); `viperlib/core.md` / `io/` / `game/persistence.md` / `system.md` / `docs/memory-management.md` updated for the new `Viper.Memory` wrappers, MessageBus locking, GC promoted-root restoration, weak-ref / finalizer ordering, and `Box` value-equality / value-type managed fields; new `docs/viperlib/graphics/rendering3d.md` documents 28 previously-undocumented `Viper.Graphics3D.*` classes; `docs/viperlib/gui/application.md` extends with round-7 widget APIs; root `README.md` had its long-running counts audited (optimizer 20→24 passes, runtime 360→378 classes / 23→21 modules, demos 16→17 games, docs 185+→200+).

---

### Commits

See `git log 7f1c4861e..HEAD -- .` for the full 17-commit history since v0.2.5.

<!-- END DRAFT -->
