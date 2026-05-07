# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.5 was cut on 2026-05-07. -->

### What this release is about

A deep security and correctness hardening cycle focused entirely on the IO runtime and audio compatibility layer. No major new features; the theme is closing attack surfaces and fixing latent correctness bugs that become important as the platform approaches real-world use.

- **IO subsystem security hardening — two rounds.** Temp-file names draw entropy from `/dev/urandom` / `rand_s` instead of PID+pointer, eliminating an ASLR side-channel. Recursive directory removal uses `openat`/`fstatat`/`unlinkat` to anchor every operation to a real fd, closing TOCTOU symlink-substitution races. `O_CLOEXEC`, `O_NOFOLLOW`, and `O_NOINHERIT` applied across all file opens in the IO subsystem. Asset loads prevent path traversal with a name-safety validator and symlink-resistant open. Temp files for asset decoding are now created in private `mkdtemp` directories so their paths cannot be predicted or pre-created. Glob rejects embedded NUL bytes and caps recursion depth.
- **SaveData path safety and corruption resilience.** Save paths are resolved to absolute before use (Windows `GetFullPathNameW`, POSIX `getcwd`), eliminating chdir-relative confusion. Windows file opens use `_O_NOINHERIT | _O_BINARY`. Loading a corrupt save file now silently skips invalid keys and values instead of trapping, making games resilient to partial writes and filesystem corruption.
- **Archive hardening.** ZIP extra-field parser rejects ZIP64 extension headers and malformed field lists. Local-file headers are cross-checked against central directory metadata (flags, method, CRC, sizes) before extraction. File-size validation guards against `SIZE_MAX` overflow.
- **VPA large-file support.** VPA archive opens use `fseeko`/`ftello` (POSIX) and `_fseeki64`/`_ftelli64` (Windows) for correct behaviour on archives larger than 2 GB. File opens use `O_NOFOLLOW` with `lstat` pre-check.
- **GZIP inflate precision.** The inflate engine now tracks exact compressed byte consumption, enabling precise member-boundary detection in concatenated GZIP streams. Padding-bits validation corrected to compute from actual consumed-bit count rather than `bits_in_buf`.
- **BinaryBuffer unsigned integer API.** Eight new methods — `WriteU16LE/BE`, `WriteU32LE/BE` and matching `Read` variants — plus range-check guards on all existing signed write methods.
- **`Viper.Audio.*` compatibility surface corrected.** The compat layer was rebuilt from `RT_ALIAS` forwarding entries into full typed class registrations. `RT_ALIAS` cannot carry typed object return values; methods returning `obj<Viper.Audio.Sound>` require their own runtime class entry for handle-type resolution to succeed at the call site.

### Runtime

#### IO

All temp-file creation paths (`rt_archive.c`, `rt_file_ext.c`, `rt_tempfile.c`, `rt_asset_decode.c`) now generate names using a 64-bit random nonce: `/dev/urandom` on POSIX, `rand_s` + `QueryPerformanceCounter` on Windows. The previous scheme encoded the calling pointer's address in the filename, leaking ASLR state and producing the same name on retry when the pointer was stable.

Asset decoding via `load_via_tempfile` creates a private directory with `mkdtemp` (POSIX) or a random-named subdirectory (Windows) so the temp file path is unguessable. The directory is cleaned up when the load completes whether or not it succeeded.

Recursive directory removal in `rt_dir.c` uses the `*at` syscall family (`openat`, `fstatat`, `unlinkat`, `fdopendir`). Every traversal step is relative to an open directory fd, so a symlink substituted between `stat` and `open` cannot redirect deletion into an unintended tree. `rt_dir_make_all` uses `rt_dir_is_sep_char()` instead of raw `'/'`/`'\\'` literals.

All POSIX file opens across the IO subsystem set `O_CLOEXEC` at open time with `FD_CLOEXEC` fallback via `fcntl`. Windows paths add `_O_NOINHERIT` / `O_NOINHERIT`. `rt_asset.c`'s `asset_name_is_safe()` rejects absolute paths, Windows drive letters, colons, empty segments, and dot/dotdot components. Asset opens use `open(O_NOFOLLOW) + fdopen` to prevent symlink traversal. VPA opens (`rt_vpa_reader.c`) perform `lstat` pre-check followed by `open(O_NOFOLLOW | O_CLOEXEC)`.

File channel validity tightened from `channel < 0` to `channel <= 0`; channel 0 is not a valid Viper IO handle. `rt_file_close` saves the fd to a local before clearing `file->fd`, closing a use-after-free race. Glob pattern and path strings are validated for embedded NUL bytes before matching; `glob_recursive_helper` traps at 4096 recursion levels. `MemStream.WriteI8` validates `[-128, 127]` before writing.

The watcher subsystem (`rt_watcher.c`) gained `watcher_start_windows_read()`, extracting the `ReadDirectoryChangesW` call from three previously identical sites. Buffer-overflow conditions (zero `bytes_returned`, `ERROR_NOTIFY_ENUM_DIR`) now emit `RT_WATCH_EVENT_OVERFLOW` and restart the read rather than silently dropping events. `inotify_init1` is called with `IN_CLOEXEC` where available; FD_CLOEXEC fallback for older kernels. kqueue fds also receive FD_CLOEXEC.

