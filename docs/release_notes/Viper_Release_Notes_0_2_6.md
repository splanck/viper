# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.5 was cut on 2026-05-07. -->

### What this release is about

A focused hardening cycle with no major new features. Every change this release closes an attack surface, eliminates a latent correctness bug, or tightens a lifetime invariant.

- **IO security hardening — two rounds.** Temp-file names now draw a 64-bit random nonce from `/dev/urandom` / `rand_s`, replacing the old scheme that encoded the calling pointer (ASLR side-channel, stable on retry). Recursive directory removal uses the `*at` syscall family so every traversal step is anchored to a real file descriptor; a symlink substituted mid-traversal cannot redirect deletion into an unintended tree. `O_CLOEXEC`, `O_NOFOLLOW`, and `O_NOINHERIT` applied to every file open in the IO subsystem. Asset loading validates names against a path-traversal allowlist and opens files with `O_NOFOLLOW`.
- **SaveData path safety and corruption resilience.** Save paths are resolved to absolute before first use, eliminating chdir-relative confusion. Loading a corrupt or partially-written save file now silently skips invalid entries instead of trapping.
- **Archive and compress correctness.** ZIP extra-field parser rejects ZIP64 extension headers; local-file headers are cross-checked against central-directory metadata before extraction begins. The DEFLATE engine gains an exact compressed-byte consumption counter, enabling precise member-boundary detection in concatenated GZIP streams and fixing a false-positive trailing-data trap.
- **VPA large-file support.** VPA archive seeks use 64-bit file offsets on all platforms, enabling correct operation on archives larger than 2 GB.
- **BinaryBuffer unsigned integer API.** Eight new unsigned read/write methods added; range-check guards retrofitted to all existing signed write methods.
- **`Viper.Audio.*` compatibility surface rebuilt.** The compat namespace was converted from `RT_ALIAS` forwarding entries to full typed class registrations. `RT_ALIAS` cannot carry typed object return values; every method returning a managed audio handle was silently failing handle-type resolution.
- **`Viper.Memory` surface hardening.** `Viper.Memory.Retain` and `Viper.Memory.Release` are re-wired to validated wrappers that authenticate live handles before touching refcounts. Previously they called raw allocator internals, bypassing all handle validation. `rt_obj_new_i64` traps on negative sizes and sizes exceeding `SIZE_MAX`.

### IO runtime

All temp-file creation paths (`rt_archive.c`, `rt_file_ext.c`, `rt_tempfile.c`, `rt_asset_decode.c`) now generate names from a 64-bit random nonce: `/dev/urandom` on POSIX, `rand_s` + `QueryPerformanceCounter` on Windows. The old scheme encoded the calling pointer's address, leaking ASLR information and producing the same filename on retry when the pointer was stable.

Asset decoding via `load_via_tempfile` now creates a private directory with `mkdtemp` (POSIX) or a random-named subdirectory (Windows) before writing the temp file. The directory is cleaned up on both success and failure paths. Asset name validation (`asset_name_is_safe()`) rejects absolute paths, Windows drive letters, colons, empty segments, and dot/dotdot components. Asset file opens use `open(O_NOFOLLOW | O_CLOEXEC) + fdopen` to prevent symlink traversal.

Recursive directory removal in `rt_dir.c` was rewritten around `rt_dir_posix_open_dir_at` — a helper that opens a subdirectory by name relative to a parent fd using `openat` with `AT_SYMLINK_NOFOLLOW` and confirms via `fstat` that the target is a real directory. Removal (`rt_dir_remove_all_at`) uses `openat`, `fstatat`, `unlinkat`, and `fdopendir` throughout; no traversal step goes through a path string that could be replaced between lookup and use.

