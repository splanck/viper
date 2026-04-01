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
#include "rt_vpa_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#endif

// ─── External declarations ──────────────────────────────────────────────────

extern void rt_trap(const char *msg);
extern rt_string rt_string_from_bytes(const char *data, size_t len);
extern const char *rt_string_cstr(rt_string s);
extern size_t rt_string_len(rt_string s);
extern rt_string rt_const_cstr(const char *s);
extern void *rt_bytes_from_raw(const uint8_t *data, size_t len);

// Type-dispatched decoder (rt_asset_decode.c)
extern void *rt_asset_decode_typed(const char *name, const uint8_t *data, size_t size);

// seq<str>
extern void *rt_seq_new(void);
extern void rt_seq_push(void *seq, void *val);

// Exe directory detection
extern char *rt_path_exe_dir_cstr(void);

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

static struct {
    vpa_archive_t *embedded;                   // From .rodata blob (NULL if none)
    vpa_archive_t *packs[RT_ASSET_MAX_PACKS];  // Mounted pack files
    char *pack_paths[RT_ASSET_MAX_PACKS];      // Paths for unmount matching
    int pack_count;
    int initialized;
} g_asset_mgr;

// ─── Helpers ────────────────────────────────────────────────────────────────

/// @brief Find asset data across all sources. Returns malloc'd buffer.
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
    FILE *f = fopen(name, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        if (fsize < 0) {
            fclose(f);
            return NULL;
        }
        rewind(f);
        *out_size = (size_t)fsize;
        uint8_t *buf = (uint8_t *)malloc(*out_size);
        if (buf) {
            if (fread(buf, 1, *out_size, f) != *out_size) {
                free(buf);
                fclose(f);
                return NULL;
            }
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
                g_asset_mgr.packs[g_asset_mgr.pack_count] = archive;
                g_asset_mgr.pack_paths[g_asset_mgr.pack_count] = strdup(path);
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
                g_asset_mgr.packs[g_asset_mgr.pack_count] = archive;
                g_asset_mgr.pack_paths[g_asset_mgr.pack_count] = strdup(path);
                g_asset_mgr.pack_count++;
            }
        }
    }
    closedir(d);
#endif
}

/// @brief Ensure the asset manager is initialized (lazy init on first use).
static void ensure_init(void) {
    if (!g_asset_mgr.initialized)
        rt_asset_init(NULL, 0);
}

// ─── rt_asset_init ──────────────────────────────────────────────────────────

void rt_asset_init(const uint8_t *blob, uint64_t size) {
    if (g_asset_mgr.initialized)
        return;
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
            g_asset_mgr.embedded =
                vpa_open_memory(viper_asset_blob, (size_t)viper_asset_blob_size);
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
}

// ─── rt_asset_load ──────────────────────────────────────────────────────────

