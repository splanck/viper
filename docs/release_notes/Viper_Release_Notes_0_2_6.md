# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.5 was cut on 2026-05-07. -->

### What this release is about

A focused hardening-and-correctness cycle on the v0.2.5 surface. No new public namespaces; every change closes an attack surface, eliminates a latent correctness bug, or tightens a lifetime, locking, or type-identity invariant.

- Memory and GC overhaul — validated `Viper.Memory.Retain` / `Release` wrappers, lock-free traversal, trap-safe finalizer phase, resurrection-aware refcounting.
- MessageBus end-to-end correctness — class-ID validation, managed callback objects, NUL-safe topic hashing, internal locking, GC traversal registration.
- `Viper.Threads.*` three-round correctness pass — repeatable joins, distinct class IDs, idempotent pool shutdown with sticky-then-cleared task traps, retain-during-call across every public threads entry, monitor finalize-while-waiting safety, async + Parallel.Map retained-result ownership, listener trap isolation, synchronous-channel `TrySend` rendezvous, nested-parallel self-deadlock guard, VM payload OOM handling.
- Crypto upgrade — `Viper.Text.*` → `Viper.Crypto.*` migration with backward-compat aliases, scrypt-SHA256, AES-256-GCM with AAD, P-256 ECDH, HMAC-SHA384/512, validation-ready `Viper.Crypto.Module` with approved-mode policy gates.
- TLS X.509 verifier hardening — Key Usage, Basic Constraints, and Extended Key Usage extensions enforced; DNS-name validation tightened.
- IO security hardening (two rounds) — random-nonce temp files, `openat`-based recursive removal, `O_CLOEXEC` / `O_NOFOLLOW` everywhere, SaveData absolute-path resolution.
- Archive / Compress / VPA correctness — ZIP64 rejection, local-vs-central header cross-check, exact GZIP boundary detection, 64-bit VPA seeks.
- 2D graphics correctness — saturating int64 clip math, validated polyline / polygon arrays, sprite tint alpha preservation, tilemap I/O ownership discipline.
- `Viper.Audio.*` compat surface rebuilt from `RT_ALIAS` aliases to full typed registrations.
- AArch64 register-allocator correctness — protection set covers def operands, fixing register-reuse clobbers.
- Windows x64 stabilization — out-of-order block-param lowering, RSP/RBP scheduling boundaries, lone-JCC trace fix, parallel-copy spill scratch reuse, 256-bone D3D11 cbuffer.
- MSVC build hardening — embedded debug info via CMP0141, expanded import policy for UCRT/Win32 symbols, WSL-aware audit and smoke scripts.
- ViperIDE polish — IntelliSense linkage fix via `fe_zia` force-load, real keyword highlighting from the live `Lexer`, scrollBeyondLastLine, BMP toolbar glyphs.

### By the Numbers

| Metric | v0.2.5 | v0.2.6 | Delta |
|---|---|---|---|
| Commits | — | 23 | +23 |
| Source files | 2,996 | 3,005 | +9 |
| Production SLOC | 552K | 562K | +10K |
| Test SLOC | 228K | 232K | +4K |
| Demo SLOC | 188K | 188K | 0 |

Counts via `scripts/count_sloc.sh` (production 561,866 / test 232,404 / demo 187,826 / source files 3,005).

---

### Memory and GC

- `Viper.Memory.Retain` / `Release` redirected through validated `rt_memory_retain` / `rt_memory_release` wrappers that authenticate live runtime handles before touching refcount state.
- New typed `RetainStr` / `ReleaseStr` variants callable from Zia without a `void *` cast.
- `Release` now releases retained element references inside `RT_HEAP_ARRAY` storage (string, owned, and box element kinds) before freeing the backing buffer — these slots previously leaked.
- `rt_obj_new_i64` traps on negative or `SIZE_MAX`-overflowing byte sizes; public heap helpers validate every payload against the live registry; `rt_obj_free` rejects non-zero refcounts.
- Resurrection uses CAS and traps if the refcount is no longer zero; `Release` reports the post-finalizer refcount.
- GC phases 2 and 3 drop the lock during traversal callbacks: outgoing edges collected into a heap-allocated `gc_edge_list`; lock re-acquired only to apply colour updates.
- Phase 3 restore rewritten as iterative worklist BFS instead of recursive in-lock callbacks.
- Phase 4 wrapped in `setjmp` / `rt_trap_set_recovery` so a trapping finalizer cleans up garbage and snapshot arrays before re-raising.
- Snapshots retained via `rt_obj_retain_maybe` before lock release — prevents UAF if another thread drops the last external reference.
- Promoted-root restoration during non-full passes preserves young objects reachable from long-lived roots; weak references cleared only after finalizers decline resurrection.
- `TotalCollected` saturates at `INT64_MAX`.

