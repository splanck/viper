# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.5 was cut on 2026-05-07. -->

### What this release is about

A focused hardening cycle with no major new features. Every change closes an attack surface, eliminates a latent correctness bug, or tightens a lifetime or locking invariant.

- **IO subsystem security hardening — two rounds.** Temp-file names now draw a 64-bit random nonce from `/dev/urandom` / `rand_s`, replacing the old scheme that encoded the calling pointer (ASLR side-channel, stable on retry). Recursive directory removal uses the `*at` syscall family so every traversal step is anchored to a real fd; a symlink substituted mid-traversal cannot redirect deletion into an unintended tree. `O_CLOEXEC`, `O_NOFOLLOW`, and `O_NOINHERIT` applied to every file open in the IO subsystem. Asset loading validates names against a path-traversal allowlist and opens files with `O_NOFOLLOW`.
- **SaveData path safety and corruption resilience.** Save paths are resolved to absolute before first use, eliminating chdir-relative confusion. Loading a corrupt or partially-written save file now silently skips invalid entries instead of trapping.
- **Archive and compress correctness.** ZIP extra-field parser rejects ZIP64 extension headers; local-file headers cross-checked against central-directory metadata before extraction. The DEFLATE engine gains an exact consumed-bytes counter, enabling precise member-boundary detection in concatenated GZIP streams.
- **VPA large-file support.** VPA archive seeks use 64-bit file offsets on all platforms, enabling correct operation on archives larger than 2 GB.
- **BinaryBuffer unsigned integer API.** Eight new unsigned read/write methods added; range-check guards retrofitted to all existing signed write methods.
- **`Viper.Audio.*` compatibility surface rebuilt.** The compat namespace was converted from `RT_ALIAS` forwarding entries to full typed class registrations; `RT_ALIAS` cannot carry typed object return values, causing silent handle-type resolution failures.
- **`Viper.Memory` surface hardening.** `Viper.Memory.Retain` and `Viper.Memory.Release` re-wired to validated wrappers; added string-typed variants and correct array-element release. `rt_obj_new_i64` traps on negative sizes and sizes exceeding `SIZE_MAX`.
- **GC traversal now lock-free.** Phases 2 and 3 of cycle collection collect edges into heap-allocated lists and apply them under brief lock windows, eliminating a class of deadlocks where traversal callbacks called back into GC APIs while the lock was held.
- **MessageBus hardening.** Class-ID tagging, type-validated casts, managed callback objects, NUL-safe topic hashing, subscription-ID overflow guard, and GC traversal registration for retained handlers.
- **Box, Heap, and Diagnostics correctness.** Boxed values participate in `Object.Equals` and `GetHashCode` dispatch. `rt_heap_realloc` holds the registry lock as a single critical section. `rt_heap_mark_disposed` return value corrected. Diagnostic assert-eq-str rewritten for NUL-safe comparison and byte-escaped output.

### IO runtime

All temp-file creation paths (`rt_archive.c`, `rt_file_ext.c`, `rt_tempfile.c`, `rt_asset_decode.c`) now generate names from a 64-bit random nonce: `/dev/urandom` on POSIX, `rand_s` + `QueryPerformanceCounter` on Windows. The old scheme encoded the calling pointer's address, leaking ASLR information and producing the same filename on retry when the pointer was stable.

Asset decoding via `load_via_tempfile` creates a private directory with `mkdtemp` (POSIX) or a random-named subdirectory (Windows) before writing the temp file, cleaned up on both success and failure. Asset name validation (`asset_name_is_safe()`) rejects absolute paths, Windows drive letters, colons, empty segments, and dot/dotdot components. Asset file opens use `open(O_NOFOLLOW | O_CLOEXEC) + fdopen`.

Recursive directory removal in `rt_dir.c` was rewritten around `rt_dir_posix_open_dir_at`, which opens a subdirectory relative to a parent fd using `openat` with `AT_SYMLINK_NOFOLLOW` and confirms via `fstat`. Removal uses `openat`, `fstatat`, `unlinkat`, and `fdopendir` throughout; no traversal step goes through a path string that could be replaced between lookup and use.

All POSIX file opens set `O_CLOEXEC` at open time with `FD_CLOEXEC` fallback via `fcntl`; Windows paths add `_O_NOINHERIT`. File channel validity tightened from `channel < 0` to `channel <= 0` — channel 0 is not a valid Viper IO handle. `rt_file_close` saves the fd to a local before clearing `file->fd`, closing a use-after-free race. Glob rejects strings with embedded NUL bytes and traps at 4096 recursion levels. `MemStream.WriteI8` validates `[-128, 127]` before writing.