void *rt_asset_load(rt_string name) {
    if (!name)
        return NULL;
    ensure_init();

    const char *cname = rt_string_cstr(name);
    size_t data_size = 0;
    uint8_t *data = asset_find_data(cname, &data_size);
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

void *rt_asset_load_bytes(rt_string name) {
    if (!name)
        return NULL;
    ensure_init();

    const char *cname = rt_string_cstr(name);
    size_t data_size = 0;
    uint8_t *data = asset_find_data(cname, &data_size);
    if (!data)
        return NULL;

    void *result = rt_bytes_from_raw(data, data_size);
    free(data);
    return result;
}

// ─── rt_asset_exists ────────────────────────────────────────────────────────

int64_t rt_asset_exists(rt_string name) {
    if (!name)
        return 0;
    ensure_init();

    const char *cname = rt_string_cstr(name);

    // Check embedded
    if (g_asset_mgr.embedded && vpa_find(g_asset_mgr.embedded, cname))
        return 1;

    // Check mounted packs
    for (int i = g_asset_mgr.pack_count - 1; i >= 0; --i) {
        if (g_asset_mgr.packs[i] && vpa_find(g_asset_mgr.packs[i], cname))
            return 1;
    }

    // Check filesystem
    FILE *f = fopen(cname, "rb");
    if (f) {
        fclose(f);
        return 1;
    }

    return 0;
}

// ─── rt_asset_size ──────────────────────────────────────────────────────────

int64_t rt_asset_size(rt_string name) {
    if (!name)
        return 0;
    ensure_init();

    const char *cname = rt_string_cstr(name);

    // Check embedded
    if (g_asset_mgr.embedded) {
        const vpa_entry_t *e = vpa_find(g_asset_mgr.embedded, cname);
        if (e)
            return (int64_t)e->data_size;
    }

    // Check mounted packs
    for (int i = g_asset_mgr.pack_count - 1; i >= 0; --i) {
        if (!g_asset_mgr.packs[i])
            continue;
        const vpa_entry_t *e = vpa_find(g_asset_mgr.packs[i], cname);
        if (e)
            return (int64_t)e->data_size;
    }

    // Check filesystem
    FILE *f = fopen(cname, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fclose(f);
        return (int64_t)(sz >= 0 ? sz : 0);
    }

    return 0;
}

// ─── rt_asset_list ──────────────────────────────────────────────────────────

void *rt_asset_list(void) {
    ensure_init();

    void *seq = rt_seq_new();

    // Add embedded asset names
    if (g_asset_mgr.embedded) {
        for (uint32_t i = 0; i < g_asset_mgr.embedded->count; i++) {
            const char *n = g_asset_mgr.embedded->entries[i].name;
            rt_string s = rt_string_from_bytes(n, strlen(n));
            rt_seq_push(seq, (void *)s);
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
        }
    }

    return seq;
}

// ─── rt_asset_mount ─────────────────────────────────────────────────────────

int64_t rt_asset_mount(rt_string path) {
    if (!path)
        return 0;
    ensure_init();

    if (g_asset_mgr.pack_count >= RT_ASSET_MAX_PACKS)
        return 0;

    const char *cpath = rt_string_cstr(path);
    vpa_archive_t *archive = vpa_open_file(cpath);
    if (!archive)
        return 0;

    g_asset_mgr.packs[g_asset_mgr.pack_count] = archive;
    g_asset_mgr.pack_paths[g_asset_mgr.pack_count] = strdup(cpath);
    g_asset_mgr.pack_count++;
    return 1;
}

// ─── rt_asset_unmount ───────────────────────────────────────────────────────

int64_t rt_asset_unmount(rt_string path) {
    if (!path)
        return 0;
    ensure_init();

    const char *cpath = rt_string_cstr(path);

    // Find the pack by path (search from end for LIFO behavior)
    for (int i = g_asset_mgr.pack_count - 1; i >= 0; --i) {
        if (!g_asset_mgr.pack_paths[i])
            continue;

        // Match by filename (not full path) for convenience
        const char *pack_name = strrchr(g_asset_mgr.pack_paths[i], '/');
        if (!pack_name)
            pack_name = strrchr(g_asset_mgr.pack_paths[i], '\\');
        if (pack_name)
            pack_name++;
        else
            pack_name = g_asset_mgr.pack_paths[i];

        const char *search_name = strrchr(cpath, '/');
        if (!search_name)
            search_name = strrchr(cpath, '\\');
        if (search_name)
            search_name++;
        else
            search_name = cpath;

        if (strcmp(pack_name, search_name) == 0) {
            vpa_close(g_asset_mgr.packs[i]);
            free(g_asset_mgr.pack_paths[i]);

            // Shift remaining packs down
            for (int j = i; j < g_asset_mgr.pack_count - 1; j++) {
                g_asset_mgr.packs[j] = g_asset_mgr.packs[j + 1];
                g_asset_mgr.pack_paths[j] = g_asset_mgr.pack_paths[j + 1];
            }
            g_asset_mgr.pack_count--;
            g_asset_mgr.packs[g_asset_mgr.pack_count] = NULL;
            g_asset_mgr.pack_paths[g_asset_mgr.pack_count] = NULL;
            return 1;
        }
    }

    return 0;
}