`rt_file_stdio.h` — new shared header consolidating the UTF-8-aware `fopen` wrapper previously duplicated across `rt_binfile.c`, `rt_linereader.c`, `rt_linewriter.c`, and `rt_stream.c`.

#### SaveData

`SaveData` paths are now resolved to absolute before first use. On Windows this calls `GetFullPathNameW`; on POSIX it prepends the result of `getcwd`, with a doubling-retry loop for long working directories. This prevents save-path confusion when the process changes its working directory after the save data object is constructed. File opens on Windows use `_O_NOINHERIT | _O_BINARY`. Loading a save file that contains a malformed key (NUL bytes, invalid UTF-8, empty) or a malformed string value now silently skips that entry rather than trapping, making games resilient to partial writes, filesystem corruption, and cross-platform encoding accidents.

#### Archive

`rt_archive.c` gained `archive_extra_is_malformed_or_zip64()`, which walks the ZIP extra-field TLV list and rejects any entry containing a ZIP64 extension header (ID `0x0001`) or a truncated/malformed list. The central-directory parser now reads `version_needed` and `flags`; entries with `version_needed >= 45`, encryption flag bits, data-descriptor flags, or ZIP64 sentinel values (`UINT32_MAX`) are rejected before extraction. Local-file headers are cross-checked against central-directory values for flags, method, CRC, and both size fields; mismatches trap with specific messages. File-size validation extended to reject archives where `st_size` exceeds `SIZE_MAX`.

#### Compress

`inflate_data_limited_ex()` extends the inflate engine with two parameters: `consumed_bytes` (output — exact number of compressed input bytes consumed) and `allow_trailing` (controls whether data after the final DEFLATE block is an error). The prior multi-member GZIP implementation used a heuristic signature scan to find member boundaries; it now uses `consumed_bytes` from `inflate_data_limited_ex` for exact positioning. The padding-bits check was also corrected: it now computes from `consumed_bits` (derived from `br.pos` and `br.bits_in_buf`) rather than from `bits_in_buf` alone, fixing a false-positive trailing-data trap on streams whose last byte contained partial padding.

#### BinaryBuffer

`WriteU16LE`, `WriteU16BE`, `WriteU32LE`, `WriteU32BE` write unsigned 16/32-bit integers with range validation against `[0, UINT16_MAX]` / `[0, UINT32_MAX]`. Matching `ReadU16LE`, `ReadU16BE`, `ReadU32LE`, `ReadU32BE` return the decoded value as `i64`. Range-check guards were also added retroactively to `WriteI16LE/BE` and `WriteI32LE/BE`, which previously silently truncated out-of-range values. All eight new functions registered in `runtime.def`.

#### VPA

`rt_vpa_reader.c` gains `vpa_fseek`/`vpa_ftell` macros selecting `_fseeki64`/`_ftelli64` (Windows) or `fseeko`/`ftello` (POSIX) for correct large-file (>2 GB) support.

#### Audio

The `Viper.Audio.*` compatibility namespace was rebuilt from `RT_ALIAS` forwarding entries into full `RT_CLASS_BEGIN` / `RT_FUNC` / `RT_METHOD` / `RT_PROP` registrations. `RT_ALIAS` cannot carry typed object return values: a method with signature `obj<Viper.Audio.Sound>` requires a class named `Viper.Audio.Sound` in the runtime type registry; an alias pointing at `Viper.Sound.*` handlers does not register that name, causing handle-type resolution failures. All eight classes promoted: `Audio`, `Sound`, `Voice`, `Music`, `Playlist`, `SoundBank`, `Synth`, `MusicGen`. `rt_audio3d_register_voice` added to `RuntimeSurfacePolicy.inc`.

#### Codegen

`DynamicSymbolPolicy.hpp` updated with syscalls required by the IO hardening code: `openat`, `fstatat`, `mkdirat`, `unlinkat`, `renameat`, `fdopendir`, `dirfd`, `chmod`, `fchmod`, `mkdtemp`. Without these entries native-linked Linux binaries failed dynamic-symbol validation.

### Tests and docs

Comprehensive new tests cover all hardening paths: archive ZIP64/extra-field rejection, local-header cross-check, asset temp-dir isolation, savedata absolute paths and corrupt-key skip, VPA large-file seek, watcher overflow events, inflate consumed-bytes and padding-bits correctness, unsigned BinaryBuffer round-trip and range traps, and OGG page CRC-32 correctness. `viperlib/io/` and `viperlib/game/persistence.md` updated.

### By the Numbers

| Metric | v0.2.5 | v0.2.6 | Delta |
|---|---|---|---|
| Commits | — | 3 | +3 |
| Source files | 2,996 | 3,000 | +4 |
| Production SLOC | 552K | 555,417 | +~3K |
| Test SLOC | 228K | 229,533 | +~2K |

Counts via `scripts/count_sloc.sh`.

---

### Commits

See `git log 7f1c4861e..HEAD -- .` for the full commit history since v0.2.5.

<!-- END DRAFT -->
