//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_asset_decode.c
// Purpose: Extension-based type dispatch for the asset manager. Decodes raw
//          bytes into typed runtime objects (Pixels, Sound, Mesh3D, etc.)
//          using the appropriate format decoder.
//
// Key invariants:
//   - For formats with internal buffer APIs (JPEG, Sound), calls them directly.
//   - For formats without buffer APIs (PNG, BMP, GIF, OBJ, etc.), writes to a
//     temp file and calls the file-based loader. This is safe and non-invasive.
//   - Extension matching is case-insensitive.
//   - Returns NULL for unknown extensions (caller should return as Bytes).
//
// Ownership/Lifetime:
//   - Input data buffer is borrowed (not freed).
//   - Returned objects are GC-managed.
//   - Temp files are cleaned up after use.
//
// Links: rt_asset.c (consumer), rt_pixels_io.c, rt_audio.c
//
//===----------------------------------------------------------------------===//

#ifdef _WIN32
#ifndef _CRT_RAND_S
#define _CRT_RAND_S
#endif
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <wchar.h>
#include <windows.h>
#else
#include <strings.h>
#include <unistd.h>
extern char *mkdtemp(char *);
#endif

#include "rt_file_path.h"
#include "rt_string.h"

// ─── External declarations ──────────────────────────────────────────────────

// Image decoders (file-based)
extern void *rt_pixels_load_png(void *path);
extern void *rt_pixels_load_bmp(void *path);
extern void *rt_pixels_load_gif(void *path);
extern void *rt_pixels_load(void *path);

// Image decoder (buffer-based)
extern void *rt_jpeg_decode_buffer(const uint8_t *data, size_t len);

// Audio decoder (buffer-based)
extern void *rt_sound_load_mem(const void *data, int64_t size);

// Runtime string helpers
extern rt_string rt_string_from_bytes(const char *data, size_t len);

static const char *asset_temp_suffix(const char *ext) {
    if (!ext || ext[0] != '.')
        return ".tmp";
    size_t len = strlen(ext);
    if (len == 0 || len > 32)
        return ".tmp";
    for (size_t i = 0; i < len; ++i) {
        if (ext[i] == '/' || ext[i] == '\\' || ext[i] == ':')
            return ".tmp";
    }
    return ext;
}

#ifdef _WIN32
static char *asset_decode_wide_to_utf8_dup(const wchar_t *wide) {
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (needed <= 0)
        return NULL;
    char *utf8 = (char *)malloc((size_t)needed);
    if (!utf8)
        return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, needed, NULL, NULL) <= 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}
#endif

static void asset_remove_temp_path(const char *path) {
#ifdef _WIN32
    wchar_t *wide = rt_file_path_utf8_to_wide(path);
    if (wide) {
        DeleteFileW(wide);
        free(wide);
    }
#else
    remove(path);
#endif
}

static void asset_remove_temp_dir(const char *path) {
#ifdef _WIN32
    wchar_t *wide = rt_file_path_utf8_to_wide(path);
    if (wide) {
        RemoveDirectoryW(wide);
        free(wide);
    }
#else
    rmdir(path);
#endif
}

static char *asset_join_temp_path(const char *dir, const char *leaf) {
    if (!dir || !leaf)
        return NULL;
    size_t dir_len = strlen(dir);
    size_t leaf_len = strlen(leaf);
    int needs_sep = dir_len > 0 && dir[dir_len - 1] != '/' && dir[dir_len - 1] != '\\';
    if (dir_len > SIZE_MAX - leaf_len - (needs_sep ? 1U : 0U) - 1U)
        return NULL;
    char *path = (char *)malloc(dir_len + (needs_sep ? 1U : 0U) + leaf_len + 1U);
    if (!path)
        return NULL;
    memcpy(path, dir, dir_len);
    size_t pos = dir_len;
    if (needs_sep)
#ifdef _WIN32
        path[pos++] = '\\';
#else
        path[pos++] = '/';
#endif
    memcpy(path + pos, leaf, leaf_len);
    path[pos + leaf_len] = '\0';
    return path;
}