### Runtime objects and MessageBus

- Boxed values carry `RT_BOX_CLASS_ID`; `Object.Equals` / `GetHashCode` dispatch correctly through `Set[Box]` and `Map[Box, ...]`.
- `rt_box_hash` normalizes `-0.0` to `+0.0` so equal values hash equally.
- New `rt_box_i1_bool(int8_t)` ABI-matching variant lets the Zia lowerer skip the `i1` → `i64` zero-extend.
- `Box.ValueType` is a class-tagged managed type — boxed structs retain object/string fields, participate in GC traversal, and release fields on finalization.
- `rt_heap_realloc` holds the registry lock as one critical section across check / `realloc` / move; `rt_heap_mark_disposed` return value corrected (was inverted); `rt_heap_try_get_header` closes a TOCTOU window on the magic check.
- `rt_type_registry` refactored to a `const char*`-error-return pattern; capacity growth no longer traps under the registry write lock.
- MessageBus: `RT_MSGBUS_CLASS_ID` and `RT_MSGBUS_CALLBACK_CLASS_ID` validated through `mb_require()` on every public entry; raw function pointers and unrelated heap objects now trap.
- Topic hashing uses full byte length via `mb_hash_bytes` / `memcmp` — embedded NULs preserved across subscribe / publish / clear.
- `mb_traverse()` registers retained callback objects with the GC tracker; an internal lock serializes public operations; `Topics()` returns an owning `Seq`; `Publish` releases its retained snapshot before re-raising a trapping handler; subscription-ID overflow caught before counter wrap.
- `Parse.DoubleOption` / `Int64Option` (with `Viper.Parse.*` aliases) — Option-returning helpers for graceful parse failure.
- `Object.RefEquals` exposed through the class method index; `rt_to_double` switched to strict `rt_parse_double` so partial parses no longer succeed silently.

### Crypto

- The Password / KeyDerive / Aes / Cipher / Hash / Rand classes formerly under `Viper.Text.*` are now canonical members of `Viper.Crypto.*`; the old names continue to resolve through the runtime alias mechanism — no source changes required.
- `KeyDerive.ScryptSHA256` / `ScryptSHA256Str` — RFC 7914 with bounded `N * r * 128` memory cap; out-of-range parameters rejected, not silently clamped.
- `Password.Hash()` defaults to scrypt and emits `SCRYPT$<log2N>$<r>$<p>$<salt_b64>$<hash_b64>`; `Password.Verify()` still accepts the legacy `PBKDF2$…` format so old hashes verify without migration.
- PBKDF2 wrappers reject below-minimum iteration counts (was silently raised).
- `Aes.EncryptAuth` / `DecryptAuth` and `Cipher.EncryptAAD` / `DecryptAAD` / `EncryptWithKeyAAD` / `DecryptWithKeyAAD` accept both AES-128-GCM and AES-256-GCM keys.
- 16-byte magic header (`'V', 'A', 'K', '1'` plus version) prevents plain-AES and authenticated-AES ciphertexts from being confused; the header is composed with the application-supplied AAD so the GCM tag covers both.
- New `Viper.Crypto.Module` adds approved-mode controls, module self-tests, and fail-closed policy gates; AES-256-GCM, HMAC-SHA384, HMAC-SHA512, HKDF-SHA384, and native P-256 ECDH primitives.
- Approved mode routes `Cipher` through AES-256-GCM (`VCA1` / `VKA1`), routes `Password.Hash` through PBKDF2, and uses an in-tree HMAC-DRBG for runtime crypto randomness.
- TLS fails closed in approved mode until the P-256/P-384 TLS key-share profile is wired and validated.
- `Hash.ConstantTimeEquals` / `ConstantTimeEqualsBytes` — branch-free equality for MAC tags, session IDs, and other secret-bearing strings; `Password.Verify()` uses these internally.

