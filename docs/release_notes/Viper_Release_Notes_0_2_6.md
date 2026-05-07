# Viper Compiler Platform — Release Notes

> **Development Status:** Pre-Alpha. Viper is under active development and not ready for production use.

## Version 0.2.6 — Pre-Alpha (DRAFT — unreleased)

<!-- DRAFT: release date TBD. v0.2.5 was cut on 2026-05-07. -->

### What this release is about

A focused security and correctness pass across the IO runtime and audio compatibility layer.

- **IO subsystem security hardening.** Temp-file names now draw entropy from `/dev/urandom` / `rand_s` instead of PID+pointer, eliminating an address-disclosure side-channel. Recursive directory removal uses `openat`/`fstatat`/`unlinkat` to anchor every operation to a real fd, closing TOCTOU symlink-substitution races. `O_CLOEXEC`, `O_NOFOLLOW`, and `O_NOINHERIT` applied across all file opens. Glob rejects embedded NUL bytes and caps recursion depth. Asset loads use `O_NOFOLLOW` to prevent symlink traversal out of the asset root.
- **BinaryBuffer unsigned integer API.** Eight new methods — `WriteU16LE`, `WriteU16BE`, `WriteU32LE`, `WriteU32BE` and their `Read` counterparts — fill the gap left by the existing signed-only surface. All write methods now range-check their input and trap on out-of-range values, including the previously unchecked `WriteI16` and `WriteI32` variants.
- **Multi-member GZIP support.** `Compress.Inflate` now decodes concatenated GZIP streams (RFC 1952 §2.2). A configurable output-size cap is exposed as `Compress.InflateLimit` for callers that need a tighter bound than the default 256 MB. Previously only the first member of a concatenated stream was decoded, silently dropping the rest.
- **`Viper.Audio.*` compatibility surface corrected.** The compat layer was rebuilt from `RT_ALIAS` entries into full typed class registrations. `RT_ALIAS` cannot carry typed object return values — methods that return `obj<Viper.Audio.Sound>` need their own runtime class entry for handle-type resolution to work correctly. All eight classes promoted: `Audio`, `Sound`, `Voice`, `Music`, `Playlist`, `SoundBank`, `Synth`, `MusicGen`.

### Runtime

#### IO

Temp-file creation in `rt_archive.c`, `rt_file_ext.c`, and `rt_tempfile.c` now uses a 64-bit random nonce read from `/dev/urandom` (POSIX) or generated with `rand_s` + `QueryPerformanceCounter` (Windows). The previous scheme encoded the calling pointer's address directly in the filename, leaking ASLR information and producing the same name on retry if the pointer was stable.

`rt_dir.c` gained `rt_dir_posix_open_dir_at` and `rt_dir_remove_all_at`, implementing recursive removal via the `*at` family of syscalls (`openat`, `fstatat`, `unlinkat`, `fdopendir`). Every step of the traversal is relative to an open directory fd rather than a freshly resolved path, so a symlink placed between a `stat` and a subsequent `open` cannot redirect the deletion into an unintended tree. `rt_dir_make_all` was also corrected to use `rt_dir_is_sep_char()` instead of raw `'/'`/`'\\'` literals.

All POSIX file opens in `rt_file.c`, `rt_file_io.c`, `rt_file_ext.c`, `rt_archive.c`, and `rt_asset.c` now set `O_CLOEXEC` at open time (with `FD_CLOEXEC` fallback via `fcntl` where `O_CLOEXEC` is unavailable), ensuring file descriptors are not inherited across `exec`. Windows paths add `_O_NOINHERIT` / `O_NOINHERIT` to the same effect.

`rt_asset.c` introduced `asset_name_is_safe()` — a validator that rejects absolute paths, Windows drive letters, colons, empty path segments, and dot/dotdot components before an asset file is opened. Asset file opens use `open(O_NOFOLLOW) + fdopen` instead of plain `fopen`, preventing symlink traversal out of the asset root.

Channel validity in `rt_file.c` and `rt_file_io.c` tightened from `channel < 0` to `channel <= 0`; channel 0 is never a valid Viper IO handle. `rt_file_close` saves the fd to a local variable before clearing `file->fd`, closing a race where another thread could read a stale fd after the clear.