#ifdef _WIN32
static uint64_t asset_random_u64(unsigned attempt) {
    unsigned int lo = 0;
    unsigned int hi = 0;
    if (rand_s(&lo) == 0 && rand_s(&hi) == 0)
        return ((uint64_t)hi << 32) | (uint64_t)lo;
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (uint64_t)GetCurrentProcessId() ^ (uint64_t)GetTickCount64() ^
           (uint64_t)counter.QuadPart ^ ((uint64_t)attempt << 48);
}
#endif

// ─── Helpers ────────────────────────────────────────────────────────────────

/// @brief Return 1 if `name`'s extension matches `ext` (case-insensitive), 0 otherwise.
static int iext(const char *name, const char *ext) {
    const char *dot = strrchr(name, '.');
    if (!dot)
        return 0;
#ifdef _WIN32
    return _stricmp(dot, ext) == 0;
#else
    return strcasecmp(dot, ext) == 0;
#endif
}

/// @brief Adapter that lets file-based image decoders consume in-memory bytes.
///
/// Viper's decoder suite is split: JPEG and audio codecs accept raw
/// memory buffers (`_decode_buffer`, `_load_mem`), but PNG, BMP, and
/// GIF decoders only have file-path entry points (they rely on
/// file-handle seek semantics internally). To feed those loaders
/// from embedded/mounted assets, we spill the bytes to an exclusively
/// created temp file, call the path-based loader, and unlink.
static void *load_via_tempfile(const uint8_t *data,
                               size_t size,
                               const char *ext,
                               void *(*loader)(void *path_str)) {
    const char *suffix = asset_temp_suffix(ext);
    char *tmppath = NULL;
    char *tmpdir_path = NULL;
    FILE *f = NULL;
#ifdef _WIN32
    DWORD need = GetTempPathW(0, NULL);
    if (need == 0)
        return NULL;
    wchar_t *wtmpdir = (wchar_t *)malloc(((size_t)need + 1) * sizeof(wchar_t));
    if (!wtmpdir)
        return NULL;
    DWORD tmpdir_len = GetTempPathW(need + 1, wtmpdir);
    if (tmpdir_len == 0 || tmpdir_len > need) {
        free(wtmpdir);
        return NULL;
    }
    char *tmpdir = asset_decode_wide_to_utf8_dup(wtmpdir);
    free(wtmpdir);
    if (!tmpdir)
        return NULL;
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 128 && h == INVALID_HANDLE_VALUE; ++attempt) {
        char dir_leaf[64];
        snprintf(dir_leaf,
                 sizeof(dir_leaf),
                 "viper_asset_%016llx_%d",
                 (unsigned long long)asset_random_u64((unsigned)attempt),
                 attempt);
        free(tmpdir_path);
        tmpdir_path = asset_join_temp_path(tmpdir, dir_leaf);
        if (!tmpdir_path)
            break;
        wchar_t *wide_dir = rt_file_path_utf8_to_wide(tmpdir_path);
        if (!wide_dir)
            continue;
        BOOL made_dir = CreateDirectoryW(wide_dir, NULL);
        DWORD dir_err = made_dir ? ERROR_SUCCESS : GetLastError();
        free(wide_dir);
        if (!made_dir) {
            if (dir_err == ERROR_ALREADY_EXISTS)
                continue;
            break;
        }
        char file_leaf[64];
        snprintf(file_leaf, sizeof(file_leaf), "asset%s", suffix);
        free(tmppath);
        tmppath = asset_join_temp_path(tmpdir_path, file_leaf);
        if (!tmppath) {
            asset_remove_temp_dir(tmpdir_path);
            break;
        }
        wchar_t *wide = rt_file_path_utf8_to_wide(tmppath);
        if (!wide) {
            asset_remove_temp_dir(tmpdir_path);
            continue;
        }
        h = CreateFileW(wide,
                        GENERIC_WRITE,
                        0,
                        NULL,
                        CREATE_NEW,
                        FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                        NULL);
        free(wide);
        if (h == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            asset_remove_temp_dir(tmpdir_path);
            free(tmpdir);
            free(tmppath);
            free(tmpdir_path);
            return NULL;
        }
        if (h == INVALID_HANDLE_VALUE)
            asset_remove_temp_dir(tmpdir_path);
    }
    free(tmpdir);
    if (h == INVALID_HANDLE_VALUE) {
        free(tmppath);
        free(tmpdir_path);
        return NULL;
    }
    int fd = _open_osfhandle((intptr_t)h, _O_BINARY | _O_NOINHERIT);
    if (fd < 0) {
        CloseHandle(h);
        asset_remove_temp_path(tmppath);
        asset_remove_temp_dir(tmpdir_path);
        free(tmppath);
        free(tmpdir_path);
        return NULL;
    }
    f = _fdopen(fd, "wb");
    if (!f) {
        _close(fd);
        asset_remove_temp_path(tmppath);
        asset_remove_temp_dir(tmpdir_path);
        free(tmppath);
        free(tmpdir_path);
        return NULL;
    }