The watcher subsystem extracts `watcher_start_windows_read()` to deduplicate three identical `ReadDirectoryChangesW` call sites. Buffer-overflow conditions emit `RT_WATCH_EVENT_OVERFLOW` and restart the read instead of silently dropping events. `inotify_init1` called with `IN_CLOEXEC`; kqueue fds receive `FD_CLOEXEC`.

`rt_file_stdio.h` — new shared header consolidating the UTF-8-aware `fopen` wrapper previously duplicated across `rt_binfile.c`, `rt_linereader.c`, `rt_linewriter.c`, and `rt_stream.c`.

**SaveData** — Paths resolved to absolute before first use via `GetFullPathNameW` (Windows) or `getcwd` with a doubling-retry loop (POSIX). Windows opens use `_O_NOINHERIT | _O_BINARY`. Loading a save file with a malformed key or string value now silently skips that entry rather than trapping, making games resilient to partial writes and filesystem corruption.

**Archive** — `archive_extra_is_malformed_or_zip64()` rejects ZIP64 extension headers (ID `0x0001`) and truncated field lists. The central-directory parser rejects entries with `version_needed >= 45`, encryption or data-descriptor flags, or ZIP64 sentinel values. Local-file headers cross-checked against central-directory values for method, flags, CRC, and both size fields.

**Compress** — `inflate_data_limited_ex()` adds a `consumed_bytes` output parameter for exact compressed-input accounting. The multi-member GZIP path uses this for precise member-boundary detection instead of a heuristic signature scan. The padding-bits check corrected to use `consumed_bits` from `br.pos` and `br.bits_in_buf`, fixing a false-positive trailing-data trap.

**VPA** — `vpa_fseek`/`vpa_ftell` macros select `_fseeki64`/`_ftelli64` (Windows) or `fseeko`/`ftello` (POSIX). VPA opens perform an `lstat` pre-check then `open(O_NOFOLLOW | O_CLOEXEC)`.

### Collections runtime

`BinaryBuffer` gains eight new unsigned integer methods: `WriteU16LE/BE`, `WriteU32LE/BE` with `[0, UINT16_MAX]` / `[0, UINT32_MAX]` range validation, and matching `ReadU16LE/BE`, `ReadU32LE/BE` returning `i64`. Retroactive range-check guards added to `WriteI16LE/BE` and `WriteI32LE/BE`, which previously silently truncated out-of-range values. All eight functions registered in `runtime.def`.

`Box` received several correctness fixes. Boxed values now carry `RT_BOX_CLASS_ID` so `rt_obj_type_name` and `rt_obj_to_string` return `"Viper.Core.Box"` for any box, and `rt_obj_equals` / `rt_obj_get_hash_code` dispatch to `rt_box_equal` / `rt_box_hash` when either argument is a boxed value — enabling correct `Set[Box]` and `Map[Box, ...]` behaviour through the System.Object surface. `rt_box_hash` normalizes `-0.0` to `+0.0` before hashing so values that compare equal hash equally. `rt_box_i1_bool(int8_t)` added as a new ABI-matching variant; `runtime.def` and the Zia lowerer updated to call it directly with the `i1` type rather than zero-extending to `i64` first. `alloc_box` routes through `rt_obj_new_i64` to inherit the bounds guards added in this cycle.

### Audio

The `Viper.Audio.*` compatibility namespace was rebuilt from `RT_ALIAS` forwarding entries into full `RT_CLASS_BEGIN` / `RT_FUNC` / `RT_METHOD` / `RT_PROP` registrations. `RT_ALIAS` cannot carry typed object return values: methods returning `obj<Viper.Audio.Sound>` require `Viper.Audio.Sound` to exist as a named class in the runtime type registry. An alias pointing at `Viper.Sound.*` handlers does not register that name, causing handle-type resolution failures at every such call site. All eight classes promoted: `Audio`, `Sound`, `Voice`, `Music`, `Playlist`, `SoundBank`, `Synth`, and `MusicGen`. `rt_audio3d_register_voice` added to `RuntimeSurfacePolicy.inc`.

### Core runtime

**Viper.Memory** — `Viper.Memory.Retain` and `Viper.Memory.Release` re-wired in `runtime.def` to validated wrapper functions (`rt_memory_retain` / `rt_memory_release`) that authenticate live runtime heap and string handles before touching refcounts and run object finalizers correctly at zero. `rt_memory_retain_str` and `rt_memory_release_str` added as string-typed variants registered as `Viper.Memory.RetainStr` / `Viper.Memory.ReleaseStr`, so Zia-emitted code can call them without a `void*` cast. `rt_memory_release` now handles `RT_HEAP_ARRAY` payloads by releasing all element references (`RT_ELEM_STR` and `RT_ELEM_NONE`) before freeing storage — previously array elements were leaked when released through this path. `rt_obj_new_i64` guards against negative `byte_size` and `byte_size > SIZE_MAX`. A `Viper.Memory` static class entry added to `runtime.def`; `RTCLS_Memory` added to `RuntimeClasses.hpp`.

