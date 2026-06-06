//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_archive_internal.h
// Purpose: Internal contract between the archive core (rt_archive.c) and the
//          filesystem / atomic-write adapter layer (rt_archive_fs.c). Carries
//          the small Bytes accessors plus the platform (Win32/POSIX) file I/O
//          helpers shared across the two translation units.
//
// Key invariants:
//   - Engine-internal; must not be included outside the io/ directory.
//   - Platform-specific declarations are guarded to match their definitions in
//     rt_archive_fs.c (Win32 handle helpers vs. POSIX fd helpers).
//   - The atomic-write helpers reject symlinked/reparse path components so an
//     archive extraction cannot escape its destination root.
//
// Ownership/Lifetime:
//   - Helpers borrow caller-owned paths/handles; Bytes-producing helpers return
//     fresh GC objects owned by the caller.
//
// Links: src/runtime/io/rt_archive.c (core/ZIP/API),
//        src/runtime/io/rt_archive_fs.c (definitions)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

// NOTE: the bytes_data/bytes_len wrappers are intentionally NOT shared here.
// They are trivial static-inline wrappers around the runtime.def symbols
// rt_bytes_data/rt_bytes_len; each consuming .c defines its own copy. Declaring
// them here (or defining them inline) would either collide at link time (the
// names are generic) or be mis-parsed by rtgen's header scan.

// Core-defined helpers consumed by the fs adapter (defined in rt_archive.c).
void archive_release_temp_object(void *obj);
void archive_save_trap_error(char *buffer, size_t buffer_size, const char *fallback);

//=============================================================================
// Filesystem / atomic-write adapter (defined in rt_archive_fs.c)
//=============================================================================

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

HANDLE archive_open_win_path(const char *cpath, DWORD access, DWORD share, DWORD create_disp);
int archive_read_exact_win_or_free(HANDLE h, uint8_t *dst, size_t total, const char *trap_msg);
int archive_read_exact_win_or_release_object(HANDLE h, void *bytes, size_t total, const char *trap_msg);
void *archive_bytes_new_win_or_close(HANDLE h, int64_t len, const char *fallback);
#else
#include <sys/types.h>

int archive_open_posix(const char *path, int flags, mode_t mode);
int archive_read_exact_posix_or_free(int fd, uint8_t *dst, size_t total, const char *trap_msg);
int archive_read_exact_posix_or_release_object(int fd, void *bytes, size_t total, const char *trap_msg);
void *archive_bytes_new_posix_or_close(int fd, int64_t len, const char *fallback);
void archive_make_dirs_posix_at(int root_fd, const char *path);
int archive_open_parent_for_file_posix(int root_fd, const char *name, char **out_leaf);
void archive_write_bytes_to_dirfd_posix(int parent_fd, const char *leaf, void *data);
int archive_open_root_dir_posix(const char *cdir);
#endif

// Cross-platform atomic-write / path helpers.
char *archive_make_temp_path(const char *path, unsigned attempt);
void archive_unlink_utf8(const char *path);
int archive_write_file_all_utf8(const char *cpath, const uint8_t *src, size_t total, const char *trap_msg);
void archive_write_bytes_to_path(const char *cpath, void *data);
size_t archive_trim_trailing_seps(const char *path, size_t len);
void archive_reject_symlink_components(const char *path, size_t root_len, int include_leaf);