#else
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";
    tmpdir_path = asset_join_temp_path(tmpdir, "viper_asset_XXXXXX");
    if (!tmpdir_path)
        return NULL;
    if (!mkdtemp(tmpdir_path)) {
        free(tmpdir_path);
        return NULL;
    }
    char file_leaf[64];
    snprintf(file_leaf, sizeof(file_leaf), "asset%s", suffix);
    tmppath = asset_join_temp_path(tmpdir_path, file_leaf);
    if (!tmppath) {
        asset_remove_temp_dir(tmpdir_path);
        free(tmpdir_path);
        return NULL;
    }
    int fd = -1;
#ifdef O_CLOEXEC
    fd = open(tmppath, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
#else
    fd = open(tmppath, O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif
    if (fd < 0) {
        asset_remove_temp_dir(tmpdir_path);
        free(tmpdir_path);
        free(tmppath);
        return NULL;
    }
#if defined(FD_CLOEXEC) && !defined(O_CLOEXEC)
    int fd_flags = fcntl(fd, F_GETFD);
    if (fd_flags >= 0)
        (void)fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
#endif
    f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        asset_remove_temp_path(tmppath);
        asset_remove_temp_dir(tmpdir_path);
        free(tmpdir_path);
        free(tmppath);
        return NULL;
    }
#endif

    if (size > 0 && fwrite(data, 1, size, f) != size) {
        fclose(f);
        asset_remove_temp_path(tmppath);
        asset_remove_temp_dir(tmpdir_path);
        free(tmppath);
        free(tmpdir_path);
        return NULL;
    }
    if (fclose(f) != 0) {
        asset_remove_temp_path(tmppath);
        asset_remove_temp_dir(tmpdir_path);
        free(tmppath);
        free(tmpdir_path);
        return NULL;
    }

    // Call file-based loader
    rt_string path_str = rt_string_from_bytes(tmppath, strlen(tmppath));
    if (!path_str) {
        asset_remove_temp_path(tmppath);
        asset_remove_temp_dir(tmpdir_path);
        free(tmppath);
        free(tmpdir_path);
        return NULL;
    }
    void *result = loader((void *)path_str);
    rt_string_unref(path_str);

    // Cleanup
    asset_remove_temp_path(tmppath);
    asset_remove_temp_dir(tmpdir_path);
    free(tmppath);
    free(tmpdir_path);
    return result;
}

// ─── rt_asset_decode_typed ──────────────────────────────────────────────────

/// @brief Decode raw bytes into a typed object based on file extension.
/// @param name  Asset name (for extension detection).
/// @param data  Raw asset bytes.
/// @param size  Size of data.
/// @return Typed GC object, or NULL if extension is unknown (return as Bytes).
void *rt_asset_decode_typed(const char *name, const uint8_t *data, size_t size) {
    if (!name || !data || size == 0)
        return NULL;

    // JPEG — direct buffer API
    if (iext(name, ".jpg") || iext(name, ".jpeg"))
        return rt_jpeg_decode_buffer(data, size);

    // Audio — direct buffer API (WAV/OGG/MP3 format detection is internal)
    if (iext(name, ".wav") || iext(name, ".ogg") || iext(name, ".mp3"))
        return rt_sound_load_mem(data, (int64_t)size);

    // PNG — via temp file
    if (iext(name, ".png"))
        return load_via_tempfile(data, size, ".png", rt_pixels_load_png);

    // BMP — via temp file
    if (iext(name, ".bmp"))
        return load_via_tempfile(data, size, ".bmp", rt_pixels_load_bmp);

    // GIF — via temp file
    if (iext(name, ".gif"))
        return load_via_tempfile(data, size, ".gif", rt_pixels_load_gif);

    // Unknown extension — return NULL (caller will return as Bytes)
    return NULL;
}