### TLS

- Key Usage (OID 2.5.29.15), Basic Constraints (2.5.29.19), and Extended Key Usage (2.5.29.37) extensions parsed and enforced during chain validation.
- Intermediate certificates without the cA flag in Basic Constraints are rejected; leaf certificates without TLS Web Server Authentication EKU are rejected.
- Key Usage bits checked against the role each certificate plays in the chain.
- `tls_dns_name_bytes_valid` rejects empty names, embedded-NUL or non-ASCII bytes, names > 255 bytes, labels > 63 bytes, and improper wildcard placement.
- `rt_crypto.c` and `rt_tls.c` thread the new constant-time helpers and per-namespace class IDs through the handshake state.

### IO runtime

- Temp-file creation uses a 64-bit random nonce (`/dev/urandom` on POSIX, `rand_s` + `QueryPerformanceCounter` on Windows) — eliminates the ASLR side-channel from the previous pointer-encoded scheme.
- Asset decoding creates a private directory (`mkdtemp` on POSIX, random subdirectory on Windows), cleaned up on success and failure.
- `asset_name_is_safe()` rejects absolute paths, drive letters, colons, empty segments, and dot/dotdot components; asset opens use `open(O_NOFOLLOW | O_CLOEXEC) + fdopen`.
- Recursive directory removal rebuilt around `openat` / `fstatat` / `unlinkat` / `fdopendir` with `AT_SYMLINK_NOFOLLOW` — a symlink substituted mid-traversal cannot redirect deletion.
- `O_CLOEXEC` at every open with `FD_CLOEXEC` fallback via `fcntl`; Windows paths add `_O_NOINHERIT`.
- Channel validity tightened: `channel <= 0` rejected (channel 0 was never valid); `rt_file_close` saves the fd locally before clearing — closes a UAF race.
- `Glob` rejects embedded NULs; recursion capped at 4096; `MemStream.WriteI8` validates `[-128, 127]`.
- Watcher emits `RT_WATCH_EVENT_OVERFLOW` on buffer overflow rather than silently dropping; `IN_CLOEXEC` / `FD_CLOEXEC` on inotify and kqueue fds.
- New `rt_file_stdio.h` consolidates the UTF-8-aware `fopen` wrapper duplicated across four files.
- SaveData paths resolved to absolute before first use (`GetFullPathNameW` / `getcwd`-with-doubling); Windows opens use `_O_NOINHERIT | _O_BINARY`; malformed entries silently skipped so games are resilient to partial writes.
- ZIP extra-field parser rejects ZIP64 extension headers; central-directory rejects `version_needed >= 45`, encryption flags, ZIP64 sentinels.
- Local-file headers cross-checked against central-directory values for method, flags, CRC, and both size fields.
- `inflate_data_limited_ex()` exposes `consumed_bytes` for exact compressed-input accounting; multi-member GZIP uses this for precise boundary detection.
- VPA seeks use 64-bit `_fseeki64` / `fseeko`; opens perform `lstat` pre-check then `O_NOFOLLOW`.

### Collections runtime

- Eight new unsigned `BinaryBuffer` methods: `WriteU16LE/BE`, `WriteU32LE/BE`, `ReadU16LE/BE`, `ReadU32LE/BE` (with `[0, UINT16_MAX]` / `[0, UINT32_MAX]` range validation).
- Retroactive range-check guards on `WriteI16LE/BE` and `WriteI32LE/BE` — previously truncated silently.

### Threading runtime