All POSIX file opens across the IO subsystem now set `O_CLOEXEC` at open time with `FD_CLOEXEC` fallback via `fcntl`; Windows paths add `_O_NOINHERIT`. File channel validity tightened from `channel < 0` to `channel <= 0` — channel 0 is not a valid Viper IO handle. `rt_file_close` saves the fd to a local variable before clearing `file->fd`, closing a use-after-free race on concurrent access. Glob rejects strings containing embedded NUL bytes before pattern matching and traps at 4096 levels of recursion to prevent stack exhaustion on cyclic symlink trees. `MemStream.WriteI8` validates `[-128, 127]` before writing.

**SaveData** — Save paths are resolved to absolute before first use: `GetFullPathNameW` on Windows, `getcwd` with a doubling-retry loop on POSIX. Windows file opens use `_O_NOINHERIT | _O_BINARY`. `SaveData.Load` now silently skips any entry whose key contains NUL bytes, invalid UTF-8, or empty segments, and any string value that fails validation, rather than trapping. Games are resilient to partial writes, filesystem corruption, and cross-platform encoding accidents.

**Archive** — `archive_extra_is_malformed_or_zip64()` walks the ZIP extra-field TLV list and rejects any entry with a ZIP64 extension header (ID `0x0001`) or a truncated field list. The central-directory parser reads `version_needed` and `flags`; entries with `version_needed >= 45`, encryption flag bits, data-descriptor flags, or ZIP64 sentinel values (`UINT32_MAX`) are rejected before extraction. Local-file headers are cross-checked against central-directory values for compression method, flags, CRC, and both size fields; any mismatch traps with a specific message. File-size validation now also rejects archives whose `st_size` exceeds `SIZE_MAX`.

**Compress** — `inflate_data_limited_ex()` extends the inflate engine with a `consumed_bytes` output parameter (exact compressed-input bytes consumed) and an `allow_trailing` flag controlling whether data after the final DEFLATE block is an error. The multi-member GZIP path uses `consumed_bytes` for precise member-boundary detection, replacing the previous heuristic `0x1F 0x8B` signature scan. The padding-bits check was corrected to derive from the actual consumed-bit count (`br.pos` and `br.bits_in_buf`) rather than from `bits_in_buf` alone, fixing a false-positive trailing-data trap on streams whose last byte contained partial padding.

**VPA** — `rt_vpa_reader.c` selects `_fseeki64`/`_ftelli64` (Windows) or `fseeko`/`ftello` (POSIX) through `vpa_fseek`/`vpa_ftell` macros, enabling correct operation on archives larger than 2 GB. VPA file opens perform an `lstat` pre-check followed by `open(O_NOFOLLOW | O_CLOEXEC)` to prevent symlink substitution between check and open.

**Watcher** — `watcher_start_windows_read()` deduplicates three previously identical `ReadDirectoryChangesW` call sites. Buffer-overflow conditions (zero `bytes_returned`, `ERROR_NOTIFY_ENUM_DIR`) now emit `RT_WATCH_EVENT_OVERFLOW` and restart the read instead of silently dropping events. `inotify_init1` is called with `IN_CLOEXEC` where supported, with a `FD_CLOEXEC` fallback for older kernels; kqueue fds also receive `FD_CLOEXEC`.

`rt_file_stdio.h` — new shared header consolidating the UTF-8-aware `fopen` wrapper previously duplicated verbatim across `rt_binfile.c`, `rt_linereader.c`, `rt_linewriter.c`, and `rt_stream.c`.

### Collections runtime

`BinaryBuffer` gains eight new unsigned integer methods. `WriteU16LE`, `WriteU16BE`, `WriteU32LE`, and `WriteU32BE` write unsigned 16- and 32-bit integers with range validation against `[0, UINT16_MAX]` / `[0, UINT32_MAX]`, trapping on out-of-range input. `ReadU16LE`, `ReadU16BE`, `ReadU32LE`, and `ReadU32BE` decode the stored value and return it as an `i64` in `[0, 65535]` / `[0, 4294967295]`. Retroactive range-check guards were also added to `WriteI16LE/BE` and `WriteI32LE/BE`, which previously silently truncated out-of-range values. All eight new functions are registered in `runtime.def`.

### Audio

