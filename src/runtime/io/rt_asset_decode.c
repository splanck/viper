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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <strings.h>
#include <unistd.h>
#endif

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

// ─── Helpers ────────────────────────────────────────────────────────────────

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

/// @brief Write data to a temp file, call a file-based loader, delete temp.
static void *load_via_tempfile(const uint8_t *data,
                               size_t size,
                               const char *ext,
                               void *(*loader)(void *path_str)) {
    // Build temp path
    char tmppath[512];
#ifdef _WIN32
    char tmpdir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpdir);
    snprintf(tmppath, sizeof(tmppath), "%sviper_asset_%d%s", tmpdir, _getpid(), ext);
#else
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir)
        tmpdir = "/tmp";
    snprintf(tmppath, sizeof(tmppath), "%s/viper_asset_%d%s", tmpdir, (int)getpid(), ext);
#endif

    // Write temp file
    FILE *f = fopen(tmppath, "wb");
    if (!f)
        return NULL;
    if (size > 0 && fwrite(data, 1, size, f) != size) {
        fclose(f);
        remove(tmppath);
        return NULL;
    }
    fclose(f);

    // Call file-based loader
    rt_string path_str = rt_string_from_bytes(tmppath, strlen(tmppath));
    void *result = loader((void *)path_str);

    // Cleanup
    remove(tmppath);
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