- Thread joins are now repeatable: the first successful `Join`, `TryJoin`, or `JoinFor` reclaims the OS handle; later calls on the same handle return success instead of trapping with `Thread.Join: already joined`. The trap is gone, ADR 0002 and `viperlib/threads.md` updated to match.
- `Thread.Start` / `StartSafe` borrow their argument consistently across native, VM, and BytecodeVM dispatch; `StartOwned` / `StartSafeOwned` retain managed arguments across all three backends.
- Safe VM and BytecodeVM workers propagate trap text into the safe-thread error boundary instead of aborting the worker.
- `ConcurrentQueue`, `ConcurrentMap`, `Scheduler`, `Debouncer`, `Throttler`, and `ThreadPool` each carry a distinct runtime class ID and validate every public-API handle before downcasting.
- Retain-during-call discipline applied to every public threads entry — `rt_thread_*`, `rt_threadpool_*`, `rt_concmap_*`, `rt_concqueue_*`, `rt_debounce_*`, `rt_throttle_*`, `rt_scheduler_*`, `rt_cancellation_*`, `rt_future_*`, `rt_promise_*` — so a concurrent finalize on the host object can no longer race the deref. Safe-thread accessors copy the inner-thread pointer under retain via a dedicated `safe_thread_copy_inner_thread` helper.
- Monitor finalize-while-waiting safety: new `RT_MON_WAITER_CANCELLED` waiter state and `monitor_cancel_queue` wake every parked waiter (acquisition queue and wait queue) when `rt_monitor_forget` retires the entry, so blocked threads bail out with `Monitor.Enter: object finalized while waiting` instead of sleeping forever. All `Enter` / `TryEnter` / `TryEnterFor` paths now check the retired flag up front; recursion overflow at `SIZE_MAX` traps instead of wrapping.
- `ConcurrentMap.Keys()` / `Values()` snapshot under the map lock and retain copied values; `ConcurrentMap.Remove()` / `Clear()` / finalization and `ConcurrentQueue.Clear()` / finalization detach nodes before releasing retained values. `ConcurrentQueue` finalize broadcasts the condvar and marks the queue closed so any thread parked in `Dequeue` / `DequeueFor` wakes immediately.
- `Channel.TrySend()` on synchronous (capacity-0) channels publishes a retained handoff only when a receiver is already waiting, then returns without waiting for acknowledgement; `TryRecv()` remains strictly non-blocking. Channel deadline math falls back to `CLOCK_REALTIME` when `CLOCK_MONOTONIC` is unavailable instead of leaving the timespec uninitialised.
- Pool shutdown is idempotent — worker handles stolen under the pool monitor and joined outside the lock — so repeated `Shutdown` / `ShutdownNow` calls and concurrent finalization can no longer double-join or double-release. Pool task trap messages are sticky-then-cleared: the next `Wait` / `WaitFor` / `Shutdown` / `ShutdownNow` rethrows the captured worker error and drains it, later calls report the current pool state normally.
- `Parallel.*Pool` detects calls from a worker already running in the target pool and runs nested work inline to avoid self-deadlock; worker callback traps preserve the original trap text instead of replacing it with a generic `Parallel.*: task trapped` message. `_Static_assert` on the function-pointer-to-void* bridge confirms size equality.
- `Async.Run` / `Map` / `All` / `Any` and `Parallel.Map` switched from a transferred-result model to a retained-result model: every callback return is retained before publication, so borrowed inputs and shared runtime objects survive past the original owner's release. The `parallel_result_is_input` / `parallel_result_is_sequence_input` identity heuristics are gone; cleanup is uniform across success and trap paths. `Async.All` / `Any` state finalizers release every retained source slot.
- Future completion listeners and listener-cancel hooks isolate trapping callbacks via a new `future_invoke_listener` helper that runs the callback under `setjmp` recovery and, if it traps, runs the registered cancel hook under its own recovery — so one bad listener cannot rethrow through promise completion or cancel cleanup.
- Cancellation propagation simplified: children retain their parent and `IsCancelled` walks upward through the retained chain, so `cancellation_mark_cancelled` only flips the immediate-children flags under the lock instead of recursing through a snapshot. `IsCancelled` snapshots `parent` under retain so a finalize on `token` between the local-flag check and the parent walk can no longer free the data.
- VM payload OOM handling: every `new VmThreadStartPayload` / `BytecodeThreadPayload` / `VmAsyncRunPayload` is now `new (std::nothrow)` plus a null-check that traps with `<entry>: payload allocation failed`. The Async.Run path on null-payload resolves the promise with the failure message and returns the future to the caller so the Zia side observes a real Future error.
- `Promise` / `ConcurrentMap` / `ConcurrentQueue` / `Scheduler` `New()` paths trap with descriptive messages and free their partial allocation when `pthread_mutex_init` or `pthread_cond_init` fails, instead of leaving a half-initialised object in the GC graph.
- `Debouncer` / `Throttler` no longer use timestamp zero as an implicit state sentinel — explicit `has_signal` / `has_last_allowed` flags distinguish "never triggered" from a coincidental zero reading, and `elapsed_since_ms` clamps backwards-clock readings to zero so a system-time adjustment cannot make a previously-ready debouncer regress.
- Win32 Future timeouts compute one absolute monotonic deadline up front (`future_deadline_tick_from_now`) saturating at `ULLONG_MAX`, replacing the relative `DWORD`-clamped `future_deadline_ms_from_now`; `rt_future_get_for` / `wait_for` / `get_for_val` no longer round-trip through `MAXDWORD`.
- `SafeI64.Add` converts the post-add `uint64_t` back to `int64_t` via `memcpy` instead of a C-style cast, avoiding implementation-defined behaviour on signed overflow on both Win32 and POSIX.

