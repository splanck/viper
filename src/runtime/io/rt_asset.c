//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_asset.c
// Purpose: Runtime asset manager implementation. Provides transparent loading
//          from embedded VPA blobs, mounted .vpa pack files, and filesystem.
//
// Key invariants:
//   - Initialization is idempotent (safe to call multiple times).
//   - Resolution order: embedded → mounted packs (LIFO) → filesystem.
//   - Pack auto-discovery scans exe directory for *.vpa files on init.
//   - Type dispatch in Assets.Load() is based on file extension.
//   - All returned objects are GC-managed.
//
// Ownership/Lifetime:
//   - Embedded blob pointer is borrowed (lives in .rodata, never freed).
//   - Mounted pack handles are owned and closed on unmount or process exit.
//   - Data buffers from vpa_read_entry are freed after creating GC objects.
//
// Links: rt_vpa_reader.h, rt_path_exe.c, rt_compress.h
//
//===----------------------------------------------------------------------===//

#include "rt_asset.h"
#include "rt_file_path.h"
#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_vpa_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <pthread.h>
#include <strings.h>
#include <sys/stat.h>
#endif

// ─── External declarations ──────────────────────────────────────────────────

#include "rt_trap.h"
extern rt_string rt_string_from_bytes(const char *data, size_t len);
extern const char *rt_string_cstr(rt_string s);
extern size_t rt_string_len(rt_string s);
extern rt_string rt_const_cstr(const char *s);
extern void *rt_bytes_from_raw(const uint8_t *data, size_t len);

// Type-dispatched decoder (rt_asset_decode.c)
extern void *rt_asset_decode_typed(const char *name, const uint8_t *data, size_t size);

// Exe directory detection
extern char *rt_path_exe_dir_cstr(void);

/// @brief Resolve `path` to its canonical form and return a heap-allocated copy.
static char *asset_canonical_path_dup(const char *path) {
    if (!path || !*path)
        return NULL;
#ifdef _WIN32
    DWORD needed = GetFullPathNameA(path, 0, NULL, NULL);
    if (needed > 0) {
        char *full = (char *)malloc((size_t)needed);
        if (full) {
            DWORD got = GetFullPathNameA(path, needed, full, NULL);
            if (got > 0 && got < needed)
                return full;
            free(full);
        }
    }
    return strdup(path);
#else
    char *resolved = realpath(path, NULL);
    if (resolved)
        return resolved;
    return strdup(path);
#endif
}

/// @brief Case-sensitive (POSIX) or case-insensitive (Windows) path comparison.
static int asset_path_equal(const char *a, const char *b) {
    if (!a || !b)
        return 0;
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    return strcmp(a, b) == 0;
#endif
}

/// @brief Return a pointer into `path` to the final filename component (after the last separator).
static const char *asset_basename(const char *path) {
    const char *name = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    if (!name || (back && back > name))
        name = back;
    return name ? name + 1 : path;
}

// ─── Weak default for embedded asset blob ───────────────────────────────────
// These are overridden by the stronger definitions in the generated asset .o
// file when assets are embedded via `embed` directives in viper.project.
// When no assets are embedded, these defaults ensure clean linking.

#ifdef _WIN32
__declspec(selectany) const unsigned char viper_asset_blob[1] = {0};
__declspec(selectany) const unsigned long long viper_asset_blob_size = 0;
#else
__attribute__((weak)) const unsigned char viper_asset_blob[1] = {0};
__attribute__((weak)) const unsigned long long viper_asset_blob_size = 0;
#endif

// ─── Constants ──────────────────────────────────────────────────────────────

#define RT_ASSET_MAX_PACKS 32

// ─── Global state ───────────────────────────────────────────────────────────

/// @brief Singleton asset manager state — all fields guarded by g_asset_lock.
static struct {
    vpa_archive_t *embedded;                  ///< Embedded .rodata VPA blob, or NULL.
    vpa_archive_t *packs[RT_ASSET_MAX_PACKS]; ///< Mounted pack file archives (LIFO).
    char *pack_paths[RT_ASSET_MAX_PACKS];     ///< Canonical paths for each mounted pack.
    int pack_count;   ///< Number of currently mounted packs.
    int initialized;  ///< Non-zero after the first call to rt_asset_init.
} g_asset_mgr;

#ifdef _WIN32
static INIT_ONCE g_asset_lock_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_asset_lock;