**GC** — The cycle collector's Phase 2 (trial decrement) and Phase 3 (trial restore / reachability marking) now run with the GC lock dropped during traversal callbacks. Each phase collects child edges into a heap-allocated `gc_edge_list` via a visitor callback; the lock is acquired only to apply the decrement or colour updates to the tracking table. Phase 3 restore uses an iterative worklist BFS (`gc_worklist`) instead of recursive visitor calls inside the lock, eliminating deadlocks where traversal callbacks called GC APIs while the lock was held. Snapshot objects are retained via `rt_obj_retain_maybe` before the lock is released, preventing use-after-free if another thread drops the last external reference during collection. `gc_release_outgoing_ref` ensures garbage objects without finalizers release their outgoing edges before `gc_reclaim_unreachable` is called, correctly decrementing external references the garbage set holds. All OOM paths on `gc_edge_list`, `gc_worklist`, and the garbage array clean up all heap allocations and release snapshot refs before trapping. `rt_obj_resurrect` switched from `atomic_store` to a CAS; traps if the refcount is not zero at the moment of resurrection.

**MessageBus** — `RT_MSGBUS_CLASS_ID` and `RT_MSGBUS_CALLBACK_CLASS_ID` class constants added. `mb_require()` is a type-validated cast used by all eight public API functions, trapping on wrong-class or NULL pointers. `rt_msgbus_callback_new()` wraps a native `void (*)(void *)` function pointer in a heap-managed callback object; `mb_invoke_callback()` validates the class ID before dispatching, so passing an unrelated heap object as a callback now traps rather than jumping into object memory. Topic hashing and lookup switched to `mb_hash_bytes()` / `memcmp` using the full byte length from `rt_str_len`, so topics with embedded NUL bytes remain distinct across subscribe, publish, and clear. MessageBus objects register with the GC tracker via `mb_traverse()` so retained callback objects in subscriber lists are visible to the cycle collector. Subscription ID overflow guard traps before the counter wraps.

**Heap** — `rt_heap_realloc` now holds the registry lock across the registry-contains check, `ensure_capacity` call, `realloc`, and registry-move as a single critical section. Previously the lock was released before `realloc` and re-acquired after, creating a window where another thread could observe a stale registry entry. `rt_heap_mark_disposed` return value corrected: it now returns 1 when marking for the first time and 0 when already marked; the previous implementation had these inverted, causing any caller testing "did I win the first-mark race" to behave backwards.

**Diagnostics** — `rt_diag_assert_eq_str` rewritten to use `rt_str_eq` instead of `strcmp` for NUL-safe comparison. Added byte-aware message escaping, which formats up to 64 bytes of string content with C-escape sequences for non-printable and non-ASCII bytes and appends `...` for longer strings. Failure messages now correctly display embedded NUL bytes as `\x00` rather than truncating the comparison at the first NUL.

**Parse** — `rt_parse_double_option` and `rt_parse_int64_option` added as Option-returning parse helpers wrapping `rt_parse_try_num` / `rt_parse_try_int`. Registered as `Viper.Core.Parse.DoubleOption` / `Int64Option` with `Viper.Parse.*` aliases. The existing `Parse.Double` / `Parse.Int64` entries are documented as low-level C-pointer ABI helpers; the new Option variants are the user-facing surface for graceful parse failure.

**Second memory/core hardening pass** — Cycle collection now restores through promoted roots during non-full passes, preserving young objects reachable from long-lived roots. Weak references are cleared only after finalizers decline resurrection, and `Viper.Memory.Release` reports the resurrected post-finalizer refcount. GC reclamation releases outgoing references for finalized garbage objects while skipping edges to objects in the same garbage set. `rt_weakref_new` now checks allocation failure before initializing the handle. String release reporting uses the actual post-decrement count for heap and small-string handles.

**Boxed value types and Option typing** — `Box.ValueType` now allocates a class-tagged managed value-type object rather than a raw heap blob. The compiler registers managed fields through `Viper.Core.Box.ValueTypeAddField`, allowing boxed structs to retain object/string fields, participate in GC traversal, and release those fields on finalization. `Viper.Option` objects now carry `RT_OPTION_CLASS_ID`; `Object.TypeName`/`TypeId` identify options, value types, and strings, and Parse option-returning helpers expose `obj<Viper.Option>` in the runtime catalog.