### Audio

- `Viper.Audio.*` compatibility namespace rebuilt from `RT_ALIAS` forwarding entries into full `RT_CLASS_BEGIN` / `RT_FUNC` / `RT_METHOD` / `RT_PROP` registrations — `RT_ALIAS` cannot carry typed object return values, so `obj<Viper.Audio.Sound>` was failing type resolution at every call site.
- All eight classes promoted: `Audio`, `Sound`, `Voice`, `Music`, `Playlist`, `SoundBank`, `Synth`, `MusicGen`.
- `rt_audio3d_register_voice` added to `RuntimeSurfacePolicy.inc`.

### 2D graphics

- New saturating int64 clip-rect arithmetic via `rtg_add_sat64` / `rtg_i64_fits_i32` helpers — extreme coordinates clip safely instead of overflowing.
- Polyline / polygon / path point arrays validated through `rt_heap_try_get_header`: NULL pointers, non-Integer arrays, and arrays shorter than `count * 2` elements are silently ignored rather than read past their bounds.
- `count` capped at `INT64_MAX / 2` so the `count * 2` multiply cannot overflow.
- Sprite tint normalization preserves the explicit-alpha tag from `Color.RGBA` — was masked to 24 bits, forcing every tinted sprite back to fully opaque.
- Tilemap I/O introduces `tilemap_io_release_ref` plus `map_set_owned` / `seq_push_owned` so reload cycles cannot double-free shared tile references.
- `Color.FromHex` rejects strings with embedded NUL bytes — was returning a partial color.
- Redundant per-subsystem class-ID constants in `rt_graphics2d.c` removed; canonical IDs come from the runtime registry.

### ViperIDE