/// @brief `InitOnce` callback that initializes the asset manager critical section.
static BOOL CALLBACK asset_init_lock(PINIT_ONCE once, PVOID parameter, PVOID *context) {
    (void)once;
    (void)parameter;
    (void)context;
    InitializeCriticalSection(&g_asset_lock);
    return TRUE;
}

/// @brief Acquire the asset-manager lock (Windows CRITICAL_SECTION, lazy-initialized).
static void asset_lock(void) {
    InitOnceExecuteOnce(&g_asset_lock_once, asset_init_lock, NULL, NULL);
    EnterCriticalSection(&g_asset_lock);
}

/// @brief Release the asset-manager lock.
static void asset_unlock(void) {
    LeaveCriticalSection(&g_asset_lock);
}
#else
static pthread_mutex_t g_asset_lock = PTHREAD_MUTEX_INITIALIZER;

/// @brief Acquire the asset-manager lock (POSIX mutex).
static void asset_lock(void) {
    pthread_mutex_lock(&g_asset_lock);
}

/// @brief Release the asset-manager lock.
static void asset_unlock(void) {
    pthread_mutex_unlock(&g_asset_lock);
}
#endif

// ─── Helpers ────────────────────────────────────────────────────────────────

/// @brief Borrow a null-terminated view of an asset name from an `rt_string`.
///
/// Returns NULL if `name` is empty or contains an embedded null byte
/// (which would produce a silent truncation on strcmp-based lookups).
///
/// @param name Viper string containing the asset name.
/// @return Borrowed C string pointer, or NULL if the name is invalid.
static const char *asset_name_cstr(rt_string name) {
    const uint8_t *data = NULL;
    size_t len = rt_file_string_view((const ViperString *)name, &data);
    if (!data)
        return NULL;
    if (memchr(data, '\0', len) != NULL)
        return NULL;
    return (const char *)data;
}

#ifdef _WIN32
/// @brief Open a file at a UTF-8 path in binary-read mode (Windows wide-string path).
static FILE *asset_fopen_utf8(const char *path) {
    wchar_t *wide = rt_file_path_utf8_to_wide(path);
    if (!wide)
        return NULL;
    FILE *fp = _wfopen(wide, L"rb");
    free(wide);
    return fp;
}