**MessageBus correctness** — The bus now serializes public operations with an internal lock. `Subscribe` rejects ordinary heap objects instead of accepting them as callbacks, `Topics()` returns an owning `Seq` of retained topic strings, and `Publish` releases its retained callback snapshot before re-raising a trapping handler.

**Third memory/core hardening pass** — Public heap helpers now validate payloads against the live heap registry before touching headers, so stale heap pointers trap in release builds. `rt_obj_free()` rejects non-zero refcounts instead of freeing live payloads, and `Viper.Memory.Release()` cleans up `RT_ELEM_BOX` arrays in addition to object/string arrays. Cycle collection releases snapshot retains before reclaiming garbage, clears weak-reference chain links when zeroing refs, clears the collecting flag after finalizer traps, and saturates `TotalCollected` at `INT64_MAX`. MessageBus subscriptions now require managed callback objects from `Callback(fn)`; raw function pointers must be wrapped. `Box.ValueType(0)` is valid, duplicate managed-field registrations are idempotent only for the same field kind, and retained managed fields are validated outside the layout lock. Type registry duplicate registrations are idempotent only when metadata matches, missing bases and unknown interface bindings trap, and capacity growth failures no longer trap while holding the registry write lock.

**Diagnostics and Object surface** — Diagnostic assert messages are formatted byte-aware, so embedded NUL bytes are escaped in trap text instead of truncating messages. `Viper.Core.Object.RefEquals` is exposed through the class method index in addition to the standalone runtime function.

**Codegen** — `DynamicSymbolPolicy.hpp` extended with all syscalls introduced by the IO hardening work: `openat`, `fstatat`, `mkdirat`, `unlinkat`, `renameat`, `fdopendir`, `dirfd`, `chmod`, `fchmod`, and `mkdtemp`. Without these entries native-linked Linux binaries failed dynamic-symbol validation.

### Tests

Comprehensive coverage added for all hardening paths: archive ZIP64/extra-field rejection, local-header cross-check, asset temp-dir isolation, savedata absolute-path resolution and corrupt-key skip, VPA large-file seek, watcher overflow event handling, inflate consumed-bytes and padding-bits correctness, unsigned `BinaryBuffer` round-trip and range-trap, OGG page CRC-32 correctness. GC tests verify traversal callbacks can invoke GC APIs without deadlocking under the new lock-free traversal scheme, promoted roots restore young children, weak refs survive finalizer resurrection, weak refs can be reset after clearing, finalized garbage releases external outgoing references, and finalizer traps do not leave collection permanently active. MessageBus tests cover NUL-embedded topic identity, callback class-ID validation, raw callback rejection, owning topic snapshots, publish trap cleanup, and subscription-ID overflow. Object tests confirm `rt_obj_equals` / `GetHashCode` box dispatch, `-0.0`/`+0.0` hash equality, `Box`/`Option`/string/value-type type identity, and `RefEquals` method-index registration. `RTMemorySurfaceTests` covers `Viper.Memory` invalid-handle traps, resurrected release counts, boxed/object/string array-element release, live-object free traps, and `RetainStr`/`ReleaseStr` round-trip. `RTParseTests` covers `DoubleOption`/`Int64Option` success, `None`, class IDs, typed return metadata, and zeroed outputs on low-level parse failures. `RTTrapContractTests` verifies NUL-embedded diagnostic message escaping. `RTMemorySurfaceTests.cpp` added as a new test file.

### Demos & docs

`viperlib/core.md` updated for Box value-equality/hash semantics, boxed value-type managed-field registration, `DoubleOption`/`Int64Option` typed Option variants, `Object.RefEquals`, and MessageBus locking/callback/topic ownership. `viperlib/io/`, `viperlib/game/persistence.md`, `docs/memory-management.md`, and `viperlib/system.md` updated for IO hardening APIs, `Viper.Memory` validated wrappers, resurrection-aware release counts, weak-ref/finalizer ordering, and GC promoted-root semantics. Full Viper file-header and `@brief` pass across all 19 `Viper.Memory.*` and `Viper.Core.*` source files.

### By the Numbers

| Metric | v0.2.5 | v0.2.6 | Delta |
|---|---|---|---|
| Commits | — | 8 | +8 |
| Source files | 2,996 | 3,001 | +5 |
| Production SLOC | 552K | 556K | +4K |
| Test SLOC | 228K | 230K | +2K |
| Demo SLOC | 188K | 188K | 0 |

Counts via `scripts/count_sloc.sh`.

---

### Commits

See `git log 7f1c4861e..HEAD -- .` for the full 8-commit history since v0.2.5.

<!-- END DRAFT -->