- The `zia` binary's CMake target now force-loads `fe_zia` (`LINKER:-force_load` on macOS, `--whole-archive` on Linux) — without this the static linker satisfied every `rt_zia_*` reference from the weak stubs in `viper_runtime/core/rt_zia_completion_stub.c`, so completion / hover / diagnostics / symbols silently returned `"Zia completion unavailable"`.
- New C-callable bridge `rt_zia_highlight.cpp` in `fe_zia` exposes `rt_zia_is_keyword(name, len)`.
- The GUI tokenizer in `rt_gui_codeeditor.c` now consults `Lexer::lookupKeyword` (52 keywords) instead of the local 28-entry table that had been silently drifting.
- Inline `/* ... */` block-comment tracking via a per-editor `zia_block_comment_depth` field; uppercase-first heuristic colors `Foo` / `MyClass` / `Math` as types; identifiers immediately followed by `(` colored as function calls (`#DCDCAA`).
- `codeeditor_max_scroll_y` uses half-viewport scrollBeyondLastLine padding (matches VS Code / Sublime) — last line floats to mid-viewport rather than being pinned to the bottom edge.
- Mouse-wheel scroll dropped from 1.0 to 0.3 lines per delta — more comfortable on macOS trackpads.
- Toolbar uses BMP-only Geometric Shapes / Arrows glyphs as button labels — the previous emoji attempt fell back to the missing-glyph "hamburger" on UI fonts that lack color emoji.
- New `tbBuild` / `tbRun` / `tbFind` items wired into the command dispatch.
- Untitled (Ctrl+N) buffers and the welcome buffer begin with `module UntitledN;` / `module Welcome;` so the completion engine can parse them on the first keystroke; without a module declaration `CompleteForFile` was returning an empty string and the popup appeared blank.

### Compiler, IL & codegen

- AArch64 protection set in `Allocator.cpp` now covers both *use* and *def* operands — prevents eviction of live def vregs mid-instruction; fields renamed `protectedUse{GPR,FPR}_` → `protectedOperand{GPR,FPR}_` to reflect the broader scope. Fixes a class of register-reuse clobber bugs.
- x86-64 lowerer pre-registers block parameters and instruction results before lowering textually out-of-order blocks, so optimized IL can reference dominated values without tripping unknown-SSA diagnostics.
- Legalize creates typed virtual registers for edge-copy arguments whose defining block has not been lowered yet — fixes out-of-order block-parameter copies in optimized BASIC demos.
- Post-RA scheduler treats physical RSP / RBP definitions as scheduling boundaries, preserving Win64 frame setup and unwind-sensitive prologue ordering before callee-save spills.
- Branch layout no longer follows a lone JCC as a trace edge after fallthrough-jump removal — a JCC-only terminator still has an implicit physical fallthrough block, and moving its taken target into that slot was changing optimized native control flow and causing immediate startup traps.
- Register coalescer lowers spilled parallel-copy sources from memory one at a time instead of demanding one scratch register per source — eliminates scratch exhaustion in 3dbowling and xenoscape.
- `DynamicSymbolPolicy` extended with the syscalls introduced by IO hardening: `openat`, `fstatat`, `mkdirat`, `unlinkat`, `renameat`, `fdopendir`, `dirfd`, `chmod`, `fchmod`, `mkdtemp`. Native-linked Linux binaries previously failed dynamic-symbol validation without these entries.
- `RuntimeSurfacePolicy.inc` registers the new `rt_threadpool_current_worker_pool` accessor used by the nested-parallel guard.

### Windows / MSVC toolchain

- Top-level CMake opts into CMP0141 (when available) and requests embedded MSVC debug information for `Debug` / `RelWithDebInfo` builds — keeps generated projects aligned with modern CMake/MSVC behavior and removes PDB-discovery friction for local Windows debug builds.
- New `size_t` compare-exchange wrapper in the MSVC atomic compatibility layer wires through `rt_atomic_compare_exchange`; size_t-refcount code now shares the same portable atomic API as the `int` and pointer paths on Windows.
- Windows import policy expanded for newly observed UCRT and Win32 symbols: `GetFullPathNameA`, `DragQueryFileW`, `CoTaskMemFree`, `__CxxFrameHandler4`, `_open_osfhandle`, `rand_s`, `fmaxl` / `fminl`, `_wcsnicmp`, `_wchmod`. Routed to the correct DLL buckets so dynamic-link audits no longer reject valid Windows surface area.
- `audit_runtime_surface.sh` and `run_cross_platform_smoke.sh` are WSL-aware: honor `VIPER_BUILD_TYPE`, pass `--config` for multi-config generators, use `cmake.exe` / `ctest.exe` plus `wslpath` when run from WSL against a Windows build tree; capability-macro reads strip CRLF; `rtgen` lookup also handles `build/src/<Config>/rtgen.exe` from Visual Studio generators.
- `rt_crypto` clamps BCrypt random-fill chunks with `UINT32_MAX` instead of the unavailable `DWORD_MAX` macro — MSVC builds the network runtime without ad-hoc workarounds.
- D3D11 backend derives the bone-palette buffer allocation from the shared 256-bone palette constants used by the shader and upload-packing path. The old 128-bone hard-coded cbuffer let the first 16 KB palette upload overrun an 8 KB mapped buffer, crashing 3dbowling under the Windows debug UCRT.
- Windows full-build TLS and timeout test stabilization — flaky network-runtime tests reworked so the MSVC full build runs the network suite cleanly without timing-sensitive false failures.