`rt_glob.c` — pattern and path strings are now validated for embedded NUL bytes before being passed to the match engine, blocking NUL-injection attacks. `glob_recursive_helper` accepts a depth counter and traps at 4096 levels, preventing stack exhaustion on cyclic symlink graphs.

`rt_memstream.c` — `MemStream.WriteI8` validates that the value is in `[-128, 127]` before writing; previously silently truncated out-of-range values.

#### Compress

`rt_compress.c` decomposes the inflate output buffer's hard-coded 256 MB ceiling into a per-call `max_output` parameter via `inflate_data_limited()`. The original `inflate_data()` path is preserved with the default cap. `rt_compress_inflate_limit()` is exposed as a new public API in `rt_compress.h`.

`gunzip_data()` now handles concatenated multi-member GZIP streams. `gunzip_next_member_offset()` scans for the next `0x1F 0x8B` signature after the current member boundary and chains decode calls, accumulating all members into a single output buffer. RFC 1952 explicitly allows concatenated members; several HTTP servers emit them, so truncation was a latent correctness bug.

#### BinaryBuffer

`WriteU16LE`, `WriteU16BE`, `WriteU32LE`, `WriteU32BE` write unsigned 16- and 32-bit integers in the specified byte order. Each validates its input against `[0, UINT16_MAX]` or `[0, UINT32_MAX]` and traps on out-of-range values. The matching `ReadU16LE`, `ReadU16BE`, `ReadU32LE`, `ReadU32BE` return the decoded value as `i64` in `[0, 65535]` / `[0, 4294967295]`. Range-check guards were also added to the previously unchecked `WriteI16LE`, `WriteI16BE`, `WriteI32LE`, and `WriteI32BE`. All eight new functions are registered in `runtime.def` and declared in `rt_binbuf.h`.

#### Audio

The `Viper.Audio.*` compatibility namespace introduced in v0.2.5 was rebuilt from `RT_ALIAS` forwarding entries into full `RT_CLASS_BEGIN` / `RT_FUNC` / `RT_METHOD` / `RT_PROP` registrations with dedicated `AudioCompat*` handler names. The fundamental problem with aliases: a method whose signature says `obj<Viper.Audio.Sound>` requires a class named `Viper.Audio.Sound` to be registered in the runtime type registry so that handle-type resolution succeeds at the call site. An alias pointing at `Viper.Sound.*` handlers does not register that class name, causing type mismatches whenever a `Viper.Audio.*` method return value was used as a typed handle. All eight classes are promoted: `Audio`, `Sound`, `Voice`, `Music`, `Playlist`, `SoundBank`, `Synth`, `MusicGen`.

`rt_audio3d_register_voice` added to `RuntimeSurfacePolicy.inc`; its absence caused policy validation failures when the 3D audio voice registration path was exercised.

#### Codegen

`DynamicSymbolPolicy.hpp` updated with the `*at`-family syscalls used by the new fd-relative directory traversal: `openat`, `fstatat`, `mkdirat`, `unlinkat`, `renameat`, `fdopendir`, `dirfd`. Without these entries, native-linked binaries using the IO hardening code failed the linker's dynamic-symbol validation step.

### Tests and docs

New and expanded tests cover unsigned BinaryBuffer read/write (including range-trap assertions), multi-member GZIP decode, configurable inflate limit, NUL-injection and recursion-depth protection in glob, WriteI8/I16/I32 range guards, asset path safety, channel-0 guard, and the OGG page CRC-32 fix (test helper now computes the correct `0x04C11DB7` polynomial checksum for synthesized pages). `viperlib/io/` docs updated for new APIs.

### By the Numbers

| Metric | v0.2.5 | v0.2.6 | Delta |
|---|---|---|---|
| Commits | — | 2 | +2 |
| Source files | 2,996 | 2,999 | +3 |
| Production SLOC | 552K | 554,747 | +~3K |
| Test SLOC | 228K | 229,289 | +~1K |

Counts via `scripts/count_sloc.sh`.

---

### Commits

See `git log 7f1c4861e..HEAD -- .` for the full commit history since v0.2.5.

<!-- END DRAFT -->