/// @brief Stat a regular file at a UTF-8 path and return its size (Windows).
///
/// Returns 0 for missing files, directories, and reparse points.
/// Writes the file size in bytes to `*out_size` when non-NULL.
///
/// @param path     UTF-8 file path.
/// @param out_size Out-parameter for size in bytes (may be NULL).
/// @return 1 if the path is a regular readable file, 0 otherwise.
static int asset_regular_file_size(const char *path, uint64_t *out_size) {
    if (out_size)
        *out_size = 0;
    wchar_t *wide = rt_file_path_utf8_to_wide(path);
    if (!wide)
        return 0;
    WIN32_FILE_ATTRIBUTE_DATA attrs;
    BOOL ok = GetFileAttributesExW(wide, GetFileExInfoStandard, &attrs);
    free(wide);
    if (!ok)
        return 0;
    if ((attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return 0;
    if ((attrs.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        return 0;
    if (out_size) {
        *out_size = ((uint64_t)attrs.nFileSizeHigh << 32) | (uint64_t)attrs.nFileSizeLow;
    }
    return 1;
}
#else
/// @brief Open a file at a UTF-8 path in binary-read mode (POSIX).
static FILE *asset_fopen_utf8(const char *path) {
    return fopen(path, "rb");
}

/// @brief Stat a regular file at a UTF-8 path and return its size (POSIX).
///
/// Returns 0 for missing files, directories, and special files.
/// Writes the file size in bytes to `*out_size` when non-NULL.
///
/// @param path     UTF-8 file path.
/// @param out_size Out-parameter for size in bytes (may be NULL).
/// @return 1 if the path is a regular readable file, 0 otherwise.
static int asset_regular_file_size(const char *path, uint64_t *out_size) {
    if (out_size)
        *out_size = 0;
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return 0;
    if (st.st_size < 0)
        return 0;
    if (out_size)
        *out_size = (uint64_t)st.st_size;
    return 1;
}
#endif

/// @brief Resolve an asset name across the layered asset sources.
///
/// Lookup order (first-hit wins):
///   1. The embedded VPA baked into the executable's .rodata — always
///      tried first so shipped binaries stay self-contained.
///   2. Mounted packs, iterated in *reverse* mount order — later
///      mounts shadow earlier ones, letting mod/DLC loads override
///      the base game's assets cleanly.
///   3. The filesystem, relative to the current working directory —
///      dev-mode convenience that lets you drop a file in-place
///      without rebuilding a VPA.
/// Returns a heap-allocated buffer (caller frees) or NULL if not found
/// or on any read error.
static uint8_t *asset_find_data(const char *name, size_t *out_size) {
    // 1. Embedded registry
    if (g_asset_mgr.embedded) {
        const vpa_entry_t *e = vpa_find(g_asset_mgr.embedded, name);
        if (e)
            return vpa_read_entry(g_asset_mgr.embedded, e, out_size);
    }

    // 2. Mounted packs (reverse order — last mounted wins)
    for (int i = g_asset_mgr.pack_count - 1; i >= 0; --i) {
        if (!g_asset_mgr.packs[i])
            continue;
        const vpa_entry_t *e = vpa_find(g_asset_mgr.packs[i], name);
        if (e)
            return vpa_read_entry(g_asset_mgr.packs[i], e, out_size);
    }

    // 3. Filesystem fallback (CWD-relative)
    uint64_t fsize = 0;
    if (asset_regular_file_size(name, &fsize)) {
        if (fsize > SIZE_MAX)
            return NULL;
        FILE *f = asset_fopen_utf8(name);
        if (!f)
            return NULL;
        *out_size = (size_t)fsize;
        uint8_t *buf = (uint8_t *)malloc(*out_size > 0 ? *out_size : 1);
        if (!buf) {
            fclose(f);
            return NULL;
        }
        if (*out_size > 0 && fread(buf, 1, *out_size, f) != *out_size) {
            free(buf);
            fclose(f);
            return NULL;
        }
        fclose(f);
        return buf;
    }

    return NULL;
}

/// @brief Scan a directory for *.vpa files and auto-mount them.
static void discover_packs(const char *dir) {
    if (!dir)
        return;

#ifdef _WIN32
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*.vpa", dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        if (g_asset_mgr.pack_count < RT_ASSET_MAX_PACKS) {
            vpa_archive_t *archive = vpa_open_file(path);
            if (archive) {
                char *path_copy = strdup(path);
                if (!path_copy) {
                    vpa_close(archive);
                    continue;
                }
                g_asset_mgr.packs[g_asset_mgr.pack_count] = archive;
                g_asset_mgr.pack_paths[g_asset_mgr.pack_count] = path_copy;
                g_asset_mgr.pack_count++;
            }
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        size_t nlen = strlen(entry->d_name);
        if (nlen < 5)
            continue;
        if (strcmp(entry->d_name + nlen - 4, ".vpa") != 0)
            continue;

        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

        // Verify it's a regular file
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;

        if (g_asset_mgr.pack_count < RT_ASSET_MAX_PACKS) {
            vpa_archive_t *archive = vpa_open_file(path);
            if (archive) {
                char *path_copy = strdup(path);
                if (!path_copy) {
                    vpa_close(archive);
                    continue;
                }
                g_asset_mgr.packs[g_asset_mgr.pack_count] = archive;
                g_asset_mgr.pack_paths[g_asset_mgr.pack_count] = path_copy;
                g_asset_mgr.pack_count++;
            }
        }
    }
    closedir(d);
#endif
}

/// @brief Ensure the asset manager is initialized (lazy init on first use).
static void ensure_init(void) {
    asset_lock();
    int initialized = g_asset_mgr.initialized;
    asset_unlock();
    if (!initialized)
        rt_asset_init(NULL, 0);
}

// ─── rt_asset_init ──────────────────────────────────────────────────────────

/// @brief Initialize the asset manager with an optional embedded VPA blob.
/// @details Parses the embedded blob (from linked .rodata or explicit argument),
///          then auto-discovers .vpa pack files next to the executable. On macOS,
///          also scans the .app bundle's Resources directory. Idempotent — safe
///          to call multiple times; only the first call has effect.
void rt_asset_init(const uint8_t *blob, uint64_t size) {
    asset_lock();
    if (g_asset_mgr.initialized) {
        asset_unlock();
        return;
    }
    g_asset_mgr.initialized = 1;

    // Parse embedded blob (explicit argument)
    if (blob && size >= 32) {
        g_asset_mgr.embedded = vpa_open_memory(blob, (size_t)size);
    }

    // Auto-discover embedded blob from linked asset .o file.
    // The AssetCompiler generates a C file with viper_asset_blob[] and
    // viper_asset_blob_size. When linked, these override the weak defaults below.
    if (!g_asset_mgr.embedded) {
        if (viper_asset_blob_size >= 32)
            g_asset_mgr.embedded = vpa_open_memory(viper_asset_blob, (size_t)viper_asset_blob_size);
    }

    // Auto-discover .vpa packs next to executable
    char *exe_dir = rt_path_exe_dir_cstr();
    if (exe_dir) {
        discover_packs(exe_dir);

#ifdef __APPLE__
        // Also check bundle Resources directory
        char res_dir[4096];
        snprintf(res_dir, sizeof(res_dir), "%s/../Resources", exe_dir);
        discover_packs(res_dir);
#endif
        free(exe_dir);
    }
    asset_unlock();
}

// ─── rt_asset_load ──────────────────────────────────────────────────────────

/// @brief Load an asset by name with automatic type dispatch based on file extension.
/// @details Searches embedded blob → mounted packs (LIFO) → filesystem. For known
///          extensions (.png, .wav, .json, etc.), decodes to the appropriate runtime
///          type (Pixels, Sound, Map). Unknown extensions return raw Bytes.
void *rt_asset_load(rt_string name) {
    if (!name)
        return NULL;
    ensure_init();

    const char *cname = asset_name_cstr(name);
    if (!cname || *cname == '\0')
        return NULL;
    size_t data_size = 0;
    asset_lock();
    uint8_t *data = asset_find_data(cname, &data_size);
    asset_unlock();
    if (!data)
        return NULL;

    // Type dispatch by extension — try typed decode first, fall back to Bytes.
    void *result = rt_asset_decode_typed(cname, data, data_size);
    if (result) {
        free(data);
        return result;
    }

    // Unknown extension: return as raw Bytes
    result = rt_bytes_from_raw(data, data_size);
    free(data);
    return result;
}

// ─── rt_asset_load_bytes ────────────────────────────────────────────────────

/// @brief Load an asset as raw Bytes (no type dispatch, always returns Bytes or NULL).
void *rt_asset_load_bytes(rt_string name) {
    if (!name)
        return NULL;
    ensure_init();

    const char *cname = asset_name_cstr(name);
    if (!cname || *cname == '\0')
        return NULL;
    size_t data_size = 0;
    asset_lock();
    uint8_t *data = asset_find_data(cname, &data_size);
    asset_unlock();
    if (!data)
        return NULL;

    void *result = rt_bytes_from_raw(data, data_size);
    free(data);
    return result;
}

// ─── rt_asset_exists ────────────────────────────────────────────────────────

/// @brief Check whether an asset exists in any source (embedded, packs, or filesystem).
int64_t rt_asset_exists(rt_string name) {
    if (!name)
        return 0;
    ensure_init();

    const char *cname = asset_name_cstr(name);
    if (!cname || *cname == '\0')
        return 0;

    asset_lock();
    // Check embedded
    if (g_asset_mgr.embedded && vpa_find(g_asset_mgr.embedded, cname)) {
        asset_unlock();
        return 1;
    }

    // Check mounted packs
    for (int i = g_asset_mgr.pack_count - 1; i >= 0; --i) {
        if (g_asset_mgr.packs[i] && vpa_find(g_asset_mgr.packs[i], cname)) {
            asset_unlock();
            return 1;
        }
    }

    // Check filesystem
    if (asset_regular_file_size(cname, NULL)) {
        asset_unlock();
        return 1;
    }

    asset_unlock();
    return 0;
}

// ─── rt_asset_size ──────────────────────────────────────────────────────────

/// @brief Get the byte size of an asset without loading it.
int64_t rt_asset_size(rt_string name) {
    if (!name)
        return 0;
    ensure_init();

    const char *cname = asset_name_cstr(name);
    if (!cname || *cname == '\0')
        return 0;

    asset_lock();
    // Check embedded
    if (g_asset_mgr.embedded) {
        const vpa_entry_t *e = vpa_find(g_asset_mgr.embedded, cname);
        if (e) {
            asset_unlock();
            return (int64_t)e->data_size;
        }
    }

    // Check mounted packs
    for (int i = g_asset_mgr.pack_count - 1; i >= 0; --i) {
        if (!g_asset_mgr.packs[i])
            continue;
        const vpa_entry_t *e = vpa_find(g_asset_mgr.packs[i], cname);
        if (e) {
            asset_unlock();
            return (int64_t)e->data_size;
        }
    }

    // Check filesystem
    uint64_t fs_size = 0;
    if (asset_regular_file_size(cname, &fs_size) && fs_size <= INT64_MAX) {
        asset_unlock();
        return (int64_t)fs_size;
    }

    asset_unlock();
    return 0;
}

// ─── rt_asset_list ──────────────────────────────────────────────────────────

/// @brief List all available asset names from embedded and mounted sources as a sequence.
void *rt_asset_list(void) {
    ensure_init();

    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);

    asset_lock();
    // Add embedded asset names
    if (g_asset_mgr.embedded) {
        for (uint32_t i = 0; i < g_asset_mgr.embedded->count; i++) {
            const char *n = g_asset_mgr.embedded->entries[i].name;
            rt_string s = rt_string_from_bytes(n, strlen(n));
            rt_seq_push(seq, (void *)s);
            rt_string_unref(s);
        }
    }

    // Add mounted pack asset names
    for (int p = 0; p < g_asset_mgr.pack_count; p++) {
        if (!g_asset_mgr.packs[p])
            continue;
        for (uint32_t i = 0; i < g_asset_mgr.packs[p]->count; i++) {
            const char *n = g_asset_mgr.packs[p]->entries[i].name;
            rt_string s = rt_string_from_bytes(n, strlen(n));
            rt_seq_push(seq, (void *)s);
            rt_string_unref(s);
        }
    }

    asset_unlock();
    return seq;
}

// ─── rt_asset_mount ─────────────────────────────────────────────────────────

/// @brief Mount an additional VPA pack file at runtime (assets become available immediately).
int64_t rt_asset_mount(rt_string path) {
    if (!path)
        return 0;
    ensure_init();

    const char *cpath = NULL;
    if (!rt_file_path_from_vstr((const ViperString *)path, &cpath) || !cpath || *cpath == '\0')
        return 0;
    vpa_archive_t *archive = vpa_open_file(cpath);
    if (!archive)
        return 0;

    char *path_copy = asset_canonical_path_dup(cpath);
    if (!path_copy) {
        vpa_close(archive);
        return 0;
    }

    asset_lock();
    if (g_asset_mgr.pack_count >= RT_ASSET_MAX_PACKS) {
        asset_unlock();
        free(path_copy);
        vpa_close(archive);
        return 0;
    }
    g_asset_mgr.packs[g_asset_mgr.pack_count] = archive;
    g_asset_mgr.pack_paths[g_asset_mgr.pack_count] = path_copy;
    g_asset_mgr.pack_count++;
    asset_unlock();
    return 1;
}

// ─── rt_asset_unmount ───────────────────────────────────────────────────────

/// @brief Unmount a previously-mounted VPA pack file.
int64_t rt_asset_unmount(rt_string path) {
    if (!path)
        return 0;
    ensure_init();

    const char *cpath = NULL;
    if (!rt_file_path_from_vstr((const ViperString *)path, &cpath) || !cpath || *cpath == '\0')
        return 0;
    char *search_path = asset_canonical_path_dup(cpath);
    if (!search_path)
        return 0;

    asset_lock();
    int match = -1;
    for (int i = g_asset_mgr.pack_count - 1; i >= 0; --i) {
        if (g_asset_mgr.pack_paths[i] && asset_path_equal(g_asset_mgr.pack_paths[i], search_path)) {
            match = i;
            break;
        }
    }
    if (match < 0) {
        const char *search_name = asset_basename(search_path);
        int basename_matches = 0;
        for (int i = g_asset_mgr.pack_count - 1; i >= 0; --i) {
            if (g_asset_mgr.pack_paths[i] &&
                asset_path_equal(asset_basename(g_asset_mgr.pack_paths[i]), search_name)) {
                match = i;
                basename_matches++;
            }
        }
        if (basename_matches != 1)
            match = -1;
    }
    if (match >= 0) {
        vpa_close(g_asset_mgr.packs[match]);
        free(g_asset_mgr.pack_paths[match]);

        // Shift remaining packs down
        for (int j = match; j < g_asset_mgr.pack_count - 1; j++) {
            g_asset_mgr.packs[j] = g_asset_mgr.packs[j + 1];
            g_asset_mgr.pack_paths[j] = g_asset_mgr.pack_paths[j + 1];
        }
        g_asset_mgr.pack_count--;
        g_asset_mgr.packs[g_asset_mgr.pack_count] = NULL;
        g_asset_mgr.pack_paths[g_asset_mgr.pack_count] = NULL;
        asset_unlock();
        free(search_path);
        return 1;
    }

    asset_unlock();
    free(search_path);
    return 0;
}