The `Viper.Audio.*` compatibility namespace was rebuilt from `RT_ALIAS` forwarding entries into full `RT_CLASS_BEGIN` / `RT_FUNC` / `RT_METHOD` / `RT_PROP` registrations. `RT_ALIAS` cannot express typed class return values: a method declaring `obj<Viper.Audio.Sound>` requires `Viper.Audio.Sound` to exist as a named class in the runtime type registry. An alias pointing at `Viper.Sound.*` handlers does not register that name, so every call site returning a managed audio object silently failed handle-type resolution. All eight classes were promoted to full registrations: `Audio`, `Sound`, `Voice`, `Music`, `Playlist`, `SoundBank`, `Synth`, and `MusicGen`. `rt_audio3d_register_voice` added to `RuntimeSurfacePolicy.inc`.

### Core runtime

`Viper.Memory.Retain` and `Viper.Memory.Release` are re-wired in `runtime.def` to validated wrapper functions — `rt_memory_retain` and `rt_memory_release` — instead of calling raw allocator internals (`rt_heap_retain` / `rt_heap_release`) directly. The validated wrappers authenticate live runtime heap and string handles before touching refcounts, route strings and objects through separate managed paths, trap on invalid or freed pointers, and run object finalizers correctly when the last reference is dropped. Both functions added to `RuntimeSurfacePolicy.inc`.

`rt_obj_new_i64` gains pre-allocation bounds guards: negative `byte_size` and `byte_size > SIZE_MAX` both trap with a descriptive message before reaching the heap allocator, replacing silent undefined behaviour.

New GC tests: `test_collect_reclaims_cycle_storage_and_finalizers` verifies a two-object cycle is fully reclaimed — both heap payloads freed, finalisers called exactly twice, heap registry entries cleared, and all weak references zeroed. `test_threshold_get_set_contract` verifies that negative threshold input clamps to 0, positive values round-trip exactly, and setting zero disables automatic collection.

**Codegen** — `DynamicSymbolPolicy.hpp` extended with the syscalls required by the IO hardening work: `openat`, `fstatat`, `mkdirat`, `unlinkat`, `renameat`, `fdopendir`, `dirfd`, `chmod`, `fchmod`, and `mkdtemp`. Without these entries, native-linked Linux binaries failed dynamic-symbol validation at link time.

### Tests

All IO hardening paths have dedicated test coverage: archive ZIP64 / extra-field rejection, local-header cross-check mismatches, asset path-traversal blocking and temp-dir isolation, savedata absolute-path resolution and corrupt-key skipping, VPA large-file seek, watcher overflow event emission, inflate consumed-bytes accounting, padding-bits correction, and unsigned `BinaryBuffer` round-trip and range-trap behaviour. `RTMemorySurfaceTests.cpp` (new) is a process-isolation suite for the `Viper.Memory` surface: invalid-pointer retain and release each trap, negative-size allocation traps, retain overflow traps, and `set_len` past capacity traps.

### Demos & docs

`viperlib/io/` pages updated for the new `BinaryBuffer` unsigned API, compress limit API, asset path-safety rules, multi-member GZIP behaviour, and savedata absolute-path semantics. `viperlib/game/persistence.md`, `docs/memory-management.md`, and `viperlib/system.md` updated for the validated `Viper.Memory` wrappers and GC threshold contract. Full Viper file-header separator and `@brief` documentation pass across all 19 `Viper.Memory.*` and `Viper.Core.*` source files.

### By the Numbers

| Metric | v0.2.5 | v0.2.6 | Delta |
|---|---|---|---|
| Commits | — | 7 | +7 |
| Source files | 2,996 | 3,001 | +5 |
| Production SLOC | 552K | 556K | +4K |
| Test SLOC | 228K | 230K | +2K |
| Demo SLOC | 188K | 188K | 0 |

Counts via `scripts/count_sloc.sh`.

---

### Commits

See `git log 7f1c4861e..HEAD -- .` for the full 7-commit history since v0.2.5.

<!-- END DRAFT -->