### Tests

- Memory / GC: traversal callbacks, promoted roots, weak-ref resurrection, finalizer trap recovery.
- MessageBus: NUL-embedded topics, callback class-ID validation, raw-callback rejection, owning topic snapshots, publish trap cleanup, subscription-ID overflow.
- Object: `rt_obj_equals` / `GetHashCode` Box dispatch, `±0.0` hash equality, type identity, `RefEquals`.
- New `RTMemorySurfaceTests`: invalid-handle traps, resurrected release counts, array-element release, `RetainStr` / `ReleaseStr` round-trip.
- New `RTParseTests`: `DoubleOption` / `Int64Option` success and `None`, class IDs, typed return metadata.
- New `RTTrapContractTests`: NUL-embedded diagnostic message escaping.
- IO: ZIP64 / extra-field rejection, asset temp-dir isolation, savedata absolute paths, VPA large-file seek, watcher overflow, inflate consumed-bytes, unsigned `BinaryBuffer`, OGG CRC.
- Threads: repeatable joins, future listener trap isolation, sticky-then-cleared pool errors through Wait / Shutdown / ShutdownNow, idempotent pool shutdown, nested-parallel same-pool execution, retained borrowed-callback results across `Async.Run` / `Async.Map` / `Parallel.Map`, synchronous-channel `TrySend`, ConcurrentMap remove/clear release-after-unlock, VM/BytecodeVM owned thread and async dispatch.
- Crypto: scrypt round-trips, AEAD encrypt / decrypt with AAD, `ConstantTimeEquals`, TLS extension parsers.
- 2D graphics: new `RTBitmapFontContractTests`; expanded `RTCanvasContractTests`; `RTColorUtilsTests` for alpha-tag preservation through `Brighten` / `Darken` / `Invert` / `Grayscale` / `Saturate` / `Desaturate` / `Complement` / `Lerp`; `RTSpriteContractTests` for tint-alpha survival; `RTGraphics2DTests`, `RTPixelsTests`, `RTCameraTests`, `TestTilemapAnim`.
- x86-64 codegen: out-of-order block-param lowering, instruction-result pre-registration, scheduler prologue-boundary preservation, large-spill `PX_COPY` regression, lone-JCC fallthrough peephole.
- D3D11: shared-backend coverage tying the bone-palette byte count to the 256 supported shader entries.
- The seven `Viper.GUI` widget tests in `src/lib/gui/tests/` were relabeled `tui` → `gui` (a new dedicated ctest label); the wheel-scroll regression contract was rewritten with floating-point tolerance to match the new mouse-wheel constant.

Demos and docs changed only as required to track the runtime work above: chess and xenoscape pinned to O0 on Windows until the optimised-build regression they triggered is resolved; per-file file-header and `@brief` audit across the `Viper.Graphics.*`, `Viper.Crypto.*`, and `Viper.Threads.*` sources; refreshed `viperlib/` reference pages for the migrated crypto namespace, the threading repeatable-join contract, the retained `Async.Run` / `Async.Map` / `Parallel.Map` result rules, and the new `rt_future_peek_value` ownership note; ADR 0002 updated; root `README.md` long-running counts re-audited (optimizer 20→24 passes, runtime 360→378 classes / 23→21 modules, demos 16→17 games, docs 185+→200+).

---

### Commits

See `git log v0.2.5-dev..HEAD -- .` for the full 23-commit history since v0.2.5.

<!-- END DRAFT -->
